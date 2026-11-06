#pragma once
#include <stdint.h>

struct TestSPI {
  bool active = false;
  unsigned int starts = 0;
  unsigned int stops = 0;
  void begin(int8_t sck, int8_t miso, int8_t mosi, int8_t cs);
  void end();
};
extern TestSPI SPI;
