#include "esp_log.h"
#include "ShadeModel.h"
#include "ModelEditor.h"

static const char *TAG = "Somfy";

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
