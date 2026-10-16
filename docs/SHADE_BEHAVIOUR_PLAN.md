# Plan: deepen the shade behaviour

**Status:** design in progress. Round 1 of the design questions is open (below); nothing is built.
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

### Round 1 (open)

**Q1. What moves into the module.**
- **Into the module:** everything that decides where the shade is and where it is going: `processFrame`, `processWaitingFrame`, `processInternalCommand`, `sendCommand`, `sendTiltCommand`, `moveToTarget`, `moveToTiltTarget`, `setMyPosition`, `moveToMyPosition`, `checkMovement` and `processTxEvent`. Their state goes too: positions, targets, directions, start times, the `setting*` flags, the favorite, `ShadeSensors` and `ShadeTxState`.
- **Stays in `SomfyShade`:** identity and settings (id, name, remotes, types, times), persistence, GPIO, JSON and rolling codes.

Recommended: yes.

**Q2. Three phases: pin, move, reconcile.**
1. **Pin:** native scenario tests of today's behaviour, including where the copies disagree, each labelled "today: …". Every later behaviour change then shows up as a test change.
2. **Move:** the code goes into the module unchanged, and the pinning tests stay green. Today's code can't be built natively, so phases 1 and 2 happen together: the seams go in while the tests are written against the moved, unchanged code.
3. **Reconcile:** the copies are made to agree, one decision per commit, each checked on real motors.

Recommended: yes.

**Q3. Time is passed in.** Every input carries `now` (`onFrame(frame, now)`, `tick(now)`, ...), as `ShadeSensors::check(…, now)` and `TxHold` already do. The alternative is a clock interface the module asks. Recommended: pass it in.

**Q4. One behaviour object per shade, settings as plain data.** Each `SomfyShade` holds one module object with the motion state. Its settings come in as a plain struct: shade type, tilt type, up/down/tilt times, step size, `simMy`, `flipCommands`. The alternative is functions with no state of their own, over a state struct the caller owns. Recommended: an object per shade.

### Round 2 (waits for round 1)

- **The echo.** Should the module apply its own commands directly, instead of reacting to its own echoed frame? This is the biggest decision; it decides what the radio seam looks like, and whether linked shades still learn about the command from the echo.
- **The radio seam.** What it takes (command, repeats, step size, delay, priority) and returns (ticket or refused). Rolling codes, NVS and frame encoding stay behind it in `SomfyRemote`. GPIO-driven shades sit behind the same seam.
- **Linked remotes and groups.** How a frame from a linked remote and a group command reach the module, and whether `processInternalCommand` stays a separate input.
- **The global `somfy`.** Where `isDirty`/`commit()` go (a change bit the caller acts on?), `updateGroupFlags()` (the controller after the pass?), `transceiver.config` (part of the settings struct?), and `isInGroup()` (given by the caller?).

### Later rounds

- The names, which go into GLOSSARY.md (for example **ShadeBehaviour**, **MotorLink**). Check for clashes with `ShadeMotion`, `ShadeOps`, `ShadeRequests` and `ShadeChanges`.
- The order of the steps and the PR split.
- What the reconcile phase decides first: Up/Down/My on tilt-only shades, `settingPos` on arrival.

## Constraints

- **[AGENTS.md](../AGENTS.md):** every change keeps `pio test -e native` green and adds or updates tests.
- **Flash:** check every commit. `esp32dev` (release, with OTA) has about 180 KB free. The debug build uses one 3 MB slot once #39 is in its history. Every file that uses ArduinoJson costs flash, so keep the new module free of it.
- **`ShadeMotion`, `ShadeSensors`, `ShadeTxState` and `stepTargets`** are deep and tested. Reuse them; don't re-derive them.
- **Vocabulary:** module, interface, seam, adapter, depth (the `/codebase-design` skill) and the domain terms in GLOSSARY.md.

## Decisions

None yet.
