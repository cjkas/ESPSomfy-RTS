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
// T(name, number, sender): sender is 'c' client, 'd' device, 'b' both.
#define PROTO_TYPES(T) \
  T(hello, 1, 'd') \
  T(reply, 2, 'd') \
  T(next, 3, 'c') \
  T(sync, 4, 'b') \
  T(syncEnd, 5, 'd') \
  T(subscribe, 6, 'c') \
  T(unsubscribe, 7, 'c') \
  T(shadeState, 128, 'd') \
  T(shade, 129, 'd') \
  T(shadeRemoved, 130, 'd') \
  T(group, 131, 'd') \
  T(groupState, 132, 'd') \
  T(groupRemoved, 133, 'd') \
  T(room, 134, 'd') \
  T(roomRemoved, 135, 'd') \
  T(roomOrder, 136, 'd') \
  T(shadeOrder, 137, 'd') \
  T(groupOrder, 138, 'd') \
  T(shadeCommand, 139, 'd') \
  T(memory, 140, 'd') \
  T(wifi, 141, 'd') \
  T(ethernet, 142, 'd') \
  T(frame, 143, 'd') \
  T(frequencyScan, 144, 'd') \
  T(repeaters, 145, 'd')

// Fields, in one dictionary for every type.  F(name, key, kind, default):
// kind is b bool (sent as 1/0), i integer, n number with a fraction, s string,
// l list, o object.  A field at its default is left out; a string's default is
// "" and a list's is empty.  hello's and the schema's own keys (fw, sh, T, F,
// E, Q) are fixed, as a client needs them before it has the schema.
#define PROTO_FIELDS(F) \
  /* the schema and the sync */ \
  F(types, "T", 'l', 0) \
  F(fields, "F", 'l', 0) \
  F(errors, "E", 'l', 0) \
  F(feeds, "Q", 'l', 0) \
  F(header, "h", 'o', 0) \
  F(rooms, "R", 'l', 0) \
  F(shades, "S", 'l', 0) \
  F(groups, "G", 'l', 0) \
  F(repeaters, "X", 'l', 0) \
  F(status, "Z", 'o', 0) \
  F(fw, "fw", 's', 0) \
  F(schemaHash, "sh", 's', 0) \
  /* replies and control */ \
  F(ok, "k", 'b', 0) \
  F(code, "e", 'i', 0) \
  F(desc, "x", 's', 0) \
  F(feed, "fe", 'i', 0) \
  F(ids, "is", 'l', 0) \
  /* rooms, shades and groups */ \
  F(shadeId, "s", 'i', 0) \
  F(groupId, "g", 'i', 0) \
  F(roomId, "r", 'i', 0) \
  F(name, "n", 's', 0) \
  F(remoteAddress, "a", 'i', 0) \
  F(lastRollingCode, "c", 'i', 0) \
  F(sortOrder, "o", 'i', 0) \
  F(flags, "f", 'i', 0) \
  F(position, "p", 'i', 0) \
  F(target, "t", 'i', 0) \
  F(direction, "d", 'i', 0) \
  F(myPos, "m", 'i', -1) \
  F(tiltType, "y", 'i', 0) \
  F(tiltPosition, "q", 'i', 0) \
  F(tiltTarget, "u", 'i', 0) \
  F(tiltDirection, "w", 'i', 0) \
  F(myTiltPos, "M", 'i', -1) \
  F(upTime, "U", 'i', 0) \
  F(downTime, "D", 'i', 0) \
  F(tiltTime, "Y", 'i', 0) \
  F(stepSize, "z", 'i', 100) \
  F(paired, "i", 'b', 0) \
  F(shadeType, "st", 'i', 0) \
  F(bitLength, "bl", 'i', 56) \
  F(proto, "pr", 'i', 0) \
  F(flipCommands, "fc", 'b', 0) \
  F(flipPosition, "fp", 'b', 0) \
  F(inGroup, "ig", 'b', 0) \
  F(sunSensor, "ss", 'b', 0) \
  F(light, "li", 'b', 0) \
  F(simMy, "sm", 'b', 0) \
  F(repeats, "rp", 'i', 1) \
  F(gpioUp, "gu", 'i', 0) \
  F(gpioDown, "gd", 'i', 0) \
  F(gpioMy, "gm", 'i', 0) \
  F(gpioLLTrigger, "gl", 'b', 0) \
  F(linkedRemotes, "lk", 'l', 0) \
  F(linkedShades, "ls", 'l', 0) \
  /* shadeCommand */ \
  F(command, "cm", 's', 0) \
  F(source, "so", 's', 0) \
  F(sourceAddress, "sr", 'i', 0) \
  /* the header */ \
  F(maxRooms, "xr", 'i', 0) \
  F(maxShades, "xs", 'i', 0) \
  F(maxGroups, "xg", 'i', 0) \
  F(maxGroupedShades, "xm", 'i', 0) \
  F(maxLinkedRemotes, "xl", 'i', 0) \
  F(startingAddress, "sa", 'i', 0) \
  F(serverId, "sv", 's', 0) \
  F(hostname, "hn", 's', 0) \
  F(model, "md", 's', 0) \
  F(chipModel, "cp", 's', 0) \
  F(connType, "ct", 's', 0) \
  /* memory, wifi and ethernet */ \
  F(freeHeap, "fr", 'i', 0) \
  F(maxHeap, "mx", 'i', 0) \
  F(minHeap, "mn", 'i', 0) \
  F(totalHeap, "to", 'i', 0) \
  F(uptime, "up", 'i', 0) \
  F(loopRate, "lp", 'i', 0) \
  F(loopMaxUs, "lx", 'i', 0) \
  F(ssid, "sd", 's', 0) \
  F(strength, "sg", 'i', 0) \
  F(channel, "ch", 'i', 0) \
  F(connected, "co", 'b', 0) \
  F(speed, "sp", 'i', 0) \
  F(fullDuplex, "fd", 'b', 0) \
  /* the frame and frequency scan feeds */ \
  F(encKey, "ek", 'i', 0) \
  F(rawCmd, "rw", 'i', 0) \
  F(rssi, "rs", 'i', 0) \
  F(bits, "bt", 'i', 0) \
  F(valid, "va", 'b', 0) \
  F(hwsync, "hs", 'i', 0) \
  F(pulses, "pu", 'l', 0) \
  F(scanning, "sc", 'b', 0) \
  F(testFreq, "tf", 'n', 0) \
  F(testRssi, "tr", 'i', 0) \
  F(frequency, "fq", 'n', 0)

