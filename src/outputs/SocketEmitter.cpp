#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "SocketEmitter.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "ESPNetwork.h"
#include "GitOTA.h"
#include "Timing.h"

static const char *TAG = "Sockets";

extern ConfigSettings settings;
extern ESPNetwork net;
extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern GitUpdater git;

AsyncWebServer wsServer(8080);
AsyncWebSocket ws("/");

#define MAX_WS_CLIENTS 5
struct ws_slot_t {
  uint32_t asyncId;      // 0 when the slot is free.
  // When the client connected and last pinged, so the logs show how long it
  // stayed and how far apart its pings arrived.
  uint32_t connectedAt;
  uint32_t lastPing;
};
static ws_slot_t slots[MAX_WS_CLIENTS] = {};
static uint8_t mapClientId(uint32_t asyncId) {
  for(uint8_t i = 0; i < MAX_WS_CLIENTS; i++)
    if(slots[i].asyncId == asyncId) return i;
  return 255;
}
static uint32_t getAsyncId(uint8_t slot) {
  if(slot < MAX_WS_CLIENTS) return slots[slot].asyncId;
  return 0;
}
static uint8_t addClient(uint32_t asyncId) {
  for(uint8_t i = 0; i < MAX_WS_CLIENTS; i++) {
    if(slots[i].asyncId == 0) { slots[i].asyncId = asyncId; return i; }
  }
  return 255;
}
static void removeClient(uint32_t asyncId) {
  for(uint8_t i = 0; i < MAX_WS_CLIENTS; i++)
    if(slots[i].asyncId == asyncId) slots[i].asyncId = 0;
}

#define MAX_SOCK_RESPONSE 2048
static char g_response[MAX_SOCK_RESPONSE];

bool room_t::isJoined(uint8_t num) {
  for(uint8_t i = 0; i < sizeof(this->clients); i++) {
    if(this->clients[i] == num) return true;
  }
  return false;
}
bool room_t::join(uint8_t num) {
  if(this->isJoined(num)) return true;
  for(uint8_t i = 0; i < sizeof(this->clients); i++) {
    if(this->clients[i] == 255) {
      this->clients[i] = num;
      return true;
    }
  }
  return false;
}
bool room_t::leave(uint8_t num) {
  if(!this->isJoined(num)) return false;
  for(uint8_t i = 0; i < sizeof(this->clients); i++) {
    if(this->clients[i] == num) this->clients[i] = 255;
  }
  return true;
}
void room_t::clear() {
  memset(this->clients, 255, sizeof(this->clients));
}
uint8_t room_t::activeClients() {
  uint8_t n = 0;
  for(uint8_t i = 0; i < sizeof(this->clients); i++) {
    if(this->clients[i] != 255) n++;
  }
  return n;
}

/*********************************************************************
 * SocketEmitter class members
 ********************************************************************/
