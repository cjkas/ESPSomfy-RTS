# Known Issues

## Concurrent mutation during chunked `/controller` response

**Status:** fixed: reads in step 2, edits in step 3; command replies still read the live model (below).
**Introduced:** 2026-04-19, alongside the chunked-response conversion of `/controller` (the original crash report was about `cbuf::resize` aborting `async_tcp`).
**Fixed:** 2026-10-16, steps 2 and 3 of [LOOP_QUEUE_PLAN.md](LOOP_QUEUE_PLAN.md).

### What it was

The chunked `/controller` response read `somfy.rooms`, `somfy.shades`, `somfy.groups` and `somfy.repeaters` on async_tcp a piece at a time as the client's TCP window opened, for hundreds of ms to seconds. Anything that changed them meanwhile could make the document contradict itself: a deleted shade in `shades` but not in a group's `linkedShades`, two positions for one shade, a linked shade skipped or written twice. `/discovery`, `/shades` and the other reads had the same exposure, only shorter.

All eight read routes (`/controller`, `/discovery`, `/rooms`, `/shades`, `/groups`, `/room`, `/shade`, `/group`) now stream a ModelSnapshot that `loop()` fills, so each document describes one moment ([ADR-0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md)). That is fix option (c), taken per read in a heap slot of about 5.6 KB.

### Edits (step 3)

The handlers that changed the shade model on the web server task (adding, saving and deleting rooms, shades and groups; linking remotes, repeaters and group members; `/setPaired`; the sort orders) now hand the edit to `loop()` through the LoopQueue, and every client learns the result from a `requestResult` socket event. So `/linkRemote` can no longer race a received frame for a linked remote's rolling code: both now change it on `loop()`, in the order they happen.

### Still open

- Command replies (`/shadeCommand`, `/tiltCommand`, `/unpairShade`, `/setRollingCode`, ...) still copy the one item they report from the live shade model on async_tcp, so a reply can show a value from the middle of a change. `/room`, `/shade`, `/group` and `/groupOptions` also check on the live model whether the item exists, because the status goes out before the snapshot is filled.
- The read routes would leave out an item that didn't fit their 3 KB unit; a test now proves none can, see [Oversized read items are left out](#oversized-read-items-are-left-out).

## Oversized read items are left out

**Status:** guarded by a test (fix option (b)). It can't happen with today's fields, and a change that would make it happen fails `test_snapshot_limits`.
**Location:** `SnapshotWriter::overflowed` in [src/ModelSnapshot.cpp](../src/ModelSnapshot.cpp); the unit is `SNAPSHOT_UNIT_SIZE` in [src/ModelSnapshot.h](../src/ModelSnapshot.h).

### Summary

Every read route builds each room, shade, group and linked shade into a 3 KB unit before sending it. When one doesn't fit, the writer logs an `ESP_LOGE` line, skips that item and carries on, so the document stays valid JSON. The client gets no sign that anything is missing: the item just isn't in the list. Before step 2 only `/controller` did this; `/discovery`, `/shades` and the rest grew their buffer instead.

`test_snapshot_limits` fills a snapshot with the largest possible items: 20-character names of control characters (each escaped to `\u00XX`), all 7 linked remotes, 32 members in every group, and the widest value of every number. It checks that every read document written with the real unit is identical to the same document written with an unlimited one. The largest items are a shade at 1,097 bytes, a linked shade at 345, a group at 333 and a room at 159, against 3,072. The log line stays as a backstop.

### Concrete failure modes

1. A shade is left out after a firmware change adds fields to it. It is missing from the config UI, and Home Assistant doesn't create an entity for it.
2. A group whose `linkedShades` entry doesn't fit loses that shade from its list.

### Fix options (pick one later)

- **(a) Fail loud.** Add a list of the skipped item ids to the end of the document, and show a warning in the UI.
- **(b) Check the size at build time.** Work out the worst case for each item from the field limits and `static_assert` that it fits the buffer, so a change that breaks it doesn't build.

### Related

