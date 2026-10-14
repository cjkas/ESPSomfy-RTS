#include <Preferences.h>
#include "esp_log.h"
#include "Somfy.h"
#include "ShadeMotion.h"

static const char *TAG = "Somfy";

extern SomfyShadeController somfy;

#define SETMY_REPEATS 35
#define TILT_REPEATS 15

// How long a shade's simulated movement waits for its command to go on air
// before it starts anyway in case the event was lost.
#define TX_PENDING_TIMEOUT 30000

void SomfyShade::clear() {
  this->setShadeId(255);
  this->setRemoteAddress(0);
  this->moveStart = 0;
  this->tiltStart = 0;
  this->sensors.clear();
  this->startPos = 0.0f;
  this->startTiltPos = 0.0f;
  this->settingMyPos = false;
  this->settingPos = false;
  this->settingTiltPos = false;
  this->awaitMy = 0;
  this->flipPosition = false;
  this->flipCommands = false;
  this->lastRollingCode = 0;
  this->shadeType = shade_types::roller;
  this->tiltType = tilt_types::none;
  //this->txQueue.clear();
  this->currentPos = 0.0f;
  this->currentTiltPos = 0.0f;
  this->direction = 0;
  this->tiltDirection = 0;  
  this->target = 0.0f;
  this->tiltTarget = 0.0f;
  this->myPos = -1.0f;
  this->myTiltPos = -1.0f;
  this->bitLength = somfy.transceiver.config.type;
  this->proto = somfy.transceiver.config.proto;
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
  Preferences pref;
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
      this->commit();
      return true;
    }
  }
  return false;
}
void SomfyShade::commit() { somfy.commit(); }
void SomfyShade::commitShadePosition() {
  Preferences pref;
  somfy.isDirty = true;
}
void SomfyShade::commitMyPosition() {
  Preferences pref;
  somfy.isDirty = true;
}
void SomfyShade::commitTiltPosition() {
  Preferences pref;
  somfy.isDirty = true;
}
bool SomfyShade::unlinkRemote(uint32_t address) {
  Preferences pref;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) {
      this->linkedRemotes[i].setRemoteAddress(0);
      this->commit();
      return true;
    }
  }
  return false;
}
bool SomfyShade::isAtTarget() { 
  float epsilon = .00001;
  if(this->tiltType == tilt_types::tiltonly) return fabs(this->currentTiltPos - this->tiltTarget) < epsilon;
  else if(this->tiltType == tilt_types::none) return fabs(this->currentPos - this->target) < epsilon;
  return fabs(this->currentPos - this->target) < epsilon && fabs(this->currentTiltPos - this->tiltTarget) < epsilon; 
}

