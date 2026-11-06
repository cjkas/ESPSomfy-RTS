#pragma once
#include <stdint.h>
using gpio_num_t = int;
int gpio_set_level(gpio_num_t pin, uint32_t value);
