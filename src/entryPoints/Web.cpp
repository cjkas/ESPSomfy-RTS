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
#include "WebRoutes.h"
#include "MqttConnection.h"
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
extern LoopStats loopStats;
extern SomfyShadeController somfy;
extern Web webServer;
extern MqttConnection mqtt;
extern GitUpdater git;
extern ESPNetwork net;
extern LoopQueue loopQueue;
extern SnapshotSlots snapshotSlots;

char g_async_content[WEB_MAX_RESPONSE];


// General responses
static const char _response_404[] = "404: Service Not Found";


// Encodings
const char _encoding_text[] = "text/plain";
const char _encoding_json[] = "application/json";

static const char *TAG = "Web";

// Hands an item to loop(), which owns the shades.  When it can't be the
// request is answered with the reason and false is returned.
bool queueLoopItem(AsyncWebServerRequest *request, const loop_item_t &item) {
  // Nothing can go on air during a frequency scan so tell the client now rather
  // than dropping the request once it reaches the radio.
  if(loopItemUsesRadio(item) && somfy.transceiver.txSuspended()) { sendRadioUnavailable(request); return false; }
  if(!loopQueue.submit(item)) { sendQueueFull(request); return false; }
  return true;
}
bool queueShadeRequest(AsyncWebServerRequest *request, const shade_request_t &req) {
  loop_item_t item;
  item.request = req;
  return queueLoopItem(request, item);
}
// Sent when a command can't be handed to the main loop.
void sendQueueFull(AsyncWebServerRequest *request) {
  request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Command queue full\"}"));
}
// Sent when the radio can't take a frame (suspended or queue full).
void sendRadioUnavailable(AsyncWebServerRequest *request) {
  request->send(503, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Radio TX unavailable\"}"));
}

AsyncWebServer asyncServer(80);
AsyncWebServer asyncApiServer(8081);
void Web::startup() {
  ESP_LOGI(TAG, "Launching web server...");
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
String asyncParam(AsyncWebServerRequest *request, const char *name) {
  if(request->hasParam(name)) return request->getParam(name)->value();
  return String();
}
bool asyncHasParam(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name);
}

// -- Serialization helpers --
// Replies that still read the live shade model on this task (command and edit
// replies) copy one item into the ModelSnapshot types and write it the way the
// read routes do.  The read routes themselves write from a ModelSnapshot that
// loop() fills (sendSnapshot).
void serializeShadeRef(SomfyShade *shade, JsonFormatter &json) {
  shade_snapshot_t s;
  snapshotShade(*shade, s);
  writeShadeRef(json, s);
}
// paired and lastRollingCode are passed in for a response that reports the
// change the main loop is about to make.  An unpair is held until it goes on
// air (holdLoopItem), so the answer comes true.
void serializeShade(SomfyShade *shade, JsonFormatter &json, bool paired, uint16_t lastRollingCode) {
  shade_snapshot_t s;
  snapshotShade(*shade, s);
  s.paired = paired;
  s.lastRollingCode = lastRollingCode;
  writeShade(json, s);
}
void serializeShade(SomfyShade *shade, JsonFormatter &json) { serializeShade(shade, json, shade->paired, shade->lastRollingCode); }
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
void serializeTransceiverConfig(JsonFormatter &json) {
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
void serializeAppVersion(JsonFormatter &json, appver_t &ver) {
  json.addElem("name", ver.name);
  json.addElem("major", ver.major);
  json.addElem("minor", ver.minor);
  json.addElem("build", ver.build);
  json.addElem("suffix", ver.suffix);
}
void serializeGitVersion(JsonFormatter &json) {
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
void serializeGitRelease(GitRelease *rel, JsonFormatter &json) {
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
  // This runs on the web server's task, so it reports the last window the
  // loop task closed rather than reading the one it is still filling.
  json.addElem("loopRate", loopStats.publishedRate());
  json.addElem("loopMaxUs", loopStats.publishedMaxUs());
  json.endObject();
}

// A read served from a ModelSnapshot that loop() fills, so the document
// describes one moment (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).
// Each item is formatted into a fixed 3 KB unit and streamed as the TCP send
// window allows, so there is no growing response buffer either.
struct SnapshotRead {
  uint8_t slot;
  char unit[SNAPSHOT_UNIT_SIZE];
  SnapshotWriter writer;
  SnapshotRead(uint8_t slot, snapshot_doc_t doc, uint8_t id, snapshot_header_t header)
    : slot(slot), writer(snapshotSlots.snapshot(slot), doc, id, header, unit, sizeof(unit)) {}
  // The response is gone, whether it finished or the client went away.
  ~SnapshotRead() { snapshotSlots.release(this->slot); }
};
void sendSnapshot(AsyncWebServerRequest *request, snapshot_doc_t doc, uint8_t id, snapshot_header_t header) {
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
  registerApiRoutes(asyncApiServer);
  registerWebUiRoutes(asyncServer);
  asyncServer.begin();
}
