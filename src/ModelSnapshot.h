#ifndef MODEL_SNAPSHOT_H
#define MODEL_SNAPSHOT_H
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include "ShadeTypes.h"
#include "JsonFormatter.h"

// A copy of the shade model taken on loop() for one read, so the reply
// describes a single moment (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md),
// and the JSON the read routes write from it.  Kept free of the shade model and
// the web server so it can be unit tested on the PC (pio test -e native).

// Positions are as clients see them: whole percentages, flipped for a shade
// with flipPosition, -1 when unset.
// No default member values: with them the SnapshotSlots would be stored in
// flash as initialized data (11 KB).  The snapshot functions set every field;
// anything else starts from {}.
struct room_snapshot_t {
  uint8_t roomId;
  char name[21];
  int8_t sortOrder;
};
struct linked_remote_snapshot_t {
  uint32_t remoteAddress;
  uint16_t lastRollingCode;
};
struct shade_snapshot_t {
  uint8_t shadeId;
  uint8_t roomId;
  char name[21];
  uint32_t remoteAddress;
  uint32_t upTime;
  uint32_t downTime;
  uint32_t tiltTime;
  uint16_t stepSize;
  bool paired;
  uint16_t lastRollingCode;
  int8_t position;
  int8_t target;
  int8_t myPos;
  int8_t direction;
  uint8_t tiltType;
  int8_t tiltPosition;
  int8_t tiltTarget;
  int8_t myTiltPos;
  int8_t tiltDirection;
  uint8_t shadeType;
  uint8_t bitLength;
  uint8_t proto;
  uint8_t flags;
  bool flipCommands;
  bool flipPosition;
  bool inGroup;
  bool sunSensor;
  bool light;
  bool simMy;
  uint8_t repeats;
  int8_t sortOrder;
  uint8_t gpioUp;
  uint8_t gpioDown;
  uint8_t gpioMy;
  bool gpioLLTrigger;
  uint8_t linkedRemoteCount;
  linked_remote_snapshot_t linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
};
struct group_snapshot_t {
  uint8_t groupId;
  uint8_t roomId;
  char name[21];
  uint32_t remoteAddress;
  uint16_t lastRollingCode;
  uint8_t bitLength;
  uint8_t proto;
  bool sunSensor;
  bool flipCommands;
  uint8_t flags;          // its linked shades' flags together
  uint8_t repeats;
  int8_t sortOrder;
  uint8_t linkedShadeCount;
  uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
};
// Only the rooms, shades, groups and repeaters that exist, in model order.
struct model_snapshot_t {
  uint32_t startingAddress;
  uint8_t radioBitLength;     // what a new shade or group gets
  uint8_t radioProto;
  uint8_t roomCount;
  uint8_t shadeCount;
  uint8_t groupCount;
  uint8_t repeaterCount;
  room_snapshot_t rooms[SOMFY_MAX_ROOMS];
  shade_snapshot_t shades[SOMFY_MAX_SHADES];
  group_snapshot_t groups[SOMFY_MAX_GROUPS];
  uint32_t repeaters[SOMFY_MAX_REPEATERS];
  const room_snapshot_t *room(uint8_t roomId) const;
  const shade_snapshot_t *shade(uint8_t shadeId) const;
  const group_snapshot_t *group(uint8_t groupId) const;
};

// The fields of one item, as every route has always written them.
void writeRoom(JsonFormatter &json, const room_snapshot_t &room);
void writeShadeRef(JsonFormatter &json, const shade_snapshot_t &shade);
void writeShade(JsonFormatter &json, const shade_snapshot_t &shade);
void writeGroupRef(JsonFormatter &json, const group_snapshot_t &group);
// The shade a group links to, or nullptr when it has gone.
typedef const shade_snapshot_t *(*shade_lookup_t)(uint8_t shadeId, void *ctx);
// writeGroupRef plus linkedShades, the refs of the shades it links to.
void writeGroup(JsonFormatter &json, const group_snapshot_t &group, shade_lookup_t lookup, void *ctx);

