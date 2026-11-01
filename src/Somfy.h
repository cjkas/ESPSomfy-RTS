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
class SomfyShadeController {
  protected:
    uint8_t m_shadeIds[SOMFY_MAX_SHADES];
    uint32_t lastCommit = 0;
  public:
    bool isDirty = false;
    uint32_t startingAddress;
    uint8_t getNextRoomId();
    uint8_t getNextShadeId();
    uint8_t getNextGroupId();
    // The remote address a new shade or group with that id is offered: one no
    // shade or group uses.
    uint32_t getNextRemoteAddress(uint8_t id);
    int8_t getMaxRoomOrder();
    int8_t getMaxShadeOrder();
    int8_t getMaxGroupOrder();
    SomfyShadeController();
    Transceiver transceiver;
    SomfyRoom *addRoom();
    SomfyShade *addShade();
    SomfyGroup *addGroup();
    bool deleteRoom(uint8_t roomId);
    bool deleteShade(uint8_t shadeId);
    bool deleteGroup(uint8_t groupId);
    bool begin();
    void loop();
    void end();
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0};
    SomfyRoom rooms[SOMFY_MAX_ROOMS];
    SomfyShade shades[SOMFY_MAX_SHADES];
    SomfyGroup groups[SOMFY_MAX_GROUPS];
    bool linkRepeater(uint32_t address);
    bool unlinkRepeater(uint32_t address);
    uint8_t repeaterCount();
    uint8_t roomCount();
    uint8_t shadeCount();
    uint8_t groupCount();
    void updateGroupFlags();
    SomfyShade * getShadeById(uint8_t shadeId);
    SomfyRoom * getRoomById(uint8_t roomId);
    SomfyGroup * getGroupById(uint8_t groupId);
    SomfyShade * findShadeByRemoteAddress(uint32_t address);
    SomfyGroup * findGroupByRemoteAddress(uint32_t address);
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
