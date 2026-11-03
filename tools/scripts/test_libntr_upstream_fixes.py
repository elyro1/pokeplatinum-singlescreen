#!/usr/bin/env python3
"""Test native wireless completion and matrix multiplication in the real libntr sources."""
import argparse
import json
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
    commands = json.loads((build / 'compile_commands.json').read_text())

    def compiler_flags(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        return flags[:flags.index('-MD')] + ['-UNDEBUG', '-ffunction-sections', '-fdata-sections']

    cpp_flags = compiler_flags('/sim/src/g3_handler.cpp')
    c_flags = compiler_flags('/wvr/src/wvr.c')
    vars_flags = compiler_flags('/sim/src/sim_vars.c')
    with tempfile.TemporaryDirectory(prefix='libntr-upstream-fixes-') as directory:
        temp = Path(directory)
        objects = []
        for source, flags in [
            (ROOT / 'subprojects/libntr/libraries/sim/src/g3_handler.cpp', cpp_flags),
            (ROOT / 'subprojects/libntr/libraries/wvr/src/wvr.c', c_flags),
            (ROOT / 'subprojects/libntr/libraries/sim/src/sim_vars.c', vars_flags),
            (ROOT / 'tools/scripts/libntr_upstream_fixes_test.cpp', cpp_flags),
        ]:
            obj = temp / (source.name + '.o')
            subprocess.run(flags + ['-c', str(source), '-o', str(obj)], cwd=build, check=True)
            objects.append(str(obj))
        executable = temp / 'test'
        subprocess.run([cpp_flags[0], *objects,
                        str(build / 'subprojects/libntr/libraries/fx/libfx.a'),
                        '-Wl,--gc-sections', '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
