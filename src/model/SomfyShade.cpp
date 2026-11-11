#include "esp_log.h"
#include "ShadeModel.h"

void SomfyShade::clear() {
  this->setShadeId(255);
  this->setRemoteAddress(0);
  this->behaviour.clear();
  this->flipPosition = false;
  this->flipCommands = false;
  this->lastRollingCode = 0;
  this->shadeType = shade_types::roller;
  this->tiltType = tilt_types::none;
  //this->txQueue.clear();
  this->bitLength = this->model->radioBitLength();
  this->proto = this->model->radioProto();
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++)
    this->linkedRemotes[i].setRemoteAddress(0);
  this->paired = false;
  this->name[0] = 0x00;
  this->upTime = 10000;
  this->downTime = 10000;
  this->tiltTime = 7000;
  this->stepSize = 100;
  this->repeats = 1;
  this->sortOrder = 255;
}
bool SomfyShade::linkRemote(uint32_t address, uint16_t rollingCode) {
  // Check to see if the remote is already linked. If it is
  // just return true after setting the rolling code
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) {
      this->linkedRemotes[i].setRollingCode(rollingCode);
      return true;
    }
  }
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == 0) {
      this->linkedRemotes[i].setRemoteAddress(address);
      this->linkedRemotes[i].setRollingCode(rollingCode);
      return true;
    }
  }
  return false;
}
bool SomfyShade::unlinkRemote(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) {
      this->linkedRemotes[i].setRemoteAddress(0);
      return true;
    }
  }
  return false;
}
bool SomfyShade::isInGroup() { return this->model->isInGroup(*this); }
uint16_t SomfyShade::p_lastRollingCode(uint16_t code) {
  uint16_t old = SomfyRemote::p_lastRollingCode(code);
  if(old != code) this->changed |= SHADE_CHANGED_ROLLING_CODE;
  return old;
}
uint8_t SomfyShade::getFlags() const { return joinShadeFlags(*this, this->light, this->state().stateFlags); }
void SomfyShade::setFlags(uint8_t flags) {
  uint8_t stateFlags = 0;
  splitShadeFlags(flags, *this, this->light, stateFlags);
  this->behaviour.setStateFlags(stateFlags);
}
int8_t SomfyShade::transformPosition(float fpos) { return displayPosition(fpos, this->flipPosition); }
SomfyRemote *SomfyShade::findRemote(uint32_t address) {
  if(this->getRemoteAddress() == address) return this;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) return &this->linkedRemotes[i];
  }
  return nullptr;
}
void SomfyShade::processFrame(somfy_frame_t &frame, bool internal, uint32_t now) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return;
  SomfyRemote *remote = this->findRemote(frame.remoteAddress);
  if(!remote) return;
  if(remote != this && frame.cmd != somfy_commands::Sensor) remote->setRollingCode(frame.rollingCode);
  this->behaviour.onFrame(frame, internal, now);
}
void SomfyShade::processWaitingFrame(uint32_t now) { this->behaviour.onWaitingFrame(now); }
void SomfyShade::processInternalCommand(somfy_commands cmd, uint8_t repeat, uint32_t now) { this->behaviour.onGroupCommand(cmd, repeat, now); }
void SomfyShade::checkMovement(uint32_t now) { this->behaviour.tick(now); }
void SomfyShade::markTxPending(uint32_t ticket, uint32_t now) { this->behaviour.onTxQueued(ticket, now); }
void SomfyShade::processTxEvent(const somfy_tx_event_t &evt) { this->behaviour.onTxEvent(evt); }
bool SomfyShade::sendCommand(somfy_commands cmd) { return this->sendCommand(cmd, this->repeats); }
bool SomfyShade::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  if(this->bitLength == 0) this->bitLength = this->model->radioBitLength();
  return this->behaviour.sendCommand(cmd, repeat, stepSize, millis());
}
bool SomfyShade::sendTiltCommand(somfy_commands cmd) { return this->behaviour.sendTiltCommand(cmd, millis()); }
bool SomfyShade::moveToTarget(float pos, float tilt) { return this->behaviour.moveToTarget(pos, tilt, millis()); }
bool SomfyShade::moveToTiltTarget(float target) { return this->behaviour.moveToTiltTarget(target, millis()); }
bool SomfyShade::setMyPosition(int8_t pos, int8_t tilt) { return this->behaviour.setMyPosition(pos, tilt, millis()); }
bool SomfyShade::moveToMyPosition() { return this->behaviour.moveToMyPosition(millis()); }
void SomfyShade::setPosition(float pos, float tilt) { this->behaviour.setPosition(pos, tilt); }
// The shade hears its own frame itself (ShadeBehaviour), so the model leaves
// it out.
send_result_t SomfyShade::send(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority, uint32_t now) {
  return this->model->send(*this, this, cmd, repeat, stepSize, delayMs, priority, this, now);
}
void SomfyShade::onCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress) { this->model->events.onCommand(*this, cmd, source, sourceAddress); }
void SomfyShade::onSaveWanted() { this->model->isDirty = true; }
void SomfyShade::onSensorFlags() { this->model->updateGroupFlags(); }