void SocketEmitter::startup() {

}
void SocketEmitter::begin() {
  ws.onEvent(SocketEmitter::wsEvent);
  wsServer.addHandler(&ws);
  wsServer.begin();
  ESP_LOGI(TAG, "Socket Server Started...");
}
void SocketEmitter::loop() {
  ws.cleanupClients();
  this->initClients();
}
JsonSockEvent *SocketEmitter::beginEmit(const char *evt) {
  this->json.beginEvent(&ws, evt, g_response, sizeof(g_response));
  return &this->json;
}
void SocketEmitter::endEmit(uint8_t num) {
  if(num == 255) {
    this->json.endEvent(0);
  } else {
    uint32_t asyncId = getAsyncId(num);
    this->json.endEvent(asyncId);
  }
  esp_task_wdt_reset();
}
void SocketEmitter::endEmitRoom(uint8_t room) {
  if(room < SOCK_MAX_ROOMS) {
    room_t *r = &this->rooms[room];
    for(uint8_t i = 0; i < sizeof(r->clients); i++) {
      if(r->clients[i] != 255) {
        uint32_t asyncId = getAsyncId(r->clients[i]);
        if(asyncId != 0) this->json.endEvent(asyncId);
      }
    }
  }
}
uint8_t SocketEmitter::activeClients(uint8_t room) {
  if(room < SOCK_MAX_ROOMS) return this->rooms[room].activeClients();
  return 0;
}
void SocketEmitter::initClients() {
  for(uint8_t i = 0; i < sizeof(this->newClients); i++) {
    uint8_t slot = this->newClients[i];
    if(slot != 255) {
      uint32_t asyncId = getAsyncId(slot);
      if(asyncId != 0 && ws.hasClient(asyncId)) {
        ESP_LOGD(TAG, "Initializing Socket Client %u (asyncId=%lu)", slot, asyncId);
        esp_task_wdt_reset();
        ElapsedTimer total, step;
        settings.emitSockets(slot);
        logElapsed(TAG, "Socket init settings", step.lap());
        if(!ws.hasClient(asyncId)) { this->newClients[i] = 255; continue; }
        somfy.emitState(slot);
        logElapsed(TAG, "Socket init somfy state", step.lap());
        if(!ws.hasClient(asyncId)) { this->newClients[i] = 255; continue; }
        git.emitUpdateCheck(slot);
        logElapsed(TAG, "Socket init update check", step.lap());
        if(!ws.hasClient(asyncId)) { this->newClients[i] = 255; continue; }
        net.emitSockets(slot);
        logElapsed(TAG, "Socket init network", step.lap());
        logElapsed(TAG, "Socket init total", total.us());
        esp_task_wdt_reset();
      }
      this->newClients[i] = 255;
    }
  }
}
void SocketEmitter::delayInit(uint8_t num) {
  for(uint8_t i=0; i < sizeof(this->newClients); i++) {
    if(this->newClients[i] == num) break;
    else if(this->newClients[i] == 255) {
      this->newClients[i] = num;
      break;
    }
  }
}
void SocketEmitter::end() {
  ws.closeAll();
  wsServer.end();
  for(uint8_t i = 0; i < SOCK_MAX_ROOMS; i++)
    this->rooms[i].clear();
  memset(slots, 0, sizeof(slots));
}
void SocketEmitter::disconnect() {
  ws.closeAll();
  memset(slots, 0, sizeof(slots));
}
void SocketEmitter::wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  uint32_t asyncId = client->id();
  switch(type) {
    case WS_EVT_CONNECT:
      {
        uint8_t slot = addClient(asyncId);
        if(slot == 255) {
          ESP_LOGE(TAG, "Socket: No free client slots, closing %lu", asyncId);
          client->close();
          return;
        }
        IPAddress ip = client->remoteIP();
        ESP_LOGI(TAG, "Socket [%lu] Connected from %d.%d.%d.%d (slot %u)", asyncId, ip[0], ip[1], ip[2], ip[3], slot);
        slots[slot].connectedAt = millis();
        slots[slot].lastPing = 0;
        client->text("Connected");
        client->setCloseClientOnQueueFull(false);
        sockEmit.delayInit(slot);
      }
      break;
    case WS_EVT_DISCONNECT:
      {
        uint8_t slot = mapClientId(asyncId);
        if(slot != 255) {
          ESP_LOGI(TAG, "Socket [%lu] Disconnected (slot %u) after %lums, last ping %lums ago", asyncId, slot,
            (unsigned long)(millis() - slots[slot].connectedAt), slots[slot].lastPing ? (unsigned long)(millis() - slots[slot].lastPing) : 0UL);
          for(uint8_t i = 0; i < SOCK_MAX_ROOMS; i++)
            sockEmit.rooms[i].leave(slot);
        }
        removeClient(asyncId);
      }
      break;
    case WS_EVT_DATA:
      {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
          uint8_t slot = mapClientId(asyncId);
          data[len] = 0;
          if(strncmp((char*)data, "join:", 5) == 0) {
            uint8_t roomNum = atoi((char*)&data[5]);
            ESP_LOGD(TAG, "Client %u joining room %u", slot, roomNum);
            if(roomNum < SOCK_MAX_ROOMS && slot != 255) sockEmit.rooms[roomNum].join(slot);
          }
          else if(strncmp((char*)data, "leave:", 6) == 0) {
            uint8_t roomNum = atoi((char*)&data[6]);
            ESP_LOGD(TAG, "Client %u leaving room %u", slot, roomNum);
            if(roomNum < SOCK_MAX_ROOMS && slot != 255) sockEmit.rooms[roomNum].leave(slot);
          }
          else {
            ESP_LOGD(TAG, "Socket [%lu] text: %s", asyncId, data);
          }
        }
      }
      break;
    case WS_EVT_ERROR:
      ESP_LOGE(TAG, "Socket [%lu] Error", asyncId);
      break;
    case WS_EVT_PING:
      {
        // Home Assistant only pings after 20s without a message from us, so
        // the gap depends on our own traffic and says nothing about the link.
        // It is kept for the disconnect line.
        uint8_t slot = mapClientId(asyncId);
        if(slot != 255) {
          uint32_t now = millis();
          if(slots[slot].lastPing) ESP_LOGD(TAG, "Socket [%lu] ping, %lums since the last", asyncId, (unsigned long)(now - slots[slot].lastPing));
          slots[slot].lastPing = now;
        }
      }
      break;
    case WS_EVT_PONG:
      break;
  }
}
