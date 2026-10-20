#include <unity.h>
#include "Utils.h"

// The C runtime only picks up TZ after it is re-read.
static void setTZ(const char *tz) {
#ifdef _WIN32
  _putenv_s("TZ", tz);
  _tzset();
#else
  setenv("TZ", tz, 1);
  tzset();
#endif
}

// 2024-03-05T07:08:09Z
static const time_t SAMPLE_UTC = 1709622489;

void setUp() { setTZ("UTC0"); }
void tearDown() {}

/*********************************************************************
 * _trim
 ********************************************************************/
static void test_trim_strips_whitespace_and_quotes() {
  char str[] = " \t\"Living Room\"\r\n";
  _trim(str);
  TEST_ASSERT_EQUAL_STRING("Living Room", str);
}
static void test_trim_keeps_clean_string() {
  char str[] = "Kitchen";
  _trim(str);
  TEST_ASSERT_EQUAL_STRING("Kitchen", str);
}
static void test_trim_all_whitespace_becomes_empty() {
  char str[] = " \t\r\n\" ";
  _trim(str);
  TEST_ASSERT_EQUAL_STRING("", str);
}
static void test_trim_empty_string() {
  char str[] = "";
  _trim(str);
  TEST_ASSERT_EQUAL_STRING("", str);
}

/*********************************************************************
 * toBoolean
 ********************************************************************/
static void test_toBoolean_missing_uses_default() {
  TEST_ASSERT_TRUE(toBoolean(nullptr, true));
  TEST_ASSERT_FALSE(toBoolean(nullptr, false));
  TEST_ASSERT_TRUE(toBoolean("", true));
  TEST_ASSERT_FALSE(toBoolean("", false));
}
static void test_toBoolean_true_values() {
  TEST_ASSERT_TRUE(toBoolean("true", false));
  TEST_ASSERT_TRUE(toBoolean("True", false));
  TEST_ASSERT_TRUE(toBoolean("1", false));
}
static void test_toBoolean_false_values() {
  TEST_ASSERT_FALSE(toBoolean("false", true));
  TEST_ASSERT_FALSE(toBoolean("False", true));
  TEST_ASSERT_FALSE(toBoolean("0", true));
}
static void test_toBoolean_unknown_uses_default() {
  TEST_ASSERT_TRUE(toBoolean("yes", true));
  TEST_ASSERT_FALSE(toBoolean("yes", false));
}

/*********************************************************************
 * SETCHARPROP
 ********************************************************************/
static void test_setcharprop_copies_short_value() {
  char prop[8];
  SETCHARPROP(prop, "abc", sizeof(prop));
  TEST_ASSERT_EQUAL_STRING("abc", prop);
}
static void test_setcharprop_truncates_long_value() {
  char prop[5];
  SETCHARPROP(prop, "abcdefgh", sizeof(prop));
  TEST_ASSERT_EQUAL_STRING("abcd", prop);
}
static void test_setcharprop_unterminated_source() {
  // A coredump's app_elf_sha256 can be full of hex digits with no terminator.
  // The copy must stop inside the array and come out as a comparable string.
  const char crashed[17] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f','9'};
  char sha[sizeof(crashed)];
  SETCHARPROP(sha, crashed, sizeof(sha));
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef", sha);
  TEST_ASSERT_EQUAL_INT(0, strcmp(sha, "0123456789abcdef"));
}

/*********************************************************************
 * sortArray / sortArrayReverse
 ********************************************************************/
static bool absLarger(int a, int b) { return abs(a) > abs(b); }

static void test_sortArray_ascending() {
  int arr[] = {5, -1, 3, 0, 3};
  const int expected[] = {-1, 0, 3, 3, 5};
  sortArray(arr, 5);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, arr, 5);
}
static void test_sortArrayReverse_descending() {
  int arr[] = {5, -1, 3, 0, 3};
  const int expected[] = {5, 3, 3, 0, -1};
  sortArrayReverse(arr, 5);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, arr, 5);
}
static void test_sortArray_custom_comparator() {
  int arr[] = {-4, 1, 3, -2};
  const int expected[] = {1, -2, 3, -4};
  sortArray(arr, 4, absLarger);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, arr, 4);
  const int reversed[] = {-4, 3, -2, 1};
  sortArrayReverse(arr, 4, absLarger);
  TEST_ASSERT_EQUAL_INT_ARRAY(reversed, arr, 4);
}
static void test_sortArray_empty_and_single() {
  int one[] = {42};
  sortArray(one, 1);
  TEST_ASSERT_EQUAL_INT(42, one[0]);
  int none[] = {7};
  sortArray(none, 0);
  TEST_ASSERT_EQUAL_INT(7, none[0]);
}
static void test_sortArray_moves_free_ids_last() {
  // Free shade id slots are 255 and must end up after the used ones.
  uint8_t ids[] = {3, 255, 1, 255, 2};
  const uint8_t expected[] = {1, 2, 3, 255, 255};
  sortArray<uint8_t>(ids, sizeof(ids));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, ids, sizeof(ids));
}

