#include <unity.h>
#include <string.h>
#include <string>
#include "ModelSnapshot.h"

static model_snapshot_t snap;
static char unitBuf[3072];

static room_snapshot_t room(uint8_t id, const char *name) {
  room_snapshot_t r{};
  r.roomId = id;
  strcpy(r.name, name);
  return r;
}
static shade_snapshot_t shade(uint8_t id, const char *name) {
  shade_snapshot_t s{};
  s.shadeId = id;
  s.roomId = 2;
  strcpy(s.name, name);
  s.remoteAddress = 1000 + id;
  s.upTime = 10000;
  s.downTime = 11000;
  s.tiltTime = 7000;
  s.stepSize = 100;
  s.paired = true;
  s.lastRollingCode = 42;
  s.position = 30;
  s.target = 30;
  s.myPos = -1;
  s.tiltType = 2;
  s.tiltPosition = 40;
  s.tiltTarget = 40;
  s.myTiltPos = -1;
  s.shadeType = 1;
  s.bitLength = 56;
  s.inGroup = true;
  s.repeats = 1;
  return s;
}
static group_snapshot_t group(uint8_t id, const char *name) {
  group_snapshot_t g{};
  g.groupId = id;
  strcpy(g.name, name);
  g.remoteAddress = 2000 + id;
  g.lastRollingCode = 5;
  g.bitLength = 80;
  g.repeats = 2;
  return g;
}
static std::string json(void (*write)(JsonFormatter &)) {
  static char buf[2048];
  BufferedJsonFormatter fmt;
  fmt.setBuffer(buf, sizeof(buf));
  fmt.beginObject();
  write(fmt);
  fmt.endObject();
  return std::string(buf, fmt.length());
}
static std::string shadeRefJson(uint8_t id) {
  static uint8_t which;
  which = id;
  return json([](JsonFormatter &f) { writeShadeRef(f, *snap.shade(which)); });
}
static std::string shadeJson(uint8_t id) {
  static uint8_t which;
  which = id;
  return json([](JsonFormatter &f) { writeShade(f, *snap.shade(which)); });
}
static std::string roomJson(uint8_t id) {
  static uint8_t which;
  which = id;
  return json([](JsonFormatter &f) { writeRoom(f, *snap.room(which)); });
}
static std::string groupRefJson(uint8_t id) {
  static uint8_t which;
  which = id;
  return json([](JsonFormatter &f) { writeGroupRef(f, *snap.group(which)); });
}
static void testHeader(JsonFormatter &json, const model_snapshot_t &s) {
  json.addElem("startingAddress", s.startingAddress);
}
// The whole document, taken chunkSize bytes at a time like the web server does.
static std::string document(snapshot_doc_t doc, uint8_t id = 0, size_t chunkSize = 7, size_t unitSize = sizeof(unitBuf)) {
  SnapshotWriter writer(snap, doc, id, testHeader, unitBuf, unitSize);
  std::string out;
  uint8_t chunk[64];
  for(int guard = 0; guard < 100000; guard++) {
    const size_t n = writer.generate(chunk, chunkSize);
    if(n == 0) break;
    out.append(reinterpret_cast<char *>(chunk), n);
  }
  return out;
}

void setUp() {
  snap = model_snapshot_t{};
  snap.startingAddress = 77;
  snap.rooms[snap.roomCount++] = room(2, "Living");
  snap.rooms[snap.roomCount++] = room(3, "Bed");
  snap.shades[snap.shadeCount++] = shade(1, "Kitchen");
  snap.shades[snap.shadeCount++] = shade(4, "Porch");
  group_snapshot_t g = group(6, "All");
  g.linkedShades[g.linkedShadeCount++] = 4;
  g.linkedShades[g.linkedShadeCount++] = 9;   // gone from the model
  g.linkedShades[g.linkedShadeCount++] = 1;
  snap.groups[snap.groupCount++] = g;
  snap.repeaters[snap.repeaterCount++] = 12345;
  snap.repeaters[snap.repeaterCount++] = 678;
}
void tearDown() {}

/*********************************************************************
 * Items, as the routes have always written them
 ********************************************************************/
