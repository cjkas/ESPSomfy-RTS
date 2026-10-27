#include "Utils.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "SomfySnapshot.h"
#include "MqttConnection.h"

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern MqttConnection mqtt;

void SomfyRoom::publish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.publish(topic, this->roomId, true);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.publish(topic, this->name, true);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.publish(topic, this->sortOrder, true);
  }
}
void SomfyRoom::unpublish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.unpublish(topic);
  }
}
// Publishes one topic of a shade: shades/<id>/<topic>.
static void publishShadeTopic(const char *topic, int32_t value, bool retain, void *ctx) {
  SomfyShade *shade = static_cast<SomfyShade *>(ctx);
  if(value < 0) shade->publish(topic, static_cast<int8_t>(value), retain);
  else shade->publish(topic, static_cast<uint32_t>(value), retain);
}
void SomfyShade::publishTopics(uint16_t changed) {
  if(!mqtt.connected()) return;
  shade_snapshot_t s;
  snapshotShade(*this, s);
  publishShadeTopics(changed, s, publishShadeTopic, this);
}
void SomfyShade::publishState() {
  if(!mqtt.connected()) return;
  shade_snapshot_t s;
  snapshotShade(*this, s);
  publishShadeTopics(shadeStateTopics(s), s, publishShadeTopic, this);
}
void SomfyShade::publishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  snprintf(topic, sizeof(topic), "%s/shades/%d", settings.MQTT.rootTopic, this->shadeId);
  obj["~"] = topic;
  JsonObject dobj = obj.createNestedObject("device");
  dobj["hw_version"] = settings.fwVersion.name;
  dobj["name"] = settings.hostname;
  dobj["mf"] = "rstrouse";
  JsonArray arrids = dobj.createNestedArray("identifiers");
  //snprintf(topic, sizeof(topic), "mqtt_espsomfyrts_%s_shade%d", settings.serverId, this->shadeId);
  snprintf(topic, sizeof(topic), "mqtt_espsomfyrts_%s", settings.serverId);
  arrids.add(topic);
  //snprintf(topic, sizeof(topic), "ESPSomfy-RTS_%s", settings.serverId);
  dobj["model"] = "ESPSomfy-RTS MQTT";
  snprintf(topic, sizeof(topic), "%s/status", settings.MQTT.rootTopic);
  obj["availability_topic"] = topic;
  obj["payload_available"] = "online";
  obj["payload_not_available"] = "offline";
  obj["name"] = this->name;
  snprintf(topic, sizeof(topic), "mqtt_%s_shade%d", settings.serverId, this->shadeId);
  obj["unique_id"] = topic;
  switch(this->shadeType) {
    case shade_types::blind:
      obj["device_class"] = "blind";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::lgate:
    case shade_types::cgate:
    case shade_types::rgate:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
    case shade_types::ldrapery:
    case shade_types::rdrapery:
    case shade_types::cdrapery:
      obj["device_class"] = "curtain";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::garage1:
    case shade_types::garage3:
      obj["device_class"] = "garage";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::awning:
      obj["device_class"] = "awning";
      obj["payload_close"] = this->flipPosition ? "1" : "-1";
      obj["payload_open"] = this->flipPosition ? "-1" : "1";
      obj["position_open"] = this->flipPosition ? 0 : 100;
      obj["position_closed"] = this->flipPosition ? 100 : 0;
      obj["state_closing"] = this->flipPosition ? "1" : "-1";
      obj["state_opening"] = this->flipPosition ? "-1" : "1";
      break;
    case shade_types::shutter:
      obj["device_class"] = "shutter";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
    case shade_types::drycontact2:
    case shade_types::drycontact:
      break;
    default:
      obj["device_class"] = "shade";
      obj["payload_close"] = this->flipPosition ? "-1" : "1";
      obj["payload_open"] = this->flipPosition ? "1" : "-1";
      obj["position_open"] = this->flipPosition ? 100 : 0;
      obj["position_closed"] = this->flipPosition ? 0 : 100;
      obj["state_closing"] = this->flipPosition ? "-1" : "1";
      obj["state_opening"] = this->flipPosition ? "1" : "-1";
      break;
  }
  if(this->shadeType != shade_types::drycontact && this->shadeType != shade_types::drycontact2) {
    if(this->tiltType != tilt_types::tiltonly) {
      obj["command_topic"] = "~/direction/set";
      obj["position_topic"] = "~/position";
      obj["set_position_topic"] = "~/target/set";
      obj["state_topic"] = "~/direction";
      obj["payload_stop"] = "0";
      obj["state_stopped"] = "0";
    }
    else {
      obj["payload_close"] = nullptr;
      obj["payload_open"] = nullptr;
      obj["payload_stop"] = nullptr;
    }
    
    if(this->tiltType != tilt_types::none) {
      obj["tilt_command_topic"] = "~/tiltTarget/set";
      obj["tilt_status_topic"] = "~/tiltPosition";
    }
    snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, this->shadeId);
  }
  else {
    obj["payload_on"] = 100;
    obj["payload_off"] = 0;
    obj["state_off"] = 0;
    obj["state_on"] = 100;
    obj["state_topic"] = "~/position";
    obj["command_topic"] = "~/target/set";
    snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, this->shadeId);
  }
  
  obj["enabled_by_default"] = true;
  mqtt.publishDisco(topic, obj, true);  
}
void SomfyShade::unpublishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  if(this->shadeType != shade_types::drycontact && this->shadeType != shade_types::drycontact2) {
    snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, this->shadeId);
  }
  else
    snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, this->shadeId);
  mqtt.unpublish(topic);
}
void SomfyShade::publish() {
  if(mqtt.connected()) {
    this->publish("shadeId", this->shadeId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("shadeType", static_cast<uint8_t>(this->shadeType), true);
    this->publish("tiltType", static_cast<uint8_t>(this->tiltType), true);
    this->publish("flags", this->getFlags(), true);
    this->publish("flipCommands", this->flipCommands, true);
    this->publish("flipPosition", this->flipPosition, true);
    this->publishState();
    this->publishDisco();
  }
}
void SomfyGroup::publishState() {
  if(mqtt.connected()) {
    this->publish("direction", this->direction, true);
    this->publish("lastRollingCode", this->lastRollingCode, true);
    this->publish("flipCommands", this->flipCommands, true);
    const uint8_t sunFlag = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
    const uint8_t isSunny = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny));
    const uint8_t isWindy = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy));
    this->publish("sunFlag", sunFlag);
    this->publish("sunny", isSunny);
    this->publish("windy", isWindy);    
  }  
}
void SomfyGroup::publish() {
  if(mqtt.connected()) {
    this->publish("groupId", this->groupId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("groupType", static_cast<uint8_t>(this->groupType), true);
    this->publish("flags", this->flags, true);
    this->publish("sunSensor", this->hasSunSensor(), true);
    this->publishState();
  }
}
char mqttTopicBuffer[55];
void SomfyGroup::unpublish() { SomfyGroup::unpublish(this->groupId); }
void SomfyShade::unpublish() { SomfyShade::unpublish(this->shadeId); }
void SomfyShade::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    SomfyShade::unpublish(id, "shadeId");
    SomfyShade::unpublish(id, "name");
    SomfyShade::unpublish(id, "remoteAddress");
    SomfyShade::unpublish(id, "shadeType");
    SomfyShade::unpublish(id, "tiltType");
    SomfyShade::unpublish(id, "flags");
    SomfyShade::unpublish(id, "flipCommands");
    SomfyShade::unpublish(id, "flipPosition");
    SomfyShade::unpublish(id, "position");
    SomfyShade::unpublish(id, "direction");
    SomfyShade::unpublish(id, "target");
    SomfyShade::unpublish(id, "lastRollingCode");
    SomfyShade::unpublish(id, "mypos");
    SomfyShade::unpublish(id, "myTiltPos");
    SomfyShade::unpublish(id, "tiltDirection");
    SomfyShade::unpublish(id, "tiltPosition");
    SomfyShade::unpublish(id, "tiltTarget");
    SomfyShade::unpublish(id, "windy");
    SomfyShade::unpublish(id, "sunny");
    if(settings.MQTT.pubDisco) {
      char topic[128] = "";
      snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, id);
      mqtt.unpublish(topic);
      snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, id);
      mqtt.unpublish(topic);
    }
  }
}
void SomfyGroup::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    SomfyGroup::unpublish(id, "groupId");
    SomfyGroup::unpublish(id, "name");
    SomfyGroup::unpublish(id, "remoteAddress");
    SomfyGroup::unpublish(id, "groupType");
    SomfyGroup::unpublish(id, "direction");
    SomfyGroup::unpublish(id, "lastRollingCode");
    SomfyGroup::unpublish(id, "flags");
    SomfyGroup::unpublish(id, "SunSensor");
    SomfyGroup::unpublish(id, "flipCommands");
  }
}
void SomfyGroup::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}
void SomfyShade::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}
bool SomfyShade::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, const char *val, bool retain) { 
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyShade::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", this->shadeId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
bool SomfyGroup::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
void SomfyShadeController::publish() {
  this->updateGroupFlags();
  char arrIds[128] = "[";
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(strlen(arrIds) > 1) strcat(arrIds, ",");
    itoa(shade->getShadeId(), &arrIds[strlen(arrIds)], 10);
    shade->publish();
  }
  strcat(arrIds, "]");
  mqtt.publish("shades", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = this->getShadeById(i);
    if(shade) continue;
    else {
      SomfyShade::unpublish(i);
    }
  }
  strcpy(arrIds, "[");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(strlen(arrIds) > 1) strcat(arrIds, ",");
    itoa(group->getGroupId(), &arrIds[strlen(arrIds)], 10);
    group->publish();
  }
  strcat(arrIds, "]");
  mqtt.publish("groups", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = this->getGroupById(i);
    if(group) continue;
    else SomfyGroup::unpublish(i);
  }
}
