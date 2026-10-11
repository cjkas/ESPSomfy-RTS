#include <unity.h>
#include "ShadeMotion.h"
#include "SomfyFrame.h"

#define ASSERT_POS(expected, actual) TEST_ASSERT_FLOAT_WITHIN(0.01f, (expected), (actual))
#define ASSERT_CMD(expected, actual) TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static const int8_t DOWN = 1;
static const int8_t UP = -1;

void setUp() {}
void tearDown() {}

/*********************************************************************
 * travelPosition
 ********************************************************************/
static void test_down_part_way() {
  ASSERT_POS(50.0f, travelPosition(0, DOWN, 5000, 10000));
  // From 30% the first 3s of travel are already done.
  ASSERT_POS(50.0f, travelPosition(30, DOWN, 2000, 10000));
}
static void test_up_part_way() {
  ASSERT_POS(75.0f, travelPosition(100, UP, 2500, 10000));
  ASSERT_POS(25.0f, travelPosition(50, UP, 5000, 20000));
}
static void test_no_time_elapsed_stays_put() {
  // The start is counted in whole milliseconds so allow for that.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.5f, travelPosition(50.5f, DOWN, 0, 10000));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.5f, travelPosition(50.5f, UP, 0, 10000));
}
static void test_stops_at_the_ends() {
  ASSERT_POS(100.0f, travelPosition(0, DOWN, 10000, 10000));
  ASSERT_POS(100.0f, travelPosition(0, DOWN, 20000, 10000));
  ASSERT_POS(0.0f, travelPosition(100, UP, 10000, 10000));
  ASSERT_POS(0.0f, travelPosition(100, UP, 20000, 10000));
}
static void test_starting_at_the_end() {
  ASSERT_POS(100.0f, travelPosition(100, DOWN, 0, 10000));
  ASSERT_POS(100.0f, travelPosition(100, DOWN, 3000, 10000));
  ASSERT_POS(0.0f, travelPosition(0, UP, 0, 10000));
  ASSERT_POS(0.0f, travelPosition(0, UP, 3000, 10000));
}
static void test_always_within_range() {
  const float starts[] = {0.0f, 12.5f, 50.0f, 99.9f, 100.0f};
  for(float start : starts) {
    for(uint32_t elapsed = 0; elapsed <= 30000; elapsed += 250) {
      for(int8_t dir : {DOWN, UP}) {
        float pos = travelPosition(start, dir, elapsed, 10000);
        TEST_ASSERT_TRUE(pos >= 0.0f && pos <= 100.0f);
      }
    }
  }
}
static void test_up_and_down_use_their_own_times() {
  // 2s of a 10s travel down covers 20%, 2s of a 20s travel up only 10%.
  ASSERT_POS(20.0f, travelPosition(0, DOWN, 2000, 10000));
  ASSERT_POS(90.0f, travelPosition(100, UP, 2000, 20000));
}
static void test_zero_travel_time_is_instant() {
  ASSERT_POS(100.0f, travelPosition(0, DOWN, 0, 0));
  ASSERT_POS(100.0f, travelPosition(42, DOWN, 0, 0));
  ASSERT_POS(0.0f, travelPosition(100, UP, 0, 0));
  ASSERT_POS(0.0f, travelPosition(42, UP, 0, 0));
}
static void test_moves_steadily() {
  // Each step of time moves the shade further, never back.
  float last = 0.0f;
  for(uint32_t elapsed = 0; elapsed <= 10000; elapsed += 100) {
    float pos = travelPosition(0, DOWN, elapsed, 10000);
    TEST_ASSERT_TRUE(pos >= last);
    last = pos;
  }
  last = 100.0f;
  for(uint32_t elapsed = 0; elapsed <= 10000; elapsed += 100) {
    float pos = travelPosition(100, UP, elapsed, 10000);
    TEST_ASSERT_TRUE(pos <= last);
    last = pos;
  }
}

/*********************************************************************
 * displayPosition
 ********************************************************************/
static void test_displayPosition() {
  TEST_ASSERT_EQUAL_INT8(-1, displayPosition(-1, false));
  TEST_ASSERT_EQUAL_INT8(-1, displayPosition(-1, true));
  TEST_ASSERT_EQUAL_INT8(37, displayPosition(37.9f, false));
  TEST_ASSERT_EQUAL_INT8(62, displayPosition(37.9f, true));
  TEST_ASSERT_EQUAL_INT8(100, displayPosition(0, true));
  TEST_ASSERT_EQUAL_INT8(0, displayPosition(100, true));
  TEST_ASSERT_EQUAL_INT8(100, displayPosition(100, false));
}

/*********************************************************************
 * flipCommand
 ********************************************************************/
static void test_flipCommand_swaps_directions() {
  ASSERT_CMD(somfy_commands::Down, flipCommand(somfy_commands::Up));
  ASSERT_CMD(somfy_commands::Up, flipCommand(somfy_commands::Down));
  ASSERT_CMD(somfy_commands::MyDown, flipCommand(somfy_commands::MyUp));
  ASSERT_CMD(somfy_commands::MyUp, flipCommand(somfy_commands::MyDown));
  ASSERT_CMD(somfy_commands::StepDown, flipCommand(somfy_commands::StepUp));
  ASSERT_CMD(somfy_commands::StepUp, flipCommand(somfy_commands::StepDown));
}
static void test_flipCommand_keeps_the_rest() {
  const somfy_commands same[] = {
    somfy_commands::My, somfy_commands::Prog, somfy_commands::Toggle,
    somfy_commands::UpDown, somfy_commands::MyUpDown, somfy_commands::Favorite
  };
  for(somfy_commands cmd : same) ASSERT_CMD(cmd, flipCommand(cmd));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_down_part_way);
  RUN_TEST(test_up_part_way);
  RUN_TEST(test_no_time_elapsed_stays_put);
  RUN_TEST(test_stops_at_the_ends);
  RUN_TEST(test_starting_at_the_end);
  RUN_TEST(test_always_within_range);
  RUN_TEST(test_up_and_down_use_their_own_times);
  RUN_TEST(test_zero_travel_time_is_instant);
  RUN_TEST(test_moves_steadily);
  RUN_TEST(test_displayPosition);
  RUN_TEST(test_flipCommand_swaps_directions);
  RUN_TEST(test_flipCommand_keeps_the_rest);
  return UNITY_END();
}
