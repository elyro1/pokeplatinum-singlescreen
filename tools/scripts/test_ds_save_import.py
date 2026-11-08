#!/usr/bin/env python3
"""Exercise DS save detection, conversion, backups and failure recovery."""

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
    entry = next(c for c in commands if c['file'].endswith('/src/port/gameplay_config.c'))
    flags = shlex.split(entry['command'])
    flags = flags[:flags.index('-MD')]
    # Test-only defines can invalidate the game's precompiled header.
    flags.remove('-fpch-preprocess')
    flags[flags.index('-include') + 1] = str(ROOT / 'include/pch/global_pch.h')
    with tempfile.TemporaryDirectory(prefix='platinum-ds-import-') as directory:
        executable = Path(directory) / 'test'
        subprocess.run(flags + ['-UNDEBUG', '-Werror=implicit-function-declaration',
                                str(ROOT / 'tools/scripts/ds_save_import_test.c'),
                                '-o', str(executable)], cwd=build, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True)


if __name__ == '__main__':
    main()
