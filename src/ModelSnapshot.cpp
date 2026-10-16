#include <new>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "ModelSnapshot.h"
#include "ModelEditor.h"

static const char *TAG = "Snapshot";

const room_snapshot_t *model_snapshot_t::room(uint8_t roomId) const {
  for(uint8_t i = 0; i < this->roomCount; i++)
    if(this->rooms[i].roomId == roomId) return &this->rooms[i];
  return nullptr;
}
const shade_snapshot_t *model_snapshot_t::shade(uint8_t shadeId) const {
  for(uint8_t i = 0; i < this->shadeCount; i++)
    if(this->shades[i].shadeId == shadeId) return &this->shades[i];
  return nullptr;
}
const group_snapshot_t *model_snapshot_t::group(uint8_t groupId) const {
  for(uint8_t i = 0; i < this->groupCount; i++)
    if(this->groups[i].groupId == groupId) return &this->groups[i];
  return nullptr;
}

/*********************************************************************
 * Items
 ********************************************************************/
void writeRoom(JsonFormatter &json, const room_snapshot_t &room) {
  json.addElem("roomId", room.roomId);
  json.addElem("name", room.name);
  json.addElem("sortOrder", room.sortOrder);
}
void writeShadeRef(JsonFormatter &json, const shade_snapshot_t &shade) {
  json.addElem("shadeId", shade.shadeId);
  json.addElem("roomId", shade.roomId);
  json.addElem("name", shade.name);
  json.addElem("remoteAddress", shade.remoteAddress);
  json.addElem("paired", shade.paired);
  json.addElem("shadeType", shade.shadeType);
  json.addElem("flipCommands", shade.flipCommands);
  json.addElem("flipPosition", shade.flipPosition);
  json.addElem("bitLength", shade.bitLength);
  json.addElem("proto", shade.proto);
  json.addElem("flags", shade.flags);
  json.addElem("sunSensor", shade.sunSensor);
  json.addElem("hasLight", shade.light);
  json.addElem("repeats", shade.repeats);
}
void writeShade(JsonFormatter &json, const shade_snapshot_t &shade) {
  json.addElem("shadeId", shade.shadeId);
  json.addElem("roomId", shade.roomId);
  json.addElem("name", shade.name);
  json.addElem("remoteAddress", shade.remoteAddress);
  json.addElem("upTime", shade.upTime);
  json.addElem("downTime", shade.downTime);
  json.addElem("paired", shade.paired);
  json.addElem("lastRollingCode", static_cast<uint32_t>(shade.lastRollingCode));
  json.addElem("position", shade.position);
  json.addElem("tiltType", shade.tiltType);
  json.addElem("tiltPosition", shade.tiltPosition);
  json.addElem("tiltDirection", shade.tiltDirection);
  json.addElem("tiltTime", shade.tiltTime);
  json.addElem("stepSize", static_cast<uint32_t>(shade.stepSize));
  json.addElem("tiltTarget", shade.tiltTarget);
  json.addElem("target", shade.target);
  json.addElem("myPos", shade.myPos);
  json.addElem("myTiltPos", shade.myTiltPos);
  json.addElem("direction", shade.direction);
  json.addElem("shadeType", shade.shadeType);
  json.addElem("bitLength", shade.bitLength);
  json.addElem("proto", shade.proto);
  json.addElem("flags", shade.flags);
  json.addElem("flipCommands", shade.flipCommands);
  json.addElem("flipPosition", shade.flipPosition);
  json.addElem("inGroup", shade.inGroup);
  json.addElem("sunSensor", shade.sunSensor);
  json.addElem("light", shade.light);
  json.addElem("repeats", shade.repeats);
  json.addElem("sortOrder", shade.sortOrder);
  json.addElem("gpioUp", shade.gpioUp);
  json.addElem("gpioDown", shade.gpioDown);
  json.addElem("gpioMy", shade.gpioMy);
  json.addElem("gpioLLTrigger", shade.gpioLLTrigger);
  json.addElem("simMy", shade.simMy);
  json.beginArray("linkedRemotes");
  for(uint8_t i = 0; i < shade.linkedRemoteCount; i++) {
    json.beginObject();
    json.addElem("remoteAddress", shade.linkedRemotes[i].remoteAddress);
    json.addElem("lastRollingCode", static_cast<uint32_t>(shade.linkedRemotes[i].lastRollingCode));
    json.endObject();
  }
  json.endArray();
}
void writeGroupRef(JsonFormatter &json, const group_snapshot_t &group) {
  json.addElem("groupId", group.groupId);
  json.addElem("roomId", group.roomId);
  json.addElem("name", group.name);
  json.addElem("remoteAddress", group.remoteAddress);
  json.addElem("lastRollingCode", static_cast<uint32_t>(group.lastRollingCode));
  json.addElem("bitLength", group.bitLength);
  json.addElem("proto", group.proto);
  json.addElem("sunSensor", group.sunSensor);
  json.addElem("flipCommands", group.flipCommands);
  json.addElem("flags", group.flags);
  json.addElem("repeats", group.repeats);
  json.addElem("sortOrder", group.sortOrder);
}
void writeGroup(JsonFormatter &json, const group_snapshot_t &group, shade_lookup_t lookup, void *ctx) {
  writeGroupRef(json, group);
  json.beginArray("linkedShades");
  for(uint8_t i = 0; i < group.linkedShadeCount; i++) {
    const shade_snapshot_t *shade = lookup(group.linkedShades[i], ctx);
    if(!shade) continue;
    json.beginObject();
    writeShadeRef(json, *shade);
    json.endObject();
  }
  json.endArray();
}

