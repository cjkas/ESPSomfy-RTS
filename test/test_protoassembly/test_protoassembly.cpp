#include <unity.h>
#include <string.h>
#include "ProtoAssembly.h"

// Putting protocol 2 messages back together from their pieces: one message at
// a time, one reply-worthy result per message.

void setUp() {}
void tearDown() {}

static proto_piece_t piece(uint32_t client, const char *text, bool first, bool last, bool isText = true) {
  proto_piece_t p;
  p.clientId = client;
  p.first = first;
  p.last = last;
  p.text = isText;
  p.data = reinterpret_cast<const uint8_t *>(text);
  p.len = strlen(text);
  return p;
}

static void test_pieces_make_the_message() {
  MessageAssembler a(64);
  TEST_ASSERT_EQUAL(assembly_t::more, a.add(piece(1, "{\"v\":2,", true, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::more, a.add(piece(1, "\"t\":3", false, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "}", false, true), 0));
  TEST_ASSERT_EQUAL_STRING("{\"v\":2,\"t\":3}", a.text());
  TEST_ASSERT_EQUAL_UINT32(13, a.length());
  a.done();
  TEST_ASSERT_FALSE(a.holding());
}
static void test_nothing_is_held_between_messages() {
  MessageAssembler a(64);
  TEST_ASSERT_FALSE(a.holding());
  a.add(piece(1, "ab", true, false), 0);
  TEST_ASSERT_TRUE(a.holding());
  a.add(piece(1, "c", false, true), 0);
  a.done();
  TEST_ASSERT_FALSE(a.holding());
  TEST_ASSERT_NULL(a.text());
}
static void test_another_clients_message_waits() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  // One busy for the other client's message, then its pieces are let go.
  TEST_ASSERT_EQUAL(assembly_t::busy, a.add(piece(2, "xy", true, false), 10));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(2, "z", false, true), 10));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "c", false, true), 20));
  TEST_ASSERT_EQUAL_STRING("abc", a.text());
}
static void test_too_large_is_said_once() {
  MessageAssembler a(4);
  TEST_ASSERT_EQUAL(assembly_t::more, a.add(piece(1, "abc", true, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::tooLarge, a.add(piece(1, "de", false, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(1, "f", false, true), 0));
  TEST_ASSERT_FALSE(a.holding());
}
static void test_a_binary_message_is_refused_once() {
  MessageAssembler a(64);
  TEST_ASSERT_EQUAL(assembly_t::notText, a.add(piece(1, "ab", true, false, false), 0));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(1, "c", false, true, false), 0));
}
static void test_a_stalled_message_gives_the_buffer_up() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  TEST_ASSERT_EQUAL(assembly_t::busy, a.add(piece(2, "x", true, true), PROTO_ASSEMBLY_TIMEOUT_MS - 1));
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(2, "x", true, true), PROTO_ASSEMBLY_TIMEOUT_MS));
  a.done();
  // The stalled client is told once, when its next piece comes.
  TEST_ASSERT_EQUAL(assembly_t::timedOut, a.add(piece(1, "c", false, false), PROTO_ASSEMBLY_TIMEOUT_MS + 1));
  TEST_ASSERT_EQUAL(assembly_t::ignored, a.add(piece(1, "d", false, true), PROTO_ASSEMBLY_TIMEOUT_MS + 1));
}
static void test_a_client_that_leaves_lets_go() {
  MessageAssembler a(64);
  a.add(piece(1, "ab", true, false), 0);
  a.drop(1);
  TEST_ASSERT_FALSE(a.holding());
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(2, "xy", true, true), 1));
}
static void test_a_message_of_one_piece_is_whole() {
  // A whole binary frame, or one piece too large for the rest of the code.
  MessageAssembler a(64);
  TEST_ASSERT_EQUAL(assembly_t::complete, a.add(piece(1, "{}", true, true), 0));
  TEST_ASSERT_EQUAL_STRING("{}", a.text());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_pieces_make_the_message);
  RUN_TEST(test_nothing_is_held_between_messages);
  RUN_TEST(test_another_clients_message_waits);
  RUN_TEST(test_too_large_is_said_once);
  RUN_TEST(test_a_binary_message_is_refused_once);
  RUN_TEST(test_a_stalled_message_gives_the_buffer_up);
  RUN_TEST(test_a_client_that_leaves_lets_go);
  RUN_TEST(test_a_message_of_one_piece_is_whole);
  return UNITY_END();
}
