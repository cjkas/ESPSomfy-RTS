#ifndef TX_BUFFER_H
#define TX_BUFFER_H
#include <Arduino.h>

// Repeater frames waiting to be handed to the radio task.  Kept free of the
// radio and timers so it can be unit tested on the PC (pio test -e native).

#define MAX_TX_BUFFER 5

struct somfy_tx_t {
  void clear() {
    this->hwsync = 0;
    this->bit_length = 0;
    memset(this->payload, 0x00, sizeof(this->payload));
  }
  uint8_t hwsync = 0;
  uint8_t bit_length = 0;
  uint8_t payload[10] = {};
};
struct somfy_tx_queue_t {
  somfy_tx_queue_t() { this->clear(); }
  void clear() {
    for (uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
      this->index[i] = 255;
      this->items[i].clear();
    }
    this->length = 0;
  }
  unsigned long delay_time = 0;  // Set by the caller, not before this millis().
  uint8_t length = 0;
  uint8_t index[MAX_TX_BUFFER] = {255};
  somfy_tx_t items[MAX_TX_BUFFER];
  // The oldest frame without removing it, so it can stay queued until the radio
  // takes it.
  bool peek(somfy_tx_t *tx) const;
  bool pop(somfy_tx_t *tx);
  // When full the oldest frame is thrown away.
  void push(uint8_t hwsync, byte *payload, uint8_t bit_length);
  private:
    // Position in index of the oldest frame, -1 when empty.
    int8_t oldest() const;
};
#endif
