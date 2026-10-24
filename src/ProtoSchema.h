#ifndef PROTO_SCHEMA_H
#define PROTO_SCHEMA_H
#include <stdint.h>
#include <type_traits>
#include "JsonFormatter.h"

// Socket protocol 2 (docs/PROTOCOL.md, docs/adr/0005-one-websocket-protocol.md):
// the one table of message types, fields, error codes and feeds.  The device
// sends it to clients as the schema, so it is the only copy anywhere.  Writers
// name fields by proto_field_t, so a field that isn't declared here can't be
// sent.  Kept free of the shade model and the web server so it can be unit
// tested on the PC (pio test -e native).

#define PROTO_VERSION 2

// Message types: 1-15 control, 16-127 client ops, 128-255 device events.  A
// number is never reused; a retired type stays reserved.
// TYPE(name, number, sender): sender is 'c' client, 'd' device, 'b' both.
#define PROTO_TYPES(TYPE) \
  TYPE(hello, 1, 'd') \
  TYPE(reply, 2, 'd') \
  TYPE(next, 3, 'c') \
  TYPE(sync, 4, 'b') \
  TYPE(syncEnd, 5, 'd') \
  TYPE(subscribe, 6, 'c') \
  TYPE(unsubscribe, 7, 'c') \
  TYPE(shadeCommand, 16, 'c') \
  TYPE(groupCommand, 17, 'c') \
  TYPE(tiltCommand, 18, 'c') \
  TYPE(repeatCommand, 19, 'c') \
  TYPE(setPositions, 20, 'c') \
  TYPE(setSensor, 21, 'c') \
  TYPE(setMyPosition, 22, 'c') \
  TYPE(unpairShade, 23, 'c') \
  TYPE(setRollingCode, 24, 'c') \
  TYPE(sendRemoteCommand, 25, 'c') \
  TYPE(addRoom, 26, 'c') \
  TYPE(addShade, 27, 'c') \
  TYPE(addGroup, 28, 'c') \
  TYPE(saveRoom, 29, 'c') \
  TYPE(saveShade, 30, 'c') \
  TYPE(saveGroup, 31, 'c') \
  TYPE(deleteRoom, 32, 'c') \
  TYPE(deleteShade, 33, 'c') \
  TYPE(deleteGroup, 34, 'c') \
  TYPE(linkRemote, 35, 'c') \
  TYPE(unlinkRemote, 36, 'c') \
  TYPE(linkToGroup, 37, 'c') \
  TYPE(unlinkFromGroup, 38, 'c') \
  TYPE(linkRepeater, 39, 'c') \
  TYPE(unlinkRepeater, 40, 'c') \
  TYPE(setPaired, 41, 'c') \
  TYPE(roomSortOrder, 42, 'c') \
  TYPE(shadeSortOrder, 43, 'c') \
  TYPE(groupSortOrder, 44, 'c') \
  TYPE(getNextRoom, 45, 'c') \
  TYPE(getNextShade, 46, 'c') \
  TYPE(getNextGroup, 47, 'c') \
  TYPE(groupOptions, 48, 'c') \
  TYPE(shadeState, 128, 'd') \
  TYPE(shade, 129, 'd') \
  TYPE(shadeRemoved, 130, 'd') \
  TYPE(group, 131, 'd') \
  TYPE(groupState, 132, 'd') \
  TYPE(groupRemoved, 133, 'd') \
  TYPE(room, 134, 'd') \
  TYPE(roomRemoved, 135, 'd') \
  TYPE(roomOrder, 136, 'd') \
  TYPE(shadeOrder, 137, 'd') \
  TYPE(groupOrder, 138, 'd') \
  TYPE(shadeCommanded, 139, 'd') \
  TYPE(memory, 140, 'd') \
  TYPE(wifi, 141, 'd') \
  TYPE(ethernet, 142, 'd') \
  TYPE(frame, 143, 'd') \
  TYPE(frequencyScan, 144, 'd') \
  TYPE(repeaters, 145, 'd')

