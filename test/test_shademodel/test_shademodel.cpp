#include <unity.h>
#include <Preferences.h>
#include "ShadeModelFakes.h"
#include "SomfySnapshot.h"

// Scenarios across several shades and groups, run on the real shade model
// with a recording radio.  What a single shade does with a frame is
// ShadeBehaviour's, tested in test_shadebehaviour; these pin what happens
// between them.  Where today's rule is one #86 will change the test says
// "today:".

#define ASSERT_POS(expected, actual) TEST_ASSERT_FLOAT_WITHIN(0.5f, (expected), (actual))
#define ASSERT_CMD(expected, actual) TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static const uint32_t SHADE_A = 0x100001;
static const uint32_t SHADE_B = 0x100002;
static const uint32_t GROUP = 0x200001;
static const uint32_t REMOTE = 0x300001;

// What NVS holds for the remote with this address, -1 when nothing.
static int32_t storedCode(uint32_t address) {
  auto &store = Preferences::stubStore();
  auto it = store.find("ShadeCodes/_" + std::to_string(address));
  return it == store.end() ? -1 : it->second;
}

// The model with its recording ports and a clock.  The shade's own commands
// still read millis(), so the clock sets it too.
struct Rig {
  RecordingRadio radio;
  RecordingEvents events;
  ShadeModel m{radio, events};
  uint32_t now = 1000;
  Rig() {
    Preferences::stubStore().clear();
    stubMillis = this->now;
  }
  ~Rig() { stubMillis = 0; }
  SomfyShade &shade(uint32_t address) {
    SomfyShade *shade = this->m.addShade();
    const uint8_t id = shade->getShadeId();
    shade->clear();
    shade->setShadeId(id);
    shade->setRemoteAddress(address);
    return *shade;
  }
  SomfyGroup &group(uint32_t address, std::initializer_list<SomfyShade *> members) {
    SomfyGroup *group = this->m.addGroup();
    const uint8_t id = group->getGroupId();
    // A slot only starts out empty in static storage, as on the device.
    group->clear();
    group->setGroupId(id);
    group->setRemoteAddress(address);
    for(SomfyShade *shade : members) group->linkShade(shade->getShadeId());
    return *group;
  }
  // loop() passes for ms, 10 ms apart.
  void run(uint32_t ms) {
    for(uint32_t t = 0; t < ms; t += 10) {
      this->now += 10;
      stubMillis = this->now;
      this->m.tick(this->now);
    }
  }
  void event(tx_event_t type, uint32_t ticket) {
    somfy_tx_event_t evt;
    evt.type = type;
    evt.ticket = ticket;
    evt.at = this->now;
    this->m.onTxEvent(evt);
  }
  // The radio starts the last job it was handed.
  void onAir() { this->event(tx_event_t::started, this->radio.last().ticket); }
  // A frame the radio heard.
  void hear(uint32_t address, somfy_commands cmd, uint16_t rollingCode) {
    somfy_frame_t frame;
    frame.valid = true;
    frame.cmd = cmd;
    frame.remoteAddress = address;
    frame.rollingCode = rollingCode;
    this->m.onFrame(frame, this->now);
  }
};

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Group commands
 ********************************************************************/