static void test_a_shade() {
  snap.shades[0].linkedRemotes[snap.shades[0].linkedRemoteCount++] = { 5555, 7 };
  TEST_ASSERT_EQUAL_STRING(
    "{\"shadeId\":1,\"roomId\":2,\"name\":\"Kitchen\",\"remoteAddress\":1001,\"upTime\":10000,\"downTime\":11000,"
    "\"paired\":true,\"lastRollingCode\":42,\"position\":30,\"tiltType\":2,\"tiltPosition\":40,\"tiltDirection\":0,"
    "\"tiltTime\":7000,\"stepSize\":100,\"tiltTarget\":40,\"target\":30,\"myPos\":-1,\"myTiltPos\":-1,\"direction\":0,"
    "\"shadeType\":1,\"bitLength\":56,\"proto\":0,\"flags\":0,\"flipCommands\":false,\"flipPosition\":false,"
    "\"inGroup\":true,\"sunSensor\":false,\"light\":false,\"repeats\":1,\"sortOrder\":0,\"gpioUp\":0,\"gpioDown\":0,"
    "\"gpioMy\":0,\"gpioLLTrigger\":false,\"simMy\":false,\"linkedRemotes\":[{\"remoteAddress\":5555,\"lastRollingCode\":7}]}",
    shadeJson(1).c_str());
}
static void test_a_shade_ref() {
  TEST_ASSERT_EQUAL_STRING(
    "{\"shadeId\":1,\"roomId\":2,\"name\":\"Kitchen\",\"remoteAddress\":1001,\"paired\":true,\"shadeType\":1,"
    "\"flipCommands\":false,\"flipPosition\":false,\"bitLength\":56,\"proto\":0,\"flags\":0,\"sunSensor\":false,"
    "\"hasLight\":false,\"repeats\":1}",
    shadeRefJson(1).c_str());
}
static void test_a_room() {
  TEST_ASSERT_EQUAL_STRING("{\"roomId\":2,\"name\":\"Living\",\"sortOrder\":0}", roomJson(2).c_str());
}
static void test_a_group_ref() {
  TEST_ASSERT_EQUAL_STRING(
    "{\"groupId\":6,\"roomId\":0,\"name\":\"All\",\"remoteAddress\":2006,\"lastRollingCode\":5,\"bitLength\":80,"
    "\"proto\":0,\"sunSensor\":false,\"flipCommands\":false,\"flags\":0,\"repeats\":2,\"sortOrder\":0}",
    groupRefJson(6).c_str());
}
static void test_names_are_escaped() {
  strcpy(snap.rooms[0].name, "A \"big\" room");
  TEST_ASSERT_EQUAL_STRING("{\"roomId\":2,\"name\":\"A \\\"big\\\" room\",\"sortOrder\":0}", roomJson(2).c_str());
}

/*********************************************************************
 * Documents
 ********************************************************************/
