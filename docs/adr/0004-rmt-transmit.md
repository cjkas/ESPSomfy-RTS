---
status: accepted
---

# Frames go on air through the RMT peripheral

The radio task hands each frame to the RMT peripheral as a list of symbols and sleeps until it has gone out. It used to toggle the TX pin itself with `delayMicroseconds()` busy-waits. The radio task runs at priority 5 on `loop()`'s core, so `loop()` stopped for the whole transmission: about 365 ms for a command with one repeat (#84). Air time is unchanged; a burst of commands still goes out one frame at a time.

`SomfyWaveform` builds the symbols from the payload and is unit tested natively. Its timings are the ones the busy-wait code used, which differ on purpose from the ones published online: the wakeup (10920/7357 µs), the software sync (4850 µs) and the gap after a 56-bit frame (27434 µs) were tuned against a Telis remote upstream. The only difference is a pulse of about 50 ns that the old code made after a final 0 bit, which had no effect.

## Considered Options

- **Lower the radio task's priority.** Rejected: with busy-waits, `loop()` would preempt it in the middle of a frame and stretch the pulses.
- **Move the radio task to the other core.** Rejected: it would then hold up WiFi and the web server instead of `loop()`.
- **Let the RMT driver refill the channel's memory from its interrupt** so the channel can stay small. Rejected: that interrupt isn't cache safe in the prebuilt IDF (`CONFIG_RMT_TX_ISR_CACHE_SAFE` is off). It waits out every flash write, and the NVS write of the rolling code now runs during the first frame, so the hardware would replay old symbols on air. Turning the option on means rebuilding the IDF libraries with `custom_sdkconfig`.
- **Queue the next frame while one is on air**, so frames follow each other with no software delay. Rejected: the driver encodes a queued frame in its interrupt, which waits out flash writes the same way.
- **Arduino's `rmtWrite()`.** Rejected: its timeout leaves the transmission running, and `rmtInit()` adds a 1 ms delay and a 10-deep queue that aren't needed.

## Consequences

- A whole frame fits in the channel's memory: 128 symbols on the ESP32 and 144 on the S3, taking the memory of the channels after it. The largest frame is an 80-bit first frame relayed for a repeater with 24 syncs (#94), 106 symbols with the end marker. The C6 has only 96, so such a frame goes out with fewer syncs there and a warning is logged.
- One `rmt_transmit()` per frame. Between frames the task wakes up and builds the next one, which adds some tens of µs, more if a flash write is in progress. 80-bit frames have no gap after them (#84), so that time is the only gap between them.
- A flush or a task waiting to reconfigure the radio still stops a job between frames, never in the middle of one.
- The channel is created when a job starts and deleted when it ends, so a TX pin shared with RX is free for receiving between jobs and a changed TX pin needs nothing else.
- A frame that hasn't gone out after 500 ms stops the channel and ends the job as cut short.
- About 40–90 ms of the 365 ms wasn't air time, probably the NVS write of the rolling code. It is measured separately.

## References

- [ESP-IDF RMT driver](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/rmt.html)
- [Somfy RTS protocol (pushstack)](https://pushstack.wordpress.com/somfy-rts-protocol/), [Nickduino/Somfy_Remote](https://github.com/Nickduino/Somfy_Remote), [Legion2/Somfy_Remote_Lib](https://github.com/Legion2/Somfy_Remote_Lib): the 56-bit frame. Their timings are the ones upstream tuned away from.
- [rstrouse/ESPSomfy-RTS#578](https://github.com/rstrouse/ESPSomfy-RTS/issues/578): 80-bit motors expect 12 hardware syncs on the first frame and 6 on repeats.
