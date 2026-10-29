#!/usr/bin/env python3
"""Exercise the real SDL/ImGui menu offscreen without a ROM."""
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
    commands = json.loads((build / 'compile_commands.json').read_text())

    def flags_for(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        return flags[:flags.index('-MD')]

    with tempfile.TemporaryDirectory(prefix='menu-gui-') as directory:
        temp = Path(directory)
        config = temp / 'config.o'
        subprocess.run(flags_for('/config/sim_config.c') + [
            '-ffunction-sections', '-fdata-sections', '-c',
            str(ROOT / 'subprojects/libntr/libraries/sim/src/config/sim_config.c'),
            '-o', str(config)], cwd=build, check=True)
        cpp = flags_for('/sim_gui.cpp')
        test = temp / 'test.o'
        subprocess.run(cpp + [f'-I{ROOT / "include"}', '-UNDEBUG', '-c', str(ROOT / 'tools/scripts/menu_gui_test.cpp'),
                             '-o', str(test)], cwd=build, check=True)
        sdl = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2'], text=True))
        executable = temp / 'menu-test'
        subprocess.run([cpp[0], str(test), str(config),
                        str(build / 'main.p/src_port_gameplay_config.c.o'),
                        str(build / 'main.p/src_port_gameplay_config_editor.cpp.o'),
                        str(build / 'main.p/src_port_gui_gameplay.cpp.o'),
                        str(build / 'subprojects/libntr/libraries/sim/libsim.a'),
                        '-Wl,--gc-sections', *sdl, '-ldl', '-o', str(executable)], check=True)
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
        subprocess.run([str(executable)], cwd=temp, env=env, check=True, timeout=60)


if __name__ == '__main__':
    main()
