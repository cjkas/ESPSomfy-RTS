// The strapping pins whose level at reset can keep each chip from starting
// the firmware, as src/BootPins.h has them for the log.  A radio that drives
// one at power-up stops the board booting until EN is pressed (issue #34).
// Keep the pins, the radio pins checked and the shared RX/TX rule in step
// with that file.
// The minify step joins this file into index.js; Node loads it on its own for
// the tests (test/js).
class BootPins {
    // A warning for each of the radio config's pins that can stop the board
    // booting.
    static radioWarnings(chipModel, config) {
        const chip = BootPins.CHIPS[chipModel || ''] || {};
        const warnings = [];
        for (const [field, name] of BootPins.RADIO_PINS) {
            const pin = config[field];
            if (field === 'RXPin' && pin === config.TXPin) continue;
            const effect = chip[pin];
            if (effect) warnings.push(`${name} is on GPIO-${pin}, a strapping pin: ${effect}.  The radio can hold it there and stop the board booting after a power cut, so move it to another pin.`);
        }
        return warnings;
    }
}
// By the chip model the header sends, '' for the original ESP32.
BootPins.CHIPS = {
    '': { 0: 'low at reset starts the download mode', 12: 'high at reset powers the flash at 1.8 V' },
    s2: { 0: 'low at reset starts the download mode', 45: 'high at reset powers the flash at 1.8 V' },
    s3: { 0: 'low at reset starts the download mode', 45: 'high at reset powers the flash at 1.8 V' },
    c3: { 9: 'low at reset starts the download mode' },
    c5: { 28: 'low at reset starts the download mode' },
    c6: { 9: 'low at reset starts the download mode' },
    p4: { 35: 'low at reset starts the download mode' },
    p4es: { 35: 'low at reset starts the download mode' }
};
// The radio pins the CC1101 drives at power-up, before it is set up: GDO0
// puts out a clock, GDO2 CHIP_RDYn, and SO follows CHIP_RDYn while CSN is
// low.
BootPins.RADIO_PINS = [['TXPin', 'TX'], ['RXPin', 'RX'], ['MISOPin', 'MISO']];
if (typeof module !== 'undefined' && module.exports) module.exports = { BootPins };
