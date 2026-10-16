#ifndef SOMFY_H
#define SOMFY_H
#include <Preferences.h>
#include "ConfigSettings.h"
#include "WResp.h"

#include "SomfyFrame.h"
#include "TxBuffer.h"
#include "RadioScheduler.h"
#include "ShadeTypes.h"
#include "ShadeMotion.h"
#include "ShadeSensors.h"
#include "Transceiver.h"

enum class somfy_flags_t : byte {
    SunFlag = 0x01,
    SunSensor = 0x02,
    DemoMode = 0x04,
    Light = 0x08,
    Windy = 0x10,
    Sunny = 0x20,
    Lighted = 0x40,
    SimMy = 0x80
};
enum class gpio_flags_t : byte {
  LowLevelTrigger = 0x01
};
struct somfy_relay_t {
  uint32_t remoteAddress = 0;
  uint8_t sync = 0;
  byte frame[10] = {0};
};

class SomfyRoom {
  public:
    uint8_t roomId = 0;
    char name[21] = "";
    int8_t sortOrder = 0;
    void clear();
    bool save();
    bool fromJSON(JsonObject &obj);
    void emitState(const char *evt = "roomState");
    void emitState(uint8_t num, const char *evt = "roomState");
    void publish();
    void unpublish();
};

// What sending a command did.  ticket is the radio job carrying it, 0 when the
// command went out over GPIO instead.
struct send_result_t {
  bool sent = false;
  uint32_t ticket = 0;
  explicit operator bool() const { return this->sent; }
};
class SomfyRemote {
  // These sizes for the data have been
  // confirmed.  The address is actually 24bits
  // and the rolling code is 16 bits.
  protected:
    char m_remotePrefId[11] = "";
    uint32_t m_remoteAddress = 0;
    // lastRollingCode is known to match NVS for this address, so the next code
    // can be worked out without opening NVS.
    bool m_rollingCodeSynced = false;
  public:
    radio_proto proto = radio_proto::RTS;
    uint8_t gpioFlags = 0;
    int8_t gpioDir = 0;
    uint8_t gpioUp = 0;
    uint8_t gpioDown = 0;
    uint8_t gpioMy = 0;
    uint32_t gpioRelease = 0;
    somfy_frame_t lastFrame;
    bool flipCommands = false;
    uint16_t lastRollingCode = 0;
    uint8_t flags = 0;
    uint8_t bitLength = 0;
    uint8_t repeats = 1;
    virtual bool isLastCommand(somfy_commands cmd);
    char *getRemotePrefId() {return m_remotePrefId;}
    virtual void setRemoteAddress(uint32_t address);
    virtual uint32_t getRemoteAddress();
    uint16_t peekNextRollingCode();
    void commitRollingCode(uint16_t code);
    virtual uint16_t setRollingCode(uint16_t code);
    bool hasSunSensor();
    bool hasLight();
    bool simMy();
    void setSunSensor(bool bHasSensor);
    void setLight(bool bHasLight);
    void setSimMy(bool bSimMy);
    // The send functions return false only when the radio could not take the
    // command.  Nothing about the remote or shade state has changed in that case.
    virtual bool sendCommand(somfy_commands cmd);
    virtual bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    send_result_t sendCommandAfter(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority = false);
    bool sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat);
    bool repeatFrame(uint8_t repeat);
    virtual uint16_t p_lastRollingCode(uint16_t code);
    somfy_commands transformCommand(somfy_commands cmd);
    virtual void triggerGPIOs(somfy_frame_t &frame);
   
};
class SomfyLinkedRemote : public SomfyRemote {
  public:
    SomfyLinkedRemote();    
};
class SomfyShade : public SomfyRemote {
  protected:
    uint8_t shadeId = 255;
    uint64_t moveStart = 0;
    uint64_t tiltStart = 0;
    ShadeSensors sensors;
    float startPos = 0.0f;
    float startTiltPos = 0.0f;
    bool settingMyPos = false;
    bool settingPos = false;
    bool settingTiltPos = false;
    uint32_t awaitMy = 0;
    // Radio jobs whose outcome decides what the motor really did.
    ShadeTxState tx;
    bool sendStop(int8_t dir, bool tilt);
    bool sendSetMy(uint16_t delayMs, float oldPos, float oldTiltPos);
    // Makes pos/tiltPos the favorite and tells the motor to store it.
    bool storeMyPosition(float pos, float tiltPos);
    // Repeats for a tilt command, more for a separate tilt motor.
    uint8_t tiltRepeats() const;
    void runToLimit(int8_t dir, bool tilt, uint32_t since);
    // Sets the targets the sun or wind sends the shade to.
    void applySensorMove(SensorMove move);
    // Sets the targets for a step in dir.  False when the step is dropped.
    bool applyStep(int8_t dir, uint8_t frameStepSize);
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    bool flipPosition = false;
    shade_types shadeType = shade_types::roller;
    tilt_types tiltType = tilt_types::none;
    float currentPos = 0.0f;
    float currentTiltPos = 0.0f;
    int8_t lastMovement = 0;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    int8_t tiltDirection = 0; // 0=stopped, 1=clockwise, -1=counter clockwise
    float target = 0.0f;
    float tiltTarget = 0.0f;
    float myPos = -1.0f;
    float myTiltPos = -1.0f;
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
    bool paired = false;
    int8_t validateJSON(JsonObject &obj);
    int8_t fromJSON(JsonObject &obj);
    
    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() { return shadeId; }
    uint32_t upTime = 10000;
    uint32_t downTime = 10000;
    uint32_t tiltTime = 7000;
    uint16_t stepSize = 100;
    bool save();
    bool isIdle();
    bool isInGroup();
    void checkMovement();
    void processFrame(somfy_frame_t &frame, bool internal = false);
    // The shade itself or the linked remote with this address, else nullptr.
    SomfyRemote *findRemote(uint32_t address);
    void processInternalCommand(somfy_commands cmd, uint8_t repeat = 1);
    void markTxPending(uint32_t ticket);
    void processTxEvent(const somfy_tx_event_t &evt);
    void setTiltMovement(int8_t dir);
    void setMovement(int8_t dir);
    void setTarget(float target);
    bool isAtTarget();
    bool isToggle();
    bool moveToTarget(float pos, float tilt = -1.0f);
    // These return false when the radio refused a command they sent.
    bool moveToTiltTarget(float target);
    bool sendTiltCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    void emitState(const char *evt = "shadeState");
    void emitState(uint8_t num, const char *evt = "shadeState");
    void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt = "shadeCommand");
    void emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt = "shadeCommand");
    // False when the radio refused a command it sent.
    bool setMyPosition(int8_t pos, int8_t tilt = -1);
    bool moveToMyPosition();
    void processWaitingFrame();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    void commit();
    void commitShadePosition();
    void commitTiltPosition();
    void commitMyPosition();
    void clear();
    int8_t transformPosition(float fpos);
    void setGPIOs();
    void triggerGPIOs(somfy_frame_t &frame);
    bool usesPin(uint8_t pin);
    // State Setters
    int8_t p_direction(int8_t dir);
    int8_t p_tiltDirection(int8_t dir);
    float p_target(float target);
    float p_tiltTarget(float target);
    float p_myPos(float pos);
    float p_myTiltPos(float pos);
    bool p_flag(somfy_flags_t flag, bool val);
    bool p_sunFlag(bool val);
    bool p_sunny(bool val);
    bool p_windy(bool val);
    float p_currentPos(float pos);
    float p_currentTiltPos(float pos);
    uint16_t p_lastRollingCode(uint16_t code);
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
    void publishDisco();
    void unpublishDisco();
};
class SomfyGroup : public SomfyRemote {
  protected:
    uint8_t groupId = 255;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    group_types groupType = group_types::channel;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    char name[21] = "";
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    void setGroupId(uint8_t id) { groupId = id; }
    uint8_t getGroupId() { return groupId; }
    bool save();
    void clear();
    bool fromJSON(JsonObject &obj);
    bool linkShade(uint8_t shadeId);
    bool unlinkShade(uint8_t shadeId);
    bool hasShadeId(uint8_t shadeId);
    void compressLinkedShadeIds();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    void updateFlags();
    void emitState(const char *evt = "groupState");
    void emitState(uint8_t num, const char *evt = "groupState");
    bool sendCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    int8_t p_direction(int8_t dir);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
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
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void markTxPending(uint32_t remoteAddress, uint32_t ticket);
    void processTxEvent(const somfy_tx_event_t &evt);
    void emitState(uint8_t num = 255);
    void publish();
    void processWaitingFrame();
    void commit();
    void writeBackup();
    String backupData;
    bool loadShadesFile(const char *filename);
};

#endif
