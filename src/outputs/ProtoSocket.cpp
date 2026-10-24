#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_log.h"
#include "ProtoSocket.h"
#include "ProtoSync.h"
#include "ProtoFrame.h"
#include "ProtoParse.h"
#include "ProtoSlots.h"
#include "ProtoOps.h"
#include "ProtoEdits.h"
#include "ProtoAssembly.h"
#include "SomfyEditor.h"
#include "LoopQueue.h"
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
extern SocketEmitter sockEmit;
extern LoopQueue loopQueue;

ProtoSocket protoSocket;
static AsyncWebSocket ws("/ws");
static_assert(MAX_SOCK_RESPONSE >= PROTO_MAX_FRAME, "protocol 2 frames share the socket event buffer");

static proto_slot_t slots[PROTO_MAX_CLIENTS];
// What loop() formats events and syncEnd with, into the socket event buffer.
static BufferedJsonFormatter eventJson;
// Messages that arrive in pieces.  async_tcp adds to it, and a disconnect can
// drop from it on another task (a client closed for a full queue is closed by
// whoever was sending), so it is only touched under its lock, and never while
// replying.
static MessageAssembler assembler(PROTO_MAX_CLIENT_FRAME);
static SemaphoreHandle_t assemblerMutex = nullptr;
struct AssemblerLock {
  AssemblerLock() { xSemaphoreTake(assemblerMutex, portMAX_DELAY); }
  ~AssemblerLock() { xSemaphoreGive(assemblerMutex); }
};

/*********************************************************************
 * The shade model as a sync source
 ********************************************************************/
