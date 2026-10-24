# Socket protocol 2

Why the protocol looks like this: [ADR 0005](adr/0005-one-websocket-protocol.md). Terms: [GLOSSARY.md](../GLOSSARY.md).

**The table of types, fields, error codes and feeds is defined once, in [src/ProtoSchema.h](../src/ProtoSchema.h).** The device sends it to every client as the schema, and `test/fixtures/proto2/schema.json` is a copy recorded by the e2e suite (`pytest test/e2e --record`). This page describes how the protocol works, not every field.

## Connection

- `ws://host/ws` on port 80. Under Secured the upgrade needs `?apikey=<token>` from `/login`; without a valid one it is refused with 401 ([ADR 0006](adr/0006-two-security-modes-eleven-http-routes.md)).
- Optional `?schema=<hash>`: the client already has the schema with that hash and doesn't need it again.
- At most 5 clients. A sixth is closed as soon as it connects.
- Text frames only, and each message in one frame: a fragmented message is answered with one bad request, without an `id` since it couldn't be read. Until the device puts messages together from their pieces, that includes a frame larger than one TCP segment (about 1.4 KB), which also reaches it in pieces. A binary encoding, if one comes, would be asked for at the upgrade (for example `?enc=bin`) and use binary frames.

## Envelope

Every frame, in both directions, is one JSON object:

```json
{"v":2,"t":129,"id":17,"d":{"s":3,"p":40}}
```

| Key | Meaning |
|---|---|
| `v` | protocol version, 2 |
| `t` | message type, a number from the schema |
| `id` | only on a client message that wants a reply, and on that reply |
| `d` | the payload, with keys from the schema's field dictionary |

Within `d`:
- keys are one or two characters, from one dictionary shared by every type;
- booleans are `1` and `0`;
- a field at its default is left out: a client fills it in from the schema;
- lists and objects use the same dictionary for the keys inside them.

The keys of `hello` (`fw`, `sh`) and of the schema itself (`T`, `F`, `E`, `Q`) never change, since a client needs them before it has a schema.

## Message types

| Range | Sender | Types |
|---|---|---|
| 1–15 | control | `hello` 1, `reply` 2, `next` 3, `sync` 4, `syncEnd` 5, `subscribe` 6, `unsubscribe` 7 |
| 16–127 | client ops | commands: `shadeCommand` 16, `groupCommand` 17, `tiltCommand` 18, `repeatCommand` 19, `setPositions` 20, `setSensor` 21, `setMyPosition` 22, `unpairShade` 23, `setRollingCode` 24, `sendRemoteCommand` 25; model edits and settings follow |
| 128–255 | device events | `shadeState`, `shade`, `shadeRemoved`, `group`, `groupState`, `groupRemoved`, `room`, `roomRemoved`, `roomOrder`, `shadeOrder`, `groupOrder`, `shadeCommanded`, `memory`, `wifi`, `ethernet`, `frame`, `frequencyScan`, `repeaters` |

A number is never reused. A retired type stays reserved.

## Connect sequence

1. The device sends `hello`: `{"fw":"<firmware>","sh":"<schema hash>"}`.
2. The device sends the first `sync` frame straight away. Unless the client passed the matching `?schema=`, the sync starts with the schema:
   - `T`: types, as `[number, name]`
   - `F`: fields, as `[key, name, kind, default]`, kind being `b` bool, `i` integer, `n` number, `s` string, `l` list, `o` object
   - `E`: error codes, as `[number, name]`
   - `Q`: feeds, as `[number, name]`
3. Then the device: `header` (limits, starting address, what a new shade gets, server id, hostname, model, chip, firmware, connection type), `rooms`, `shades`, `groups`, `repeaters` and `status` (memory, and WiFi or Ethernet). A frame is at most 4 KB, an item is never split across frames, and several sections can share one.
4. The client sends `next` after each `sync` frame, and the device answers with the following one. With no `next` for 30 s the device closes the socket.
5. `syncEnd` follows the last `sync` frame.

The client can send `sync` at any time to get the shade model and status again (without the schema).

Events go out to every client from the moment it connects, so they can arrive during the sync, and even before the schema. A client keeps any frame it can't decode yet until it has the schema. Each sync frame is formatted from the live model, so an item in a later frame already holds every change before it. A client treats sync items and events alike: insert or update by id, and ignore removing an id it doesn't have.

## Events