/*********************************************************************
 * What the next room, shade or group would get (ModelEditor's rules,
 * as SomfyShadeController applies them when one is added)
 ********************************************************************/
uint8_t nextRoomId(const model_snapshot_t &snap) {
  uint8_t ids[SOMFY_MAX_ROOMS];
  for(uint8_t i = 0; i < snap.roomCount; i++) ids[i] = snap.rooms[i].roomId;
  return firstFreeId(ids, snap.roomCount, SOMFY_MAX_ROOMS - 2);
}
uint8_t nextShadeId(const model_snapshot_t &snap) {
  uint8_t ids[SOMFY_MAX_SHADES];
  for(uint8_t i = 0; i < snap.shadeCount; i++) ids[i] = snap.shades[i].shadeId;
  const uint8_t id = firstFreeId(ids, snap.shadeCount, SOMFY_MAX_SHADES - 2);
  return id == 0 ? 255 : id;
}
uint8_t nextGroupId(const model_snapshot_t &snap) {
  uint8_t ids[SOMFY_MAX_GROUPS];
  for(uint8_t i = 0; i < snap.groupCount; i++) ids[i] = snap.groups[i].groupId;
  const uint8_t id = firstFreeId(ids, snap.groupCount, SOMFY_MAX_GROUPS - 2);
  return id == 0 ? 255 : id;
}
uint32_t nextRemoteAddress(const model_snapshot_t &snap, uint8_t id) {
  uint32_t used[SOMFY_MAX_SHADES + SOMFY_MAX_GROUPS];
  size_t count = 0;
  for(uint8_t i = 0; i < snap.shadeCount; i++) used[count++] = snap.shades[i].remoteAddress;
  for(uint8_t i = 0; i < snap.groupCount; i++) used[count++] = snap.groups[i].remoteAddress;
  return freeRemoteAddress(snap.startingAddress + id, used, count);
}

/*********************************************************************
 * Documents
 ********************************************************************/
