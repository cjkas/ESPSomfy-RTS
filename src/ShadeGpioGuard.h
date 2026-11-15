#ifndef SHADE_GPIO_GUARD_H
#define SHADE_GPIO_GUARD_H
#include "BoardProfile.h"
#include "SomfyFrame.h"

// A backup may carry GPIO assignments from another board.  Whether a shade's
// outputs would drive a pad the board's own hardware uses.  Header only so the
// native tests can build it for a given board; free of the shade model.
inline bool shadeOutputsUseBoardPins(radio_proto proto, uint8_t up, uint8_t down, uint8_t my) {
  if(proto != radio_proto::GP_Relay && proto != radio_proto::GP_Remote) return false;
  return BoardProfile::usesPin(up) || BoardProfile::usesPin(down) ||
    (proto == radio_proto::GP_Remote && BoardProfile::usesPin(my));
}
#endif
