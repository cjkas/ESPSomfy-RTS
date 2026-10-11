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
#endif