// Fields, in one dictionary for every type.  FIELD(name, key, kind, default):
// kind is b bool (sent as 1/0), i integer, n number with a fraction, s string,
// l list, o object.  A field at its default is left out; a string's default is
// "" and a list's is empty.  hello's and the schema's own keys (fw, sh, T, F,
// E, Q) are fixed, as a client needs them before it has the schema.
#define PROTO_FIELDS(FIELD) \
  /* the schema and the sync */ \
  FIELD(types, "T", 'l', 0) \
  FIELD(fields, "F", 'l', 0) \
  FIELD(errors, "E", 'l', 0) \
  FIELD(feeds, "Q", 'l', 0) \
  FIELD(header, "h", 'o', 0) \
  FIELD(rooms, "R", 'l', 0) \
  FIELD(shades, "S", 'l', 0) \
  FIELD(groups, "G", 'l', 0) \
  FIELD(repeaters, "X", 'l', 0) \
  FIELD(status, "Z", 'o', 0) \
  FIELD(fw, "fw", 's', 0) \
  FIELD(schemaHash, "sh", 's', 0) \
  /* replies and control */ \
  FIELD(ok, "k", 'b', 0) \
  FIELD(code, "e", 'i', 0) \
  FIELD(desc, "x", 's', 0) \
  FIELD(feed, "fe", 'i', 0) \
  FIELD(ids, "is", 'l', 0) \
  /* rooms, shades and groups */ \
  FIELD(shadeId, "s", 'i', 0) \
  FIELD(groupId, "g", 'i', 0) \
  FIELD(roomId, "r", 'i', 0) \
  FIELD(name, "n", 's', 0) \
  FIELD(remoteAddress, "a", 'i', 0) \
  FIELD(lastRollingCode, "c", 'i', 0) \
  FIELD(sortOrder, "o", 'i', 0) \
  FIELD(flags, "f", 'i', 0) \
  FIELD(position, "p", 'i', 0) \
  FIELD(target, "t", 'i', 0) \
  FIELD(direction, "d", 'i', 0) \
  FIELD(myPos, "m", 'i', -1) \
  FIELD(tiltType, "y", 'i', 0) \
  FIELD(tiltPosition, "q", 'i', 0) \
  FIELD(tiltTarget, "u", 'i', 0) \
  FIELD(tiltDirection, "w", 'i', 0) \
  FIELD(myTiltPos, "M", 'i', -1) \
  FIELD(upTime, "U", 'i', 0) \
  FIELD(downTime, "D", 'i', 0) \
  FIELD(tiltTime, "Y", 'i', 0) \
  FIELD(stepSize, "z", 'i', 100) \
  FIELD(paired, "i", 'b', 0) \
  FIELD(shadeType, "st", 'i', 0) \
  FIELD(bitLength, "bl", 'i', 56) \
  FIELD(proto, "pr", 'i', 0) \
  FIELD(flipCommands, "fc", 'b', 0) \
  FIELD(flipPosition, "fp", 'b', 0) \
  FIELD(inGroup, "ig", 'b', 0) \
  FIELD(sunSensor, "ss", 'b', 0) \
  FIELD(light, "li", 'b', 0) \
  FIELD(simMy, "sm", 'b', 0) \
  FIELD(repeats, "rp", 'i', 1) \
  FIELD(gpioUp, "gu", 'i', 0) \
  FIELD(gpioDown, "gd", 'i', 0) \
  FIELD(gpioMy, "gm", 'i', 0) \
  FIELD(gpioLLTrigger, "gl", 'b', 0) \
  FIELD(linkedRemotes, "lk", 'l', 0) \
  FIELD(linkedShades, "ls", 'l', 0) \
  /* the command ops and shadeCommanded */ \
  FIELD(command, "cm", 's', 0) \
  FIELD(source, "so", 's', 0) \
  FIELD(sourceAddress, "sr", 'i', 0) \
  FIELD(repeat, "re", 'i', 0) \
  FIELD(sunny, "sn", 'b', 0) \
  FIELD(windy, "wn", 'b', 0) \
  /* the edit ops */ \
  FIELD(validation, "vc", 'i', 0) \
  /* the header */ \
  FIELD(maxRooms, "xr", 'i', 0) \
  FIELD(maxShades, "xs", 'i', 0) \
  FIELD(maxGroups, "xg", 'i', 0) \
  FIELD(maxGroupedShades, "xm", 'i', 0) \
  FIELD(maxLinkedRemotes, "xl", 'i', 0) \
  FIELD(startingAddress, "sa", 'i', 0) \
  FIELD(serverId, "sv", 's', 0) \
  FIELD(hostname, "hn", 's', 0) \
  FIELD(model, "md", 's', 0) \
  FIELD(chipModel, "cp", 's', 0) \
  FIELD(connType, "ct", 's', 0) \
  /* memory, wifi and ethernet */ \
  FIELD(freeHeap, "fr", 'i', 0) \
  FIELD(maxHeap, "mx", 'i', 0) \
  FIELD(minHeap, "mn", 'i', 0) \
  FIELD(totalHeap, "to", 'i', 0) \
  FIELD(uptime, "up", 'i', 0) \
  FIELD(loopRate, "lp", 'i', 0) \
  FIELD(loopMaxUs, "lx", 'i', 0) \
  FIELD(ssid, "sd", 's', 0) \
  FIELD(strength, "sg", 'i', 0) \
  FIELD(channel, "ch", 'i', 0) \
  FIELD(connected, "co", 'b', 0) \
  FIELD(speed, "sp", 'i', 0) \
  FIELD(fullDuplex, "fd", 'b', 0) \
  /* the frame and frequency scan feeds */ \
  FIELD(encKey, "ek", 'i', 0) \
  FIELD(rawCmd, "rw", 'i', 0) \
  FIELD(rssi, "rs", 'i', 0) \
  FIELD(bits, "bt", 'i', 0) \
  FIELD(valid, "va", 'b', 0) \
  FIELD(hwsync, "hs", 'i', 0) \
  FIELD(pulses, "pu", 'l', 0) \
  FIELD(scanning, "sc", 'b', 0) \
  FIELD(testFreq, "tf", 'n', 0) \
  FIELD(testRssi, "tr", 'i', 0) \
  FIELD(frequency, "fq", 'n', 0)

