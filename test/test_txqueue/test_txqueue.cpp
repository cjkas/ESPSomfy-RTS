#include <unity.h>
#include "TxQueuePolicy.h"
#include "TxBuffer.h"

static const uint32_t ADDR = 0x123456;

// A frame as SomfyRemote::sendCommandAfter builds it.
static somfy_frame_t makeFrame(somfy_commands cmd, uint8_t bitLength = 56) {
  somfy_frame_t f;
  f.cmd = cmd;
  f.remoteAddress = ADDR;
  f.rollingCode = 0x0102;
  f.encKey = 0xA2;
  f.bitLength = bitLength;
  f.valid = true;
  return f;
}

void setUp() {}
void tearDown() {}

/*********************************************************************
 * txAdmits
 ********************************************************************/
static void test_normal_job_leaves_the_reserved_slots() {
  TEST_ASSERT_FALSE(txAdmits(0, false));
  TEST_ASSERT_FALSE(txAdmits(TX_RESERVED_SLOTS, false));
  TEST_ASSERT_TRUE(txAdmits(TX_RESERVED_SLOTS + 1, false));
  TEST_ASSERT_TRUE(txAdmits(TX_JOB_QUEUE_SIZE, false));
}
static void test_priority_job_uses_the_reserved_slots() {
  TEST_ASSERT_FALSE(txAdmits(0, true));
  TEST_ASSERT_TRUE(txAdmits(1, true));
  TEST_ASSERT_TRUE(txAdmits(TX_RESERVED_SLOTS, true));
}

/*********************************************************************
 * Sequence numbers
 ********************************************************************/
static void test_nextTxSeq_counts_up() {
  TEST_ASSERT_EQUAL_UINT32(1, nextTxSeq(0));
  TEST_ASSERT_EQUAL_UINT32(2, nextTxSeq(1));
}
static void test_nextTxSeq_skips_zero_on_wrap() {
  TEST_ASSERT_EQUAL_UINT32(1, nextTxSeq(0xFFFFFFFFu));
}
static void test_txSeqQueued_zero_is_never_queued() {
  TEST_ASSERT_FALSE(txSeqQueued(0, 0));
  TEST_ASSERT_FALSE(txSeqQueued(0, 10));
  TEST_ASSERT_FALSE(txSeqQueued(0, 0xFFFFFFF0u));
}
static void test_txSeqQueued_after_taken() {
  TEST_ASSERT_FALSE(txSeqQueued(9, 10));
  TEST_ASSERT_FALSE(txSeqQueued(10, 10));
  TEST_ASSERT_TRUE(txSeqQueued(11, 10));
}
static void test_txSeqQueued_across_wrap() {
  // Job 5 was numbered after the counter wrapped, so it is still waiting.
  TEST_ASSERT_TRUE(txSeqQueued(5, 0xFFFFFFF0u));
  TEST_ASSERT_TRUE(txSeqQueued(0xFFFFFFFFu, 0xFFFFFFF0u));
  TEST_ASSERT_FALSE(txSeqQueued(0xFFFFFFF0u, 5));
}
static void test_txSeqAfter_never_moves_back() {
  // A flush marks every job up to 20 as taken.  The radio task then reports
  // job 15, which it took off the queue before the flush.  That must not move
  // the mark back or jobs 16-20 would look queued again.
  uint32_t taken = 20;
  if(txSeqAfter(15, taken)) taken = 15;
  TEST_ASSERT_EQUAL_UINT32(20, taken);
  if(txSeqAfter(21, taken)) taken = 21;
  TEST_ASSERT_EQUAL_UINT32(21, taken);
  TEST_ASSERT_FALSE(txSeqAfter(21, 21));
}

/*********************************************************************
 * stopGoesToFront
 ********************************************************************/
static void test_stop_goes_to_front_when_nothing_waits() {
  TEST_ASSERT_TRUE(stopGoesToFront(true, false));
}
static void test_stop_queues_behind_its_remote() {
  // Overtaking the remote's waiting frame would send the rolling codes out of
  // order.
  TEST_ASSERT_FALSE(stopGoesToFront(true, true));
}
static void test_normal_job_never_goes_to_front() {
  TEST_ASSERT_FALSE(stopGoesToFront(false, false));
  TEST_ASSERT_FALSE(stopGoesToFront(false, true));
}

/*********************************************************************
 * txDueInMs
 ********************************************************************/
static void test_txDueInMs() {
  TEST_ASSERT_EQUAL_INT32(500, txDueInMs(1500, 1000));
  TEST_ASSERT_EQUAL_INT32(0, txDueInMs(1000, 1000));
  TEST_ASSERT_EQUAL_INT32(-200, txDueInMs(1000, 1200));
}
static void test_txDueInMs_across_millis_wrap() {
  // Queued 0x100ms before the wrap with a 0x200ms delay.
  const uint32_t notBefore = 0xFFFFFF00u + 0x200u;
  TEST_ASSERT_EQUAL_INT32(0x200, txDueInMs(notBefore, 0xFFFFFF00u));
  TEST_ASSERT_EQUAL_INT32(0x100, txDueInMs(notBefore, 0));
  TEST_ASSERT_EQUAL_INT32(-0x10, txDueInMs(notBefore, 0x110u));
}

