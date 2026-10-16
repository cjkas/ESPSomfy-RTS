#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "mbedtls/md.h"
#include "ConfigSettings.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "SSDP.h"
#include "Somfy.h"
#include "WResp.h"
#include "LoopQueue.h"
#include "ModelSnapshot.h"
#include "SomfySnapshot.h"
#include "SomfyEditor.h"
#include "Web.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "ESPNetwork.h"
#include "Timing.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <memory>

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MQTTClass mqtt;
extern GitUpdater git;
extern ESPNetwork net;
extern LoopQueue loopQueue;
extern SnapshotSlots snapshotSlots;

//#define WEB_MAX_RESPONSE 34768
#define WEB_MAX_RESPONSE 4096
static char g_async_content[WEB_MAX_RESPONSE];


// General responses
static const char _response_404[] = "404: Service Not Found";


// Encodings
static const char _encoding_text[] = "text/plain";
static const char _encoding_html[] = "text/html";
static const char _encoding_json[] = "application/json";

static const char *TAG = "Web";

static void sendQueueFull(AsyncWebServerRequest *request);
static void sendRadioUnavailable(AsyncWebServerRequest *request);
// Hands an item to loop(), which owns the shades.  When it can't be the
// request is answered with the reason and false is returned.
static bool queueLoopItem(AsyncWebServerRequest *request, const loop_item_t &item) {
  // Nothing can go on air during a frequency scan so tell the client now rather
  // than dropping the request once it reaches the radio.
  if(loopItemUsesRadio(item) && somfy.transceiver.txSuspended()) { sendRadioUnavailable(request); return false; }
  if(!loopQueue.submit(item)) { sendQueueFull(request); return false; }
  return true;
}
static bool queueShadeRequest(AsyncWebServerRequest *request, const shade_request_t &req) {
  loop_item_t item;
  item.request = req;
  return queueLoopItem(request, item);
}
static bool queueSetMyPosition(AsyncWebServerRequest *request, SomfyShade *shade, int8_t pos, int8_t tilt) {
  shade_request_t req;
  req.type = shade_request_type_t::set_favorite;
  req.id = shade->getShadeId();
  req.position = pos;
  req.tiltPosition = tilt;
  return queueShadeRequest(request, req);
}
// loop() clears paired and saves it once the Prog is queued.  The shade
// belongs to loop() so it isn't changed here.
static bool queueUnpairShade(AsyncWebServerRequest *request, SomfyShade *shade) {
  shade_request_t req;
  req.type = shade_request_type_t::unpair;
  req.id = shade->getShadeId();
  return queueShadeRequest(request, req);
}
// Sending a command reads and commits the rolling code on loop(), so a new
// code is set there too.
static bool queueSetRollingCode(AsyncWebServerRequest *request, SomfyShade *shade, uint16_t rollingCode) {
  shade_request_t req;
  req.type = shade_request_type_t::set_rolling_code;
  req.id = shade->getShadeId();
  req.rollingCode = rollingCode;
  return queueShadeRequest(request, req);
}
// A restore's options travel through the loop queue as RESTORE_* bits.
static uint8_t restoreParts(const restore_options_t &opts) {
  return (opts.settings ? RESTORE_SETTINGS : 0) | (opts.shades ? RESTORE_SHADES : 0) |
    (opts.network ? RESTORE_NETWORK : 0) | (opts.transceiver ? RESTORE_TRANSCEIVER : 0) |
    (opts.repeaters ? RESTORE_REPEATERS : 0) | (opts.mqtt ? RESTORE_MQTT : 0);
}
// The radio and MQTT are shut down once an upload starts and only a reboot
// brings them back.  The completion handler schedules it but never runs if the
// client goes away mid-upload, so make sure the device reboots then too.
static void onUploadDisconnect() {
  if(webServer.uploadSuccess || rebootDelay.reboot) return;
  ESP_LOGW(TAG, "Upload abandoned, rebooting");
  if(Update.isRunning()) Update.abort();
  rebootDelay.reboot = true;
  rebootDelay.rebootTime = millis() + 500;
}
// Sent when a command can't be handed to the main loop.
static void sendQueueFull(AsyncWebServerRequest *request) {
  request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Command queue full\"}"));
}
// Sent when the radio can't take a frame (suspended or queue full).
static void sendRadioUnavailable(AsyncWebServerRequest *request) {
  request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Radio TX unavailable\"}"));
}

AsyncWebServer asyncServer(80);
AsyncWebServer asyncApiServer(8081);
void Web::startup() {
  ESP_LOGI(TAG, "Launching web server...");

  asyncServer.on("/loginContext", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    root["type"] = static_cast<uint8_t>(settings.Security.type);
    root["permissions"] = settings.Security.permissions;
    root["serverId"] = settings.serverId;
    root["version"] = settings.fwVersion.name;
    root["model"] = "ESPSomfyRTS";
    root["hostname"] = settings.hostname;    
    response->setLength();
    request->send(response);
  });
  asyncApiServer.begin();
  ESP_LOGI(TAG, "Async API server started on port 8081");
}
void Web::loop() {
  delay(1);
}
bool Web::isAuthenticated(AsyncWebServerRequest *request, bool cfg) {
  ESP_LOGD(TAG, "Checking async authentication");
  if(settings.Security.type == security_types::None) return true;
  else if(!cfg && (settings.Security.permissions & static_cast<uint8_t>(security_permissions::ConfigOnly)) == 0x01) return true;
  else if(request->hasHeader("apikey")) {
    ESP_LOGD(TAG, "Checking API Key...");
    char token[65];
    memset(token, 0x00, sizeof(token));
    this->createAPIToken(request->client()->remoteIP(), token);
    if(String(token) != request->getHeader("apikey")->value()) {
      request->send(401, _encoding_text, "Unauthorized API Key");
      return false;
    }
    // Key is valid
  }
  else {
    ESP_LOGE(TAG, "Not authenticated...");
    request->send(401, _encoding_text, "Unauthorized API Key");
    return false;
  }
  return true;
}
bool Web::createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token) {
  return this->createAPIToken((String(pin) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token) {
  return this->createAPIToken((String(username) + ":" + String(password) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIToken(const char *payload, char *token) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)settings.serverId, strlen(settings.serverId));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, strlen(payload)); 
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    token[0] = '\0';
    for(int i = 0; i < sizeof(hmacResult); i++){
        char str[3];
        sprintf(str, "%02x", (int)hmacResult[i]);
        strcat(token, str);
    }
    ESP_LOGD(TAG, "Hash: %s", token);
    return true;
}
bool Web::createAPIToken(const IPAddress ipAddress, char *token) {
    String payload;
    if(settings.Security.type == security_types::Password) createAPIPasswordToken(ipAddress, settings.Security.username, settings.Security.password, token);
    else if(settings.Security.type == security_types::PinEntry) createAPIPinToken(ipAddress, settings.Security.pin, token);
    else createAPIToken(ipAddress.toString().c_str(), token);
    return true;
}
// =====================================================
// Async API Handlers
// =====================================================
// Helper: get a query param as String, or empty if missing
static String asyncParam(AsyncWebServerRequest *request, const char *name) {
  if(request->hasParam(name)) return request->getParam(name)->value();
  return String();
}
static bool asyncHasParam(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name);
}

