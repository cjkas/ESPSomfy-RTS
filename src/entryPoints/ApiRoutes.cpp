#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "Web.h"
#include "WebRoutes.h"

// ApiRoutes: the routes integrations such as Home Assistant call on port 8081.
// The handlers are the ones the config web UI calls too (Web.cpp).

extern Web webServer;

void registerApiRoutes(AsyncWebServer &server) {
  // GET endpoints
  server.on("/discovery", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleDiscovery(r); });
  server.on("/rooms", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetRooms(r); });
  server.on("/shades", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetShades(r); });
  server.on("/groups", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleGetGroups(r); });
  server.on("/controller", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleController(r); });
  server.on("/room", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleRoom(r); });
  server.on("/shade", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleShade(r); });
  server.on("/group", HTTP_GET, [](AsyncWebServerRequest *r) { webServer.handleGroup(r); });
  server.on("/downloadFirmware", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleDownloadFirmware(r); });
  server.on("/backup", WebRequestMethodComposite(HTTP_GET) | HTTP_POST, [](AsyncWebServerRequest *r) { webServer.handleBackup(r); });
  server.on("/reboot", WebRequestMethodComposite(HTTP_POST) | HTTP_PUT, [](AsyncWebServerRequest *r) { webServer.handleReboot(r); });
  // JSON body endpoints
  server.addHandler(new AsyncCallbackJsonWebHandler("/shadeCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleShadeCommand(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/groupCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleGroupCommand(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/tiltCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleTiltCommand(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/repeatCommand",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleRepeatCommand(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/setPositions",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleSetPositions(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/setSensor",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleSetSensor(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/login",
    [](AsyncWebServerRequest *r, JsonVariant &j) { webServer.handleLogin(r, j); }));
  // GET fallback for command endpoints (query params)
  server.on("/shadeCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleShadeCommand(r, v); });
  server.on("/groupCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleGroupCommand(r, v); });
  server.on("/tiltCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleTiltCommand(r, v); });
  server.on("/repeatCommand", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleRepeatCommand(r, v); });
  server.on("/setPositions", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleSetPositions(r, v); });
  server.on("/setSensor", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleSetSensor(r, v); });
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *r) { JsonVariant v; webServer.handleLogin(r, v); });
  // OPTIONS preflight + not found
  server.onNotFound([](AsyncWebServerRequest *r) {
    if(r->method() == HTTP_OPTIONS) { r->send(200); return; }
    webServer.handleNotFound(r);
  });
}
