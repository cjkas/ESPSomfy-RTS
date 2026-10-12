#include <unity.h>
#include "LogLockPolicy.h"

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

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_logLock_waits_for_a_running_holder);
  RUN_TEST(test_logLock_waits_for_a_holder_just_taking_it);
  RUN_TEST(test_logLock_waits_for_a_holder_preempted_here);
  RUN_TEST(test_logLock_gives_up_on_a_holder_preempted_elsewhere);
  RUN_TEST(test_logLock_no_wait_allowed);
  return UNITY_END();
}
