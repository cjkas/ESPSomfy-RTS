#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "esp_log.h"
#include "ProtoSocket.h"
#include "ProtoSync.h"
#include "ProtoParse.h"
#include "SomfySnapshot.h"
#include "Somfy.h"
#include "ConfigSettings.h"
#include "ESPNetwork.h"
#include "Utils.h"
#include "SocketEmitter.h"
#include "../entryPoints/Web.h"
#if HAS_ETHERNET
#include <ETH.h>
#endif

static const char *TAG = "Proto";

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern ESPNetwork net;
extern Web webServer;
extern LoopStats loopStats;

ProtoSocket protoSocket;
static AsyncWebSocket ws("/ws");
static_assert(MAX_SOCK_RESPONSE >= PROTO_MAX_FRAME, "protocol 2 frames share the socket event buffer");

// What a client asked for, from async_tcp to loop().
enum : uint32_t {
  REQ_START = 0x01,   // just connected
  REQ_NEXT = 0x02,
  REQ_RESYNC = 0x04
};
struct proto_slot_t {
  // Set and cleared on async_tcp; 0 when the slot is free.
  std::atomic<uint32_t> asyncId{0};
  std::atomic<uint32_t> requests{0};
  std::atomic<uint8_t> feeds{0};     // a bit per feed number
  std::atomic<bool> withSchema{true};
  // loop() only.
  uint32_t loopId = 0;               // the client loop() is serving here
  sync_cursor_t cur;
  bool awaitingNext = false;
  uint32_t sentAt = 0;
};
static proto_slot_t slots[PROTO_MAX_CLIENTS];
static BufferedJsonFormatter fmt;

static proto_slot_t *slotOf(uint32_t asyncId) {
  for(proto_slot_t &s : slots)
    if(s.asyncId.load() == asyncId) return &s;
  return nullptr;
}

/*********************************************************************
 * The shade model as a sync source
 ********************************************************************/
static void writeHeader(ProtoWriter &w) {
  w.add(pf::maxRooms, (uint8_t)SOMFY_MAX_ROOMS);
  w.add(pf::maxShades, (uint8_t)SOMFY_MAX_SHADES);
  w.add(pf::maxGroups, (uint8_t)SOMFY_MAX_GROUPS);
  w.add(pf::maxGroupedShades, (uint8_t)SOMFY_MAX_GROUPED_SHADES);
  w.add(pf::maxLinkedRemotes, (uint8_t)SOMFY_MAX_LINKED_REMOTES);
  w.add(pf::startingAddress, somfy.startingAddress);
  // What a new shade or group gets.
  w.add(pf::bitLength, somfy.transceiver.config.type);
  w.add(pf::proto, static_cast<uint8_t>(somfy.transceiver.config.proto));
  w.add(pf::serverId, settings.serverId);
  w.add(pf::hostname, settings.hostname);
  w.add(pf::model, "ESPSomfyRTS");
  w.add(pf::chipModel, settings.chipModel);
  w.add(pf::fw, settings.fwVersion.name);
  const char *connType = "Unknown";
  if(net.connType == conn_types_t::ethernet) connType = "Ethernet";
  else if(net.connType == conn_types_t::wifi) connType = "Wifi";
  w.add(pf::connType, connType);
}
static void writeMemory(ProtoWriter &w) {
  w.add(pf::freeHeap, ESP.getFreeHeap());
  w.add(pf::maxHeap, ESP.getMaxAllocHeap());
  w.add(pf::minHeap, ESP.getMinFreeHeap());
  w.add(pf::totalHeap, ESP.getHeapSize());
  w.add(pf::uptime, (uint32_t)millis());
  w.add(pf::loopRate, loopStats.publishedRate());
  w.add(pf::loopMaxUs, loopStats.publishedMaxUs());
}
static void writeWifi(ProtoWriter &w) {
  if(WiFi.status() == WL_CONNECTED) {
    w.add(pf::ssid, WiFi.SSID().c_str());
    w.add(pf::strength, (int32_t)WiFi.RSSI());
    w.add(pf::channel, (int32_t)net.channel);
  }
  else {
    w.add(pf::strength, (int32_t)-100);
    w.add(pf::channel, (int32_t)-1);
  }
}
static void writeEthernet(ProtoWriter &w) {
#if HAS_ETHERNET
  if(net.connType == conn_types_t::ethernet) {
    w.add(pf::connected, net.connected());
    w.add(pf::speed, (uint32_t)ETH.linkSpeed());
    w.add(pf::fullDuplex, ETH.fullDuplex());
  }
#else
  (void)w;
#endif
}
class LiveSource : public SyncSource {
  public:
    bool room(uint8_t slot, room_snapshot_t &out) override {
      if(somfy.rooms[slot].roomId == 0) return false;
      snapshotRoom(somfy.rooms[slot], out);
      return true;
    }
    bool shade(uint8_t slot, shade_snapshot_t &out) override {
      if(somfy.shades[slot].getShadeId() == 255) return false;
      snapshotShade(somfy.shades[slot], out);
      return true;
    }
    bool group(uint8_t slot, group_snapshot_t &out) override {
      if(somfy.groups[slot].getGroupId() == 255) return false;
      snapshotGroup(somfy.groups[slot], out);
      return true;
    }
    bool repeater(uint8_t slot, uint32_t &address) override {
      address = somfy.repeaters[slot];
      return address != 0;
    }
    void header(ProtoWriter &w) override { writeHeader(w); }
    void status(ProtoWriter &w) override {
      writeMemory(w);
      if(net.connType == conn_types_t::ethernet) writeEthernet(w);
      else writeWifi(w);
    }
};

