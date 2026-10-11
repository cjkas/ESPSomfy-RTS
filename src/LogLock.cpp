// Prints each log line whole.
//
// The Arduino core's log_printf() writes with ets_printf() and has its UART
// lock commented out, so lines logged at the same time from async_tcp (core 0)
// and loop() (core 1) come out interleaved character by character.  It also
// formats short lines into one static buffer shared by every task.
//
// platformio.ini links with -Wl,--wrap=log_printf so every ESP_LOGx/log_x call
// lands here and takes this lock around the core's log_printfv().
#include <Arduino.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Defined in the core's esp32-hal-uart.c but not declared in a header.
extern "C" int log_printfv(const char *format, va_list arg);

// Created during static initialisation, before anything can race for it.  A
// constructor that logs before this runs simply goes unlocked.
static SemaphoreHandle_t logMutex = xSemaphoreCreateRecursiveMutex();
// Never wait longer than this for the lock; the line is printed anyway, so a
// task stuck mid-log can only garble output, not hang another task.
#define LOG_LOCK_WAIT_MS 50

extern "C" int __wrap_log_printf(const char *format, ...) {
  // No blocking before the scheduler runs, in an ISR or in a critical section.
  const bool canLock = logMutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && xPortCanYield();
  const bool locked = canLock && xSemaphoreTakeRecursive(logMutex, pdMS_TO_TICKS(LOG_LOCK_WAIT_MS)) == pdTRUE;
  va_list arg;
  va_start(arg, format);
  int len = log_printfv(format, arg);
  va_end(arg);
  if(locked) xSemaphoreGiveRecursive(logMutex);
  return len;
}
