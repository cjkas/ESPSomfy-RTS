// Exercise the actual radio setup, RX and queue-admission methods, replacing
// only hardware/RTOS boundaries. The script extracts those definitions from
// Transceiver.cpp so the test cannot silently exercise a copy of the logic.
#include <assert.h>
#include <new>
#include <initializer_list>
#include <stdio.h>
#include "Transceiver.h"
#include "SPI.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

TestSerial Serial;
TestESP ESP;
TestSPI SPI;
void TestSPI::begin(int8_t, int8_t, int8_t, int8_t) { active = true; }
void TestSPI::end() { active = false; }
static int levels[49] = {};
static bool stuckMiso;
static unsigned long clockMs;
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) { levels[pin] = value; }
int digitalRead(uint8_t pin) { return pin == 10 && stuckMiso ? HIGH : levels[pin]; }
int gpio_set_level(gpio_num_t pin, uint32_t value) { levels[pin] = value; return 0; }
unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { clockMs += ms; }
static unsigned int attached;
static unsigned int rxStarts;
static unsigned int initializations;
static unsigned int saves;
static bool observedReadyDuringRx;
static uint32_t queuedDuringRx;
int digitalPinToInterrupt(uint8_t pin) { return pin; }
constexpr int CHANGE = 1;
void attachInterrupt(int, void (*)(), int) { attached++; }
void detachInterrupt(int) { attached = 0; }
static const char *TAG = "Somfy";
static uint8_t rxmode;
static int interruptPin;
uint8_t radioBitLength;
bool testRadioInit = true;
struct { Transceiver transceiver; } somfy;
struct RadioLock { RadioLock(int = 0, bool = false) {} };
struct TxQueueLock { ~TxQueueLock() {} };
constexpr int portMAX_DELAY = 0;
static bool scanEmitPending;
static void *txTaskHandle = reinterpret_cast<void *>(1);
void xTaskNotifyGive(void *) {}
void Transceiver::handleReceive() {}
void Transceiver::resumeTx(uint8_t) {}
static void warnBootPin(const char *, uint8_t) {}
void transceiver_config_t::save() {
  saves++;
  if(BoardProfile::integratedRadio)
    assert(SCKPin == 11 && MISOPin == 10 && MOSIPin == 9 && CSNPin == 12 && TXPin == 3 && RXPin == 38);
}

ELECHOUSE_CC1101 ELECHOUSE_cc1101;
static bool detected;
static uint8_t part, version, gdo0, gdo2, packet, modem, rxState;
static uint32_t frequencyWord;
void ELECHOUSE_CC1101::Init() { initializations++; }
bool ELECHOUSE_CC1101::getCC1101() { return detected; }
byte ELECHOUSE_CC1101::SpiReadStatus(byte addr) {
  if(addr == CC1101_PARTNUM) return part;
  if(addr == CC1101_VERSION) return version;
  if(addr == CC1101_MARCSTATE) return rxState;
  assert(false); return 0;
}
byte ELECHOUSE_CC1101::SpiReadReg(byte addr) {
  if(addr == CC1101_IOCFG0) return gdo0;
  if(addr == CC1101_IOCFG2) return gdo2;
  if(addr == CC1101_PKTCTRL0) return packet;
  if(addr == CC1101_MDMCFG2) return modem;
  if(addr == CC1101_FREQ2) return frequencyWord >> 16;
  if(addr == CC1101_FREQ1) return frequencyWord >> 8;
  if(addr == CC1101_FREQ0) return frequencyWord;
  assert(false); return 0;
}
void ELECHOUSE_CC1101::setSpiPin(byte sck, byte miso, byte mosi, byte cs) {
  if(BoardProfile::integratedRadio) assert(sck == 11 && miso == 10 && mosi == 9 && cs == 12);
}
void ELECHOUSE_CC1101::SetRx() {
  rxStarts++;
  observedReadyDuringRx = somfy.transceiver.available();
  somfy_tx_job_t job;
  queuedDuringRx = somfy.transceiver.submitTx(job);
}
void ELECHOUSE_CC1101::setSidle() {}
void ELECHOUSE_CC1101::setGDO(byte, byte) {}
void ELECHOUSE_CC1101::setGDO0(byte) {}
#define NOOP(name, type) void ELECHOUSE_CC1101::name(type) {}
NOOP(setCCMode, bool) NOOP(setMHZ, float) NOOP(setRxBW, float)
NOOP(setDeviation, float) NOOP(setPA, int) NOOP(setModulation, byte)
NOOP(setManchester, bool) NOOP(setPktFormat, byte) NOOP(setDcFilterOff, bool)
NOOP(setCrc, bool) NOOP(setCRC_AF, bool) NOOP(setSyncMode, byte) NOOP(setAdrChk, byte)