static void test_group_down_moves_its_shades_once_on_air() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyShade &b = r.shade(SHADE_B);
  SomfyShade &other = r.shade(0x100003);
  SomfyGroup &g = r.group(GROUP, {&a, &b});
  TEST_ASSERT_TRUE(r.m.sendGroupCommand(g, somfy_commands::Down, 1, 0, r.now));
  // One frame, as the group.
  TEST_ASSERT_EQUAL(1, r.radio.jobs.size());
  TEST_ASSERT_EQUAL_HEX32(GROUP, r.radio.last().frame.remoteAddress);
  ASSERT_CMD(somfy_commands::Down, r.radio.last().frame.cmd);
  TEST_ASSERT_EQUAL_UINT16(1, r.radio.last().frame.rollingCode);
  // Only the group's rolling code was taken.
  TEST_ASSERT_EQUAL(1, storedCode(GROUP));
  TEST_ASSERT_EQUAL(-1, storedCode(SHADE_A));
  TEST_ASSERT_EQUAL(-1, storedCode(SHADE_B));
  TEST_ASSERT_EQUAL_INT8(1, g.direction);
  // Its shades wait for the frame to go on air.
  r.run(2000);
  ASSERT_POS(0, a.state().currentPos);
  ASSERT_POS(0, b.state().currentPos);
  r.onAir();
  r.run(5000);
  ASSERT_POS(50, a.state().currentPos);
  ASSERT_POS(50, b.state().currentPos);
  ASSERT_POS(0, other.state().currentPos);
  // Each one reports the command as the group's.
  TEST_ASSERT_EQUAL(2, r.events.commands.size());
  for(const ModelCommand &c : r.events.commands) {
    TEST_ASSERT_EQUAL_STRING("group", c.source.c_str());
    TEST_ASSERT_EQUAL_HEX32(GROUP, c.sourceAddress);
    ASSERT_CMD(somfy_commands::Down, c.cmd);
  }
  TEST_ASSERT_TRUE(g.changed & GROUP_CHANGED_DIRECTION);
}
static void test_refused_group_command_changes_nothing() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyGroup &g = r.group(GROUP, {&a});
  r.radio.refuseNext = true;
  TEST_ASSERT_FALSE(r.m.sendGroupCommand(g, somfy_commands::Down, 1, 0, r.now));
  TEST_ASSERT_EQUAL(-1, storedCode(GROUP));
  TEST_ASSERT_EQUAL_INT8(0, g.direction);
  ASSERT_POS(0, a.state().target);
  r.run(1000);
  TEST_ASSERT_EQUAL_INT8(0, a.state().direction);
  TEST_ASSERT_EQUAL(0, r.events.commands.size());
}
static void test_group_up_and_my_set_its_direction() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyGroup &g = r.group(GROUP, {&a});
  r.m.sendGroupCommand(g, somfy_commands::Up, 1, 0, r.now);
  TEST_ASSERT_EQUAL_INT8(-1, g.direction);
  r.m.sendGroupCommand(g, somfy_commands::My, 1, 0, r.now);
  TEST_ASSERT_EQUAL_INT8(0, g.direction);
  TEST_ASSERT_EQUAL(2, storedCode(GROUP));
}

/*********************************************************************
 * Linked remotes
 ********************************************************************/