/*********************************************************************
 * async_tcp: connections and what clients send
 ********************************************************************/
// Small fixed frames sent straight from async_tcp: nothing in them comes from
// the shade model, so ADR 0002 doesn't send them through loop().
static void sendFixed(AsyncWebSocketClient *client, void (*write)(JsonFormatter &json, void *ctx), void *ctx) {
  char buf[192];
  BufferedJsonFormatter json;
  json.setBuffer(buf, sizeof(buf));
  write(json, ctx);
  if(!json.overflowed()) client->text(buf, json.length());
}
struct error_reply_t {
  uint32_t id;
  proto_error_t code;
  const char *desc;
};
static void replyError(AsyncWebSocketClient *client, uint32_t id, proto_error_t code, const char *desc) {
  error_reply_t e = {id, code, desc};
  sendFixed(client, [](JsonFormatter &json, void *ctx) {
    const error_reply_t &e = *static_cast<error_reply_t *>(ctx);
    writeErrorReply(json, e.id, e.code, e.desc);
  }, &e);
}
static void replyOk(AsyncWebSocketClient *client, uint32_t id) {
  if(id == 0) return;
  sendFixed(client, [](JsonFormatter &json, void *ctx) {
    beginFrame(json, proto_type_t::reply, *static_cast<uint32_t *>(ctx));
    ProtoWriter(json).add(pf::ok, true);
    endFrame(json);
  }, &id);
}
static bool authorized(AsyncWebServerRequest *request) {
  if(settings.Security.type == security_types::None) return true;
  const AsyncWebParameter *key = request->getParam("apikey");
  if(!key || !request->client()) return false;
  char token[65];
  webServer.createAPIToken(request->client()->remoteIP(), token);
  return key->value() == token;
}
static void onConnect(AsyncWebSocketClient *client, AsyncWebServerRequest *request) {
  const uint32_t id = client->id();
  proto_slot_t *slot = nullptr;
  for(proto_slot_t &s : slots) {
    uint32_t expected = 0;
    if(s.asyncId.compare_exchange_strong(expected, id)) { slot = &s; break; }
  }
  if(!slot) {
    ESP_LOGW(TAG, "Socket [%lu] refused: all %u clients connected", (unsigned long)id, PROTO_MAX_CLIENTS);
    client->close();
    return;
  }
  const AsyncWebParameter *schema = request ? request->getParam("schema") : nullptr;
  slot->withSchema = !(schema && schema->value() == protoSchemaHash());
  slot->feeds = 0;
  client->setCloseClientOnQueueFull(true);
  sendFixed(client, [](JsonFormatter &json, void *) { writeHello(json, settings.fwVersion.name); }, nullptr);
  slot->requests.fetch_or(REQ_START);
  IPAddress ip = client->remoteIP();
  ESP_LOGI(TAG, "Socket [%lu] connected from %u.%u.%u.%u%s", (unsigned long)id, ip[0], ip[1], ip[2], ip[3], slot->withSchema ? "" : " (has the schema)");
}
static void onDisconnect(AsyncWebSocketClient *client) {
  proto_slot_t *slot = slotOf(client->id());
  if(!slot) return;
  slot->requests = 0;
  slot->feeds = 0;
  slot->asyncId = 0;
  ESP_LOGI(TAG, "Socket [%lu] disconnected", (unsigned long)client->id());
}
static void onFrame(AsyncWebSocketClient *client, AwsFrameInfo *info, uint8_t *data, size_t len) {
  if(!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
    replyError(client, 0, proto_error_t::badRequest, "Send each message as one text frame");
    return;
  }
  proto_slot_t *slot = slotOf(client->id());
  if(!slot) return;
  JsonDocument doc;
  proto_request_t req;
  const proto_error_t err = parseClientFrame(reinterpret_cast<const char *>(data), len, doc, req);
  if(err != proto_error_t::none) {
    replyError(client, req.id, err, err == proto_error_t::unknownOp ? "Unknown message type" : "Malformed message");
    return;
  }
  switch(static_cast<proto_type_t>(req.type)) {
    case proto_type_t::next:
      slot->requests.fetch_or(REQ_NEXT);
      return;
    case proto_type_t::sync:
      slot->requests.fetch_or(REQ_RESYNC);
      return;
    case proto_type_t::subscribe:
    case proto_type_t::unsubscribe: {
      const int32_t feed = protoInt(doc["d"], pf::feed);
      bool known = false;
      for(uint8_t i = 0; i < protoFeedCount(); i++) known |= protoFeedAt(i).number == feed;
      if(!known) { replyError(client, req.id, proto_error_t::badRequest, "Unknown feed"); return; }
      if(req.type == static_cast<uint8_t>(proto_type_t::subscribe)) slot->feeds.fetch_or(1 << feed);
      else slot->feeds.fetch_and(~(1 << feed));
      replyOk(client, req.id);
      return;
    }
    default:
      // The ops arrive with the protocol core's later PRs.
      replyError(client, req.id, proto_error_t::unknownOp, "Unknown message type");
      return;
  }
}
void ProtoSocket::wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server;
  switch(type) {
    case WS_EVT_CONNECT: onConnect(client, static_cast<AsyncWebServerRequest *>(arg)); break;
    case WS_EVT_DISCONNECT: onDisconnect(client); break;
    case WS_EVT_DATA: onFrame(client, static_cast<AwsFrameInfo *>(arg), data, len); break;
    default: break;
  }
}

