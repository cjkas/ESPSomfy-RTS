#include "esp_log.h"
#include "ConfigSettings.h"
#include "Somfy.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;
extern ConfigSettings settings;

int8_t SomfyShade::validateJSON(JsonObject &obj) {
  int8_t ret = 0;
  shade_types type = this->shadeType;
  if(!obj["shadeType"].isNull()) {
    if(obj["shadeType"].is<const char *>()) {
      if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
        type = shade_types::roller;
      else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
        type = shade_types::ldrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
        type = shade_types::rdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
        type = shade_types::cdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
        type = shade_types::garage1;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
        type = shade_types::garage3;
      else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
        type = shade_types::blind;
      else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
        type = shade_types::awning;
      else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
        type = shade_types::shutter;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
        type = shade_types::drycontact2;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
        type = shade_types::drycontact;
    }
    else {
      this->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
    }
  }
  if(!obj["proto"].isNull()) {
    radio_proto proto = this->proto;
    if(proto == radio_proto::GP_Relay || proto == radio_proto::GP_Remote) {
      // Check to see if we are using the up and or down
      // GPIOs anywhere else.
      uint8_t upPin = !obj["gpioUp"].isNull() ? obj["gpioUp"].as<uint8_t>() : this->gpioUp;
      uint8_t downPin = !obj["gpioDown"].isNull() ? obj["gpioDown"].as<uint8_t>() : this->gpioDown;
      uint8_t myPin = !obj["gpioMy"].isNull() ? obj["gpioMy"].as<uint8_t>() : this->gpioMy;
      if(type == shade_types::drycontact || 
        ((type == shade_types::garage1 || type == shade_types::lgate1 || type == shade_types::cgate1 || type == shade_types::rgate1) 
        && proto == radio_proto::GP_Remote)) upPin = myPin = 255;
      else if(type == shade_types::drycontact2) myPin = 255;
      if(proto == radio_proto::GP_Relay) myPin = 255;
      if(somfy.transceiver.config.enabled) {
        if((upPin != 255 && somfy.transceiver.usesPin(upPin)) ||
          (downPin != 255 && somfy.transceiver.usesPin(downPin)) ||
          (myPin != 255 && somfy.transceiver.usesPin(myPin)))
          ret = -10;
      }
#ifndef CONFIG_IDF_TARGET_ESP32C6
      if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref) {
        if((upPin != 255 && settings.Ethernet.usesPin(upPin)) ||
          (downPin != 255 && somfy.transceiver.usesPin(downPin)) ||
          (myPin != 255 && somfy.transceiver.usesPin(myPin)))
          ret = -11;
      }
#endif
      if(ret == 0) {
        for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
          SomfyShade *shade = &somfy.shades[i];
          if(shade->getShadeId() == this->getShadeId() || shade->getShadeId() == 255) continue;
          if((upPin != 255 && shade->usesPin(upPin)) ||
            (downPin != 255 && shade->usesPin(downPin)) ||
            (myPin != 255 && shade->usesPin(myPin))){
            ret = -12;
            break;
          }
        }
      }
    }
  }
  return ret;
}
int8_t SomfyShade::fromJSON(JsonObject &obj) {
  int8_t err = this->validateJSON(obj);
  if(err == 0) {
    if(!obj["name"].isNull()) strlcpy(this->name, obj["name"], sizeof(this->name));
    if(!obj["roomId"].isNull()) this->roomId = obj["roomId"];
    if(!obj["upTime"].isNull()) this->upTime = obj["upTime"];
    if(!obj["downTime"].isNull()) this->downTime = obj["downTime"];
    if(!obj["remoteAddress"].isNull()) this->setRemoteAddress(obj["remoteAddress"]);
    if(!obj["tiltTime"].isNull()) this->tiltTime = obj["tiltTime"];
    if(!obj["stepSize"].isNull()) this->stepSize = obj["stepSize"];
    if(!obj["hasTilt"].isNull()) this->tiltType = static_cast<bool>(obj["hasTilt"]) ? tilt_types::none : tilt_types::tiltmotor;
    if(!obj["bitLength"].isNull()) this->bitLength = obj["bitLength"];
    if(!obj["proto"].isNull()) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(!obj["sunSensor"].isNull()) this->setSunSensor(obj["sunSensor"]);
    if(!obj["simMy"].isNull()) this->setSimMy(obj["simMy"]);
    if(!obj["light"].isNull()) this->setLight(obj["light"]);
    if(!obj["gpioFlags"].isNull()) this->gpioFlags = obj["gpioFlags"];
    if(!obj["gpioLLTrigger"].isNull()) {
      if(obj["gpioLLTrigger"].as<bool>())
        this->gpioFlags |= (uint8_t)gpio_flags_t::LowLevelTrigger;
      else
        this->gpioFlags &= ~(uint8_t)gpio_flags_t::LowLevelTrigger;
    }
    
    if(!obj["shadeType"].isNull()) {
      if(obj["shadeType"].is<const char *>()) {
        if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
          this->shadeType = shade_types::roller;
        else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
          this->shadeType = shade_types::ldrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
          this->shadeType = shade_types::rdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
          this->shadeType = shade_types::cdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
          this->shadeType = shade_types::garage1;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
          this->shadeType = shade_types::garage3;
        else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
          this->shadeType = shade_types::blind;
        else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
          this->shadeType = shade_types::awning;
        else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
          this->shadeType = shade_types::shutter;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
          this->shadeType = shade_types::drycontact2;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
          this->shadeType = shade_types::drycontact;
      }
      else {
        this->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
      }
    }
    if(!obj["flipCommands"].isNull()) this->flipCommands = obj["flipCommands"].as<bool>();
    if(!obj["flipPosition"].isNull()) this->flipPosition = obj["flipPosition"].as<bool>();
    if(!obj["repeats"].isNull()) this->repeats = obj["repeats"];
    if(!obj["tiltType"].isNull()) {
      if(obj["tiltType"].is<const char *>()) {
        if(strncmp(obj["tiltType"].as<const char *>(), "none", 4) == 0)
          this->tiltType = tilt_types::none;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltmotor", 9) == 0)
          this->tiltType = tilt_types::tiltmotor;
        else if(strncmp(obj["tiltType"].as<const char *>(), "integ", 5) == 0)
          this->tiltType = tilt_types::integrated;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltonly", 8) == 0)
          this->tiltType = tilt_types::tiltonly;
      }
      else {
        this->tiltType = static_cast<tilt_types>(obj["tiltType"].as<uint8_t>());
      }
    }
    if(!obj["linkedAddresses"].isNull()) {
      uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
      memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
      JsonArray arr = obj["linkedAddresses"];
      uint8_t i = 0;
      for(uint32_t addr : arr) {
        linkedAddresses[i++] = addr;
      }
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        this->linkedRemotes[j].setRemoteAddress(linkedAddresses[j]);
      }
    }
    if(!obj["flags"].isNull()) this->flags = obj["flags"];
    if(this->proto == radio_proto::GP_Remote || this->proto == radio_proto::GP_Relay) {
      if(!obj["gpioUp"].isNull()) this->gpioUp = obj["gpioUp"];
      if(!obj["gpioDown"].isNull()) this->gpioDown = obj["gpioDown"];
      pinMode(this->gpioUp, OUTPUT);
      pinMode(this->gpioDown, OUTPUT);
    }
    if(this->proto == radio_proto::GP_Remote) {
      if(!obj["gpioMy"].isNull()) this->gpioMy = obj["gpioMy"];
      pinMode(this->gpioMy, OUTPUT);
    }
  }
  return err;
}
bool SomfyRoom::fromJSON(JsonObject &obj) {
  if(!obj["name"].isNull()) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(!obj["sortOrder"].isNull()) this->sortOrder = obj["sortOrder"];
  return true;
}
bool SomfyGroup::fromJSON(JsonObject &obj) {
  if(!obj["name"].isNull()) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(!obj["roomId"].isNull()) this->roomId = obj["roomId"];
  if(!obj["remoteAddress"].isNull()) this->setRemoteAddress(obj["remoteAddress"]);
  if(!obj["bitLength"].isNull()) this->bitLength = obj["bitLength"];
  if(!obj["proto"].isNull()) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
  if(!obj["flipCommands"].isNull()) this->flipCommands = obj["flipCommands"].as<bool>();
  
  //if(!obj["sunSensor"].isNull()) this->hasSunSensor() = obj["sunSensor"];  This is calculated
  if(!obj["repeats"].isNull()) this->repeats = obj["repeats"];
  if(!obj["linkedShades"].isNull()) {
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    memset(linkedShades, 0x00, sizeof(linkedShades));
    JsonArray arr = obj["linkedShades"];
    uint8_t i = 0;
    for(uint8_t shadeId : arr) {
      linkedShades[i++] = shadeId;
    }
  }
  return true;
}
bool Transceiver::fromJSON(JsonObject& obj) {
    if (!obj["config"].isNull()) {
      JsonObject objConfig = obj["config"];
      // Parse into a copy so the radio task never sees a half updated config.
      transceiver_config_t cfg = this->config;
      cfg.fromJSON(objConfig);
      this->setConfig(cfg);
    }
    return true;
}
void transceiver_config_t::fromJSON(JsonObject& obj) {
    //Serial.print("Deserialize Radio JSON ");
    if(!obj["type"].isNull()) this->type = obj["type"];
    if(!obj["CSNPin"].isNull()) this->CSNPin = obj["CSNPin"];
    if(!obj["MISOPin"].isNull()) this->MISOPin = obj["MISOPin"];
    if(!obj["MOSIPin"].isNull()) this->MOSIPin = obj["MOSIPin"];
    if(!obj["RXPin"].isNull()) this->RXPin = obj["RXPin"];
    if(!obj["SCKPin"].isNull()) this->SCKPin = obj["SCKPin"];
    if(!obj["TXPin"].isNull()) this->TXPin = obj["TXPin"];
    if(!obj["rxBandwidth"].isNull()) this->rxBandwidth = obj["rxBandwidth"]; // float
    if(!obj["frequency"].isNull()) this->frequency = obj["frequency"];  // float
    if(!obj["deviation"].isNull()) this->deviation = obj["deviation"];  // float
    if(!obj["enabled"].isNull()) this->enabled = obj["enabled"];
    if(!obj["txPower"].isNull()) this->txPower = obj["txPower"];
    if(!obj["proto"].isNull()) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(!obj["noiseDetection"].isNull()) this->noiseDetection = obj["noiseDetection"];
    ESP_LOGD(TAG, "SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}