enum : uint8_t {
  ST_HEADER,        // {<header>,"rooms":[
  ST_ROOMS,
  ST_SHADES,
  ST_GROUPS,
  ST_REPEATERS,
  ST_ROOM,
  ST_SHADE,
  ST_GROUP,
  ST_OPEN_ARRAY,    // [
  ST_CLOSE_ARRAY,   // ]
  ST_TO_SHADES,     // ],"shades":[
  ST_TO_GROUPS,     // ],"groups":[
  ST_TO_REPEATERS,  // ],"repeaters":[
  ST_CLOSE_DOC,     // ]}
  ST_NEXT_ROOM,
  ST_NEXT_SHADE,
  ST_NEXT_GROUP,
  ST_GROUP_OPTIONS, // the group, then ],"availShades":[
  ST_AVAIL_SHADES,  // the shades not in that group
  ST_END
};
static const uint8_t CONTROLLER_STEPS[] = { ST_HEADER, ST_ROOMS, ST_TO_SHADES, ST_SHADES, ST_TO_GROUPS, ST_GROUPS, ST_TO_REPEATERS, ST_REPEATERS, ST_CLOSE_DOC, ST_END };
static const uint8_t DISCOVERY_STEPS[] = { ST_HEADER, ST_ROOMS, ST_TO_SHADES, ST_SHADES, ST_TO_GROUPS, ST_GROUPS, ST_CLOSE_DOC, ST_END };
static const uint8_t ROOMS_STEPS[] = { ST_OPEN_ARRAY, ST_ROOMS, ST_CLOSE_ARRAY, ST_END };
static const uint8_t SHADES_STEPS[] = { ST_OPEN_ARRAY, ST_SHADES, ST_CLOSE_ARRAY, ST_END };
static const uint8_t GROUPS_STEPS[] = { ST_OPEN_ARRAY, ST_GROUPS, ST_CLOSE_ARRAY, ST_END };
static const uint8_t ROOM_STEPS[] = { ST_ROOM, ST_END };
static const uint8_t SHADE_STEPS[] = { ST_SHADE, ST_END };
static const uint8_t GROUP_STEPS[] = { ST_GROUP, ST_END };
static const uint8_t REPEATERS_STEPS[] = { ST_OPEN_ARRAY, ST_REPEATERS, ST_CLOSE_ARRAY, ST_END };
static const uint8_t NEXT_ROOM_STEPS[] = { ST_NEXT_ROOM, ST_END };
static const uint8_t NEXT_SHADE_STEPS[] = { ST_NEXT_SHADE, ST_END };
static const uint8_t NEXT_GROUP_STEPS[] = { ST_NEXT_GROUP, ST_END };
static const uint8_t GROUP_OPTIONS_STEPS[] = { ST_GROUP_OPTIONS, ST_AVAIL_SHADES, ST_CLOSE_DOC, ST_END };

static const uint8_t *stepsFor(snapshot_doc_t doc) {
  switch(doc) {
    case snapshot_doc_t::controller: return CONTROLLER_STEPS;
    case snapshot_doc_t::discovery: return DISCOVERY_STEPS;
    case snapshot_doc_t::rooms: return ROOMS_STEPS;
    case snapshot_doc_t::shades: return SHADES_STEPS;
    case snapshot_doc_t::groups: return GROUPS_STEPS;
    case snapshot_doc_t::room: return ROOM_STEPS;
    case snapshot_doc_t::shade: return SHADE_STEPS;
    case snapshot_doc_t::group: return GROUP_STEPS;
    case snapshot_doc_t::repeaters: return REPEATERS_STEPS;
    case snapshot_doc_t::nextRoom: return NEXT_ROOM_STEPS;
    case snapshot_doc_t::nextShade: return NEXT_SHADE_STEPS;
    case snapshot_doc_t::nextGroup: return NEXT_GROUP_STEPS;
    case snapshot_doc_t::groupOptions: return GROUP_OPTIONS_STEPS;
  }
  return SHADES_STEPS;
}
static const shade_snapshot_t *snapshotShade(uint8_t shadeId, void *ctx) {
  return static_cast<const model_snapshot_t *>(ctx)->shade(shadeId);
}

SnapshotWriter::SnapshotWriter(const model_snapshot_t &snap, snapshot_doc_t doc, uint8_t id, snapshot_header_t header, char *unit, size_t unitSize)
  : snap(snap), steps(stepsFor(doc)), id(id), header(header), unit(unit), unitSize(unitSize) {}