static void test_today_a_shade_linked_to_another_only_reports_its_command() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyShade &b = r.shade(SHADE_B);
  b.linkRemote(SHADE_A);
  TEST_ASSERT_TRUE(a.sendCommand(somfy_commands::Down));
  TEST_ASSERT_EQUAL(1, r.radio.jobs.size());
  TEST_ASSERT_EQUAL_HEX32(SHADE_A, r.radio.last().frame.remoteAddress);
  // B heard the frame: its linked remote is up to date with the code sent,
  // in the NVS key it shares with A.
  TEST_ASSERT_EQUAL(1, storedCode(SHADE_A));
  TEST_ASSERT_EQUAL_UINT16(1, b.findRemote(SHADE_A)->lastRollingCode);
  // B hears it first through the model, then A hears its own frame.
  TEST_ASSERT_EQUAL(2, r.events.commands.size());
  TEST_ASSERT_EQUAL_UINT8(b.getShadeId(), r.events.commands[0].shadeId);
  TEST_ASSERT_EQUAL_UINT8(a.getShadeId(), r.events.commands[1].shadeId);
  for(const ModelCommand &c : r.events.commands) {
    TEST_ASSERT_EQUAL_STRING("internal", c.source.c_str());
    TEST_ASSERT_EQUAL_HEX32(SHADE_A, c.sourceAddress);
  }
  r.onAir();
  r.run(5000);
  ASSERT_POS(50, a.state().currentPos);
  // today: a frame another shade sent sets no target, so B stays put.
  ASSERT_POS(0, b.state().target);
  ASSERT_POS(0, b.state().currentPos);
}
static void test_a_dropped_group_frame_moves_none_of_its_shades() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyShade &b = r.shade(SHADE_B);
  SomfyGroup &g = r.group(GROUP, {&a, &b});
  SomfyShade &c = r.shade(0x100003);
  c.sendCommand(somfy_commands::Down);
  r.m.sendGroupCommand(g, somfy_commands::Down, 1, 0, r.now);
  TEST_ASSERT_EQUAL(2, r.radio.jobs.size());
  // The group's job is dropped, the other shade's goes on air.
  r.event(tx_event_t::dropped, r.radio.jobs[1].ticket);
  r.event(tx_event_t::started, r.radio.jobs[0].ticket);
  r.run(5000);
  ASSERT_POS(0, a.state().currentPos);
  ASSERT_POS(0, b.state().currentPos);
  TEST_ASSERT_EQUAL_INT8(0, a.state().direction);
  TEST_ASSERT_EQUAL_INT8(0, b.state().direction);
  ASSERT_POS(50, c.state().currentPos);
}
static void test_a_remote_linked_to_two_shades_moves_both() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyShade &b = r.shade(SHADE_B);
  a.linkRemote(REMOTE, 500);
  b.linkRemote(REMOTE, 500);
  r.hear(REMOTE, somfy_commands::Down, 501);
  // A remote's frame was on air already, so they move at once.
  r.run(5000);
  ASSERT_POS(50, a.state().currentPos);
  ASSERT_POS(50, b.state().currentPos);
  // Both linked remotes share the remote's one NVS key.
  TEST_ASSERT_EQUAL(501, storedCode(REMOTE));
  TEST_ASSERT_EQUAL_UINT16(501, a.findRemote(REMOTE)->lastRollingCode);
  TEST_ASSERT_EQUAL_UINT16(501, b.findRemote(REMOTE)->lastRollingCode);
  TEST_ASSERT_EQUAL(0, r.radio.jobs.size());
}

/*********************************************************************
 * Group flags
 ********************************************************************/
static void sunny(Rig &r, uint32_t address, bool on) {
  somfy_frame_t frame;
  frame.valid = true;
  frame.cmd = somfy_commands::Sensor;
  frame.remoteAddress = address;
  // The sensor's bits ride in the rolling code, shifted down by four.
  frame.rollingCode = on ? 0x02 : 0;
  r.m.onFrame(frame, r.now);
}
static void test_a_sensor_frame_updates_its_groups_flags() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyShade &b = r.shade(SHADE_B);
  a.sunSensor = true;
  SomfyGroup &g = r.group(GROUP, {&a, &b});
  SomfyGroup &h = r.group(0x200002, {&b});
  sunny(r, SHADE_A, true);
  const uint8_t sunnyBit = static_cast<uint8_t>(somfy_flags_t::Sunny);
  TEST_ASSERT_TRUE(a.getFlags() & sunnyBit);
  TEST_ASSERT_TRUE(g.flags & sunnyBit);
  TEST_ASSERT_EQUAL_UINT8(a.getFlags() | b.getFlags(), g.flags);
  TEST_ASSERT_EQUAL_UINT8(GROUP_CHANGED_STATE, g.changed);
  // The other group's shade didn't change.
  TEST_ASSERT_EQUAL_UINT8(0, h.changed);
  group_snapshot_t snap;
  snapshotGroup(r.m, g, snap);
  TEST_ASSERT_EQUAL_UINT8(g.flags, snap.flags);
  TEST_ASSERT_TRUE(snap.sunSensor);
  // The same frame again changes nothing.
  g.changed = 0;
  sunny(r, SHADE_A, true);
  TEST_ASSERT_EQUAL_UINT8(0, g.changed);
}
static void test_snapshot_shows_a_shade_in_a_group() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  SomfyShade &b = r.shade(SHADE_B);
  r.group(GROUP, {&a});
  shade_snapshot_t snap;
  snapshotShade(r.m, a, snap);
  TEST_ASSERT_TRUE(snap.inGroup);
  snapshotShade(r.m, b, snap);
  TEST_ASSERT_FALSE(snap.inGroup);
  TEST_ASSERT_TRUE(a.isInGroup());
}

