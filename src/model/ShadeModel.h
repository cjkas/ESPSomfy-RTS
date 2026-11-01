#ifndef SHADE_MODEL_H
#define SHADE_MODEL_H
#include <ArduinoJson.h>
#include "SomfyFrame.h"
#include "TxBuffer.h"
#include "ShadeTypes.h"
#include "ShadeSettings.h"
#include "ShadeBehaviour.h"
#include "ShadeMotion.h"
#include "ShadeSensors.h"
#include "ShadeChanges.h"

// The shade model: rooms, shades, groups and their remotes, and what happens
// between them when a frame is sent or heard.  Kept free of the radio,
// settings and network so it can be unit tested on the PC (pio test -e
// native); rolling codes go to NVS through Preferences (ADR 0001).  Only
// loop() calls it (ADR 0002).

class ShadeModel;

enum class gpio_flags_t : byte {
  LowLevelTrigger = 0x01
};

class SomfyRoom {
  public:
    uint8_t roomId = 0;
    char name[21] = "";
    int8_t sortOrder = 0;
    void clear();
    bool fromJSON(JsonObject &obj);
    void publish();
    void unpublish();
};

class SomfyRemote : public RemoteSettings {
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
    uint16_t lastRollingCode = 0;
    virtual bool isLastCommand(somfy_commands cmd);
    char *getRemotePrefId() {return m_remotePrefId;}
    virtual void setRemoteAddress(uint32_t address);
    virtual uint32_t getRemoteAddress();
    uint16_t peekNextRollingCode();
    void commitRollingCode(uint16_t code);
    virtual uint16_t setRollingCode(uint16_t code);
    // The flags byte (somfy_flags_t) as it is saved and sent.
    virtual uint8_t getFlags() const { return 0; }
    virtual uint16_t p_lastRollingCode(uint16_t code);
    somfy_commands transformCommand(somfy_commands cmd);
};
class SomfyLinkedRemote : public SomfyRemote {
  public:
    SomfyLinkedRemote();
};
// A shade: its identity, settings, remotes and outputs.  What it does with
// frames and commands is its ShadeBehaviour; the shade sends for it through
// the model (MotorLink) and passes on what it reports (ShadeEvents).
class SomfyShade : public SomfyRemote, public ShadeSettings, private MotorLink, private ShadeEvents {
  friend class ShadeModel;
  protected:
    uint8_t shadeId = 255;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    bool light = false;
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
    bool paired = false;
    int8_t validateJSON(JsonObject &obj);
    int8_t fromJSON(JsonObject &obj);

    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() const { return shadeId; }
    // Where the shade is and where it is going.
    const ShadeBehaviour::State &state() const { return this->behaviour.state(); }
    bool isIdle() const { return this->behaviour.isIdle(); }
    uint8_t getFlags() const override;
    // Sets the setting and state bits from a saved or sent flags byte.
    void setFlags(uint8_t flags);
    bool isInGroup();
    // The shade itself or the linked remote with this address, else nullptr.
    SomfyRemote *findRemote(uint32_t address);
    bool isToggle() { return isToggleShade(this->shadeType); }
    bool moveToTarget(float pos, float tilt = -1.0f);
    // These return false when the radio refused a command they sent.
    bool moveToTiltTarget(float target);
    bool sendTiltCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd);
    bool sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    // Puts the shade at pos and tilt (-1 leaves one alone) without moving it.
    void setPosition(float pos, float tilt);
    // Where the config file says the shade was.
    void restore(float pos, float tiltPos, float myPos, float myTiltPos) { this->behaviour.restore(pos, tiltPos, myPos, myTiltPos); }
    // Forgets the tilt of a shade that has none.
    void clearTilt() { this->behaviour.clearTilt(); }
    // The caller saves the change.
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    // Marks the shade as changed; the change fan-out sends shadeState once per
    // loop() pass.
    void emitState() { this->changed |= SHADE_CHANGED_STATE; }
    // What changed since the fan-out last sent the shade (SHADE_CHANGED_* bits).
    uint16_t changed = 0;
    // A setting changed: protocol 2 clients get the whole shade.
    void configChanged() { this->changed |= SHADE_CHANGED_CONFIG; }
    void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress);
    // False when the radio refused a command it sent.
    bool setMyPosition(int8_t pos, int8_t tilt = -1);
    bool moveToMyPosition();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    // Publishes the MQTT topics of the SHADE_CHANGED_* bits in changed.
    void publishTopics(uint16_t changed);
    void clear();
    int8_t transformPosition(float fpos);
    void setGPIOs();
    // Pulses the GP_Remote outputs for frame.
    void triggerGPIOs(const somfy_frame_t &frame);
    bool usesPin(uint8_t pin);
    uint16_t p_lastRollingCode(uint16_t code);
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
    void publishDisco();
    void unpublishDisco();
  private:
    // The model the shade is in, set by its constructor.
    ShadeModel *model = nullptr;
    // What the model hands the shade.
    void processFrame(somfy_frame_t &frame, bool internal, uint32_t now);
    void processWaitingFrame(uint32_t now);
    void processInternalCommand(somfy_commands cmd, uint8_t repeat, uint32_t now);
    void checkMovement(uint32_t now);
    void markTxPending(uint32_t ticket, uint32_t now);
    void processTxEvent(const somfy_tx_event_t &evt);
    // MotorLink: the radio or GPIO path every remote sends through.
    send_result_t send(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority, uint32_t now) override;
    // ShadeEvents
    void onChanged(uint16_t bits) override { this->changed |= bits; }
    void onCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress) override;
    void onSaveWanted() override;
    void onSensorFlags() override;
    ShadeBehaviour behaviour{*this, *this, this->lastFrame, *this, *this, this->shadeId, this->name};
};
class SomfyGroup : public SomfyRemote {
  protected:
    uint8_t groupId = 255;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    group_types groupType = group_types::channel;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    // Its linked shades' flags together as last worked out, see
    // ShadeModel::groupFlags.
    uint8_t flags = 0;
    uint8_t getFlags() const override { return this->flags; }
    bool hasSunSensor() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::SunSensor)) > 0; }
    char name[21] = "";
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    void setGroupId(uint8_t id) { groupId = id; }
    uint8_t getGroupId() const { return groupId; }
    void clear();
    bool fromJSON(JsonObject &obj);
    bool linkShade(uint8_t shadeId);
    bool unlinkShade(uint8_t shadeId);
    bool hasShadeId(uint8_t shadeId) const;
    void compressLinkedShadeIds();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    // Marks the group as changed; the change fan-out sends groupState once per
    // loop() pass.
    void emitState() { this->changed |= GROUP_CHANGED_STATE; }
    // What changed since the fan-out last sent the group (GROUP_CHANGED_* bits).
    uint8_t changed = 0;
    // A setting or its shades changed: protocol 2 clients get the whole group.
    void configChanged() { this->changed |= GROUP_CHANGED_CONFIG; }
    int8_t p_direction(int8_t dir);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
};

