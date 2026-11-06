"""Compile selected production methods without the unrelated RTOS/network code."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
names = [
    'static bool checkBoardRadio(',
    'void transceiver_config_t::applyBoardPins(',
    'void transceiver_config_t::apply(',
    'void Transceiver::setConfig(',
    'bool Transceiver::usesPin(',
    'void Transceiver::enableReceive(',
    'bool Transceiver::startReceive(',
    'void Transceiver::disableReceive(',
    'uint32_t Transceiver::submitTx(',
]
methods = []
for name in names:
    assert source.count(name) == 1, f'Production setup method changed: {name}'
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        assert end < len(source), f'Unterminated method: {name}'
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end])
Path(sys.argv[2]).write_text('\n\n'.join(methods) + '\n')