/*********************************************************************
 * encodeTxJob
 ********************************************************************/
static void test_encode_56bit_job() {
  somfy_frame_t f = makeFrame(somfy_commands::Up);
  somfy_tx_job_t job;
  encodeTxJob(job, f, 3);
  byte expected[10] = {};
  somfy_frame_t copy = makeFrame(somfy_commands::Up);
  copy.encodeFrame(expected);
  TEST_ASSERT_EQUAL_UINT8(2, job.tx.hwsync);
  TEST_ASSERT_EQUAL_UINT8(56, job.tx.bit_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, job.tx.payload, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT8(3, job.repeats);
}
static void test_encode_80bit_job() {
  somfy_frame_t f = makeFrame(somfy_commands::Down, 80);
  somfy_tx_job_t job;
  encodeTxJob(job, f, 1);
  byte expected[10] = {};
  somfy_frame_t copy = makeFrame(somfy_commands::Down, 80);
  copy.encodeFrame(expected);
  TEST_ASSERT_EQUAL_UINT8(12, job.tx.hwsync);
  TEST_ASSERT_EQUAL_UINT8(80, job.tx.bit_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, job.tx.payload, sizeof(expected));
}
static void test_encode_job_copies_the_frame() {
  somfy_frame_t f = makeFrame(somfy_commands::My);
  somfy_tx_job_t job;
  encodeTxJob(job, f, 1);
  TEST_ASSERT_EQUAL_UINT32(ADDR, job.frame.remoteAddress);
  TEST_ASSERT_EQUAL_UINT16(0x0102, job.frame.rollingCode);
  // The radio task uses its own copy, so later changes to the remote's frame
  // don't reach it.
  f.rollingCode = 0x0200;
  TEST_ASSERT_EQUAL_UINT16(0x0102, job.frame.rollingCode);
}
static void test_encode_job_limits_repeats() {
  somfy_frame_t f = makeFrame(somfy_commands::My);
  somfy_tx_job_t job;
  encodeTxJob(job, f, TX_MAX_REPEATS);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS, job.repeats);
  encodeTxJob(job, f, TX_MAX_REPEATS + 1);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS, job.repeats);
  encodeTxJob(job, f, 255);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS, job.repeats);
  encodeTxJob(job, f, 0);
  TEST_ASSERT_EQUAL_UINT8(0, job.repeats);
}

/*********************************************************************
 * Repeat jobs
 ********************************************************************/
static void test_repeat_job_counts_on_from_the_frame() {
  somfy_frame_t f = makeFrame(somfy_commands::Up, 80);
  f.repeats = 4;
  somfy_tx_job_t job;
  encodeRepeatJob(job, f, 2);
  TEST_ASSERT_EQUAL_UINT8(6, job.firstRepeat);
  TEST_ASSERT_EQUAL_UINT8(2, job.repeats);
  // Nothing moves until the radio takes the job.
  TEST_ASSERT_EQUAL_UINT8(4, f.repeats);
}
static void test_repeat_queued_advances_the_counter() {
  somfy_frame_t f = makeFrame(somfy_commands::Up, 80);
  f.repeats = 4;
  somfy_tx_job_t job;
  encodeRepeatJob(job, f, 2);
  noteRepeatQueued(f, job);
  // The first frame plus two repeats went out.
  TEST_ASSERT_EQUAL_UINT8(7, f.repeats);
  // The next repeat job picks up where this one stops.
  somfy_tx_job_t next;
  encodeRepeatJob(next, f, 1);
  TEST_ASSERT_EQUAL_UINT8(9, next.firstRepeat);
}
static void test_repeat_queued_uses_the_limited_repeats() {
  somfy_frame_t f = makeFrame(somfy_commands::Up);
  somfy_tx_job_t job;
  encodeRepeatJob(job, f, 200);
  noteRepeatQueued(f, job);
  TEST_ASSERT_EQUAL_UINT8(TX_MAX_REPEATS + 1, f.repeats);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_normal_job_leaves_the_reserved_slots);
  RUN_TEST(test_priority_job_uses_the_reserved_slots);
  RUN_TEST(test_nextTxSeq_counts_up);
  RUN_TEST(test_nextTxSeq_skips_zero_on_wrap);
  RUN_TEST(test_txSeqQueued_zero_is_never_queued);
  RUN_TEST(test_txSeqQueued_after_taken);
  RUN_TEST(test_txSeqQueued_across_wrap);
  RUN_TEST(test_txSeqAfter_never_moves_back);
  RUN_TEST(test_stop_goes_to_front_when_nothing_waits);
  RUN_TEST(test_stop_queues_behind_its_remote);
  RUN_TEST(test_normal_job_never_goes_to_front);
  RUN_TEST(test_txDueInMs);
  RUN_TEST(test_txDueInMs_across_millis_wrap);
  RUN_TEST(test_encode_56bit_job);
  RUN_TEST(test_encode_80bit_job);
  RUN_TEST(test_encode_job_copies_the_frame);
  RUN_TEST(test_encode_job_limits_repeats);
  RUN_TEST(test_repeat_job_counts_on_from_the_frame);
  RUN_TEST(test_repeat_queued_advances_the_counter);
  RUN_TEST(test_repeat_queued_uses_the_limited_repeats);
  return UNITY_END();
}
