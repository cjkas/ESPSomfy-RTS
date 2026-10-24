#include "SomfyWaveform.h"

static somfy_symbol_t symbol(uint8_t level0, uint16_t us0, uint8_t level1, uint16_t us1) {
  somfy_symbol_t s;
  s.level0 = level0;
  s.duration0 = us0;
  s.level1 = level1;
  s.duration1 = us1;
  return s;
}
somfy_waveform_t buildWaveform(const uint8_t *payload, uint8_t sync, uint8_t bitLength, somfy_symbol_t *out, uint16_t capacity) {
  somfy_waveform_t wave;
  // The payload holds 80 bits.
  if(bitLength > 80) return wave;
  const bool first = isFirstFrame(sync);
  // One symbol is kept for the driver's end marker.
  const uint16_t fixed = waveformSymbols(0, bitLength) + (first ? 1 : 0) + 1;
  if(capacity < fixed) return wave;
  wave.sync = capacity - fixed < sync ? (uint8_t)(capacity - fixed) : sync;
  uint16_t n = 0;
  if(first) out[n++] = symbol(1, WAKEUP_HIGH_US, 0, WAKEUP_LOW_US);
  for(uint8_t i = 0; i < wave.sync; i++) out[n++] = symbol(1, 4 * SYMBOL, 0, 4 * SYMBOL);
  // The software sync, then the low half of a 0 that starts the payload.
  out[n++] = symbol(1, SOFTWARE_SYNC_US, 0, SYMBOL);
  // Manchester bits: a 1 rises in the middle, a 0 falls.
  for(uint8_t i = 0; i < bitLength; i++) {
    if((payload[i / 8] >> (7 - (i % 8))) & 1) out[n++] = symbol(0, SYMBOL, 1, SYMBOL);
    else out[n++] = symbol(1, SYMBOL, 0, SYMBOL);
  }
  // The pin goes low after the last symbol either way.
  if(bitLength != 80) out[n++] = symbol(0, FRAME_GAP_US / 2, 0, FRAME_GAP_US - FRAME_GAP_US / 2);
  wave.symbols = n;
  return wave;
}