/*********************************************************************
 * Timestamp
 ********************************************************************/
static struct tm sampleTm() {
  struct tm dt = {};
  dt.tm_year = 2024 - 1900;
  dt.tm_mon = 2;
  dt.tm_mday = 5;
  dt.tm_hour = 7;
  dt.tm_min = 8;
  dt.tm_sec = 9;
  return dt;
}
static void test_formatISO_offsets() {
  Timestamp ts;
  struct tm dt = sampleTm();
  TEST_ASSERT_EQUAL_STRING("2024-03-05T07:08:09.000+0000", ts.formatISO(&dt, 0));
  TEST_ASSERT_EQUAL_STRING("2024-03-05T07:08:09.000-0500", ts.formatISO(&dt, -500));
  TEST_ASSERT_EQUAL_STRING("2024-03-05T07:08:09.000+0530", ts.formatISO(&dt, 530));
}
static void test_calcTZOffset_whole_hours() {
  time_t t = SAMPLE_UTC;
  setTZ("UTC0");
  TEST_ASSERT_EQUAL_INT(0, Timestamp::calcTZOffset(&t));
  setTZ("EST5");
  TEST_ASSERT_EQUAL_INT(-500, Timestamp::calcTZOffset(&t));
}
static void test_calcTZOffset_half_hour_east() {
  time_t t = SAMPLE_UTC;
  setTZ("IST-5:30");
  TEST_ASSERT_EQUAL_INT(530, Timestamp::calcTZOffset(&t));
}
static void test_calcTZOffset_half_hour_west() {
  time_t t = SAMPLE_UTC;
  setTZ("NST3:30");
  TEST_ASSERT_EQUAL_INT(-330, Timestamp::calcTZOffset(&t));
}
static void test_parseUTCTime_in_utc() {
  setTZ("UTC0");
  TEST_ASSERT_EQUAL_INT64((int64_t)SAMPLE_UTC, (int64_t)Timestamp::parseUTCTime("2024-03-05T07:08:09Z"));
}
static void test_parseUTCTime_ignores_local_zone() {
  setTZ("IST-5:30");
  TEST_ASSERT_EQUAL_INT64((int64_t)SAMPLE_UTC, (int64_t)Timestamp::parseUTCTime("2024-03-05T07:08:09Z"));
}

/*********************************************************************
 * rssiNeedsEmit
 ********************************************************************/
static void test_rssiNeedsEmit_channel_change_always_emits() {
  TEST_ASSERT_TRUE(rssiNeedsEmit(-70, -70, 6, 11, 0, 3, 10000));
}
static void test_rssiNeedsEmit_small_change_is_ignored() {
  TEST_ASSERT_FALSE(rssiNeedsEmit(-72, -70, 6, 6, 60000, 3, 10000));
}
static void test_rssiNeedsEmit_real_change_after_interval_emits() {
  TEST_ASSERT_TRUE(rssiNeedsEmit(-73, -70, 6, 6, 10000, 3, 10000));
}
static void test_rssiNeedsEmit_real_change_too_soon_waits() {
  TEST_ASSERT_FALSE(rssiNeedsEmit(-73, -70, 6, 6, 5000, 3, 10000));
}
static void test_rssiNeedsEmit_either_direction() {
  TEST_ASSERT_TRUE(rssiNeedsEmit(-67, -70, 6, 6, 10000, 3, 10000));
  TEST_ASSERT_TRUE(rssiNeedsEmit(-73, -70, 6, 6, 10000, 3, 10000));
}

/*********************************************************************
 * RssiBroadcast
 ********************************************************************/
