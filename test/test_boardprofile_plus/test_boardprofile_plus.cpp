// The LILYGO T-Embed CC1101 profile: which pads the board owns, the hardware
// startup order and the radio bring-up.  BoardProfile.cpp is included so it
// is built for this board whatever the env's flags are.
#include <unity.h>
#include <cmath>
#define ESPSOMFY_LILYGO_T_EMBED_CC1101 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "BoardProfile.cpp"
#include "ShadeGpioGuard.h"

using E = StubGpioEvent;
static const uint8_t RESERVED[] = {3, 9, 10, 11, 12, 13, 15, 21, 38, 41, 43, 44, 47, 48};

void setUp() {
  // begin() runs once per boot, so what it set up stays between tests.
  int modes[64];
  memcpy(modes, stubPinMode, sizeof(modes));
  stubGpioReset();
  memcpy(stubPinMode, modes, sizeof(modes));
  SPI = StubSPI();
}
void tearDown() {}

static unsigned countEvents(E::Kind kind, uint8_t pin, int value) {
  unsigned n = 0;
  for(const auto &e : stubGpioLog) if(e.kind == kind && e.pin == pin && e.value == value) n++;
  return n;
}
static int firstEvent(E::Kind kind, uint8_t pin) {
  for(size_t i = 0; i < stubGpioLog.size(); i++)
    if(stubGpioLog[i].kind == kind && stubGpioLog[i].pin == pin) return (int)i;
  return -1;
}

// --- reserved pads ---

static void test_profile_has_an_integrated_radio() {
  TEST_ASSERT_TRUE(BoardProfile::integratedRadio);
}
static void test_the_wiring_is_the_lilygo_one() {
  TEST_ASSERT_EQUAL(15, BoardProfile::power);
  TEST_ASSERT_EQUAL(11, BoardProfile::sck);
  TEST_ASSERT_EQUAL(9, BoardProfile::mosi);
  TEST_ASSERT_EQUAL(10, BoardProfile::miso);
  TEST_ASSERT_EQUAL(12, BoardProfile::cs);
  TEST_ASSERT_EQUAL(3, BoardProfile::tx);
  TEST_ASSERT_EQUAL(38, BoardProfile::rx);
}
static void test_every_board_pad_is_reserved() {
  for(uint8_t pin : RESERVED) TEST_ASSERT_TRUE_MESSAGE(BoardProfile::usesPin(pin), "pad not reserved");
}
static void test_usb_and_flash_pads_are_reserved() {
  TEST_ASSERT_TRUE(BoardProfile::usesPin(19));
  TEST_ASSERT_TRUE(BoardProfile::usesPin(20));
  for(uint8_t pin = 26; pin <= 37; pin++) TEST_ASSERT_TRUE(BoardProfile::usesPin(pin));
}
static void test_other_pads_are_free() {
  TEST_ASSERT_FALSE(BoardProfile::usesPin(0));
  TEST_ASSERT_FALSE(BoardProfile::usesPin(16));
  TEST_ASSERT_FALSE(BoardProfile::usesPin(18));
  TEST_ASSERT_FALSE(BoardProfile::usesPin(25));
  TEST_ASSERT_FALSE(BoardProfile::usesPin(39));
  TEST_ASSERT_FALSE(BoardProfile::usesPin(46));
  TEST_ASSERT_FALSE(BoardProfile::usesPin(255));
}

// --- shade outputs on board pads (a backup may carry another board's pins) ---

static void test_a_relay_output_on_any_board_pad_is_refused() {
  for(uint8_t pin = 0; pin < 64; pin++) {
    if(!BoardProfile::usesPin(pin)) continue;
    TEST_ASSERT_TRUE(shadeOutputsUseBoardPins(radio_proto::GP_Relay, pin, 17, 18));
    TEST_ASSERT_TRUE(shadeOutputsUseBoardPins(radio_proto::GP_Relay, 16, pin, 18));
    TEST_ASSERT_TRUE(shadeOutputsUseBoardPins(radio_proto::GP_Remote, pin, 17, 18));
    TEST_ASSERT_TRUE(shadeOutputsUseBoardPins(radio_proto::GP_Remote, 16, pin, 18));
    TEST_ASSERT_TRUE(shadeOutputsUseBoardPins(radio_proto::GP_Remote, 16, 17, pin));
  }
}
static void test_a_relay_has_no_my_output() {
  for(uint8_t pin = 0; pin < 64; pin++)
    if(BoardProfile::usesPin(pin))
      TEST_ASSERT_FALSE(shadeOutputsUseBoardPins(radio_proto::GP_Relay, 16, 17, pin));
}
static void test_free_pads_are_allowed() {
  TEST_ASSERT_FALSE(shadeOutputsUseBoardPins(radio_proto::GP_Relay, 16, 17, 18));
  TEST_ASSERT_FALSE(shadeOutputsUseBoardPins(radio_proto::GP_Remote, 16, 17, 18));
}
static void test_radio_shades_have_no_gpio_outputs() {
  TEST_ASSERT_FALSE(shadeOutputsUseBoardPins(radio_proto::RTS, 3, 9, 10));
  TEST_ASSERT_FALSE(shadeOutputsUseBoardPins(radio_proto::RTW, 3, 9, 10));
  TEST_ASSERT_FALSE(shadeOutputsUseBoardPins(radio_proto::RTV, 3, 9, 10));
}

