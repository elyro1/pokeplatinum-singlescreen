#!/usr/bin/env python3
"""Test production exp-heap alignment under ASan/UBSan, with heap checks on and off."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--check-unpatched', action='store_true',
                        help='also verify both probes detect the original alignment defects')
    args = parser.parse_args()
    build = args.build.resolve()
    sdk = ROOT / 'subprojects/libntrsystem'
    commands = json.loads((build / 'compile_commands.json').read_text())
    sanitize = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer']

    def flags_for(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        flags = flags[:flags.index('-MD')]
        if '-include' in flags:
            flags[flags.index('-include') + 1] = str(
                ROOT / 'subprojects/libntr/include/pch/nitro_pch.h')
        return flags + ['-O1', '-ffunction-sections', '-fdata-sections', *sanitize]

    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='libntrsystem-heap-alignment-') as directory:
        temp = Path(directory)
        variants = [('production', sdk / 'libraries/fnd/src/expheap.c', [])]
        variants.append(('heap-checks', variants[0][1], ['-UNNS_FINALROM']))
        if args.check_unpatched:
            original = temp / 'expheap.c'
            original.write_text(variants[0][1].read_text())
            patch = ROOT / 'subprojects/packagefiles/libntrsystem_patch/native-heap-alignment.patch'
            subprocess.run(['patch', '--batch', '--reverse', str(original), str(patch)],
                           check=True)
            variants.append(('unpatched', original,
                             ['-I' + str(sdk / 'libraries/fnd/src')]))

        for label, expheap, extra in variants:
            objects = []
            sources = [
                (expheap, '/fnd/src/expheap.c'),
                (sdk / 'libraries/fnd/src/heapcommon.c', '/fnd/src/heapcommon.c'),
                (sdk / 'libraries/fnd/src/list.c', '/fnd/src/list.c'),
                (sdk / 'libraries/fnd/src/allocator.c', '/fnd/src/allocator.c'),
                (ROOT / 'subprojects/libntr/libraries/mi/src/mi_memory.c', '/mi_memory.c'),
                (ROOT / 'tools/scripts/libntrsystem_heap_alignment_test.c', '/fnd/src/expheap.c'),
            ]
            for source, suffix in sources:
                obj = temp / (label + '-' + source.name + '.o')
                subprocess.run(flags_for(suffix) + extra + ['-c', str(source), '-o', str(obj)],
                               cwd=build, check=True)
                objects.append(str(obj))
            executable = temp / label
            subprocess.run([flags_for('/fnd/src/expheap.c')[0], *objects, *sanitize,
                            '-Wl,--gc-sections', '-o', str(executable)], check=True)
            for mode in ['create', 'split'] + ([] if label == 'unpatched' else ['sweep']):
                result = subprocess.run([str(executable), mode], env=env, text=True,
                                        capture_output=True, timeout=30)
                if label == 'unpatched':
                    diagnostic = result.stdout + result.stderr
                    expected = ('NNSiFndHeapHead' if mode == 'create'
                                else 'NNSiFndExpHeapMBlockHead')
                    if not (result.returncode and 'misaligned address' in diagnostic
                            and expected in diagnostic):
                        raise RuntimeError(f'{label}/{mode}: expected alignment failure\n{diagnostic}')
                    print(f'PASS: {mode} probe detects original {expected} misalignment', flush=True)
                else:
                    print(f'{label}/{mode}: {result.stdout}', end='', flush=True)
                    if result.stderr:
                        print(result.stderr, end='', flush=True)
                    result.check_returncode()


if __name__ == '__main__':
    main()
