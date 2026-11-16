#!/usr/bin/env python3
"""Check config warnings, --print-config sources, and that the PLATINUM_* list matches the code and docs."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LIBNTR = ROOT / 'subprojects/libntr/libraries/sim/src'


def check_environment_list():
    used = set()
    for directory in (ROOT / 'src', LIBNTR, LIBNTR.parent.parent / 'rtc/src'):
        for path in directory.rglob('*'):
            if path.suffix in ('.c', '.cpp') and 'imgui' not in path.parts:
                used |= set(re.findall(r'getenv\("(PLATINUM_[A-Z0-9_]+)"\)', path.read_text(errors='ignore')))
    report = (ROOT / 'src/port/config/config_report.c').read_text()
    table = report[report.index('sEnvironmentVariables[] = {'):]
    listed = set(re.findall(r'"(PLATINUM_[A-Z0-9_]+)"', table[:table.index('};')]))
    documented = set(re.findall(r'^\| `(PLATINUM_[A-Z0-9_]+)`', (ROOT / 'docs/environment_variables.md').read_text(),
                                re.MULTILINE))
    assert used == listed, f'config_report.c list differs from getenv calls: {sorted(used ^ listed)}'
    assert used == documented, f'docs/environment_variables.md differs from getenv calls: {sorted(used ^ documented)}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    build = parser.parse_args().build.resolve()
    check_environment_list()
    commands = json.loads((build / 'compile_commands.json').read_text())

    def flags_for(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        return flags[:flags.index('-MD')]

    with tempfile.TemporaryDirectory(prefix='config-check-') as directory:
        temp = Path(directory)
        objects = []
        for source, suffix in [(LIBNTR / 'config/sim_config.c', '/config/sim_config.c'),
                               (LIBNTR / 'config/config_check.c', '/config/config_check.c'),
                               (LIBNTR / 'config/ini.c', '/config/ini.c'),
                               (ROOT / 'src/port/config/sim_config_prj.c', '/port/config/sim_config_prj.c'),
                               (ROOT / 'src/port/gameplay/gameplay_config.c', '/port/gameplay/gameplay_config.c'),
                               (ROOT / 'tools/scripts/config_check_test.c', '/port/config/sim_config_prj.c')]:
            output = temp / (source.stem + '.o')
            subprocess.run(flags_for(suffix) + ['-UNDEBUG', '-ffunction-sections', '-fdata-sections', '-c',
                                                str(source), '-o', str(output)], cwd=build, check=True)
            objects.append(str(output))
        sdl = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2'], text=True))
        executable = temp / 'config-check-test'
        subprocess.run(['gcc', *objects, '-Wl,--gc-sections', *sdl, '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], cwd=temp, capture_output=True, text=True, timeout=60)
        print(result.stdout, end='')
        assert result.returncode == 0, result.stderr
        warnings = result.stderr
        for expected in [
            './sim_config_prj.ini:7: BW2BattleVisuals is ignored: it is in [Cheats] but belongs in [General]',
            './sim_config_prj.ini:2: [General] PokemonFollower=yes is ignored: expected true or false',
            '[General] NicknameMode=Sometimes is ignored: expected Ask, Always or Never',
            'pokemonfollower is ignored: names are case-sensitive, did you mean [General] PokemonFollower?',
            './sim_config_prj.ini:6: [General] GridBag is also set on line 5; this later value wins',
            './sim_config.ini:9: WindowHeight is ignored: it is in [PAD] but belongs in [General]',
            './gameplay_config.ini:3: [Gameplay] catchRateMultiplier=999 is ignored: expected a number from 0 to 255',
            './gameplay_config.ini:3: not a [section] or key=value line, so the whole file is ignored',
        ]:
            assert expected in warnings, f'missing warning: {expected}\n{warnings}'


if __name__ == '__main__':
    main()