- The same "lost without telling the client" problem as [Large websocket events are dropped](#large-websocket-events-are-dropped).

## Large websocket events are dropped

**Status:** fixed (fix option (c)): the buffer is 4 KB, and the largest event provably fits.
**Location:** [`JsonSockEvent` in src/WResp.cpp](../src/WResp.cpp), buffer `g_response[MAX_SOCK_RESPONSE]` (now 4096 bytes, defined in [src/outputs/SocketEmitter.h](../src/outputs/SocketEmitter.h)).

### Summary

Socket events are built into one static buffer. When a value doesn't fit, [`JsonSockEvent::_safecat`](../src/WResp.cpp) marks the event as overflowed, and `endEvent` logs an error and doesn't send it. Clients never see malformed JSON. The event is simply lost, and the client isn't told.

Unlike the `/controller` HTTP crash, this path does **not** abort: there is no growing cbuf and no `new[]` on the send path.

### What can be large

- **`remoteFrame`** (the frame log) lists every pulse of a received frame: up to `MAX_TIMINGS` = 300. A pulse recorded while waiting for a sync has no upper bound, so in theory each can be 10 digits: 300 × 11 bytes plus about 300 bytes of fields is about 3.8 KB. Real frames come to 1.8–2.1 KB, which was already at the old 2 KB limit. A `static_assert` in `Transceiver.cpp` fails the build if `MAX_TIMINGS` or the buffer change so that it no longer fits.
- **The state events are small.** `shadeState` has about 20 fields and no linked remotes, about 400 bytes. (The 1.3–1.5 KB figure this entry used to give was for a shade *with* its linked remotes, which only the read routes send.) `groupState` lists at most 32 shade ids. `frequencyScan`, `memStatus`, `fwStatus`, `ethernet` and `requestResult` have a few fixed fields each.

The 2 KB more of static RAM is the cost. The log line stays as a backstop.

### Options that weren't taken

- **(a) Fail loud:** send a small error event in place of the dropped one so the client reloads the state.
- **(b) Split large events across frames:** the socket.io ack/chunk pattern, with matching client-side reassembly.

### Related

- Not the same code path as the `/controller` crash. Solve independently.
- Worth grepping for `JsonSockEvent` usages that iterate collections (see references in `SomfyEvents.cpp`, `Transceiver.cpp`, `ESPNetwork.cpp`, `GitOTA.cpp`) to identify the most at-risk events.
- [Group reads emit events from the web server task](#group-reads-emit-events-from-the-web-server-task) (fixed): a second task writing the same buffer.

## Group reads emit events from the web server task

**Status:** fixed 2026-10-16, step 2 of [LOOP_QUEUE_PLAN.md](LOOP_QUEUE_PLAN.md) (fix option (a)). Read routes write from a ModelSnapshot, and the replies still written on async_tcp copy the group with `snapshotGroup()`, which works its flags out without changing the group or sending anything.
**Location:** `serializeGroupRef` in [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp) called `SomfyGroup::updateFlags()` in [src/SomfyGroup.cpp](../src/SomfyGroup.cpp).

### Summary

Serializing a group for an HTTP reply first calls `group->updateFlags()`. When the group's flags no longer match its shades', that writes `group->flags`, sends a `groupState` socket event and publishes the group's MQTT topics, all on the async_tcp task, while `loop()` sends events and publishes too.

Every socket event is formatted into the one static buffer `g_response` in [src/outputs/SocketEmitter.cpp](../src/outputs/SocketEmitter.cpp) through the one `JsonSockEvent` in `sockEmit`. Two tasks emitting at once write over each other's event. The MQTT client (PubSubClient) isn't safe to use from two tasks either.

Reached from every route that serializes a group: `/controller`, `/discovery`, `/groups`, `/group`, `/groupCommand`, `/addGroup`, `/saveGroup`, `/linkToGroup`, `/unlinkFromGroup` and others.

### Concrete failure modes

1. A `groupState` from a `/groups` request and a `shadeState` from `loop()` are formatted at the same time: clients receive garbled JSON, or an event with another event's fields.
2. The group's MQTT publish interleaves with one from `loop()` in the client's buffer, and the broker gets a broken packet or the connection drops.
3. `group->flags` is written on async_tcp while `loop()` updates the same group through `updateGroupFlags()`.

It needs a group whose flags are out of date at the moment a client reads it, for example right after a sensor frame changed a linked shade's sun or wind flags, so it is rare.

### Fix options

- **(a) Drop `updateFlags()` from the serializers.** `loop()` already keeps group flags current through `updateGroupFlags()`. This is part of step 2 of [LOOP_QUEUE_PLAN.md](LOOP_QUEUE_PLAN.md): reads come from a ModelSnapshot that `loop()` fills, and serializers only read.
- **(b) Only emit from `loop()`.** Covered by ADR-0002 once nothing outside `loop()` changes the shade model.

### Related

- [ADR-0002](adr/0002-loop-task-is-the-only-writer-of-the-shade-model.md): the loop task is the only writer of the shade model.
- [Large websocket events are dropped](#large-websocket-events-are-dropped): the same `g_response` buffer.

## A saved WiFi passphrase or MQTT password can't be cleared from the UI

**Status:** open. A workaround exists (below).
**Introduced:** 2026-10-18, when the device stopped sending stored secrets back to clients.
**Location:** `applySecret` in [src/Secrets.h](../src/Secrets.h), called from `WifiSettings::fromJSON` and `MQTTSettings::fromJSON` in [src/ConfigSettings.cpp](../src/ConfigSettings.cpp) and from `/connectwifi`. The UI side is `keepSavedPassphrase` in `Wifi` and `connectMQTT` in `MQTT`, in [data-src/index.js](../data-src/index.js).

### Summary

The settings routes report only whether a secret is stored (`hasPassphrase`, `hasPassword`), so the UI loads every secret field empty. The firmware decides what an empty secret in a request means, so that every UI version is safe, including one older than this change, which sends `""` for the field it can no longer fill in:

| Secret | Key absent | `""` | A value |
|---|---|---|---|
| WiFi passphrase | keep | keep, or clear if the same request changes the SSID | replace |
| MQTT password | keep | keep, or clear if the same request changes the username | replace |
| Login password, PIN | keep | keep | replace |

Without "empty keeps", saving any other setting from an old UI would wipe the secret: for WiFi the device would reboot and drop into AP mode.

The cost: an empty value can't mean "no password" while the SSID or username stays the same. Switching to a different open network works.

The login password and PIN aren't affected. Neither can be empty, and security is turned off with the type selector.

### Concrete failure modes

1. The network the device is on is changed to open on the router, keeping its SSID. Saving the network page with a blank passphrase keeps the old one. When a passphrase is set, `WiFi.begin` won't accept anything weaker than WPA2-PSK, so the device can't join the now-open network.
2. The MQTT broker stops requiring a password but keeps the username. The device keeps sending the old password.

### Workaround

- **MQTT:** save once with a different username, then change it back and save again with the password blank.
- **WiFi:** there's no safe workaround. Saving a different SSID makes the device reboot and try to join that network.

### Fix options

- **(a) "Clear saved password" checkbox** next to the WiFi passphrase and MQTT password fields, shown only when a value is stored. Since `""` means keep, ticking it sends an explicit flag (`clearPassphrase` / `clearPassword`), which `fromJSON` turns into `applySecret(..., identityChanged = true)`.

## Port 80 routes don't check login

**Status:** open. Fix planned as PR A of [SECURITY_PLAN.md](SECURITY_PLAN.md).
**Location:** `registerWebUiRoutes` in [src/entryPoints/WebUiRoutes.cpp](../src/entryPoints/WebUiRoutes.cpp).

### Summary

Only the ten port-80 routes that share a handler with port 8081 call `isAuthenticated`. The other ~60 check nothing, including every settings route, `/restore`, firmware updates, pairing and rolling codes. Logging in only hides parts of the UI. `serveStatic("/")` also serves `shades.cfg` and `shades.tmp`, which hold the rolling codes, to anyone.

### Concrete failure modes

1. With a PIN or password set, any LAN client can `PUT /saveSecurity` and replace them, or change the network, MQTT or radio settings.
2. Any LAN client can download `/shades.cfg` and read every shade's remote address and rolling code.

## API token key is public

**Status:** open. Fix planned as PR B of [SECURITY_PLAN.md](SECURITY_PLAN.md).
**Location:** `Web::createAPIToken` in [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp).

### Summary

Tokens are `HMAC(serverId, PIN-or-password + client IP)`. `serverId` is the last 3 bytes of the MAC address, and it is published in mDNS, in the retained MQTT `serverId` topic, in `/discovery` and in `/loginContext`. So the HMAC key gives no protection: whoever sees a token can try every PIN offline. Tokens never expire, and they are compared with a non-constant-time `String` comparison. `serverId` itself is an identity (HA's unique id) and stays public. Only the key is replaced.

## CORS allows any origin

**Status:** open. Fix planned as PR A of [SECURITY_PLAN.md](SECURITY_PLAN.md).
**Location:** `DefaultHeaders` in `Web::begin`, [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp).

### Summary

Every response carries `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: PUT,POST,GET,OPTIONS` and `Access-Control-Allow-Headers: *`. Any website open in a browser on the LAN can call the device and read the replies. With security None it can change every setting, without the user noticing.

## Websocket has no auth

**Status:** open. Fix planned as PR A of [SECURITY_PLAN.md](SECURITY_PLAN.md).
**Location:** `SocketEmitter::wsEvent` in [src/outputs/SocketEmitter.cpp](../src/outputs/SocketEmitter.cpp).

### Summary

Anyone can connect to `ws://host:8080/` and join any room. Clients can't change anything over it, but they receive all shade and group state, and room 0 (the frame log) carries received remote addresses and rolling codes. Browsers don't apply CORS to websockets, so a page from any origin can connect too.

## `/login` has no rate limit

**Status:** open. Fix planned as PR C of [SECURITY_PLAN.md](SECURITY_PLAN.md).
**Location:** `Web::handleLogin` in [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp).

### Summary

Failed logins aren't throttled. A digits-only 4-character PIN has 10,000 options and falls to an online attack within minutes. An alphanumeric one has about 1.7 million and lasts a day or two at about 10 attempts per second.

## Boot waits 5 s for the clock and then skips the time zone

**Status:** open. Seen in the serial logs from 2026-10-18 and 2026-10-19.
**Location:** `NTPSettings::apply` in [src/ConfigSettings.cpp](../src/ConfigSettings.cpp), called from `ConfigSettings::begin` in `setup()`.

### Summary

`apply()` calls `configTime()` and then `getLocalTime(&dt)`. Arduino's `getLocalTime` takes a timeout that defaults to 5000 ms, and polls until the year is past 2016. `setup()` calls it before the network is up, so on a boot whose clock isn't set yet it waits the full 5 s and returns false. In the logs, `File system mounted successfully` is followed 5.1 s later by `Launching web server...`. A soft reboot four hours after boot passed straight through in 90 ms, because the RTC keeps the time across a soft reset. One soft reboot two minutes after a cold boot still waited the full 5 s, presumably because SNTP hadn't set the clock yet.

`apply()` returns as soon as `getLocalTime` fails, before `setenv("TZ", ...)`. Nothing else sets `TZ`. So after such a boot the configured time zone isn't applied.

### Concrete failure modes

1. Every cold boot is 5 s slower. The web server, WiFi and the radio all start after the wait.
2. After a cold boot, local times are worked out without the configured time zone, as if it were UTC, until something calls `apply()` again.

### Fix options

- **(a) Don't wait.** Call `getLocalTime(&dt, 0)`, or skip the check, and set `TZ` whatever the result. SNTP sets the clock in the background once the network is up.
- **(b) Use `configTzTime(posixZone, ntpServer)`.** It sets the zone and the server together and doesn't wait.

## A restore reboots even when the backup fails validation

**Status:** open. Seen during a restore on 2026-10-18.
**Location:** the `restore_shades` case in `LoopQueue::drain` in [src/LoopQueue.cpp](../src/LoopQueue.cpp); `ShadeConfigFile::validate` and `restoreFile` in [src/ConfigFile.cpp](../src/ConfigFile.cpp); the `/shades.tmp` route in [src/entryPoints/WebUiRoutes.cpp](../src/entryPoints/WebUiRoutes.cpp).

### Summary

The uploaded backup is written to `/shades.tmp`, and `loop()` restores it with `ShadeConfigFile::restore()`. The result is ignored: the device logs `Rebooting ESP for restored settings...` and reboots whether the restore worked or not. If validation fails, the device reboots on its old settings and the user is never told.

Validation is also looser than it looks. When the file size doesn't match what the header adds up to, `validate()` logs `File size is not correct` but carries on. The repeater records are walked without checking their length. The backup restored on 2026-10-18 was 6 bytes longer than its header said, and was restored anyway. Where the 6 bytes came from wasn't checked; the repeater records are the one section that could hide them.

Nothing deletes `/shades.tmp` afterwards. A full copy of the backup, rolling codes included, stays in LittleFS, and `GET /shades.tmp` serves it without a login (see [Port 80 routes don't check login](#port-80-routes-dont-check-login)). PR A of [SECURITY_PLAN.md](SECURITY_PLAN.md) puts the route behind login and stops `serveStatic` serving `*.tmp`. Even then, the file has no reason to outlive the restore.

### Concrete failure modes

1. A corrupt or wrong-version backup is rejected, the device reboots anyway, and the UI shows the old shades with no error.
2. A file with extra or missing bytes at the end is restored, as long as the record markers line up.
3. Until PR A lands, anyone on the LAN can download the last restored backup long after the restore.

### Fix options

- Only reboot when `restore()` returns true, and send the failure to the client (a `requestResult` event, like the other queued edits).
- Make the size mismatch fail validation, and check the repeater record length like the other record types.
- Delete `/shades.tmp` once the restore has run, whatever the result.

## A shade command holds up the main loop for about 365 ms

**Status:** open. Measured in the serial logs from 2026-10-18 and 2026-10-19.
**Location:** the `shade_request` case in `LoopQueue::drain` in [src/LoopQueue.cpp](../src/LoopQueue.cpp).

### Summary

Every shade command that moves a shade is followed by `Loop queue took 364-370ms` in the log. A command whose shade doesn't need to move takes no time. In a burst of 10 commands from Home Assistant, the four that came after a moving shade waited behind it, and that drain took 426 ms. Once, `Loop somfy took 855ms` came about 12 s after the last command, with no command near it.

Where the time goes inside the request wasn't measured.

### Concrete failure modes

1. While the queue drains, `loop()` doesn't receive or handle anything else: radio frames, sockets, MQTT.
2. A group command, or a burst of shade commands, adds up to several hundred ms per shade that moves.

## A read of `/controller` took 2.4 s

**Status:** seen once, not explained.
**Location:** the `/controller` route; the warning comes from `logElapsed` in [src/Timing.h](../src/Timing.h).

### Summary

On 2026-10-19 the log shows `web GET /controller from 192.168.1.197 total took 2353.274ms (slow)`. That was about 30 s after Home Assistant's socket connected, and 80 s after boot. Every other `/controller` read in the logs took 100-235 ms.

## Log lines that mislead or are missing

**Status:** open. All cosmetic.

- **A command for an unknown shade leaves no trace.** In `Web::handleShadeCommand` in [src/entryPoints/Web.cpp](../src/entryPoints/Web.cpp), the `handleShadeCommand shade=...` line is only written when the shade exists. On 2026-10-18 Home Assistant sent 10 commands to a device that had no shades yet, and the log shows only the response times.
- **`last ping 0ms ago` means the client never pinged.** In the disconnect line in [src/outputs/SocketEmitter.cpp](../src/outputs/SocketEmitter.cpp), a client that never pinged prints 0. Browsers don't ping, so they always show it.
- **`.gz does not exist` errors are noise.** `serveStatic` tries `<file>.gz` first. For files stored without one (`appversion`, `icon.png`), LittleFS logs an `E`-level `does not exist, no permits for creation` line on every request.
