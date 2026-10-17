# Plan: split and refactor Somfy.cpp

**Status:** done. Phases 0 and 1 in `bbfb23e`..`505108c`, phase 2 in `0eb0e63`..`a176017` (2026-10-13).
**Written:** 2026-10-13, against commit `4544eb6` on `fix/live-bugs`. Line numbers in the analysis below refer to that commit. The code now lives in the files listed under [Phase 1](#phase-1-split-the-file-by-concern).
Since then `SomfyMqtt.cpp` and `SomfyEvents.cpp` have moved to `src/outputs/`.

## Why

`src/Somfy.cpp` is 4,766 lines. About 690 of them never compile. Six classes, two config structs and the receive interrupt handler share the file. The methods of each class are scattered: `SomfyShade`'s 71 methods are spread over about 20 separate runs. Some logic exists in several copies that have already drifted apart (see [Problems](#problems-beyond-file-size)).

## What the file contains

| What | Lines | Live lines (approx.) | Changes when… |
|---|---|---|---|
| Dead code: `/* */` blocks and `#ifdef USE_NVS` blocks (`USE_NVS` is never defined) | everywhere | ~690 (392 + 296) | never |
| Radio: `Transceiver`, `transceiver_config_t`, receive interrupt and pulse decoder, radio task, frequency scan, `RadioLock`/`TxQueueLock` | 1–116, 3700–4766 | ~950 | the radio hardware or RX/TX changes |
| MQTT: `publish*`/`unpublish*` for shades, groups and rooms, plus Home Assistant discovery | 980–1348, 3113 | ~400 | the MQTT topics change |
| Socket events: `emitState`/`emitCommand` | 1451–1611, 3105 | ~90 | the socket protocol changes |
| JSON config: `validateJSON`/`fromJSON` | 2775–3066 | ~200 | the web config form changes |
| GPIO and dry contacts: `setGPIOs`, `triggerGPIOs`, `usesPin` | 570–695, 2758 | ~140 | the relay wiring changes |
| `SomfyRemote`: rolling codes, `sendCommandAfter`, `repeatFrame` | scattered | ~200 | how frames are sent changes |
| Shade behaviour: `processFrame`, `processInternalCommand`, `processWaitingFrame`, `checkMovement`, my-position, `moveTo*`, `sendCommand` | 696–923, 1612–2706 | ~1,350 | what a shade does |
| `SomfyShadeController` and `SomfyRoom`: add/delete/find, ids, commit | 117–300, 3076–3700 | ~600 | the shade list changes |

## Problems beyond file size

1. **The same logic exists in several copies, and the copies have drifted apart.**
   - **Step targets.** `processFrame` uses the tested `stepPosition()` from `ShadeMotion`. `processInternalCommand` (~2172–2242) has its own inline maths, which ignores the frame's step count. `processWaitingFrame` uses a fixed 1% step.
   - **Sun/wind.** Sensor state is set in the `Sensor` case of `processFrame`, acted on by timers in `checkMovement`, and checked again in the `Down` case of `processInternalCommand` (`windLast`). The SunFlag handling exists twice. **These look like real bugs for tilt-only shades:**
     - the copy in `processFrame` (~1865) moves the tilt target of a tilt-only shade;
     - the copy in `processInternalCommand` (~2262) moves the lift target instead;
     - the sun timer in `checkMovement` (~744) also moves the lift target of a tilt-only shade, while the no-sun timer two branches later moves the tilt.
2. **The receive pulse decoder has no tests.** `handleReceive` (~3839–3969) turns pulse durations into bits. It is pure logic, so recorded pulse trains could test it in the native env, the way `SomfyFrame` is tested.
3. **`SomfyShade` has a wide interface:** 6 `publish` overloads, 12 `p_*` setters, `public` fields, and seven sun/wind fields (timestamps and done flags) that are really one sensor state.
4. **Hidden dependencies.** Shade methods use the global `somfy` for `isDirty`, `commit()`, `transceiver.config` and `updateGroupFlags()`. This plan leaves them alone. Removing them belongs to the "only `loop()` owns the shade model" work in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Plan

### Phase 0: delete the dead code
**Done** in `bbfb23e`. `sort_asc()` was dead too (only called from a dead block) and went with it.

One commit, no behaviour change.

- Remove every `#ifdef USE_NVS` block (296 lines), including `loadLegacy()` and `SomfyShade::load()`. Also remove `useNVS()`: it sits outside the blocks, but only the blocks call it.
- Remove the 16 commented-out blocks (392 lines), mostly old `toJSON` versions and `snprintf` emitters.
- Remove the matching declarations in `Somfy.h`.

This takes the file to about 4,050 lines before anything moves.

### Phase 1: split the file by concern
**Done** in `0a329ca`..`505108c`, one commit per file. Code moved unchanged, apart from `bit_length`, which became the shared `radioBitLength`. The 253 top-level definitions before and after match one for one.

Several commits. Code moves as it is: the classes and `Somfy.h`'s interface stay the same, only the files change. A class's methods may end up in several files by concern. Check each move with the `esp32devdbg` build and the native tests.

| New file | Lines | Contents |
|---|---|---|
| `Transceiver.h/.cpp` | 125 + 949 | `Transceiver`, `transceiver_config_t`, `RadioLock`/`TxQueueLock` and the radio statics, the receive interrupt handler and RX queue, the radio task, the frequency scan, and `somfy_frame_t::decodeFrame(somfy_rx_t*)` (it needs `RadioLock`). `Somfy.h` includes `Transceiver.h`, so callers don't change. |
| `SomfyRemote.cpp` | 170 | `SomfyRemote` and `SomfyLinkedRemote` |
| `SomfyShade.cpp` | 1,532 | shade behaviour (the rest of `Somfy.cpp`, renamed so history follows it) |
| `ShadeGpio.cpp` | 148 | the GPIO and dry-contact methods |
| `SomfyGroup.cpp` | 134 | `SomfyGroup` behaviour |
| `ShadeController.cpp` | 449 | `SomfyShadeController` and `SomfyRoom`. Not `SomfyController.cpp`, which would look like a pair with `SomfyController.ino`. |
| `SomfyMqtt.cpp` | 412 | every `publish*`/`unpublish*`/`publishDisco` |
| `SomfyEvents.cpp` | 99 | every socket `emitState`/`emitCommand` |
| `SomfyJson.cpp` | 235 | `validateJSON`/`fromJSON` for shades, groups and rooms, **and the radio's** (see below) |

#### Flash size: what the split taught us

The `esp32devdbg` app partition is 1,572,864 bytes, and before the split the image used 99.4% of it.

- At `-Og`, GCC only caps inlining growth in units over `large-unit-insns` (10,000 instructions). The big `Somfy.cpp` was over that, so ArduinoJson's helpers stayed calls. In smaller files they were inlined into every caller: moving the JSON parsing out grew the image by 19 KB, past the limit. `--param=large-unit-insns=1000` in `[env:esp32devdbg]` (`4cb676a`) treats every file as large. That freed 13 KB, and the split itself then cost nothing.
- Every file that uses ArduinoJson also gets its own copy of ArduinoJson's debug-assert strings, and its own copies of helpers it inlines. So all JSON parsing lives in `SomfyJson.cpp`, including the radio's `fromJSON`, which the plan had kept in `Transceiver.cpp`. Grouping it saved 2.8 KB. `SomfyMqtt.cpp` (`publishDisco` builds a `JsonDocument`) is the only other new file that uses ArduinoJson.
- Result: 1,546,797 bytes (98.3%), 16.8 KB less than before phase 0.

For phase 2, keep new modules free of ArduinoJson, and check the flash size of every commit. If space runs out, remove log lines first.

### Phase 2: pull out the copied logic
**Done** in `0eb0e63`..`a176017`. Each module is plain C++ like `ShadeMotion`, with native tests (222 tests in all now):

| Module | What it holds |
|---|---|
| `ShadeTypes.h` | `group_types`, `shade_types` and `tilt_types`, moved out of `Somfy.h` so the modules below can use them in the native tests. |
| `ShadeSensors.h/.cpp` | The sun and wind timers and their rules: `onSensorFrame()`, `check()`, `onSunFlag()`, `windBlocksDown()`. `sensorTargets()` turns extend/retract into targets for all three callers. |
| `stepTargets()` in `ShadeMotion` | Where a step takes a shade, shared by received steps (`processFrame`) and group steps (`processInternalCommand`). |
| `PulseDecoder.h/.cpp` | `decodePulse()`, the RTS timings and `somfy_rx_t`, moved out of `handleReceive`. It stays in IRAM (`0x4008xxxx`), and the tests feed it the edges `sendFrame()` produces. |

What changed in behaviour:
- **Tilt-only shades and the weather** (`5f26340`): sun and wind now move only the tilt of a tilt-only shade, to the favorite tilt (or fully) and back to 0, and only the lift of every other shade. Before, each of the three callers did something different.
- **Sun and wind timers across the `millis()` rollover**: the timers are now 32-bit like `millis()`. As 64-bit values they fired at once after the rollover every 49.7 days. This also saves 768 bytes of RAM.
- **Step edge cases**: a group step on a tilt-only shade already at its end now goes through like a remote step. A step that can't be sized (no travel time) leaves the targets alone.
- The 8 debug logs of the sun and wind timers are gone. They were compiled out at `CORE_DEBUG_LEVEL=3`.

Kept as it was: the fixed 1% step of a held step button in `processWaitingFrame`. It later turned out never to run: a step frame is acted on as soon as it arrives, so it is never left waiting. The branch was deleted, and `test_a_held_step_button_steps_once` pins what a held step button does.

Left out on purpose, since neither fixes a bug or can be reached by the native tests:
- the four near-copy arrive-at-target blocks in `checkMovement`. They are now in `ShadeBehaviour::tick` ([SHADE_BEHAVIOUR_PLAN.md](SHADE_BEHAVIOUR_PLAN.md)), still four copies, but pinned by native tests;
- the six `getNext*Id`/`getMax*Order` copies in the controller. Since replaced by ModelEditor's `firstFreeId` (step 3 of [LOOP_QUEUE_PLAN.md](LOOP_QUEUE_PLAN.md)).

Flash went from 1,546,797 to 1,545,717 bytes.

## Risks

- **IRAM.** The decoder runs inside the interrupt and must stay in IRAM. Watch the build for `dangerous relocation` or IRAM link errors.
- **Flash.** See [Flash size](#flash-size-what-the-split-taught-us).
- **Inlining flag.** `--param=large-unit-insns=1000` changes inlining across the whole debug build. Check on the device that radio RX/TX still work.

## Decisions

Decided for phases 0 and 1:
- Phases ran in the order 0 → 1.
- Headers: `Somfy.h` stays the one header, with only `Transceiver.h` split out.
- Log tags: every file keeps `"Somfy"`. Files that never log (`SomfyMqtt.cpp`, `SomfyEvents.cpp`) have no tag.

Decided for phase 2:
- The tilt-only sun/wind bugs were fixed in their own commit, right after `ShadeSensors` was extracted with behaviour unchanged.
- A tilt-only shade's lift is never moved by the weather.
