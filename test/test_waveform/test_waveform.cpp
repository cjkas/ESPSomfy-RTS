#include <unity.h>
#include "SomfyWaveform.h"

void setUp() {}
void tearDown() {}

static const uint8_t FRAME[10] = {0xA7, 0x5C, 0x13, 0xF0, 0x0F, 0x81, 0x42, 0x99, 0x3C, 0xE1};
// The smallest RMT channel memory of any chip (C3, C5 and C6).
#define CAPACITY 96

struct Built {
  somfy_waveform_t wave;
  somfy_symbol_t symbols[256];
};
static Built build(const uint8_t *payload, uint8_t sync, uint8_t bitLength, uint16_t capacity = CAPACITY) {
  Built b;
  b.wave = buildWaveform(payload, sync, bitLength, b.symbols, capacity);
  return b;
}
static void assertSymbol(uint8_t level0, uint32_t us0, uint8_t level1, uint32_t us1, const somfy_symbol_t &s) {
  TEST_ASSERT_EQUAL_UINT8(level0, s.level0);
  TEST_ASSERT_EQUAL_UINT32(us0, s.duration0);
  TEST_ASSERT_EQUAL_UINT8(level1, s.level1);
  TEST_ASSERT_EQUAL_UINT32(us1, s.duration1);
}
// The bits of FRAME as Transceiver sent them before RMT: a 1 is low then
// high, a 0 high then low, MSB first.
static void assertBits(const somfy_symbol_t *s, uint8_t bitLength) {
  for(uint8_t i = 0; i < bitLength; i++) {
    const bool one = (FRAME[i / 8] >> (7 - (i % 8))) & 1;
    if(one) assertSymbol(0, 640, 1, 640, s[i]);
    else assertSymbol(1, 640, 0, 640, s[i]);
  }
}
static uint32_t totalUs(const Built &b) {
  uint32_t us = 0;
  for(uint16_t i = 0; i < b.wave.symbols; i++) us += b.symbols[i].duration0 + b.symbols[i].duration1;
  return us;
}

