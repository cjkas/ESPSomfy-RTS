# Plan: split and refactor Somfy.cpp

**Status:** phases 0 and 1 done (2026-10-13, commits `bbfb23e`..`505108c`); phase 2 open.
**Written:** 2026-10-13, against commit `4544eb6` on `fix/live-bugs`. Line numbers in the analysis below refer to that commit. The code now lives in the files listed under [Phase 1](#phase-1-split-the-file-by-concern).

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
One module per commit, each with native tests. These follow the `ShadeMotion` pattern: plain C++, no globals, and added to `build_src_filter` in the native env in `platformio.ini`.

1. **`ShadeSensors` (sun/wind).** Holds the seven sun/wind fields. Interface:
   - `onSensorFrame(sunny, windy, now)`, called when a sensor frame arrives
   - `onSunFlag()`, called when the sun flag is turned on
   - `check(now, sunFlag)`, which returns `none`, `extend` or `retract`
   - `windBlocksDown(now)`

   One shade method turns `extend`/`retract` into lift and tilt targets, so all three call sites use the same rule. Move the code with behaviour unchanged first. Then fix the tilt-only bugs from problem 1 in a separate commit, with a test that shows the fix.
2. **`stepTargets()` in `ShadeMotion`.** The integrated-tilt, tilt-only and lift-only branching, built on `stepPosition()`, used by both `processFrame` and `processInternalCommand`. The 1% step in `processWaitingFrame` (a held step button) may be deliberate; keep it unless decided otherwise.
3. **`PulseDecoder`.** The bit decoding from `handleReceive`, as `feed(durationUs)` returning true when a frame is complete. It must stay in IRAM: mark it `IRAM_ATTR` and don't call out of IRAM. The interrupt handler keeps only the noise guard, the timing and the push to the RX queue. Tests feed recorded 56-bit and 80-bit pulse trains.
4. **Optional, later.**
   - The four arrive-at-target blocks in `checkMovement` (lift up/down, tilt up/down) are near-copies that could share one helper.
   - The six `getNext*Id`/`getMax*Order` functions in the controller are copies of each other.

When each module is created, add its terms to [GLOSSARY.md](../GLOSSARY.md): "sun flag", "sensor frame", "pulse decoder".

## Risks

- **IRAM.** The decoder runs inside the interrupt and must stay in IRAM. Watch the build for `dangerous relocation` or IRAM link errors.
- **Flash.** See [Flash size](#flash-size-what-the-split-taught-us).
- **Inlining flag.** `--param=large-unit-insns=1000` changes inlining across the whole debug build. Check on the device that radio RX/TX still work.

## Decisions

Decided for phases 0 and 1:
- Phases ran in the order 0 → 1.
- Headers: `Somfy.h` stays the one header, with only `Transceiver.h` split out.
- Log tags: every file keeps `"Somfy"`. Files that never log (`SomfyMqtt.cpp`, `SomfyEvents.cpp`) have no tag.

Still open for phase 2:
- **Tilt-only sun/wind bugs.** Fix them in their own commit right after `ShadeSensors` is extracted (recommended), or record them in KNOWN_ISSUES.md for now.
