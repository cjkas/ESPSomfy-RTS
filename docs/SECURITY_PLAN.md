# Plan: authentication on every entry point, one HTTP server

**Status:** agreed, nothing built yet.
**Written:** 2026-10-18, on top of PR #64 (stored secrets are write-only). Terms are defined in [GLOSSARY.md](../GLOSSARY.md).

## Why

- **Port 80 doesn't check login.** About 60 of its 70 routes call no auth check at all: settings, security, network, MQTT, radio, firmware, `/restore`, pairing, rolling codes. Only the ten that share a handler with port 8081 check it (`/controller`, `/rooms`, `/shades`, `/groups`, `/room`, `/shade`, `/group`, `/backup`, `/reboot`, `/downloadFirmware`). Logging in only hides parts of the UI.
- **The token key is public.** API tokens are `HMAC(serverId, PIN-or-password + client IP)`. `serverId` is the last 3 bytes of the MAC address, which every device on the LAN can see, and it is also published in mDNS, in the retained MQTT `serverId` topic and in `/discovery`. Anyone who sees a token can recover a 4-character PIN offline. Tokens never expire.
- **`/login` has no rate limit.** A digits-only PIN (10,000 options) falls to an online attack within minutes.
- **CORS is wide open.** Every response carries `Access-Control-Allow-Origin: *` and `Allow-Headers: *`. Any website open in a browser on the LAN can call the device and read the replies.
- **The websocket has no auth.** It is read-only, but room 0 (the frame log) carries received remote addresses and rolling codes.
- **`serveStatic("/")` serves all of LittleFS**, including `shades.cfg` and `shades.tmp`.
- **Three servers for one device.** Port 80 (UI), 8081 (API) and 8080 (websocket). 8081 dates from upstream's blocking `WebServer`, so that Home Assistant calls didn't wait behind UI file downloads. Every server is now an `AsyncWebServer` on the same AsyncTCP task, and every API route except `/discovery` is already registered on port 80 too.

## Threat model

- **In scope:** another client on the same LAN that can send HTTP and websocket requests. That includes a browser on the LAN running a malicious page.
- **Out of scope:** someone sniffing traffic (everything is plain HTTP, and TLS on the ESP32 is out of reach), and a device exposed to the internet.

## Decisions

### Auth rules
- Every entry point applies the rules `Web::isAuthenticated(request, cfg)` already applies on 8081. No new rules are invented.
  - **Security None:** open.
  - **PIN or password:** a valid `apikey` is required.
  - **Secure Config Only:** control and read routes are open; config routes need `apikey`.
- `serverId` stays public everywhere, including `/loginContext`. It is an identity, and HA uses it as the device's unique id. Only the token key changes (PR B).

### Route access levels (port 80 and the 8081 alias)
- **Public:** static web assets, `/loginContext`, `/login`, `/upnp.xml` (SSDP), OPTIONS preflight requests.
- **Control** (`cfg=false`):
  - reads: `/controller`, `/rooms`, `/shades`, `/groups`, `/room`, `/shade`, `/group`, `/discovery`
  - commands: `/shadeCommand`, `/groupCommand`, `/tiltCommand`, `/repeatCommand`, `/setPositions`, `/setSensor`, `/setMyPosition`
  - `/groupOptions`
- **Config** (`cfg=true`): everything else:
  - settings get/set: security, network, MQTT, radio, general, IP, `/connectwifi`, `/scanaps`, `/modulesettings`
  - pairing, rolling code, linking, deleting, `/getNext*`
  - firmware and filesystem: `/getReleases`, `/downloadFirmware`, `/cancelFirmware`, `/update*`, `/recoverFilesystem`
  - `/backup`, `/restore`, `/reboot`, `/shades.cfg`, `/shades.tmp`
  - `/sendRemoteCommand`, `/beginFrequencyScan`, `/endFrequencyScan`
- The table lives in one pure function with native tests, not in ~70 hand-placed checks.
- `serveStatic` refuses `*.cfg` and `*.tmp`, so the rolling codes are only reachable through the authenticated routes.

### Websocket
- **Handshake:** the token goes in `?apikey=<token>`, because browsers can't set headers on a websocket. `handleHandshake` checks it with the same comparison the HTTP routes use and stores an `authenticated` flag on the client slot. `request->url()` is the path only, so the token doesn't reach the slow-request log.
- **Rules:**
  - **Security None:** open.
  - **PIN or password:** the handshake needs a valid token.
  - **Secure Config Only:** anyone can connect; only an authenticated connection can join room 0.