void ProtoSocket::begin(AsyncWebServer &server) {
  this->_owner = xTaskGetCurrentTaskHandle();
  ws.handleHandshake(authorized);
  ws.onEvent(ProtoSocket::wsEvent);
  server.addHandler(&ws);
}
bool ProtoSocket::onLoopTask() const { return emitAllowed(xTaskGetCurrentTaskHandle(), this->_owner); }
bool ProtoSocket::hasClients() const {
  for(const proto_slot_t &s : slots)
    if(s.asyncId.load()) return true;
  return false;
}

/*********************************************************************
 * loop(): syncs
 ********************************************************************/
static void sendSyncFrame(proto_slot_t &s) {
  LiveSource src;
  char *buf = socketFrameBuffer();
  const size_t len = writeSyncFrame(src, s.cur, buf, PROTO_MAX_FRAME);
  if(len) ws.text(s.loopId, buf, len);
  if(s.cur.active()) {
    s.awaitingNext = true;
    s.sentAt = millis();
    return;
  }
  fmt.setBuffer(buf, PROTO_MAX_FRAME);
  beginFrame(fmt, proto_type_t::syncEnd);
  endFrame(fmt);
  ws.text(s.loopId, buf, fmt.length());
  s.awaitingNext = false;
}
void ProtoSocket::loop() {
  if(!this->onLoopTask()) return;
  for(proto_slot_t &s : slots) {
    const uint32_t id = s.asyncId.load();
    if(id != s.loopId) {
      // A client left or another took the slot.
      s.loopId = id;
      s.cur = sync_cursor_t();
      s.awaitingNext = false;
    }
    if(!id) continue;
    const uint32_t req = s.requests.exchange(0);
    if(req & (REQ_START | REQ_RESYNC)) {
      s.cur = beginSync((req & REQ_START) && s.withSchema.load());
      sendSyncFrame(s);
    }
    else if((req & REQ_NEXT) && s.awaitingNext) sendSyncFrame(s);
    else if(s.awaitingNext && millis() - s.sentAt > PROTO_SYNC_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Socket [%lu] sent no next for %us, closing it", (unsigned long)id, PROTO_SYNC_TIMEOUT_MS / 1000);
      s.awaitingNext = false;
      s.cur = sync_cursor_t();
      ws.close(id);
    }
  }
  ws.cleanupClients(PROTO_MAX_CLIENTS);
}

