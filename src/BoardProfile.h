#pragma once
#include <Arduino.h>

namespace BoardProfile {
#if defined(ESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS)
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "The LILYGO T-Embed CC1101 Plus profile requires an ESP32-S3 target"
#endif
constexpr bool integratedRadio = true;
#else
constexpr bool integratedRadio = false;
#endif

// LILYGO T-Embed CC1101 Plus integrated hardware; not the non-Plus board.
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
bool usesPin(uint8_t pin);
}
