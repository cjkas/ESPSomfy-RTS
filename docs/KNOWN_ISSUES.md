# Known Issues

## Concurrent mutation during chunked `/controller` response

**Status:** open
**Introduced:** 2026-04-19, alongside the chunked-response conversion of `/controller` (see [`ControllerChunker` in src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp) and the original crash report about `cbuf::resize` aborting `async_tcp`).

### Summary

The chunked streaming of `/controller` lazily reads `somfy.rooms`, `somfy.shades`, `somfy.groups`, and `somfy.repeaters` across many `async_tcp` callback invocations as the client's TCP send window opens. If another task mutates those arrays mid-stream, the rendered JSON can be internally inconsistent.

### Why this is strictly worse than the pre-chunked code

The old `handleController` ran the serializers synchronously inside a single request-handler invocation, so the full JSON was built in memory *before* any bytes were sent. Window of exposure: a few milliseconds.

The new `ControllerChunker` reads state as the response drains. On a slow link or under backpressure, that window is hundreds of ms to seconds.

### Concrete failure modes

1. A shade is deleted mid-stream (e.g. via `/deleteShade`) — it may appear in the `shades` array but be missing from a later group's `linkedShades`, or vice versa.
2. `lastRollingCode` / position fields change between the `shades` pass and a later group's `linkedShades` pass — the client sees two values for the same shade in one document.
3. A group's `linkedShades` list is mutated while the chunker iterates inside `S_GROUPS` — an entry is skipped or emitted twice.

### Fix options (pick one later)

- **(a) Document and accept.** In practice users rarely mutate shades while the config UI is loading. Zero code change.
- **(b) FreeRTOS mutex around `somfy` reads/writes.** Acquire for the full duration of the chunked response and in every mutating path (RF RX, MQTT, web handlers). Cleanest but wide-reaching.
- **(c) Up-front snapshot.** At handler start, copy the subset of `somfy` state the response will serialize into the `ControllerChunker`. Defeats part of the memory benefit — a full snapshot of shades + groups is close in size to the old growing cbuf. Could be reduced by snapshotting only minimal fields (IDs, names, rolling codes) and reading the rest live.

### Related

- Same exposure exists in any other endpoint converted to chunked responses next (`/discovery`, `/shades`). Resolve this issue before expanding the pattern.
- Other handlers on the web server task still change the shade model that `loop()` owns:
  - `/addRoom`, `/saveRoom`, `/deleteRoom`, `/addShade`, `/saveShade`, `/deleteShade`, `/addGroup`, `/saveGroup` and `/deleteGroup` add, change and remove rooms, shades and groups.
  - `/linkRemote` and `/unlinkRemote` change a shade's linked remotes. `/linkRemote` also sets a linked remote's rolling code, which `loop()` updates whenever it receives a frame from that remote: the last write wins, so a received frame can undo the code just set.
  - `/linkToGroup` and `/unlinkFromGroup` change a group's linked shades.
  - `/setPaired` sets `paired`.

  Shade commands, set-My, unpair, `/setRollingCode`, a shades restore and `/updateShadeConfig` already go through the web command queue to `loop()`.
- `/controller` leaves out a room, shade, group or linked shade that doesn't fit in its chunk buffer, see [Oversized `/controller` items are left out](#oversized-controller-items-are-left-out).

## Oversized `/controller` items are left out

**Status:** open
**Location:** `ControllerChunker::overflowed` in [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp).

### Summary

`ControllerChunker` builds each room, shade, group and linked shade into a 3 KB buffer before sending it. When one doesn't fit, the chunker logs an `ESP_LOGE` line, skips that item and carries on, so the document stays valid JSON. The client gets no sign that anything is missing: the item just isn't in the list.

A shade with every linked remote populated is about 1.5 KB today, so nothing overflows yet. It becomes a silent loss as soon as a change to the shade, room or group fields pushes an item past 3 KB.

### Concrete failure modes

1. A shade is left out after a firmware change adds fields to it. It is missing from the config UI, and Home Assistant doesn't create an entity for it.
2. A group whose `linkedShades` entry doesn't fit loses that shade from its list.

### Fix options (pick one later)

- **(a) Fail loud.** Add a list of the skipped item ids to the end of the document, and show a warning in the UI.
- **(b) Check the size at build time.** Work out the worst case for each item from the field limits and `static_assert` that it fits the buffer, so a change that breaks it doesn't build.

### Related

- The same "lost without telling the client" problem as [Large websocket events are dropped](#large-websocket-events-are-dropped).

## Large websocket events are dropped

**Status:** open
**Location:** [`JsonSockEvent` in src/WResp.cpp](../src/WResp.cpp), buffer `g_response[MAX_SOCK_RESPONSE]` (2048 bytes) in [src/outputs/Sockets.cpp](../src/outputs/Sockets.cpp).

### Summary

Socket events are built into a fixed 2 KB static buffer. When a value doesn't fit, [`JsonSockEvent::_safecat`](../src/WResp.cpp) marks the event as overflowed, and `endEvent` logs an error and doesn't send it. Clients never see malformed JSON. The event is simply lost, and the client isn't told.

Unlike the `/controller` HTTP crash, this path does **not** abort — there is no growing cbuf and no `new[]` on the send path. Per-client frame allocations inside `AsyncWebSocket` are bounded by the 2 KB buffer size and have their own overflow guard (queue drop / client disconnect).

### Concrete failure modes

1. A single event serializing a fully-populated shade (~1.3–1.5 KB for a shade with all `SOMFY_MAX_LINKED_REMOTES` = 7 populated) gets close to the 2 KB limit. Any additional fields or long names push it over, and that shade's state update never reaches the clients.
2. Any event that loops over a collection (e.g. frequency-scan results in `Transceiver.cpp`, batch emits in `SomfyEvents.cpp`) can exceed 2 KB depending on size, with no indication to the client beyond the ESP-side `ESP_LOGE` line.

### Fix options (pick one later)

- **(a) Fail loud.** Send a small error event in place of the dropped one so the client knows to reload the state.
- **(b) Split large events across frames.** Use the socket.io ack/chunk pattern to send an event in multiple frames when it wouldn't fit. Requires matching client-side reassembly.
- **(c) Raise `MAX_SOCK_RESPONSE`.** Cheapest, but just pushes the limit — doesn't eliminate the failure mode.

### Related

- Not the same code path as the `/controller` crash. Solve independently.
- Worth grepping for `JsonSockEvent` usages that iterate collections (see references in `SomfyEvents.cpp`, `Transceiver.cpp`, `ESPNetwork.cpp`, `GitOTA.cpp`) to identify the most at-risk events.
