#include "TaskWdt.h"
#include "Somfy.h"
#include "SomfyEditor.h"
#include "SocketEmitter.h"
#include "ProtoSocket.h"
#include "MqttConnection.h"

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern MqttConnection mqtt;

void SomfyShade::emitState() { this->changed |= SHADE_CHANGED_STATE; }
void SomfyShade::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyShade::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId", this->shadeId);
  json->addElem("type", static_cast<uint8_t>(this->shadeType));
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("name", this->name);
  json->addElem("direction", this->state().direction);
  json->addElem("position", this->transformPosition(this->state().currentPos));
  json->addElem("target", this->transformPosition(this->state().target));
  json->addElem("myPos", this->transformPosition(this->state().myPos));
  json->addElem("tiltType", static_cast<uint8_t>(this->tiltType));
  json->addElem("flipCommands", this->flipCommands);
  json->addElem("flipPosition", this->flipPosition);
  json->addElem("flags", this->getFlags());
  json->addElem("sunSensor", this->sunSensor);
  json->addElem("light", this->light);
  json->addElem("sortOrder", this->sortOrder);
  if(this->tiltType != tilt_types::none) {
    json->addElem("tiltDirection", this->state().tiltDirection);
    json->addElem("tiltTarget", this->transformPosition(this->state().tiltTarget));
    json->addElem("tiltPosition", this->transformPosition(this->state().currentTiltPos));
    json->addElem("myTiltPos", this->transformPosition(this->state().myTiltPos));
  }
  json->endObject();
  sockEmit.endEmit(num);
}
void SomfyShade::emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt) { this->emitCommand(255, cmd, source, sourceAddress, evt); }
void SomfyShade::emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId", this->shadeId);
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("cmd", translateSomfyCommand(cmd).c_str());
  json->addElem("source", source);
  json->addElem("rcode", (uint32_t)this->lastRollingCode);
  json->addElem("sourceAddress", (uint32_t)sourceAddress);
  json->endObject();
  sockEmit.endEmit(num);
  if(num == 255) protoSocket.shadeCommanded(*this, translateSomfyCommand(cmd).c_str(), source, sourceAddress);
  if(mqtt.connected()) {
    this->publish("cmdSource", source);
    this->publish("cmdAddress", sourceAddress);
    this->publish("cmd", translateSomfyCommand(cmd).c_str());
  }
}
void SomfyRoom::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyRoom::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("roomId", this->roomId);
  json->addElem("name", this->name);
  json->addElem("sortOrder", this->sortOrder);
  json->endObject();
  sockEmit.endEmit(num);
  this->publish();
}
void SomfyGroup::emitState() { this->changed |= GROUP_CHANGED_STATE; }
void SomfyGroup::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyGroup::emitState(uint8_t num, const char *evt) {
  uint8_t flags = 0;
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("groupId", this->groupId);
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("name", this->name);
  json->addElem("sunSensor", this->hasSunSensor());
  json->beginArray("shades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 255 && this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) {
        json->addElem(this->linkedShades[i]);
        flags |= shade->getFlags();
      }
    }
  }
  json->endArray();
  json->addElem("flags", flags);
  json->endObject();
  sockEmit.endEmit(num);
  this->publish();
}
// To every client: the sender picks out its own by requestId.
void emitRequestResult(uint32_t requestId, const edit_result_t &result) {
  JsonSockEvent *json = sockEmit.beginEmit("requestResult");
  json->beginObject();
  json->addElem("requestId", requestId);
  json->addElem("ok", result.ok);
  json->addElem("id", result.id);
  json->addElem("code", result.code);
  json->addElem("desc", result.desc);
  json->endObject();
  sockEmit.endEmit();
}
// The change fan-out.  Everything that changed about a shade or group in this
// loop() pass goes out once: its MQTT topics, then one state event.
void SomfyShadeController::sendChanges() {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    const uint16_t changed = shade.changed;
    shade.changed = 0;
    if(!changed || shade.getShadeId() == 255) continue;
    if(changed & SHADE_CHANGED_TOPICS) shade.publishTopics(changed);
    shade.emitState(255, "shadeState");
    if(changed & SHADE_CHANGED_CONFIG) protoSocket.shade(shade);
    else protoSocket.shadeState(shade);
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    const uint8_t changed = group.changed;
    group.changed = 0;
    if(!changed || group.getGroupId() == 255) continue;
    if(changed & GROUP_CHANGED_DIRECTION) group.publish("direction", group.direction);
    group.emitState(255, "groupState");
    if(changed & GROUP_CHANGED_CONFIG) protoSocket.group(group);
    else protoSocket.groupState(group);
  }
}
void SomfyShadeController::emitState(uint8_t num) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    resetTaskWdt();
    shade->emitState(num);
  }
}
