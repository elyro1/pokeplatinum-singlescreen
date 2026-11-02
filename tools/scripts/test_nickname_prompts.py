#!/usr/bin/env python3
"""Exercise nickname decisions and relative branches in the assembled gift scripts."""
import argparse
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    build = parser.parse_args().build.resolve()
    names = re.findall(r'ScriptCommand\((\w+),',
                       (ROOT / 'include/data/scripts/scrcmd.h').read_text())
    opcodes = {name.removeprefix('SCRCMD_'): i for i, name in enumerate(names)}
    scripts = ['hearthome_city_northwest_house', 'mining_museum',
               'sandgem_town_pokemon_research_lab', 'veilstone_city_northeast_house']
    for script in scripts:
        data = (build / 'res/field/scripts/scr_seq.narc.p' / ('scripts_' + script)).read_bytes()
        marker = struct.pack('<H', opcodes['GETNICKNAMEMODE'])
        starts = [m.start() for m in re.finditer(re.escape(marker), data)]
        assert len(starts) == 1, (script, starts)
        start = starts[0]
        for mode, answer, expected in [(0, 0, 0), (0, 1, 1), (1, 1, 0), (2, 0, 1)]:
            pc, condition, messages, menus = start, False, 0, 0
            values = {}
            destination = struct.unpack_from('<H', data, start + 2)[0]
            end = None
            # The Ask path's unconditional jump identifies the shared continuation.
            cursor = start
            sizes = {'GETNICKNAMEMODE': 4, 'COMPAREVARTOVALUE': 6, 'GOTOIF': 7,
                     'MESSAGE': 3, 'SHOWYESNOMENU': 4}
            while end is None:
                opcode = struct.unpack_from('<H', data, cursor)[0]
                name = names[opcode].removeprefix('SCRCMD_')
                if name == 'GOTO':
                    end = cursor + 6 + struct.unpack_from('<i', data, cursor + 2)[0]
                else:
                    cursor += sizes[name]
            for _ in range(20):
                if pc == end:
                    break
                opcode = struct.unpack_from('<H', data, pc)[0]
                name = names[opcode].removeprefix('SCRCMD_')
                pc += 2
                if name == 'GETNICKNAMEMODE':
                    var, = struct.unpack_from('<H', data, pc)
                    values[var] = mode
                    pc += 2
                elif name in ('COMPAREVARTOVALUE', 'SETVARFROMVALUE'):
                    var, value = struct.unpack_from('<HH', data, pc)
                    pc += 4
                    if name == 'COMPAREVARTOVALUE':
                        condition = values[var] == value
                    else:
                        values[var] = value
                elif name == 'GOTOIF':
                    test, offset = struct.unpack_from('<Bi', data, pc)
                    assert test == 1  # Equality, as used by GoToIfEq.
                    pc += 5
                    if condition:
                        pc += offset
                elif name == 'GOTO':
                    offset, = struct.unpack_from('<i', data, pc)
                    pc += 4 + offset
                elif name == 'MESSAGE':
                    messages += 1
                    pc += 1
                elif name == 'SHOWYESNOMENU':
                    var, = struct.unpack_from('<H', data, pc)
                    values[var] = answer
                    menus += 1
                    pc += 2
                else:
                    raise AssertionError((script, name, pc))
            assert pc == end, (script, mode, 'did not reach continuation')
            assert values[destination] == expected, (script, mode, values)
            assert messages == menus == (1 if mode == 0 else 0), (script, mode)
    print('Nickname scripts: 16 assembled paths passed (Ask Yes/No, Always, Never).')


if __name__ == '__main__':
    main()
