---
status: proposed
---

# The shade model is one module, and the only sender

`ShadeModel` (`src/model/`) owns the rooms, shades, groups and repeaters and is the only place frames are built and sent. One private `send()` takes the rolling code, has every other shade hear the frame, and marks the shades the frame sets moving as waiting for it to go on air. Group, sensor and repeat commands go through it too. It reaches the radio through `RadioPort` (jobs, and pulses for a GP_Remote shade) and tells the rest of the firmware about commands through `ModelEvents`. Time comes in as a `now` parameter. `SomfyShadeController` derives from it and implements both ports, so the code that uses the `somfy` global keeps working.

Before this, the echo, group commands, linked remotes, tx-pending and group flags ran through the global `somfy` in files that only built for the ESP32, and none of it was tested. `ShadeBehaviour` was tested, but only for a single shade. The model now builds natively, so tests can run scenarios across several shades and groups (`test/test_shademodel`).

## Considered Options

- **A rolling-code port.** Rejected, in line with [ADR 0001](0001-local-nvs-handles-no-wrapper.md): the remotes keep local `Preferences` handles, and the native tests use a map-backed stand-in for `Preferences.h` that they can seed and read back.
- **A port for the transceiver's calls into the model.** Put off. The transceiver still calls the model through the global (`onFrame`, `onIdle`, `onTxEvent`, `isRepeater`) until the RMT transmit work settles the radio side.
- **Composition instead of inheritance for the controller.** Rejected for now: deriving from `ShadeModel` leaves about 90 `somfy.` call sites as they are.

## Consequences

- Everything in `src/model/` builds in the native env. A file there that needs the radio, settings or network fails `pio test -e native`, so the folder guards its own boundary.
- The room, shade and group classes no longer save. The code that edits them (`SomfyJson`) commits the config file itself.
- A raw frame from a protocol 2 client still goes straight to the transceiver. It changes nothing in the model.
