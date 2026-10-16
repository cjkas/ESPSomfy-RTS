#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "esp_log.h"
#include "LoopQueue.h"
#include "ConfigFile.h"
#include "Somfy.h"
#include "SomfySnapshot.h"
#include "Utils.h"

static const char *TAG = "LoopQueue";

extern SomfyShadeController somfy;
extern ShadeRequests shadeRequests;
extern SnapshotSlots snapshotSlots;
extern rebootDelay_t rebootDelay;

static restore_options_t restoreOptions(uint8_t parts) {
  restore_options_t opts;
  opts.settings = parts & RESTORE_SETTINGS;
  opts.shades = parts & RESTORE_SHADES;
  opts.network = parts & RESTORE_NETWORK;
  opts.transceiver = parts & RESTORE_TRANSCEIVER;
  opts.repeaters = parts & RESTORE_REPEATERS;
  opts.mqtt = parts & RESTORE_MQTT;
  return opts;
}

void LoopQueue::begin() {
  if(!this->queue) this->queue = xQueueCreate(LOOP_QUEUE_SIZE, sizeof(loop_item_t));
}
bool LoopQueue::submit(const loop_item_t &item) {
  if(!this->queue) return false;
  // async_tcp also services the sockets, so it must never wait on loop() or
  // the radio.
  if(xQueueSend(static_cast<QueueHandle_t>(this->queue), &item, 0) != pdTRUE) {
    ESP_LOGE(TAG, "Loop queue full, dropping item");
    return false;
  }
  return true;
}
void LoopQueue::drain() {
  if(!this->queue) return;
  QueueHandle_t q = static_cast<QueueHandle_t>(this->queue);
  loop_item_t item;
  // Items run in order, so only the one at the head can be held back.
  while(xQueuePeek(q, &item, 0) == pdTRUE) {
    if(holdLoopItem(item, somfy.transceiver.txSuspended(), somfy.transceiver.txHasRoom())) break;
    // loop() is the only consumer so this is the item we peeked.
    xQueueReceive(q, &item, 0);
    switch(item.type) {
      case loop_item_type_t::shade_request:
        if(!shadeRequests.run(item.request))
          ESP_LOGW(TAG, "Radio didn't take shade request %u", static_cast<uint8_t>(item.request.type));
        break;
      case loop_item_type_t::restore_shades: {
        restore_options_t opts = restoreOptions(item.restoreParts);
        ShadeConfigFile::restore(&somfy, "/shades.tmp", opts);
        ESP_LOGI(TAG, "Rebooting ESP for restored settings...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 1000;
        break;
      }
      case loop_item_type_t::load_shades:
        somfy.loadShadesFile("/shades.tmp");
        break;
      case loop_item_type_t::snapshot_fill:
        snapshotSlots.fill(item.slot, fillSnapshot);
        break;
    }
  }
}