// How the model's frames reach the air.
class RadioPort {
  public:
    // Queues job.  Returns its ticket, 0 when the radio can't take it.
    virtual uint32_t submit(somfy_tx_job_t &job, bool priority, uint16_t delayMs) = 0;
    // A GP_Remote shade's command: pulses its outputs instead.
    virtual void pulse(SomfyShade &shade, const somfy_frame_t &frame) = 0;
};
// What the model tells the rest of the firmware, as it happens.
class ModelEvents {
  public:
    // A command reached the shade.  source is "remote", "group" and so on.
    virtual void onCommand(SomfyShade &shade, somfy_commands cmd, const char *source, uint32_t sourceAddress) = 0;
};

class ShadeModel {
  friend class SomfyShade;
  public:
    ShadeModel(RadioPort &radio, ModelEvents &events);
    ShadeModel(const ShadeModel &) = delete;
    ShadeModel &operator=(const ShadeModel &) = delete;
    // Something worth saving changed; loop() writes the config file.
    bool isDirty = false;
    // The first remote address offered to a new shade or group.
    uint32_t startingAddress = 0;
    // The radio's bit length and protocol, for shades and groups without their
    // own.
    void setRadioDefaults(uint8_t bitLength, radio_proto proto);
    uint8_t radioBitLength() const { return this->bitLength; }
    radio_proto radioProto() const { return this->proto; }

