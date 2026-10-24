#ifndef SOMFY_EDITOR_H
#define SOMFY_EDITOR_H
#include <ArduinoJson.h>
#include "ModelEditor.h"

// ModelEditor on the device: an edit of the shade model handed from the web
// server task to loop(), which applies it with the ModelEditor rules and tells
// the clients how it went.

// One edit on its way to loop(): the edit route and a copy of its request.
// From an edit route its result goes to every client as requestResult; from a
// protocol 2 edit op, as the reply to replyId for the socket client
// replyClient.
struct model_edit_t {
  model_edit_type_t type = model_edit_type_t::add_room;
  uint32_t requestId = 0;
  uint32_t replyClient = 0;
  uint32_t replyId = 0;
  JsonDocument doc;
};

// One edit at a time: false when one is already waiting or running.  Any task.
bool beginModelEdit();
// The id the HTTP reply and the requestResult event carry.  Any task.
uint32_t nextEditRequestId();
// Gives up an edit that was begun but never queued.  Any task.
void cancelModelEdit(model_edit_t *edit);
// Applies the edit, saves it, tells how it went and frees it.  loop() only.
void runModelEdit(model_edit_t *edit);
// Applies the edit to the shade model and saves it.  loop() only.
edit_result_t applyModelEdit(model_edit_t &edit);
// Tells every client how the edit went (SomfyEvents.cpp).
void emitRequestResult(uint32_t requestId, const edit_result_t &result);
#endif