| Event | When | Payload |
|---|---|---|
| `shadeState` | a shade's state changed, or it moved: while it travels, at most every 250 ms, and its stop at once | id, position, target, direction, favorite, tilt, flags, rolling code |
| `shade` | a shade was added or a setting changed (edit, pairing, linked remote, group, its room deleted) | the whole shade, as in the sync |
| `group`, `groupState` | the same for a group: its settings or linked shades, or its flags and direction | |
| `room` | a room was added or saved | the whole room |
| `*Removed` | a shade, group or room was deleted | its id |
| `roomOrder`, `shadeOrder`, `groupOrder` | the sort order changed | `is`: the ids in order |
| `repeaters` | a repeater was linked or unlinked | the list |
| `shadeCommanded` | a remote or the device commanded a shade | id, address, command, source, rolling code, source address |
| `memory`, `wifi`, `ethernet` | every 30 s, WiFi only when the signal moved | |

## Commands

Every op carries an `id` and gets one reply. The ops do what the HTTP routes of the same name do, with these arguments:

| Op | Payload | Does |
|---|---|---|
| `shadeCommand` | `shadeId`; `command` or `target`; optional `repeat`, `stepSize` | sends the command, or moves the shade to the position 0–100 |
| `tiltCommand` | `shadeId`; `command` or `target` | the same for the tilt |
| `groupCommand` | `groupId`, `command`; optional `repeat`, `stepSize` | sends the command as the group |
| `repeatCommand` | `shadeId` or `groupId`, `command`; optional `repeat`, `stepSize` | repeats the last frame when it was this command, or sends it |
| `setPositions` | `shadeId`; `position`, `tiltPosition` or both | sets where the shade is thought to be; nothing goes on air |
| `setSensor` | `shadeId` or `groupId`; `sunny`, `windy` or both; optional `repeat` | sends a sensor frame |
| `setMyPosition` | `shadeId`; `myPos`, `myTiltPos` or both | sets the favorite |
| `unpairShade` | `shadeId` | sends Prog and clears paired |
| `setRollingCode` | `shadeId`, `lastRollingCode` | sets the next rolling code; nothing goes on air |
| `sendRemoteCommand` | `remoteAddress`, `command`, `lastRollingCode`; optional `encKey`, `repeats` | sends one frame as a remote that isn't one of the shades |

- **An op reads only the fields that were sent.** Unlike what the device sends, a field left out of an op is not its schema default: it is not given, so `setPositions` with only `position` leaves the tilt as it is. A client sends every field it means, defaults included.
- `command` is a command's name, in any case: `My`, `Up`, `Down`, `MyUp`, `MyDown`, `UpDown`, `MyUpDown`, `Prog`, `SunFlag`, `Flag`, `StepUp`, `StepDown`, `Toggle`, `Sensor`, `Favorite`, `Stop`, or the spelling `shadeCommanded` reports (`Step Up`, `My+Up`).
- `repeat` left out means the shade's or the group's own repeats.
- Where they differ from the routes:
  - The keys come from the dictionary: `setMyPosition` takes `myPos` and `myTiltPos` (the route: `pos`, `tilt`); `setRollingCode` and `sendRemoteCommand` take `lastRollingCode` (the routes: `rollingCode`, `rcode`); `sendRemoteCommand` takes `remoteAddress` (the route: `address`).
  - `groupCommand`, `repeatCommand` and `sendRemoteCommand` need a `command`, where the routes send My without one.
  - `setMyPosition` takes a tilt on its own and keeps the lift favorite, where the route needs a lift position.
- A field of the wrong kind, a value out of range, or both of two alternatives, is a bad request, and `desc` names the field.
- The reply comes once the command has gone to the radio, or was refused: not found (no shade or group with that id), radio unavailable (the radio is paused, as during a frequency scan, or didn't take it), busy (the device's command queue is full, or `setMyPosition` on a shade that is moving) or bad request (`setPositions` with only a tilt, for a shade without one). It carries no state: the shade's `shadeState` events follow as it moves.
- `sendRemoteCommand` is answered as soon as the radio has taken the frame, not after the ops queued before it, and with radio unavailable when the radio is full rather than waiting for room.

## Replies

A client message with an `id` gets exactly one `reply` with that `id`, to that client only, whether it is an op or a control message such as `next`: `ok` 1 on success, or an error `code` and a `desc` for people. Error codes are in the schema: bad request, unknown op, unauthorized, not found, busy, radio unavailable, internal.

A malformed message gets a bad request, with its `id` when it had one. A type a client may not send gets an unknown op.

## Feeds

`subscribe` and `unsubscribe` take a feed number in `fe`: `frames` (received remote frames, with their pulses) or `frequencyScan`. Their events go only to clients subscribed to them.

## Inside the device

async_tcp reads client frames and answers with fixed frames that hold nothing of the shade model: `hello`, errors and `ok`. Everything formatted from the shade model goes out from `loop()`, which is the only task that reads and writes it ([ADR 0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md)). Sync frames and events share the socket event buffer, so protocol 2 needs no more static memory.
