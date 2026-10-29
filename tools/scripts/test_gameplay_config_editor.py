#!/usr/bin/env python3
"""Check the gameplay editor without a ROM or writes to player configuration."""
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

    with tempfile.TemporaryDirectory(prefix='gameplay-editor-') as directory:
        temp = Path(directory)
        original = ROOT / 'src/port/gameplay_config.c'
        disabled = temp / 'defaults_only.c'
        call = '    GameplayConfig_LoadFile();'
        assert original.read_text().count(call) == 1
        disabled.write_text(original.read_text().replace(call, '    // GameplayConfig_LoadFile();'))
        cpp = flags_for('/port/gameplay_config_editor.cpp')
        for name, source, defines in [('file', original, []),
                                      ('defaults-only', disabled, ['-DGAMEPLAY_CONFIG_DEFAULTS_ONLY'])]:
            config = temp / f'{name}-config.o'
            subprocess.run(flags_for('/port/gameplay_config.c') + ['-c', str(source), '-o', str(config)],
                           cwd=build, check=True)
            test = temp / f'{name}-test.o'
            subprocess.run(cpp + ['-UNDEBUG', *defines, '-c',
                                 str(ROOT / 'tools/scripts/gameplay_config_editor_test.cpp'), '-o', str(test)],
                           cwd=build, check=True)
            executable = temp / f'{name}-test'
            subprocess.run([cpp[0], str(test), str(config),
                            str(build / 'main.p/src_port_gameplay_config_editor.cpp.o'),
                            str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_config_ini.c.o'),
                            '-o', str(executable)], check=True)
            run = temp / name
            run.mkdir()
            subprocess.run([str(executable)], cwd=run, check=True)


if __name__ == '__main__':
    main()
