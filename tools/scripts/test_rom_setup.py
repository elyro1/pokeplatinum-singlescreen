#!/usr/bin/env python3
"""Exercise ROM discovery and both real extractors with synthetic NDS files.

Run after a Linux build. No retail ROM or existing game/save directory is used.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ARCS = [4, 6, 11, 16, 21, 60, 61, 65, 66, 71, 72, 82, 94, 151, 231]


def rom(code, files):
    tree = {}
    for path, data in files.items():
        node = tree
        parts = path.split('/')
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = data
    nodes = []

    def visit(node):
        nodes.append(node)
        for child in node.values():
            if isinstance(child, dict):
                visit(child)
    visit(tree)
    ids = {id(node): 0xf000 + i for i, node in enumerate(nodes)}
    fnt = bytearray(len(nodes) * 8)
    payloads = []
    for i, node in enumerate(nodes):
        struct.pack_into('<IHH', fnt, i * 8, len(fnt), len(payloads), len(nodes) if i == 0 else 0xf000)
        for name, data in node.items():
            subdir = isinstance(data, dict)
            fnt.append(len(name) | (0x80 if subdir else 0))
            fnt.extend(name.encode())
            if subdir:
                fnt.extend(struct.pack('<H', ids[id(data)]))
            else:
                payloads.append(data)
        fnt.append(0)
    result = bytearray(0x4000)
    result[0x0c:0x10] = code.encode()
    fnt_offset = len(result)
    result.extend(fnt)
    result.extend(b'\0' * (-len(result) % 4))
    fat_offset = len(result)
    result.extend(b'\0' * (len(payloads) * 8))
    for i, data in enumerate(payloads):
        start = len(result)
        result.extend(data)
        struct.pack_into('<II', result, fat_offset + i * 8, start, len(result))
    struct.pack_into('<IIII', result, 0x40, fnt_offset, len(fnt), fat_offset, len(payloads) * 8)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='rom-setup-') as temp:
        base = Path(temp)
        exe_dir = base / 'executable'
        exe_dir.mkdir()
        harness = base / 'harness.cpp'
        harness.write_text(r'''
#include "port/assets/rom_setup.h"
#include "port/config/sim_config_prj.h"
#include <cstdio>
static SIM_Config_prj_type config;
static int saves;
extern "C" SIM_Config_prj_type *SIM_Config_prj_GetConfig() { return &config; }
extern "C" void SIM_Config_prj_SaveConfigFile(SIM_Config_prj_type *) { ++saves; }
extern "C" void GFL_ArcSysInit(const char **, unsigned) {}
extern "C" void FollowerAssets_Reload(void) {}
extern "C" void BwMonSprites_Reload(void) {}
int main() {
    int platinum = RomSetup_TryExtractPlatinum();
    RomSetup_AutoExtractBw2();
    std::printf("RESULT %d %d %d\n", platinum, config.bw2BattleVisuals, saves);
}
''')
        binary = exe_dir / 'probe'
        subprocess.run(['g++', '-std=c++17', '-DSDK_BUILD_LINUX', '-DSDK_PORT',
                        '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'subprojects/libntr/include'),
                        str(harness), str(ROOT / 'src/port/assets/rom_setup.cpp'),
                        str(ROOT / 'subprojects/libntr/libraries/sim/src/fireflower/nds-extract.cpp'),
                        str(args.build.resolve() / 'src/bwbattle/libbwbattle.a.p/assets_bw2_assets.c.o'),
                        '-lSDL2', '-o', str(binary)], check=True)

        def run(name, expected):
            work = base / name
            work.mkdir()
            result = subprocess.run([str(binary)], cwd=work, text=True, capture_output=True, check=True)
            assert f'RESULT {expected}\n' in result.stdout, result.stdout + result.stderr
            return work

        run('no-roms', '0 0 0')
        (exe_dir / 'misnamed-black2.nds').write_bytes(rom('IRBO', {'ignored': b'BW1'}))
        (exe_dir / 'tiny.nds').write_bytes(b'tiny')
        run('unsupported', '0 0 0')
        # Renamed ROMs, upper-case extensions and separate working directory.
        platinum = exe_dir / 'anything.NDS'
        platinum.write_bytes(rom('CPUE', {'data/probe.bin': b'Platinum'}))
        bw = exe_dir / 'renamed.nds'
        bw_files = {f'a/{n // 100}/{n // 10 % 10}/{n % 10}': f'arc{n}'.encode() for n in ARCS}
        bw_files['swan_sound_data.sdat'] = b'sound'
        for code in ['IREO', 'IRDO']:
            bw.write_bytes(rom(code, bw_files))
            work = run(code, '1 1 1')
            assert (work / 'data/probe.bin').read_bytes() == b'Platinum'
            for path, data in bw_files.items():
                assert (work / 'bw2' / path).read_bytes() == data, path
            (work / 'data/probe.bin').write_bytes(b'preserved')
            (work / 'bw2/a/0/0/4').write_bytes(b'preserved')
            subprocess.run([str(binary)], cwd=work, capture_output=True, check=True)
            assert (work / 'data/probe.bin').read_bytes() == b'preserved'
            assert (work / 'bw2/a/0/0/4').read_bytes() == b'preserved'
        # A corrupt Platinum candidate must not crash or write outside its tree.
        bad = rom('CPUE', {'../escape': b'bad'})
        platinum.write_bytes(bad)
        bw.unlink()
        run('unsafe-path', '0 0 0')
        platinum.write_bytes(bad[:16])
        run('truncated', '0 0 0')
        bad = rom('CPUE', {'data/probe.bin': b'data'})
        fat = struct.unpack_from('<I', bad, 0x48)[0]
        struct.pack_into('<II', bad, fat, len(bad), len(bad) + 100)
        platinum.write_bytes(bad)
        run('invalid-fat', '0 0 0')
        bad = rom('CPUE', {'dir/file': b'data'})
        fnt = struct.unpack_from('<I', bad, 0x40)[0]
        # Root entry's child points back at the root directory.
        struct.pack_into('<H', bad, fnt + 16 + 1 + len('dir'), 0xf000)
        platinum.write_bytes(bad)
        run('cyclic-fnt', '0 0 0')
        platinum.unlink()
        # Working-directory fallback when the executable directory has no match.
        work = base / 'working-fallback'
        work.mkdir()
        (work / 'local.nds').write_bytes(rom('CPUE', {'data/probe.bin': b'local'}))
        result = subprocess.run([str(binary)], cwd=work, capture_output=True, text=True, check=True)
        assert 'RESULT 1 0 0' in result.stdout
        assert (work / 'data/probe.bin').read_bytes() == b'local'
        # A recognized but incomplete BW2 ROM must not enable the setting.
        bw.write_bytes(rom('IREO', {'unrelated': b'data'}))
        run('invalid-bw2', '0 0 0')
    print('PASS: Platinum/B2/W2 discovery, extraction, setting enablement, repeat launches, fallback and invalid ROMs')


if __name__ == '__main__':
    main()