// -- Serialization helpers --
// Replies that still read the live shade model on this task (command and edit
// replies) copy one item into the ModelSnapshot types and write it the way the
// read routes do.  The read routes themselves write from a ModelSnapshot that
// loop() fills (sendSnapshot).
static void serializeShadeRef(SomfyShade *shade, JsonFormatter &json) {
  shade_snapshot_t s;
  snapshotShade(*shade, s);
  writeShadeRef(json, s);
}
// paired and lastRollingCode are passed in for a response that reports the
// change the main loop is about to make.  An unpair is held until it goes on
// air (holdLoopItem), so the answer comes true.
static void serializeShade(SomfyShade *shade, JsonFormatter &json, bool paired, uint16_t lastRollingCode) {
  shade_snapshot_t s;
  snapshotShade(*shade, s);
  s.paired = paired;
  s.lastRollingCode = lastRollingCode;
  writeShade(json, s);
}
static void serializeShade(SomfyShade *shade, JsonFormatter &json) { serializeShade(shade, json, shade->paired, shade->lastRollingCode); }
static void serializeGroupRef(SomfyGroup *group, JsonFormatter &json) {
  group_snapshot_t s;
  snapshotGroup(*group, s);
  writeGroupRef(json, s);
}
static void serializeGroup(SomfyGroup *group, JsonFormatter &json) {
  group_snapshot_t s;
  snapshotGroup(*group, s);
  shade_snapshot_t linked;
  writeGroup(json, s, liveShade, &linked);
}
static void serializeTransceiverConfig(JsonFormatter &json) {
  auto &cfg = somfy.transceiver.config;
  json.addElem("type", cfg.type);
  json.addElem("TXPin", cfg.TXPin);
  json.addElem("RXPin", cfg.RXPin);
  json.addElem("SCKPin", cfg.SCKPin);
  json.addElem("MOSIPin", cfg.MOSIPin);
  json.addElem("MISOPin", cfg.MISOPin);
  json.addElem("CSNPin", cfg.CSNPin);
  json.addElem("rxBandwidth", cfg.rxBandwidth);
  json.addElem("frequency", cfg.frequency);
  json.addElem("deviation", cfg.deviation);
  json.addElem("txPower", cfg.txPower);
  json.addElem("proto", static_cast<uint8_t>(cfg.proto));
  json.addElem("enabled", cfg.enabled);
  json.addElem("noiseDetection", cfg.noiseDetection);
  json.addElem("radioInit", cfg.radioInit);
}
static void serializeAppVersion(JsonFormatter &json, appver_t &ver) {
  json.addElem("name", ver.name);
  json.addElem("major", ver.major);
  json.addElem("minor", ver.minor);
  json.addElem("build", ver.build);
  json.addElem("suffix", ver.suffix);
}
static void serializeGitVersion(JsonFormatter &json) {
  json.addElem("available", git.updateAvailable);
  json.addElem("status", git.status);
  json.addElem("error", (int32_t)git.error);
  json.addElem("cancelled", git.cancelled);
  json.addElem("checkForUpdate", settings.checkForUpdate);
  json.addElem("inetAvailable", git.inetAvailable);
  json.beginObject("fwVersion");
  serializeAppVersion(json, settings.fwVersion);
  json.endObject();
  json.beginObject("appVersion");
  serializeAppVersion(json, settings.appVersion);
  json.endObject();
  json.beginObject("latest");
  serializeAppVersion(json, git.latest);
  json.endObject();
}
static void serializeGitRelease(GitRelease *rel, JsonFormatter &json) {
  Timestamp ts;
  char buff[20];
  sprintf(buff, "%llu", rel->id);
  json.addElem("id", buff);
  json.addElem("name", rel->name);
  json.addElem("date", ts.getISOTime(rel->releaseDate));
  json.addElem("draft", rel->draft);
  json.addElem("preRelease", rel->preRelease);
  json.addElem("main", rel->main);
  json.addElem("hasFS", rel->hasFS);
  json.addElem("hwVersions", rel->hwVersions);
  json.beginObject("version");
  serializeAppVersion(json, rel->version);
  json.endObject();
}

// The parts of /controller that aren't in the shade model.
static void controllerHeader(JsonFormatter &json, const model_snapshot_t &snap) {
  json.addElem("maxRooms", (uint8_t)SOMFY_MAX_ROOMS);
  json.addElem("maxShades", (uint8_t)SOMFY_MAX_SHADES);
  json.addElem("maxGroups", (uint8_t)SOMFY_MAX_GROUPS);
  json.addElem("maxGroupedShades", (uint8_t)SOMFY_MAX_GROUPED_SHADES);
  json.addElem("maxLinkedRemotes", (uint8_t)SOMFY_MAX_LINKED_REMOTES);
  json.addElem("startingAddress", snap.startingAddress);
  json.beginObject("transceiver");
  json.beginObject("config");
  serializeTransceiverConfig(json);
  json.endObject();
  json.endObject();
  json.beginObject("version");
  serializeGitVersion(json);
  json.endObject();
}
// The parts of /discovery that aren't in the shade model.
static void discoveryHeader(JsonFormatter &json, const model_snapshot_t &snap) {
  (void)snap;
  const char *connType = "Unknown";
  if(net.connType == conn_types_t::ethernet) connType = "Ethernet";
  else if(net.connType == conn_types_t::wifi) connType = "Wifi";
  json.addElem("serverId", settings.serverId);
  json.addElem("version", settings.fwVersion.name);
  json.addElem("latest", git.latest.name);
  json.addElem("model", "ESPSomfyRTS");
  json.addElem("hostname", settings.hostname);
  json.addElem("authType", static_cast<uint8_t>(settings.Security.type));
  json.addElem("permissions", settings.Security.permissions);
  json.addElem("chipModel", settings.chipModel);
  json.addElem("connType", connType);
  json.addElem("checkForUpdate", settings.checkForUpdate);
  json.beginObject("memory");
  json.addElem("max", ESP.getMaxAllocHeap());
  json.addElem("free", ESP.getFreeHeap());
  json.addElem("min", ESP.getMinFreeHeap());
  json.addElem("total", ESP.getHeapSize());
  json.addElem("uptime", (uint64_t)millis());
  json.endObject();
}

// A read served from a ModelSnapshot that loop() fills, so the document
// describes one moment (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).
// Each item is formatted into a fixed 3 KB unit and streamed as the TCP send
// window allows, so there is no growing response buffer either.
struct SnapshotRead {
  uint8_t slot;
  char unit[3072];
  SnapshotWriter writer;
  SnapshotRead(uint8_t slot, snapshot_doc_t doc, uint8_t id, snapshot_header_t header)
    : slot(slot), writer(snapshotSlots.snapshot(slot), doc, id, header, unit, sizeof(unit)) {}
  // The response is gone, whether it finished or the client went away.
  ~SnapshotRead() { snapshotSlots.release(this->slot); }
};
static void sendSnapshot(AsyncWebServerRequest *request, snapshot_doc_t doc, uint8_t id = 0, snapshot_header_t header = nullptr) {
  const int8_t slot = snapshotSlots.acquire();
  if(slot < 0) {
    request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Busy, try the read again\"}"));
    return;
  }
  loop_item_t item;
  item.type = loop_item_type_t::snapshot_fill;
  item.slot = static_cast<uint8_t>(slot);
  if(!loopQueue.submit(item)) {
    snapshotSlots.cancel(slot);
    sendQueueFull(request);
    return;
  }
  auto read = std::make_shared<SnapshotRead>(static_cast<uint8_t>(slot), doc, id, header);
  // The headers go now; the body waits until loop() has filled the snapshot.
  // The library asks again on the next ACK or poll.
  AsyncWebServerResponse *response = request->beginChunkedResponse(_encoding_json,
    [read](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      (void)index;
      if(!snapshotSlots.filled(read->slot)) return RESPONSE_TRY_AGAIN;
      return read->writer.generate(buffer, maxLen);
    });
  request->send(response);
}

