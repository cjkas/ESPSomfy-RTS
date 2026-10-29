#ifndef BOOT_PINS_H
#define BOOT_PINS_H
#include <stdint.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

// The strapping pins whose level at reset can keep the chip from starting
// the firmware.  The ROM reads them before any code runs, so a radio that
// drives one at power-up stops the board booting until EN is pressed (issue
// #34).  The strapping pins that only pick a download mode, quiet the boot
// log or set the SDIO timing aren't listed.  The web UI has the same pins in
// data-src/js/pins.js, and checks the same radio pins as
// transceiver_config_t::apply(): keep the three in step.
//
// Returns what the pin does at reset, or nullptr for a pin that can't stop
// the boot.
inline const char *bootPinEffect(uint8_t pin) {
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  if(pin == 0) return "low at reset starts the download mode";
  if(pin == 45) return "high at reset powers the flash at 1.8 V";
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
  if(pin == 9) return "low at reset starts the download mode";
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
  if(pin == 28) return "low at reset starts the download mode";
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  if(pin == 35) return "low at reset starts the download mode";
#else
  // The original ESP32.  MTDI picks the flash voltage unless the
  // VDD_SDIO efuses are burned.
  if(pin == 0) return "low at reset starts the download mode";
  if(pin == 12) return "high at reset powers the flash at 1.8 V";
#endif
  return nullptr;
}
#endif
