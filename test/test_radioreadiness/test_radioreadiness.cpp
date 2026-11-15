#include <unity.h>
#include <cmath>
#include "RadioReadiness.h"

void setUp() {}
void tearDown() {}

// What a healthy CC1101 reports when set up for 433.42 MHz async OOK.
static radio_readings_t healthy() {
  radio_readings_t r;
  r.partnum = 0;
  r.version = 0x14;
  r.iocfg0 = 0x0D;
  r.iocfg2 = 0x0D;
  r.pktctrl0 = 0x32;
  r.mdmcfg2 = 0x3C;
  r.freqWord = 0x10AB85;
  return r;
}
static const float MHZ = 433.42f;

static void test_frequency_word_converts_to_mhz() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 433.42f, radioFrequencyMHz(0x10AB85));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, radioFrequencyMHz(0));
}
static void test_a_healthy_radio_is_ready() {
  TEST_ASSERT_TRUE(radioRegistersReady(healthy(), MHZ));
}
static void test_a_part_that_is_not_a_cc1101_is_refused() {
  radio_readings_t r = healthy();
  r.partnum = 1;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r.partnum = 0xFF;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
}
static void test_no_chip_answering_is_refused() {
  radio_readings_t r = healthy();
  r.version = 0;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r.version = 0xFF;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
}
static void test_gdo_pins_must_carry_the_async_data() {
  radio_readings_t r = healthy();
  r.iocfg0 = 0;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r = healthy();
  r.iocfg2 = 0;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
}
static void test_packet_format_and_modulation_must_be_set() {
  radio_readings_t r = healthy();
  r.pktctrl0 = 0;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r = healthy();
  r.pktctrl0 = 0x10; // only part of the async format bits
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r = healthy();
  r.mdmcfg2 = 0;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r = healthy();
  r.mdmcfg2 = 0x0C; // 2-FSK instead of OOK
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
}
static void test_unrelated_register_bits_are_ignored() {
  radio_readings_t r = healthy();
  r.pktctrl0 = 0x3F;
  r.mdmcfg2 = 0xBF;
  TEST_ASSERT_TRUE(radioRegistersReady(r, MHZ));
}
static void test_the_frequency_readback_must_match_the_request() {
  radio_readings_t r = healthy();
  r.freqWord = 0;
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  r.freqWord = 0x10AB85 + 100; // 0.04 MHz away
  TEST_ASSERT_FALSE(radioRegistersReady(r, MHZ));
  TEST_ASSERT_FALSE(radioRegistersReady(healthy(), 433.92f));
  TEST_ASSERT_FALSE(radioRegistersReady(healthy(), NAN));
}

static void test_rx_state_ready() {
  TEST_ASSERT_EQUAL(rx_outcome_t::ready, radioRxOutcome(RADIO_MARCSTATE_RX, 0, 1));
  TEST_ASSERT_EQUAL(rx_outcome_t::ready, radioRxOutcome(0xED, 0, 1)); // upper bits are not state
}
static void test_a_radio_not_in_rx_is_unavailable_with_one_try() {
  TEST_ASSERT_EQUAL(rx_outcome_t::unavailable, radioRxOutcome(0x00, 0, 1));
  TEST_ASSERT_EQUAL(rx_outcome_t::unavailable, radioRxOutcome(0x01, 0, 1)); // IDLE
  TEST_ASSERT_EQUAL(rx_outcome_t::unavailable, radioRxOutcome(0x11, 0, 1)); // RXFIFO_OVERFLOW
}
static void test_a_radio_not_in_rx_may_retry_while_tries_are_left() {
  TEST_ASSERT_EQUAL(rx_outcome_t::retry, radioRxOutcome(0x01, 0, 2));
  TEST_ASSERT_EQUAL(rx_outcome_t::unavailable, radioRxOutcome(0x01, 1, 2));
  TEST_ASSERT_EQUAL(rx_outcome_t::ready, radioRxOutcome(RADIO_MARCSTATE_RX, 1, 2));
}
static void test_zero_tries_never_retry() {
  TEST_ASSERT_EQUAL(rx_outcome_t::unavailable, radioRxOutcome(0x01, 0, 0));
}
static void test_the_firmware_makes_one_try_for_now() {
  TEST_ASSERT_EQUAL(1, RADIO_RX_ATTEMPTS);
}

static void test_a_generic_radio_may_always_queue_and_send() {
  TEST_ASSERT_TRUE(radioMayTransmit(false, false));
  TEST_ASSERT_TRUE(radioMayTransmit(false, true));
}
static void test_the_integrated_radio_transmits_only_when_ready() {
  TEST_ASSERT_TRUE(radioMayTransmit(true, true));
  TEST_ASSERT_FALSE(radioMayTransmit(true, false));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_frequency_word_converts_to_mhz);
  RUN_TEST(test_a_healthy_radio_is_ready);
  RUN_TEST(test_a_part_that_is_not_a_cc1101_is_refused);
  RUN_TEST(test_no_chip_answering_is_refused);
  RUN_TEST(test_gdo_pins_must_carry_the_async_data);
  RUN_TEST(test_packet_format_and_modulation_must_be_set);
  RUN_TEST(test_unrelated_register_bits_are_ignored);
  RUN_TEST(test_the_frequency_readback_must_match_the_request);
  RUN_TEST(test_rx_state_ready);
  RUN_TEST(test_a_radio_not_in_rx_is_unavailable_with_one_try);
  RUN_TEST(test_a_radio_not_in_rx_may_retry_while_tries_are_left);
  RUN_TEST(test_zero_tries_never_retry);
  RUN_TEST(test_the_firmware_makes_one_try_for_now);
  RUN_TEST(test_a_generic_radio_may_always_queue_and_send);
  RUN_TEST(test_the_integrated_radio_transmits_only_when_ready);
  return UNITY_END();
}
