#include <assert.h>
#include <stdio.h>
#include "BoardProfile.h"
#include <driver/gpio.h>
#include "SomfyFrame.h"
#include "ShadeSettings.h"

// Substitute only the shade model/storage boundary. The GPIO methods below
// are compiled from the production ShadeGpio.cpp and observe real board policy.
#define SOMFY_H
enum class gpio_flags_t : byte { LowLevelTrigger = 0x01 };
struct SomfyRemote {
  void triggerGPIOs(somfy_frame_t &frame);
};
struct SomfyShade : SomfyRemote, ShadeSettings {
  radio_proto proto = radio_proto::GP_Relay;
  uint8_t gpioFlags = 0;
  int8_t gpioDir = 0;
  uint8_t gpioUp = 16, gpioDown = 17, gpioMy = 18;
  uint32_t gpioRelease = 0;
  struct State { int8_t direction = -1, tiltDirection = 0; float currentPos = 0; } position;
  const State &state() const { return position; }
  bool isToggle() { return isToggleShade(shadeType); }
  void setGPIOs();
  void triggerGPIOs(somfy_frame_t &frame);
  bool usesPin(uint8_t pin);
};
#include "ShadeGpio.cpp"

TestSerial Serial;
TestESP ESP;
static unsigned int writes = 0;
static int levels[49] = {};
int gpio_set_level(gpio_num_t pin, uint32_t value) { levels[pin] = value; return 0; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) { writes++; levels[pin] = value; }
int digitalRead(uint8_t pin) { return levels[pin]; }
unsigned long millis() { return 100; }
void delay(unsigned long) {}
// BoardProfile preflight is linked but this test never starts the hardware.
#include "SPI.h"
TestSPI SPI;
void TestSPI::begin(int8_t, int8_t, int8_t, int8_t) {}
void TestSPI::end() {}

int main() {
  SomfyShade shade;
  shade.setGPIOs();
  assert(writes == 2 && levels[16] == HIGH && levels[17] == LOW);
  somfy_frame_t frame;
  frame.cmd = somfy_commands::Down;
  frame.repeats = 2;
  shade.proto = radio_proto::GP_Remote;
  shade.triggerGPIOs(frame);
  assert(writes == 5 && levels[17] == HIGH);
  const unsigned int before = writes;
  // These assignments can arrive from an old backup with the radio disabled.
  // Test every reserved pad in each output slot, including USB/flash pads.
  for(uint8_t pin = 0; pin < 49; pin++) {
    if(!BoardProfile::usesPin(pin)) continue;
    for(uint8_t slot = 0; slot < 3; slot++) {
      shade.gpioUp = slot == 0 ? pin : 16;
      shade.gpioDown = slot == 1 ? pin : 17;
      shade.gpioMy = slot == 2 ? pin : 18;
      shade.proto = radio_proto::GP_Remote;
      shade.triggerGPIOs(frame);
      shade.setGPIOs();
      if(slot != 2) {
        shade.proto = radio_proto::GP_Relay;
        shade.setGPIOs();
      }
      assert(writes == before);
    }
  }
  puts("Reserved shade GPIO checks passed");
}
