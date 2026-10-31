#!/usr/bin/env bash
# Writes a release's version into the sources: bash .github/set_version.sh v4.0.1
# The build jobs build with it, the release job commits it.
set -euo pipefail

VERSION="${1#v}"
# The version goes into sed replacements and into the firmware, so only
# digits, letters, dots and dashes: 4.0.1, 4.0.0.beta, 4.1.0-rc1.
if [[ ! "$VERSION" =~ ^[0-9]+(\.[0-9A-Za-z-]+)*$ ]]; then
  echo "Not a version: $1" >&2
  exit 1
fi

sed -i "s/#define FW_VERSION \"v[0-9.]*\"/#define FW_VERSION \"v${VERSION}\"/" src/ConfigSettings.h
sed -i "s/^[0-9.].*/${VERSION}/" data-src/appversion
sed -i "s/\?v=[0-9.]*c/?v=${VERSION}c/g" data-src/index.html
sed -i "s/appVersion = 'v[0-9.]*'/appVersion = 'v${VERSION}'/" data-src/index.js