static RssiBroadcast broadcastAt(int rssi, int channel, uint32_t at) {
  RssiBroadcast b;
  b.sent(true, rssi, channel, at);
  return b;
}
static void test_rssiBroadcast_client_emit_keeps_baseline() {
  // A new client gets the current RSSI right as it drops 5 dBm.  The clients
  // already connected must still get the change.
  RssiBroadcast b = broadcastAt(-60, 6, 0);
  b.sent(false, -65, 6, 20000);
  TEST_ASSERT_EQUAL_INT(-60, b.rssi);
  TEST_ASSERT_EQUAL_UINT32(0, b.at);
  TEST_ASSERT_TRUE(b.due(-65, 6, 20000, 3, 10000));
}
static void test_rssiBroadcast_broadcast_moves_baseline() {
  RssiBroadcast b = broadcastAt(-60, 6, 0);
  b.sent(true, -65, 6, 20000);
  TEST_ASSERT_EQUAL_INT(-65, b.rssi);
  TEST_ASSERT_EQUAL_UINT32(20000, b.at);
  // Measured from the new baseline and too soon after it.
  TEST_ASSERT_FALSE(b.due(-66, 6, 40000, 3, 10000));
  TEST_ASSERT_FALSE(b.due(-70, 6, 25000, 3, 10000));
  TEST_ASSERT_TRUE(b.due(-70, 6, 30000, 3, 10000));
}
static void test_rssiBroadcast_channel_change_is_due() {
  RssiBroadcast b = broadcastAt(-60, 6, 1000);
  TEST_ASSERT_TRUE(b.due(-60, 11, 1000, 3, 10000));
}
static void test_rssiBroadcast_client_emit_on_disconnect_keeps_baseline() {
  RssiBroadcast b = broadcastAt(-60, 6, 0);
  b.sent(false, -100, -1, 5000);
  TEST_ASSERT_EQUAL_INT(6, b.channel);
  TEST_ASSERT_FALSE(b.due(-60, 6, 5000, 3, 10000));
}
static void test_rssiBroadcast_hold_off_across_millis_wrap() {
  RssiBroadcast b = broadcastAt(-60, 6, 0xFFFFF000u);
  TEST_ASSERT_FALSE(b.due(-70, 6, 0x00001000u, 3, 10000));  // 8192ms later
  TEST_ASSERT_TRUE(b.due(-70, 6, 0x00002000u, 3, 10000));   // 12288ms later
}

/*********************************************************************
 * worthRoaming
 ********************************************************************/
static void test_worthRoaming_needs_the_full_gain() {
  TEST_ASSERT_FALSE(worthRoaming(true, -70, -65, 6));
  TEST_ASSERT_TRUE(worthRoaming(true, -70, -64, 6));
  TEST_ASSERT_TRUE(worthRoaming(true, -70, -50, 6));
}
static void test_worthRoaming_never_to_a_weaker_ap() {
  // The scan also lists the AP we are on.
  TEST_ASSERT_FALSE(worthRoaming(true, -70, -70, 6));
  TEST_ASSERT_FALSE(worthRoaming(true, -70, -80, 6));
}
static void test_worthRoaming_any_ap_while_disconnected() {
  TEST_ASSERT_TRUE(worthRoaming(false, 0, -90, 6));
}

/*********************************************************************
 * otaProven
 ********************************************************************/
static void test_otaProven_after_the_minimum_uptime_with_network() {
  TEST_ASSERT_TRUE(otaProven(true, 60000, 60000));
  TEST_ASSERT_TRUE(otaProven(true, 3600000, 60000));
}
static void test_otaProven_not_before_the_minimum_uptime() {
  TEST_ASSERT_FALSE(otaProven(true, 59999, 60000));
}
static void test_otaProven_not_without_network() {
  // A firmware that can't connect would leave the device unreachable.
  TEST_ASSERT_FALSE(otaProven(false, 3600000, 60000));
}

/*********************************************************************
 * releaseAssetName / isFsAsset
 ********************************************************************/
static void test_releaseAssetName_matches_the_release_workflow() {
  // .github/workflows/build.yaml fwname and fsname.
  char buf[64];
  TEST_ASSERT_TRUE(releaseAssetName(buf, sizeof(buf), "ino", "esp32"));
  TEST_ASSERT_EQUAL_STRING("SomfyController.ino.esp32.bin", buf);
  TEST_ASSERT_TRUE(releaseAssetName(buf, sizeof(buf), "littlefs", "esp32s3"));
  TEST_ASSERT_EQUAL_STRING("SomfyController.littlefs.esp32s3.bin", buf);
}
static void test_releaseAssetName_too_small_buffer() {
  char buf[16];
  TEST_ASSERT_FALSE(releaseAssetName(buf, sizeof(buf), "littlefs", "esp32c6"));
  TEST_ASSERT_EQUAL(sizeof(buf) - 1, strlen(buf));
}
static void test_isFsAsset_per_chip_images() {
  TEST_ASSERT_TRUE(isFsAsset("SomfyController.littlefs.esp32.bin"));
  TEST_ASSERT_TRUE(isFsAsset("SomfyController.littlefs.esp32c6.bin"));
  TEST_ASSERT_TRUE(isFsAsset("SomfyController.littlefs.bin"));
}
static void test_isFsAsset_not_the_other_assets() {
  TEST_ASSERT_FALSE(isFsAsset("SomfyController.ino.esp32.bin"));
  TEST_ASSERT_FALSE(isFsAsset("SomfyController.onboard.esp32.bin.zip"));
  TEST_ASSERT_FALSE(isFsAsset("SomfyController.littlefs.bin.sha256"));
  TEST_ASSERT_FALSE(isFsAsset(""));
}