static std::string groupWithLinked() {
  std::string g = groupRefJson(6);
  g.pop_back();  // reopen it for linkedShades
  return g + ",\"linkedShades\":[" + shadeRefJson(4) + "," + shadeRefJson(1) + "]}";
}
static void test_rooms_list() {
  TEST_ASSERT_EQUAL_STRING(("[" + roomJson(2) + "," + roomJson(3) + "]").c_str(), document(snapshot_doc_t::rooms).c_str());
}
static void test_shades_list() {
  TEST_ASSERT_EQUAL_STRING(("[" + shadeJson(1) + "," + shadeJson(4) + "]").c_str(), document(snapshot_doc_t::shades).c_str());
}
static void test_groups_list_skips_a_linked_shade_that_is_gone() {
  TEST_ASSERT_EQUAL_STRING(("[" + groupWithLinked() + "]").c_str(), document(snapshot_doc_t::groups).c_str());
}
static void test_empty_lists() {
  snap = model_snapshot_t{};
  TEST_ASSERT_EQUAL_STRING("[]", document(snapshot_doc_t::rooms).c_str());
  TEST_ASSERT_EQUAL_STRING("[]", document(snapshot_doc_t::shades).c_str());
  TEST_ASSERT_EQUAL_STRING("[]", document(snapshot_doc_t::groups).c_str());
}
static void test_controller() {
  const std::string expected = "{\"startingAddress\":77,\"rooms\":[" + roomJson(2) + "," + roomJson(3) +
    "],\"shades\":[" + shadeJson(1) + "," + shadeJson(4) +
    "],\"groups\":[" + groupWithLinked() +
    "],\"repeaters\":[12345,678]}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), document(snapshot_doc_t::controller).c_str());
}
static void test_discovery_has_no_repeaters() {
  const std::string expected = "{\"startingAddress\":77,\"rooms\":[" + roomJson(2) + "," + roomJson(3) +
    "],\"shades\":[" + shadeJson(1) + "," + shadeJson(4) +
    "],\"groups\":[" + groupWithLinked() + "]}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), document(snapshot_doc_t::discovery).c_str());
}
static void test_controller_of_an_empty_model() {
  snap = model_snapshot_t{};
  TEST_ASSERT_EQUAL_STRING("{\"startingAddress\":0,\"rooms\":[],\"shades\":[],\"groups\":[],\"repeaters\":[]}",
    document(snapshot_doc_t::controller).c_str());
}
static void test_chunk_size_does_not_change_the_document() {
  const std::string whole = document(snapshot_doc_t::controller, 0, 64);
  for(size_t n = 1; n < 20; n++)
    TEST_ASSERT_EQUAL_STRING(whole.c_str(), document(snapshot_doc_t::controller, 0, n).c_str());
}
static void test_single_items() {
  TEST_ASSERT_EQUAL_STRING(roomJson(3).c_str(), document(snapshot_doc_t::room, 3).c_str());
  TEST_ASSERT_EQUAL_STRING(shadeJson(4).c_str(), document(snapshot_doc_t::shade, 4).c_str());
  TEST_ASSERT_EQUAL_STRING(groupWithLinked().c_str(), document(snapshot_doc_t::group, 6).c_str());
}
static void test_a_single_item_gone_by_the_snapshot_is_an_error() {
  TEST_ASSERT_EQUAL_STRING("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}", document(snapshot_doc_t::shade, 9).c_str());
  TEST_ASSERT_EQUAL_STRING("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}", document(snapshot_doc_t::group, 9).c_str());
  TEST_ASSERT_EQUAL_STRING("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}", document(snapshot_doc_t::room, 9).c_str());
}
static void test_an_item_too_big_for_the_unit_is_left_out() {
  // Room JSON is about 40 bytes and a shade's well over 300, so a 200 byte
  // unit takes the rooms and leaves the shades out, keeping the JSON valid.
  TEST_ASSERT_EQUAL_STRING(("[" + roomJson(2) + "," + roomJson(3) + "]").c_str(),
    document(snapshot_doc_t::rooms, 0, 7, 200).c_str());
  TEST_ASSERT_EQUAL_STRING("[]", document(snapshot_doc_t::shades, 0, 7, 200).c_str());
}
static void test_a_left_out_item_does_not_leave_a_stray_comma() {
  // Give the first shade every linked remote so it no longer fits a unit the
  // second one does.
  snap.shades[0].linkedRemoteCount = SOMFY_MAX_LINKED_REMOTES;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) snap.shades[0].linkedRemotes[i] = { 4000000000u, 65535 };
  const size_t fits = shadeJson(4).length() + 8;
  TEST_ASSERT_TRUE(shadeJson(1).length() > fits);
  TEST_ASSERT_EQUAL_STRING(("[" + shadeJson(4) + "]").c_str(), document(snapshot_doc_t::shades, 0, 7, fits).c_str());
}

/*********************************************************************
 * Repeaters, what a new item would get, and group options
 ********************************************************************/
static void test_repeaters_list() {
  TEST_ASSERT_EQUAL_STRING("[12345,678]", document(snapshot_doc_t::repeaters).c_str());
}
static void test_next_room_fills_the_lowest_gap() {
  TEST_ASSERT_EQUAL_STRING("{\"roomId\":1}", document(snapshot_doc_t::nextRoom).c_str());
}
static void test_next_shade() {
  // Ids 1 and 4 are taken, so 2; its address is startingAddress + id unless taken.
  snap.radioBitLength = 80;
  snap.radioProto = 1;
  TEST_ASSERT_EQUAL_STRING("{\"shadeId\":2,\"remoteAddress\":79,\"bitLength\":80,\"stepSize\":100,\"proto\":1}",
    document(snapshot_doc_t::nextShade).c_str());
}
static void test_next_address_skips_one_already_used() {
  snap.shades[0].remoteAddress = 79;  // what shade 2 would get
  snap.shades[1].remoteAddress = 80;
  TEST_ASSERT_EQUAL_UINT32(81, nextRemoteAddress(snap, 2));
}
static void test_next_group() {
  TEST_ASSERT_EQUAL_STRING("{\"groupId\":1,\"remoteAddress\":78,\"bitLength\":0,\"proto\":0}",
    document(snapshot_doc_t::nextGroup).c_str());
}
static void test_no_room_for_another_shade() {
  snap.shadeCount = 0;
  for(uint8_t id = 1; id <= SOMFY_MAX_SHADES - 2; id++) snap.shades[snap.shadeCount++] = shade(id, "S");
  TEST_ASSERT_EQUAL_UINT8(255, nextShadeId(snap));
}
static void test_group_options_lists_the_shades_not_in_it() {
  snap.shades[snap.shadeCount++] = shade(7, "Spare");
  const std::string expected = groupWithLinked().substr(0, groupWithLinked().length() - 1) +
    ",\"availShades\":[" + shadeRefJson(7) + "]}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), document(snapshot_doc_t::groupOptions, 6).c_str());
}
static void test_group_options_of_a_group_gone_by_the_snapshot() {
  TEST_ASSERT_EQUAL_STRING("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}",
    document(snapshot_doc_t::groupOptions, 9).c_str());
}