// Hands an edit of the shade model to loop() (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).
// The reply only says it was accepted; the result reaches every client as a
// requestResult event carrying the same requestId once loop() has applied it.
static void queueModelEdit(AsyncWebServerRequest *request, model_edit_type_t type, JsonVariantConst body) {
  if(!beginModelEdit()) {
    request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Busy, try the change again\"}"));
    return;
  }
  model_edit_t *edit = new (std::nothrow) model_edit_t();
  if(!edit) {
    cancelModelEdit(nullptr);
    sendQueueFull(request);
    return;
  }
  edit->type = type;
  edit->requestId = nextEditRequestId();
  edit->doc.set(body);
  const uint32_t requestId = edit->requestId;
  loop_item_t item;
  item.type = loop_item_type_t::model_edit;
  item.edit = edit;
  if(edit->doc.overflowed() || !loopQueue.submit(item)) {
    cancelModelEdit(edit);
    sendQueueFull(request);
    return;
  }
  char reply[64];
  snprintf(reply, sizeof(reply), "{\"status\":\"accepted\",\"requestId\":%lu}", (unsigned long)requestId);
  request->send(200, _encoding_json, reply);
}
// The JSON (PUT/POST) form of an edit route.  Bodies over 4 KB are refused with
// 413 before they reach the heap.
static AsyncCallbackJsonWebHandler *editHandler(const char *uri, model_edit_type_t type) {
  auto *handler = new AsyncCallbackJsonWebHandler(uri, [type](AsyncWebServerRequest *request, JsonVariant &json) {
    if(json.isNull()) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No object supplied.\"}")); return; }
    queueModelEdit(request, type, json);
  });
  handler->setMaxContentLength(4096);
  return handler;
}
// The GET form of an edit route: its query parameters become the request.
static void queueModelEditFromParams(AsyncWebServerRequest *request, model_edit_type_t type, std::initializer_list<const char *> names) {
  JsonDocument body;
  for(const char *name : names) {
    if(!asyncHasParam(request, name)) continue;
    const String value = asyncParam(request, name);
    if(strcmp(name, "paired") == 0) body[name] = toBoolean(value.c_str(), false);
    else body[name] = strtoul(value.c_str(), nullptr, 10);
  }
  queueModelEdit(request, type, body.as<JsonVariantConst>());
}

