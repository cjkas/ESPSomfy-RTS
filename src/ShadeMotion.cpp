#include <algorithm>
#include "ShadeMotion.h"

float travelPosition(float startPos, int8_t dir, uint32_t elapsedMs, int32_t travelMs) {
  // Work in time travelled from the end we are moving away from: the start
  // position is turned into the time it would take to get there, the elapsed
  // time is added and the total is turned back into a position.  The addition
  // wraps like the uint32 millis() so it keeps working across a rollover.
  if(dir > 0) {
    if(travelMs == 0) return 100.0f;
    int32_t ms = (int32_t)floor((startPos / 100) * travelMs);
    ms += elapsedMs;
    ms = std::min(travelMs, ms);
    if(ms >= travelMs) return 100.0f;
    return std::min(std::max(0.0f, (float)ms / (float)travelMs), 1.0f) * 100;
  }
  if(dir < 0) {
    if(travelMs == 0) return 0.0f;
    int32_t ms = travelMs - (int32_t)floor((startPos / 100) * travelMs);
    ms += elapsedMs;
    ms = std::min(travelMs, ms);
    if(ms >= travelMs) return 0.0f;
    float pos = (1.0f - std::min(std::max(0.0f, (float)ms / (float)travelMs), 1.0f)) * 100;
    return pos <= 0.0f ? 0.0f : pos;
  }
  return startPos;
}
int8_t displayPosition(float pos, bool flip) {
  if(pos < 0) return -1;
  return static_cast<int8_t>(flip && pos >= 0.00f ? floor(100.0f - pos) : floor(pos));
}
float limitTarget(int8_t dir) { return dir > 0 ? 100.0f : 0.0f; }
bool stepPosition(float pos, int8_t dir, uint32_t travelMs, uint16_t stepSize, uint8_t frameStepSize, float &out) {
  if(travelMs == 0 || stepSize == 0) return false;
  const float step = 100.0f / (static_cast<float>(travelMs) / static_cast<float>(stepSize * frameStepSize));
  out = dir > 0 ? std::min(100.0f, pos + step) : std::max(0.0f, pos - step);
  return true;
}
void TxHold::mark(uint32_t jobSeq, bool moving, uint32_t now) {
  if(jobSeq == 0 || this->seq != 0 || moving) return;
  this->seq = jobSeq;
  this->since = now;
}
bool TxHold::release(uint32_t jobSeq) {
  if(jobSeq == 0 || jobSeq != this->seq) return false;
  this->seq = 0;
  return true;
}
TxHoldCheck TxHold::check(bool moving, uint32_t now, uint32_t timeoutMs) {
  if(!this->held()) return TxHoldCheck::none;
  if(!moving) {
    // The command didn't set the shade moving so there is nothing to hold.
    this->clear();
    return TxHoldCheck::none;
  }
  if(!this->timedOut(now, timeoutMs)) return TxHoldCheck::holding;
  this->clear();
  return TxHoldCheck::expired;
}
void ShadeTxState::stopSent(uint32_t seq, uint32_t now, int8_t dir, bool tilt) {
  this->stopSeq = seq;
  this->stopAt = now;
  this->stopDir = dir;
  this->stopTilt = tilt;
}
void ShadeTxState::setMySent(uint32_t seq, float oldPos, float oldTiltPos) {
  this->mySeq = seq;
  this->myOldPos = oldPos;
  this->myOldTiltPos = oldTiltPos;
}
ShadeTxActions ShadeTxState::onEvent(const somfy_tx_event_t &evt, bool moving) {
  ShadeTxActions act;
  if(evt.seq == 0) return act;
  if(this->hold.release(evt.seq)) {
    if(evt.type == tx_event_t::started) {
      act.startMovement = true;
      act.startAt = evt.at;
    }
    else act.cancelMovement = true;
  }
  if(this->mySeq == evt.seq && evt.type != tx_event_t::started) {
    this->mySeq = 0;
    // The motor only stores the favorite when it gets the whole long press.
    if(evt.type == tx_event_t::finished) act.commitMy = true;
    else {
      act.restoreMy = true;
      act.myOldPos = this->myOldPos;
      act.myOldTiltPos = this->myOldTiltPos;
    }
  }
  if(this->stopSeq == evt.seq) {
    this->stopSeq = 0;
    // Unless something has moved the shade since, the motor never got the stop
    // and is still running.
    if(evt.type == tx_event_t::dropped && !moving) {
      act.runToLimit = true;
      act.limitDir = this->stopDir;
      act.limitTilt = this->stopTilt;
      act.limitSince = this->stopAt;
    }
  }
  return act;
}
