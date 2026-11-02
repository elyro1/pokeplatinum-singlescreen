#!/usr/bin/env python3
"""Test the release patcher against original and development archive members.

No ROM is needed: original members come from the source tree. All writes are
isolated in a temporary directory; the development game's files are untouched.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

from make_map_patches import LAND, MATRIX, ROOT, TEXT, SCRIPTS, SUBSCRIPTS, MENU, menu_baseline, resource_baselines, members as narc_members
from verify_hearthome_scenery import members


def repack(template, files):
    data = template.read_bytes()
    header_size = struct.unpack_from('<H', data, 12)[0]
    blocks, offset = [], header_size
    while offset < len(data):
        size = struct.unpack_from('<I', data, offset + 4)[0]
        blocks.append(bytearray(data[offset:offset + size]))
        offset += size
    fat = next(b for b in blocks if b[:4] == b'BTAF')
    fat[12:] = b'\0' * (8 * len(files))
    struct.pack_into('<I', fat, 4, len(fat))
    struct.pack_into('<H', fat, 8, len(files))
    image = next(b for b in blocks if b[:4] == b'GMIF')
    image[8:] = b''
    for i, member in enumerate(files):
        start = len(image) - 8
        image.extend(member)
        struct.pack_into('<II', fat, 12 + i * 8, start, len(image) - 8)
        image.extend(b'\xff' * (-len(image) % 4))
    struct.pack_into('<I', image, 4, len(image))
    result = bytearray(data[:header_size]) + b''.join(blocks)
    struct.pack_into('<I', result, 8, len(result))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--cxx', default='g++', help='C++ compiler executable')
    parser.add_argument('--runner', default='', help='Executable runner, e.g. /usr/bin/wine for a Windows cross-build')
    args = parser.parse_args()
    runner = shlex.split(args.runner)
    build = args.build.resolve()
    land = build / 'res/field/maps/data/land_data.narc'
    matrix = build / 'res/field/matrices/map_matrix.narc'
    expected = {LAND: members(land), MATRIX: members(matrix)}
    originals = {LAND: [(ROOT / 'res/field/maps/data' / name).read_bytes()
                       for name in (ROOT / 'res/field/maps/data/map_data.order').read_text().splitlines()],
                 MATRIX: list(expected[MATRIX])}
    templates = {LAND: land, MATRIX: matrix,
                 TEXT: build / 'res/text/pl_msg.narc',
                 SCRIPTS: build / 'res/field/scripts/scr_seq.narc',
                 SUBSCRIPTS: build / 'res/battle/scripts/sub_seq.narc'}
    from iris_patch_baselines import resources as iris_resources, SOUND
    iris, original_sound = iris_resources(build, narc_members)
    for path, (archive, _) in iris.items():
        templates[path] = archive
    for path in templates:
        if path in (LAND, MATRIX):
            continue

        expected[path] = members(templates[path])
        originals[path] = list(expected[path])
    for path, _, index, before in resource_baselines(build):
        originals[path][index] = before
    for path, (_, retail) in iris.items():
        originals[path] = originals[path][:len(retail)]
    menu, _, original_menu = menu_baseline(build)
    templates[MENU] = menu
    expected[MENU] = members(menu)
    originals[MENU] = original_menu
    # Check the baseline source snapshots against the independently recorded
    # retail archive hashes, rather than only comparing our own generated data.
    for path, digest in (
        (TEXT, '3614557aa04eb1ad7e5f260578de925f73da584d'),
        (SCRIPTS, '9f4d3587b7032f4f673ccc1b8971685b2199396b'),
        (SUBSCRIPTS, 'b646973b4c92a1d25479c5b08d28ff283274db33'),
        (MENU, '1d70dd8856d2f85beb2d4e461cfaf7179153640a'),
    ):
        assert hashlib.sha1(repack(templates[path], originals[path])).hexdigest() == digest, path
    print('PASS: pristine text/script/subscript/menu archives match recorded retail hashes')
    with tempfile.TemporaryDirectory(prefix='map-patches-') as temp:
        work = Path(temp)
        original_matrix = work / 'matrix.bin'
        subprocess.run(['python3', str(ROOT / 'tools/jsoncnv/map_matrix.py'),
                        str(ROOT / 'res/field/matrices/map_matrix_000.json'), str(original_matrix)],
                       env=dict(os.environ, PYTHONPATH=str(build)), check=True)
        originals[MATRIX][0] = original_matrix.read_bytes()
        subprocess.run(['python3', str(ROOT / 'tools/scripts/make_map_patches.py'),
                        str(build), str(land), str(matrix), str(work / 'map_patches.inc'),
                        '--text', str(templates[TEXT]), '--scripts', str(templates[SCRIPTS]),
                        '--baseline-text', str(build / 'res/port_patch_baselines/original_text.narc'),
                        '--baseline-scripts', str(build / 'res/port_patch_baselines/original_scripts.narc'),
                        '--subscripts', str(templates[SUBSCRIPTS]),
                        '--baseline-subscripts', str(build / 'res/port_patch_baselines/original_battle_subscripts.narc')], check=True)
        assert (work / 'map_patches.inc').read_bytes() == (build / 'src/map_patches.inc').read_bytes(), 'Embedded build patches are stale'
        target = subprocess.check_output([args.cxx, '-dumpmachine'], text=True).strip()
        windows = os.name == 'nt' or sys.platform.startswith(('msys', 'cygwin')) or 'mingw' in target
        binary = work / ('patcher.exe' if windows else 'patcher')
        static = ['-static'] if windows else []
        subprocess.run([args.cxx, *static, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-DMAP_PATCH_TEST',
                        '-I' + str(ROOT / 'include'), '-I' + str(work),
                        str(ROOT / 'src/port/map_patches.cpp'), '-o', str(binary)], check=True)
        def run(directory, success=True):
            result = subprocess.run([*runner, str(binary)], cwd=directory, capture_output=True, text=True,
                                    env=dict(os.environ, PATH=str(work / 'no-python')))
            assert (result.returncode == 0) == success, result.stderr
            return result.stderr
        def populate(directory, data):
            directory.mkdir()
            for path, files in data.items():
                target = directory / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(repack(templates[path], files))
            target = directory / SOUND
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original_sound if data is originals else (build / 'res/sound/pl_sound_data.sdat').read_bytes())
        clean = work / 'clean'
        populate(clean, originals)
        run(clean)
        assert (clean / SOUND).read_bytes() == (build / 'res/sound/pl_sound_data.sdat').read_bytes()
        for path in expected:
            assert members(clean / path) == expected[path], path
            assert members(clean / '.map-patch-originals' / path) == originals[path]
        print('PASS: all map/text/script/menu members match the development build with an empty PATH')
        before = {path: (clean / path).stat().st_mtime_ns for path in expected}
        run(clean)
        assert before == {path: (clean / path).stat().st_mtime_ns for path in expected}
        print('PASS: repeat startup performs no writes')
        # Simulate an older release's output. Upgrade must use pristine backups,
        # while keeping a player's change to an unrelated member.
        changed = members(clean / LAND)
        changed[0] = b'older release output'
        changed[1] = b'unrelated mod'
        (clean / LAND).write_bytes(repack(land, changed))
        run(clean)
        changed = members(clean / LAND)
        assert changed[0] == expected[LAND][0] and changed[1] == b'unrelated mod'
        print('PASS: upgrades use original backups and preserve unrelated members')
        changed = members(clean / MENU)
        changed[11] = b'older icon animation'
        changed[12] = b'older icon palette'
        (clean / MENU).write_bytes(repack(menu, changed))
        run(clean)
        assert members(clean / MENU) == expected[MENU]
        print('PASS: appended menu resources upgrade from retail backups')
        (clean / SOUND).write_bytes(b'older sound archive')
        run(clean)
        assert (clean / SOUND).read_bytes() == (build / 'res/sound/pl_sound_data.sdat').read_bytes()
        print('PASS: sound archive upgrades from its verified retail backup')
        dev = work / 'dev'
        populate(dev, expected)
        run(dev)
        assert not (dev / '.map-patch-originals').exists()
        print('PASS: development assets are recognized without backups or rewriting')
        corrupt_indices = {MATRIX: 0, MENU: 3}
        for path, _, index, before_member in resource_baselines(build):
            if expected[path][index] != before_member:
                corrupt_indices.setdefault(path, index)
        for path in iris:
            if len(expected[path]) > len(originals[path]):
                corrupt_indices[path] = 0
        for number, (path, index) in enumerate(corrupt_indices.items()):
            corrupt = work / ('corrupt-%d' % number)
            bad = {p: list(files) for p, files in originals.items()}
            bad[path][index] = b'wrong ROM member'
            populate(corrupt, bad)
            before = {p: (corrupt / p).read_bytes() for p in expected}
            assert 'Original map data unavailable' in run(corrupt, False)
            assert before == {p: (corrupt / p).read_bytes() for p in expected}
        corrupt_sound = work / 'corrupt-sound'
        populate(corrupt_sound, originals)
        (corrupt_sound / SOUND).write_bytes(b'unknown sound archive')
        before = {p: (corrupt_sound / p).read_bytes() for p in expected}
        assert 'Unexpected original map member' in run(corrupt_sound, False)
        assert before == {p: (corrupt_sound / p).read_bytes() for p in expected}
        print('PASS: unknown assets fail without replacing any game archives')
        missing = work / 'missing'
        missing.mkdir()
        assert 'Cannot read' in run(missing, False)
        print('PASS: missing filesystem fails clearly in the standalone patcher')


if __name__ == '__main__':
    main()