/*********************************************************************
 * Sensor frames, repeats and GPIO
 ********************************************************************/
static void test_today_a_shade_hears_its_own_sensor_frame() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  a.sunSensor = true;
  TEST_ASSERT_TRUE(r.m.sendSensorCommand(a, -1, 1, 1, r.now));
  ASSERT_CMD(somfy_commands::Sensor, r.radio.last().frame.cmd);
  TEST_ASSERT_EQUAL_HEX32(SHADE_A, r.radio.last().frame.remoteAddress);
  // today: unlike a command, the sender of a sensor frame hears it through
  // the model, so its own flags follow the frame.
  TEST_ASSERT_TRUE(a.getFlags() & static_cast<uint8_t>(somfy_flags_t::Sunny));
  // A sensor frame takes no rolling code.
  TEST_ASSERT_EQUAL(-1, storedCode(SHADE_A));
}
static void test_repeat_sends_the_last_frame_again() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  a.sendCommand(somfy_commands::Down);
  TEST_ASSERT_TRUE(r.m.repeatFrame(a, 4));
  TEST_ASSERT_EQUAL(2, r.radio.jobs.size());
  TEST_ASSERT_EQUAL_UINT16(r.radio.jobs[0].frame.rollingCode, r.radio.last().frame.rollingCode);
  ASSERT_CMD(somfy_commands::Down, r.radio.last().frame.cmd);
  // A repeat takes no new rolling code.
  TEST_ASSERT_EQUAL(1, storedCode(SHADE_A));
}
static void test_gp_remote_shade_pulses_its_outputs() {
  Rig r;
  SomfyShade &a = r.shade(SHADE_A);
  a.proto = radio_proto::GP_Remote;
  TEST_ASSERT_TRUE(a.sendCommand(somfy_commands::Up));
  TEST_ASSERT_EQUAL(0, r.radio.jobs.size());
  TEST_ASSERT_EQUAL(1, r.radio.pulses.size());
  TEST_ASSERT_EQUAL_UINT8(a.getShadeId(), r.radio.pulses[0].shadeId);
  ASSERT_CMD(somfy_commands::Up, r.radio.pulses[0].cmd);
  // The rolling code still counts up.
  TEST_ASSERT_EQUAL(1, storedCode(SHADE_A));
}
static void test_radio_defaults_fill_in_a_cleared_shade() {
  Rig r;
  r.m.setRadioDefaults(80, radio_proto::RTW);
  SomfyShade &a = r.shade(SHADE_A);
  TEST_ASSERT_EQUAL_UINT8(80, a.bitLength);
  TEST_ASSERT_TRUE(a.proto == radio_proto::RTW);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_group_down_moves_its_shades_once_on_air);
  RUN_TEST(test_refused_group_command_changes_nothing);
  RUN_TEST(test_group_up_and_my_set_its_direction);
  RUN_TEST(test_today_a_shade_linked_to_another_only_reports_its_command);
  RUN_TEST(test_a_dropped_group_frame_moves_none_of_its_shades);
  RUN_TEST(test_a_remote_linked_to_two_shades_moves_both);
  RUN_TEST(test_a_sensor_frame_updates_its_groups_flags);
  RUN_TEST(test_snapshot_shows_a_shade_in_a_group);
  RUN_TEST(test_today_a_shade_hears_its_own_sensor_frame);
  RUN_TEST(test_repeat_sends_the_last_frame_again);
  RUN_TEST(test_gp_remote_shade_pulses_its_outputs);
  RUN_TEST(test_radio_defaults_fill_in_a_cleared_shade);
  return UNITY_END();
}
