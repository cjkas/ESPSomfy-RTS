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
// The shade model on the device: its radio, config file, GPIO outputs and
// MQTT and protocol 2 clients.
class SomfyShadeController : private RadioPort, private ModelEvents, public ShadeModel {
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
    // A raw frame from a protocol 2 client, sent as is: it changes nothing in
    // the shade model.  Returns the radio job's ticket, 0 when the radio can't
    // take it.
    uint32_t sendFrame(somfy_frame_t &frame, uint8_t repeats = 0, uint16_t delayMs = 0, bool priority = false);
    // The change fan-out: sends every shade and group that changed since the
    // last pass, its changed MQTT topics and one state event.  loop() only.
    void sendChanges();
    void publish();
    void commit();
    void writeBackup();
    String backupData;
  private:
    // RadioPort
    uint32_t submit(somfy_tx_job_t &job, bool priority, uint16_t delayMs) override;
    void pulse(SomfyShade &shade, const somfy_frame_t &frame) override;
    // ModelEvents
    void onCommand(SomfyShade &shade, somfy_commands cmd, const char *source, uint32_t sourceAddress) override;
};

#endif