/*********************************************************************
 * loop(): events
 ********************************************************************/
JsonFormatter &ProtoSocket::beginEvent(proto_type_t type) {
  fmt.setBuffer(socketFrameBuffer(), PROTO_MAX_FRAME);
  beginFrame(fmt, type);
  return fmt;
}
void ProtoSocket::endEvent() {
  endFrame(fmt);
  if(fmt.overflowed()) {
    ESP_LOGE(TAG, "Event too large for %u bytes, not sent", PROTO_MAX_FRAME);
    return;
  }
  ws.textAll(socketFrameBuffer(), fmt.length());
}
// Each event checks the task and the clients first, so nothing is formatted
// for nobody.
#define PROTO_EVENT_GUARD() if(!this->onLoopTask() || !this->hasClients()) return

void ProtoSocket::shadeState(SomfyShade &shade) {
  PROTO_EVENT_GUARD();
  shade_snapshot_t s;
  snapshotShade(shade, s);
  ProtoWriter w(this->beginEvent(proto_type_t::shadeState));
  protoShadeState(w, s);
  this->endEvent();
}
void ProtoSocket::shade(SomfyShade &shade) {
  PROTO_EVENT_GUARD();
  shade_snapshot_t s;
  snapshotShade(shade, s);
  ProtoWriter w(this->beginEvent(proto_type_t::shade));
  protoShade(w, s);
  this->endEvent();
}
void ProtoSocket::shadeRemoved(uint8_t shadeId) {
  PROTO_EVENT_GUARD();
  ProtoWriter(this->beginEvent(proto_type_t::shadeRemoved)).add(pf::shadeId, shadeId);
  this->endEvent();
}
void ProtoSocket::group(SomfyGroup &group) {
  PROTO_EVENT_GUARD();
  group_snapshot_t g;
  snapshotGroup(group, g);
  ProtoWriter w(this->beginEvent(proto_type_t::group));
  protoGroup(w, g);
  this->endEvent();
}
void ProtoSocket::groupState(SomfyGroup &group) {
  PROTO_EVENT_GUARD();
  group_snapshot_t g;
  snapshotGroup(group, g);
  ProtoWriter w(this->beginEvent(proto_type_t::groupState));
  protoGroupState(w, g.groupId, g.flags, group.direction);
  this->endEvent();
}
void ProtoSocket::groupRemoved(uint8_t groupId) {
  PROTO_EVENT_GUARD();
  ProtoWriter(this->beginEvent(proto_type_t::groupRemoved)).add(pf::groupId, groupId);
  this->endEvent();
}
void ProtoSocket::room(SomfyRoom &room) {
  PROTO_EVENT_GUARD();
  room_snapshot_t r;
  snapshotRoom(room, r);
  ProtoWriter w(this->beginEvent(proto_type_t::room));
  protoRoom(w, r);
  this->endEvent();
}
void ProtoSocket::roomRemoved(uint8_t roomId) {
  PROTO_EVENT_GUARD();
  ProtoWriter(this->beginEvent(proto_type_t::roomRemoved)).add(pf::roomId, roomId);
  this->endEvent();
}
void ProtoSocket::order(proto_type_t type) {
  PROTO_EVENT_GUARD();
  struct entry_t { int8_t order; uint8_t id; };
  entry_t items[SOMFY_MAX_SHADES];
  uint8_t n = 0;
  if(type == proto_type_t::roomOrder) {
    for(SomfyRoom &r : somfy.rooms) if(r.roomId != 0) items[n++] = {r.sortOrder, r.roomId};
  }
  else if(type == proto_type_t::shadeOrder) {
    for(SomfyShade &s : somfy.shades) if(s.getShadeId() != 255) items[n++] = {s.sortOrder, s.getShadeId()};
  }
  else {
    for(SomfyGroup &g : somfy.groups) if(g.getGroupId() != 255) items[n++] = {g.sortOrder, g.getGroupId()};
  }
  for(uint8_t i = 1; i < n; i++)
    for(uint8_t j = i; j > 0 && items[j].order < items[j - 1].order; j--) {
      const entry_t t = items[j];
      items[j] = items[j - 1];
      items[j - 1] = t;
    }
  ProtoWriter w(this->beginEvent(type));
  w.beginList(pf::ids);
  for(uint8_t i = 0; i < n; i++) w.item(items[i].id);
  w.endList();
  this->endEvent();
}
void ProtoSocket::repeaters() {
  PROTO_EVENT_GUARD();
  ProtoWriter w(this->beginEvent(proto_type_t::repeaters));
  w.beginList(pf::repeaters);
  for(uint32_t address : somfy.repeaters) if(address != 0) w.item(address);
  w.endList();
  this->endEvent();
}
void ProtoSocket::shadeCommand(SomfyShade &shade, const char *command, const char *source, uint32_t sourceAddress) {
  PROTO_EVENT_GUARD();
  ProtoWriter w(this->beginEvent(proto_type_t::shadeCommand));
  w.add(pf::shadeId, shade.getShadeId());
  w.add(pf::remoteAddress, shade.getRemoteAddress());
  w.add(pf::command, command);
  w.add(pf::source, source);
  w.add(pf::lastRollingCode, shade.lastRollingCode);
  w.add(pf::sourceAddress, sourceAddress);
  this->endEvent();
}
void ProtoSocket::memory() {
  PROTO_EVENT_GUARD();
  ProtoWriter w(this->beginEvent(proto_type_t::memory));
  writeMemory(w);
  this->endEvent();
}
void ProtoSocket::wifi() {
  PROTO_EVENT_GUARD();
  ProtoWriter w(this->beginEvent(proto_type_t::wifi));
  writeWifi(w);
  this->endEvent();
}
void ProtoSocket::ethernet() {
  PROTO_EVENT_GUARD();
  ProtoWriter w(this->beginEvent(proto_type_t::ethernet));
  writeEthernet(w);
  this->endEvent();
}
bool ProtoSocket::subscribed(proto_feed_t feed) const {
  const uint8_t bit = 1 << static_cast<uint8_t>(feed);
  for(const proto_slot_t &s : slots)
    if(s.asyncId.load() && (s.feeds.load() & bit)) return true;
  return false;
}
ProtoWriter ProtoSocket::beginFeed(proto_type_t type) { return ProtoWriter(this->beginEvent(type)); }
void ProtoSocket::endFeed(proto_feed_t feed) {
  endFrame(fmt);
  if(fmt.overflowed() || !this->onLoopTask()) return;
  const uint8_t bit = 1 << static_cast<uint8_t>(feed);
  for(proto_slot_t &s : slots) {
    const uint32_t id = s.asyncId.load();
    if(id && (s.feeds.load() & bit)) ws.text(id, socketFrameBuffer(), fmt.length());
  }
}
