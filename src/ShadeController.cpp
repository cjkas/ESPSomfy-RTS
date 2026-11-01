#include <Preferences.h>
#include "TaskWdt.h"
#include "esp_log.h"
#include "ConfigSettings.h"
#include "Somfy.h"
#include "ConfigFile.h"
#include "ModelEditor.h"
#include "model/ShadeChanges.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;
extern ConfigSettings settings;

void SomfyShadeController::end() { this->transceiver.disableReceive(); }
SomfyShadeController::SomfyShadeController() {
  memset(this->m_shadeIds, 255, sizeof(this->m_shadeIds));
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0x0FFFFF;
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
  resetTaskWdt(); // Make sure we don't reset inadvertently.
  ShadeConfigFile file;
  file.begin();
  file.save(this);
  file.end();
  this->isDirty = false;
  this->lastCommit = millis();
}
void SomfyShadeController::writeBackup() {
  resetTaskWdt(); // Make sure we don't reset inadvertently.
  this->backupData = "";
  this->backupData.reserve(16384);
  ShadeConfigFile file;
  file.beginRAM(&this->backupData);
  file.backup(this);
  file.end();
}
bool SomfyRoom::save() { somfy.commit(); return true; }
void SomfyShadeController::processFrame(somfy_frame_t &frame, bool internal, const SomfyRemote *skip) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(&this->shades[i] == skip) continue;
    if(this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal);
  }
}
void SomfyShadeController::markTxPending(uint32_t remoteAddress, uint32_t ticket, const SomfyRemote *skip) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade == skip) continue;
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
      shades[i].unpublish();
      this->shades[i].clear();
    }
  }
  // Its groups lose it, or a new shade given its id would join them.
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255 && this->groups[i].unlinkShade(shadeId)) this->groups[i].configChanged();
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
          shades[j].configChanged();
        }
      }
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
        if(groups[j].roomId == roomId) {
          groups[j].roomId = 0;
          groups[j].emitState();
          groups[j].configChanged();
        }
      }
      this->rooms[i].clear();
    }
  }
  this->commit();
  return true;
}
bool SomfyShadeController::deleteGroup(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) {
      // Its shades are no longer in a group, which their inGroup shows.
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES && groups[i].linkedShades[j] != 0; j++) {
        SomfyShade *shade = this->getShadeById(groups[i].linkedShades[j]);
        if(shade) shade->configChanged();
      }
      groups[i].unpublish();
      this->groups[i].clear();
    }
  }
  this->commit();
  return true;
}
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
