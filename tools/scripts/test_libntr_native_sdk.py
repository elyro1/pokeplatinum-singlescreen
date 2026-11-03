#!/usr/bin/env python3
"""Run native SDK regressions against production sources with ASan and UBSan."""
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
    args = parser.parse_args()
    build = args.build.resolve()
    sdk = ROOT / 'subprojects/libntr'
    commands = json.loads((build / 'compile_commands.json').read_text())
    sanitizers = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    with tempfile.TemporaryDirectory(prefix='libntr-native-sdk-') as directory:
        temp = Path(directory)
        objects = []
        for suffix in ['/mi_memory.c', '/os_arena.c', '/g2.c', '/sim_vars.c', '/g3x.c', '/snd_channel.c', 'test']:
            entry = next(x for x in commands if x['file'].endswith('/g2.c' if suffix == 'test' else suffix))
            flags = shlex.split(entry['command'])
            flags = flags[:flags.index('-MD')]
            # GCC PCH created without sanitizers cannot safely be reused here.
            flags[flags.index('-include') + 1] = str(sdk / 'include/pch/nitro_pch.h')
            source = ROOT / 'tools/scripts/libntr_native_sdk_test.c' if suffix == 'test' else (build / entry['file']).resolve()
            obj = temp / (source.name + '.o')
            subprocess.run(flags + ['-O1', '-ffunction-sections', '-fdata-sections', *sanitizers,
                                    '-c', str(source), '-o', str(obj)], cwd=build, check=True)
            objects.append(str(obj))
        executable = temp / 'test'
        subprocess.run([flags[0], *objects, *sanitizers, '-Wl,--gc-sections', '-o', str(executable)], check=True)
        subprocess.run([str(executable)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True, timeout=30)


if __name__ == '__main__':
    main()