/*********************************************************************
 * wifiAuthModeName
 ********************************************************************/
static void test_wifiAuthModeName_matches_the_idf_enum() {
  TEST_ASSERT_EQUAL_STRING("OPEN", wifiAuthModeName(0));
  TEST_ASSERT_EQUAL_STRING("WPA2", wifiAuthModeName(3));
  TEST_ASSERT_EQUAL_STRING("WPA3", wifiAuthModeName(6));
  TEST_ASSERT_EQUAL_STRING("WPA2/WPA3", wifiAuthModeName(7));
  TEST_ASSERT_EQUAL_STRING("WAPI", wifiAuthModeName(8));
}
static void test_wifiAuthModeName_unknown_past_the_shared_values() {
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", wifiAuthModeName(9));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", wifiAuthModeName(255));
}

/*********************************************************************
 * heapEmitTarget
 ********************************************************************/
static void test_heapEmitTarget_one_client_always_gets_it() {
  TEST_ASSERT_TRUE(heap_emit_t::client == heapEmitTarget(3, 0, 30000, false));
  TEST_ASSERT_TRUE(heap_emit_t::client == heapEmitTarget(3, 60000, 30000, true));
}
static void test_heapEmitTarget_broadcasts_once_per_interval() {
  TEST_ASSERT_TRUE(heap_emit_t::broadcast == heapEmitTarget(255, 30000, 30000, false));
  TEST_ASSERT_TRUE(heap_emit_t::broadcast == heapEmitTarget(255, 30000, 30000, true));
  TEST_ASSERT_TRUE(heap_emit_t::none == heapEmitTarget(255, 29999, 30000, false));
}
static void test_heapEmitTarget_room_in_between() {
  TEST_ASSERT_TRUE(heap_emit_t::room == heapEmitTarget(255, 1000, 30000, true));
}

/*********************************************************************
 * LoopStats
 ********************************************************************/
