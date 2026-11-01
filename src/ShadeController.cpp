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
SomfyShadeController::SomfyShadeController() : ShadeModel(*this, *this) {
  memset(this->m_shadeIds, 255, sizeof(this->m_shadeIds));
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0x0FFFFF;
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
uint32_t SomfyShadeController::sendFrame(somfy_frame_t &frame, uint8_t repeat, uint16_t delayMs, bool priority) {
  somfy_tx_job_t job;
  encodeTxJob(job, frame, repeat);
  job.firstRepeat = 1;
  return this->transceiver.submitTx(job, priority, delayMs);
}
uint32_t SomfyShadeController::submit(somfy_tx_job_t &job, bool priority, uint16_t delayMs) { return this->transceiver.submitTx(job, priority, delayMs); }
void SomfyShadeController::pulse(SomfyShade &shade, const somfy_frame_t &frame) { shade.triggerGPIOs(frame); }
void SomfyShadeController::onCommand(SomfyShade &shade, somfy_commands cmd, const char *source, uint32_t sourceAddress) { shade.emitCommand(cmd, source, sourceAddress); }
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
  this->tick(millis());
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].setGPIOs();
  // Only commit the file once per second.
  if(this->isDirty && millis() - this->lastCommit > 1000) {
    this->commit();
  }
}
