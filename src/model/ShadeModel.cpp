#include "esp_log.h"
#include "ShadeModel.h"
#include "ModelEditor.h"

static const char *TAG = "Somfy";

ShadeModel::ShadeModel(RadioPort &radio, ModelEvents &events) : radio(radio), events(events) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) this->shades[i].model = this;
}
void ShadeModel::setRadioDefaults(uint8_t bitLength, radio_proto proto) {
  this->bitLength = bitLength;
  this->proto = proto;
}
void ShadeModel::onFrame(somfy_frame_t &frame, uint32_t now) { this->hear(frame, false, nullptr, now); }
void ShadeModel::onIdle(uint32_t now) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processWaitingFrame(now);
}
void ShadeModel::onTxEvent(const somfy_tx_event_t &evt) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processTxEvent(evt);
}
void ShadeModel::tick(uint32_t now) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].checkMovement(now);
}
send_result_t ShadeModel::send(SomfyRemote &from, SomfyShade *pulseAs, somfy_commands cmd, uint8_t repeat, uint8_t stepSize,
    uint16_t delayMs, bool priority, const SomfyRemote *skip, uint32_t now) {
  // Build the frame aside and only take the rolling code once the frame is
  // queued so a dropped command leaves the remote untouched.
  somfy_frame_t frame = from.lastFrame;
  frame.rollingCode = from.peekNextRollingCode();
  frame.remoteAddress = from.getRemoteAddress();
  frame.cmd = from.transformCommand(cmd);
  frame.repeats = repeat;
  frame.bitLength = from.bitLength;
  frame.stepSize = stepSize;
  frame.valid = true;
  // Match the encKey to the rolling code.  These keys range from 160 to 175.
  frame.encKey = 0xA0 | static_cast<uint8_t>(frame.rollingCode & 0x000F);
  frame.proto = from.proto;
  if(frame.bitLength == 0) frame.bitLength = this->bitLength;
  if(frame.rollingCode == 0) ESP_LOGE(TAG, "Setting rcode to 0");
  // We have to set the processed to clear this if we are sending
  // another command.
  frame.processed = false;
  send_result_t result;
  if(from.proto == radio_proto::GP_Relay) {
    ESP_LOGD(TAG, "CMD:%s ADDR:%d RCODE:%d SETTING GPIO",
        translateSomfyCommand(frame.cmd).c_str(),
        frame.remoteAddress, frame.rollingCode);
  }
  else if(from.proto == radio_proto::GP_Remote) {
    ESP_LOGD(TAG, "CMD:%s ADDR:%d RCODE:%d TRIGGER GPIO",
        translateSomfyCommand(frame.cmd).c_str(),
        frame.remoteAddress, frame.rollingCode);
    if(pulseAs) this->radio.pulse(*pulseAs, frame);
  }
  else {
    ESP_LOGD(TAG, "CMD:%s ADDR:%d RCODE:%d REPEAT:%d",
        translateSomfyCommand(frame.cmd).c_str(),
        frame.remoteAddress, frame.rollingCode, repeat);
    // Only simulate the command if it is actually going on air.
    result.ticket = this->submitFrame(frame, repeat, delayMs, priority);
    if(result.ticket == 0) return result;
  }
  result.sent = true;
  from.commitRollingCode(frame.rollingCode);
  from.lastFrame = frame;
  this->hear(from.lastFrame, true, skip, now);
  // Idle shades (and group members) this sets moving wait for the frame to go
  // on air.  Stops are sent ahead of everything else so they don't need to.
  if(result.ticket != 0 && !priority && startsMovement(frame.cmd)) this->markTxPending(frame.remoteAddress, result.ticket, skip, now);
  return result;
}
uint32_t ShadeModel::submitFrame(somfy_frame_t &frame, uint8_t repeat, uint16_t delayMs, bool priority) {
  somfy_tx_job_t job;
  encodeTxJob(job, frame, repeat);
  job.firstRepeat = 1;
  return this->radio.submit(job, priority, delayMs);
}
void ShadeModel::hear(somfy_frame_t &frame, bool internal, const SomfyRemote *skip, uint32_t now) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(&this->shades[i] == skip) continue;
    if(this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal, now);
  }
}
void ShadeModel::markTxPending(uint32_t remoteAddress, uint32_t ticket, const SomfyRemote *skip, uint32_t now) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade == skip) continue;
    if(shade->getShadeId() != 255 && shade->findRemote(remoteAddress)) shade->markTxPending(ticket, now);
  }
  // Group commands move their member shades through processInternalCommand.
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255 || group->getRemoteAddress() != remoteAddress) continue;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
      if(group->linkedShades[j] == 0) continue;
      SomfyShade *shade = this->getShadeById(group->linkedShades[j]);
      if(shade) shade->markTxPending(ticket, now);
    }
  }
}
bool ShadeModel::sendGroupCommand(SomfyGroup &group, somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint32_t now) {
  ESP_LOGI(TAG, "[Group %u] sendCommand cmd=%s repeat=%u", group.getGroupId(), translateSomfyCommand(cmd).c_str(), repeat);
  if(group.bitLength == 0) group.bitLength = this->bitLength;
  // Leave the group and its shades alone if the radio didn't take the command.
  if(!this->send(group, nullptr, cmd, repeat, stepSize, 0, false, nullptr, now).sent) return false;

  switch(cmd) {
    case somfy_commands::My:
      group.p_direction(0);
      break;
    case somfy_commands::Up:
      group.p_direction(-1);
      break;
    case somfy_commands::Down:
      group.p_direction(1);
      break;
    default:
      break;
  }

  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(group.linkedShades[i] != 0) {
      SomfyShade *shade = this->getShadeById(group.linkedShades[i]);
      if(shade) {
        ESP_LOGI(TAG, "[Group %u] processInternalCommand on shade %u cmd=%s", group.getGroupId(), shade->getShadeId(), translateSomfyCommand(cmd).c_str());
        shade->processInternalCommand(cmd, repeat, now);
        this->events.onCommand(*shade, cmd, "group", group.getRemoteAddress());
      }
    }
  }
  this->updateGroupFlags(group);
  group.emitState();
  return true;
}
bool ShadeModel::sendSensorCommand(SomfyRemote &remote, int8_t isWindy, int8_t isSunny, uint8_t repeat, uint32_t now) {
  uint8_t flags = (remote.getFlags() >> 4) & 0x0F;
  if(isWindy > 0) flags |= 0x01;
  if(isSunny > 0) flags |= 0x02;
  if(isWindy == 0) flags &= ~0x01;
  if(isSunny == 0) flags &= ~0x02;

  // Now ship this off as an 80 bit command.  Built aside so lastFrame only
  // changes when the frame is queued.
  somfy_frame_t frame = remote.lastFrame;
  frame.remoteAddress = remote.getRemoteAddress();
  frame.repeats = repeat;
  frame.bitLength = remote.bitLength;
  frame.rollingCode = (uint16_t)flags;
  frame.encKey = 160; // Sensor commands are always encryption code 160.
  frame.cmd = somfy_commands::Sensor;
  frame.processed = false;
  ESP_LOGD(TAG, "CMD:%s ADDR:%d RCODE:%d REPEAT:%d",
      translateSomfyCommand(frame.cmd).c_str(),
      frame.remoteAddress, frame.rollingCode, repeat);
  if(!this->submitFrame(frame, repeat, 0, false)) return false;
  remote.lastFrame = frame;
  this->hear(remote.lastFrame, true, nullptr, now);
  return true;
}
bool ShadeModel::repeatFrame(SomfyShade &shade, uint8_t repeat) { return this->repeatFrame(shade, &shade, repeat); }
bool ShadeModel::repeatFrame(SomfyGroup &group, uint8_t repeat) { return this->repeatFrame(group, nullptr, repeat); }
bool ShadeModel::repeatFrame(SomfyRemote &remote, SomfyShade *pulseAs, uint8_t repeat) {
  if(remote.proto == radio_proto::GP_Relay)
    return true;
  else if(remote.proto == radio_proto::GP_Remote) {
    if(pulseAs) this->radio.pulse(*pulseAs, remote.lastFrame);
    return true;
  }
  somfy_tx_job_t job;
  encodeRepeatJob(job, remote.lastFrame, repeat);
  if(!this->radio.submit(job, false, 0)) return false;
  noteRepeatQueued(remote.lastFrame, job);
  return true;
}
bool ShadeModel::isRepeater(uint32_t address) const {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++)
    if(this->repeaters[i] == address) return true;
  return false;
}
bool ShadeModel::isInGroup(const SomfyShade &shade) const {
  if(shade.getShadeId() == 255) return false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255 && this->groups[i].hasShadeId(shade.getShadeId())) return true;
  }
  return false;
}
const SomfyShade *ShadeModel::findShade(uint8_t shadeId) const {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}
uint8_t ShadeModel::groupFlags(const SomfyGroup &group) const {
  uint8_t flags = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES && group.linkedShades[i] != 0; i++) {
    const SomfyShade *shade = this->findShade(group.linkedShades[i]);
    if(shade) flags |= shade->getFlags();
  }
  return flags;
}
void ShadeModel::updateGroupFlags() {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
    if(this->groups[i].getGroupId() != 255) this->updateGroupFlags(this->groups[i]);
}
void ShadeModel::updateGroupFlags(SomfyGroup &group) {
  const uint8_t flags = this->groupFlags(group);
  if(flags == group.flags) return;
  group.flags = flags;
  group.emitState();
}
SomfyShade *ShadeModel::findShadeByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getRemoteAddress() == address) return &shade;
    else {
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        if(shade.linkedRemotes[j].getRemoteAddress() == address) return &shade;
      }
    }
  }
  return nullptr;
}
SomfyGroup *ShadeModel::findGroupByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getRemoteAddress() == address) return &group;
  }
  return nullptr;
}
SomfyRoom * ShadeModel::getRoomById(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) return &this->rooms[i];
  }
  return nullptr;
}
SomfyShade * ShadeModel::getShadeById(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}
SomfyGroup * ShadeModel::getGroupById(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) return &this->groups[i];
  }
  return nullptr;
}
void SomfyRoom::clear() {
  this->roomId = 0;
  strcpy(this->name, "");
}
// A deleted shade, group or room leaves a gap, so a new one takes the lowest
// free id, from 1 up to the maximum.
uint8_t ShadeModel::getNextShadeId() {
  uint8_t ids[SOMFY_MAX_SHADES];
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) ids[i] = this->shades[i].getShadeId();
  const uint8_t id = firstFreeId(ids, SOMFY_MAX_SHADES, SOMFY_MAX_SHADES);
  return id == 0 ? 255 : id;
}
int8_t ShadeModel::getMaxShadeOrder() {
  int8_t orders[SOMFY_MAX_SHADES];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) orders[count++] = this->shades[i].sortOrder;
  return nextSortOrder(orders, count) - 1;
}
int8_t ShadeModel::getMaxGroupOrder() {
  int8_t orders[SOMFY_MAX_GROUPS];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
    if(this->groups[i].getGroupId() != 255) orders[count++] = this->groups[i].sortOrder;
  return nextSortOrder(orders, count) - 1;
}
uint8_t ShadeModel::getNextGroupId() {
  uint8_t ids[SOMFY_MAX_GROUPS];
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) ids[i] = this->groups[i].getGroupId();
  const uint8_t id = firstFreeId(ids, SOMFY_MAX_GROUPS, SOMFY_MAX_GROUPS);
  return id == 0 ? 255 : id;
}
uint32_t ShadeModel::getNextRemoteAddress(uint8_t id) {
  uint32_t used[SOMFY_MAX_SHADES + SOMFY_MAX_GROUPS];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) used[count++] = this->shades[i].getRemoteAddress();
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
    if(this->groups[i].getGroupId() != 255) used[count++] = this->groups[i].getRemoteAddress();
  return offeredRemoteAddress(this->startingAddress, id, used, count);
}
uint8_t ShadeModel::getNextRoomId() {
  uint8_t ids[SOMFY_MAX_ROOMS];
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) ids[i] = this->rooms[i].roomId;
  return firstFreeId(ids, SOMFY_MAX_ROOMS, SOMFY_MAX_ROOMS);
}
int8_t ShadeModel::getMaxRoomOrder() {
  int8_t orders[SOMFY_MAX_ROOMS];
  size_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++)
    if(this->rooms[i].roomId != 0) orders[count++] = this->rooms[i].sortOrder;
  return nextSortOrder(orders, count) - 1;
}
uint8_t ShadeModel::repeaterCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) count++;
  }
  return count;
}
uint8_t ShadeModel::roomCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId != 0) count++;
  }
  return count;
}
uint8_t ShadeModel::shadeCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) count++;
  }
  return count;
}
uint8_t ShadeModel::groupCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255) count++;
  }
  return count;
}
SomfyShade *ShadeModel::addShade() {
  uint8_t shadeId = this->getNextShadeId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.  The time has come so the sort order is set below.
  if(shadeId == 255) return nullptr;
  SomfyShade *shade = &this->shades[shadeId - 1];
  if(shade) {
    shade->setShadeId(shadeId);
    shade->sortOrder = this->getMaxShadeOrder() + 1;
    ESP_LOGD(TAG, "Sort order set to %d", shade->sortOrder);
    this->isDirty = true;
  }
  return shade;
}
bool ShadeModel::unlinkRepeater(uint32_t address) {
  removeFromList(this->repeaters, SOMFY_MAX_REPEATERS, address);
  this->isDirty = true;
  return true;
}
// False when every repeater slot is taken.
bool ShadeModel::linkRepeater(uint32_t address) {
  if(!addToList(this->repeaters, SOMFY_MAX_REPEATERS, address)) return false;
  this->isDirty = true;
  return true;
}
SomfyRoom *ShadeModel::addRoom() {
  uint8_t roomId = this->getNextRoomId();
  // So the next room id will be the first one we run into with an id of 0 so
  if(roomId == 0) return nullptr;
  SomfyRoom *room = &this->rooms[roomId - 1];
  if(room) {
    room->roomId = roomId;
    room->sortOrder = this->getMaxRoomOrder() + 1;
    this->isDirty = true;
  }
  return room;
}
SomfyGroup *ShadeModel::addGroup() {
  uint8_t groupId = this->getNextGroupId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.
  if(groupId == 255) return nullptr;
  SomfyGroup *group = &this->groups[groupId - 1];
  if(group) {
    group->setGroupId(groupId);
    group->sortOrder = this->getMaxGroupOrder() + 1;
    this->isDirty = true;
  }
  return group;
}
