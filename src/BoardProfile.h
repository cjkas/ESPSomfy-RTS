#pragma once
#include <Arduino.h>

namespace BoardProfile {
#if defined(ESPSOMFY_LILYGO_T_EMBED_CC1101)
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "The LILYGO T-Embed CC1101 / Plus profile requires an ESP32-S3 target"
#endif
constexpr bool integratedRadio = true;
#else
constexpr bool integratedRadio = false;
#endif

// LILYGO T-Embed CC1101 wiring, hardware-tested on the Plus model; the standard model is expected to match.
constexpr uint8_t power = 15;
constexpr uint8_t sck = 11;
constexpr uint8_t mosi = 9;
constexpr uint8_t miso = 10;
constexpr uint8_t cs = 12;
constexpr uint8_t tx = 3;
constexpr uint8_t rx = 38;
constexpr uint8_t sw1 = 47;
constexpr uint8_t sw0 = 48;
constexpr uint8_t displayCS = 41;
constexpr uint8_t sdCS = 13;
constexpr uint8_t nrfCS = 44;
constexpr uint8_t nrfCE = 43;
constexpr uint8_t backlight = 21;

void begin();
bool prepareRadio(float frequency);

// Whether the board's own hardware uses this pad.  Pure, so it is unit tested
// on the PC (pio test -e native).
#if defined(ESPSOMFY_LILYGO_T_EMBED_CC1101)
constexpr bool usesPin(uint8_t pin) {
  // USB CDC and in-package flash/OPI PSRAM must never become relay outputs.
  return pin == 19 || pin == 20 || (pin >= 26 && pin <= 37) ||
    pin == power || pin == sck || pin == mosi || pin == miso ||
    pin == cs || pin == tx || pin == rx || pin == sw1 || pin == sw0 ||
    pin == displayCS || pin == sdCS || pin == nrfCS || pin == nrfCE || pin == backlight;
}
#else
constexpr bool usesPin(uint8_t) { return false; }
#endif
}
