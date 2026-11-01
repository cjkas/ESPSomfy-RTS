#ifndef SOMFY_H
#define SOMFY_H
#include <Preferences.h>
#include "ConfigSettings.h"

#include "model/ShadeModel.h"
#include "RadioScheduler.h"
#include "Transceiver.h"

struct somfy_relay_t {
  uint32_t remoteAddress = 0;
  uint8_t sync = 0;
  byte frame[10] = {0};
};
class SomfyShadeController : public ShadeModel {
  protected:
    uint8_t m_shadeIds[SOMFY_MAX_SHADES];
    uint32_t lastCommit = 0;
  public:
    SomfyShadeController();
    Transceiver transceiver;
    bool deleteRoom(uint8_t roomId);
    bool deleteShade(uint8_t shadeId);
    bool deleteGroup(uint8_t groupId);
    bool begin();
    void loop();
    void end();
    void updateGroupFlags();
    // Returns the radio job's ticket, 0 when the radio can't take it.
    uint32_t sendFrame(somfy_frame_t &frame, uint8_t repeats = 0, uint16_t delayMs = 0, bool priority = false);
    // skip, when set, is left out: a shade that hears its own frame.
    void processFrame(somfy_frame_t &frame, bool internal = false, const SomfyRemote *skip = nullptr);
    void markTxPending(uint32_t remoteAddress, uint32_t ticket, const SomfyRemote *skip = nullptr);
    void processTxEvent(const somfy_tx_event_t &evt);
    // The change fan-out: sends every shade and group that changed since the
    // last pass, its changed MQTT topics and one state event.  loop() only.
    void sendChanges();
    void publish();
    void processWaitingFrame();
    void commit();
    void writeBackup();
    String backupData;
};

#endif
