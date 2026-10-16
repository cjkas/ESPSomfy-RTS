#include <unity.h>
#include <set>
#include "ModelEditor.h"

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Ids, sort orders and remote addresses
 ********************************************************************/
static void test_first_free_id_fills_gaps() {
  const uint8_t ids[] = { 1, 2, 4, 255, 255 };
  TEST_ASSERT_EQUAL_UINT8(3, firstFreeId(ids, 5, 30));
}
static void test_first_free_id_of_an_empty_list_is_1() {
  TEST_ASSERT_EQUAL_UINT8(1, firstFreeId(nullptr, 0, 30));
}
static void test_first_free_id_is_0_when_every_one_is_taken() {
  const uint8_t ids[] = { 1, 2, 3 };
  TEST_ASSERT_EQUAL_UINT8(0, firstFreeId(ids, 3, 3));
  TEST_ASSERT_EQUAL_UINT8(4, firstFreeId(ids, 3, 4));
}
static void test_next_sort_order_goes_after_the_last() {
  const int8_t orders[] = { 3, 0, 7, 2 };
  TEST_ASSERT_EQUAL_INT8(8, nextSortOrder(orders, 4));
  TEST_ASSERT_EQUAL_INT8(0, nextSortOrder(nullptr, 0));
}
static void test_free_remote_address_skips_every_taken_one() {
  // 101 is taken, so 102 is tried, which is taken too, and so on.
  const uint32_t used[] = { 103, 101, 102, 500 };
  TEST_ASSERT_EQUAL_UINT32(104, freeRemoteAddress(101, used, 4));
  TEST_ASSERT_EQUAL_UINT32(100, freeRemoteAddress(100, used, 4));
}

/*********************************************************************
 * Lists
 ********************************************************************/
static void test_add_goes_into_the_first_empty_slot() {
  uint8_t list[4] = { 5, 7, 0, 0 };
  TEST_ASSERT_TRUE(addToList(list, 4, 9));
  const uint8_t expected[] = { 5, 7, 9, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 4);
}
static void test_adding_one_already_there_changes_nothing() {
  uint8_t list[4] = { 5, 7, 0, 0 };
  TEST_ASSERT_TRUE(addToList(list, 4, 7));
  const uint8_t expected[] = { 5, 7, 0, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 4);
}
static void test_adding_to_a_full_list_fails() {
  uint8_t list[2] = { 5, 7 };
  TEST_ASSERT_FALSE(addToList(list, 2, 9));
}
static void test_remove_packs_the_rest_in_order() {
  uint8_t list[5] = { 1, 4, 3, 4, 0 };
  TEST_ASSERT_TRUE(removeFromList(list, 5, 4));
  const uint8_t expected[] = { 1, 3, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 5);
}
static void test_removing_one_not_there_reports_it() {
  uint8_t list[3] = { 1, 2, 0 };
  TEST_ASSERT_FALSE(removeFromList(list, 3, 9));
}
static void test_pack_closes_gaps() {
  uint8_t list[7] = { 1, 0, 4, 3, 0, 0, 6 };
  packList(list, 7);
  const uint8_t expected[] = { 1, 4, 3, 6, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, list, 7);
}
static void test_repeater_addresses_use_the_same_rules() {
  uint32_t list[3] = { 0, 0, 0 };
  TEST_ASSERT_TRUE(addToList(list, 3, 0xABCDEFu));
  TEST_ASSERT_TRUE(addToList(list, 3, 0x123456u));
  TEST_ASSERT_TRUE(addToList(list, 3, 0xABCDEFu));
  TEST_ASSERT_TRUE(removeFromList(list, 3, 0xABCDEFu));
  TEST_ASSERT_EQUAL_UINT32(0x123456u, list[0]);
  TEST_ASSERT_EQUAL_UINT32(0, list[1]);
}

/*********************************************************************
 * GPIO pins
 ********************************************************************/
static std::set<uint8_t> radioPins, ethernetPins, shadePins;
static bool inSet(uint8_t pin, void *ctx) { return static_cast<std::set<uint8_t> *>(ctx)->count(pin) > 0; }
static pin_users_t users(std::set<uint8_t> &pins) {
  pin_users_t u;
  u.uses = inSet;
  u.ctx = &pins;
  return u;
}
static int8_t conflict(shade_types type, radio_proto proto, uint8_t up, uint8_t down, uint8_t my) {
  return gpioPinConflict(type, proto, up, down, my, users(radioPins), users(ethernetPins), users(shadePins));
}

static void test_a_radio_shade_uses_no_pins() {
  radioPins = { 4 };
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::RTS, 4, 4, 4));
}
static void test_free_pins_are_fine() {
  radioPins = { 4 }; ethernetPins = { 18 }; shadePins = { 21 };
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_a_pin_the_radio_uses() {
  radioPins = { 26 }; ethernetPins = {}; shadePins = {};
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_RADIO, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_every_pin_is_checked_against_ethernet() {
  // It used to check only the up pin against Ethernet.
  radioPins = {}; shadePins = {};
  ethernetPins = { 26 };
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_ETHERNET, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
  ethernetPins = { 27 };
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_ETHERNET, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_a_pin_another_shade_uses() {
  radioPins = {}; ethernetPins = {};
  shadePins = { 25 };
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_SHADE, conflict(shade_types::roller, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_only_the_pins_a_shade_drives_are_checked() {
  radioPins = {}; ethernetPins = {};
  shadePins = { 25, 27 };
  // A relay never drives My.
  TEST_ASSERT_EQUAL_INT8(PIN_CONFLICT_SHADE, conflict(shade_types::roller, radio_proto::GP_Relay, 25, 26, 27));
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::GP_Relay, 24, 26, 27));
  // A dry contact only drives down; a two way dry contact up and down.
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::drycontact, radio_proto::GP_Remote, 25, 26, 27));
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::drycontact2, radio_proto::GP_Remote, 24, 26, 27));
  // A single button gate on a remote only drives down.
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::garage1, radio_proto::GP_Remote, 25, 26, 27));
}
static void test_255_means_no_pin() {
  radioPins = { 255 }; ethernetPins = { 255 }; shadePins = { 255 };
  TEST_ASSERT_EQUAL_INT8(0, conflict(shade_types::roller, radio_proto::GP_Remote, 255, 26, 255));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_free_id_fills_gaps);
  RUN_TEST(test_first_free_id_of_an_empty_list_is_1);
  RUN_TEST(test_first_free_id_is_0_when_every_one_is_taken);
  RUN_TEST(test_next_sort_order_goes_after_the_last);
  RUN_TEST(test_free_remote_address_skips_every_taken_one);
  RUN_TEST(test_add_goes_into_the_first_empty_slot);
  RUN_TEST(test_adding_one_already_there_changes_nothing);
  RUN_TEST(test_adding_to_a_full_list_fails);
  RUN_TEST(test_remove_packs_the_rest_in_order);
  RUN_TEST(test_removing_one_not_there_reports_it);
  RUN_TEST(test_pack_closes_gaps);
  RUN_TEST(test_repeater_addresses_use_the_same_rules);
  RUN_TEST(test_a_radio_shade_uses_no_pins);
  RUN_TEST(test_free_pins_are_fine);
  RUN_TEST(test_a_pin_the_radio_uses);
  RUN_TEST(test_every_pin_is_checked_against_ethernet);
  RUN_TEST(test_a_pin_another_shade_uses);
  RUN_TEST(test_only_the_pins_a_shade_drives_are_checked);
  RUN_TEST(test_255_means_no_pin);
  return UNITY_END();
}
