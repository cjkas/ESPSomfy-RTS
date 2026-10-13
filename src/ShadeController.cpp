#include <Preferences.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "ConfigFile.h"
#include "GitOTA.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern GitUpdater git;

void SomfyShadeController::end() { this->transceiver.disableReceive(); }
SomfyShadeController::SomfyShadeController() {
  memset(this->m_shadeIds, 255, sizeof(this->m_shadeIds));
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0x0FFFFF;
}
SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getRemoteAddress() == address) return &shade;
    else {
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        if(shade.linkedRemotes[j].getRemoteAddress() == address) return &shade;
      }
    }
  }
  return nullptr;
}
SomfyGroup *SomfyShadeController::findGroupByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getRemoteAddress() == address) return &group;
  }
  return nullptr;
}
void SomfyShadeController::updateGroupFlags() {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group && group->getGroupId() != 255) {
      uint8_t flags = group->flags;
      group->updateFlags();
      if(flags != group->flags)
        group->emitState();
    }
  }
}
bool SomfyShadeController::begin() {
  Preferences pref;
  // Load up all the configuration data.
  //ShadeConfigFile::getAppVersion(this->appVersion);
  ESP_LOGI(TAG, "App Version:%u.%u.%u", settings.appVersion.major, settings.appVersion.minor, settings.appVersion.build);
  if(ShadeConfigFile::exists()) {
    ESP_LOGI(TAG, "shades.cfg exists so we are using that");
    ShadeConfigFile::load(this);
  }
  else {
    ESP_LOGI(TAG, "Starting clean");
  }
  this->transceiver.begin();

  // Set the radio type for shades that have yet to be specified.
  bool saveFlag = false;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() != 255 && shade->bitLength == 0) {
      //Serial.printf("Setting bit length to %d\n", this->transceiver.config.type);
      shade->bitLength = this->transceiver.config.type;
      saveFlag = true;
    }
  }
  if(saveFlag) somfy.commit();
  return true;
}
void SomfyShadeController::commit() {
  if(git.lockFS) return;
  esp_task_wdt_reset(); // Make sure we don't reset inadvertently.
  ShadeConfigFile file;
  file.begin();
  file.save(this);
  file.end();
  this->isDirty = false;
  this->lastCommit = millis();
}
void SomfyShadeController::writeBackup() {
  if(git.lockFS) return;
  esp_task_wdt_reset(); // Make sure we don't reset inadvertently.
  this->backupData = "";
  this->backupData.reserve(16384);
  ShadeConfigFile file;
  file.beginRAM(&this->backupData);
  file.backup(this);
  file.end();
}
SomfyRoom * SomfyShadeController::getRoomById(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) return &this->rooms[i];
  }
  return nullptr;
}
SomfyShade * SomfyShadeController::getShadeById(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}
SomfyGroup * SomfyShadeController::getGroupById(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) return &this->groups[i];
  }
  return nullptr;
}
void SomfyRoom::clear() {
  this->roomId = 0;
  strcpy(this->name, "");
}
void SomfyShadeController::compressRepeaters() {
  for(uint8_t i = 0, j = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) {
      if(i != j) {
        this->repeaters[j] = this->repeaters[i];
        this->repeaters[i] = 0;
      }
      j++;
    }
  }
}
bool SomfyRoom::save() { somfy.commit(); return true; }
void SomfyShadeController::processFrame(somfy_frame_t &frame, bool internal) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal);
  }
}
void SomfyShadeController::markTxPending(uint32_t remoteAddress, uint32_t ticket) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() != 255 && shade->findRemote(remoteAddress)) shade->markTxPending(ticket);
  }
  // Group commands move their member shades through processInternalCommand.
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255 || group->getRemoteAddress() != remoteAddress) continue;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
      if(group->linkedShades[j] == 0) continue;
      SomfyShade *shade = this->getShadeById(group->linkedShades[j]);
      if(shade) shade->markTxPending(ticket);
    }
  }
}
void SomfyShadeController::processTxEvent(const somfy_tx_event_t &evt) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processTxEvent(evt);
}
void SomfyShadeController::processWaitingFrame() {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processWaitingFrame();
}
uint8_t SomfyShadeController::getNextShadeId() {
  // There is no shortcut for this since the deletion of
  // a shade in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_SHADES - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
      SomfyShade *shade = &this->shades[j];
      if(shade->getShadeId() == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      ESP_LOGD(TAG, "Got next Shade Id:%d", i);
      return i;
    }
  }
  return 255;
}
int8_t SomfyShadeController::getMaxShadeOrder() {
  int8_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(order < shade->sortOrder) order = shade->sortOrder;
  }
  return order;
}
int8_t SomfyShadeController::getMaxGroupOrder() {
  int8_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(order < group->sortOrder) order = group->sortOrder;
  }
  return order;
}
uint8_t SomfyShadeController::getNextGroupId() {
  // There is no shortcut for this since the deletion of
  // a group in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_GROUPS - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
      SomfyGroup *group = &this->groups[j];
      if(group->getGroupId() == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      ESP_LOGD(TAG, "Got next Group Id:%d", i);
      return i;
    }
  }
  return 255;
}
uint8_t SomfyShadeController::getNextRoomId() {
  // There is no shortcut for this since the deletion of
  // a room in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_ROOMS - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_ROOMS; j++) {
      SomfyRoom *room = &this->rooms[j];
      if(room->roomId == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      ESP_LOGD(TAG, "Got next room Id:%d", i);
      return i;
    }
  }
  return 0;
}
int8_t SomfyShadeController::getMaxRoomOrder() {
  int8_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &this->rooms[i];
    if(room->roomId == 0) continue;
    if(order < room->sortOrder) order = room->sortOrder;
  }
  return order;
}
uint8_t SomfyShadeController::repeaterCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) count++;
  }
  return count;
}
uint8_t SomfyShadeController::roomCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId != 0) count++;
  }
  return count;
}
uint8_t SomfyShadeController::shadeCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) count++;
  }
  return count;
}
uint8_t SomfyShadeController::groupCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255) count++;
  }
  return count;
}
uint32_t SomfyShadeController::getNextRemoteAddress(uint8_t id) {
  uint32_t address = this->startingAddress + id;
  uint8_t i = 0;
  // The assumption here is that the max number of groups will
  // always be less than or equal to the max number of shades.
  while(i < SOMFY_MAX_SHADES) {
    if((i < SOMFY_MAX_SHADES && this->shades[i].getShadeId() != 255 && this->shades[i].getRemoteAddress() == address) ||
      (i < SOMFY_MAX_GROUPS && this->groups[i].getGroupId() != 255 && this->groups[i].getRemoteAddress() == address)) {
      address++;
      i = 0; // Start over we cannot share addresses.
    }
    else i++;
  }
  i = 0;
  return address;
}
SomfyShade *SomfyShadeController::addShade(JsonObject &obj) {
  SomfyShade *shade = this->addShade();
  if(shade) {
    shade->fromJSON(obj);
    shade->save();
    shade->emitState("shadeAdded");
  }
  return shade;
}
SomfyShade *SomfyShadeController::addShade() {
  Preferences pref;
  uint8_t shadeId = this->getNextShadeId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.  The time has come so the sort order is set below.
  if(shadeId == 255) return nullptr;
  SomfyShade *shade = &this->shades[shadeId - 1];
  if(shade) {
    shade->setShadeId(shadeId);
    shade->sortOrder = this->getMaxShadeOrder() + 1;
    ESP_LOGD(TAG, "Sort order set to %d", shade->sortOrder);
    this->isDirty = true;
  }
  return shade;
}
bool SomfyShadeController::unlinkRepeater(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] == address) this->repeaters[i] = 0;
  }
  this->compressRepeaters();
  this->isDirty = true;
  return true;  
}
bool SomfyShadeController::linkRepeater(uint32_t address) {
  bool bSet = false;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(!bSet && this->repeaters[i] == address) bSet = true;
    else if(bSet && this->repeaters[i] == address) this->repeaters[i] = 0;
  }
  if(!bSet) {
    for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
      if(this->repeaters[i] == 0) {
        this->repeaters[i] = address;
        return true;
      }
    }
  }
  return true;
}
SomfyRoom *SomfyShadeController::addRoom(JsonObject &obj) {
  SomfyRoom *room = this->addRoom();
  if(room) {
    room->fromJSON(obj);
    room->save();
    room->emitState("roomAdded");
  }
  return room;
}
SomfyRoom *SomfyShadeController::addRoom() {
  uint8_t roomId = this->getNextRoomId();
  // So the next room id will be the first one we run into with an id of 0 so
  if(roomId == 0) return nullptr;
  SomfyRoom *room = &this->rooms[roomId - 1];
  if(room) {
    room->roomId = roomId;
    room->sortOrder = this->getMaxRoomOrder() + 1;
    this->isDirty = true;
  }
  return room;
}
SomfyGroup *SomfyShadeController::addGroup(JsonObject &obj) {
  SomfyGroup *group = this->addGroup();
  if(group) {
    group->fromJSON(obj);
    group->save();
    group->emitState("groupAdded");
  }
  return group;
}
SomfyGroup *SomfyShadeController::addGroup() {
  uint8_t groupId = this->getNextGroupId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.
  if(groupId == 255) return nullptr;
  SomfyGroup *group = &this->groups[groupId - 1];
  if(group) {
    group->setGroupId(groupId);
    group->sortOrder = this->getMaxGroupOrder() + 1;
    this->isDirty = true;
  }
  return group;
}
uint32_t SomfyShadeController::sendFrame(somfy_frame_t &frame, uint8_t repeat, uint16_t delayMs, bool priority) {
  somfy_tx_job_t job;
  encodeTxJob(job, frame, repeat);
  job.firstRepeat = 1;
  return this->transceiver.submitTx(job, priority, delayMs);
}
bool SomfyShadeController::deleteShade(uint8_t shadeId) {
  Preferences pref;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) {
      shades[i].emitState("shadeRemoved");
      shades[i].unpublish();
      this->shades[i].clear();
    }
  }
  this->commit();
  return true;
}
bool SomfyShadeController::deleteRoom(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) {
      rooms[i].unpublish();
      for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
        if(shades[j].roomId == roomId) {
          shades[j].roomId = 0;
          shades[j].emitState();
        }
      }
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
        if(groups[j].roomId == roomId) {
          groups[j].roomId = 0;
          groups[j].emitState();
        }
      }
      rooms[i].emitState("roomRemoved");
      this->rooms[i].clear();
    }
  }
  this->commit();
  return true;
}
bool SomfyShadeController::deleteGroup(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) {
      groups[i].emitState("groupRemoved");
      groups[i].unpublish();
      this->groups[i].clear();
    }
  }
  this->commit();
  return true;
}
bool SomfyShadeController::loadShadesFile(const char *filename) { return ShadeConfigFile::load(this, filename); }
void SomfyShadeController::loop() { 
  this->transceiver.loop(); 
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) {
      this->shades[i].checkMovement();
      this->shades[i].setGPIOs();
    }
  }
  // Only commit the file once per second.
  if(this->isDirty && millis() - this->lastCommit > 1000) {
    this->commit();
  }
}
