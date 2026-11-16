#!/usr/bin/env python3
"""Test real simulator input and config using an SDL virtual controller (no ROM needed)."""
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

    def flags_for(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        return flags[:flags.index('-MD')]

    with tempfile.TemporaryDirectory(prefix='gamepad-input-') as directory:
        temp = Path(directory)
        config = temp / 'config.o'
        subprocess.run(flags_for('/config/sim_config.c') + ['-ffunction-sections', '-fdata-sections',
                       '-c', str(ROOT / 'subprojects/libntr/libraries/sim/src/config/sim_config.c'),
                       '-o', str(config)], cwd=build, check=True)
        test = temp / 'test.o'
        cpp = flags_for('/sim_main.cpp')
        subprocess.run(cpp + ['-UNDEBUG', '-c', str(ROOT / 'tools/scripts/gamepad_input_test.cpp'),
                       '-o', str(test)], cwd=build, check=True)
        sdl = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2'], text=True))
        executable = temp / 'gamepad-test'
        subprocess.run([cpp[0], str(test), str(config),
                       str(build / 'subprojects/libntr/libraries/sim/libsim_vendor.a.p/src_config_ini.c.o'),
                       str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_config_config_check.c.o'),
                       '-Wl,--gc-sections', *sdl, '-o', str(executable)], check=True)
        subprocess.run([str(executable)], cwd=temp, check=True)


if __name__ == '__main__':
    main()
