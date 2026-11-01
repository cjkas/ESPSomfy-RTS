#include <Preferences.h>
#include "esp_log.h"
#include "ShadeModel.h"
#include "RollingCode.h"

static const char *TAG = "Somfy";

void SomfyRemote::setRemoteAddress(uint32_t address) { this->m_remoteAddress = address; this->m_rollingCodeSynced = false; snprintf(this->m_remotePrefId, sizeof(this->m_remotePrefId), "_%lu", (unsigned long)this->m_remoteAddress); }
uint32_t SomfyRemote::getRemoteAddress() { return this->m_remoteAddress; }
somfy_commands SomfyRemote::transformCommand(somfy_commands cmd) { return this->flipCommands ? flipCommand(cmd) : cmd; }
bool SomfyRemote::isLastCommand(somfy_commands cmd) {
  if(this->lastFrame.cmd != cmd || this->lastFrame.rollingCode != this->lastRollingCode) {
    ESP_LOGD(TAG, "Not the last command %d: %d - %d", static_cast<uint8_t>(this->lastFrame.cmd), this->lastFrame.rollingCode, this->lastRollingCode);
    return false;
  }
  return true;
}
// The next rolling code is consumed by commitRollingCode once the frame using
// it has been queued.  After that lastRollingCode holds what NVS does.
uint16_t SomfyRemote::peekNextRollingCode() {
  return nextRollingCode(this->m_rollingCodeSynced, this->lastRollingCode, [this]() {
    Preferences pref;
    if(!pref.begin("ShadeCodes")) {
      ESP_LOGE(TAG, "peekNextRollingCode() pref.begin(ShadeCodes) FAILED");
    }
    uint16_t code = pref.getUShort(this->m_remotePrefId, 0);
    pref.end();
    return code;
  });
}
void SomfyRemote::commitRollingCode(uint16_t code) {
  Preferences pref;
  if(!pref.begin("ShadeCodes")) {
    ESP_LOGE(TAG, "commitRollingCode() pref.begin(ShadeCodes) FAILED");
  }
  pref.putUShort(this->m_remotePrefId, code);
  pref.end();
  this->p_lastRollingCode(code);
  this->m_rollingCodeSynced = true;
}
uint16_t SomfyRemote::p_lastRollingCode(uint16_t code) { 
  uint16_t old = this->lastRollingCode;
  this->lastRollingCode = code; 
  return old;
}
uint16_t SomfyRemote::setRollingCode(uint16_t code) {
  Preferences pref;
  if(this->lastRollingCode != code) {
    pref.begin("ShadeCodes");
    pref.putUShort(this->m_remotePrefId, code);
    pref.end();  
    this->lastRollingCode = code;
    this->m_rollingCodeSynced = true;
    ESP_LOGD(TAG, "Setting Last Rolling code %d", this->lastRollingCode);
  }
  return code;
}
SomfyLinkedRemote::SomfyLinkedRemote() {}
