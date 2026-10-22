#include <ESPAsyncWebServer.h>
#include "WResp.h"
#ifndef sockets_h
#define sockets_h

#define SOCK_MAX_ROOMS 1
#define ROOM_EMIT_FRAME 0
// The buffer every socket event is formatted into.  An event that doesn't
// fit is logged and not sent, so the largest one has to fit: see remoteFrame
// in Transceiver.cpp.
#define MAX_SOCK_RESPONSE 4096
// That buffer, which protocol 2 events and sync frames are formatted into too.
// loop() only, and only between an event's start and its send.
char *socketFrameBuffer();

struct room_t {
  uint8_t clients[5] = {255, 255, 255, 255, 255};
  uint8_t activeClients();
  bool isJoined(uint8_t num);
  bool join(uint8_t num);
  bool leave(uint8_t num);
  void clear();
};
class SocketEmitter {
  protected:
    uint8_t newclients = 0;
    uint8_t newClients[5] = {255,255,255,255,255};
    // loop()'s task, recorded by begin().  Only it may send; see emitAllowed().
    TaskHandle_t _owner = nullptr;
    // What beginEmit() hands any other task, so the event goes nowhere.
    JsonSockEvent _discard;
    bool onLoopTask() const;
    void delayInit(uint8_t num);
  public:
    JsonSockEvent json;
    room_t rooms[SOCK_MAX_ROOMS];
    uint8_t activeClients(uint8_t room);
    void initClients();
    void startup();
    void begin();
    void loop();
    void end();
    void disconnect();
    JsonSockEvent * beginEmit(const char *evt);
    void endEmit(uint8_t num = 255);
    void endEmitRoom(uint8_t num);
    static void wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
};
#endif