#include "radio-setup-methods.inc"

static void reset() {
  somfy.transceiver.~Transceiver();
  new (&somfy.transceiver) Transceiver;
  detected = true; part = 0; version = 0x14;
  gdo0 = gdo2 = 0x0D; packet = 0x32; modem = 0x3C;
  frequencyWord = 0x10AB85; rxState = 0x0D;
  attached = rxStarts = initializations = saves = 0;
  observedReadyDuringRx = false; queuedDuringRx = 0;
  testRadioInit = true; stuckMiso = false; rxmode = 0; clockMs = 0;
}
static void assertUnavailable() {
  auto &radio = somfy.transceiver;
  assert(!radio.available() && !radio.config.radioInit && attached == 0);
  somfy_tx_job_t job;
  assert(radio.submitTx(job) == 0);
  radio.enableReceive();
  assert(attached == 0);
}
int main() {
  reset(); auto &radio = somfy.transceiver;
  if(BoardProfile::integratedRadio) {
    radio.config.SCKPin = radio.config.TXPin = radio.config.RXPin = 16;
    radio.config.apply();
    assert(radio.available() && radio.config.radioInit && attached == 1);
    assert(!observedReadyDuringRx && queuedDuringRx == 0);
    assert(radio.config.TXPin == 3 && radio.config.RXPin == 38 && radio.config.SCKPin == 11);
    somfy_tx_job_t job; assert(radio.submitTx(job) != 0);
    stuckMiso = true; radio.config.apply(); assertUnavailable();
    stuckMiso = false; radio.config.apply(); assert(radio.available());
    transceiver_config_t next = radio.config;
    next.enabled = false;
    next.SCKPin = next.MOSIPin = next.MISOPin = next.CSNPin = next.TXPin = next.RXPin = 16;
    radio.setConfig(next); assertUnavailable(); assert(saves == 1);
    for(uint8_t pin : {uint8_t(3), uint8_t(12), uint8_t(19), uint8_t(33), uint8_t(48)}) assert(radio.usesPin(pin));
    assert(!radio.usesPin(16));
    reset(); stuckMiso = true; radio.config.apply(); assertUnavailable(); assert(initializations == 0);
    reset(); testRadioInit = false; radio.config.apply(); assertUnavailable(); assert(initializations == 0);
    reset(); detected = false; radio.config.apply(); assertUnavailable(); assert(rxStarts == 0);
    reset(); part = 1; radio.config.apply(); assertUnavailable();
    reset(); version = 0; radio.config.apply(); assertUnavailable();
    reset(); version = 0xFF; radio.config.apply(); assertUnavailable(); assert(rxStarts == 0);
    reset(); gdo0 = 0; radio.config.apply(); assertUnavailable();
    reset(); gdo2 = 0; radio.config.apply(); assertUnavailable();
    reset(); packet = 0; radio.config.apply(); assertUnavailable();
    reset(); modem = 0; radio.config.apply(); assertUnavailable();
    reset(); frequencyWord = 0; radio.config.apply(); assertUnavailable();
    reset(); rxState = 0; radio.config.apply(); assertUnavailable(); assert(rxStarts == 1 && millis() <= 25);
    rxState = 0x0D; radio.config.apply(); assert(radio.available() && attached == 1);
  } else {
    // Generic radios retain upstream's enabled-based availability and queue
    // admission behavior, including when disabled or detection fails.
    radio.config.apply();
    somfy_tx_job_t job; assert(radio.submitTx(job) != 0);
    radio.config.enabled = true; detected = false; radio.config.apply();
    assert(radio.available() && !radio.config.radioInit);
    radio.config.apply(); // Still no Plus-specific register gating.
    detected = true; rxState = 0; radio.config.apply();
    assert(radio.available() && radio.config.radioInit && attached == 1);
    assert(observedReadyDuringRx && queuedDuringRx != 0);
  }
  puts("Radio readiness and RX/queue gating checks passed");
}