// The documents the read routes send.
enum class snapshot_doc_t : uint8_t {
  controller,  // /controller
  discovery,   // /discovery
  rooms,       // /rooms
  shades,      // /shades
  groups,      // /groups
  room,        // /room?roomId=
  shade,       // /shade?shadeId=
  group,       // /group?groupId=
  repeaters,   // /repeaters
  nextRoom,    // /getNextRoom: the id and defaults a new room would get
  nextShade,   // /getNextShade
  nextGroup,   // /getNextGroup
  groupOptions // /groupOptions?groupId=: the group and the shades it could link
};
// Writes the fields of /controller or /discovery that aren't in the snapshot
// (device, radio and firmware details) into the open top-level object.
typedef void (*snapshot_header_t)(JsonFormatter &json, const model_snapshot_t &snap);

// Writes a document from a snapshot a piece at a time for a chunked response.
// Each room, shade, group and linked shade is formatted into unit on its own;
// one that doesn't fit is logged and left out so the JSON stays valid.
class SnapshotWriter {
  public:
    // id picks the item for the single-item documents.  header is only called
    // for controller and discovery.  unit must outlive the writer.
    SnapshotWriter(const model_snapshot_t &snap, snapshot_doc_t doc, uint8_t id, snapshot_header_t header, char *unit, size_t unitSize);
    // Copies up to maxLen more bytes of the document into out.  0 once it is all
    // out.
    size_t generate(uint8_t *out, size_t maxLen);
  private:
    const model_snapshot_t &snap;
    const uint8_t *steps;
    uint8_t step = 0;
    uint8_t id;
    snapshot_header_t header;
    BufferedJsonFormatter fmt;
    char *unit;
    size_t unitSize;
    size_t unitLen = 0;
    size_t consumed = 0;
    uint8_t idx = 0;
    uint8_t lsIdx = 0;          // next linked shade of the group being written
    bool groupOpen = false;     // the group's ref is out, its linked shades are next
    bool firstItem = true;
    bool firstLinked = true;
    void produceUnit();
    void nextStep();
    void literal(const char *text);
    void resetFmt(size_t offset);
    bool overflowed(const char *what, uint8_t itemId);
    size_t itemStart();
    void groupUnit(const group_snapshot_t &group, bool inList, const char *close = "]}");
    void finish(const char *text);
};
// What the next room, shade or group added would get: its id (0 for a room and
// 255 for a shade or group when there is no room for one) and remote address.
uint8_t nextRoomId(const model_snapshot_t &snap);
uint8_t nextShadeId(const model_snapshot_t &snap);
uint8_t nextGroupId(const model_snapshot_t &snap);
uint32_t nextRemoteAddress(const model_snapshot_t &snap, uint8_t id);

// The snapshots reads are served from.  A read takes a slot on the web server
// task and queues a fill; loop() fills it; the read streams it and lets it go.
// The read may go away before loop() gets to it, so the slot's state decides
// who frees it: whichever side is last.  A slot's snapshot (about 5.6 KB) is
// only allocated while a read holds it.
#define SNAPSHOT_SLOTS 2
class SnapshotSlots {
  public:
    // A free slot with its snapshot allocated, or -1 when every slot is in use
    // or there is no memory.  Any task.
    int8_t acquire();
    // Fills the slot with fill, unless its read has gone.  loop() only.
    void fill(uint8_t slot, void (*fill)(model_snapshot_t &snap));
    // Whether loop() has filled the slot.  The read's task.
    bool filled(uint8_t slot) const;
    // Valid from acquire() until the read releases the slot.
    model_snapshot_t &snapshot(uint8_t slot) { return *this->snaps[slot]; }
    // The read is done with the slot, or gone.  The read's task.
    void release(uint8_t slot);
    // Frees a slot whose fill was never queued.  The read's task.
    void cancel(uint8_t slot);
  private:
    enum : uint32_t { FREE, WAITING, FILLING, FILLED, ABANDONED };
    model_snapshot_t *snaps[SNAPSHOT_SLOTS] = {};
    std::atomic<uint32_t> state[SNAPSHOT_SLOTS] = {};
    void freeSlot(uint8_t slot);
};
#endif
