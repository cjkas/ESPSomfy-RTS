#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "ConfigSettings.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "SSDP.h"
#include "Somfy.h"
#include "WResp.h"
#include "LoopQueue.h"
#include "ModelSnapshot.h"
#include "SomfyEditor.h"
#include "Web.h"
#include "WebRoutes.h"
#include "MqttConnection.h"
#include "GitOTA.h"
#include "ESPNetwork.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

// WebUiRoutes: the routes the config web UI calls on port 80, and the UI's own
// files.

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MqttConnection mqtt;
extern GitUpdater git;
extern ESPNetwork net;
extern LoopQueue loopQueue;

static const char *TAG = "Web";
static const char _encoding_html[] = "text/html";

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

void registerWebUiRoutes(AsyncWebServer &server) {
  server.on("/loginContext", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  // Web Interface
  // Command endpoints - delegate to async handler methods
  server.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleTiltCommand(request, json); }));
  server.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleTiltCommand(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/repeatCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleRepeatCommand(request, json); }));
  server.on("/repeatCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleRepeatCommand(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleShadeCommand(request, json); }));
  server.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleShadeCommand(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleGroupCommand(request, json); }));
  server.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleGroupCommand(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/setPositions",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleSetPositions(request, json); }));
  server.on("/setPositions", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleSetPositions(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/setSensor",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleSetSensor(request, json); }));
  server.on("/setSensor", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleSetSensor(request, v); });

  server.on("/upnp.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/xml");
    SSDP.schema(*response);
    request->send(response);
  });

  // /  and /loginContext are already handled by serveStatic and startup()

  server.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleLogin(request, json); }));
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleLogin(request, v); });

  server.on("/shades.cfg", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/shades.cfg", _encoding_text);
  });
  server.on("/shades.tmp", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/shades.tmp", _encoding_text);
  });

  server.on("/getReleases", HTTP_GET, [](AsyncWebServerRequest *request) {
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

  server.on("/downloadFirmware", HTTP_GET, [](AsyncWebServerRequest *request) { webServer.handleDownloadFirmware(request); });

  server.on("/cancelFirmware", HTTP_GET, [](AsyncWebServerRequest *request) {
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

  server.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) { webServer.handleBackup(request); });

  server.on("/restore", HTTP_POST,
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

  server.on("/controller", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleController(request); });
  server.on("/rooms", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetRooms(request); });
  server.on("/shades", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetShades(request); });
  server.on("/groups", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGetGroups(request); });
  server.on("/room", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleRoom(request); });
  server.on("/shade", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleShade(request); });
  server.on("/group", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) { webServer.handleGroup(request); });

  // What a new room, shade or group would get, from a snapshot.
  server.on("/getNextRoom", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::nextRoom); });
  server.on("/getNextShade", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::nextShade); });
  server.on("/getNextGroup", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::nextGroup); });
  server.on("/groupOptions", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server.addHandler(editHandler("/addRoom", model_edit_type_t::add_room));
  server.addHandler(editHandler("/addShade", model_edit_type_t::add_shade));
  server.addHandler(editHandler("/addGroup", model_edit_type_t::add_group));
  server.addHandler(editHandler("/saveRoom", model_edit_type_t::save_room));
  server.addHandler(editHandler("/saveShade", model_edit_type_t::save_shade));
  server.addHandler(editHandler("/saveGroup", model_edit_type_t::save_group));


  // setMyPosition - supports both GET with query params and POST/PUT with JSON body
  server.addHandler(new AsyncCallbackJsonWebHandler("/setMyPosition",
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
  server.on("/setMyPosition", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server.addHandler(new AsyncCallbackJsonWebHandler("/setRollingCode",
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
  server.on("/setRollingCode", HTTP_GET, [](AsyncWebServerRequest *request) {
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

  server.addHandler(editHandler("/setPaired", model_edit_type_t::set_paired));
  server.on("/setPaired", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::set_paired, { "shadeId", "paired" });
  });

  // unpairShade
  server.addHandler(new AsyncCallbackJsonWebHandler("/unpairShade",
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
  server.on("/unpairShade", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server.addHandler(editHandler("/linkRepeater", model_edit_type_t::link_repeater));
  server.on("/linkRepeater", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::link_repeater, { "address" });
  });
  server.addHandler(editHandler("/unlinkRepeater", model_edit_type_t::unlink_repeater));
  server.on("/unlinkRepeater", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::unlink_repeater, { "address" });
  });
  server.addHandler(editHandler("/linkRemote", model_edit_type_t::link_remote));
  server.addHandler(editHandler("/unlinkRemote", model_edit_type_t::unlink_remote));
  server.addHandler(editHandler("/linkToGroup", model_edit_type_t::link_to_group));
  server.addHandler(editHandler("/unlinkFromGroup", model_edit_type_t::unlink_from_group));
  server.addHandler(editHandler("/deleteRoom", model_edit_type_t::delete_room));
  server.on("/deleteRoom", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::delete_room, { "roomId" });
  });
  server.addHandler(editHandler("/deleteShade", model_edit_type_t::delete_shade));
  server.on("/deleteShade", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::delete_shade, { "shadeId" });
  });
  server.addHandler(editHandler("/deleteGroup", model_edit_type_t::delete_group));
  server.on("/deleteGroup", HTTP_GET, [](AsyncWebServerRequest *request) {
    queueModelEditFromParams(request, model_edit_type_t::delete_group, { "groupId" });
  });
  // The repeaters, from a snapshot like the other reads.
  server.on("/repeaters", HTTP_GET, [](AsyncWebServerRequest *request) { sendSnapshot(request, snapshot_doc_t::repeaters); });


  // updateFirmware - file upload
  server.on("/updateFirmware", HTTP_POST,
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
  server.on("/updateShadeConfig", HTTP_POST,
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
  server.on("/updateApplication", HTTP_POST,
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

  server.on("/scanaps", HTTP_GET, [](AsyncWebServerRequest *request) {
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

  server.on("/reboot", WebRequestMethodComposite(HTTP_POST) | HTTP_PUT, [](AsyncWebServerRequest *request) { webServer.handleReboot(request); });

  // saveSecurity
  server.addHandler(new AsyncCallbackJsonWebHandler("/saveSecurity",
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
  server.on("/getSecurity", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // saveRadio
  server.addHandler(new AsyncCallbackJsonWebHandler("/saveRadio",
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
  server.on("/getRadio", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  // sendRemoteCommand - supports GET with query params and POST/PUT with JSON body
  server.addHandler(new AsyncCallbackJsonWebHandler("/sendRemoteCommand",
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
  server.on("/sendRemoteCommand", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server.addHandler(new AsyncCallbackJsonWebHandler("/setgeneral",
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
  server.addHandler(new AsyncCallbackJsonWebHandler("/setNetwork",
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
  server.addHandler(new AsyncCallbackJsonWebHandler("/setIP",
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
  server.addHandler(new AsyncCallbackJsonWebHandler("/connectwifi",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      if(json.isNull()) {
        request->send(500, "application/json", "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
        return;
      }
      JsonObject obj = json.as<JsonObject>();
      ESP_LOGD(TAG, "Settings WIFI connection...");
      String ssid = "";
      // Clients never receive the stored passphrase, so an omitted one keeps it.
      String passphrase = settings.WIFI.passphrase;
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
  server.on("/modulesettings", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server.on("/networksettings", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server.addHandler(new AsyncCallbackJsonWebHandler("/connectmqtt",
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
  server.on("/mqttsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.MQTT.toJSON(obj);
    serializeJson(doc, g_async_content, sizeof(g_async_content));
    request->send(200, _encoding_json, g_async_content);
  });

  // Sort orders: the ids in the order the client lists them.
  server.addHandler(editHandler("/roomSortOrder", model_edit_type_t::room_order));
  server.addHandler(editHandler("/shadeSortOrder", model_edit_type_t::shade_order));
  server.addHandler(editHandler("/groupSortOrder", model_edit_type_t::group_order));

  server.on("/beginFrequencyScan", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy.transceiver.beginFrequencyScan();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  server.on("/endFrequencyScan", HTTP_GET, [](AsyncWebServerRequest *request) {
    somfy.transceiver.endFrequencyScan();
    AsyncJsonResp resp;
    resp.beginResponse(request, g_async_content, sizeof(g_async_content));
    resp.beginObject();
    serializeTransceiverConfig(resp);
    resp.endObject();
    resp.endResponse();
  });

  server.on("/recoverFilesystem", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *request) {
    if(git.status == GIT_UPDATING)
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Filesystem is updating.  Please wait!!!\"}");
    else if(git.status != GIT_STATUS_READY)
      request->send(200, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Cannot recover file system at this time.\"}");
    else {
      git.recoverFilesystem();
      request->send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Recovering filesystem from github please wait!!!\"}");
    }
  });

  server.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });

  // serveStatic MUST be registered AFTER all route handlers so it doesn't shadow them
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

}
