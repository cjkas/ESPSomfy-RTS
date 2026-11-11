---
status: proposed
---

# Noise encrypts the socket, keyed by the PIN or password

Everything between a client and the device travels in clear. `/login` sends the PIN or password. The token it returns never expires and goes with every request, so one captured request gives access until the password changes. The sync carries every shade's rolling code, and a security save carries the new password. [SECURITY_PLAN.md](../SECURITY_PLAN.md) left sniffing out of scope because TLS doesn't fit in the heap.

We decided that a passive sniffer is now in scope. All authenticated traffic goes over the socket, encrypted with Noise (`Noise_NNpsk0_25519_AESGCM_SHA256`). Its pre-shared key, the *login key*, is derived from the PIN or password with PBKDF2, so users keep logging in the way they do today. HTTP keeps only the public routes and the three script commands. This ships as 5.0.0.

## Threat model

- **In scope:** another client on the LAN that sends requests (as before), and a **passive sniffer**: anyone who can read LAN traffic without changing it, such as someone with the WiFi password who captured a client's handshake, a mirrored switch port, or a compromised device on the network.
- **Out of scope:** an **active attacker on the path** (ARP spoofing, a rogue access point). The web UI's files are still served over plain HTTP, so such an attacker can serve a changed page that leaks what the user types. Only HTTPS stops that (see Considered Options). A device exposed to the internet stays out of scope.
- **Weak secrets are guessable offline.** One captured handshake lets a sniffer test guesses at home, as fast as PBKDF2 allows. A strong password holds; a 4-digit PIN falls in minutes. The PIN mode stays, protecting against LAN clients but not against sniffers, and the web UI says so when it is chosen.

## Budget

The ESP32 without PSRAM (esp32dev) sets the limits. A part that misses them in the measuring spike is cut back, not the budget raised.

| | Limit | Measured |
|---|---|---|
| Heap per connected client | ≤ 200 B | step 0 |
| Heap during a handshake (freed after) | ≤ 2 KB | step 0 |
| Flash, all of it | ≤ 10 KB | step 0 |
| Handshake time on the device | ≤ 100 ms | step 0 |
| Cost per frame | none measurable next to the link | step 0 |
| New C libraries, framework rebuild (`custom_sdkconfig`) | none | |

The framework's mbedtls already has X25519, SHA-256, HKDF, PBKDF2 and AES-GCM (with hardware AES and SHA). ChaCha20-Poly1305 is not compiled in, hence AES-GCM.

## How it works

- **Login key.** `PBKDF2-HMAC-SHA256(secret, salt, iterations)`, 32 bytes. The secret is `username:password`, or the PIN. The salt is 16 random bytes the device makes once and keeps in NVS. `/loginContext` publishes the salt and the iteration count, so the count can rise later without a protocol change. The count is set in step 0 so that one derivation takes about 1 s in pure JS on a mid-range phone.
- **Where the login key lives.** The device derives it when the security settings are saved and keeps it in NVS. The first boot after the upgrade derives it once from the stored password. The browser derives it at login and keeps the login key, never the password, in `sessionStorage`. Home Assistant derives it at setup and keeps it with its credentials. A connect costs the handshake only, never the derivation.
- **None.** The login key is 32 zero bytes. The handshake and the encryption are the same: anyone may connect, but a sniffer can't read the traffic.
- **Handshake.** The client puts Noise message 1 (48 bytes) in the upgrade: `/ws?n=<base64url>`. Message 1 already proves the login key, so the device refuses a wrong one with 401 before upgrading, and the login throttle counts it as a failed login (429 while locked out). The device answers with message 2 as the first binary frame. The prologue is `ESPSomfy-RTS/2` and the device's `serverId`, so a handshake made for one device fails on another. There is no round trip beyond today's upgrade.
- **Frames.** After the handshake every frame is a binary websocket frame holding one Noise transport message: at most 4 KB of plaintext and a 16-byte tag. The plaintext is the JSON envelope of [ADR 0005](0005-one-websocket-protocol.md), or a file chunk. A frame that fails to decrypt closes the socket. The 64-bit nonce counter can't wrap within a connection, so there is no rekeying.
- **Events.** An event is built once and encrypted for each client as it is sent.
- **File ops.** Backup, restore and both updates become socket ops, so the file goes over the encrypted socket rather than a second TCP connection. A chunk is a type byte, the op's 2-byte id and up to 4 KB of data, with no base64. A backup streams with `next` from the client, as the sync does. A restore or an update sends chunks that the device acknowledges one by one, then an end op that replies with the result. One file op runs at a time (another gets `busy`), and one with no traffic for 30 s is aborted. Flash writes stay on async_tcp, as the upload handlers do today.
- **Security save.** Saving new credentials derives the new login key and closes every socket, as today. The salt doesn't change, so the client that saved derives the new key itself from what the user typed and reconnects. The reply carries no secret. Other clients get 401 and log in again.
- **HTTP.** Six routes are left: the web UI's files, `/loginContext`, `/upnp.xml`, and `/shadeCommand`, `/groupCommand`, `/tiltCommand`. `/login`, `/backup`, `/restore`, `/updateFirmware` and `/updateApplication` go, and so do the token and the token key.
- **Script key.** Scripts and Node-RED can't run a handshake, so under Secured the three command routes take a *script key*: 128 random bits as 32 hex characters, in the `apikey` header or `?apikey=` as today. The web UI generates it with an op whose reply shows it once, and can replace or clear it. It is kept in NVS and never in a backup. It moves shades and nothing else: no config, no backup, no socket. It travels in clear, so a sniffer can move shades but not take over the device. With no script key set, the command routes refuse everything under Secured; under None they stay open.
- **Implementations.** The firmware has its own NNpsk0 responder on mbedtls (`NoiseSession`, pure, with native tests); noise-c is too large for one pattern. The web UI has its own NNpsk0 initiator (`data-src/js/noise.js`) on a vendored, pinned build of the noble libraries (`@noble/curves`, `@noble/ciphers`, `@noble/hashes`), built by a script in the repo and joined into `index.js` like the other modules. WebCrypto can't help, since browsers offer it only on HTTPS pages. Home Assistant uses the `noiseprotocol` package, which aioesphomeapi already brings in. All three run the official Noise test vectors for this suite.

