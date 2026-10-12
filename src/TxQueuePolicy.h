#ifndef TX_QUEUE_POLICY_H
#define TX_QUEUE_POLICY_H
#include <stdint.h>
#include "TxBuffer.h"

// The rules the radio TX queue follows (src/Somfy.cpp).  Kept free of FreeRTOS
// so they can be unit tested on the PC (pio test -e native).

// Whether a job fits with spaces slots free.  Normal jobs leave the reserved
// slots free for stops.
inline bool txAdmits(uint32_t spaces, bool priority) {
  return spaces > (priority ? 0u : (uint32_t)TX_RESERVED_SLOTS);
}
// The sequence number after seq.  0 means no job so it is skipped on wrap.
inline uint32_t nextTxSeq(uint32_t seq) {
  if(++seq == 0) ++seq;
  return seq;
}
// Whether job seq was queued after job cur, allowing for wrap.
inline bool txSeqAfter(uint32_t seq, uint32_t cur) { return (int32_t)(seq - cur) > 0; }
// Whether a job queued at the back is still waiting, given taken is the last
// back job the radio task took or a flush dropped.  Those go out in order.
inline bool txSeqQueued(uint32_t seq, uint32_t taken) { return seq != 0 && txSeqAfter(seq, taken); }
// A stop jumps the queue unless an earlier frame of its remote is still
// waiting.  Overtaking it would put the rolling codes out of order and the
// motor would reject the older one.
inline bool stopGoesToFront(bool priority, bool lastBackQueued) { return priority && !lastBackQueued; }
// Milliseconds until a delayed job is due, 0 or less once it is.  Works
// across a millis() wrap.
inline int32_t txDueInMs(uint32_t notBefore, uint32_t now) { return (int32_t)(notBefore - now); }
#endif