// Error codes in a reply.  E(name, number)
#define PROTO_ERRORS(E) \
  E(badRequest, 1) \
  E(unknownOp, 2) \
  E(unauthorized, 3) \
  E(notFound, 4) \
  E(busy, 5) \
  E(radioUnavailable, 6) \
  E(internal, 7)

// Feeds a client can subscribe to.  Q(name, number)
#define PROTO_FEEDS(Q) \
  Q(frames, 1) \
  Q(frequencyScan, 2)

#define PROTO_ENUM_ENTRY(name, number, ...) name = number,
enum class proto_type_t : uint8_t { PROTO_TYPES(PROTO_ENUM_ENTRY) };
enum class proto_error_t : uint8_t { none = 0, PROTO_ERRORS(PROTO_ENUM_ENTRY) };
enum class proto_feed_t : uint8_t { PROTO_FEEDS(PROTO_ENUM_ENTRY) };
#undef PROTO_ENUM_ENTRY
#define PROTO_FIELD_ENTRY(name, ...) name,
enum class pf : uint8_t { PROTO_FIELDS(PROTO_FIELD_ENTRY) count };
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
const proto_field_info_t &protoField(pf field);
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
    template<typename T> void add(pf field, T value) {
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
    void beginList(pf field) { this->json.beginArray(protoField(field).key); }
    void endList() { this->json.endArray(); }
    void beginObject(pf field) { this->json.beginObject(protoField(field).key); }
    void endObject() { this->json.endObject(); }
    // An object or a value inside a list.
    void beginItem() { this->json.beginObject(); }
    void endItem() { this->json.endObject(); }
    void item(int64_t value);
    JsonFormatter &json;
  private:
    void addInt(const proto_field_info_t &f, int64_t value);
};

// A frame: {"v":2,"t":<type>,"id":<id>,"d":{ ... }}.  id 0 leaves it out.
void beginFrame(JsonFormatter &json, proto_type_t type, uint32_t id = 0);
void endFrame(JsonFormatter &json);
// A reply carrying an error.
void writeErrorReply(JsonFormatter &json, uint32_t id, proto_error_t code, const char *desc);
// hello: the firmware version and the schema hash.
void writeHello(JsonFormatter &json, const char *fw);
#endif