static void test_loopStats_rate_is_passes_per_second() {
  LoopStats stats;
  for(int i = 0; i < 4500; i++) stats.record(100);
  TEST_ASSERT_EQUAL_UINT32(1500, stats.rate(3000));
}
static void test_loopStats_empty_window_has_no_rate() {
  LoopStats stats;
  stats.record(100);
  TEST_ASSERT_EQUAL_UINT32(0, stats.rate(0));
}
static void test_loopStats_keeps_the_longest_pass() {
  LoopStats stats;
  stats.record(120);
  stats.record(4800);
  stats.record(90);
  TEST_ASSERT_EQUAL_UINT32(4800, stats.maxUs());
}
static void test_loopStats_nothing_published_before_the_first_window() {
  LoopStats stats;
  stats.record(4800);
  TEST_ASSERT_EQUAL_UINT32(0, stats.publishedRate());
  TEST_ASSERT_EQUAL_UINT32(0, stats.publishedMaxUs());
}
static void test_loopStats_publish_keeps_the_window_and_starts_a_new_one() {
  LoopStats stats;
  for(int i = 0; i < 300; i++) stats.record(100);
  stats.record(4800);
  stats.publish(1000);
  TEST_ASSERT_EQUAL_UINT32(301, stats.publishedRate());
  TEST_ASSERT_EQUAL_UINT32(4800, stats.publishedMaxUs());
  TEST_ASSERT_EQUAL_UINT32(0, stats.rate(1000));
  TEST_ASSERT_EQUAL_UINT32(0, stats.maxUs());
  for(int i = 0; i < 50; i++) stats.record(200);
  // The new window started at the publish, not at boot.
  TEST_ASSERT_EQUAL_UINT32(100, stats.rate(1500));
  TEST_ASSERT_EQUAL_UINT32(200, stats.maxUs());
  TEST_ASSERT_EQUAL_UINT32(301, stats.publishedRate());
}
static void test_loopStats_reading_does_not_reset_the_window() {
  LoopStats stats;
  stats.record(4800);
  (void)stats.rate(500);
  (void)stats.maxUs();
  stats.record(100);
  TEST_ASSERT_EQUAL_UINT32(4800, stats.maxUs());
  TEST_ASSERT_EQUAL_UINT32(2, stats.rate(1000));
}
static void test_loopStats_window_across_millis_wrap() {
  LoopStats stats;
  stats.publish(0xFFFFFE00);
  for(int i = 0; i < 1024; i++) stats.record(100);
  // 0x200 ms before the wrap plus 0x200 after it is 1024 ms.
  TEST_ASSERT_EQUAL_UINT32(1000, stats.rate(0x00000200));
}
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_trim_strips_whitespace_and_quotes);
  RUN_TEST(test_trim_keeps_clean_string);
  RUN_TEST(test_trim_all_whitespace_becomes_empty);
  RUN_TEST(test_trim_empty_string);
  RUN_TEST(test_toBoolean_missing_uses_default);
  RUN_TEST(test_toBoolean_true_values);
  RUN_TEST(test_toBoolean_false_values);
  RUN_TEST(test_toBoolean_unknown_uses_default);
  RUN_TEST(test_setcharprop_copies_short_value);
  RUN_TEST(test_setcharprop_truncates_long_value);
  RUN_TEST(test_setcharprop_unterminated_source);
  RUN_TEST(test_sortArray_ascending);
  RUN_TEST(test_sortArrayReverse_descending);
  RUN_TEST(test_sortArray_custom_comparator);
  RUN_TEST(test_sortArray_empty_and_single);
  RUN_TEST(test_sortArray_moves_free_ids_last);
  RUN_TEST(test_formatISO_offsets);
  RUN_TEST(test_calcTZOffset_whole_hours);
  RUN_TEST(test_calcTZOffset_half_hour_east);
  RUN_TEST(test_calcTZOffset_half_hour_west);
  RUN_TEST(test_parseUTCTime_in_utc);
  RUN_TEST(test_parseUTCTime_ignores_local_zone);
  RUN_TEST(test_rssiNeedsEmit_channel_change_always_emits);
  RUN_TEST(test_rssiNeedsEmit_small_change_is_ignored);
  RUN_TEST(test_rssiNeedsEmit_real_change_after_interval_emits);
  RUN_TEST(test_rssiNeedsEmit_real_change_too_soon_waits);
  RUN_TEST(test_rssiNeedsEmit_either_direction);
  RUN_TEST(test_rssiBroadcast_client_emit_keeps_baseline);
  RUN_TEST(test_rssiBroadcast_broadcast_moves_baseline);
  RUN_TEST(test_rssiBroadcast_channel_change_is_due);
  RUN_TEST(test_rssiBroadcast_client_emit_on_disconnect_keeps_baseline);
  RUN_TEST(test_rssiBroadcast_hold_off_across_millis_wrap);
  RUN_TEST(test_worthRoaming_needs_the_full_gain);
  RUN_TEST(test_worthRoaming_never_to_a_weaker_ap);
  RUN_TEST(test_worthRoaming_any_ap_while_disconnected);
  RUN_TEST(test_otaProven_after_the_minimum_uptime_with_network);
  RUN_TEST(test_otaProven_not_before_the_minimum_uptime);
  RUN_TEST(test_otaProven_not_without_network);
  RUN_TEST(test_releaseAssetName_matches_the_release_workflow);
  RUN_TEST(test_releaseAssetName_too_small_buffer);
  RUN_TEST(test_isFsAsset_per_chip_images);
  RUN_TEST(test_isFsAsset_not_the_other_assets);
  RUN_TEST(test_wifiAuthModeName_matches_the_idf_enum);
  RUN_TEST(test_wifiAuthModeName_unknown_past_the_shared_values);
  RUN_TEST(test_heapEmitTarget_one_client_always_gets_it);
  RUN_TEST(test_heapEmitTarget_broadcasts_once_per_interval);
  RUN_TEST(test_heapEmitTarget_room_in_between);
  RUN_TEST(test_loopStats_rate_is_passes_per_second);
  RUN_TEST(test_loopStats_empty_window_has_no_rate);
  RUN_TEST(test_loopStats_keeps_the_longest_pass);
  RUN_TEST(test_loopStats_nothing_published_before_the_first_window);
  RUN_TEST(test_loopStats_publish_keeps_the_window_and_starts_a_new_one);
  RUN_TEST(test_loopStats_reading_does_not_reset_the_window);
  RUN_TEST(test_loopStats_window_across_millis_wrap);
  return UNITY_END();
}
