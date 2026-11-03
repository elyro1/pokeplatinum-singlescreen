#!/usr/bin/env python3
"""Test the real CPU lighting and command decoder without a GL window."""
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
        return flags[:flags.index('-MD')] + ['-ffunction-sections', '-fdata-sections']

    cpp_flags = compiler_flags('/sim/src/g3_handler.cpp')
    c_flags = compiler_flags('/gx/src/g3imm.c')
    with tempfile.TemporaryDirectory(prefix='lighting-correctness-') as directory:
        temp = Path(directory)
        objects = []
        sources = [
            (ROOT / 'subprojects/libntr/libraries/sim/src/g3_handler.cpp', cpp_flags),
            (ROOT / 'subprojects/libntr/libraries/sim/src/sim_g3_CommandProcessor.cpp', cpp_flags),
            (ROOT / 'subprojects/libntr/libraries/gx/src/g3imm.c', c_flags),
            (ROOT / 'subprojects/libntr/libraries/mi/src/mi_dma_gxcommand.c', c_flags),
            (ROOT / 'subprojects/libntr/libraries/sim/src/sim_vars.c', c_flags),
            (ROOT / 'tools/scripts/lighting_correctness_test.cpp', cpp_flags),
        ]
        for source, flags in sources:
            obj = temp / (source.name + '.o')
            subprocess.run(flags + ['-c', str(source), '-o', str(obj)], cwd=build, check=True)
            objects.append(str(obj))
        executable = temp / 'test'
        subprocess.run([
            cpp_flags[0], *objects,
            str(build / 'subprojects/libntr/libraries/fx/libfx.a'),
            str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_glad.c.o'),
            '-Wl,--gc-sections', '-lm', '-o', str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
