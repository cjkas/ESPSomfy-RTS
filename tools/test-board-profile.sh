#!/usr/bin/env bash
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
compiler="${CXX:-c++}"
common=(-std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -I"$repo/tests/board-profile" -I"$repo/src")
sources=("$repo/tests/board-profile/board-profile-test.cpp" "$repo/src/BoardProfile.cpp")
"$compiler" "${common[@]}" -DESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS -DCONFIG_IDF_TARGET_ESP32S3 \
  "${sources[@]}" -o "$scratch/lilygo-test"
"$scratch/lilygo-test"
"$compiler" "${common[@]}" "${sources[@]}" -o "$scratch/generic-test"
"$scratch/generic-test"
"$compiler" "${common[@]}" -DESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS -DCONFIG_IDF_TARGET_ESP32S3 \
  "$repo/tests/board-profile/shade-gpio-test.cpp" "$repo/src/BoardProfile.cpp" -o "$scratch/shade-gpio-test"
"$scratch/shade-gpio-test"
if "$compiler" "${common[@]}" -DESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS \
  "${sources[@]}" -o "$scratch/wrong-target" > "$scratch/wrong-target.log" 2>&1; then
  echo 'LILYGO profile incorrectly accepted a non-S3 target' >&2
  exit 1
fi
echo 'Non-S3 LILYGO build rejected as expected'

# Test production setup/RX/queue methods against register and readiness faults.
python3 "$repo/tests/board-profile/extract-radio-setup.py" "$repo/src/Transceiver.cpp" "$scratch/radio-setup-methods.inc"
for profile in generic lilygo; do
  flags=()
  if [[ "$profile" == lilygo ]]; then
    flags=(-DESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS -DCONFIG_IDF_TARGET_ESP32S3)
  fi
  "$compiler" "${common[@]}" -I"$scratch" -I"$repo/lib/SmartRC-CC1101-Driver-Lib" "${flags[@]}" \
    "$repo/tests/board-profile/radio-setup-test.cpp" "$repo/src/BoardProfile.cpp" \
    "$repo/src/RadioScheduler.cpp" -o "$scratch/radio-$profile"
  "$scratch/radio-$profile"
done