// -- Async handler implementations --
void Web::handleDiscovery(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    ESP_LOGD(TAG, "Async Discovery Requested");
    sendSnapshot(request, snapshot_doc_t::discovery, 0, discoveryHeader);
    net.needsBroadcast = true;
  }
  else
    request->send(500, _encoding_text, "Invalid http method");
}
void Web::handleGetRooms(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) sendSnapshot(request, snapshot_doc_t::rooms);
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleGetShades(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) sendSnapshot(request, snapshot_doc_t::shades);
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleGetGroups(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) sendSnapshot(request, snapshot_doc_t::groups);
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleController(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_GET) {
    settings.printAvailHeap();
    sendSnapshot(request, snapshot_doc_t::controller, 0, controllerHeader);
  }
  else request->send(404, _encoding_text, _response_404);
}
void Web::handleLogin(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  char token[65];
  memset(&token, 0x00, sizeof(token));
  this->createAPIToken(request->client()->remoteIP(), token);
  if(settings.Security.type == security_types::None) {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"apiKey\":\"%s\",\"msg\":\"Success\",\"success\":true}",
      static_cast<uint8_t>(settings.Security.type), token);
    request->send(200, _encoding_json, g_async_content);
    return;
  }
  char username[33] = "";
  char password[33] = "";
  char pin[5] = "";
  // Try query params first
  if(asyncHasParam(request, "username")) strlcpy(username, asyncParam(request, "username").c_str(), sizeof(username));
  if(asyncHasParam(request, "password")) strlcpy(password, asyncParam(request, "password").c_str(), sizeof(password));
  if(asyncHasParam(request, "pin")) strlcpy(pin, asyncParam(request, "pin").c_str(), sizeof(pin));
  // Override from JSON body if present
  if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["username"].isNull()) strlcpy(username, obj["username"], sizeof(username));
    if(!obj["password"].isNull()) strlcpy(password, obj["password"], sizeof(password));
    if(!obj["pin"].isNull()) strlcpy(pin, obj["pin"], sizeof(pin));
  }
  bool success = false;
  if(settings.Security.type == security_types::PinEntry) {
    char ptok[65];
    memset(ptok, 0x00, sizeof(ptok));
    this->createAPIPinToken(request->client()->remoteIP(), pin, ptok);
    if(String(ptok) == String(token)) success = true;
  }
  else if(settings.Security.type == security_types::Password) {
    char ptok[65];
    memset(ptok, 0x00, sizeof(ptok));
    this->createAPIPasswordToken(request->client()->remoteIP(), username, password, ptok);
    if(String(ptok) == String(token)) success = true;
  }
  if(success) {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"apiKey\":\"%s\",\"msg\":\"Success\",\"success\":true}",
      static_cast<uint8_t>(settings.Security.type), token);
    request->send(200, _encoding_json, g_async_content);
  }
  else {
    snprintf(g_async_content, sizeof(g_async_content),
      "{\"type\":%u,\"msg\":\"Invalid credentials\",\"success\":false}",
      static_cast<uint8_t>(settings.Security.type));
    request->send(401, _encoding_json, g_async_content);
  }
}
void Web::handleShadeCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  shade_request_t req;
  req.type = shade_request_type_t::shade_command;
  // Try query params
  if(asyncHasParam(request, "shadeId")) {
    req.id = asyncParam(request, "shadeId").toInt();
    if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
    else if(asyncHasParam(request, "target")) req.target = requestTarget(asyncParam(request, "target").toInt());
    if(asyncHasParam(request, "repeat")) req.repeat = requestRepeat(asyncParam(request, "repeat").toInt());
    if(asyncHasParam(request, "stepSize")) req.stepSize = asyncParam(request, "stepSize").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) req.id = obj["shadeId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    else if(!obj["target"].isNull()) req.target = requestTarget(obj["target"].as<int>());
    if(!obj["repeat"].isNull()) req.repeat = requestRepeat(obj["repeat"].as<int>());
    if(!obj["stepSize"].isNull()) req.stepSize = obj["stepSize"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
  SomfyShade *shade = somfy.getShadeById(req.id);
  if(shade) {
    ESP_LOGI(TAG, "handleShadeCommand shade=%u target=%d command=%s", req.id, req.target, translateSomfyCommand(req.command).c_str());
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
}
void Web::handleGroupCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  shade_request_t req;
  req.type = shade_request_type_t::group_command;
  if(asyncHasParam(request, "groupId")) {
    req.id = asyncParam(request, "groupId").toInt();
    if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
    if(asyncHasParam(request, "repeat")) req.repeat = requestRepeat(asyncParam(request, "repeat").toInt());
    if(asyncHasParam(request, "stepSize")) req.stepSize = asyncParam(request, "stepSize").toInt();
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["groupId"].isNull()) req.id = obj["groupId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    if(!obj["repeat"].isNull()) req.repeat = requestRepeat(obj["repeat"].as<int>());
    if(!obj["stepSize"].isNull()) req.stepSize = obj["stepSize"].as<uint8_t>();
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}")); return; }
  SomfyGroup *group = somfy.getGroupById(req.id);
  if(group) {
    ESP_LOGI(TAG, "handleGroupCommand group=%u command=%s", req.id, translateSomfyCommand(req.command).c_str());
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeGroupRef(group, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
}
void Web::handleTiltCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  shade_request_t req;
  req.type = shade_request_type_t::tilt_command;
  if(asyncHasParam(request, "shadeId")) {
    req.id = asyncParam(request, "shadeId").toInt();
    if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
    else if(asyncHasParam(request, "target")) req.target = requestTarget(asyncParam(request, "target").toInt());
  }
  else if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) req.id = obj["shadeId"];
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    else if(!obj["target"].isNull()) req.target = requestTarget(obj["target"].as<int>());
  }
  else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
  SomfyShade *shade = somfy.getShadeById(req.id);
  if(shade) {
    ESP_LOGI(TAG, "handleTiltCommand shade=%u target=%d command=%s", req.id, req.target, translateSomfyCommand(req.command).c_str());
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
}
void Web::handleRepeatCommand(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = 255;
  uint8_t groupId = 255;
  shade_request_t req;
  if(asyncHasParam(request, "shadeId")) shadeId = asyncParam(request, "shadeId").toInt();
  else if(asyncHasParam(request, "groupId")) groupId = asyncParam(request, "groupId").toInt();
  if(asyncHasParam(request, "command")) req.command = translateSomfyCommand(asyncParam(request, "command"));
  if(asyncHasParam(request, "repeat")) req.repeat = requestRepeat(asyncParam(request, "repeat").toInt());
  if(asyncHasParam(request, "stepSize")) req.stepSize = asyncParam(request, "stepSize").toInt();
  if(shadeId == 255 && groupId == 255 && !json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
    if(!obj["groupId"].isNull()) groupId = obj["groupId"];
    if(!obj["stepSize"].isNull()) req.stepSize = obj["stepSize"];
    if(!obj["command"].isNull()) { String scmd = obj["command"]; req.command = translateSomfyCommand(scmd); }
    if(!obj["repeat"].isNull()) req.repeat = requestRepeat(obj["repeat"].as<int>());
  }
  if(shadeId != 255) {
    ESP_LOGI(TAG, "handleRepeatCommand shade=%u command=%s", shadeId, translateSomfyCommand(req.command).c_str());
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(!shade) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade reference could not be found.\"}")); return; }
    req.type = shade_request_type_t::shade_repeat;
    req.id = shadeId;
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeShadeRef(shade, resp);
    resp.endObject();
    resp.endResponse();
  }
  else if(groupId != 255) {
    ESP_LOGI(TAG, "handleRepeatCommand group=%u command=%s", groupId, translateSomfyCommand(req.command).c_str());
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(!group) { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group reference could not be found.\"}")); return; }
    req.type = shade_request_type_t::group_repeat;
    req.id = groupId;
    if(!queueShadeRequest(request, req)) return;
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeGroupRef(group, resp);
    resp.endObject();
    resp.endResponse();
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleRoom(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_GET) {
    if(asyncHasParam(request, "roomId")) {
      int roomId = asyncParam(request, "roomId").toInt();
      // The status goes out before the snapshot is filled, so whether the room
      // exists is checked here, on the live model.
      if(somfy.getRoomById(roomId)) sendSnapshot(request, snapshot_doc_t::room, roomId);
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid room id.\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleShade(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_GET) {
    if(asyncHasParam(request, "shadeId")) {
      int shadeId = asyncParam(request, "shadeId").toInt();
      // As for /room, existence is checked on the live model.
      if(somfy.getShadeById(shadeId)) sendSnapshot(request, snapshot_doc_t::shade, shadeId);
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleGroup(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_GET) {
    if(asyncHasParam(request, "groupId")) {
      int groupId = asyncParam(request, "groupId").toInt();
      // As for /room, existence is checked on the live model.
      if(somfy.getGroupById(groupId)) sendSnapshot(request, snapshot_doc_t::group, groupId);
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleSetPositions(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  shade_request_t req;
  req.type = shade_request_type_t::set_positions;
  if(asyncHasParam(request, "shadeId")) req.id = asyncParam(request, "shadeId").toInt();
  if(asyncHasParam(request, "position")) req.position = requestTarget(asyncParam(request, "position").toInt());
  if(asyncHasParam(request, "tiltPosition")) req.tiltPosition = requestTarget(asyncParam(request, "tiltPosition").toInt());
  if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) req.id = obj["shadeId"];
    if(!obj["position"].isNull()) req.position = requestTarget(obj["position"].as<int>());
    if(!obj["tiltPosition"].isNull()) req.tiltPosition = requestTarget(obj["tiltPosition"].as<int>());
  }
  if(req.id != 255) {
    ESP_LOGI(TAG, "handleSetPositions shade=%u pos=%d tiltPos=%d", req.id, req.position, req.tiltPosition);
    SomfyShade *shade = somfy.getShadeById(req.id);
    if(shade) {
      if(!queueShadeRequest(request, req)) return;
      // loop() applies the request after we answer so this is the state before
      // it.  Clients get the result from the shadeState socket event.
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
}
void Web::handleSetSensor(AsyncWebServerRequest *request, JsonVariant &json) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  uint8_t shadeId = asyncHasParam(request, "shadeId") ? asyncParam(request, "shadeId").toInt() : 255;
  uint8_t groupId = asyncHasParam(request, "groupId") ? asyncParam(request, "groupId").toInt() : 255;
  shade_request_t req;
  if(asyncHasParam(request, "sunny")) req.sunny = toBoolean(asyncParam(request, "sunny").c_str(), false) ? 1 : 0;
  if(asyncHasParam(request, "windy")) req.windy = asyncParam(request, "windy").toInt();
  if(asyncHasParam(request, "repeat")) req.repeat = requestRepeat(asyncParam(request, "repeat").toInt());
  if(!json.isNull()) {
    JsonObject obj = json.as<JsonObject>();
    if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"].as<uint8_t>();
    if(!obj["groupId"].isNull()) groupId = obj["groupId"].as<uint8_t>();
    if(!obj["sunny"].isNull()) {
      if(obj["sunny"].is<bool>()) req.sunny = obj["sunny"].as<bool>() ? 1 : 0;
      else req.sunny = obj["sunny"].as<int8_t>();
    }
    if(!obj["windy"].isNull()) {
      if(obj["windy"].is<bool>()) req.windy = obj["windy"].as<bool>() ? 1 : 0;
      else req.windy = obj["windy"].as<int8_t>();
    }
    if(!obj["repeat"].isNull()) req.repeat = requestRepeat(obj["repeat"].as<int>());
  }
  if(shadeId != 255) {
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(shade) {
      req.type = shade_request_type_t::shade_sensor;
      req.id = shadeId;
      if(!queueShadeRequest(request, req)) return;
      // The main loop applies the command after we answer so this is the state
      // before it.  Clients get the result from the shadeState socket event.
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
  }
  else if(groupId != 255) {
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(group) {
      req.type = shade_request_type_t::group_sensor;
      req.id = groupId;
      if(!queueShadeRequest(request, req)) return;
      // As above, this is the state before the command and groupState follows.
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeGroup(group, resp);
      resp.endObject();
      resp.endResponse();
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid groupId was provided\"}"));
  }
  else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
}
void Web::handleDownloadFirmware(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  GitRepo repo;
  GitRelease *rel = nullptr;
  int8_t err = repo.getReleases();
  ESP_LOGI(TAG, "Async downloadFirmware called...");
  if(err == 0) {
    if(asyncHasParam(request, "ver")) {
      String ver = asyncParam(request, "ver");
      if(ver == "latest") rel = &repo.releases[0];
      else if(ver == "main") rel = &repo.releases[GIT_MAX_RELEASES];
      else {
        for(uint8_t i = 0; i < GIT_MAX_RELEASES; i++) {
          if(repo.releases[i].id == 0) continue;
          if(strcmp(repo.releases[i].name, ver.c_str()) == 0) { rel = &repo.releases[i]; break; }
        }
      }
      if(rel) {
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeGitRelease(rel, resp);
        resp.endObject();
        resp.endResponse();
        strcpy(git.targetRelease, rel->name);
        git.status = GIT_AWAITING_UPDATE;
      }
      else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release not found in repo.\"}"));
    }
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release version not supplied.\"}"));
  }
  else request->send(err, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error communicating with Github.\"}"));
}
void Web::handleBackup(AsyncWebServerRequest *request) {
  if(!this->isAuthenticated(request)) return;
  bool attach = false;
  if(asyncHasParam(request, "attach")) attach = toBoolean(asyncParam(request, "attach").c_str(), false);
  ESP_LOGI(TAG, "Async saving current shade information");
  somfy.writeBackup();
  if(somfy.backupData.length() == 0) {
    request->send(500, _encoding_text, "backup failed");
    return;
  }
  if(attach) {
    char filename[120];
    Timestamp ts;
    char *iso = ts.getISOTime();
    for(uint8_t i = 0; i < strlen(iso); i++) {
      if(iso[i] == '.') { iso[i] = '\0'; break; }
      if(iso[i] == ':') iso[i] = '_';
    }
    snprintf(filename, sizeof(filename), "attachment; filename=\"ESPSomfyRTS %s.backup\"", iso);
    AsyncWebServerResponse *response = request->beginResponse(200, _encoding_text, somfy.backupData);
    response->addHeader("Content-Disposition", filename);
    response->addHeader("Access-Control-Expose-Headers", "Content-Disposition");
    request->send(response);
  }
  else {
    request->send(200, _encoding_text, somfy.backupData);
  }
}
void Web::handleReboot(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  if(!this->isAuthenticated(request)) return;
  if(request->method() == HTTP_POST || request->method() == HTTP_PUT) {
    ESP_LOGI(TAG, "Async Rebooting ESP...");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    request->send(200, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully started reboot\"}");
  }
  else request->send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method\"}");
}
void Web::handleNotFound(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_OPTIONS) { request->send(200); return; }
  snprintf(g_async_content, sizeof(g_async_content), "404 Service Not Found: %s", request->url().c_str());
  request->send(404, _encoding_text, g_async_content);
}