- **Logging in with a socket already open:** the UI reconnects the socket with the token. There is no in-band upgrade message.
- **Origin:** a handshake whose `Origin` header doesn't match `Host` is refused.

### CORS
- The wildcard `Access-Control-Allow-*` default headers go. The UI is served from the same origin, and HA and scripts aren't browsers, so CORS doesn't affect them.
- A build flag turns the headers back on for UI development from `file://`, which `baseUrl` in `index.js` supports.

### UI
- On a 401, the UI opens the login panel (`security.authUser()`) and drops that request, with no automatic retry. The user repeats the action.

### Token key (PR B)
- The token keeps its shape, `HMAC(key, PIN-or-password + client IP)`: stateless, and it survives reboots. Only the key changes.
- **Key:** 32 random bytes from `esp_fill_random`, generated on first boot in their own NVS namespace. Never sent anywhere, never in a backup, replaced only when NVS is erased. There is no rotate button: changing the PIN or password already changes every token.
- **Comparison:** constant-time, used for HTTP and the websocket handshake alike.
- **Migration:** none needed. The browser keeps its token in memory only, and HA logs in again at runtime (HA PR).

### Login throttling (PR C)
- Per client IP: after 5 failed `/login` attempts, refuse that IP for 30 s, doubling each time up to 15 min. A success resets it.
- Only `/login` is throttled. After PR B a token is a 256-bit HMAC with a secret key, so guessing one through `apikey` is hopeless.

### One server on port 80
- Recorded as ADR-0003, "One HTTP server on port 80; 8081 and 8080 are deprecated aliases", plus an "API and ports" section in the README for script and Node-RED users.
- **Port 80 serves everything:** the UI, the API (with `/discovery` added) and the websocket at `ws://host/ws`. The mDNS service record moves to port 80, with a TXT entry for the websocket path.
- **Aliases:** 8081 and 8080 keep working for one release, with the same routes and the same auth.
- **Deprecation signal:** `ESP_LOGW` once per boot on the first use of 8081 or 8080, plus a release-notes entry.
- **Removal:** in the first firmware release after the HA fork's port-80 release is out.

### Home Assistant ([ESPSomfy-RTS-HA](https://github.com/cjkas/ESPSomfy-RTS-HA))
- Today HA logs in only during the config flow, on a temporary API object, and sends `apikey: ""` at runtime. It only works with security None or Secure Config Only.
- **Ports:** it probes `http://host/discovery` on port 80 and falls back to 8081/8080 on a 404, so it works with firmware from before PR 0.
- **Login:** at `async_setup_entry` it logs in with the PIN or password stored in `entry.data`. On a 401 it logs in again once and retries; if that fails, it raises HA's reauth flow.
- **Websocket:** it adds `?apikey=` to the websocket URL.

## PR sequence

| # | Repo | Scope | Breaks anything? |
|---|---|---|---|
| 0 | firmware | `/discovery` and `/ws` on port 80, UI switches to `/ws`; deprecation log on 8081/8080; ADR-0003 and README "API and ports" | no |
| HA | HA fork | port-80 probe with fallback; runtime login, re-login on 401, reauth flow; `?apikey=` on the websocket | no |
| A | firmware | access table on port 80 and the 8081 alias; websocket handshake auth and room 0; `*.cfg`/`*.tmp` static filter; UI 401 handling; CORS headers removed; Origin check | old HA with full security (already broken today) |
| B | firmware | random HMAC key in NVS; constant-time token comparison | no (tokens are re-issued on login) |
| C | firmware | per-IP `/login` backoff | no |
| D | firmware | remove the 8081 and 8080 aliases, one release after HA's port-80 release | scripts still on 8081/8080 |

The HA PR must merge and ship before PR A.

## Tests

The rules go into small pure helpers with native tests, following [src/Secrets.h](../src/Secrets.h):
- the route-to-access-level table
- the token comparison
- the Origin/Host match
- the backoff schedule

Everything else is checked with the `esp32devdbg` build, plus curl and websocket checks against a device under each security type.

## Not in this plan

- TLS.
- Session tokens with expiry.
- Contents of the binary backup file (it holds no passwords, but it does hold rolling codes; `/backup` becomes a config route).
