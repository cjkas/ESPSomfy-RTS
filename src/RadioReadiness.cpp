#include "RadioReadiness.h"

float radioFrequencyMHz(uint32_t freqWord) {
  return freqWord * (26.0f / 65536.0f);
}

bool radioRegistersReady(const radio_readings_t &r, float requestedMHz) {
  const float actual = radioFrequencyMHz(r.freqWord);
  return r.partnum == 0 && r.version != 0 && r.version != 0xFF &&
    r.iocfg0 == 0x0D && r.iocfg2 == 0x0D && (r.pktctrl0 & 0x30) == 0x30 &&
    (r.mdmcfg2 & 0x70) == 0x30 && actual > requestedMHz - 0.01f && actual < requestedMHz + 0.01f;
}

rx_outcome_t radioRxOutcome(uint8_t marcstate, uint8_t attempt, uint8_t maxAttempts) {
  if((marcstate & 0x1F) == RADIO_MARCSTATE_RX) return rx_outcome_t::ready;
  return attempt + 1 < maxAttempts ? rx_outcome_t::retry : rx_outcome_t::unavailable;
}

bool radioMayTransmit(bool integratedRadio, bool radioAvailable) {
  return !integratedRadio || radioAvailable;
}

const char *radioRefusalReason(bool enabled, bool radioAvailable) {
  if(!enabled) return "The radio is disabled";
  if(!radioAvailable) return "The radio did not respond";
  return nullptr;
}

bool repeaterFrameDropped(bool txSuspended, bool integratedRadio, bool radioAvailable) {
  return txSuspended || !radioMayTransmit(integratedRadio, radioAvailable);
}
