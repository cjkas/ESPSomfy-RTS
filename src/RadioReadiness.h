#ifndef RADIO_READINESS_H
#define RADIO_READINESS_H
#include <stdint.h>

// Whether the integrated CC1101 is fit to use, and whether a queued job may
// still go on air.  Every register is read in Transceiver.cpp and passed in,
// so this is free of SPI and the RTOS and can be unit tested on the PC
// (pio test -e native).

// What the radio reported after it was set up.
struct radio_readings_t {
  uint8_t partnum = 0;   // CC1101_PARTNUM, 0 for a CC1101
  uint8_t version = 0;   // CC1101_VERSION, 0 and 0xFF mean no chip answered
  uint8_t iocfg0 = 0;    // GDO0, TX data
  uint8_t iocfg2 = 0;    // GDO2, RX data
  uint8_t pktctrl0 = 0;
  uint8_t mdmcfg2 = 0;
  uint32_t freqWord = 0; // FREQ2:FREQ1:FREQ0
};

// The carrier in MHz that FREQ2:FREQ1:FREQ0 selects with the 26 MHz crystal.
float radioFrequencyMHz(uint32_t freqWord);
// True when the registers say a CC1101 is wired up and configured for async
// OOK/ASK on the pins and frequency (MHz) we asked for.
bool radioRegistersReady(const radio_readings_t &r, float requestedMHz);

// CC1101_MARCSTATE value while receiving.
#define RADIO_MARCSTATE_RX 0x0D
// Tries at entering receive before the radio is given up as unavailable.
// One retry: go idle and call SetRx again, so a single missed switch after a
// transmission does not disable the radio until reboot.
#define RADIO_RX_ATTEMPTS 2

enum class rx_outcome_t : uint8_t {
  ready,       // the radio is receiving
  retry,       // not receiving yet, and another try is allowed
  unavailable  // not receiving and no tries are left
};
// What to do after try number `attempt` (counting from 0) at entering receive
// left the radio in `marcstate`.  Only the 5 state bits are looked at.  A
// caller allowed N tries passes maxAttempts = N.
rx_outcome_t radioRxOutcome(uint8_t marcstate, uint8_t attempt, uint8_t maxAttempts);

// Whether a job for the radio may be queued or sent.  A generic radio is
// always allowed, as before; the integrated one only once it is ready.
bool radioMayTransmit(bool integratedRadio, bool radioAvailable);

// Why a request that needs the radio (a frequency scan) is refused, or nullptr
// when it may go ahead.  Texts are the ones clients show.  A generic radio is
// available exactly when it is enabled, so it only ever gets the first one.
const char *radioRefusalReason(bool enabled, bool radioAvailable);

// Whether a queued repeater frame is discarded instead of kept for another
// try: while transmitting is suspended, or the integrated radio is unavailable
// (a stale repeat must not go out when the radio comes back).
bool repeaterFrameDropped(bool txSuspended, bool integratedRadio, bool radioAvailable);

#endif
