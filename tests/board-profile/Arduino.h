#pragma once
#include <stdint.h>
#include <string.h>
using byte = uint8_t;
class String;

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT = 0x01;
constexpr int OUTPUT = 0x03;
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
unsigned long millis();
void delay(unsigned long duration);

struct TestSerial {
  template <typename... Args> void printf(const char *, Args...) {}
  void println(const char *) {}
};
extern TestSerial Serial;
struct TestESP {
  unsigned int getFlashChipSize() { return 16777216; }
  unsigned int getPsramSize() { return 8388608; }
};
extern TestESP ESP;