// --- startup order; this runs first as begin() only acts once ---

static void test_begin_deselects_every_slave_before_the_power_rail() {
  BoardProfile::begin();
  const int power = firstEvent(E::Level, BoardProfile::power);
  TEST_ASSERT_TRUE(power >= 0);
  TEST_ASSERT_EQUAL_UINT(1, countEvents(E::Level, BoardProfile::power, HIGH));
  // Nothing happens after the rail comes up except settling.
  TEST_ASSERT_EQUAL((int)stubGpioLog.size() - 2, power); // then its own pinMode
  TEST_ASSERT_EQUAL(E::Mode, stubGpioLog.back().kind);
  TEST_ASSERT_EQUAL(BoardProfile::power, stubGpioLog.back().pin);
  const uint8_t high[] = {BoardProfile::cs, BoardProfile::sdCS, BoardProfile::displayCS,
    BoardProfile::nrfCS, BoardProfile::sw1, BoardProfile::sw0};
  const uint8_t low[] = {BoardProfile::nrfCE, BoardProfile::backlight};
  for(uint8_t pin : high) {
    TEST_ASSERT_TRUE(firstEvent(E::Level, pin) < power);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(E::Level, pin, HIGH));
    TEST_ASSERT_EQUAL(OUTPUT, stubPinMode[pin]);
  }
  for(uint8_t pin : low) {
    TEST_ASSERT_TRUE(firstEvent(E::Level, pin) < power);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(E::Level, pin, LOW));
    TEST_ASSERT_EQUAL(OUTPUT, stubPinMode[pin]);
  }
  TEST_ASSERT_EQUAL(OUTPUT, stubPinMode[BoardProfile::power]);
  TEST_ASSERT_EQUAL(10, (int)stubMillis); // rail settling
  // Each pin's safe level is latched before it is made an output.
  for(const auto &e : stubGpioLog) {
    if(e.kind != E::Mode) continue;
    TEST_ASSERT_EQUAL(OUTPUT, e.value);
    int latch = -1, mode = -1;
    for(size_t i = 0; i < stubGpioLog.size(); i++) {
      if(stubGpioLog[i].pin != e.pin) continue;
      if(stubGpioLog[i].kind == E::Level && latch < 0) latch = (int)i;
      if(stubGpioLog[i].kind == E::Mode && mode < 0) mode = (int)i;
    }
    TEST_ASSERT_TRUE_MESSAGE(latch >= 0 && latch < mode, "pin became an output before its level was latched");
  }
  // The shared SPI pads are left alone until the radio is prepared.
  for(uint8_t pin : {BoardProfile::sck, BoardProfile::mosi, BoardProfile::miso})
    TEST_ASSERT_EQUAL(-1, firstEvent(E::Mode, pin));
  TEST_ASSERT_FALSE(SPI.active);
}
static void test_a_second_begin_does_nothing() {
  // begin() ran in the previous test; starting again changes nothing.
  stubGpioLog.clear();
  BoardProfile::begin();
  TEST_ASSERT_EQUAL_UINT(0, stubGpioLog.size());
}

// --- radio bring-up ---

