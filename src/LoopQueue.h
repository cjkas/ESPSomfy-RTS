#ifndef LOOP_QUEUE_H
#define LOOP_QUEUE_H
#include <stdint.h>
#include "ShadeRequests.h"

// The one queue through which other tasks hand work to loop(), which alone
// changes the shade model (docs/adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).
// The items and the hold rule are kept free of FreeRTOS so they can be unit
// tested on the PC (pio test -e native).

// Big enough for Home Assistant firing a command at every shade at once.
#define LOOP_QUEUE_SIZE 32

// What a restore_shades item restores, as bits of restoreParts.
#define RESTORE_SETTINGS    0x01
#define RESTORE_SHADES      0x02
#define RESTORE_NETWORK     0x04
#define RESTORE_TRANSCEIVER 0x08
#define RESTORE_REPEATERS   0x10
#define RESTORE_MQTT        0x20

enum class loop_item_type_t : uint8_t {
  shade_request,   // run by ShadeRequests
  restore_shades,  // restore from /shades.tmp, then reboot
  load_shades,     // replace the shades with /shades.tmp
  snapshot_fill,   // fill a read's ModelSnapshot
  model_edit       // apply an edit with ModelEditor
};
struct model_edit_t;
struct loop_item_t {
  loop_item_type_t type = loop_item_type_t::shade_request;
  shade_request_t request;        // shade_request
  uint8_t restoreParts = 0;       // restore_shades: RESTORE_* bits
  uint8_t slot = 0;               // snapshot_fill: the SnapshotSlots slot
  model_edit_t *edit = nullptr;   // model_edit: loop() runs and frees it
};

// Whether the item sends anything on air.
inline bool loopItemUsesRadio(const loop_item_t &item) {
  return item.type == loop_item_type_t::shade_request && shadeRequestUsesRadio(item.request.type);
}
// Whether the item at the head of the queue waits for the radio.  The client
// was already told it was accepted so it stays queued until the radio has room
// rather than have the radio drop it.  While TX is suspended nothing gets
// through anyway so nothing is held back then, except an unpair: the client was
// told the shade is unpaired, so it waits until TX resumes.
inline bool holdLoopItem(const loop_item_t &item, bool txSuspended, bool txHasRoom) {
  if(txSuspended) return item.type == loop_item_type_t::shade_request && item.request.type == shade_request_type_t::unpair;
  return loopItemUsesRadio(item) && !txHasRoom;
}

class LoopQueue {
  public:
    void begin();
    // Hands the item to loop() without waiting, so it is safe from any task,
    // including async_tcp.  False when the queue is full.
    bool submit(const loop_item_t &item);
    // Runs the waiting items in order.  Only from loop().
    void drain();
  private:
    void *queue = nullptr;
};
#endif
