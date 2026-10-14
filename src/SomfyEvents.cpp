#include <esp_task_wdt.h>
#include "Somfy.h"
#include "Sockets.h"
#include "MQTT.h"

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern MQTTClass mqtt;

void SomfyShade::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyShade::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId", this->shadeId);
  json->addElem("type", static_cast<uint8_t>(this->shadeType));
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("name", this->name);
  json->addElem("direction", this->direction);
  json->addElem("position", this->transformPosition(this->currentPos));
  json->addElem("target", this->transformPosition(this->target));
  json->addElem("myPos", this->transformPosition(this->myPos));
  json->addElem("tiltType", static_cast<uint8_t>(this->tiltType));
  json->addElem("flipCommands", this->flipCommands);
  json->addElem("flipPosition", this->flipPosition);
  json->addElem("flags", this->flags);
  json->addElem("sunSensor", this->hasSunSensor());
  json->addElem("light", this->hasLight());
  json->addElem("sortOrder", this->sortOrder);
  if(this->tiltType != tilt_types::none) {
    json->addElem("tiltDirection", this->tiltDirection);
    json->addElem("tiltTarget", this->transformPosition(this->tiltTarget));
    json->addElem("tiltPosition", this->transformPosition(this->currentTiltPos));
    json->addElem("myTiltPos", this->transformPosition(this->myTiltPos));
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
        flags |= shade->flags;
      }
    }
  }
  json->endArray();
  json->addElem("flags", flags);
  json->endObject();
  sockEmit.endEmit(num);
  this->publish();
}
void SomfyShadeController::emitState(uint8_t num) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    esp_task_wdt_reset();
    shade->emitState(num);
  }
}