// Logs how long part of a request took, when it is worth logging.
static void logRequestTime(const char *server, AsyncWebServerRequest *request, const IPAddress &ip, const char *part, uint32_t us) {
  if(!worthLogging(us)) return;
  char what[112];
  snprintf(what, sizeof(what), "%s %s %s from %u.%u.%u.%u %s", server, request->methodToString(), request->url().c_str(),
    ip[0], ip[1], ip[2], ip[3], part);
  logElapsed(TAG, what, us);
}
// Logs how long a request takes to handle and, once the connection closes, how
// long the whole exchange took.  The total includes chunked bodies like
// /controller that are built while they are sent, after the handler returns.
// The server name must be a literal as the disconnect handler keeps it.
static void timeRequest(AsyncWebServerRequest *request, ArMiddlewareNext next, const char *server) {
  ElapsedTimer timer;
  // Taken now as the client may already be gone when the disconnect runs.
  IPAddress ip = request->client() ? request->client()->remoteIP() : IPAddress();
  // Uploads set their own disconnect handler while the body arrives, before
  // this runs, and replacing it would stop the reboot after an update.
  if(!request->multipart()) {
    request->onDisconnect([request, timer, server, ip]() { logRequestTime(server, request, ip, "total", timer.us()); });
  }
  next();
  logRequestTime(server, request, ip, "handler", timer.us());
}