/*********************************************************************
 * Slots
 ********************************************************************/
static SnapshotSlots slots;
static int fills = 0;
static void countFill(model_snapshot_t &s) {
  fills++;
  s.shadeCount = 1;
}

static void test_two_reads_at_once_and_no_more() {
  const int8_t a = slots.acquire();
  const int8_t b = slots.acquire();
  TEST_ASSERT_TRUE(a >= 0 && b >= 0 && a != b);
  TEST_ASSERT_EQUAL_INT8(-1, slots.acquire());
  slots.cancel(a);
  slots.cancel(b);
}
static void test_a_read_is_filled_then_freed() {
  fills = 0;
  const int8_t s = slots.acquire();
  TEST_ASSERT_FALSE(slots.filled(s));
  slots.fill(s, countFill);
  TEST_ASSERT_TRUE(slots.filled(s));
  TEST_ASSERT_EQUAL_UINT8(1, slots.snapshot(s).shadeCount);
  slots.release(s);
  TEST_ASSERT_EQUAL_INT8(s, slots.acquire());
  slots.cancel(s);
  TEST_ASSERT_EQUAL_INT(1, fills);
}
static void test_a_read_gone_before_its_fill_is_not_filled() {
  fills = 0;
  const int8_t s = slots.acquire();
  const int8_t other = slots.acquire();
  slots.release(s);
  // Not free yet: loop() still has the fill queued.
  TEST_ASSERT_EQUAL_INT8(-1, slots.acquire());
  slots.fill(s, countFill);
  TEST_ASSERT_EQUAL_INT(0, fills);
  TEST_ASSERT_EQUAL_INT8(s, slots.acquire());
  slots.cancel(s);
  slots.cancel(other);
}
static void test_a_cancelled_slot_is_free_again() {
  const int8_t s = slots.acquire();
  slots.cancel(s);
  TEST_ASSERT_EQUAL_INT8(s, slots.acquire());
  slots.cancel(s);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_shade);
  RUN_TEST(test_a_shade_ref);
  RUN_TEST(test_a_room);
  RUN_TEST(test_a_group_ref);
  RUN_TEST(test_names_are_escaped);
  RUN_TEST(test_rooms_list);
  RUN_TEST(test_shades_list);
  RUN_TEST(test_groups_list_skips_a_linked_shade_that_is_gone);
  RUN_TEST(test_empty_lists);
  RUN_TEST(test_controller);
  RUN_TEST(test_discovery_has_no_repeaters);
  RUN_TEST(test_controller_of_an_empty_model);
  RUN_TEST(test_chunk_size_does_not_change_the_document);
  RUN_TEST(test_single_items);
  RUN_TEST(test_a_single_item_gone_by_the_snapshot_is_an_error);
  RUN_TEST(test_an_item_too_big_for_the_unit_is_left_out);
  RUN_TEST(test_a_left_out_item_does_not_leave_a_stray_comma);
  RUN_TEST(test_repeaters_list);
  RUN_TEST(test_next_room_fills_the_lowest_gap);
  RUN_TEST(test_next_shade);
  RUN_TEST(test_next_address_skips_one_already_used);
  RUN_TEST(test_next_group);
  RUN_TEST(test_no_room_for_another_shade);
  RUN_TEST(test_group_options_lists_the_shades_not_in_it);
  RUN_TEST(test_group_options_of_a_group_gone_by_the_snapshot);
  RUN_TEST(test_two_reads_at_once_and_no_more);
  RUN_TEST(test_a_read_is_filled_then_freed);
  RUN_TEST(test_a_read_gone_before_its_fill_is_not_filled);
  RUN_TEST(test_a_cancelled_slot_is_free_again);
  return UNITY_END();
}