static void test_56_bit_first_frame() {
  Built b = build(FRAME, 2, 56);
  TEST_ASSERT_EQUAL_UINT16(61, b.wave.symbols);
  TEST_ASSERT_EQUAL_UINT8(2, b.wave.sync);
  assertSymbol(1, 10920, 0, 7357, b.symbols[0]);  // Wakeup.
  assertSymbol(1, 2560, 0, 2560, b.symbols[1]);   // Hardware syncs.
  assertSymbol(1, 2560, 0, 2560, b.symbols[2]);
  assertSymbol(1, 4850, 0, 640, b.symbols[3]);    // Software sync.
  assertBits(&b.symbols[4], 56);
  assertSymbol(0, 13717, 0, 13717, b.symbols[60]);  // Inter-frame silence.
}
static void test_56_bit_repeat() {
  Built b = build(FRAME, 7, 56);
  TEST_ASSERT_EQUAL_UINT16(65, b.wave.symbols);
  for(uint8_t i = 0; i < 7; i++) assertSymbol(1, 2560, 0, 2560, b.symbols[i]);
  assertSymbol(1, 4850, 0, 640, b.symbols[7]);
  assertBits(&b.symbols[8], 56);
  assertSymbol(0, 13717, 0, 13717, b.symbols[64]);
}
static void test_80_bit_first_frame() {
  Built b = build(FRAME, 12, 80);
  TEST_ASSERT_EQUAL_UINT16(94, b.wave.symbols);
  assertSymbol(1, 10920, 0, 7357, b.symbols[0]);
  for(uint8_t i = 1; i <= 12; i++) assertSymbol(1, 2560, 0, 2560, b.symbols[i]);
  assertSymbol(1, 4850, 0, 640, b.symbols[13]);
  // No silence after an 80-bit frame: it ends with the last bit.
  assertBits(&b.symbols[14], 80);
}
static void test_80_bit_repeat() {
  Built b = build(FRAME, 6, 80);
  TEST_ASSERT_EQUAL_UINT16(87, b.wave.symbols);
  for(uint8_t i = 0; i < 6; i++) assertSymbol(1, 2560, 0, 2560, b.symbols[i]);
  assertSymbol(1, 4850, 0, 640, b.symbols[6]);
  assertBits(&b.symbols[7], 80);
}
// The air time of each frame as the delays in the old sendFrame() added up.
static void test_frame_durations() {
  TEST_ASSERT_EQUAL_UINT32(133121, totalUs(build(FRAME, 2, 56)));
  TEST_ASSERT_EQUAL_UINT32(140444, totalUs(build(FRAME, 7, 56)));
  TEST_ASSERT_EQUAL_UINT32(187607, totalUs(build(FRAME, 12, 80)));
  TEST_ASSERT_EQUAL_UINT32(138610, totalUs(build(FRAME, 6, 80)));
}
static void test_bits_go_msb_first() {
  const uint8_t payload[10] = {0x80, 0x01};
  Built b = build(payload, 7, 56);
  assertSymbol(0, 640, 1, 640, b.symbols[8]);       // Bit 0 is the top bit of byte 0.
  for(uint8_t i = 9; i < 23; i++) assertSymbol(1, 640, 0, 640, b.symbols[i]);
  assertSymbol(0, 640, 1, 640, b.symbols[23]);      // Bit 15 is the bottom bit of byte 1.
}
static void test_symbol_count_matches() {
  TEST_ASSERT_EQUAL_UINT16(waveformSymbols(2, 56), build(FRAME, 2, 56).wave.symbols);
  TEST_ASSERT_EQUAL_UINT16(waveformSymbols(7, 56), build(FRAME, 7, 56).wave.symbols);
  TEST_ASSERT_EQUAL_UINT16(waveformSymbols(12, 80), build(FRAME, 12, 80).wave.symbols);
  TEST_ASSERT_EQUAL_UINT16(waveformSymbols(6, 80), build(FRAME, 6, 80).wave.symbols);
}
static void test_first_frame_fits_every_chip() {
  // With the end marker.
  TEST_ASSERT_LESS_OR_EQUAL_UINT16(CAPACITY, waveformSymbols(12, 80) + 1);
}
// A repeater relays an 80-bit first frame with 24 syncs (#94).
static void test_syncs_that_dont_fit_are_left_out() {
  Built b = build(FRAME, 24, 80, 96);
  TEST_ASSERT_EQUAL_UINT8(14, b.wave.sync);
  TEST_ASSERT_EQUAL_UINT16(95, b.wave.symbols);
  for(uint8_t i = 0; i < 14; i++) assertSymbol(1, 2560, 0, 2560, b.symbols[i]);
  assertSymbol(1, 4850, 0, 640, b.symbols[14]);
  assertBits(&b.symbols[15], 80);
}
static void test_syncs_that_fit_are_all_sent() {
  Built b = build(FRAME, 24, 80, 144);
  TEST_ASSERT_EQUAL_UINT8(24, b.wave.sync);
  TEST_ASSERT_EQUAL_UINT16(105, b.wave.symbols);
}
// Leaving a sync out must not turn a frame into a first frame: that adds a
// wakeup pulse, one more symbol than there is room for.
static void test_capped_frame_gets_no_wakeup() {
  Built b = build(FRAME, 14, 80, 95);
  TEST_ASSERT_EQUAL_UINT8(13, b.wave.sync);
  TEST_ASSERT_EQUAL_UINT16(94, b.wave.symbols);
  assertSymbol(1, 2560, 0, 2560, b.symbols[0]);
}
static void test_frame_that_cant_fit_is_not_built() {
  // Wakeup, software sync, 80 bits and the end marker take 83.
  TEST_ASSERT_EQUAL_UINT16(0, build(FRAME, 12, 80, 82).wave.symbols);
  TEST_ASSERT_EQUAL_UINT8(0, build(FRAME, 12, 80, 83).wave.sync);
  TEST_ASSERT_EQUAL_UINT16(82, build(FRAME, 12, 80, 83).wave.symbols);
}
static void test_more_than_80_bits_is_not_built() {
  TEST_ASSERT_EQUAL_UINT16(0, build(FRAME, 7, 81).wave.symbols);
}
// test_pulse decodes buildWaveform()'s output with our receiver.

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_56_bit_first_frame);
  RUN_TEST(test_56_bit_repeat);
  RUN_TEST(test_80_bit_first_frame);
  RUN_TEST(test_80_bit_repeat);
  RUN_TEST(test_frame_durations);
  RUN_TEST(test_bits_go_msb_first);
  RUN_TEST(test_symbol_count_matches);
  RUN_TEST(test_first_frame_fits_every_chip);
  RUN_TEST(test_syncs_that_dont_fit_are_left_out);
  RUN_TEST(test_syncs_that_fit_are_all_sent);
  RUN_TEST(test_capped_frame_gets_no_wakeup);
  RUN_TEST(test_frame_that_cant_fit_is_not_built);
  RUN_TEST(test_more_than_80_bits_is_not_built);
  return UNITY_END();
}
