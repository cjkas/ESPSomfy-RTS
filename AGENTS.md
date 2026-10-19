## Testing instructions
- Fix any test or type errors until the whole suite is green.
- Add or update tests for the code you change, even if nobody asked.
- to test run `pio test -e native`

## Building
- Firmware builds need pioarduino's Core (`pip install pioarduino`, or the pioarduino IDE extension). Stock PlatformIO Core refuses the pinned platform, see [ADR 0003](docs/adr/0003-pioarduino-idf55.md).
- The platform installs its Python packages into `~/.platformio/penv` and fails on Windows while `pio.exe` from that env is running. Run builds as `python -m platformio` from that env's Python.
