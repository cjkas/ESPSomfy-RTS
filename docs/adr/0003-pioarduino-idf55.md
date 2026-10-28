# Firmware builds on pioarduino (Arduino 3.x, ESP-IDF 5.5), pinned to one release

We moved every firmware env from `espressif32 @ 6.13.0` (Arduino 2.0.17 on ESP-IDF 4.4.7) to pioarduino's `55.03.312` (Arduino 3.3.12 on ESP-IDF 5.5). IDF 4.4 no longer gets security fixes for Wi-Fi, lwIP and mbedTLS, and the planned RMT radio needs IDF 5's RMT driver. The official PlatformIO platform never shipped Arduino 3.x, so pioarduino, a community fork, is the only way to get it in PlatformIO. We pin one release URL rather than `stable`, so a framework upgrade is always its own PR with its own size check and hardware test.

## Considered Options

- **Stay on IDF 4.4.** Rejected: no more security fixes, and no new RMT driver.
- **Rebuild the IDF libraries with `custom_sdkconfig`** to leave out PPP, NAT and provisioning (about 20 KB the firmware never uses). Deferred: it makes builds much slower, and log level 2 already makes the ESP32 build fit.
- **Grow the app slots in the partition table.** Rejected: an OTA update can't change the partition table, so every existing device would need a USB flash.

## Consequences

- **Firmware size.** The ESP32 image grows from 1,382,640 bytes (v3.0.14) to 1,555,472 at `CORE_DEBUG_LEVEL=2`, leaving 17 KB of the 1.5 MB OTA slot. At level 3 it doesn't fit, so release envs build at level 2 (warnings and errors) and debug envs stay at 3. PlatformIO's "Flash:" figure counts ELF sections, not the image, and reads about 45 KB low, so CI checks `firmware.bin` against the app partition and fails under 8 KB free (`.github/size_check.py`).
- **No more C3 builds.** Its image grows by 214 KB to 1,651,216 bytes, 76.5 KB over the OTA slot even at level 2. C3 boards stay on the 3.0.x releases.

  > **Amended in 4.0.0.** The C3 builds again. Without the old API (ports 8080 and 8081), its image is 1,498,512 bytes at level 2 and leaves 72.6 KB of the OTA slot.
- **Ethernet PHY ids are ours.** Arduino 3.x renumbered `eth_phy_type_t` (`ETH_PHY_GENERIC` is 0). NVS, backups and the web UI keep the Arduino 2.x numbers as `eth_phy_id_t` (`src/EthPhy.h`), translated by name when Ethernet starts. Never cast a stored id to the framework enum.
- **Ethernet only on the original ESP32.** Arduino 3.x has the RMII MAC API only where `CONFIG_ETH_USE_ESP32_EMAC` is set. S3 and C6 build without Ethernet.
- **Updates roll back until proven.** We override the core's `verifyRollbackLater()` hook to return true, and `loop()` marks the new firmware valid once it has run 60 s and the network is up. A reset before then boots the previous firmware. The LittleFS image an update wrote stays, so the old firmware runs with the new web UI.
- **Tooling.** pioarduino needs its own Core fork (`pip install pioarduino`, the pioarduino IDE extension in VS Code), which it installs into `~/.platformio/penv` in place of PlatformIO Core.
