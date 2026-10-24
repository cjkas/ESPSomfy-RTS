# e2e suite

Runs against a real device on your network, by hand, never in CI. It checks socket protocol 2 ([PROTOCOL.md](../../docs/PROTOCOL.md)) and that the device survives the loads that used to run its heap out.

```sh
~/.platformio/penv/Scripts/uv run --no-project --with pytest -- pytest test/e2e --device 192.168.1.120
```

| Option | Use |
|---|---|
| `--device` | the device's address (required) |
| `--password`, `--username` / `--pin` | the credentials of a Secured device |
| `--radio --shade <id>` | also send real commands: that shade moves a little and back |
| `--record` | save hello, the sync and the schema to `test/fixtures/proto2/`, for the web UI and Home Assistant tests |

A plain run sends nothing on air, changes no settings and doesn't reboot the device. The command tests send ops that are refused before they reach the radio, and one `setPositions` that sets a shade to the position it already has. It does fill every socket slot for a few seconds, and replays a page load.

The tests run in whatever security mode the device is in; the suite never changes it. To test both modes, run the suite once in each.
