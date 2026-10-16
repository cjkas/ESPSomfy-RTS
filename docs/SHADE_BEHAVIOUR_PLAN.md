# Plan: deepen the shade behaviour

**Status:** design settled (see "Decisions"). PR A is #46. PR B (`refactor/shade-behaviour`) is built: the behaviour is in `ShadeBehaviour`, pinned by `test_shadebehaviour`. The reconcile order is the open question (below).
**Written:** 2026-10-16, on top of `refactor/split-transports` (PR #44, the top of the LoopQueue stack #40–#44, which is not yet merged or run on a device). Line numbers refer to that branch.
**Start here:** read [GLOSSARY.md](../GLOSSARY.md), [ADR-0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md) and [LOOP_QUEUE_PLAN.md](LOOP_QUEUE_PLAN.md) first. Their decisions are not reopened here.

## Why

This is candidate #1 of the architecture review, the one ranked first. The LoopQueue plan (Q7 there) deliberately did it second, so that the bugs in front of the shade could be fixed first. Steps 1–5 rebuilt everything around the shade. The shade's own behaviour is still about 1,000 lines in [src/SomfyShade.cpp](../src/SomfyShade.cpp).

### The same rule exists in several drifted copies

What a shade does for Up, Down or My is written out separately in each place it can be triggered from:

| Function | Triggered by |
|---|---|
| `processFrame` | a frame received from a remote, and the shade's own echoed frame (see below) |
| `processWaitingFrame` | a remote button held (long press, tilt) |
| `processInternalCommand` | a group command reaching its member shades |
| `sendCommand`, `sendTiltCommand`, `moveToTarget`, `moveToTiltTarget`, `setMyPosition`, `moveToMyPosition` | commands from the web, MQTT and Home Assistant, through ShadeRequests and `SomfyShadeOps` |
| `checkMovement` | every `loop()` pass: simulated travel, arriving at the target, sending the stop, storing the favorite |
| `processTxEvent` | the radio reporting a job started, finished, aborted or dropped |

Known drift:
- **Up on a tilt-only shade.** `sendCommand` and `processInternalCommand` set target 100, position 100 and tilt target 0. `processFrame` sets only the tilt target.
- **My while a tilt-only shade moves.** `processInternalCommand` sets the lift target to 100; `processFrame` leaves the lift alone.
- **The four "arrived at target" blocks in `checkMovement`** aren't symmetric. The tilt blocks clear `settingTiltPos`; the lift blocks never clear `settingPos`, which may send an extra stop on a later arrival. This is on the "still open" list in LOOP_QUEUE_PLAN.md, not confirmed on a device. Their stop conditions also differ by hand-written literals.
- **`setMyPosition`** has three near-copy branches (tilt-only, tilt, none).

### None of it is tested

The native tests can't build it. It reads `millis()`, sends through the radio, and uses the global `somfy`. Only the helpers it calls are tested: `travelPosition`, `stepTargets`, `ShadeSensors`, `TxHold`/`ShadeTxState` and `requestedFavoriteTilt` (`ShadeMotion`, `ShadeSensors`). The fixes just before this work (set-favorite tilt, rolling codes on the main loop) were in this code.

## What the shade code depends on today

- **Time:** `millis()` throughout `checkMovement`, `processFrame` and the senders.
- **Radio:** every send goes through `SomfyRemote::sendCommandAfter` ([src/SomfyRemote.cpp](../src/SomfyRemote.cpp), around lines 61–108). It:
  1. takes the next rolling code from NVS;
  2. encodes the frame and submits it with `somfy.sendFrame()`, getting a ticket, or 0 when the radio refuses;
  3. commits the rolling code;
  4. sets `lastFrame`;
  5. calls `somfy.processFrame(lastFrame, internal = true)`;
  6. calls `somfy.markTxPending()` for commands that start movement.
- **The echo (step 5 above).** `somfy.processFrame()` runs `processFrame` on every shade, the sender included. So part of what a command does to the sending shade happens in `sendCommand`, and part in its own `processFrame(internal)` on the echoed frame. The same echo moves any other shade that has the sender's address as a linked remote. GPIO-driven shades (`GP_Remote`/`GP_Relay`) skip the radio but still echo.
- **Groups.** A group command sends the group's frame, then calls `processInternalCommand` on each member shade ([src/SomfyGroup.cpp](../src/SomfyGroup.cpp)).
- **The global `somfy`:** `isDirty`/`commit()` for saving positions and the favorite, `updateGroupFlags()` after sensor and sun-flag changes, `transceiver.config` for bit length and protocol, and `groups` in `isInGroup()`.
- **GPIO:** `triggerGPIOs`, `setGPIOs` and `usesPin` for dry contacts and GPIO motors.
- **Outputs:** only `changed` bits since step 4. The change fan-out publishes them (`ShadeChanges.h`, `SomfyShadeController::sendChanges()`).

## What makes it easier now

- **Callers:** ShadeRequests reaches the shade only through the `ShadeOps` seam ([src/ShadeRequests.h](../src/ShadeRequests.h)). The real implementation is `SomfyShadeOps`, and a recording fake exists in `test/test_shaderequests`. Once the behaviour module exists, only `SomfyShadeOps` changes.
- **Outputs:** the shade only marks what changed (step 4).
- **Concurrency:** only `loop()` touches shades (ADR-0002), so the module is single-threaded code.
- **Snapshots:** `shade_snapshot_t` and `snapshotShade()` already describe a shade as clients see it, which is useful for test assertions.

## The goal

One module holding the shade's behaviour behind a small interface:
- **Inputs:** a received frame, a command, a group command, a radio event, and a tick, each with the current time.
- **Sending:** goes out through a seam that returns a ticket or a refusal. On the device that is the radio path; in tests, a fake that records what was sent.
- **Inside:** one rule per command and tilt type.
- **Tested** natively, by scenarios such as: *an integrated-tilt shade at 0%; a remote sends Down; at 1.5 s it is still tilting; at 4 s it is lifting at about 25%; reaching the 40% target sends a stop; the radio drops the stop and the shade runs on to 100%.*

## Design questions

The decisions are the user's. Each question comes with a recommended answer, and they are asked in rounds, as in the LoopQueue plan. Settled answers move to "Decisions".

### Found while preparing round 2

- **The echo does part of the sender's work, and it varies by command.** For a frame marked internal, `processFrame` leaves the targets alone on Up and Down (the sender sets them itself after the send). It does set them on My (when idle), Stop, Toggle, Favorite, StepUp and StepDown. So for Stop, Step, Toggle and Favorite sent from the web, the shade's *only* reaction is in its echo. A linked shade gets the same internal frame, so it follows My/Stop/Step/Toggle/Favorite but not Up/Down. The echo also resets `moveStart`/`startPos` and copies the frame into `lastFrame` on every shade it reaches.
- **The order inside a send is:** encode → submit → commit rolling code → `lastFrame` → echo to every shade → `markTxPending` on the sender, its linked shades and the members of a group with that address. The sender's caller (`moveToTarget`, `sendCommand`, …) sets its targets *after* all of that.
- **`lastFrame` serves two purposes.** It is the template `sendCommandAfter` builds the next frame from, and the long-press state `processWaitingFrame` reads (`await`, `repeats`, `processed`, `cmd`, `stepSize`).
- **`emitCommand` is still a direct output.** It builds a socket event with ArduinoJson and publishes three MQTT topics on the spot, about 40 times in the shade code and once per member in `SomfyGroup::sendCommand`. `emitState()` only marks the shade changed.
- **`isInGroup()` isn't used by the behaviour code.** Only the delete check in SomfyJson and the snapshot call it, so it stays in `SomfyShade`.
- **`flags` mixes settings and state.** SimMy, SunSensor and Light are settings; SunFlag, Sunny, Windy and DemoMode are sensor state the behaviour changes. ConfigFile saves the whole byte.
- **Outside writers of motion state:** ConfigFile at boot (positions, targets, favorite) and `SomfyShadeOps` setting a position without moving (`p_target`/`p_currentPos`). The other files only read it.

### Found while preparing round 3

- **The sender's own echo needs the frame as sent, not just the command.** The Flag and SunFlag rules ignore a frame whose rolling code is 0x8000 or above (a guard against bogus sensors), and they apply it to the shade's own echo too. `flipCommands` flips the command on the way out and flips it back when the echo comes in.
- **`sendSensorCommand` doesn't move.** ShadeRequests calls it through `SomfyShadeOps`, and it stays in `SomfyRemote`. It keeps echoing to every shade, the sender included, so the shade hears it through `onFrame`.
- **GPIO:** `GP_Remote` pulses its pins inside the send; `GP_Relay` sends nothing and `setGPIOs()` drives the relays from `loop()` by reading the direction. Both stay outside the module.

### Still open

- **Reconcile: what is decided first, and in what order.** The candidates, each pinned by a "today:" test in `test_shadebehaviour` unless marked:
  - ~~`settingPos` is never cleared on a lift arrival, so a later step also sends a stop.~~ Fixed (Q18).
  - Up and My on tilt-only shades differ between the web, a remote and a group (`test_today_up_on_tilt_only_*`, `test_today_my_while_tilt_only_moves`).
  - An integrated tilt sent to a lift target without a tilt turns its slats back with an Up after the stop (`test_today_integrated_tilt_turns_back_after_a_lift_target`).
  - A remote step uses the step size this shade last *sent*, because `copy()` never copies `stepSize` (`test_remote_step_uses_the_step_size_last_sent`).
  - A group's SunFlag or Flag doesn't make other groups work their flags out again (`test_group_sun_flag_needs_a_sensor`).
  - The self-echo (Q5) and merging group commands into the frame rule (Q8).
  - `lastFrame` doing two jobs (the Q6 revisit).
  - The 500 ms long-press deadline is compared as `now > await`, which isn't wrap-safe (not tested).

## Constraints

- **[AGENTS.md](../AGENTS.md):** every change keeps `pio test -e native` green and adds or updates tests.
- **Flash:** check every commit. `esp32dev` (release, with OTA) has about 180 KB free. The debug build uses one 3 MB slot once #39 is in its history. Every file that uses ArduinoJson costs flash, so keep the new module free of it.
- **`ShadeMotion`, `ShadeSensors`, `ShadeTxState` and `stepTargets`** are deep and tested. Reuse them; don't re-derive them.
- **Vocabulary:** module, interface, seam, adapter, depth (the `/codebase-design` skill) and the domain terms in GLOSSARY.md.

## Decisions

**Round 1 (2026-10-16), all as recommended:**

- **Q1. What moves in.** `processFrame`, `processWaitingFrame`, `processInternalCommand`, `sendCommand`, `sendTiltCommand`, `moveToTarget`, `moveToTiltTarget`, `setMyPosition`, `moveToMyPosition`, `checkMovement` and `processTxEvent` move into the module. So do their helpers (`sendStop`, `sendSetMy`, `storeMyPosition`, `runToLimit`, `applySensorMove`, `applyStep`, `tiltRepeats`) and their state: positions, targets, directions, `lastMovement`, start times and positions, the `setting*` flags, `awaitMy`, the favorite, `ShadeSensors` and `ShadeTxState`. The `p_*` setters and change marking also move in. `SomfyShade` keeps identity, settings, persistence, GPIO, JSON, MQTT topics and rolling codes.
- **Q2. Pin, move, reconcile.** Phases 1 and 2 land together and change no behaviour. Disagreements are pinned as "today: …" tests. Reconcile makes one decision per commit, each checked on real motors.
- **Q3. Time is passed in.** Every input carries `now` as `uint32_t`, with wrap-safe elapsed times as in `TxHold`.
- **Q4. One object per shade.** It holds a `const ShadeSettings&`. The settings fields move into that struct, which `SomfyShade` owns, so there is one copy and nothing to drift.

**Round 2 (2026-10-16), all as recommended.** The principle: the module never touches anything outside itself; `SomfyShade` wraps it and does the outside work.

- **Q5. The echo.** Today a sent frame is handed back to every shade as if a remote had sent it, and some commands (Stop, Step, Toggle, Favorite, My when idle) do their work only there. The module calls its own frame rule directly after a successful send, at the same point as today. Other shades (linked remotes) still hear the frame through the controller. Whether the sender should hear itself at all is a reconcile decision.
- **Q6. Held-button state.** The module keeps its own record of the held button (`cmd`, `repeats`, `await`, `processed`, `stepSize`, `remoteAddress`). `SomfyRemote::lastFrame` stays the send template, and `SomfyShade` keeps copying frames into it as today.
- **Q7. Command events.** They leave through an output seam, `onCommand(cmd, source, sourceAddress)`, which calls today's `emitCommand` on the device and records in tests. The timing is unchanged, and ArduinoJson stays out of the module.
- **Q8. Group commands.** They stay a separate input, `onGroupCommand(cmd, repeat, now)`, with today's rule. `markTxPending` becomes `onTxQueued(ticket, now)`. Merging group commands into the frame rule is a reconcile decision.
- **Q9. The global `somfy`.** The module reports "save wanted" and "sensor flags changed"; `SomfyShade` sets `somfy.isDirty` and calls `updateGroupFlags()`. `bitLength` goes into `ShadeSettings`, still filled in lazily by `SomfyShade`. `isInGroup()` stays in `SomfyShade`.
- **Q10. The `flags` byte.** SunFlag, Sunny, Windy and DemoMode go into the module; SimMy and SunSensor into `ShadeSettings`; Light stays in `SomfyShade`. The byte is put back together for ConfigFile, JSON and MQTT, so nothing saved or sent changes.
- **Q11. Reading the motion state.** A read-only `state()`. ConfigFile at boot writes through `restore(...)` and `SomfyShadeOps` through `setPosition(pos, tilt)`. The 12 reading files switch over in the move phase.

**Round 3 (2026-10-16), all as recommended:**

- **Q12. The radio seam.** The module sends with `send(cmd, repeats, stepSize, delayMs, priority)` and gets back `{sent, ticket, frame}`. On the device, `SomfyRemote` does what `sendCommandAfter` does today (rolling code, encode, submit or pulse GPIO, commit the code, `lastFrame`), but it echoes and marks the radio hold only for the *other* shades and group members. The module runs its own echo on the returned frame and marks its own hold. In tests a fake records each send, hands out tickets 1, 2, 3, … and can be told to refuse.
- **Q13. Names.** These go into GLOSSARY.md with PR B, when the code exists:
  - **ShadeBehaviour**: the module (`src/ShadeBehaviour.h/.cpp`, `test/test_shadebehaviour`).
  - **ShadeSettings**: the read-only settings.
  - **ShadeBehaviour::State**: the motion state `state()` returns.
  - **MotorLink**: the send seam, radio or GPIO.
  - **ShadeEvents**: the `onCommand(...)` output.
- **Q14. Order and PRs**, stacked on #44:
  1. **PR A:** `ShadeSettings` and the `flags` split, with tests that the saved byte and JSON don't change.
  2. **PR B:** the move: `ShadeBehaviour`, `MotorLink`, `ShadeEvents`, `restore`/`setPosition`, the 12 readers and the pinning scenario tests. No behaviour change.
  3. **Reconcile:** one PR per decision. These start only after #40–#44, A and B have run on a device.

**Before PR A (2026-10-16), all as recommended.** Q4 assumed every setting lives on `SomfyShade`. But `flipCommands`, `bitLength`, `repeats` and the `flags` byte are on the `SomfyRemote` base, which groups share and the send path uses.

- **Q15. Two settings structs, held as bases.** `RemoteSettings` (`flipCommands`, `bitLength`, `repeats`) is a base of `SomfyRemote`, so shades and groups each have one. `ShadeSettings` (types, travel times, step size, `flipPosition`, `simMy`, `sunSensor`) is a second base of `SomfyShade`. Existing code like `shade->upTime` keeps compiling. The module will get two read-only references. That is still one copy of each, so nothing can drift.
- **Q16. The `flags` byte.**
  - SimMy and SunSensor are `bool`s in `ShadeSettings`, and Light is a `bool` on `SomfyShade`.
  - SunFlag, Sunny, Windy, DemoMode and the unused Lighted stay on `SomfyShade` as `stateFlags` until PR B moves them into the module.
  - A shade's byte is `getFlags()` (put together) and `setFlags()` (split), used by ConfigFile, JSON, MQTT, events and group flags.
  - Groups keep their own `flags` byte (the OR of their shades), and `SomfyRemote::getFlags()` is virtual for `sendSensorCommand`.
  - `test_shadesettings` checks that all 256 byte values come back unchanged.

**During PR B (2026-10-16):**

- **Q6 revisited: `lastFrame` is shared.** The module holds a reference to `SomfyRemote::lastFrame` instead of its own copy. A copy would have to mirror five writes exactly: each send overwrites it; the shade's own echo then counts as a repeat; `repeatFrame` adds the queued repeats; a sensor send overwrites it; and `copy()` never copies `stepSize` or `await`. The user chose sharing. Splitting the two jobs is a reconcile candidate.
- **Q12 refined.** `MotorLink::send` returns `{sent, ticket}`; the frame as sent is in the shared `lastFrame`. `SomfyRemote::sendCommandAfter(…, hearsItself = true)` leaves the sender out of the echo and the radio hold; `ShadeBehaviour::transmit` does both for itself.
- **Q7 and Q9 refined: one output seam that fires on the spot.** `ShadeEvents` carries `onChanged` (the change bits), `onCommand`, `onSaveWanted` and `onSensorFlags`. Nothing is collected and read back after each call, so the timing is today's. `onSensorFlags` fires exactly where `somfy.updateGroupFlags()` was called, not on every sensor-bit change.
- **`onTxEvent` takes no `now`:** the event carries its own time.
- **A known ordering change:** when a simMy shade's Favorite or My sends again from inside its own echo, a shade linked to it now hears the outer frame before the inner one. Before, it depended on the two shades' slots. Nothing else changes order.
- **Flash:** `esp32dev` `firmware.bin` grows 1,397,776 → 1,399,776 B (+2,000 B, 88.6%), mostly the virtual calls through the two seams.

**Reconcile round 1 (2026-10-16), all as recommended.** Each fix is one PR stacked on #47, and each changes a "today:" test.

- **Q17. Order.** Clear bugs first: `settingPos` (Q18), the remote step size (Q19), group sun flags (Q20), the long-press wrap (Q21). Then the rules where the copies disagree: tilt-only Up/Down/My, and the integrated tilt turning back. The structural changes (self-echo, merging group commands, splitting `lastFrame`) wait until those are done and may be dropped.
- **Q18. `settingPos` is cleared on a lift arrival,** as `settingTiltPos` is on a tilt arrival. A later step or group move then stops by itself instead of getting a My, which would have sent a stopped motor to its favorite.
- **Q19. A remote's step uses the frame's own step size** (`frame.stepSize`, 1 when 0) instead of `lastFrame.stepSize`, which `copy()` never updates. `copy()` itself stays as it is.
- **Q20. A group's SunFlag/Flag calls `onSensorFlags()`,** as the frame rule does, so every group works its flags out again.
- **Q21. The long-press wait is wrap-safe:** `(int32_t)(now - await) > 0`.
