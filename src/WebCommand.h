#ifndef WEB_COMMAND_H
#define WEB_COMMAND_H
#include <stdint.h>
#include "SomfyFrame.h"

// Commands the web servers hand to the main loop (src/Web.cpp).  Kept free of
// the web server and FreeRTOS so it can be unit tested on the PC
// (pio test -e native).

// Commands are queued by the async web server and drained by the main loop.
// Big enough for Home Assistant firing a command at every shade at once.
#define WEB_CMD_QUEUE_SIZE 32

enum class web_cmd_t : uint8_t {
    shade_command,      // moveToTarget or sendCommand
    group_command,      // group sendCommand
    tilt_command,       // moveToTiltTarget or sendTiltCommand
    shade_repeat,       // shade sendCommand/repeatFrame
    group_repeat,       // group sendCommand/repeatFrame
    set_positions,      // set shade position directly
    shade_sensor,       // shade sensor command
    group_sensor,       // group sensor command
    set_my_position,    // shade setMyPosition (position/tiltPosition)
    unpair_shade,       // shade unpair (Prog) and clear paired flag
};

struct web_command_t {
    web_cmd_t type;
    uint8_t shadeId;
    uint8_t groupId;
    uint8_t target;         // 0-100 or 255 (none)
    somfy_commands command;
    int8_t repeat;
    uint8_t stepSize;
    int8_t position;        // for setPositions
    int8_t tiltPosition;    // for setPositions/tilt
    int8_t sunny;           // for sensor
    int8_t windy;           // for sensor
};

// Only setting positions never goes on air.
inline bool webCommandUsesRadio(web_cmd_t type) { return type != web_cmd_t::set_positions; }
// Whether the command at the head of the queue waits for the radio.  The
// client was already told it was accepted so it stays queued until the radio
// has room rather than have the radio drop it.  While TX is suspended nothing
// gets through anyway so nothing is held back then.
inline bool holdWebCommand(web_cmd_t type, bool txSuspended, bool txHasRoom) {
  return webCommandUsesRadio(type) && !txSuspended && !txHasRoom;
}
#endif