bool SomfyShade::isInGroup() {
  if(this->getShadeId() == 255) return false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(somfy.groups[i].getGroupId() != 255 && somfy.groups[i].hasShadeId(this->getShadeId())) return true;
  }
  return false;
}
void SomfyShade::checkMovement() {
  const uint64_t curTime = millis();
  const bool sunFlag = this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag);
  const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
  const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
  // We need to first evaluate the sensor flags as these could be triggering movement from previous sensor inputs. So
  // we must check this before setting the directional items or it will not get processed until the next loop.
  int32_t downTime = (int32_t)this->downTime;
  int32_t upTime = (int32_t)this->upTime;
  int32_t tiltTime = (int32_t)this->tiltTime;
  if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) downTime = upTime = tiltTime = 1;
  

  // We are checking movement for essentially 3 types of motors.
  // If this is an integrated tilt we need to first tilt in the direction we are moving then move.  We know 
  // what needs to be done by the tilt type.  Set a tilt first flag to indicate whether we should be tilting or
  // moving. If this is only a tilt action then the regular tilt action should operate fine.
  int8_t currDir = this->direction;
  int8_t currTiltDir = this->tiltDirection;
  this->p_direction(this->currentPos == this->target ? 0 : this->currentPos > this->target ? -1 : 1);
  bool tilt_first = this->tiltType == tilt_types::integrated && ((this->direction == -1 && this->currentTiltPos != 0.0f) || (this->direction == 1 && this->currentTiltPos != 100.0f));

  this->p_tiltDirection(this->currentTiltPos == this->tiltTarget ? 0 : this->currentTiltPos > this->tiltTarget ? -1 : 1);
  if(tilt_first) this->p_tiltDirection(this->direction);
  else if(this->direction != 0) this->p_tiltDirection(0);
  uint8_t currPos = floor(this->currentPos);
  uint8_t currTiltPos = floor(this->currentTiltPos);
  if(this->direction != 0) this->lastMovement = this->direction;
  switch(this->tx.hold.check(this->direction != 0 || this->tiltDirection != 0, (uint32_t)curTime, TX_PENDING_TIMEOUT)) {
    case TxHoldCheck::holding:
      // The command that set us moving hasn't gone on air yet so the motor
      // hasn't started.  Hold the simulated position until it does.
      this->moveStart = this->tiltStart = curTime;
      if(currDir != this->direction || currTiltDir != this->tiltDirection) this->emitState();
      return;
    case TxHoldCheck::expired:
      ESP_LOGW(TAG, "[Shade %u] Timed out waiting for the radio, moving anyway", this->shadeId);
      break;
    case TxHoldCheck::none:
      break;
  }
  this->applySensorMove(this->sensors.check(sunFlag, isSunny, isWindy, (uint32_t)curTime));

  if(!tilt_first && this->direction > 0) {
    this->p_currentPos(travelPosition(this->startPos, 1, (uint32_t)(curTime - this->moveStart), downTime));
    if(this->currentPos >= this->target) {
      this->p_currentPos(this->target);
      //if(this->settingMyPos) Serial.printf("IsAtTarget: %d  %f=%f\n", this->isAtTarget(), this->currentPos, this->target);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        // At the end limit the motor stops by itself.  If the stop doesn't go
        // out the shade is now running on to the limit so leave the tilt be.
        if(this->target == 100.0 || this->sendStop(1, false)) {
          if(!isAtTarget()) {
            ESP_LOGD(TAG, "We are not at our tilt target: %.2f", this->tiltTarget);
            // We now need to move the tilt to the position we requested.
            this->moveToTiltTarget(this->tiltTarget);
          }
        }
      }
      this->p_direction(0);
      this->tiltStart = curTime;
      this->startTiltPos = this->currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(!tilt_first && this->direction < 0) {
    // Shades often move slower up so this uses its own travel time.
    this->p_currentPos(travelPosition(this->startPos, -1, (uint32_t)(curTime - this->moveStart), upTime));
    if(this->currentPos <= this->target) {
      this->p_currentPos(this->target);
      //if(this->settingMyPos) Serial.printf("IsAtTarget: %d  %f=%f\n", this->isAtTarget(), this->currentPos, this->target);
      
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        // At the end limit the motor stops by itself.  If the stop doesn't go
        // out the shade is now running on to the limit so leave the tilt be.
        if(this->target == 0.0 || this->sendStop(-1, false)) {
          if(!isAtTarget()) {
            ESP_LOGD(TAG, "We are not at our tilt target: %.2f", this->tiltTarget);
            // We now need to move the tilt to the position we requested.
            this->moveToTiltTarget(this->tiltTarget);
          }
        }
      }
      this->p_direction(0);
      this->tiltStart = curTime;
      this->startTiltPos = this->currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->tiltDirection > 0) {
    if(tilt_first) this->moveStart = curTime;
    this->p_currentTiltPos(travelPosition(this->startTiltPos, 1, (uint32_t)(curTime - this->tiltStart), tiltTime));
    if(tilt_first) {
      if(this->currentTiltPos >= 100.0f) {
        this->p_currentTiltPos(100.0f);
        this->moveStart = curTime;
        this->startPos = this->currentPos;
        //this->p_tiltDirection(0);
        //Serial.println("Setting tiltDirection to 0 (tilt_first)");
      }
    }
    else if(this->currentTiltPos >= this->tiltTarget) {
      this->p_currentTiltPos(this->tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          //Serial.printf("Sending My -- tiltTarget: %.2f, tiltDirection: %d\n", this->tiltTarget, this->tiltDirection);
          if(this->tiltTarget != 100.0f || this->currentPos != 100.0f) this->sendStop(1, true);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 100.
          if(this->tiltTarget != 100.0f) this->sendStop(1, true);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(this->tiltDirection < 0) {
    if(tilt_first) this->moveStart = curTime;
    if(tiltTime == 0) this->p_tiltDirection(0);
    this->p_currentTiltPos(travelPosition(this->startTiltPos, -1, (uint32_t)(curTime - this->tiltStart), tiltTime));
    if(tilt_first) {
      if(this->currentTiltPos <= 0.0f) {
        this->p_currentTiltPos(0.0f);
        this->moveStart = curTime;
        this->startPos = this->currentPos;
        //this->p_tiltDirection(0);
      }
    }
    else if(this->currentTiltPos <= this->tiltTarget) {
      this->p_currentTiltPos(this->tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          //Serial.printf("Sending My -- tiltTarget: %.2f, tiltDirection: %d\n", this->tiltTarget, this->tiltDirection);
          if(this->tiltTarget != 0.0 || this->currentPos != 0.0) this->sendStop(-1, true);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 0.
          if(this->tiltTarget != 0.0) this->sendStop(-1, true);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      ESP_LOGI(TAG, "Stopping at tilt position");
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->settingMyPos && this->isAtTarget()) {
    // Set this position before sending the command.  If you don't the processFrame function
    // will send the shade back to its original My position.
    const float oldMyPos = this->myPos;
    const float oldMyTiltPos = this->myTiltPos;
    if(this->tiltType != tilt_types::none) {
      if(this->myTiltPos == this->currentTiltPos && this->myPos == this->currentPos) this->myPos = this->myTiltPos = -1;
      else {
        this->p_myPos(this->currentPos);
        this->p_myTiltPos(this->currentTiltPos);
      }
    }
    else {
      this->p_myTiltPos(-1);
      if(this->myPos == this->currentPos) this->p_myPos(-1);
      else this->p_myPos(this->currentPos);
    }
    // Give the motor time to settle at the target before it is told to store it.
    this->sendSetMy(200, oldMyPos, oldMyTiltPos);
    this->settingMyPos = false;
    this->emitState();
  }
  else if(currDir != this->direction || currPos != floor(this->currentPos) || currTiltDir != this->tiltDirection || currTiltPos != floor(this->currentTiltPos)) {
    // We need to emit on the socket that our state has changed.
    this->emitState();
  }
}


// State Setters
float SomfyShade::p_currentPos(float pos) {
  float old = this->currentPos;
  this->currentPos = pos;
  if(floor(old) != floor(pos)) this->publish("position", (uint8_t)50);
  return old;
}
float SomfyShade::p_currentTiltPos(float pos) {
  float old = this->currentTiltPos;
  this->currentTiltPos = pos;
  if(floor(old) != floor(pos)) this->publish("tiltPosition", this->transformPosition(static_cast<uint8_t>(floor(this->currentTiltPos))));
  return old;
}
uint16_t SomfyShade::p_lastRollingCode(uint16_t code) {
  uint16_t old = SomfyRemote::p_lastRollingCode(code);
  if(old != code) this->publish("lastRollingCode", code);
  return old;
}
bool SomfyShade::p_flag(somfy_flags_t flag, bool val) {
  bool old = !!(this->flags & static_cast<uint8_t>(flag));
  if(val) 
      this->flags |= static_cast<uint8_t>(flag);
  else
      this->flags &= ~(static_cast<uint8_t>(flag));
  return old;
}
bool SomfyShade::p_sunFlag(bool val) {
  bool old = this->p_flag(somfy_flags_t::SunFlag, val);
  if(old != val) this->publish("sunFlag", static_cast<uint8_t>(val));
  return old;
}
bool SomfyShade::p_windy(bool val) {
  bool old = this->p_flag(somfy_flags_t::Windy, val);
  if(old != val) this->publish("windy", static_cast<uint8_t>(val));
  return old;
}
bool SomfyShade::p_sunny(bool val) {
  bool old = this->p_flag(somfy_flags_t::Sunny, val);
  if(old != val) this->publish("sunny", static_cast<uint8_t>(val));
  return old;
}
int8_t SomfyShade::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->publish("direction", this->direction, true);
  }
  return old;
}
int8_t SomfyShade::p_tiltDirection(int8_t dir) {
  int8_t old = this->tiltDirection;
  if(old != dir) {
    this->tiltDirection = dir;
    this->publish("tiltDirection", this->tiltDirection, true);
  }
  return old;
}
float SomfyShade::p_target(float target) {
  float old = this->target;
  if(old != target) {
    this->target = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->publish("target", this->transformPosition(this->target), true);
  }
  return old;
}
float SomfyShade::p_tiltTarget(float target) {
  float old = this->tiltTarget;
  if(old != target) {
    this->tiltTarget = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->publish("tiltTarget", this->transformPosition(this->tiltTarget), true);
  }
  return old;
}
float SomfyShade::p_myPos(float pos) {
  float old = this->myPos;
  if(old != pos) {
    //if(this->transformPosition(pos) == 0) Serial.println("MyPos = %.2f", pos);
    this->myPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->publish("mypos", this->transformPosition(this->myPos), true);
  }
  return old;
}
float SomfyShade::p_myTiltPos(float pos) {
  float old = this->myTiltPos;
  if(old != pos) {
    this->myTiltPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->publish("myTiltPos", this->transformPosition(this->myTiltPos), true);
  }
  return old;
}

int8_t SomfyShade::transformPosition(float fpos) { return displayPosition(fpos, this->flipPosition); }
bool SomfyShade::isIdle() { 
  return this->isAtTarget() && this->direction == 0 && this->tiltDirection == 0; 
}
void SomfyShade::processWaitingFrame() {
  if(this->shadeId == 255) {
    this->lastFrame.await = 0; 
    return;
  }
  if(this->lastFrame.processed) return;
  if(this->lastFrame.await > 0 && (millis() > this->lastFrame.await)) {
    somfy_commands cmd = this->transformCommand(this->lastFrame.cmd);
    switch(cmd) {
      case somfy_commands::StepUp:
          this->lastFrame.processed = true;
          // Simply move the shade up by 1%.
          if(this->currentPos > 0) {
            this->p_target(floor(this->currentPos) - 1);
            this->setMovement(-1);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          break;
      case somfy_commands::StepDown:
          this->lastFrame.processed = true;
          // Simply move the shade down by 1%.
          if(this->currentPos < 100) {
            this->p_target(floor(this->currentPos) + 1);
            this->setMovement(1);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          break;
      case somfy_commands::Down:
      case somfy_commands::Up:
        if(this->tiltType == tilt_types::tiltmotor) { // Theoretically this should get here unless it does have a tilt motor.
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100.0f : 0.0f);
            this->setTiltMovement(dir);
            this->lastFrame.processed = true;
            ESP_LOGD(TAG, "%s Processing tilt %s after %d repeats",
                this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100 : 0);
            this->setMovement(dir);
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        else if(this->tiltType == tilt_types::euromode) {
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100.0f : 0.0f);
            this->setMovement(dir);
            this->lastFrame.processed = true;
            ESP_LOGD(TAG, "%s Processing %s after %d repeats",
                this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100 : 0);
            this->setTiltMovement(dir);
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        break;
      case somfy_commands::My:
        if(this->lastFrame.repeats >= SETMY_REPEATS && this->isIdle()) {
          if(floor(this->myPos) == floor(this->currentPos)) {
            // We are clearing it.
            this->p_myPos(-1);
            this->p_myTiltPos(-1);
          }
          else {
            this->p_myPos(this->currentPos);
            this->p_myTiltPos(this->currentTiltPos);
          }
          this->commitMyPosition();
          this->lastFrame.processed = true;
          this->emitState();
        }
        else if(this->isIdle()) {
          if(this->simMy())
            this->moveToMyPosition(); // Call out like this (instead of move to target) so that we don't get some of the goofy tilt only problems.
          else {
            if(this->myPos >= 0.0f && this->myPos <= 100.0f) this->p_target(this->myPos);
            if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          }
          this->setMovement(0);
          this->lastFrame.processed = true;
          this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
        }
        else {
          this->p_target(this->currentPos);
          this->p_tiltTarget(this->currentTiltPos);
        }
        if(this->lastFrame.repeats > SETMY_REPEATS + 2) this->lastFrame.processed = true;
        if(this->lastFrame.processed) {
          ESP_LOGD(TAG, "%s Processing MY after %d repeats", this->name, this->lastFrame.repeats);
        }
        break;
      default:
        break;
    }
  }
}
SomfyRemote *SomfyShade::findRemote(uint32_t address) {
  if(this->getRemoteAddress() == address) return this;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(this->linkedRemotes[i].getRemoteAddress() == address) return &this->linkedRemotes[i];
  }
  return nullptr;
}
void SomfyShade::processFrame(somfy_frame_t &frame, bool internal) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  SomfyRemote *remote = this->findRemote(frame.remoteAddress);
  if(!remote) return;
  if(remote != this && frame.cmd != somfy_commands::Sensor) remote->setRollingCode(frame.rollingCode);
  const uint64_t curTime = millis();
  this->lastFrame.copy(frame);
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->currentPos;
  this->startTiltPos = this->currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  if(!internal) this->settingMyPos = this->settingPos = this->settingTiltPos = false;
  somfy_commands cmd = this->transformCommand(frame.cmd);
  // At this point we are not processing the combo buttons
  // will need to see what the shade does when you press both.
  switch(cmd) {
    case somfy_commands::Sensor:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      {
        const uint8_t prevFlags = this->flags;
        const bool wasSunny = prevFlags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        const bool wasWindy = prevFlags & static_cast<uint8_t>(somfy_flags_t::Windy);
        const uint16_t status = frame.rollingCode << 4;
        if (status & static_cast<uint8_t>(somfy_flags_t::Sunny))
          this->p_sunny(true);
          //this->flags |= static_cast<uint8_t>(somfy_flags_t::Sunny);
        else
          this->p_sunny(false);
          //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::Sunny));
        if (status & static_cast<uint8_t>(somfy_flags_t::Windy))
          this->p_windy(true);
          //this->flags |= static_cast<uint8_t>(somfy_flags_t::Windy);
        else
          this->p_windy(false);
          //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::Windy));
        if(frame.rollingCode & static_cast<uint8_t>(somfy_flags_t::DemoMode))
          this->flags |= static_cast<uint8_t>(somfy_flags_t::DemoMode);
        else
          this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::DemoMode));
        const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
        this->sensors.onSensorFrame(wasSunny, wasWindy, isSunny, isWindy, (uint32_t)curTime);
        this->emitState();
        somfy.updateGroupFlags();
      }
      break;
    case somfy_commands::Prog:
    case somfy_commands::MyUp:
    case somfy_commands::MyDown:
    case somfy_commands::MyUpDown:
    case somfy_commands::UpDown:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
      
    case somfy_commands::Flag:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      this->p_sunFlag(false);
      //this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SunFlag));
      somfy.isDirty = true;
      this->emitState();
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      somfy.updateGroupFlags();
      break;    
    case somfy_commands::SunFlag:
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      {
        const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
        const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        //this->flags |= static_cast<uint8_t>(somfy_flags_t::SunFlag);
        this->p_sunFlag(true);
        this->applySensorMove(this->sensors.onSunFlag(isSunny, isWindy));
        somfy.isDirty = true;
        this->emitState();
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        somfy.updateGroupFlags();
      }
      break;
    case somfy_commands::Up:
      if(this->shadeType == shade_types::drycontact) {
        this->lastFrame.processed = true;
        return;
      }
      else if(this->shadeType == shade_types::drycontact2) {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->currentPos != 0.0f) this->p_target(0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if(this->tiltType == tilt_types::tiltmotor || this->tiltType == tilt_types::euromode) {
        // Wait another half second just in case we are potentially processing a tilt.
        if(!internal) this->lastFrame.await = curTime + 500;
        else this->lastFrame.processed = true;
      }
      else {
        // If from a remote we will simply be going up.
        if(this->tiltType == tilt_types::tiltonly && !internal) this->p_tiltTarget(0.0f);
        else if(!internal) {
          if(this->tiltType != tilt_types::tiltonly) this->p_target(0.0f);
          this->p_tiltTarget(0.0f);
        }
        this->lastFrame.processed = true;
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::Down:
      if(this->shadeType == shade_types::drycontact) {
        this->lastFrame.processed = true;
        return;
      }
      else if(this->shadeType == shade_types::drycontact2) {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->currentPos != 100.0f) this->p_target(100);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if (!this->sensors.windBlocksDown((uint32_t)curTime)) {
        if(this->tiltType == tilt_types::tiltmotor || this->tiltType == tilt_types::euromode) {
          // Wait another half seccond just in case we are potentially processing a tilt.
          if(!internal) this->lastFrame.await = curTime + 500;
          else this->lastFrame.processed = true;
        }
        else {
          this->lastFrame.processed = true;
          if(!internal) {
            if(this->tiltType != tilt_types::tiltonly) this->p_target(100.0f);
            if(this->tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
          }
        }
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::My:
      if(this->shadeType == shade_types::drycontact2) return;
      if(this->isToggle()) { // This is a one button device
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(!this->isIdle()) this->p_target(this->currentPos);
        else if(this->currentPos == 100.0f) this->p_target(0.0f);
        else if(this->currentPos == 0.0f) this->p_target(100.0f);
        else this->p_target(this->lastMovement == -1 ? 100 : 0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      else if(this->shadeType == shade_types::drycontact) {
        // In this case we need to toggle the contact but we only should do this if
        // this is not a repeat.
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(this->currentPos == 100.0f) this->p_target(0);
        else if(this->currentPos == 0.0f) this->p_target(100);
        else this->p_target(this->lastMovement == -1 ? 100 : 0);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
      }
      if(this->isIdle()) {
        if(!internal) {
          // This frame is coming from a remote. We are potentially setting
          // the my position.
          this->lastFrame.await = curTime + 500;
        }
        else {
          if(this->lastFrame.processed) return;
          ESP_LOGI(TAG, "Moving to My target");
          this->lastFrame.processed = true;
          if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
          this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        }
      }
      else {
        if(this->lastFrame.processed) return;
        this->lastFrame.processed = true;
        if(!internal) {
          if(this->tiltType != tilt_types::tiltonly) this->p_target(this->currentPos);
          this->p_tiltTarget(this->currentTiltPos);
        }
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    case somfy_commands::StepUp:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      dir = 0;
      if(this->lastFrame.stepSize == 0) this->lastFrame.stepSize = 1;
      if(!this->applyStep(-1, this->lastFrame.stepSize)) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::StepDown:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      dir = 1;
      if(this->lastFrame.stepSize == 0) this->lastFrame.stepSize = 1;
      if(!this->applyStep(1, this->lastFrame.stepSize)) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Toggle:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(!this->isIdle()) this->p_target(this->currentPos);
      else if(this->currentPos == 100.0f) this->p_target(0);
      else if(this->currentPos == 0.0f) this->p_target(100);
      else this->p_target(this->lastMovement == -1 ? 100 : 0);
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Stop:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      this->p_target(this->currentPos);
      this->p_tiltTarget(this->currentTiltPos);      
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Favorite:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->simMy()) {
        this->moveToMyPosition();
      }
      else {
        if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
        if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    default:
      dir = 0;
      break;
  }
  //if(dir == 0 && this->tiltType == tilt_types::tiltmotor && this->tiltDirection != 0) this->setTiltMovement(0);
  this->setMovement(dir);
}
void SomfyShade::processInternalCommand(somfy_commands cmd, uint8_t repeat) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  const uint64_t curTime = millis();
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->currentPos;
  this->startTiltPos = this->currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  switch(cmd) {
    case somfy_commands::Up:
      if(this->tiltType == tilt_types::tiltmotor) {
        if(repeat >= TILT_REPEATS)
          this->p_tiltTarget(0.0f);
        else
          this->p_target(0.0f);
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_currentPos(100.0f);
        this->p_tiltTarget(0.0f);
      }
      else {
        this->p_target(0.0f);
        this->p_tiltTarget(0.0f);
      }
      break;
    case somfy_commands::Down:
      if (!this->sensors.windBlocksDown((uint32_t)curTime)) {
        if(this->tiltType == tilt_types::tiltmotor) {
          if(repeat >= TILT_REPEATS)
            this->p_tiltTarget(100.0f);
          else
            this->p_target(100.0f);
        }
        else if(this->tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
          this->p_currentPos(100.0f);
          this->p_tiltTarget(100.0f);
        }
        else {
            this->p_target(100.0f);
            if(this->tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
        }
      }
      break;
    case somfy_commands::My:
      if(this->isIdle()) {
        ESP_LOGD(TAG, "Shade #%d is idle", this->getShadeId());
        if(this->simMy()) {
          this->moveToMyPosition();
        }
        else {
          if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
        }
      }
      else {
        if(this->tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
        }
        else this->p_target(this->currentPos);
        this->p_tiltTarget(this->currentTiltPos);
      }
      break;
    case somfy_commands::StepUp:
      // A group step is always one step.
      if(!this->applyStep(-1, 1)) return;
      break;
    case somfy_commands::StepDown:
      dir = 1;
      if(!this->applyStep(1, 1)) return;
      break;
    case somfy_commands::Flag:
      this->p_sunFlag(false);
      if(this->hasSunSensor()) {
        somfy.isDirty = true;
        this->emitState();
      }
      else {
        ESP_LOGW(TAG, "Shade does not have sensor %d", this->flags);
      }
      break;    
    case somfy_commands::SunFlag:
      if(this->hasSunSensor()) {
        const bool isWindy = this->flags & static_cast<uint8_t>(somfy_flags_t::Windy);
        const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
        this->p_sunFlag(true);
        //this->flags |= static_cast<uint8_t>(somfy_flags_t::SunFlag);
        this->applySensorMove(this->sensors.onSunFlag(isSunny, isWindy));
        somfy.isDirty = true;
        this->emitState();
      }
      else
        ESP_LOGW(TAG, "Shade does not have sensor %d", this->flags);
      break;
    default:
      dir = 0;
      break;
  }
  this->setMovement(dir);
}
void SomfyShade::setTiltMovement(int8_t dir) {
  int8_t currDir = this->tiltDirection;
  if(dir == 0) {
    // The shade tilt is stopped.
    this->startTiltPos = this->currentTiltPos;
    this->tiltStart = 0;
    this->p_tiltDirection(dir);
    if(currDir != dir) {
      this->commitTiltPosition();
    }
  }
  else if(this->tiltDirection != dir) {
    this->tiltStart = millis();
    this->startTiltPos = this->currentTiltPos;
    this->p_tiltDirection(dir);
  }
  if(this->tiltDirection != currDir) {
    this->emitState();
  }
}
void SomfyShade::markTxPending(uint32_t ticket) {
  // This runs before the caller sets the new target so direction still says
  // whether the shade was moving.  A reversal is simulated as soon as it is
  // queued even though the motor turns once it is on air.  See TxHold::mark
  // for which of several queued commands is held.
  this->tx.hold.mark(ticket, this->direction != 0 || this->tiltDirection != 0, millis());
}
void SomfyShade::processTxEvent(const somfy_tx_event_t &evt) {
  const ShadeTxActions act = this->tx.onEvent(evt, this->direction != 0 || this->tiltDirection != 0);
  if(act.startMovement) {
    // The motor starts now so the simulated movement does too.
    this->moveStart = this->tiltStart = act.startAt;
  }
  if(act.cancelMovement) {
    // The motor never got the command so it hasn't moved.
    ESP_LOGW(TAG, "[Shade %u] Command dropped by the radio, staying at %.2f%%", this->shadeId, this->currentPos);
    this->settingPos = this->settingTiltPos = this->settingMyPos = false;
    this->p_target(this->currentPos);
    this->p_tiltTarget(this->currentTiltPos);
    this->emitState();
  }
  if(act.commitMy) this->commitMyPosition();
  if(act.restoreMy) {
    ESP_LOGW(TAG, "[Shade %u] Favorite not sent in full, keeping the previous one", this->shadeId);
    this->p_myPos(act.myOldPos);
    this->p_myTiltPos(act.myOldTiltPos);
    this->emitState();
  }
  if(act.runToLimit) {
    ESP_LOGW(TAG, "[Shade %u] Stop dropped by the radio, running on to the limit", this->shadeId);
    this->runToLimit(act.limitDir, act.limitTilt, act.limitSince);
    this->emitState();
  }
}
bool SomfyShade::sendStop(int8_t dir, bool tilt) {
  const uint32_t now = millis();
  // Stops jump the radio queue so the motor stops as close to the target as it can.
  if(const send_result_t sent = SomfyRemote::sendCommandAfter(somfy_commands::My, this->repeats, 0, 0, true)) {
    this->tx.stopSent(sent.ticket, now, dir, tilt);
    return true;
  }
  ESP_LOGW(TAG, "[Shade %u] Stop not sent, running on to the limit", this->shadeId);
  this->runToLimit(dir, tilt, now);
  return false;
}
bool SomfyShade::sendSetMy(uint16_t delayMs, float oldPos, float oldTiltPos) {
  // The new favorite is already in myPos/myTiltPos.  It is committed once the
  // whole long press is on air, see processTxEvent.
  if(const send_result_t sent = SomfyRemote::sendCommandAfter(somfy_commands::My, SETMY_REPEATS, 0, delayMs)) {
    this->tx.setMySent(sent.ticket, oldPos, oldTiltPos);
    // Without the radio (GPIO remotes) there is nothing to wait for.
    if(sent.ticket == 0) this->commitMyPosition();
    return true;
  }
  // The motor never learned it so keep the favorite it has.
  this->p_myPos(oldPos);
  this->p_myTiltPos(oldTiltPos);
  return false;
}
void SomfyShade::runToLimit(int8_t dir, bool tilt, uint32_t since) {
  // The motor didn't get a stop at the target so it keeps going until its end
  // limit.  Carry on simulating that from where and when it should have stopped.
  if(tilt) {
    this->startTiltPos = this->currentTiltPos;
    this->tiltStart = since;
    this->settingTiltPos = false;
    this->p_tiltTarget(limitTarget(dir));
  }
  else {
    this->startPos = this->currentPos;
    this->moveStart = since;
    this->settingPos = false;
    this->p_target(limitTarget(dir));
  }
  // The favorite can't be set where the shade won't stop.
  this->settingMyPos = false;
}
void SomfyShade::applySensorMove(SensorMove move) {
  const SensorTargets t = sensorTargets(move, this->tiltType, this->myPos, this->myTiltPos);
  if(t.setLift) this->p_target(t.lift);
  if(t.setTilt) this->p_tiltTarget(t.tilt);
}
bool SomfyShade::applyStep(int8_t dir, uint8_t frameStepSize) {
  const StepTargets t = stepTargets(this->tiltType, dir, this->currentPos, this->currentTiltPos,
    this->upTime, this->downTime, this->tiltTime, this->stepSize, frameStepSize);
  if(t.action == StepAction::ignore) return false;
  if(t.setLift) this->p_target(t.lift);
  if(t.setTilt) this->p_tiltTarget(t.tilt);
  return true;
}
void SomfyShade::setMovement(int8_t dir) {
  int8_t currDir = this->direction;
  int8_t currTiltDir = this->tiltDirection;
  if(dir == 0) {
    if(currDir != dir || currTiltDir != dir) this->commitShadePosition();
  }
  else {
    this->tiltStart = this->moveStart = millis();
    this->startPos = this->currentPos;
    this->startTiltPos = this->currentTiltPos;
  }
  if(this->direction != currDir || currTiltDir != this->tiltDirection) {
    this->emitState();
  }
}
bool SomfyShade::setMyPosition(int8_t pos, int8_t tilt) {
  if(!this->isIdle()) return true; // Don't do this if it is moving.
  bool sent = true;
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_myPos(-1.0f);    
    if(tilt != floor(this->currentTiltPos)) {
      // The favorite is set once we get there, if the move went out.
      if(tilt == floor(this->myTiltPos))
        sent = this->settingMyPos = this->moveToMyPosition();
      else
        sent = this->settingMyPos = this->moveToTarget(100, tilt);
    }
    else if(tilt == floor(this->myTiltPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->currentTiltPos != this->myTiltPos) {
        sent = this->settingMyPos = this->moveToMyPosition();
      }
      else if((sent = SomfyRemote::sendCommand(somfy_commands::My, this->repeats))) {
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else return this->storeMyPosition(-1.0f, this->currentTiltPos);
    this->commitMyPosition();
    this->emitState();
  }
  else if(this->tiltType != tilt_types::none) {
      if(tilt < 0) tilt = 0;
      if(pos != floor(this->currentPos) || tilt != floor(this->currentTiltPos)) {
        // The favorite is set once we get there, if the move went out.
        if(pos == floor(this->myPos) && tilt == floor(this->myTiltPos))
          sent = this->settingMyPos = this->moveToMyPosition();
        else
          sent = this->settingMyPos = this->moveToTarget(pos, tilt);
      }
      else if(pos == floor(this->myPos) && tilt == floor(this->myTiltPos)) {
        // Of so we need to clear the my position. These motors are finicky so send
        // a my command to ensure we are actually at the my position then send the clear
        // command.  There really is no other way to do this.
        if(this->currentPos != this->myPos || this->currentTiltPos != this->myTiltPos) {
          sent = this->settingMyPos = this->moveToMyPosition();
        }
        else if((sent = SomfyRemote::sendCommand(somfy_commands::My, this->repeats))) {
          this->settingPos = false;
          this->settingMyPos = true;
        }
      }
      else return this->storeMyPosition(this->currentPos, this->currentTiltPos);
      this->commitMyPosition();
      this->emitState();
  }
  else {
    if(pos != floor(this->currentPos)) {
      // The favorite is set once we get there, if the move went out.
      if(pos == floor(this->myPos))
        sent = this->settingMyPos = this->moveToMyPosition();
      else
        sent = this->settingMyPos = this->moveToTarget(pos);
    }
    else if(pos == floor(this->myPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->myPos != this->currentPos) {
        sent = this->settingMyPos = this->moveToMyPosition();
      }
      else if((sent = SomfyRemote::sendCommand(somfy_commands::My, this->repeats))) {
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else sent = this->storeMyPosition(this->currentPos, -1.0f);
  }
  return sent;
}
bool SomfyShade::storeMyPosition(float pos, float tiltPos) {
  // Set it before sending so processFrame doesn't head for the old one.  It is
  // only committed once the motor was told about it.
  const float oldPos = this->myPos;
  const float oldTiltPos = this->myTiltPos;
  this->p_myPos(pos);
  this->p_myTiltPos(tiltPos);
  const bool sent = this->sendSetMy(0, oldPos, oldTiltPos);
  this->emitState();
  return sent;
}
bool SomfyShade::moveToMyPosition() {
  if(!this->isIdle()) return true;
  ESP_LOGI(TAG, "Moving to My Position");
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_currentPos(100.0f);
    this->p_myPos(-1.0f);
  }
  if(this->currentPos == this->myPos) {
    if(this->tiltType != tilt_types::none) {
      if(this->currentTiltPos == this->myTiltPos) return true; // Nothing to see here since we are already here.
    }
    else
      return true;
  }
  if(this->myPos == -1 && (this->tiltType == tilt_types::none || this->myTiltPos == -1)) return true;
  this->settingPos = false;
  if(this->simMy()) {
    ESP_LOGI(TAG, "Moving to simulated favorite");
    if(!this->moveToTarget(this->myPos, this->myTiltPos)) return false;
  }
  else if(!SomfyRemote::sendCommand(somfy_commands::My, this->repeats)) return false;
  if(this->tiltType != tilt_types::tiltonly && this->myPos >= 0.0f && this->myPos <= 100.0f) this->p_target(this->myPos);
  if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
  return true;
}
bool SomfyShade::sendCommand(somfy_commands cmd) { return this->sendCommand(cmd, this->repeats); }
bool SomfyShade::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  ESP_LOGD(TAG, "Send command start");
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  if(this->bitLength == 0) this->bitLength = somfy.transceiver.config.type;
  // If same direction command sent while already moving, stop the state tracking.
  // The real motor stops on its own when it receives the same direction again.
  if((cmd == somfy_commands::Up && this->direction == -1) ||
     (cmd == somfy_commands::Down && this->direction == 1)) {
    ESP_LOGD(TAG, "Same command as dir");
    if(!SomfyRemote::sendCommand(cmd, repeat)) return false;
    this->p_target(this->currentPos);
    this->p_tiltTarget(this->currentTiltPos);
    this->setMovement(0);
    return true;
  }
  // The simulated state is only moved for commands the radio took.
  if(cmd == somfy_commands::Up) {
    if(this->tiltType == tilt_types::euromode) {
      // In euromode we need to long press for 2 seconds on the
      // up command.
      if(!SomfyRemote::sendCommand(cmd, TILT_REPEATS)) return false;
      this->p_target(0.0f);
    }
    else {
      if(!SomfyRemote::sendCommand(cmd, repeat)) return false;
      if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_tiltTarget(0.0f);
        this->p_currentPos(100.0f);
      }
      else this->p_target(0.0f);
      if(this->tiltType == tilt_types::integrated) this->p_tiltTarget(0.0f);
    }
  }
  else if(cmd == somfy_commands::Down) {
    if(this->tiltType == tilt_types::euromode) {
      // In euromode we need to long press for 2 seconds on the
      // down command.
      if(!SomfyRemote::sendCommand(cmd, TILT_REPEATS)) return false;
      this->p_target(100.0f);
    }
    else {
      if(!SomfyRemote::sendCommand(cmd, repeat)) return false;
      if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_tiltTarget(100.0f);
        this->p_currentPos(100.0f);
      }
      else this->p_target(100.0f);
      if(this->tiltType == tilt_types::integrated) this->p_tiltTarget(100.0f);
    }
  }
  else if(cmd == somfy_commands::My) {
    if(this->isToggle() || this->shadeType == shade_types::drycontact) {
      if(!SomfyRemote::sendCommand(cmd, repeat)) return false;
    }
    else if(this->shadeType == shade_types::drycontact2){
        ESP_LOGD(TAG, "Send command start 1");
      return true;
    }
    else if(this->isIdle()) {
      bool sent = this->moveToMyPosition();
        ESP_LOGD(TAG, "Send command end 2");
      return sent;
    }
    else {
      if(!SomfyRemote::sendCommand(cmd, repeat)) return false;
      if(this->tiltType != tilt_types::tiltonly) this->p_target(this->currentPos);
      this->p_tiltTarget(this->currentTiltPos);
    }
  }
  else if(cmd == somfy_commands::Toggle) {
    if(this->bitLength != 80) { if(!SomfyRemote::sendCommand(somfy_commands::My, repeat, stepSize)) return false; }
    else if(!SomfyRemote::sendCommand(somfy_commands::Toggle, repeat)) return false;
  }
  else if(this->isToggle() && cmd == somfy_commands::Prog) {
    if(!SomfyRemote::sendCommand(somfy_commands::Toggle, repeat, stepSize)) return false;
  }
  else {
    if(!SomfyRemote::sendCommand(cmd, repeat, stepSize)) return false;
  }
  ESP_LOGD(TAG, "Send command end");
  return true;
}
uint8_t SomfyShade::tiltRepeats() const { return this->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->repeats; }
bool SomfyShade::sendTiltCommand(somfy_commands cmd) {
  float target;
  if(cmd == somfy_commands::Up) target = 0.0f;
  else if(cmd == somfy_commands::Down) target = 100.0f;
  else if(cmd == somfy_commands::My) target = this->currentTiltPos;
  else return true;
  if(!SomfyRemote::sendCommand(cmd, this->tiltRepeats())) return false;
  this->p_tiltTarget(target);
  return true;
}
bool SomfyShade::moveToTiltTarget(float target) {
  somfy_commands cmd = somfy_commands::My;
  if(target < this->currentTiltPos)
    cmd = somfy_commands::Up;
  else if(target > this->currentTiltPos)
    cmd = somfy_commands::Down;
  if(target >= 0.0f && target <= 100.0f) {
    // Only send a command if the lift is not moving.
    if(this->currentPos == this->target || this->tiltType == tilt_types::tiltmotor) {
      if(cmd != somfy_commands::My) {
        ESP_LOGI(TAG, "Moving Tilt to %.2f%% from %.2f%% using %s",
            target, this->currentTiltPos, translateSomfyCommand(cmd).c_str());
        if(!SomfyRemote::sendCommand(cmd, this->tiltRepeats())) return false;
      }
      // If the blind is currently moving then the command to stop it
      // will occur on its own when the tilt target is set.
    }
    this->p_tiltTarget(target);
  }
  if(cmd != somfy_commands::My) this->settingTiltPos = true;
  return true;
}
bool SomfyShade::moveToTarget(float pos, float tilt) {
  ESP_LOGI(TAG, "[Shade %u] moveToTarget(pos=%.2f, tilt=%.2f) settingPos=%d direction=%d currentTarget=%.2f currentPos=%.2f",
      this->getShadeId(), pos, tilt, this->settingPos, this->direction, this->target, this->currentPos);
  somfy_commands cmd = somfy_commands::My;
  if(this->isToggle()) {
    // Overload this as we cannot seek a position on a garage door or single button device.
    this->p_target(pos);
    this->p_currentPos(pos);
    this->emitState();
    return true;
  }
  if(this->tiltType == tilt_types::tiltonly) {
    // A tilt only shade is always at 100%.
    this->p_target(100.0f);
    this->p_myPos(-1.0f);
    this->p_currentPos(100.0f);
    pos = 100;
    if(tilt < this->currentTiltPos) cmd = somfy_commands::Up;
    else if(tilt > this->currentTiltPos) cmd = somfy_commands::Down;
  }
  else {
    if(pos < this->currentPos)
      cmd = somfy_commands::Up;
    else if(pos > this->currentPos)
      cmd = somfy_commands::Down;
    else if(tilt >= 0 && tilt < this->currentTiltPos)
      cmd = somfy_commands::Up;
    else if(tilt >= 0 && tilt > this->currentTiltPos)
      cmd = somfy_commands::Down;
  }
  if(cmd != somfy_commands::My) {
    if(tilt >= 0) {
      ESP_LOGI(TAG, "Moving to %.2f%% from %.2f%% tilt %.2f%% from %.2f%% using %s",
          pos, this->currentPos, tilt, this->currentTiltPos, translateSomfyCommand(cmd).c_str());
    } else {
      ESP_LOGI(TAG, "Moving to %.2f%% from %.2f%% using %s",
          pos, this->currentPos, translateSomfyCommand(cmd).c_str());
    }
    if(!SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::euromode ? TILT_REPEATS : this->repeats)) return false;
    this->settingPos = true;
    this->p_target(pos);
    if(tilt >= 0) {
      this->p_tiltTarget(tilt);
      this->settingTiltPos = true;
    }
  }
  return true;
}
bool SomfyShade::save() {
  Preferences pref;
  this->commit();
  this->publish();
  return true;
}
bool SomfyShade::isToggle() {
  switch(this->shadeType) {
    case shade_types::garage1:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
      return true;
    default:
      break;
  }
  return false;
}