## Considered Options

- **TLS (HTTPS and WSS).** Rejected on every board, PSRAM ones included. With the framework's mbedtls settings (16 KB records, internal RAM only, no dynamic buffers) a connection costs about 35–40 KB of internal RAM, and Home Assistant plus one browser tab already exceeds the free heap. ESPAsyncWebServer has no TLS server on the ESP32, so it would mean moving to `esp_https_server`, and putting the buffers in PSRAM would need a framework rebuild. The certificate would be self-signed, so every browser warns. Whoever needs real HTTPS can put a reverse proxy (Caddy, nginx, a Home Assistant add-on) in front of the device.
- **CPace, a PAKE** (draft-irtf-cfrg-cpace). It derives the session key from the PIN or password in a way a recorded handshake can't be guessed from, so a PIN would stay safe from sniffers. Rejected in favour of Noise: Noise is a published specification with official test vectors and a maintained Python implementation, where CPace is a draft we would implement ourselves on three platforms, including its map to a curve point. The cost is the offline guessing described above.
- **Noise with a random 32-byte key.** Strong, but the key replaces the PIN and password: it would have to be pasted into every browser and phone, and shown somewhere first. Rejected for the usability cost.
- **Protect Home Assistant only,** as ESPHome does (its native API uses Noise, its web server stays plain HTTP). Rejected: here the web UI is the main client, and it changes passwords and makes backups.
- **A challenge-response login and expiring tokens, without encryption.** It stops a captured request from granting access, but the traffic stays readable, rolling codes and new passwords included.
- **Keep the file routes on HTTP, with bodies encrypted under keys from the socket session and a MAC per request.** Rejected: a second encrypted channel to build and test.
- **Base64 file chunks in the JSON envelope.** Rejected: a third more data, about 5.5 minutes instead of 4 for a firmware update over a weak link.
- **Argon2 instead of PBKDF2.** Rejected: a new library on every platform.
- **Script commands on the login, or removed under Secured.** Rejected: a login key sent in clear would give a sniffer the whole device, and removing the routes breaks today's Node-RED users.

## Consequences

- **Released as 5.0.0, a clean break.** No plaintext mode is kept for a transition. Home Assistant integration 4.0 reads `/loginContext` (`"enc":"noise1"`) and speaks either scheme, so users update Home Assistant first. The web UI is served by the device, so it always matches. The release notes cover: scripts need a script key under Secured, and a PIN doesn't protect against sniffers.
- **[ADR 0005](0005-one-websocket-protocol.md) and [ADR 0006](0006-two-security-modes-eleven-http-routes.md) are amended:** binary frames, file ops on the socket, `?n=` in place of `?apikey=`, and six HTTP routes.
- **The login throttle counts failed handshakes** in place of failed `/login` requests.
- **Each encrypted client costs a copy of every event** it is sent. Events are a few hundred bytes, and there are at most five clients.
- **Firmware updates use the socket's slot** rather than a second connection, which saves about 14 KB of heap during an upload.
- **The README gets a security section** that says what is encrypted and what isn't: the web UI's files and the script commands travel in clear, and an active attacker can change the page.
- **Tests.** Native tests run `NoiseSession` against the official vectors and cover the chunk format. The JS client runs the same vectors with `node --test`, and Home Assistant's tests use a fake device that speaks Noise. The e2e suite connects through `noiseprotocol`.
- **Order of work** is in [SECURITY_PLAN.md](../SECURITY_PLAN.md): a measuring spike first, then each layer next to today's token, the removals last.
