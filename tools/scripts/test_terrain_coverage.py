#!/usr/bin/env python3
"""Check native field coverage through the real GPU compositor (Mesa offscreen works)."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--benchmark', action='store_true', help='Also measure GPU frame time at 4K output')
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix='terrain-coverage-') as directory:
        executable = Path(directory) / 'test'
        sdl = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'], text=True))
        subprocess.run(['g++', '-std=c++11', f'-I{ROOT / "subprojects/libntr/include"}',
                        str(ROOT / 'tools/scripts/terrain_coverage_gpu_test.cpp'),
                        str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_glad.c.o'),
                        *sdl, '-ldl', '-o', str(executable)], check=True)
        env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get('SDL_VIDEODRIVER', 'offscreen'))
        command = [str(executable), str(ROOT / 'subprojects/libntr/libraries/sim/src/shaders')]
        if args.benchmark:
            command.append('--benchmark')
        subprocess.run(command,
                       cwd=directory, env=env, check=True)


if __name__ == '__main__':
    main()
