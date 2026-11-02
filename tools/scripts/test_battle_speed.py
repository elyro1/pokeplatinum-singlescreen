#!/usr/bin/env python3
"""Exercise speed policy, shared HP/EXP progression and real text rendering without a ROM."""
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
    build = parser.parse_args().build.resolve()
    commands = json.loads((build / 'compile_commands.json').read_text())

    def flags_for(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        return flags[:flags.index('-MD')]

    with tempfile.TemporaryDirectory(prefix='battle-speed-') as directory:
        temp = Path(directory)
        for name, suffix, extra in [
            ('config', '/port/sim_config_prj.c', [ROOT / 'subprojects/libntr/libraries/sim/src/config/ini.c']),
            ('bars', '/battle/healthbox.c', []),
            ('text', '/src/text.c', []),
            ('waits', '/battle/battle_script.c', []),
            ('nickname', '/battle/battle_script.c', []),
            ('graphics_cleanup', '/battle/battle_main.c', []),
            ('exit', '/battle/battle_display.c', []),
            ('animation', '/src/sys_task_manager.c', []),
            ('capture', '/battle/battle_display.c', []),
            ('ball', '/battle_anim/ov12_02235E94.c', []),
            ('breakout', '/battle/battle_display.c', []),
        ]:
            executable = temp / name
            subprocess.run(flags_for(suffix) + [
                '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
                str(ROOT / f'tools/scripts/battle_speed_{name}_test.c'),
                *map(str, extra), '-Wl,--gc-sections', '-o', str(executable)], cwd=build, check=True)
            subprocess.run([str(executable)], cwd=temp, check=True, timeout=30)


if __name__ == '__main__':
    main()
