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
  return UNITY_END();
}
