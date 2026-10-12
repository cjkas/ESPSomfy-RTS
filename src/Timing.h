#ifndef timing_h
#define timing_h
#include <Arduino.h>
#include "esp_log.h"

// Timings shorter than this are not logged.  Every line costs several ms on a
// 115200 baud console, and per-request timings would add dozens of lines to
// every page load on the task that serves the sockets.  Build with
// -DTIMING_LOG_MIN_MS=0 to see every timing.
#ifndef TIMING_LOG_MIN_MS
#define TIMING_LOG_MIN_MS 100
#endif
// Anything that takes at least this long is logged as a warning so the slow
// calls stand out from the routine timings.
#ifndef TIMING_SLOW_MS
#define TIMING_SLOW_MS 1000
#endif

// Measures elapsed time in microseconds.  micros() wraps every ~71 minutes but
// the unsigned subtraction keeps any span shorter than that correct.
class ElapsedTimer {
  uint32_t _start;
  public:
    ElapsedTimer() : _start(micros()) {}
    void restart() { _start = micros(); }
    uint32_t us() const { return micros() - _start; }
};

// Logs how long something took as milliseconds with microsecond precision.
static inline void logElapsed(const char *tag, const char *what, uint32_t us) {
  const unsigned long ms = us / 1000, frac = us % 1000;
  if(ms < TIMING_LOG_MIN_MS) return;
  if(ms >= TIMING_SLOW_MS) ESP_LOGW(tag, "%s took %lu.%03lums (slow)", what, ms, frac);
  else ESP_LOGI(tag, "%s took %lu.%03lums", what, ms, frac);
}
// For calls that run constantly, like loop(), where logging every pass would
// flood the console.  Only logs once the time reaches minMs.
static inline void logIfSlow(const char *tag, const char *what, uint32_t us, uint32_t minMs) {
  if(us >= minMs * 1000) logElapsed(tag, what, us);
}
#endif
