#ifndef ROLLING_CODE_H
#define ROLLING_CODE_H
#include <stdint.h>

// Working out a remote's next rolling code (src/SomfyRemote.cpp).  Kept free of NVS
// so it can be unit tested on the PC (pio test -e native).

// The code the next frame uses.  It is only read here and consumed once the
// frame using it has been queued.  synced says last matches what is stored for
// this address, so the store is only read through loadStored until a code has
// been written.
template<typename LoadStored>
inline uint16_t nextRollingCode(bool synced, uint16_t last, LoadStored loadStored) {
  return (uint16_t)((synced ? last : loadStored()) + 1);
}
#endif
