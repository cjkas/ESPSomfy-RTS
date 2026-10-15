---
status: accepted
---

# The loop task is the only writer of the shade model

The shade model (rooms, shades, groups and repeaters) is changed only from `loop()` (the Arduino `loopTask`). Code on any other task, mainly the HTTP handlers on the async_tcp task, hands its change over as a request through the LoopQueue and reads the shade model only from a ModelSnapshot that `loop()` fills. On a dual-core ESP32 the two tasks run at the same instant, and on a single-core one async_tcp preempts `loop()` mid-step, so a second writer races `loop()`. Examples: `/linkRemote` setting a rolling code that a received frame overwrites a moment later, `/deleteShade` clearing a shade halfway through `checkMovement`, both tasks writing `/shades.cfg` and the plain `isDirty` flag, and `/controller` streaming a document in which one shade has two positions.

## Considered Options

- **A mutex around the shade model.** Rejected: every reader and writer has to remember to take it, and a missed lock is a silent bug. A chunked `/controller` response would hold it for seconds and stop `loop()` from receiving frames and moving shades in the meantime.
- **Keep the mix.** Shade commands were already queued while model edits and reads ran on async_tcp. Rejected because the races above stay.

## Consequences

- async_tcp must still never wait for `loop()`, so a model edit's HTTP reply can't block until the change is made. The reply only says the request was accepted; the result reaches clients as a WebSocket event once `loop()` has made the change. We chose that over a chunked HTTP response that `loop()` completes, because pushing results to clients is what the WebSocket is for and it needs no pool of pending responses.
- A read has to reply with data, so it does wait, without blocking: its chunked response sends nothing until `loop()` has filled the read's ModelSnapshot.
- MQTT commands already run on `loop()` (`mqtt.loop()` is called from it).
- With one writer, the domain modules are plain single-threaded code and can be tested natively without threads.
