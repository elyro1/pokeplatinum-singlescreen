#!/usr/bin/env python3
"""Embed member-level copy/literal deltas, never complete game archives."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LAND = 'fielddata/land_data/land_data.narc'
MATRIX = 'fielddata/mapmatrix/map_matrix.narc'
TEXT = 'msgdata/pl_msg.narc'
SCRIPTS = 'fielddata/script/scr_seq.narc'
MENU = 'graphic/menu_gra.narc'
SUBSCRIPTS = 'battle/skill/sub_seq.narc'


def menu_baseline(build):
    """The retail menu has the unchanged first eleven members."""
    archive = build / 'res/graphics/start_menu/start_menu.narc'
    files = members(archive.read_bytes())
    order = (ROOT / 'res/graphics/start_menu/start_menu.order').read_text().splitlines()
    if len(files) != len(order) or len(files) != 13:
        raise ValueError('Start menu member order changed')
    original = files[:11]
    return archive, order, original


def resource_baselines(build):
    """Return (runtime path, built archive, destination index, pristine member)."""
    resources = []
    for kind, path, archive, order in (
        ('text', TEXT, 'res/text/pl_msg.narc',
         [line.removeprefix('TEXT_BANK_').lower() for line in
          (ROOT / 'generated/text_banks.txt').read_text().splitlines()]),
        ('scripts', SCRIPTS, 'res/field/scripts/scr_seq.narc',
         (ROOT / 'res/field/scripts/scripts.order').read_text().splitlines()),
        ('battle_subscripts', SUBSCRIPTS, 'res/battle/scripts/sub_seq.narc',
         (ROOT / 'res/battle/scripts/subscripts/sub_seq.order').read_text().splitlines()),
    ):
        names = (ROOT / 'res/port_patch_baselines' / kind / 'original.order').read_text().splitlines()
        baseline = build / 'res/port_patch_baselines' / ('original_%s.narc' % kind)
        originals = members(baseline.read_bytes())
        if len(names) != len(originals):
            raise ValueError('Baseline member count changed: ' + kind)
        resources.extend((path, build / archive, order.index(name), before)
                         for name, before in zip(names, originals))
    from iris_patch_baselines import resources as iris_resources
    iris, _ = iris_resources(build.resolve(), members)
    covered = {(path, index) for path, _, index, _ in resources}
    for path, (archive, originals) in iris.items():
        resources.extend((path, archive, index, before) for index, before in enumerate(originals)
                         if (path, index) not in covered)
    return resources


def members(data):
    if data[:4] != b'NARC' or struct.unpack_from('<I', data, 8)[0] != len(data):
        raise ValueError('Invalid NARC')
    offset = struct.unpack_from('<H', data, 12)[0]
    ranges = payload = None
    while offset < len(data):
        size = struct.unpack_from('<I', data, offset + 4)[0]
        if size < 8 or offset + size > len(data):
            raise ValueError('Invalid NARC block')
        tag = data[offset:offset + 4]
        if tag == b'BTAF':
            ranges = [struct.unpack_from('<II', data, offset + 12 + i * 8)
                      for i in range(struct.unpack_from('<H', data, offset + 8)[0])]
        elif tag == b'GMIF':
            payload = data[offset + 8:offset + size]
        offset += size
    if ranges is None or payload is None or any(a > b or b > len(payload) for a, b in ranges):
        raise ValueError('Invalid NARC members')
    return [payload[a:b] for a, b in ranges]


def delta(before, after):
    # A linear-time indexed matcher handles repetitive GX streams without the
    # quadratic behavior of byte-wise SequenceMatcher with autojunk disabled.
    blocks = {}
    for i in range(0, len(before) - 15, 32 if len(before) > 1000000 else 4):
        blocks.setdefault(before[i:i + 16], []).append(i)
    ops, literal = [], bytearray()
    i = 0
    while i < len(after):
        candidates = blocks.get(after[i:i + 16], [])
        best, length = 0, 0
        for start in candidates[:64]:
            n = 16
            while start + n < len(before) and i + n < len(after) and before[start + n] == after[i + n]:
                n += 1
            if n > length:
                best, length = start, n
        if length:
            if literal:
                ops.append(b'\x01' + struct.pack('<I', len(literal)) + literal)
                literal.clear()
            ops.append(b'\x00' + struct.pack('<II', best, length))
            i += length
        else:
            literal.append(after[i])
            i += 1
    if literal:
        ops.append(b'\x01' + struct.pack('<I', len(literal)) + literal)
    return b''.join(ops)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('land', type=Path)
    parser.add_argument('matrix', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--text', type=Path)
    parser.add_argument('--scripts', type=Path)
    parser.add_argument('--baseline-text', type=Path)
    parser.add_argument('--baseline-scripts', type=Path)
    parser.add_argument('--subscripts', type=Path)
    parser.add_argument('--baseline-subscripts', type=Path)
    args = parser.parse_args()
    order = (ROOT / 'res/field/maps/data/map_data.order').read_text().splitlines()
    originals = [(ROOT / 'res/field/maps/data' / name).read_bytes() for name in order]
    modified = members(args.land.read_bytes())
    if len(originals) != len(modified):
        raise ValueError('Land member count changed')
    patches = [(LAND, i, i, a, b) for i, (a, b) in enumerate(zip(originals, modified)) if a != b]
    with tempfile.TemporaryDirectory() as temp:
        original_matrix = Path(temp) / 'matrix.bin'
        env = dict(os.environ, PYTHONPATH=str(args.build.resolve()))
        subprocess.run(['python3', str(ROOT / 'tools/jsoncnv/map_matrix.py'),
                        str(ROOT / 'res/field/matrices/map_matrix_000.json'), str(original_matrix)],
                       env=env, check=True)
        matrix_order = (ROOT / 'res/field/matrices/map_matrices.order').read_text().splitlines()
        index = matrix_order.index('map_matrix_000')
        before, after = original_matrix.read_bytes(), members(args.matrix.read_bytes())[index]
        if before != after:
            patches.append((MATRIX, index, index, before, after))
    # Explicit archive inputs give Meson dependency tracking; the shared helper
    # also supplies pristine members to the release-patcher integration test.
    extra = [args.text, args.scripts, args.baseline_text, args.baseline_scripts,
             args.subscripts, args.baseline_subscripts]
    if not all(extra):
        raise ValueError('Text/script archives and their baselines are required')
    expected_paths = [args.build / 'res/text/pl_msg.narc',
                      args.build / 'res/field/scripts/scr_seq.narc',
                      args.build / 'res/port_patch_baselines/original_text.narc',
                      args.build / 'res/port_patch_baselines/original_scripts.narc',
                      args.build / 'res/battle/scripts/sub_seq.narc',
                      args.build / 'res/port_patch_baselines/original_battle_subscripts.narc']
    if any(a.resolve() != b.resolve() for a, b in zip(extra, expected_paths)):
        raise ValueError('Resource archives must belong to the selected build')
    archives = {}
    for path, archive, member, before in resource_baselines(args.build):
        if path not in archives:
            archives[path] = members(archive.read_bytes())
        after = archives[path][member]
        if before != after:
            patches.append((path, member, member, before, after))
    # Added menu members reuse retail animation/palette bytes as their bases.
    # No complete copied game resource needs to be embedded in the executable.
    menu_archive, menu_order, original_menu = menu_baseline(args.build)
    menu_files = members(menu_archive.read_bytes())
    for target, source in (('poketch_icon_anim.NANR', 'icons_anim.NANR'),
                           ('poketch_icon.NCLR', 'menu.NCLR')):
        index, source_index = menu_order.index(target), menu_order.index(source)
        patches.append((MENU, index, source_index, original_menu[source_index], menu_files[index]))
    from iris_patch_baselines import resources as iris_resources, SOUND
    iris, original_sound = iris_resources(args.build.resolve(), members)
    for path, (archive, originals) in iris.items():
        current = members(archive.read_bytes())
        for index in range(len(originals), len(current)):
            # Appended members use an existing retail member as the verified base.
            source = 0
            patches.append((path, index, source, originals[source], current[index]))
    patches.append((SOUND, 0xffffffff, 0xffffffff, original_sound,
                    (args.build / 'res/sound/pl_sound_data.sdat').read_bytes()))
    output = ['// Generated by make_map_patches.py; contains copy/literal deltas.']
    for i, (_, _, _, before, after) in enumerate(patches):
        blob = delta(before, after)
        output.append('static const unsigned char delta_%d[] = {%s};' % (i, ','.join(map(str, blob))))
    output.append('static const MapPatch mapPatches[] = {')
    for i, (path, member, source_member, before, after) in enumerate(patches):
        output.append('{"%s",%d,%d,"%s","%s",%d,delta_%d,sizeof(delta_%d)},' %
                      (path, member, source_member, hashlib.sha256(before).hexdigest(), hashlib.sha256(after).hexdigest(), len(after), i, i))
    output.append('};\n')
    args.output.write_text('\n'.join(output))
    print('Embedded %d resource member patches' % len(patches))


if __name__ == '__main__':
    main()