size_t SnapshotWriter::generate(uint8_t *out, size_t maxLen) {
  size_t written = 0;
  while(written < maxLen) {
    if(this->consumed < this->unitLen) {
      size_t n = this->unitLen - this->consumed;
      if(n > maxLen - written) n = maxLen - written;
      memcpy(out + written, this->unit + this->consumed, n);
      this->consumed += n;
      written += n;
    }
    else {
      // A step or item can produce nothing, so go on until something is
      // produced or the document is done.
      this->consumed = 0;
      do this->produceUnit(); while(this->unitLen == 0 && this->steps[this->step] != ST_END);
      if(this->unitLen == 0) return written;
    }
  }
  return written;
}
void SnapshotWriter::nextStep() {
  this->step++;
  this->idx = 0;
  this->firstItem = true;
  this->groupOpen = false;
}
void SnapshotWriter::literal(const char *text) {
  strncpy(this->unit, text, this->unitSize - 1);
  this->unit[this->unitSize - 1] = '\0';
  this->unitLen = strlen(this->unit);
  this->nextStep();
}
// Writes text as the whole rest of the document.
void SnapshotWriter::finish(const char *text) {
  this->literal(text);
  while(this->steps[this->step] != ST_END) this->step++;
}
void SnapshotWriter::resetFmt(size_t offset) {
  this->unit[offset] = '\0';
  this->fmt.setBuffer(this->unit + offset, this->unitSize - offset);
}
bool SnapshotWriter::overflowed(const char *what, uint8_t itemId) {
  if(!this->fmt.overflowed()) return false;
  ESP_LOGE(TAG, "%s %u doesn't fit in %u bytes, leaving it out", what, itemId, (unsigned)this->unitSize);
  this->unitLen = 0;
  return true;
}
// Starts a list item, after a comma unless it is the first.
size_t SnapshotWriter::itemStart() {
  size_t pos = 0;
  if(!this->firstItem) this->unit[pos++] = ',';
  this->resetFmt(pos);
  return pos;
}
// One piece of a group: its ref, then each linked shade, then the close.
// inList is whether it is an item of a list rather than the whole document.
void SnapshotWriter::groupUnit(const group_snapshot_t &group, bool inList, const char *close) {
  auto done = [this, inList]() {
    this->groupOpen = false;
    if(inList) this->idx++;
    else this->nextStep();
  };
  if(!this->groupOpen) {
    const size_t pos = inList ? this->itemStart() : 0;
    if(!inList) this->resetFmt(0);
    this->fmt.beginObject();
    writeGroupRef(this->fmt, group);
    this->fmt.beginArray("linkedShades");
    if(this->overflowed("group", group.groupId)) { done(); return; }
    this->unitLen = pos + this->fmt.length();
    this->groupOpen = true;
    this->lsIdx = 0;
    this->firstLinked = true;
    if(inList) this->firstItem = false;
    return;
  }
  const shade_snapshot_t *shade = nullptr;
  while(this->lsIdx < group.linkedShadeCount && !(shade = this->snap.shade(group.linkedShades[this->lsIdx]))) this->lsIdx++;
  if(!shade) {
    strcpy(this->unit, close);
    this->unitLen = strlen(close);
    done();
    return;
  }
  size_t pos = 0;
  if(!this->firstLinked) this->unit[pos++] = ',';
  this->resetFmt(pos);
  this->fmt.beginObject();
  writeShadeRef(this->fmt, *shade);
  this->fmt.endObject();
  this->lsIdx++;
  if(this->overflowed("linked shade", shade->shadeId)) return;
  this->unitLen = pos + this->fmt.length();
  this->firstLinked = false;
}
void SnapshotWriter::produceUnit() {
  this->unitLen = 0;
  switch(this->steps[this->step]) {
    case ST_HEADER:
      this->resetFmt(0);
      this->fmt.beginObject();
      if(this->header) this->header(this->fmt, this->snap);
      this->fmt.beginArray("rooms");
      // Only fixed size values so this can't happen short of a bug.
      if(this->fmt.overflowed()) ESP_LOGE(TAG, "Header doesn't fit in %u bytes", (unsigned)this->unitSize);
      this->unitLen = this->fmt.length();
      this->nextStep();
      return;
    case ST_ROOMS: {
      if(this->idx >= this->snap.roomCount) { this->nextStep(); return; }
      const room_snapshot_t &room = this->snap.rooms[this->idx++];
      const size_t pos = this->itemStart();
      this->fmt.beginObject();
      writeRoom(this->fmt, room);
      this->fmt.endObject();
      if(this->overflowed("room", room.roomId)) return;
      this->unitLen = pos + this->fmt.length();
      this->firstItem = false;
      return;
    }
    case ST_SHADES: {
      if(this->idx >= this->snap.shadeCount) { this->nextStep(); return; }
      const shade_snapshot_t &shade = this->snap.shades[this->idx++];
      const size_t pos = this->itemStart();
      this->fmt.beginObject();
      writeShade(this->fmt, shade);
      this->fmt.endObject();
      if(this->overflowed("shade", shade.shadeId)) return;
      this->unitLen = pos + this->fmt.length();
      this->firstItem = false;
      return;
    }
    case ST_GROUPS:
      if(this->idx >= this->snap.groupCount) { this->nextStep(); return; }
      this->groupUnit(this->snap.groups[this->idx], true);
      return;
    case ST_REPEATERS: {
      if(this->idx >= this->snap.repeaterCount) { this->nextStep(); return; }
      size_t pos = 0;
      if(!this->firstItem) this->unit[pos++] = ',';
      pos += snprintf(this->unit + pos, this->unitSize - pos, "%lu", (unsigned long)this->snap.repeaters[this->idx++]);
      this->unitLen = pos;
      this->firstItem = false;
      return;
    }
    case ST_ROOM: {
      const room_snapshot_t *room = this->snap.room(this->id);
      if(!room) { this->literal("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"); return; }
      this->resetFmt(0);
      this->fmt.beginObject();
      writeRoom(this->fmt, *room);
      this->fmt.endObject();
      if(!this->overflowed("room", room->roomId)) this->unitLen = this->fmt.length();
      this->nextStep();
      return;
    }
    case ST_SHADE: {
      const shade_snapshot_t *shade = this->snap.shade(this->id);
      if(!shade) { this->literal("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"); return; }
      this->resetFmt(0);
      this->fmt.beginObject();
      writeShade(this->fmt, *shade);
      this->fmt.endObject();
      if(!this->overflowed("shade", shade->shadeId)) this->unitLen = this->fmt.length();
      this->nextStep();
      return;
    }
    case ST_GROUP: {
      const group_snapshot_t *group = this->snap.group(this->id);
      if(!group) { this->literal("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"); return; }
      this->groupUnit(*group, false);
      return;
    }
    case ST_NEXT_ROOM:
    case ST_NEXT_SHADE:
    case ST_NEXT_GROUP:
      this->resetFmt(0);
      this->fmt.beginObject();
      if(this->steps[this->step] == ST_NEXT_ROOM) this->fmt.addElem("roomId", nextRoomId(this->snap));
      else if(this->steps[this->step] == ST_NEXT_SHADE) {
        const uint8_t shadeId = nextShadeId(this->snap);
        this->fmt.addElem("shadeId", shadeId);
        this->fmt.addElem("remoteAddress", nextRemoteAddress(this->snap, shadeId));
        this->fmt.addElem("bitLength", this->snap.radioBitLength);
        this->fmt.addElem("stepSize", (uint8_t)100);
        this->fmt.addElem("proto", this->snap.radioProto);
      }
      else {
        const uint8_t groupId = nextGroupId(this->snap);
        this->fmt.addElem("groupId", groupId);
        this->fmt.addElem("remoteAddress", nextRemoteAddress(this->snap, groupId));
        this->fmt.addElem("bitLength", this->snap.radioBitLength);
        this->fmt.addElem("proto", this->snap.radioProto);
      }
      this->fmt.endObject();
      this->unitLen = this->fmt.length();
      this->nextStep();
      return;
    case ST_GROUP_OPTIONS: {
      const group_snapshot_t *group = this->snap.group(this->id);
      if(!group) { this->finish("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"); return; }
      this->groupUnit(*group, false, "],\"availShades\":[");
      return;
    }
    case ST_AVAIL_SHADES: {
      const group_snapshot_t *group = this->snap.group(this->id);
      if(!group || this->idx >= this->snap.shadeCount) { this->nextStep(); return; }
      const shade_snapshot_t &shade = this->snap.shades[this->idx++];
      for(uint8_t i = 0; i < group->linkedShadeCount; i++)
        if(group->linkedShades[i] == shade.shadeId) return;
      const size_t pos = this->itemStart();
      this->fmt.beginObject();
      writeShadeRef(this->fmt, shade);
      this->fmt.endObject();
      if(this->overflowed("shade", shade.shadeId)) return;
      this->unitLen = pos + this->fmt.length();
      this->firstItem = false;
      return;
    }
    case ST_OPEN_ARRAY: this->literal("["); return;
    case ST_CLOSE_ARRAY: this->literal("]"); return;
    case ST_TO_SHADES: this->literal("],\"shades\":["); return;
    case ST_TO_GROUPS: this->literal("],\"groups\":["); return;
    case ST_TO_REPEATERS: this->literal("],\"repeaters\":["); return;
    case ST_CLOSE_DOC: this->literal("]}"); return;
    default:
      return;
  }
}

