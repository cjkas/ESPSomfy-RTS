// Prints each log line whole.
//
// The Arduino core's log_printfv() writes with ets_printf() and has its UART
// lock commented out, so lines logged at the same time from async_tcp (core 0)
// and loop() (core 1) come out interleaved character by character.  It also
// formats short lines into one static buffer shared by every task.
//
// The core already links with --wrap=log_printf, which sends every
// ESP_LOGx/log_x call to the ESP Insights hook in libesp_diagnostics, and that
// hook prints through log_printfv().  platformio.ini adds
// -Wl,--wrap=log_printfv so that call lands here and takes this lock.
//
// The lock is a binary semaphore, not a mutex.  A mutex passes its waiter's
// priority to the holder, so async_tcp waiting on loop() would lift loop()
// above the radio task and let it break into a frame on the air.
#include <Arduino.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

extern "C" int __real_log_printfv(const char *format, va_list arg);

static SemaphoreHandle_t createLogLock() {
  SemaphoreHandle_t sem = xSemaphoreCreateBinary();
  if(sem) xSemaphoreGive(sem);
  return sem;
}
// Created during static initialisation, before anything can race for it.  A
// constructor that logs before this runs simply goes unlocked.
static SemaphoreHandle_t logLock = createLogLock();
// The task holding the lock, so a log from inside a log doesn't wait on itself.
static volatile TaskHandle_t logOwner = nullptr;
// Never wait longer than this for the lock; the line is printed anyway, so a
// task stuck mid-log can only garble output, not hang another task.  A long
// line takes about 20ms at 115200 baud.
#define LOG_LOCK_WAIT_MS 20

extern "C" int __wrap_log_printfv(const char *format, va_list arg) {
  // No blocking before the scheduler runs, in an ISR or in a critical section.
  const bool canLock = logLock && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && xPortCanYield() &&
    logOwner != xTaskGetCurrentTaskHandle();
  const bool locked = canLock && xSemaphoreTake(logLock, pdMS_TO_TICKS(LOG_LOCK_WAIT_MS)) == pdTRUE;
  if(locked) logOwner = xTaskGetCurrentTaskHandle();
  int len = __real_log_printfv(format, arg);
  if(locked) {
    logOwner = nullptr;
    xSemaphoreGive(logLock);
  }
  return len;
}
