// Arduino SPI stand-in for the native test env: remembers whether the bus is
// up and logs begin/end in order with the pin calls (see Arduino.h).
#pragma once
#include <Arduino.h>
struct StubSPI {
  bool active = false;
  unsigned int begins = 0;
  unsigned int ends = 0;
  int8_t sck = -1, miso = -1, mosi = -1, cs = -1;
  void begin(int8_t s, int8_t mi, int8_t mo, int8_t c) {
    active = true; begins++; sck = s; miso = mi; mosi = mo; cs = c;
    stubGpioLog.push_back({StubGpioEvent::SpiBegin, 0, 0});
  }
  void end() {
    active = false; ends++;
    stubGpioLog.push_back({StubGpioEvent::SpiEnd, 0, 0});
  }
};
inline StubSPI SPI;
