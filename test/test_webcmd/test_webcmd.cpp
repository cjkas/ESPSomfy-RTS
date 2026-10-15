#include <unity.h>
#include "WebCommand.h"

static const web_cmd_t RADIO_COMMANDS[] = {
  web_cmd_t::shade_command, web_cmd_t::group_command, web_cmd_t::tilt_command,
  web_cmd_t::shade_repeat, web_cmd_t::group_repeat, web_cmd_t::shade_sensor,
  web_cmd_t::group_sensor, web_cmd_t::set_my_position, web_cmd_t::unpair_shade
};

void setUp() {}
void tearDown() {}

static const web_cmd_t LOOP_COMMANDS[] = {
  web_cmd_t::set_positions, web_cmd_t::set_rolling_code, web_cmd_t::restore_shades, web_cmd_t::load_shades
};

static void test_only_loop_commands_stay_off_the_radio() {
  int offRadio = 0;
  for(uint8_t t = 0; t <= static_cast<uint8_t>(web_cmd_t::load_shades); t++) {
    if(!webCommandUsesRadio(static_cast<web_cmd_t>(t))) offRadio++;
  }
  TEST_ASSERT_EQUAL_INT(4, offRadio);
  for(web_cmd_t t : LOOP_COMMANDS) TEST_ASSERT_FALSE(webCommandUsesRadio(t));
}
static void test_loop_commands_are_never_held() {
  // Nothing goes on air, so a full or suspended radio doesn't matter.
  for(web_cmd_t t : LOOP_COMMANDS) {
    TEST_ASSERT_FALSE(holdWebCommand(t, false, false));
    TEST_ASSERT_FALSE(holdWebCommand(t, true, false));
  }
}
static void test_radio_command_waits_for_room() {
  for(web_cmd_t t : RADIO_COMMANDS) {
    TEST_ASSERT_TRUE(holdWebCommand(t, false, false));
    TEST_ASSERT_FALSE(holdWebCommand(t, false, true));
  }
}
static void test_commands_are_not_held_while_suspended() {
  // Nothing goes on air during a frequency scan or OTA anyway.
  for(web_cmd_t t : RADIO_COMMANDS) {
    if(t == web_cmd_t::unpair_shade) continue;
    TEST_ASSERT_FALSE(holdWebCommand(t, true, false));
    TEST_ASSERT_FALSE(holdWebCommand(t, true, true));
  }
}
static void test_unpair_waits_out_a_suspend() {
  // The client was already told the shade is unpaired.
  TEST_ASSERT_TRUE(holdWebCommand(web_cmd_t::unpair_shade, true, false));
  TEST_ASSERT_TRUE(holdWebCommand(web_cmd_t::unpair_shade, true, true));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_only_loop_commands_stay_off_the_radio);
  RUN_TEST(test_loop_commands_are_never_held);
  RUN_TEST(test_radio_command_waits_for_room);
  RUN_TEST(test_commands_are_not_held_while_suspended);
  RUN_TEST(test_unpair_waits_out_a_suspend);
  return UNITY_END();
}
