#include <unity.h>
#include "ConfigHeader.h"

void setUp() {}
void tearDown() {}

static config_header_t header() {
  config_header_t h;
  h.version = 22;
  h.roomRecords = 3;
  h.roomRecordSize = 29;
  h.shadeRecords = 5;
  h.shadeRecordSize = 276;
  h.groupRecords = 2;
  h.groupRecordSize = 200;
  h.repeaterRecords = 1;
  h.repeaterRecordSize = 77;
  h.settingsRecordSize = 100;
  return h;
}

static void test_shade_model_counts_rooms_shades_and_groups() {
  // A restore that leaves the shades out has to skip the rooms too, or it
  // reads the repeaters and settings from the room records.
  TEST_ASSERT_EQUAL_UINT32(3 * 29 + 5 * 276 + 2 * 200, shadeModelBytes(header()));
}
static void test_shade_model_leaves_out_the_rest() {
  config_header_t h = header();
  const uint32_t before = shadeModelBytes(h);
  h.repeaterRecordSize = 1;
  h.settingsRecordSize = 1;
  h.netRecordSize = 1;
  h.transRecordSize = 1;
  TEST_ASSERT_EQUAL_UINT32(before, shadeModelBytes(h));
}
static void test_an_old_file_without_rooms() {
  config_header_t h = header();
  h.roomRecords = 0;
  TEST_ASSERT_EQUAL_UINT32(5 * 276 + 2 * 200, shadeModelBytes(h));
}
static void test_a_full_model_does_not_overflow() {
  config_header_t h;
  h.roomRecords = h.shadeRecords = h.groupRecords = 255;
  h.roomRecordSize = h.shadeRecordSize = h.groupRecordSize = 0xFFFF;
  TEST_ASSERT_EQUAL_UINT32(3UL * 255 * 0xFFFF, shadeModelBytes(h));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_shade_model_counts_rooms_shades_and_groups);
  RUN_TEST(test_shade_model_leaves_out_the_rest);
  RUN_TEST(test_an_old_file_without_rooms);
  RUN_TEST(test_a_full_model_does_not_overflow);
  return UNITY_END();
}
