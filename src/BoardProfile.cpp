#include "BoardProfile.h"
#include <SPI.h>
#if defined(ESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS)
#include <driver/gpio.h>
#endif

namespace BoardProfile {
#if defined(ESPSOMFY_LILYGO_T_EMBED_CC1101_PLUS)
static bool initialized = false;

static void output(uint8_t pin, uint8_t level) {
  // Arduino 3 rejects digitalWrite before GPIO ownership is established.
  // Preload the hardware latch so active-low selects do not pulse on enable.
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
  pinMode(pin, OUTPUT);
}

void begin() {
  if(initialized) return;
  // Deselect every shared-SPI slave before enabling its power rail.
  // https://github.com/Xinyuan-LilyGO/T-Embed-CC1101/blob/c36826e0f91311cb591c4545fccf91e254fc55dc/examples/cc1101_recv_irq/cc1101_recv_irq.ino
  output(displayCS, HIGH);
  output(sdCS, HIGH);
  output(nrfCS, HIGH);
  output(cs, HIGH);
  output(nrfCE, LOW);
  output(backlight, LOW);
  output(sw1, HIGH);
  output(sw0, HIGH);
  output(power, HIGH);
  delay(10); // Conservative rail settling; no peripheral drivers are started.
  initialized = true;
}

bool prepareRadio(float frequency) {
  begin();
  // This hardware profile selects only the 433 MHz RF filter path.
  if(!(frequency >= 387.0f && frequency <= 464.0f)) {
    Serial.println("Radio disabled: frequency is outside the selected 433 MHz band");
    return false;
  }
  // Arduino 3 owns these pads while SPI is active. Release that ownership
  // before the GPIO readiness probe, including when reapplying settings.
  SPI.end();
  pinMode(miso, INPUT);
  output(sck, LOW);
  output(mosi, LOW);
  digitalWrite(cs, LOW);
  unsigned long started = millis();
  while(digitalRead(miso) == HIGH && millis() - started < 20) delay(1);
  bool ready = digitalRead(miso) == LOW;
  digitalWrite(cs, HIGH);
  if(!ready) Serial.println("CC1101 not ready: MISO stayed HIGH; check power/shared SPI before retrying");
  if(ready) SPI.begin(sck, miso, mosi, cs);
  return ready;
}

bool usesPin(uint8_t pin) {
  // USB CDC and in-package flash/OPI PSRAM must never become relay outputs.
  return pin == 19 || pin == 20 || (pin >= 26 && pin <= 37) ||
    pin == power || pin == sck || pin == mosi || pin == miso ||
    pin == cs || pin == tx || pin == rx || pin == sw1 || pin == sw0 ||
    pin == displayCS || pin == sdCS || pin == nrfCS || pin == nrfCE || pin == backlight;
}
#else
void begin() {}
bool prepareRadio(float) { return true; }
bool usesPin(uint8_t) { return false; }
#endif
}
