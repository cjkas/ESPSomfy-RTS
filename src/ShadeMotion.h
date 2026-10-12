#ifndef SHADE_MOTION_H
#define SHADE_MOTION_H
#include <Arduino.h>

// Shade position maths.  Kept free of the shade state, radio and network so it
// can be unit tested on the PC (pio test -e native).

// Where a shade (or its tilt) is after travelling elapsedMs from startPos
// (0-100).  dir > 0 moves toward 100 and dir < 0 toward 0.  travelMs is the
// time a full travel takes in that direction, 0 meaning it gets there at once.
float travelPosition(float startPos, int8_t dir, uint32_t elapsedMs, int32_t travelMs);
// A position as reported to clients: -1 when unset, otherwise the whole
// percentage, counted from the other end when flipped.
int8_t displayPosition(float pos, bool flip);

// The radio job that set an idle shade moving.  Its simulated movement is held
// until the job goes on air, since that is when the motor starts.
struct TxHold {
  uint32_t seq = 0;    // 0 when nothing is held.
  uint32_t since = 0;  // millis() when the hold started.
  // Holds for job seq unless the shade was already moving (its motor carries on
  // with the earlier command) or a job is already held.  The first job held is
  // the one that starts the motor.  A command sent while processing another
  // registers before it: for a simMy shade My/Favorite queues its Up/Down from
  // moveToMyPosition() first, and that Up/Down is what starts a motor with no
  // favorite of its own, so the outer My/Favorite is ignored on purpose.
  void mark(uint32_t jobSeq, bool moving, uint32_t now);
  // Ends the hold when jobSeq is the held job.  True when it was.
  bool release(uint32_t jobSeq);
  bool held() const { return this->seq != 0; }
  bool timedOut(uint32_t now, uint32_t timeoutMs) const { return now - this->since >= timeoutMs; }
  void clear() { this->seq = 0; }
};
#endif
