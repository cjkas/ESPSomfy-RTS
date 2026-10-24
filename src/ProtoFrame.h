#ifndef PROTO_FRAME_H
#define PROTO_FRAME_H
#include <stdint.h>
#include "JsonFormatter.h"
#include "ProtoSchema.h"

// The envelope of every socket protocol 2 frame, and the frames that hold
// nothing of the shade model (docs/PROTOCOL.md).  Kept free of the shade model
// and the web server so it can be unit tested on the PC (pio test -e native).

// A frame: {"v":2,"t":<type>,"id":<id>,"d":{ ... }}.  id 0 leaves it out.
void beginFrame(JsonFormatter &json, proto_type_t type, uint32_t id = 0);
void endFrame(JsonFormatter &json);
// A reply that says ok.
void writeOkReply(JsonFormatter &json, uint32_t id);
// A reply carrying an error.
void writeErrorReply(JsonFormatter &json, uint32_t id, proto_error_t code, const char *desc);
// hello: the firmware version and the schema hash.
void writeHello(JsonFormatter &json, const char *fw);
#endif
