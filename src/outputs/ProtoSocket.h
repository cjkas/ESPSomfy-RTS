#ifndef PROTO_SOCKET_H
#define PROTO_SOCKET_H
#include <ESPAsyncWebServer.h>
#include "ProtoSchema.h"
#include "ModelEditor.h"

class SomfyShade;
class SomfyGroup;
class SomfyRoom;

// The socket protocol 2 endpoint, ws://host/ws on port 80 (docs/PROTOCOL.md,
// docs/adr/0005-one-websocket-protocol.md).  It runs next to the old socket on
// port 8080 until the web UI and Home Assistant have moved over.
//
// async_tcp only reads what clients send and answers with fixed frames that
// hold nothing of the shade model (hello, errors, ok).  Everything formatted
// from the model goes out from loop(), in the socket event buffer.  The
// clients' slots and syncs are in ProtoSlots.
class ProtoSocket {
  public:
    // Adds /ws to the server.  From setup(), on loop()'s task.
    void begin(AsyncWebServer &server);
    // Moves each client's sync on.  loop() only.
    void loop();
    bool hasClients() const;
    // Events, loop() only.
    void shadeState(SomfyShade &shade);
    void shade(SomfyShade &shade);
    void shadeRemoved(uint8_t shadeId);
    void group(SomfyGroup &group);
    void groupState(SomfyGroup &group);
    void groupRemoved(uint8_t groupId);
    void room(SomfyRoom &room);
    void roomRemoved(uint8_t roomId);
    // roomOrder, shadeOrder or groupOrder: the list's ids, in sort order.
    void order(sort_list_t list);
    void repeaters();
    void shadeCommand(SomfyShade &shade, const char *command, const char *source, uint32_t sourceAddress);
    void memory();
    void wifi();
    void ethernet();
    // A feed event: write its fields into the writer between the two.  Only
    // when subscribed() says someone is, which is also only on loop().
    bool subscribed(proto_feed_t feed) const;
    ProtoWriter beginFeed(proto_type_t type);
    void endFeed(proto_feed_t feed);
    static void wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
  private:
    // On loop()'s task, with a client to send to: nothing is formatted for
    // nobody, or off the task that owns the event buffer.
    bool sending() const;
};
extern ProtoSocket protoSocket;
#endif
