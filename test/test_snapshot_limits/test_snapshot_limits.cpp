#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "ModelSnapshot.h"

// The read routes format each room, shade, group and linked shade into a
// SNAPSHOT_UNIT_SIZE unit and leave out one that doesn't fit, without telling
// the client (KNOWN_ISSUES.md).  These tests fill the snapshot with the
// largest possible items and check every document comes out whole.

static model_snapshot_t snap;
static char unitBuf[SNAPSHOT_UNIT_SIZE];
// Big enough for any item, so nothing is ever left out with it.
static char bigUnit[64 * 1024];

// The widest name: 20 control characters, each escaped to \u00XX.
static void widestName(char *name) {
  memset(name, 0x01, 20);
  name[20] = '\0';
}
static void fill() {
  snap = model_snapshot_t{};
  snap.startingAddress = 0xFFFFFFFF;
  snap.radioBitLength = 255;
  snap.radioProto = 255;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    room_snapshot_t &r = snap.rooms[snap.roomCount++];
    r.roomId = i + 1;
    widestName(r.name);
    r.sortOrder = -128;
  }
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    shade_snapshot_t &s = snap.shades[snap.shadeCount++];
    s.shadeId = i + 1;
    s.roomId = 255;
    widestName(s.name);
    s.remoteAddress = s.upTime = s.downTime = s.tiltTime = 0xFFFFFFFF;
    s.stepSize = s.lastRollingCode = 0xFFFF;
    s.position = s.target = s.myPos = s.direction = -128;
    s.tiltPosition = s.tiltTarget = s.myTiltPos = s.tiltDirection = -128;
    s.tiltType = s.shadeType = s.bitLength = s.proto = s.flags = s.repeats = 255;
    s.gpioUp = s.gpioDown = s.gpioMy = 255;
    s.sortOrder = -128;
    s.paired = s.flipCommands = s.flipPosition = s.inGroup = false;
    s.sunSensor = s.light = s.simMy = s.gpioLLTrigger = false;
    s.linkedRemoteCount = SOMFY_MAX_LINKED_REMOTES;
    for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) s.linkedRemotes[j] = {0xFFFFFFFF, 0xFFFF};
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    group_snapshot_t &g = snap.groups[snap.groupCount++];
    g.groupId = i + 1;
    g.roomId = 255;
    widestName(g.name);
    g.remoteAddress = 0xFFFFFFFF;
    g.lastRollingCode = 0xFFFF;
    g.bitLength = g.proto = g.flags = g.repeats = 255;
    g.sortOrder = -128;
    g.sunSensor = g.flipCommands = false;
    g.linkedShadeCount = SOMFY_MAX_GROUPED_SHADES;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) g.linkedShades[j] = j + 1;
  }
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) snap.repeaters[snap.repeaterCount++] = 0xFFFFFFFF;
}
// The device details aren't in the snapshot (Web.cpp writes them), so the test
// header writes nothing.
static void noHeader(JsonFormatter &, const model_snapshot_t &) {}
static std::string document(snapshot_doc_t doc, uint8_t id, char *unit, size_t unitSize) {
  SnapshotWriter writer(snap, doc, id, noHeader, unit, unitSize);
  std::string out;
  uint8_t chunk[1460];
  for(int guard = 0; guard < 100000; guard++) {
    const size_t n = writer.generate(chunk, sizeof(chunk));
    if(n == 0) break;
    out.append(reinterpret_cast<char *>(chunk), n);
  }
  return out;
}
// The document as the device writes it is the one nothing is left out of.
static void assertWhole(snapshot_doc_t doc, uint8_t id = 0) {
  const std::string full = document(doc, id, bigUnit, sizeof(bigUnit));
  const std::string device = document(doc, id, unitBuf, sizeof(unitBuf));
  TEST_ASSERT_TRUE(full.length() > 2);
  TEST_ASSERT_EQUAL_UINT32(full.length(), device.length());
  TEST_ASSERT_TRUE(full == device);
}
// How big an item is, formatted as the writer does on its own.
static size_t itemSize(void (*write)(JsonFormatter &)) {
  BufferedJsonFormatter fmt;
  fmt.setBuffer(bigUnit, sizeof(bigUnit));
  write(fmt);
  return fmt.length();
}

void setUp() { fill(); }
void tearDown() {}

static void test_largest_items_fit_the_unit() {
  const size_t room = itemSize([](JsonFormatter &j) { j.beginObject(); writeRoom(j, snap.rooms[0]); j.endObject(); });
  const size_t shade = itemSize([](JsonFormatter &j) { j.beginObject(); writeShade(j, snap.shades[0]); j.endObject(); });
  const size_t shadeRef = itemSize([](JsonFormatter &j) { j.beginObject(); writeShadeRef(j, snap.shades[0]); j.endObject(); });
  const size_t group = itemSize([](JsonFormatter &j) { j.beginObject(); writeGroupRef(j, snap.groups[0]); j.beginArray("linkedShades"); });
  char msg[160];
  snprintf(msg, sizeof(msg), "largest items in a %u-byte unit: room %u, shade %u, linked shade %u, group %u",
      (unsigned)SNAPSHOT_UNIT_SIZE, (unsigned)room, (unsigned)shade, (unsigned)shadeRef, (unsigned)group);
  TEST_MESSAGE(msg);
  // One byte for the comma before an item, one for the end of the string.
  TEST_ASSERT_TRUE(room + 2 <= SNAPSHOT_UNIT_SIZE);
  TEST_ASSERT_TRUE(shade + 2 <= SNAPSHOT_UNIT_SIZE);
  TEST_ASSERT_TRUE(shadeRef + 2 <= SNAPSHOT_UNIT_SIZE);
  TEST_ASSERT_TRUE(group + 2 <= SNAPSHOT_UNIT_SIZE);
}
static void test_controller_is_whole() { assertWhole(snapshot_doc_t::controller); }
static void test_discovery_is_whole() { assertWhole(snapshot_doc_t::discovery); }
static void test_rooms_are_whole() { assertWhole(snapshot_doc_t::rooms); }
static void test_shades_are_whole() { assertWhole(snapshot_doc_t::shades); }
static void test_groups_are_whole() { assertWhole(snapshot_doc_t::groups); }
static void test_single_items_are_whole() {
  assertWhole(snapshot_doc_t::room, SOMFY_MAX_ROOMS);
  assertWhole(snapshot_doc_t::shade, SOMFY_MAX_SHADES);
  assertWhole(snapshot_doc_t::group, SOMFY_MAX_GROUPS);
  assertWhole(snapshot_doc_t::groupOptions, SOMFY_MAX_GROUPS);
  assertWhole(snapshot_doc_t::repeaters);
}
static void test_a_too_small_unit_is_caught() {
  // The comparison itself works: a unit too small for a shade loses shades.
  const std::string full = document(snapshot_doc_t::shades, 0, bigUnit, sizeof(bigUnit));
  const std::string small = document(snapshot_doc_t::shades, 0, unitBuf, 200);
  TEST_ASSERT_TRUE(small.length() < full.length());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_largest_items_fit_the_unit);
  RUN_TEST(test_controller_is_whole);
  RUN_TEST(test_discovery_is_whole);
  RUN_TEST(test_rooms_are_whole);
  RUN_TEST(test_shades_are_whole);
  RUN_TEST(test_groups_are_whole);
  RUN_TEST(test_single_items_are_whole);
  RUN_TEST(test_a_too_small_unit_is_caught);
  return UNITY_END();
}
