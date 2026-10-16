#include <string.h>
#include "SomfySnapshot.h"

extern SomfyShadeController somfy;

static void copyName(char *out, const char *name) {
  strncpy(out, name, 20);
  out[20] = '\0';
}

void snapshotRoom(SomfyRoom &room, room_snapshot_t &out) {
  out.roomId = room.roomId;
  copyName(out.name, room.name);
  out.sortOrder = room.sortOrder;
}
void snapshotShade(SomfyShade &shade, shade_snapshot_t &out) {
  out.shadeId = shade.getShadeId();
  out.roomId = shade.roomId;
  copyName(out.name, shade.name);
  out.remoteAddress = shade.getRemoteAddress();
  out.upTime = shade.upTime;
  out.downTime = shade.downTime;
  out.tiltTime = shade.tiltTime;
  out.stepSize = shade.stepSize;
  out.paired = shade.paired;
  out.lastRollingCode = shade.lastRollingCode;
  out.position = shade.transformPosition(shade.state().currentPos);
  out.target = shade.transformPosition(shade.state().target);
  out.myPos = shade.transformPosition(shade.state().myPos);
  out.direction = shade.state().direction;
  out.tiltType = static_cast<uint8_t>(shade.tiltType);
  out.tiltPosition = shade.transformPosition(shade.state().currentTiltPos);
  out.tiltTarget = shade.transformPosition(shade.state().tiltTarget);
  out.myTiltPos = shade.transformPosition(shade.state().myTiltPos);
  out.tiltDirection = shade.state().tiltDirection;
  out.shadeType = static_cast<uint8_t>(shade.shadeType);
  out.bitLength = shade.bitLength;
  out.proto = static_cast<uint8_t>(shade.proto);
  out.flags = shade.getFlags();
  out.flipCommands = shade.flipCommands;
  out.flipPosition = shade.flipPosition;
  out.inGroup = shade.isInGroup();
  out.sunSensor = shade.sunSensor;
  out.light = shade.light;
  out.simMy = shade.simMy;
  out.repeats = shade.repeats;
  out.sortOrder = shade.sortOrder;
  out.gpioUp = shade.gpioUp;
  out.gpioDown = shade.gpioDown;
  out.gpioMy = shade.gpioMy;
  out.gpioLLTrigger = (shade.gpioFlags & static_cast<uint8_t>(gpio_flags_t::LowLevelTrigger)) != 0;
  out.linkedRemoteCount = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote &remote = shade.linkedRemotes[i];
    if(remote.getRemoteAddress() == 0) continue;
    linked_remote_snapshot_t &l = out.linkedRemotes[out.linkedRemoteCount++];
    l.remoteAddress = remote.getRemoteAddress();
    l.lastRollingCode = remote.lastRollingCode;
  }
}
void snapshotGroup(SomfyGroup &group, group_snapshot_t &out) {
  out.groupId = group.getGroupId();
  out.roomId = group.roomId;
  copyName(out.name, group.name);
  out.remoteAddress = group.getRemoteAddress();
  out.lastRollingCode = group.lastRollingCode;
  out.bitLength = group.bitLength;
  out.proto = static_cast<uint8_t>(group.proto);
  out.sunSensor = group.hasSunSensor();
  out.flipCommands = group.flipCommands;
  out.repeats = group.repeats;
  out.sortOrder = group.sortOrder;
  // The same sum as SomfyGroup::updateFlags, without its event.
  out.flags = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES && group.linkedShades[i] != 0; i++) {
    SomfyShade *shade = somfy.getShadeById(group.linkedShades[i]);
    if(shade) out.flags |= shade->getFlags();
  }
  out.linkedShadeCount = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    const uint8_t shadeId = group.linkedShades[i];
    if(shadeId > 0 && shadeId < 255) out.linkedShades[out.linkedShadeCount++] = shadeId;
  }
}
void fillSnapshot(model_snapshot_t &snap) {
  snap.startingAddress = somfy.startingAddress;
  snap.radioBitLength = somfy.transceiver.config.type;
  snap.radioProto = static_cast<uint8_t>(somfy.transceiver.config.proto);
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++)
    if(somfy.rooms[i].roomId != 0) snapshotRoom(somfy.rooms[i], snap.rooms[snap.roomCount++]);
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(somfy.shades[i].getShadeId() != 255) snapshotShade(somfy.shades[i], snap.shades[snap.shadeCount++]);
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
    if(somfy.groups[i].getGroupId() != 255) snapshotGroup(somfy.groups[i], snap.groups[snap.groupCount++]);
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++)
    if(somfy.repeaters[i] != 0) snap.repeaters[snap.repeaterCount++] = somfy.repeaters[i];
}
const shade_snapshot_t *liveShade(uint8_t shadeId, void *ctx) {
  SomfyShade *shade = somfy.getShadeById(shadeId);
  if(!shade) return nullptr;
  shade_snapshot_t *out = static_cast<shade_snapshot_t *>(ctx);
  snapshotShade(*shade, *out);
  return out;
}
