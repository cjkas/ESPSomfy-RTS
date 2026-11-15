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
#include <string>
#include <vector>
#include <type_traits>

typedef uint8_t byte;

// Starts at 0 and only moves when a test calls delay() or sets it, so anything
// formatted from it is deterministic.
inline unsigned long stubMillis = 0;
inline unsigned long millis() { return stubMillis; }
inline void delay(unsigned long ms) { stubMillis += ms; }

// A fake GPIO bank for board code.  Every pin call is also logged in order
// so tests can check the startup sequence, not only the final levels.
constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT = 0x01;
constexpr int OUTPUT = 0x03;
struct StubGpioEvent {
  enum Kind : char { Mode = 'M', Level = 'L', Write = 'W', SpiBegin = 'B', SpiEnd = 'E' };
  Kind kind;
  uint8_t pin;
  int value;
};
inline std::vector<StubGpioEvent> stubGpioLog;
inline int stubPinLevel[64] = {};
inline int stubPinMode[64] = {};
inline int stubPinReadOverride[64] = {}; // -1 for none, set by stubGpioReset()
inline void stubGpioReset() {
  stubGpioLog.clear();
  for(int i = 0; i < 64; i++) { stubPinLevel[i] = 0; stubPinMode[i] = 0; stubPinReadOverride[i] = -1; }
  stubMillis = 0;
}
// The ESP-IDF call that sets the output latch whatever the pin mode is.
inline void stubSetLatch(uint8_t pin, int level) {
  stubPinLevel[pin] = level;
  stubGpioLog.push_back({StubGpioEvent::Level, pin, level});
}
inline void pinMode(uint8_t pin, uint8_t mode) {
  stubPinMode[pin] = mode;
  stubGpioLog.push_back({StubGpioEvent::Mode, pin, mode});
}
// Arduino 3 only accepts writes once pinMode has made the pin an output.
inline void digitalWrite(uint8_t pin, uint8_t level) {
  if(stubPinMode[pin] != OUTPUT) return;
  stubPinLevel[pin] = level;
  stubGpioLog.push_back({StubGpioEvent::Write, pin, level});
}
inline int digitalRead(uint8_t pin) {
  return stubPinReadOverride[pin] >= 0 ? stubPinReadOverride[pin] : stubPinLevel[pin];
}

// Serial output is discarded.
struct StubSerial {
  template <typename... Args> void printf(const char *, Args...) {}
  void println(const char *) {}
};
inline StubSerial Serial;

// Tests that measure time set this.
inline unsigned long stubMicros = 0;
inline unsigned long micros() { return stubMicros; }
inline bool getLocalTime(struct tm *info, uint32_t ms = 5000) {
  (void)ms;
  time_t now = time(nullptr);
  return localtime_r(&now, info) != nullptr;
}

// The parts of Arduino's String the firmware code under test uses.
class String {
  std::string _s;
  public:
    String() {}
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}
    // Arduino formats integers in decimal.
    template<typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    explicit String(T val) : _s(std::to_string(val)) {}
    const char *c_str() const { return _s.c_str(); }
    unsigned int length() const { return (unsigned int)_s.length(); }
    bool equalsIgnoreCase(const String &other) const {
      if(_s.length() != other._s.length()) return false;
      for(size_t i = 0; i < _s.length(); i++)
        if(tolower((unsigned char)_s[i]) != tolower((unsigned char)other._s[i])) return false;
      return true;
    }
    bool startsWith(const String &prefix) const { return _s.compare(0, prefix._s.length(), prefix._s) == 0; }
    friend String operator+(const String &a, const String &b) { return String(a._s + b._s); }
    friend bool operator==(const String &a, const String &b) { return a._s == b._s; }
};
#endif