// Error codes in a reply.  ERR(name, number)
#define PROTO_ERRORS(ERR) \
  ERR(badRequest, 1) \
  ERR(unknownOp, 2) \
  ERR(unauthorized, 3) \
  ERR(notFound, 4) \
  ERR(busy, 5) \
  ERR(radioUnavailable, 6) \
  ERR(internal, 7)

// Feeds a client can subscribe to.  FEED(name, number)
#define PROTO_FEEDS(FEED) \
  FEED(frames, 1) \
  FEED(frequencyScan, 2)

#define PROTO_ENUM_ENTRY(name, number, ...) name = number,
enum class proto_type_t : uint8_t { PROTO_TYPES(PROTO_ENUM_ENTRY) };
enum class proto_error_t : uint8_t { none = 0, PROTO_ERRORS(PROTO_ENUM_ENTRY) };
enum class proto_feed_t : uint8_t { PROTO_FEEDS(PROTO_ENUM_ENTRY) };
#undef PROTO_ENUM_ENTRY
#define PROTO_FIELD_ENTRY(name, ...) name,
enum class proto_field_t : uint8_t { PROTO_FIELDS(PROTO_FIELD_ENTRY) count };
#undef PROTO_FIELD_ENTRY

struct proto_field_info_t {
  const char *name;
  const char *key;
  char kind;
  int32_t def;
};
struct proto_named_t {
  uint8_t number;
  const char *name;
  char sender;  // types only
};
const proto_field_info_t &protoField(proto_field_t field);
const proto_field_info_t *protoFieldByKey(const char *key);
// The tables as the schema lists them, in declaration order.
uint8_t protoTypeCount();
const proto_named_t &protoTypeAt(uint8_t index);
uint8_t protoErrorCount();
const proto_named_t &protoErrorAt(uint8_t index);
uint8_t protoFeedCount();
const proto_named_t &protoFeedAt(uint8_t index);
const char *protoTypeName(uint8_t number);
// What a client may send: control messages it sends and the ops.
bool protoClientMaySend(uint8_t type);
// Hex FNV-1a of every row of the schema: 8 characters and a terminator.
const char *protoSchemaHash();

// Writes fields by their short keys into the open object of json: booleans as
// 1/0, and nothing at all for a value at the field's default.
class ProtoWriter {
  public:
    explicit ProtoWriter(JsonFormatter &json) : json(json) {}
    template<typename T> void add(proto_field_t field, T value) {
      const proto_field_info_t &f = protoField(field);
      if constexpr(std::is_same<T, bool>::value) {
        if((value ? 1 : 0) == f.def) return;
        this->json.addElem(f.key, (uint8_t)(value ? 1 : 0));
      }
      else if constexpr(std::is_floating_point<T>::value) {
        if(value == (T)f.def) return;
        this->json.addElem(f.key, (float)value);
      }
      else if constexpr(std::is_integral<T>::value) {
        this->addInt(f, (int64_t)value);
      }
      else {
        const char *s = value;
        if(!s || !s[0]) return;
        this->json.addElem(f.key, s);
      }
    }
    void beginList(proto_field_t field) { this->json.beginArray(protoField(field).key); }
    void endList() { this->json.endArray(); }
    void beginObject(proto_field_t field) { this->json.beginObject(protoField(field).key); }
    void endObject() { this->json.endObject(); }
    // An object or a value inside a list.
    void beginItem() { this->json.beginObject(); }
    void endItem() { this->json.endObject(); }
    void item(int64_t value);
    JsonFormatter &json;
  private:
    void addInt(const proto_field_info_t &f, int64_t value);
};
#endif
