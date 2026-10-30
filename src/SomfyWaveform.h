#ifndef SOMFY_WAVEFORM_H
#define SOMFY_WAVEFORM_H
#include <stdint.h>
#include "PulseDecoder.h"

// The TX pin levels for one RTS frame, as RMT symbols with a 1us tick.  Kept
// free of the radio and the RMT driver so the timings can be unit tested on
// the PC (pio test -e native).  See docs/adr/0004-rmt-transmit.md.

// The wakeup pulse before the first frame of a command.  There is no silence
// after it: a Telis showed none, and battery powered motors appear to go back
// to sleep during one.
#define WAKEUP_HIGH_US 10920
#define WAKEUP_LOW_US 7357
#define SOFTWARE_SYNC_US 4850
// Silence after a 56-bit frame, about 27.5ms measured from a remote.  80-bit
// frames have none (#84).
#define FRAME_GAP_US 27434

// Same layout as the RMT driver's rmt_symbol_word_t: two levels and how many
// ticks each lasts.
union somfy_symbol_t {
  struct {
    uint16_t duration0 : 15;
    uint16_t level0 : 1;
    uint16_t duration1 : 15;
    uint16_t level1 : 1;
  };
  uint32_t val;
};

// The first frame of a command has 2 (56-bit) or 12 (80-bit) hardware syncs and
// a wakeup pulse.  Repeats have 7 or 6 and none.
constexpr bool isFirstFrame(uint8_t sync) { return sync == 2 || sync == 12; }
// Symbols buildWaveform() writes for a frame, not counting the end marker the
// RMT driver adds after them.
constexpr uint16_t waveformSymbols(uint8_t sync, uint8_t bitLength) {
  return (isFirstFrame(sync) ? 1 : 0) + sync + 1 + bitLength + (bitLength == 80 ? 0 : 1);
}

struct somfy_waveform_t {
  uint16_t symbols = 0;  // Written, 0 when the frame can't be sent.
  uint8_t sync = 0;      // Hardware syncs sent, fewer than asked when they didn't fit.
};
// Writes the frame to out, which holds capacity symbols including the
// driver's end marker.  Hardware syncs that don't fit are left out so the
// frame still goes out.  bitLength bits of payload are sent, MSB first.
somfy_waveform_t buildWaveform(const uint8_t *payload, uint8_t sync, uint8_t bitLength, somfy_symbol_t *out, uint16_t capacity);
#endif