/*********************************************************************
 * Slots
 ********************************************************************/
int8_t SnapshotSlots::acquire() {
  for(uint8_t i = 0; i < SNAPSHOT_SLOTS; i++) {
    uint32_t expected = FREE;
    if(!this->state[i].compare_exchange_strong(expected, WAITING)) continue;
    // Only the holder of a WAITING slot touches its pointer until it is queued.
    this->snaps[i] = new (std::nothrow) model_snapshot_t;
    if(!this->snaps[i]) {
      this->state[i].store(FREE);
      return -1;
    }
    return static_cast<int8_t>(i);
  }
  return -1;
}
// The snapshot goes before the slot is marked free, so acquire() never sees a
// free slot that still holds one.
void SnapshotSlots::freeSlot(uint8_t slot) {
  delete this->snaps[slot];
  this->snaps[slot] = nullptr;
  this->state[slot].store(FREE);
}
void SnapshotSlots::fill(uint8_t slot, void (*fill)(model_snapshot_t &snap)) {
  uint32_t expected = WAITING;
  if(!this->state[slot].compare_exchange_strong(expected, FILLING)) {
    // The read went away before it was filled; nobody else will free it.
    if(expected == ABANDONED) this->freeSlot(slot);
    return;
  }
  model_snapshot_t &snap = *this->snaps[slot];
  snap.startingAddress = 0;
  snap.radioBitLength = snap.radioProto = 0;
  snap.roomCount = snap.shadeCount = snap.groupCount = snap.repeaterCount = 0;
  fill(snap);
  expected = FILLING;
  // Released while being filled: the read is gone, so free it here.
  if(!this->state[slot].compare_exchange_strong(expected, FILLED)) this->freeSlot(slot);
}
bool SnapshotSlots::filled(uint8_t slot) const { return this->state[slot].load() == FILLED; }
void SnapshotSlots::cancel(uint8_t slot) { this->freeSlot(slot); }
void SnapshotSlots::release(uint8_t slot) {
  // Still waiting for or being filled by loop(): it frees the slot when it sees
  // ABANDONED.  Filled: nothing else touches it, so free it now.
  if(this->state[slot].exchange(ABANDONED) == FILLED) this->freeSlot(slot);
}
