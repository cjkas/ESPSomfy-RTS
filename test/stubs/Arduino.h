// Minimal stand-in for the Arduino core so firmware sources can be built for
// the native test env (pio test -e native).  Only add what tested code uses.
#ifndef arduino_stub_h
#define arduino_stub_h
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Fixed so anything formatted from it is deterministic.
inline unsigned long millis() { return 0; }
inline bool getLocalTime(struct tm *info, uint32_t ms = 5000) {
  (void)ms;
  time_t now = time(nullptr);
  return localtime_r(&now, info) != nullptr;
}
#endif
