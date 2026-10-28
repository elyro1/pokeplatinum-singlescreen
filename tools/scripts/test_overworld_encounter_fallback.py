#!/usr/bin/env python3
"""Check normal encounter fallback using the real field and visible encounter policy."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
build = parser.parse_args().build.resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
entry = next(x for x in commands if x['file'].endswith('/overlay005/field_control.c'))
flags = shlex.split(entry['command'])
flags = flags[:flags.index('-MD')]
with tempfile.TemporaryDirectory(prefix='encounter-fallback-') as directory:
    executable = Path(directory) / 'test'
    subprocess.run(flags + ['-UNDEBUG', '-ffunction-sections', '-fdata-sections',
        str(ROOT / 'tools/scripts/overworld_encounter_fallback_test.c'),
        str(ROOT / 'src/overworld_encounters.c'), '-Wl,--gc-sections', '-o', str(executable)],
        cwd=build, check=True)
    subprocess.run([str(executable)], check=True)
