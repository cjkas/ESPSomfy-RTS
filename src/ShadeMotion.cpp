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
