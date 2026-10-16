# Plan: domain modules behind the LoopQueue

**Status:** steps 0–2 built (PRs #39, #40 and #41); steps 3–5 planned.
**Written:** 2026-10-15, against commit `e624a41` on `refactor/entry-points-outputs`. Terms are defined in [GLOSSARY.md](../GLOSSARY.md); the threading rule is [ADR-0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md).

## Why

- **Two translators for the same requests.** HTTP (`Web::processQueue`) and MQTT (`MQTTClass::receive`) each turn a request into shade calls, and they have drifted apart:
  - MQTT `tiltTarget`, `mypos` and `tiltPosition` ignore `flipPosition`;
  - `/setPositions` ignores it the other way round;
  - `repeat=0` means "default" for shades but "no repeats" for groups.
- **Writers on the web task.** Model edits (`/addShade`, `/linkRemote`, `/setPaired`, ...) and every read (`/controller`, `/shades`, `/discovery`, ...) touch the shade model on the async_tcp task while `loop()` changes it. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and ADR-0002.
- **Publishing depends on discipline.** Each `p_*` setter publishes its own MQTT topic, and `emitState()` calls are placed by hand. Writes that skip a setter are never published, and MQTT `position` has been a constant 50 since `c94819e`.
- **No native tests.** None of this compiles in the native env, so it has no tests.

## Shape

```
WebUiRoutes ─┐                       ┌─► ShadeRequests ─► shades/groups ─► radio
ApiRoutes  ──┼─► LoopQueue ─► loop() ┼─► ModelEditor   ─► shade model
MqttCommands ┘   (one FIFO)          └─► ModelSnapshot fill ─► read reply (chunked)
                                               │ dirty bits, once per loop pass
                                               ▼
                                    MqttPublisher · SocketEmitter
```

| Module | Covers | Called by |
|---|---|---|
| **ShadeRequests** | shade and group commands; favorite and pairing; position and rolling-code correction | WebUiRoutes, ApiRoutes, MqttCommands |
| **ModelEditor** | add, save, delete, link and sort rooms, shades, groups and repeaters | WebUiRoutes |
| **ModelSnapshot** | the eight read routes: `/discovery` `/controller` `/rooms` `/shades` `/groups` `/room` `/shade` `/group` | WebUiRoutes, ApiRoutes |

**Settings, network, radio config, firmware and discovery** have one transport each and little logic of their own. They keep calling the modules they use today (`ConfigSettings`, `ESPNetwork`, `Transceiver`, `GitOTA`, `SSDP`); a new module would only pass calls through. The exception is pure validation, such as the pin-conflict check, which moves into a function the native tests can reach.

## Decisions

### LoopQueue
- One FIFO from every other task into `loop()`. It carries shade requests, model edits and snapshot fills.
- No item jumps the queue, model edits included.
- MQTT goes through it too, although it already runs on `loop()`. That gives MQTT the radio back-pressure that web commands have (`holdWebCommand`) and keeps arrival order.

### ShadeRequests
- **Interface:** one request value, grown out of `web_command_t`. Positions are given as the client sees them.
- **It owns:**
  - looking up the shade or group;
  - flipping positions;
  - range checks;
  - repeat defaults;
  - refusing a request when the radio can't take it.
- **Test seam:** the shade sits behind exactly the operations `processQueue` calls today:
  - `moveToTarget`, `moveToTiltTarget`;
  - `sendCommand`, `sendTiltCommand`, `repeatFrame`/`isLastCommand`, `sendSensorCommand`;
  - `setMyPosition`, unpair, `setRollingCode`, set positions;
  - a lookup giving the shade's or group's existence, flip and tilt type.
- **Two implementations behind it:** the real one forwards to `SomfyShade`/`SomfyGroup`; a fake records the calls in native tests.
- **Later:** when the shade behaviour itself is deepened (candidate #1 of the architecture review), only the real implementation changes.

### ModelEditor
- **What it takes over** from the `Web.cpp` handlers and `SomfyShadeController`:
  - id and sort-order allocation (the six `getNext*Id`/`getMax*Order` copies);
  - limits;
  - validation;
  - linking rules: remote to shade, shade to group, repeaters;
  - what is committed and which events go out after each edit.
- **What `SomfyShadeController` keeps:** storage and lookup.
- **Request bodies:**
  - The handler copies the JSON body to the heap and queues a pointer to it.
  - `loop()` parses and validates it with the code in `SomfyJson.cpp`; keeping it there avoids a second ArduinoJson file.
  - At most one edit is in flight; another gets 503. A body over 4 KB gets 413.
- **Reply and result:**
  - The HTTP reply is `{status: "accepted", requestId}`.
  - Once `loop()` has made the change, it sends the existing `…Added`/`…Removed`/`…State` events, with their payloads unchanged (Home Assistant reads `shadeRemoved`).
  - It also broadcasts `requestResult {requestId, ok, desc, id}`. The UI that sent the request picks it out by `requestId`.
- **Web UI:**
  - It fetches the details it needs through a read route, for example `/shade?shadeId=` after an add.
  - If no `requestResult` arrives within about 5 s, it shows a popup that says the change wasn't confirmed and offers a reload.
- **Socket-handler fixes** in `index.js`, part of this step:
  - `shadeAdded`/`shadeRemoved` are no-ops;
  - `procRoomRemoved` keeps the removed room;
  - the reply body is read even on errors.

### ModelSnapshot
- **One snapshot type** with the reported fields of rooms, shades, groups and repeaters. That is about 5.5 KB at 32 shades, 16 groups and 16 rooms, against roughly 24 KB for a copy of the full `SomfyShade` objects.
- **One snapshot per read:**
  1. The handler allocates the snapshot (one item for `/shade`, `/group` and `/room`) and queues a fill.
  2. The chunked response returns `RESPONSE_TRY_AGAIN` until `loop()` has filled the snapshot.
  3. It then streams JSON from that private copy.
- **Not in the snapshot:** the parts outside the shade model are built on the fly. That covers the `/discovery` device info and memory figures, and the `/controller` header (limits, radio config, firmware version).
- **Limit:** at most 2 reads in flight; a third gets 503.
- **Serializers** work from the snapshot, not from `somfy`. They lose `group->updateFlags()`, which sends events from the web task today, and they become testable natively through `JsonFormatter`.

### Change fan-out
- **Marking:**
  - Each shade and group gets a bitmask with one bit per reported value: position, target, direction, tilt, favorite, sun flags, rolling code, and so on.
  - The `p_*` setters set the bit, and only when the displayed value changes, as today.
- **Sending:** once per `loop()` pass, for each marked shade or group:
  - the socket gets one `shadeState`/`groupState`;
  - MQTT gets only the topics whose bits are set;
  - then the bits are cleared.
- **Reconnects:** what an output misses while disconnected comes back on reconnect. MQTT `connect()` publishes everything, and a new socket client gets `initClients()`.
- **Events that aren't state** (`shadeCommand`, "remote X pressed Up") stay explicit calls.

### Flash
- **Debug build:** `esp32devdbg` moves to `esp32_huge_app.csv` (one 3 MB app, no OTA) and stays there. It isn't shipped, and at 98.3% of the OTA slot it had 26 KB left.
- **Release builds:** the release envs keep their partition tables, and CI builds them. `esp32dev` is at 1,396,957 bytes (88.8%), so 176 KB is free.
- **Firmware-update testing** has to use a release build.

## Steps

Each step is a PR of its own that can be tested on the device. Each one runs `pio test -e native` and the `esp32devdbg` and `esp32dev` builds.

| # | Step | Closes |
|---|---|---|
| 0 | Move `esp32devdbg` to `esp32_huge_app.csv`, in a commit of its own. The device then needs one USB flash (see below). | flash pressure during the refactor |
| 1 | LoopQueue + ShadeRequests. Web and MQTT both submit shade requests. Native tests run through the fake shade. | MQTT flip drift, repeat defaults, MQTT commands lost while the radio is full |
| 2 | ModelSnapshot for all eight read routes. Native tests cover the serializers. | inconsistent `/controller` documents, events sent from the web task |
| 3 | ModelEditor, `requestResult` and the `index.js` changes. | the rest of the web-task writers in KNOWN_ISSUES |
| 4 | Change fan-out with dirty bits, replacing publishing from the `p_*` setters. | writes that skip MQTT, `position` stuck at 50 |
| 5 | Split `Web.cpp` into WebUiRoutes and ApiRoutes, and `MQTT.cpp`/`SomfyMqtt.cpp` into MqttCommands, an MQTT connection and MqttPublisher. Edit parsing stays in `SomfyJson.cpp`. | the glossary names land in the code |

### Step 1: what changed in behaviour
- **Flip handling is the same for every entry point.** MQTT `tiltTarget`, `mypos`, `myTiltPos` and `tiltPosition`, and `/setPositions` (which Home Assistant calls), now turn the position round for a shade with `flipPosition`, as the other routes always did.
- **Setting the favorite over MQTT keeps the other half.** `mypos` on a shade with tilt keeps the favorite tilt; before, it set it to 0. `myTiltPos` keeps the favorite lift, or uses where the shade is when it has none.
- **`repeat` means the same everywhere.** Left out or negative is the shade's or group's own repeats; 0 is no repeats. `/shadeCommand` used to read 0 as "the shade's own". A JSON `repeat` over 127 is capped; it used to wrap round to "the shade's own".
- **Position corrections reach MQTT.** `/setPositions` and MQTT `position`/`tiltPosition` go through the setters, so they are published.
- **MQTT commands go through the LoopQueue.** While the radio is full they wait instead of being refused, and they run in order with web requests. An MQTT command topic that isn't understood is logged.
- **The LoopQueue is drained on every `loop()` pass**, with or without a network. It is no longer drained from inside a GitHub firmware download, so a restore can't reboot the device in the middle of one.

`WebCommand.h` became `LoopQueue.h` (the item and the hold rule) and `ShadeRequests.h` (the request, the `ShadeOps` seam and the module). `SomfyShadeOps` is the device side of the seam. `entryPoints/MqttCommands` holds the topic parsing. There are 45 new native tests (271 in all).

Flash: `esp32devdbg` 1,546,405 → 1,547,381 bytes, `esp32dev` 1,396,957 → 1,398,337 bytes.

### Step 2: what changed in behaviour
- **Each read describes one moment.** `/controller`, `/discovery`, `/rooms`, `/shades`, `/groups`, `/room`, `/shade` and `/group` stream a ModelSnapshot that `loop()` fills. The first two KNOWN_ISSUES items about `/controller` are fixed for reads.
- **Every read is chunked.** `/discovery`, `/shades` and the rest used to be built whole into a growing response buffer, the path of the old `cbuf::resize` crash.
- **A read waits for `loop()`.** The headers go out at once; the body follows once the snapshot is filled. That is usually one `loop()` pass, but if the headers' ACK comes back first the library only asks again on its next poll, up to about 500 ms later. A read queued behind a command that waits for the radio waits with it.
- **At most two reads at once.** A third gets 503 "Busy, try the read again". Each read holds a 5.6 KB snapshot and a 3 KB unit on the heap only while it runs.
- **An item too big for the 3 KB unit is left out of every read route**, not only `/controller` (see KNOWN_ISSUES).
- **`/room`, `/shade` and `/group`** still answer 500 for an id that doesn't exist. If the item disappears between that check and the snapshot, the reply is 200 with `{"status":"ERROR","desc":"… Id not found."}`.
- **Group flags in replies are worked out without side effects.** Serializing a group no longer calls `updateFlags()`, which sent `groupState` and published MQTT from the web server task.
- **`/discovery` no longer logs where its time went.** Building it is now spread over the chunks.

`ModelSnapshot.h/.cpp` holds the snapshot types, the JSON for each item, `SnapshotWriter` (the eight documents, built a unit at a time) and `SnapshotSlots`. `SomfySnapshot.cpp` copies the shade model into it, for `loop()`'s fills and for the replies still written on the web server task. The `SOMFY_MAX_*` limits moved to `ShadeTypes.h` so the native tests can use them. 21 new native tests, 292 in all.

Flash: `esp32devdbg` 1,547,381 → 1,547,821 bytes, `esp32dev` 1,398,337 → 1,400,401 bytes.

### Step 0 on a device
NVS stays at `0x9000`, so the rolling codes survive. The file system partition shrinks from `0xE0000` to `0xD0000`, and the existing LittleFS won't mount, because `LittleFS.begin()` doesn't format. So:
1. Take a backup through `/backup`.
2. Flash the partition table, firmware and file system over USB: `pio run -e esp32devdbg -t upload -t uploadfs`.
3. Restore the backup.

## Risks

- **async_tcp must never wait.** Every handler only queues and returns. The two kinds of waiting reply, the read routes, poll with `RESPONSE_TRY_AGAIN` and never block.
- **Heap.** Edit bodies (one at a time, up to 4 KB) and snapshots (two at a time, about 5.5 KB each) come from the heap. Check the `memStatus` `max` figure on the device after steps 2 and 3.
- **The 2 KB socket event limit** (see KNOWN_ISSUES). That is why `requestResult` carries an id and not the object.
- **The UI changes ship with the firmware** in step 3, because the UI is served from LittleFS. An older UI against the new firmware would wait for HTTP reply bodies that no longer come.

## Found during the review, not part of this plan

These are open, and each can be fixed on its own:
- MQTT `position`/`target` published as a constant 50 ([SomfyShade.cpp:305](../src/SomfyShade.cpp#L305), [SomfyMqtt.cpp:36-38](../src/outputs/SomfyMqtt.cpp#L36-L38)). It's a one-line fix that needn't wait for step 4.
- A restore without shades skips the shade and group records but not the room records, so it reads the rest of the file from the wrong place ([ConfigFile.cpp:578-580](../src/ConfigFile.cpp#L578-L580)).
- The Ethernet pin conflict is only checked for the up pin ([SomfyJson.cpp:63-65](../src/SomfyJson.cpp#L63-L65)).
- `linkRepeater` never sets `isDirty`, so a newly linked repeater isn't saved ([ShadeController.cpp:323](../src/ShadeController.cpp#L323)).
- `/linkRemote` and `/unlinkRemote` send a 500 and then a second response ([Web.cpp:2055](../src/entryPoints/Web.cpp#L2055), [2083](../src/entryPoints/Web.cpp#L2083)).
- `settingPos` is never cleared when the lift arrives, while `settingTiltPos` is. A later arrival may send an extra stop. Not confirmed on a device.
- The web UI's `procRoomRemoved` keeps the removed room and drops the others (`data-src/index.js:2184`).
- The Home Assistant integration sends no `apikey` header on shade, group and tilt commands.
