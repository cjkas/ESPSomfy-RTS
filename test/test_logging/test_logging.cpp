#include <unity.h>
#include "LogLockPolicy.h"
#include "Timing.h"

void setUp() {}
void tearDown() {}

/*********************************************************************
 * keepWaitingForLogLock
 ********************************************************************/
static void assertWaitsUntilMax(LogHolder holder) {
  TEST_ASSERT_TRUE(keepWaitingForLogLock(holder, 0, 20));
  TEST_ASSERT_TRUE(keepWaitingForLogLock(holder, 19, 20));
  TEST_ASSERT_FALSE(keepWaitingForLogLock(holder, 20, 20));
  TEST_ASSERT_FALSE(keepWaitingForLogLock(holder, 1000, 20));
}
static void test_logLock_waits_for_a_running_holder() { assertWaitsUntilMax(LogHolder::running); }
static void test_logLock_waits_for_a_holder_just_taking_it() { assertWaitsUntilMax(LogHolder::unknown); }
static void test_logLock_waits_for_a_holder_preempted_here() { assertWaitsUntilMax(LogHolder::preemptedHere); }
static void test_logLock_gives_up_on_a_holder_preempted_elsewhere() {
  // loop() preempted by the radio task can't let go until the job is done.
  TEST_ASSERT_FALSE(keepWaitingForLogLock(LogHolder::preemptedElsewhere, 0, 20));
  TEST_ASSERT_FALSE(keepWaitingForLogLock(LogHolder::preemptedElsewhere, 1, 20));
}
static void test_logLock_no_wait_allowed() {
  TEST_ASSERT_FALSE(keepWaitingForLogLock(LogHolder::running, 0, 0));
}

/*********************************************************************
 * worthLogging
 ********************************************************************/
static void test_worthLogging_fast_timings_are_skipped() {
  TEST_ASSERT_FALSE(worthLogging(0));
  TEST_ASSERT_FALSE(worthLogging(999));
  TEST_ASSERT_FALSE(worthLogging(TIMING_LOG_MIN_MS * 1000 - 1));
}
static void test_worthLogging_from_the_threshold() {
  TEST_ASSERT_TRUE(worthLogging(TIMING_LOG_MIN_MS * 1000));
  TEST_ASSERT_TRUE(worthLogging(TIMING_SLOW_MS * 1000));
  TEST_ASSERT_TRUE(worthLogging(UINT32_MAX));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_logLock_waits_for_a_running_holder);
  RUN_TEST(test_logLock_waits_for_a_holder_just_taking_it);
  RUN_TEST(test_logLock_waits_for_a_holder_preempted_here);
  RUN_TEST(test_logLock_gives_up_on_a_holder_preempted_elsewhere);
  RUN_TEST(test_logLock_no_wait_allowed);
  RUN_TEST(test_worthLogging_fast_timings_are_skipped);
  RUN_TEST(test_worthLogging_from_the_threshold);
  return UNITY_END();
}