static void writeHeader(ProtoWriter &w) {
  w.add(proto_field_t::maxRooms, (uint8_t)SOMFY_MAX_ROOMS);
  w.add(proto_field_t::maxShades, (uint8_t)SOMFY_MAX_SHADES);
  w.add(proto_field_t::maxGroups, (uint8_t)SOMFY_MAX_GROUPS);
  w.add(proto_field_t::maxGroupedShades, (uint8_t)SOMFY_MAX_GROUPED_SHADES);
  w.add(proto_field_t::maxLinkedRemotes, (uint8_t)SOMFY_MAX_LINKED_REMOTES);
  w.add(proto_field_t::startingAddress, somfy.startingAddress);
  // What a new shade or group gets.
  w.add(proto_field_t::bitLength, somfy.transceiver.config.type);
  w.add(proto_field_t::proto, static_cast<uint8_t>(somfy.transceiver.config.proto));
  w.add(proto_field_t::serverId, settings.serverId);
  w.add(proto_field_t::hostname, settings.hostname);
  w.add(proto_field_t::model, "ESPSomfyRTS");
  w.add(proto_field_t::chipModel, settings.chipModel);
  w.add(proto_field_t::fw, settings.fwVersion.name);
  w.add(proto_field_t::connType, net.connTypeName());
}
static void writeMemory(ProtoWriter &w) {
  w.add(proto_field_t::freeHeap, ESP.getFreeHeap());
  w.add(proto_field_t::maxHeap, ESP.getMaxAllocHeap());
  w.add(proto_field_t::minHeap, ESP.getMinFreeHeap());
  w.add(proto_field_t::totalHeap, ESP.getHeapSize());
  w.add(proto_field_t::uptime, (uint32_t)millis());
  w.add(proto_field_t::loopRate, loopStats.publishedRate());
  w.add(proto_field_t::loopMaxUs, loopStats.publishedMaxUs());
}
static void writeWifi(ProtoWriter &w) {
  if(WiFi.status() == WL_CONNECTED) {
    w.add(proto_field_t::ssid, WiFi.SSID().c_str());
    w.add(proto_field_t::strength, (int32_t)WiFi.RSSI());
    w.add(proto_field_t::channel, (int32_t)net.channel);
  }
  else {
    w.add(proto_field_t::strength, (int32_t)-100);
    w.add(proto_field_t::channel, (int32_t)-1);
  }
}
static void writeEthernet(ProtoWriter &w) {
#if HAS_ETHERNET
  if(net.connType == conn_types_t::ethernet) {
    w.add(proto_field_t::connected, net.connected());
    w.add(proto_field_t::speed, (uint32_t)ETH.linkSpeed());
    w.add(proto_field_t::fullDuplex, ETH.fullDuplex());
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
// Only a message with an id wants a reply.
static void replyOk(AsyncWebSocketClient *client, uint32_t id) {
  if(id == 0) return;
  sendFixed(client, [](JsonFormatter &json, void *ctx) { writeOkReply(json, *static_cast<uint32_t *>(ctx)); }, &id);
}
// A command op that becomes a shade request: loop() runs it and replies.
static void queueShadeOp(AsyncWebSocketClient *client, const proto_message_t &req, JsonObjectConst d) {
  loop_item_t item;
  const char *bad = parseShadeOp(req.type, d, item.request);
  if(bad) { replyError(client, req.id, proto_error_t::badRequest, bad); return; }
  // Nothing goes on air during a frequency scan, so say so now rather than
  // once it reaches the radio.
  if(loopItemUsesRadio(item) && somfy.transceiver.txSuspended()) {
    replyError(client, req.id, proto_error_t::radioUnavailable, "The radio is paused");
    return;
  }
  item.replyTo = {client->id(), req.id};
  if(!loopQueue.submit(item)) replyError(client, req.id, proto_error_t::busy, "Command queue full");
}
// A frame as a remote that isn't one of the shades.  It touches nothing of the
// shade model and the radio takes jobs from any task, so it goes from here.
static void sendRemoteCommand(AsyncWebSocketClient *client, const proto_message_t &req, JsonObjectConst d) {
  somfy_frame_t frame;
  uint8_t repeats;
  const char *bad = parseRemoteCommandOp(d, frame, repeats);
  if(bad) { replyError(client, req.id, proto_error_t::badRequest, bad); return; }
  if(!somfy.sendFrame(frame, repeats)) {
    replyError(client, req.id, proto_error_t::radioUnavailable, PROTO_RADIO_REFUSED);
    return;
  }
  replyOk(client, req.id);
}
// An edit op: loop() applies it as its edit route does, and replies.
static void queueEditOp(AsyncWebSocketClient *client, const proto_message_t &msg, model_edit_type_t type, JsonObjectConst d) {
  bool busy;
  model_edit_t *edit = newModelEdit(type, busy);
  if(!edit) {
    if(busy) replyError(client, msg.id, proto_error_t::busy, "Another edit is running, send it again");
    else replyError(client, msg.id, proto_error_t::internal, "Out of memory");
    return;
  }
  const char *bad = expandEditPayload(type, d, edit->doc);
  if(bad) {
    cancelModelEdit(edit);
    replyError(client, msg.id, proto_error_t::badRequest, bad);
    return;
  }
  edit->replyTo = {client->id(), msg.id};
  if(!submitModelEdit(edit)) replyError(client, msg.id, proto_error_t::busy, "Command queue full");
}
// A read op: loop() answers it from the shade model.
static void queueReadOp(AsyncWebSocketClient *client, const proto_message_t &msg, JsonObjectConst d) {
  loop_item_t item;
  item.type = loop_item_type_t::proto_read;
  item.readOp = msg.type;
  if(msg.type == static_cast<uint8_t>(proto_type_t::groupOptions)) {
    JsonVariantConst groupId = d[protoField(proto_field_t::groupId).key];
    if(groupId.isNull()) { replyError(client, msg.id, proto_error_t::badRequest, "groupId is missing"); return; }
    if(!groupId.is<uint8_t>()) { replyError(client, msg.id, proto_error_t::badRequest, "groupId must be 0-255"); return; }
    item.groupId = groupId.as<uint8_t>();
  }
  item.replyTo = {client->id(), msg.id};
  if(!loopQueue.submit(item)) replyError(client, msg.id, proto_error_t::busy, "Command queue full");
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
  const AsyncWebParameter *schema = request ? request->getParam("schema") : nullptr;
  const bool withSchema = !(schema && schema->value() == protoSchemaHash());
  proto_slot_t *slot = claimSlot(slots, PROTO_MAX_CLIENTS, id, withSchema);
  if(!slot) {
    ESP_LOGW(TAG, "Socket [%lu] refused: all %u clients connected", (unsigned long)id, PROTO_MAX_CLIENTS);
    client->close();
    return;
  }
  client->setCloseClientOnQueueFull(true);
  sendFixed(client, [](JsonFormatter &json, void *) { writeHello(json, settings.fwVersion.name); }, nullptr);
  startSync(*slot);
  IPAddress ip = client->remoteIP();
  ESP_LOGI(TAG, "Socket [%lu] connected from %u.%u.%u.%u%s", (unsigned long)id, ip[0], ip[1], ip[2], ip[3], withSchema ? "" : " (has the schema)");
}
static void onDisconnect(AsyncWebSocketClient *client) {
  {
    AssemblerLock lock;
    assembler.drop(client->id());
  }
  proto_slot_t *slot = slotOf(slots, PROTO_MAX_CLIENTS, client->id());
  if(!slot) return;
  freeSlot(*slot);
  ESP_LOGI(TAG, "Socket [%lu] disconnected", (unsigned long)client->id());
}
// One whole message from a client.
static void onMessage(AsyncWebSocketClient *client, const char *text, size_t len) {
  proto_slot_t *slot = slotOf(slots, PROTO_MAX_CLIENTS, client->id());
  if(!slot) return;
  JsonDocument doc;
  proto_message_t msg;
  const proto_error_t err = parseClientFrame(text, len, doc, msg);
  if(err != proto_error_t::none) {
    replyError(client, msg.id, err, err == proto_error_t::unknownOp ? "Unknown message type" : "Malformed message");
    return;
  }
  switch(static_cast<proto_type_t>(msg.type)) {
    case proto_type_t::next:
      slot->wants.fetch_or(WANTS_NEXT);
      replyOk(client, msg.id);
      return;
    case proto_type_t::sync:
      slot->wants.fetch_or(WANTS_SYNC_AGAIN);
      replyOk(client, msg.id);
      return;
    case proto_type_t::subscribe:
    case proto_type_t::unsubscribe: {
      const int32_t feed = protoInt(doc["d"], proto_field_t::feed);
      bool known = false;
      for(uint8_t i = 0; i < protoFeedCount(); i++) known |= protoFeedAt(i).number == feed;
      if(!known) { replyError(client, msg.id, proto_error_t::badRequest, "Unknown feed"); return; }
      if(msg.type == static_cast<uint8_t>(proto_type_t::subscribe)) slot->feeds.fetch_or(1 << feed);
      else slot->feeds.fetch_and(~(1 << feed));
      replyOk(client, msg.id);
      return;
    }
    default:
      break;
  }
  // The ops, by what becomes of them (the TYPE table).
  switch(protoOpKind(msg.type)) {
    case proto_op_t::shadeRequest:
      queueShadeOp(client, msg, doc["d"]);
      return;
    case proto_op_t::remoteFrame:
      sendRemoteCommand(client, msg, doc["d"]);
      return;
    case proto_op_t::edit: {
      model_edit_type_t edit;
      if(protoEditType(msg.type, edit)) queueEditOp(client, msg, edit, doc["d"]);
      else replyError(client, msg.id, proto_error_t::internal, "An edit op without an edit");
      return;
    }
    case proto_op_t::read:
      queueReadOp(client, msg, doc["d"]);
      return;
    case proto_op_t::none:
      // The settings ops arrive with the protocol core's next PR.
      replyError(client, msg.id, proto_error_t::unknownOp, "Unknown message type");
      return;
  }
}
static void onFrame(AsyncWebSocketClient *client, AwsFrameInfo *info, uint8_t *data, size_t len) {
  // Most messages are a whole text frame in one piece.
  if(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    onMessage(client, reinterpret_cast<const char *>(data), len);
    return;
  }
  // The rest come in pieces: a frame larger than a TCP segment, or a message
  // sent as several frames.  Only a message's first frame has a text or
  // binary opcode; the frames after it are continuations.
  proto_piece_t piece;
  piece.clientId = client->id();
  piece.first = info->index == 0 && info->opcode != WS_CONTINUATION;
  piece.last = info->final && info->index + len == info->len;
  piece.text = info->message_opcode == WS_TEXT;
  piece.data = data;
  piece.len = len;
  // Each refusal comes once per message, so the client gets one reply.  The
  // message couldn't be read, so the reply has no id.
  assembly_t result;
  char *message = nullptr;
  size_t messageLen = 0;
  {
    AssemblerLock lock;
    result = assembler.add(piece, millis());
    if(result == assembly_t::complete) message = assembler.take(messageLen);
  }
  switch(result) {
    case assembly_t::complete:
      onMessage(client, message, messageLen);
      free(message);
      return;
    case assembly_t::busy:
      replyError(client, 0, proto_error_t::busy, "Another long message is on its way, send it again");
      return;
    case assembly_t::tooLarge:
      replyError(client, 0, proto_error_t::badRequest, "Message too large");
      return;
    case assembly_t::notText:
      replyError(client, 0, proto_error_t::badRequest, "Send text messages");
      return;
    case assembly_t::noMemory:
      replyError(client, 0, proto_error_t::internal, "Out of memory");
      return;
    case assembly_t::more:
    case assembly_t::ignored:
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
  assemblerMutex = xSemaphoreCreateMutex();
  ws.handleHandshake(authorized);
  ws.onEvent(ProtoSocket::wsEvent);
  server.addHandler(&ws);
}
bool ProtoSocket::hasClients() const {
  for(const proto_slot_t &s : slots)
    if(s.clientId.load()) return true;
  return false;
}
bool ProtoSocket::sending() const { return sockEmit.onLoopTask() && this->hasClients(); }

/*********************************************************************
 * loop(): syncs
 ********************************************************************/
static void sendSyncFrame(proto_slot_t &s) {
  LiveSource src;
  char *buf = socketFrameBuffer();
  const size_t len = writeSyncFrame(src, s.cur, buf, PROTO_MAX_FRAME);
  if(len) ws.text(s.loopId, buf, len);
  syncFrameSent(s, millis());
  if(s.cur.active()) return;
  eventJson.setBuffer(buf, PROTO_MAX_FRAME);
  beginFrame(eventJson, proto_type_t::syncEnd);
  endFrame(eventJson);
  ws.text(s.loopId, buf, eventJson.length());
}
void ProtoSocket::loop() {
  if(!sockEmit.onLoopTask()) return;
  for(proto_slot_t &s : slots) {
    switch(nextSyncStep(s, millis())) {
      case sync_step_t::send:
        sendSyncFrame(s);
        break;
      case sync_step_t::timedOut:
        ESP_LOGW(TAG, "Socket [%lu] sent no next for %us, closing it", (unsigned long)s.loopId, PROTO_SYNC_TIMEOUT_MS / 1000);
        ws.close(s.loopId);
        break;
      case sync_step_t::none:
        break;
    }
  }
  // A long message that stalled gives its buffer up, and its client hears so.
  uint32_t stalled;
  {
    AssemblerLock lock;
    stalled = assembler.expire(millis());
  }
  if(stalled) {
    char buf[160];
    BufferedJsonFormatter json;
    json.setBuffer(buf, sizeof(buf));
    writeErrorReply(json, 0, proto_error_t::badRequest, "The message took too long to arrive");
    ws.text(stalled, buf, json.length());
  }
  ws.cleanupClients(PROTO_MAX_CLIENTS);
}

// Sends a reply formatted on loop().  One that didn't fit goes as an internal
// error instead, so the op still gets its one reply.  A client that has gone
// is skipped.
static void sendReply(const proto_reply_to_t &to, BufferedJsonFormatter &json, char *buf, size_t size) {
  if(json.overflowed()) {
    json.setBuffer(buf, size);
    writeErrorReply(json, to.id, proto_error_t::internal, "Reply too large");
  }
  ws.text(to.client, buf, json.length());
}
void ProtoSocket::replyRequest(const proto_reply_to_t &to, const shade_request_t &req, shade_request_result_t result) {
  if(!sockEmit.onLoopTask() || to.id == 0) return;
  char buf[192];
  BufferedJsonFormatter json;
  json.setBuffer(buf, sizeof(buf));
  const proto_error_t code = protoRequestError(result);
  if(code == proto_error_t::none) writeOkReply(json, to.id);
  else writeErrorReply(json, to.id, code, protoRequestErrorDesc(req, result));
  sendReply(to, json, buf, sizeof(buf));
}

// What an edit op's reply says: for an add, the new item's id; for a refused
// edit, the route's reason and the shade's validation code when it has one.
void ProtoSocket::replyEdit(const proto_reply_to_t &to, model_edit_type_t type, const edit_result_t &result) {
  if(!sockEmit.onLoopTask() || to.id == 0) return;
  const uint32_t id = to.id;
  char buf[256];
  BufferedJsonFormatter json;
  json.setBuffer(buf, sizeof(buf));
  beginFrame(json, proto_type_t::reply, id);
  ProtoWriter w(json);
  w.add(proto_field_t::ok, result.ok);
  if(!result.ok) {
    w.add(proto_field_t::code, static_cast<uint8_t>(result.notFound ? proto_error_t::notFound : proto_error_t::badRequest));
    w.add(proto_field_t::desc, result.desc);
    w.add(proto_field_t::validation, result.code);
  }
  else if(type == model_edit_type_t::add_room) w.add(proto_field_t::roomId, result.id);
  else if(type == model_edit_type_t::add_shade) w.add(proto_field_t::shadeId, result.id);
  else if(type == model_edit_type_t::add_group) w.add(proto_field_t::groupId, result.id);
  endFrame(json);
  sendReply(to, json, buf, sizeof(buf));
}
// The reads an edit form needs: what a new room, shade or group would get,
// and the shades a group could take.
void ProtoSocket::replyRead(const proto_reply_to_t &to, uint8_t op, uint8_t groupId) {
  if(!sockEmit.onLoopTask() || to.id == 0) return;
  const uint32_t id = to.id;
  char buf[256];
  BufferedJsonFormatter json;
  json.setBuffer(buf, sizeof(buf));
  const char *refused = nullptr;
  proto_error_t code = proto_error_t::badRequest;
  beginFrame(json, proto_type_t::reply, id);
  ProtoWriter w(json);
  switch(static_cast<proto_type_t>(op)) {
    case proto_type_t::getNextRoom: {
      const uint8_t roomId = somfy.getNextRoomId();
      if(roomId == 0) { refused = EDIT_ROOMS_FULL; break; }
      w.add(proto_field_t::ok, true);
      w.add(proto_field_t::roomId, roomId);
      break;
    }
    case proto_type_t::getNextShade:
    case proto_type_t::getNextGroup: {
      const bool isShade = op == static_cast<uint8_t>(proto_type_t::getNextShade);
      const uint8_t newId = isShade ? somfy.getNextShadeId() : somfy.getNextGroupId();
      if(newId == 255) { refused = isShade ? EDIT_SHADES_FULL : EDIT_GROUPS_FULL; break; }
      w.add(proto_field_t::ok, true);
      w.add(isShade ? proto_field_t::shadeId : proto_field_t::groupId, newId);
      w.add(proto_field_t::remoteAddress, somfy.getNextRemoteAddress(newId));
      w.add(proto_field_t::bitLength, somfy.transceiver.config.type);
      w.add(proto_field_t::proto, static_cast<uint8_t>(somfy.transceiver.config.proto));
      break;
    }
    case proto_type_t::groupOptions: {
      SomfyGroup *group = somfy.getGroupById(groupId);
      if(!group) { refused = "No group with that id"; code = proto_error_t::notFound; break; }
      w.add(proto_field_t::ok, true);
      w.add(proto_field_t::groupId, groupId);
      // The shades it could take: every one it doesn't have.
      w.beginList(proto_field_t::ids);
      for(SomfyShade &shade : somfy.shades)
        if(shade.getShadeId() != 255 && !group->hasShadeId(shade.getShadeId())) w.item(shade.getShadeId());
      w.endList();
      break;
    }
    default:
      refused = "Not a read op";
      break;
  }
  if(refused) {
    json.setBuffer(buf, sizeof(buf));
    writeErrorReply(json, id, code, refused);
  }
  else endFrame(json);
  sendReply(to, json, buf, sizeof(buf));
}

/*********************************************************************
 * loop(): events
 ********************************************************************/
static JsonFormatter &beginEvent(proto_type_t type) {
  eventJson.setBuffer(socketFrameBuffer(), PROTO_MAX_FRAME);
  beginFrame(eventJson, type);
  return eventJson;
}
// Closes the event; false when it didn't fit, which is logged.
static bool finishEvent() {
  endFrame(eventJson);
  if(!eventJson.overflowed()) return true;
  ESP_LOGE(TAG, "Event too large for %u bytes, not sent", PROTO_MAX_FRAME);
  return false;
}
static void sendEvent() {
  if(finishEvent()) ws.textAll(socketFrameBuffer(), eventJson.length());
}
// An item whole, or only its state: a snapshot of it, written by write.
template<typename Model, typename Snapshot>
static void sendItem(proto_type_t type, Model &item, void (*snapshot)(Model &, Snapshot &), void (*write)(ProtoWriter &, const Snapshot &)) {
  Snapshot s;
  snapshot(item, s);
  ProtoWriter w(beginEvent(type));
  write(w, s);
  sendEvent();
}

void ProtoSocket::shadeState(SomfyShade &shade) {
  if(this->sending()) sendItem(proto_type_t::shadeState, shade, snapshotShade, protoShadeState);
}
void ProtoSocket::shade(SomfyShade &shade) {
  if(this->sending()) sendItem(proto_type_t::shade, shade, snapshotShade, protoShade);
}
void ProtoSocket::shadeRemoved(uint8_t shadeId) {
  if(!this->sending()) return;
  ProtoWriter(beginEvent(proto_type_t::shadeRemoved)).add(proto_field_t::shadeId, shadeId);
  sendEvent();
}
void ProtoSocket::group(SomfyGroup &group) {
  if(this->sending()) sendItem(proto_type_t::group, group, snapshotGroup, protoGroup);
}
void ProtoSocket::groupState(SomfyGroup &group) {
  if(!this->sending()) return;
  group_snapshot_t g;
  snapshotGroup(group, g);
  ProtoWriter w(beginEvent(proto_type_t::groupState));
  protoGroupState(w, g.groupId, g.flags, group.direction);
  sendEvent();
}
void ProtoSocket::groupRemoved(uint8_t groupId) {
  if(!this->sending()) return;
  ProtoWriter(beginEvent(proto_type_t::groupRemoved)).add(proto_field_t::groupId, groupId);
  sendEvent();
}
void ProtoSocket::room(SomfyRoom &room) {
  if(this->sending()) sendItem(proto_type_t::room, room, snapshotRoom, protoRoom);
}
void ProtoSocket::roomRemoved(uint8_t roomId) {
  if(!this->sending()) return;
  ProtoWriter(beginEvent(proto_type_t::roomRemoved)).add(proto_field_t::roomId, roomId);
  sendEvent();
}
void ProtoSocket::order(sort_list_t list) {
  if(!this->sending()) return;
  struct entry_t { int8_t order; uint8_t id; };
  static constexpr uint8_t MOST = std::max({SOMFY_MAX_ROOMS, SOMFY_MAX_SHADES, SOMFY_MAX_GROUPS});
  entry_t items[MOST];
  uint8_t n = 0;
  proto_type_t type;
  switch(list) {
    case sort_list_t::rooms:
      type = proto_type_t::roomOrder;
      for(SomfyRoom &r : somfy.rooms) if(r.roomId != 0) items[n++] = {r.sortOrder, r.roomId};
      break;
    case sort_list_t::shades:
      type = proto_type_t::shadeOrder;
      for(SomfyShade &s : somfy.shades) if(s.getShadeId() != 255) items[n++] = {s.sortOrder, s.getShadeId()};
      break;
    case sort_list_t::groups:
      type = proto_type_t::groupOrder;
      for(SomfyGroup &g : somfy.groups) if(g.getGroupId() != 255) items[n++] = {g.sortOrder, g.getGroupId()};
      break;
    default:
      return;
  }
  for(uint8_t i = 1; i < n; i++)
    for(uint8_t j = i; j > 0 && items[j].order < items[j - 1].order; j--) std::swap(items[j], items[j - 1]);
  ProtoWriter w(beginEvent(type));
  w.beginList(proto_field_t::ids);
  for(uint8_t i = 0; i < n; i++) w.item(items[i].id);
  w.endList();
  sendEvent();
}
void ProtoSocket::repeaters() {
  if(!this->sending()) return;
  ProtoWriter w(beginEvent(proto_type_t::repeaters));
  w.beginList(proto_field_t::repeaters);
  for(uint32_t address : somfy.repeaters) if(address != 0) w.item(address);
  w.endList();
  sendEvent();
}
void ProtoSocket::shadeCommanded(SomfyShade &shade, const char *command, const char *source, uint32_t sourceAddress) {
  if(!this->sending()) return;
  ProtoWriter w(beginEvent(proto_type_t::shadeCommanded));
  w.add(proto_field_t::shadeId, shade.getShadeId());
  w.add(proto_field_t::remoteAddress, shade.getRemoteAddress());
  w.add(proto_field_t::command, command);
  w.add(proto_field_t::source, source);
  w.add(proto_field_t::lastRollingCode, shade.lastRollingCode);
  w.add(proto_field_t::sourceAddress, sourceAddress);
  sendEvent();
}
void ProtoSocket::memory() {
  if(!this->sending()) return;
  ProtoWriter w(beginEvent(proto_type_t::memory));
  writeMemory(w);
  sendEvent();
}
void ProtoSocket::wifi() {
  if(!this->sending()) return;
  ProtoWriter w(beginEvent(proto_type_t::wifi));
  writeWifi(w);
  sendEvent();
}
void ProtoSocket::ethernet() {
  if(!this->sending()) return;
  ProtoWriter w(beginEvent(proto_type_t::ethernet));
  writeEthernet(w);
  sendEvent();
}
bool ProtoSocket::subscribed(proto_feed_t feed) const {
  // The feed is formatted into the event buffer, which is loop()'s.
  if(!sockEmit.onLoopTask()) return false;
  const uint8_t bit = 1 << static_cast<uint8_t>(feed);
  for(const proto_slot_t &s : slots)
    if(s.clientId.load() && (s.feeds.load() & bit)) return true;
  return false;
}
ProtoWriter ProtoSocket::beginFeed(proto_type_t type) { return ProtoWriter(beginEvent(type)); }
void ProtoSocket::endFeed(proto_feed_t feed) {
  if(!finishEvent()) return;
  const uint8_t bit = 1 << static_cast<uint8_t>(feed);
  for(proto_slot_t &s : slots) {
    const uint32_t id = s.clientId.load();
    if(id && (s.feeds.load() & bit)) ws.text(id, socketFrameBuffer(), eventJson.length());
  }
}
