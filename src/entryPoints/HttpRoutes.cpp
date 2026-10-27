#include <LittleFS.h>
#include <Update.h>
#include "TaskWdt.h"
#include "esp_log.h"
#include "ConfigSettings.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "SSDP.h"
#include "Somfy.h"
#include "LoopQueue.h"
#include "Web.h"
#include "WebRoutes.h"
#include "MqttConnection.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

// HttpRoutes: the HTTP routes on port 80, the web UI's files and the few that
// stay HTTP (docs/adr/0006-two-security-modes-eleven-http-routes.md).
// Everything else is an op on the socket at /ws.

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MqttConnection mqtt;

static const char *TAG = "Web";

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
void registerHttpRoutes(AsyncWebServer &server) {
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
  // Commands for scripts, as a JSON body or query parameters.
  server.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleShadeCommand(request, json); }));
  server.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleShadeCommand(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleGroupCommand(request, json); }));
  server.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleGroupCommand(request, v); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleTiltCommand(request, json); }));
  server.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleTiltCommand(request, v); });

  server.on("/upnp.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/xml");
    SSDP.schema(*response);
    request->send(response);
  });

  server.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *request, JsonVariant &json) { webServer.handleLogin(request, json); }));
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) { JsonVariant v; webServer.handleLogin(request, v); });

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
            request->send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"JSON parse error\"}");
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
        // Checked here so the client hears about a bad file.  It only reads
        // the file; the shades are still changed on loop() alone.
        if(!ShadeConfigFile::validateFile("/shades.tmp")) {
          LittleFS.remove("/shades.tmp");
          request->send(400, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"This isn't a valid backup file, so nothing was restored.\"}"));
          return;
        }
        // The main loop restores, since it owns the shades and their rolling
        // codes, and then reboots.
        loop_item_t item;
        item.type = loop_item_type_t::restore_shades;
        item.restoreParts = restoreParts(opts);
        if(!queueLoopItem(request, item)) return;
        request->send(200, _encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
      }
      else request->send(400, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"The upload didn't complete, so nothing was restored.\"}"));
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      resetTaskWdt();
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
      resetTaskWdt();
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
      resetTaskWdt();
    });

  server.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });

  // serveStatic MUST be registered AFTER all route handlers so it doesn't shadow them
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

}