void Web::begin() {
  ESP_LOGI(TAG, "Creating Web MicroServices...");
  asyncApiServer.addMiddleware([](AsyncWebServerRequest *r, ArMiddlewareNext next) { timeRequest(r, next, "api"); });
  asyncServer.addMiddleware([](AsyncWebServerRequest *r, ArMiddlewareNext next) { timeRequest(r, next, "web"); });
  // Async API Server (port 8081)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
  // GET endpoints
  asyncApiServer.on("/discovery", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleDiscovery(r); });
  asyncApiServer.on("/rooms", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetRooms(r); });
  asyncApiServer.on("/shades", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetShades(r); });
  asyncApiServer.on("/groups", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetGroups(r); });
  asyncApiServer.on("/controller", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleController(r); });
  asyncApiServer.on("/room", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleRoom(r); });
  asyncApiServer.on("/shade", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleShade(r); });
  asyncApiServer.on("/group", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleGroup(r); });
  asyncApiServer.on("/downloadFirmware", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleDownloadFirmware(r); });
  asyncApiServer.on("/backup", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleBackup(r); });
  asyncApiServer.on("/reboot", WebRequestMethodComposite(HTTP_POST) | HTTP_PUT, [](AsyncWebServerRequest *r) { webServer.handleReboot(r); });
  // JSON body endpoints
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleShadeCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleGroupCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleTiltCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/repeatCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleRepeatCommand(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/setPositions",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleSetPositions(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/setSensor",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleSetSensor(r, j); }));
  asyncApiServer.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleLogin(r, j); }));
  // GET fallback for command endpoints (query params)
  asyncApiServer.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleShadeCommand(r, v); });
  asyncApiServer.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleGroupCommand(r, v); });
  asyncApiServer.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleTiltCommand(r, v); });
  asyncApiServer.on("/repeatCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleRepeatCommand(r, v); });
  asyncApiServer.on("/setPositions", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleSetPositions(r, v); });
  asyncApiServer.on("/setSensor", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleSetSensor(r, v); });
  asyncApiServer.on("/login", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleLogin(r, v); });
  // OPTIONS preflight + not found
  asyncApiServer.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });

  // Web Interface

  // Web Interface
  // Command endpoints - delegate to async handler methods
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleTiltCommand(request, json); }));
  asyncServer.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleTiltCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/repeatCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleRepeatCommand(request, json); }));
  asyncServer.on("/repeatCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleRepeatCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleShadeCommand(request, json); }));
  asyncServer.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleShadeCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleGroupCommand(request, json); }));
  asyncServer.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleGroupCommand(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setPositions",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleSetPositions(request, json); }));
  asyncServer.on("/setPositions", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleSetPositions(request, v); });

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setSensor",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleSetSensor(request, json); }));
  asyncServer.on("/setSensor", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleSetSensor(request, v); });

  asyncServer.on("/upnp.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/xml");
    SSDP.schema(*response);
    request->send(response);
  });

  // /  and /loginContext are already handled by serveStatic and startup()

  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleLogin(request, json); }));
  asyncServer.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleLogin(request, v); });

  asyncServer.on("/shades.cfg", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/shades.cfg", _encoding_text);
  });
  asyncServer.on("/shades.tmp", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/shades.tmp", _encoding_text);
  });

  asyncServer.on("/getReleases", HTTP_GET, [](AsyncWebServerRequest *request) {
    GitRepo repo;
    repo.getReleases();
    git.setCurrentRelease(repo);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    // Inline GitRepo::toJSON
    resp.beginObject("fwVersion");
    serializeAppVersion(resp, settings.fwVersion);
    resp.endObject();
    resp.beginObject("appVersion");
    serializeAppVersion(resp, settings.appVersion);
    resp.endObject();
    resp.beginArray("releases");
    for(uint8_t i = 0; i < GIT_MAX_RELEASES + 1; i++) {
      if(repo.releases[i].id == 0) continue;
      resp.beginObject();
      serializeGitRelease(&repo.releases[i], resp);
      resp.endObject();
    }
    resp.endArray();
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/downloadFirmware", HTTP_GET, [](AsyncWebServerRequest *request) { webServer.handleDownloadFirmware(request); });

  asyncServer.on("/cancelFirmware", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!git.lockFS) {
      git.status = GIT_UPDATE_CANCELLING;
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeGitVersion(resp);
      resp.endObject();
      resp.endResponse();
      git.cancelled = true;
    }
    else {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Cannot cancel during filesystem update.\"}"));
    }
  });

  asyncServer.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) { webServer.handleBackup(request); });

  asyncServer.on("/restore", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(webServer.uploadSuccess) {
        restore_options_t opts;
        if(asyncHasParam(request, "data")) {
          String dataStr = asyncParam(request, "data");
          ESP_LOGD(TAG, "%s", dataStr.c_str());
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, dataStr);
          if(err) {
            request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
            return;
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            opts.fromJSON(obj);
          }
        }
        else {
          ESP_LOGD(TAG, "No restore options sent.  Using defaults...");
          opts.shades = true;
        }
        // The main loop restores, since it owns the shades and their rolling
        // codes, and then reboots.
        loop_item_t item;
        item.type = loop_item_type_t::restore_shades;
        item.restoreParts = restoreParts(opts);
        if(!queueLoopItem(request, item)) return;
        request->send(200, _encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      esp_task_wdt_reset();
      if(index == 0) {
        webServer.uploadSuccess = false;
        ESP_LOGD(TAG, "Restore: %s", filename.c_str());
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      if(len > 0) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(data, len);
        fup.close();
      }
      if(final) {
        webServer.uploadSuccess = true;
      }
    });

  // Static file routes removed - handled by serveStatic in startup()

  asyncServer.on("/controller", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleController(request); });
  asyncServer.on("/rooms", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetRooms(request); });
  asyncServer.on("/shades", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetShades(request); });
  asyncServer.on("/groups", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetGroups(request); });
  asyncServer.on("/room", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleRoom(request); });
  asyncServer.on("/shade", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleShade(request); });
  asyncServer.on("/group", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGroup(request); });

  // What a new room, shade or group would get, from a snapshot.
  asyncServer.on("/getNextRoom", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::nextRoom); });
  asyncServer.on("/getNextShade", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::nextShade); });
  asyncServer.on("/getNextGroup", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::nextGroup); });
  asyncServer.on("/groupOptions", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!asyncHasParam(request, "groupId")) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
      return;
    }
    const int groupId = atoi(asyncParam(request, "groupId").c_str());
    // As for /group, existence is checked on the live model.
    if(somfy.getGroupById(groupId)) sendSnapshot(request, snapshot_doc_t::groupOptions, groupId);
    else request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
  });
  // Adding and saving rooms, shades and groups.  Edits of the shade model:
  // loop() applies them and sends requestResult.
  asyncServer.addHandler(editHandler("/addRoom", model_edit_type_t::add_room));
  asyncServer.addHandler(editHandler("/addShade", model_edit_type_t::add_shade));
  asyncServer.addHandler(editHandler("/addGroup", model_edit_type_t::add_group));
  asyncServer.addHandler(editHandler("/saveRoom", model_edit_type_t::save_room));
  asyncServer.addHandler(editHandler("/saveShade", model_edit_type_t::save_shade));
  asyncServer.addHandler(editHandler("/saveGroup", model_edit_type_t::save_group));


  // setMyPosition - supports both GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setMyPosition",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      int8_t pos = -1;
      int8_t tilt = -1;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
        else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}")); return; }
        if(!obj["pos"].isNull()) pos = obj["pos"].as<int8_t>();
        if(!obj["tilt"].isNull()) tilt = obj["tilt"].as<int8_t>();
      }
      SomfyShade* shade = somfy.getShadeById(shadeId);
      if(shade) {
        if(pos >= 0 && pos <= 100)
          if(!queueSetMyPosition(request, shade, pos, requestTarget(tilt))) return;
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShadeRef(shade, resp);
        resp.endObject();
        resp.endResponse();
      }
      else {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
      }
    }));
  asyncServer.on("/setMyPosition", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    int8_t pos = -1;
    int8_t tilt = -1;
    if(asyncHasParam(request, "shadeId")) {
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
      if(asyncHasParam(request, "pos")) pos = atoi(asyncParam(request, "pos").c_str());
      if(asyncHasParam(request, "tilt")) tilt = atoi(asyncParam(request, "tilt").c_str());
    }
    else { request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}")); return; }
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if(shade) {
      if(pos >= 0 && pos <= 100)
        if(!queueSetMyPosition(request, shade, pos, requestTarget(tilt))) return;
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShadeRef(shade, resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
    }
  });

  // setRollingCode - supports both query params and JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setRollingCode",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      uint16_t rollingCode = 0;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
        if(!obj["rollingCode"].isNull()) rollingCode = obj["rollingCode"];
      }
      SomfyShade* shade = nullptr;
      if(shadeId != 255) shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to set rolling code\"}"));
      }
      else {
        if(!queueSetRollingCode(request, shade, rollingCode)) return;
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp, shade->paired, rollingCode);
        resp.endObject();
        resp.endResponse();
      }
    }));
  asyncServer.on("/setRollingCode", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    uint16_t rollingCode = 0;
    if(asyncHasParam(request, "shadeId")) {
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
      rollingCode = atoi(asyncParam(request, "rollingCode").c_str());
    }
    SomfyShade* shade = nullptr;
    if(shadeId != 255) shade = somfy.getShadeById(shadeId);
    if(!shade) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to set rolling code\"}"));
    }
    else {
      if(!queueSetRollingCode(request, shade, rollingCode)) return;
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp, shade->paired, rollingCode);
      resp.endObject();
      resp.endResponse();
    }
  });

  asyncServer.addHandler(editHandler("/setPaired", model_edit_type_t::set_paired));
  asyncServer.on("/setPaired", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::set_paired, { "shadeId", "paired" });
  });

  // unpairShade
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/unpairShade",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      uint8_t shadeId = 255;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        if(!obj["shadeId"].isNull()) shadeId = obj["shadeId"];
      }
      SomfyShade* shade = nullptr;
      if(shadeId != 255) shade = somfy.getShadeById(shadeId);
      if(!shade) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to unpair\"}"));
      }
      else {
        if(!queueUnpairShade(request, shade)) return;
        AsyncJsonResp resp;
        resp.beginResponse(request, g_async_content, sizeof(g_async_content));
        resp.beginObject();
        serializeShade(shade, resp, false, shade->lastRollingCode);
        resp.endObject();
        resp.endResponse();
      }
    }));
  asyncServer.on("/unpairShade", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t shadeId = 255;
    if(asyncHasParam(request, "shadeId"))
      shadeId = atoi(asyncParam(request, "shadeId").c_str());
    SomfyShade* shade = nullptr;
    if(shadeId != 255) shade = somfy.getShadeById(shadeId);
    if(!shade) {
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found to unpair\"}"));
    }
    else {
      if(!queueUnpairShade(request, shade)) return;
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      serializeShade(shade, resp, false, shade->lastRollingCode);
      resp.endObject();
      resp.endResponse();
    }
  });

  // Linking repeaters, remotes and group members, and deleting.  Edits of the
  // shade model: loop() applies them and sends requestResult.
  asyncServer.addHandler(editHandler("/linkRepeater", model_edit_type_t::link_repeater));
  asyncServer.on("/linkRepeater", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::link_repeater, { "address" });
  });
  asyncServer.addHandler(editHandler("/unlinkRepeater", model_edit_type_t::unlink_repeater));
  asyncServer.on("/unlinkRepeater", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::unlink_repeater, { "address" });
  });
  asyncServer.addHandler(editHandler("/linkRemote", model_edit_type_t::link_remote));
  asyncServer.addHandler(editHandler("/unlinkRemote", model_edit_type_t::unlink_remote));
  asyncServer.addHandler(editHandler("/linkToGroup", model_edit_type_t::link_to_group));
  asyncServer.addHandler(editHandler("/unlinkFromGroup", model_edit_type_t::unlink_from_group));
  asyncServer.addHandler(editHandler("/deleteRoom", model_edit_type_t::delete_room));
  asyncServer.on("/deleteRoom", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::delete_room, { "roomId" });
  });
  asyncServer.addHandler(editHandler("/deleteShade", model_edit_type_t::delete_shade));
  asyncServer.on("/deleteShade", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::delete_shade, { "shadeId" });
  });
  asyncServer.addHandler(editHandler("/deleteGroup", model_edit_type_t::delete_group));
  asyncServer.on("/deleteGroup", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::delete_group, { "groupId" });
  });
  // The repeaters, from a snapshot like the other reads.
  asyncServer.on("/repeaters", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::repeaters); });


  // updateFirmware - file upload
  asyncServer.on("/updateFirmware", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(Update.hasError())
        request->send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
      else
        request->send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated firmware\"}");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(index == 0) {
        webServer.uploadSuccess = false;
        ESP_LOGI(TAG, "Update: %s", filename.c_str());
        if(!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          ESP_LOGE(TAG, "Update begin failed");
        }
        else {
          somfy.transceiver.end();
          mqtt.end();
          request->onDisconnect(onUploadDisconnect);
        }
      }
      if(len > 0) {
        if(Update.write(data, len) != len) {
          ESP_LOGE(TAG, "Upload of %s aborted invalid size %d", filename.c_str(), len);
          Update.abort();
        }
      }
      if(final) {
        if(Update.end(true)) {
          ESP_LOGI(TAG, "Update Success: %u Rebooting...", index + len);
          webServer.uploadSuccess = true;
        }
        else {
          ESP_LOGE(TAG, "Update end failed");
        }
      }
      esp_task_wdt_reset();
    });

  // updateShadeConfig - file upload
  asyncServer.on("/updateShadeConfig", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(git.lockFS) {
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
        return;
      }
      request->send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Shade Config: \"}");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(index == 0) {
        ESP_LOGI(TAG, "Update: shades.cfg");
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
      }
      if(len > 0) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(data, len);
        fup.close();
      }
      if(final) {
        // The main loop loads it, since it owns the shades.
        loop_item_t item;
        item.type = loop_item_type_t::load_shades;
        loopQueue.submit(item);
      }
    });

  // updateApplication - file upload
  asyncServer.on("/updateApplication", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if(Update.hasError())
        request->send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating application: \"}");
      else
        request->send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated application\"}");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(index == 0) {
        webServer.uploadSuccess = false;
        ESP_LOGI(TAG, "Update: %s", filename.c_str());
        if(!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
          ESP_LOGE(TAG, "Update begin failed");
        }
        else {
          somfy.transceiver.end();
          mqtt.end();
          request->onDisconnect(onUploadDisconnect);
        }
      }
      if(len > 0) {
        if(Update.write(data, len) != len) {
          ESP_LOGE(TAG, "Upload of %s aborted invalid size %d", filename.c_str(), len);
          Update.abort();
        }
      }
      if(final) {
        if(Update.end(true)) {
          webServer.uploadSuccess = true;
          ESP_LOGI(TAG, "Update Success: %u Rebooting...", index + len);
          somfy.commit();
        }
        else {
          somfy.commit();
          ESP_LOGE(TAG, "Update end failed");
        }
      }
      esp_task_wdt_reset();
    });

  asyncServer.on("/scanaps", HTTP_GET, [](AsyncWebServerRequest *request) {
    esp_task_wdt_reset();
    esp_task_wdt_delete(NULL);
    if(net.softAPOpened) WiFi.disconnect(false);
    int n = WiFi.scanNetworks(false, true);
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Scanned %d networks", n);
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.beginObject("connected");
    resp.addElem("name", settings.WIFI.ssid);
    resp.addElem("passphrase", settings.WIFI.passphrase);
    resp.addElem("strength", (int32_t)WiFi.RSSI());
    resp.addElem("channel", (int32_t)WiFi.channel());
    resp.endObject();
    resp.beginArray("accessPoints");
    for(int i = 0; i < n; ++i) {
      if(WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue;
      resp.beginObject();
      resp.addElem("name", WiFi.SSID(i).c_str());
      resp.addElem("channel", (int32_t)WiFi.channel(i));
      resp.addElem("strength", (int32_t)WiFi.RSSI(i));
      resp.addElem("macAddress", WiFi.BSSIDstr(i).c_str());
      resp.endObject();
    }
    resp.endArray();
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/reboot", WebRequestMethodComposite(HTTP_POST) | HTTP_PUT, [](AsyncWebServerRequest *request) { webServer.handleReboot(request); });

  // saveSecurity
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveSecurity",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(400, _encoding_html, "Error parsing JSON body");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      settings.Security.fromJSON(obj);
      settings.Security.save();
      char token[65];
      webServer.createAPIToken(request->client()->remoteIP(), token);
      JsonDocument sdoc;
      JsonObject sobj = sdoc.to<JsonObject>();
      settings.Security.toJSON(sobj);
      sobj["apiKey"] = token;
      serializeJson(sdoc, g_async_content, sizeof(g_async_content));
      request->send(200, _encoding_json, g_async_content);
    }));

  // getSecurity
  asyncServer.on("/getSecurity", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // saveRadio
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/saveRadio",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(400, _encoding_html, "Error parsing JSON body");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      somfy.transceiver.fromJSON(obj);
      AsyncJsonResp resp;
      resp.beginResponse(request, g_async_content, sizeof(g_async_content));
      resp.beginObject();
      resp.beginObject("config");
      serializeTransceiverConfig(resp);
      resp.endObject();
      resp.endObject();
      resp.endResponse();
    }));

  // getRadio
  asyncServer.on("/getRadio", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  // sendRemoteCommand - supports GET with query params and POST/PUT with JSON body
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/sendRemoteCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      somfy_frame_t frame;
      uint8_t repeats = 0;
      if(!json.isNull()) {
        JsonObject obj = json.as<JsonObject>();
        String scmd;
        if(!obj["address"].isNull()) frame.remoteAddress = obj["address"];
        if(!obj["command"].isNull()) scmd = obj["command"].as<String>();
        if(!obj["repeats"].isNull()) repeats = obj["repeats"];
        if(!obj["rcode"].isNull()) frame.rollingCode = obj["rcode"];
        if(!obj["encKey"].isNull()) frame.encKey = obj["encKey"];
        frame.cmd = translateSomfyCommand(scmd.c_str());
      }
      if(frame.remoteAddress > 0 && frame.rollingCode > 0) {
        if(somfy.sendFrame(frame, repeats))
          request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
        else
          sendRadioUnavailable(request);
      }
      else
        request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
    }));
  asyncServer.on("/sendRemoteCommand", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy_frame_t frame;
    uint8_t repeats = 0;
    if(asyncHasParam(request, "address")) {
      frame.remoteAddress = atoi(asyncParam(request, "address").c_str());
      if(asyncHasParam(request, "encKey")) frame.encKey = atoi(asyncParam(request, "encKey").c_str());
      if(asyncHasParam(request, "command")) frame.cmd = translateSomfyCommand(asyncParam(request, "command"));
      if(asyncHasParam(request, "rcode")) frame.rollingCode = atoi(asyncParam(request, "rcode").c_str());
      if(asyncHasParam(request, "repeats")) repeats = atoi(asyncParam(request, "repeats").c_str());
    }
    if(frame.remoteAddress > 0 && frame.rollingCode > 0) {
      if(somfy.sendFrame(frame, repeats))
        request->send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
      else
        sendRadioUnavailable(request);
    }
    else
      request->send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
  });

  // setgeneral
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setgeneral",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      if(!obj["hostname"].isNull() || !obj["ssdpBroadcast"].isNull() || !obj["checkForUpdate"].isNull()) {
        bool checkForUpdate = settings.checkForUpdate;
        settings.fromJSON(obj);
        settings.save();
        if(settings.checkForUpdate != checkForUpdate) git.emitUpdateCheck();
        if(!obj["hostname"].isNull()) net.updateHostname();
      }
      if(!obj["ntpServer"].isNull() || !obj["ntpServer"].isNull()) {
        settings.NTP.fromJSON(obj);
        settings.NTP.save();
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set General Settings\"}");
    }));

  // setNetwork
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setNetwork",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(400, _encoding_html, "Error parsing JSON body");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      bool reboot = false;
      if(!obj["connType"].isNull() && obj["connType"].as<uint8_t>() != static_cast<uint8_t>(settings.connType)) {
        settings.connType = static_cast<conn_types_t>(obj["connType"].as<uint8_t>());
        settings.save();
        reboot = true;
      }
      if(!obj["wifi"].isNull()) {
        JsonObject objWifi = obj["wifi"];
        if(settings.connType == conn_types_t::wifi) {
          if(!objWifi["ssid"].isNull() && objWifi["ssid"].as<String>().compareTo(settings.WIFI.ssid) != 0) {
            if(WiFi.softAPgetStationNum() == 0) reboot = true;
          }
          if(!objWifi["passphrase"].isNull() && objWifi["passphrase"].as<String>().compareTo(settings.WIFI.passphrase) != 0) {
            if(WiFi.softAPgetStationNum() == 0) reboot = true;
          }
        }
        settings.WIFI.fromJSON(objWifi);
        settings.WIFI.save();
      }
      if(!obj["ethernet"].isNull()) {
        JsonObject objEth = obj["ethernet"];
        if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref)
          reboot = true;
