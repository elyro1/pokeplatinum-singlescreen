#!/usr/bin/env python3
"""Test real audio teardown and native fast/async GX completion without a ROM."""
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
    sdk = ROOT / 'subprojects/libntr'
    commands = json.loads((build / 'compile_commands.json').read_text())
    sanitize = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']

    def flags_for(suffix):
        entry = next(x for x in commands if x['file'].endswith(suffix))
        flags = shlex.split(entry['command'])
        flags = flags[:flags.index('-MD')]
        if '-include' in flags:
            flags[flags.index('-include') + 1] = str(sdk / 'include/pch/nitro_pch.h')
        return flags + ['-ffunction-sections', '-fdata-sections']

    with tempfile.TemporaryDirectory(prefix='libntr-lifecycle-') as directory:
        temp = Path(directory)

        def compile_source(source, suffix, extra=()):
            obj = temp / (source.name + '.o')
            subprocess.run(flags_for(suffix) + list(extra) + ['-c', str(source), '-o', str(obj)], cwd=build, check=True)
            return str(obj)

        # Run the actual callback with SDL's dummy device and real SoundTouch.
        audio_objects = [
            compile_source(sdk / 'libraries/sim/src/sim_audio.cpp', '/sim_audio.cpp'),
            compile_source(ROOT / 'tools/scripts/audio_shutdown_test.cpp', '/sim_audio.cpp'),
        ]
        audio = temp / 'audio-test'
        audio_libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2', 'soundtouch'], text=True))
        subprocess.run([flags_for('/sim_audio.cpp')[0], *audio_objects,
                        str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_blip-buf_blip_buf.c.o'),
                        '-Wl,--gc-sections', '-Wl,--wrap=SDL_OpenAudio', '-Wl,--wrap=SDL_CloseAudio',
                        *audio_libs, '-o', str(audio)], check=True)
        for mode in [[], ['exit'], ['fail-open'], ['fail-open', 'exit']]:
            subprocess.run([str(audio), *mode], env=dict(os.environ, SDL_AUDIODRIVER='dummy'), check=True, timeout=10)

        gx_objects = []
        for source, suffix in [
            (sdk / 'libraries/mi/src/mi_dma_gxcommand.c', '/mi_dma_gxcommand.c'),
            (sdk / 'libraries/mi/src/mi_memory.c', '/mi_memory.c'),
            (sdk / 'libraries/sim/src/sim_vars.c', '/sim_vars.c'),
            (ROOT / 'subprojects/libntrsystem/libraries/g3d/src/gecom.c', '/g3d/src/gecom.c'),
            (ROOT / 'tools/scripts/gx_submission_test.c', '/g3d/src/gecom.c'),
        ]:
            gx_objects.append(compile_source(source, suffix, ['-O1', *sanitize]))
        gx = temp / 'gx-test'
        subprocess.run([flags_for('/mi_dma_gxcommand.c')[0], *gx_objects, *sanitize,
                        '-Wl,--gc-sections', '-o', str(gx)], check=True)
        subprocess.run([str(gx)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True, timeout=10)


if __name__ == '__main__':
    main()
