// ESP-IDF gpio_set_level stand-in for the native test env; see Arduino.h.
#pragma once
#include <Arduino.h>
using gpio_num_t = int;
inline int gpio_set_level(gpio_num_t pin, uint32_t level) {
  stubSetLatch(static_cast<uint8_t>(pin), static_cast<int>(level));
  return 0;
}