#ifndef CONFIG_IDF_TARGET_ESP32C6
        settings.Ethernet.fromJSON(objEth);
        settings.Ethernet.save();
#endif
      }
      if(reboot) {
        ESP_LOGI(TAG, "Rebooting ESP for new Network settings...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 1000;
      }
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    }));

  // setIP
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/setIP",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      ESP_LOGD(TAG, "Setting IP...");
      JsonObject obj = json.as<JsonObject>();
      settings.IP.fromJSON(obj);
      settings.IP.save();
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    }));

  // connectwifi
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/connectwifi",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      ESP_LOGD(TAG, "Settings WIFI connection...");
      String ssid = "";
      String passphrase = "";
      if(!obj["ssid"].isNull()) ssid = obj["ssid"].as<String>();
      if(!obj["passphrase"].isNull()) passphrase = obj["passphrase"].as<String>();
      bool reboot = false;
      if(ssid.compareTo(settings.WIFI.ssid) != 0) reboot = true;
      if(passphrase.compareTo(settings.WIFI.passphrase) != 0) reboot = true;
      if(!settings.WIFI.ssidExists(ssid.c_str()) && ssid.length() > 0) {
        request->send(400, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"WiFi Network Does not exist\"}");
      }
      else {
        SETCHARPROP(settings.WIFI.ssid, ssid.c_str(), sizeof(settings.WIFI.ssid));
        SETCHARPROP(settings.WIFI.passphrase, passphrase.c_str(), sizeof(settings.WIFI.passphrase));
        settings.WIFI.save();
        settings.WIFI.print();
        request->send(201, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully set server connection\"}");
        if(reboot) {
          ESP_LOGI(TAG, "Rebooting ESP for new WiFi settings...");
          rebootDelay.reboot = true;
          rebootDelay.rebootTime = millis() + 1000;
        }
      }
    }));

  // modulesettings
  asyncServer.on("/modulesettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    resp.addElem("fwVersion", settings.fwVersion.name);
    resp.addElem("ssdpBroadcast", settings.ssdpBroadcast);
    resp.addElem("hostname", settings.hostname);
    resp.addElem("connType", static_cast<uint8_t>(settings.connType));
    resp.addElem("chipModel", settings.chipModel);
    resp.addElem("checkForUpdate", settings.checkForUpdate);
    resp.addElem("ntpServer", settings.NTP.ntpServer);
    resp.addElem("posixZone", settings.NTP.posixZone);
    resp.endObject();
    resp.endResponse();
  });

  // networksettings
  asyncServer.on("/networksettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.toJSON(obj);
    obj["fwVersion"] = settings.fwVersion.name;
    JsonObject eth = obj.createNestedObject("ethernet");
#ifndef CONFIG_IDF_TARGET_ESP32C6
    settings.Ethernet.toJSON(eth);
#endif
    JsonObject wifi = obj.createNestedObject("wifi");
    settings.WIFI.toJSON(wifi);
    JsonObject ip = obj.createNestedObject("ip");
    settings.IP.toJSON(ip);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // connectmqtt
  asyncServer.addHandler(new AsyncCallbackJsonWebHandler("/connectmqtt",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      ESP_LOGD(TAG, "Saving MQTT");
      mqtt.disconnect();
      settings.MQTT.fromJSON(obj);
      settings.MQTT.save();
      JsonDocument sdoc;
      JsonObject sobj = sdoc.to<JsonObject>();
      settings.MQTT.toJSON(sobj);
      serializeJson(sdoc, g_async_content, sizeof(g_async_content));
      request->send(200, _encoding_json, g_async_content);
    }));

  // mqttsettings
  asyncServer.on("/mqttsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.MQTT.toJSON(obj);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // Sort orders: the ids in the order the client lists them.
  asyncServer.addHandler(editHandler("/roomSortOrder", model_edit_type_t::room_order));
  asyncServer.addHandler(editHandler("/shadeSortOrder", model_edit_type_t::shade_order));
  asyncServer.addHandler(editHandler("/groupSortOrder", model_edit_type_t::group_order));

  asyncServer.on("/beginFrequencyScan", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy.transceiver.beginFrequencyScan();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/endFrequencyScan", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy.transceiver.endFrequencyScan();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  asyncServer.on("/recoverFilesystem", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) {
    if(git.status == GIT_UPDATING)
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Filesystem is updating.  Please wait!!!\"}");
    else if(git.status != GIT_STATUS_READY)
      request->send(200, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Cannot recover file system at this time.\"}");
    else {
      git.recoverFilesystem();
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Recovering filesystem from github please wait!!!\"}");
    }
  });

  asyncServer.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });

  // serveStatic MUST be registered AFTER all route handlers so it doesn't shadow them
  asyncServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  asyncServer.begin();
}
