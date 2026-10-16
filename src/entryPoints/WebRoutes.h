#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H
#include <ESPAsyncWebServer.h>
#include "Somfy.h"
#include "LoopQueue.h"
#include "ModelSnapshot.h"
#include "GitOTA.h"

// What WebUiRoutes, ApiRoutes and the handlers both of them call (Web.cpp)
// share.  Internal to the web servers.

// Registers the routes the config web UI calls on port 80, and its files.
void registerWebUiRoutes(AsyncWebServer &server);
// Registers the routes integrations such as Home Assistant call on port 8081.
void registerApiRoutes(AsyncWebServer &server);

#define WEB_MAX_RESPONSE 4096
// Where replies are formatted before they are sent.  async_tcp only.
extern char g_async_content[WEB_MAX_RESPONSE];
extern const char _encoding_text[];
extern const char _encoding_json[];

// Sent when a request can't be handed to loop().
void sendQueueFull(AsyncWebServerRequest *request);
// Sent when the radio can't take a frame (suspended or queue full).
void sendRadioUnavailable(AsyncWebServerRequest *request);
// Hands an item to loop(), which owns the shades.  When it can't be the
// request is answered with the reason and false is returned.
bool queueLoopItem(AsyncWebServerRequest *request, const loop_item_t &item);
bool queueShadeRequest(AsyncWebServerRequest *request, const shade_request_t &req);
// A query parameter, or an empty string when it is missing.
String asyncParam(AsyncWebServerRequest *request, const char *name);
bool asyncHasParam(AsyncWebServerRequest *request, const char *name);
// Serves a read from a ModelSnapshot that loop() fills.
void sendSnapshot(AsyncWebServerRequest *request, snapshot_doc_t doc, uint8_t id = 0, snapshot_header_t header = nullptr);

// One item of a reply, copied from the live shade model.
void serializeShadeRef(SomfyShade *shade, JsonFormatter &json);
void serializeShade(SomfyShade *shade, JsonFormatter &json);
void serializeShade(SomfyShade *shade, JsonFormatter &json, bool paired, uint16_t lastRollingCode);
void serializeTransceiverConfig(JsonFormatter &json);
void serializeAppVersion(JsonFormatter &json, appver_t &ver);
void serializeGitVersion(JsonFormatter &json);
void serializeGitRelease(GitRelease *rel, JsonFormatter &json);
#endif