static void test_prepare_releases_spi_before_it_probes_the_pads() {
  TEST_ASSERT_TRUE(BoardProfile::prepareRadio(433.42f));
  const int end = firstEvent(E::SpiEnd, 0);
  TEST_ASSERT_TRUE(end >= 0);
  for(uint8_t pin : {BoardProfile::sck, BoardProfile::mosi, BoardProfile::miso})
    TEST_ASSERT_TRUE(firstEvent(E::Mode, pin) > end);
  TEST_ASSERT_EQUAL(INPUT, stubPinMode[BoardProfile::miso]);
  TEST_ASSERT_EQUAL(OUTPUT, stubPinMode[BoardProfile::mosi]);
  TEST_ASSERT_EQUAL(OUTPUT, stubPinMode[BoardProfile::sck]);
  TEST_ASSERT_EQUAL(HIGH, stubPinLevel[BoardProfile::cs]); // released after the probe
  TEST_ASSERT_TRUE(SPI.active);
  TEST_ASSERT_EQUAL_UINT(1, SPI.begins);
  TEST_ASSERT_EQUAL(11, SPI.sck);
  TEST_ASSERT_EQUAL(10, SPI.miso);
  TEST_ASSERT_EQUAL(9, SPI.mosi);
  TEST_ASSERT_EQUAL(12, SPI.cs);
}
static void test_prepare_can_run_again_on_a_running_bus() {
  TEST_ASSERT_TRUE(BoardProfile::prepareRadio(433.42f));
  TEST_ASSERT_TRUE(BoardProfile::prepareRadio(433.42f));
  TEST_ASSERT_EQUAL_UINT(2, SPI.begins);
  TEST_ASSERT_EQUAL_UINT(2, SPI.ends);
  TEST_ASSERT_TRUE(SPI.active);
}
static void test_prepare_refuses_a_frequency_outside_the_433_band() {
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(868.0f));
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(386.9f));
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(464.1f));
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(0.0f));
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(NAN));
  TEST_ASSERT_TRUE(BoardProfile::prepareRadio(387.0f));
  TEST_ASSERT_TRUE(BoardProfile::prepareRadio(464.0f));
}
static void test_a_rejected_frequency_leaves_the_bus_alone() {
  BoardProfile::prepareRadio(868.0f);
  TEST_ASSERT_EQUAL_UINT(0, stubGpioLog.size());
  TEST_ASSERT_EQUAL_UINT(0, SPI.begins);
}
static void test_prepare_fails_fast_when_the_radio_holds_miso_high() {
  stubPinReadOverride[BoardProfile::miso] = HIGH;
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(433.42f));
  TEST_ASSERT_TRUE(stubMillis <= 25); // not a hang inside the radio driver
  TEST_ASSERT_EQUAL(HIGH, stubPinLevel[BoardProfile::cs]);
  TEST_ASSERT_FALSE(SPI.active);
  TEST_ASSERT_EQUAL_UINT(0, SPI.begins);
}
static void test_a_retry_after_a_fault_does_not_power_cycle_the_board() {
  stubPinReadOverride[BoardProfile::miso] = HIGH;
  TEST_ASSERT_FALSE(BoardProfile::prepareRadio(433.42f));
  stubPinReadOverride[BoardProfile::miso] = -1;
  TEST_ASSERT_TRUE(BoardProfile::prepareRadio(433.42f));
  TEST_ASSERT_EQUAL_UINT(0, countEvents(E::Level, BoardProfile::power, HIGH));
  TEST_ASSERT_EQUAL_UINT(0, countEvents(E::Level, BoardProfile::power, LOW));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_deselects_every_slave_before_the_power_rail);
  RUN_TEST(test_a_second_begin_does_nothing);
  RUN_TEST(test_profile_has_an_integrated_radio);
  RUN_TEST(test_the_wiring_is_the_lilygo_one);
  RUN_TEST(test_every_board_pad_is_reserved);
  RUN_TEST(test_usb_and_flash_pads_are_reserved);
  RUN_TEST(test_other_pads_are_free);
  RUN_TEST(test_a_relay_output_on_any_board_pad_is_refused);
  RUN_TEST(test_a_relay_has_no_my_output);
  RUN_TEST(test_free_pads_are_allowed);
  RUN_TEST(test_radio_shades_have_no_gpio_outputs);
  RUN_TEST(test_prepare_releases_spi_before_it_probes_the_pads);
  RUN_TEST(test_prepare_can_run_again_on_a_running_bus);
  RUN_TEST(test_prepare_refuses_a_frequency_outside_the_433_band);
  RUN_TEST(test_a_rejected_frequency_leaves_the_bus_alone);
  RUN_TEST(test_prepare_fails_fast_when_the_radio_holds_miso_high);
  RUN_TEST(test_a_retry_after_a_fault_does_not_power_cycle_the_board);
  return UNITY_END();
}