    // A frame the radio heard.
    void onFrame(somfy_frame_t &frame, uint32_t now);
    // A receive pass with no frame: a held remote button may have been let go.
    void onIdle(uint32_t now);
    // A radio job started, finished, was aborted or dropped.
    void onTxEvent(const somfy_tx_event_t &evt);
    // Every loop() pass: moves the simulated positions.
    void tick(uint32_t now);

    // Sends cmd as the group and moves its shades.  These return false when the
    // radio refused the command; nothing has changed then.
    bool sendGroupCommand(SomfyGroup &group, somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint32_t now);
    // A sensor frame from the shade or group, as a sun and wind sensor would
    // send it.  -1 leaves a flag as it is.
    bool sendSensorCommand(SomfyRemote &remote, int8_t isWindy, int8_t isSunny, uint8_t repeat, uint32_t now);
    // Sends the last frame again.
    bool repeatFrame(SomfyShade &shade, uint8_t repeat);
    bool repeatFrame(SomfyGroup &group, uint8_t repeat);

    uint8_t getNextRoomId();
    uint8_t getNextShadeId();
    uint8_t getNextGroupId();
    // The remote address a new shade or group with that id is offered: one no
    // shade or group uses.
    uint32_t getNextRemoteAddress(uint8_t id);
    int8_t getMaxRoomOrder();
    int8_t getMaxShadeOrder();
    int8_t getMaxGroupOrder();
    SomfyRoom *addRoom();
    SomfyShade *addShade();
    SomfyGroup *addGroup();
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0};
    SomfyRoom rooms[SOMFY_MAX_ROOMS];
    SomfyShade shades[SOMFY_MAX_SHADES];
    SomfyGroup groups[SOMFY_MAX_GROUPS];
    bool linkRepeater(uint32_t address);
    bool unlinkRepeater(uint32_t address);
    bool isRepeater(uint32_t address) const;
    uint8_t repeaterCount();
    uint8_t roomCount();
    uint8_t shadeCount();
    uint8_t groupCount();
    SomfyShade * getShadeById(uint8_t shadeId);
    SomfyRoom * getRoomById(uint8_t roomId);
    SomfyGroup * getGroupById(uint8_t groupId);
    SomfyShade * findShadeByRemoteAddress(uint32_t address);
    SomfyGroup * findGroupByRemoteAddress(uint32_t address);
    bool isInGroup(const SomfyShade &shade) const;
    // The group's shades' flags together.
    uint8_t groupFlags(const SomfyGroup &group) const;
    // Works every group's flags out again; a group whose flags changed is
    // marked changed.
    void updateGroupFlags();
  private:
    RadioPort &radio;
    ModelEvents &events;
    uint8_t bitLength = 56;
    radio_proto proto = radio_proto::RTS;
    // Sends cmd as from with its next rolling code, then every shade but skip
    // hears the frame and the shades it sets moving wait for it to go on air.
    // pulseAs is the shade whose outputs a GP_Remote command pulses.
    send_result_t send(SomfyRemote &from, SomfyShade *pulseAs, somfy_commands cmd, uint8_t repeat, uint8_t stepSize,
        uint16_t delayMs, bool priority, const SomfyRemote *skip, uint32_t now);
    bool repeatFrame(SomfyRemote &remote, SomfyShade *pulseAs, uint8_t repeat);
    // Returns the radio job's ticket, 0 when the radio can't take it.
    uint32_t submitFrame(somfy_frame_t &frame, uint8_t repeat, uint16_t delayMs, bool priority);
    // skip, when set, is left out: a shade that hears its own frame.
    void hear(somfy_frame_t &frame, bool internal, const SomfyRemote *skip, uint32_t now);
    void markTxPending(uint32_t remoteAddress, uint32_t ticket, const SomfyRemote *skip, uint32_t now);
    void updateGroupFlags(SomfyGroup &group);
    const SomfyShade *findShade(uint8_t shadeId) const;
};
#endif
