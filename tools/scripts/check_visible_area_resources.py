#!/usr/bin/env python3
"""Check native scenery resources across area boundaries using copied saves.

Requires an unstripped Linux build, gdb (only for reading DWARF offsets), and
packaged assets with a usable save. No running game or original save is touched.
Captures both sides of the Jubilife/Route 218 gate by day and night, repeats
transitions through Eterna and the gate interior, and checks cancellation cleanup.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SYMBOLS = {
    'FIELD': 'DEBUG_GetFieldSystem', 'BUSY': 'FieldSystem_IsRunningTask',
    'WARP': 'GUI_MapJump_JumpToMap', 'RESET': 'FieldVisuals_ResetTimePreview',
    'CYCLE': 'FieldVisuals_CycleTimePreview', 'BINDTEX': 'NNS_G3dBindMdlTex',
    'BINDPALETTE': 'NNS_G3dBindMdlPltt',
    'NEW_ANIMATIONS': 'MapPropAnimationManager_NewOnHeap',
    'NEW_AREA': 'AreaDataManager_AllocPreview', 'AREA_STEP': 'AreaDataManager_LoadPreviewStep',
    'UNLOAD_ANIMATIONS': 'MapPropAnimationManager_UnloadAllAnimations',
    'FREE_ANIMATIONS': 'MapPropAnimationManager_Free', 'FREE_AREA': 'AreaDataManager_FreePreview',
    'HEAP_FREE': 'HeapExp_FndGetTotalFreeSize',
    'ALLOC_TEX': 'NNS_GfdAllocLnkTexVram', 'FREE_TEX': 'NNS_GfdFreeLnkTexVram',
    'ALLOC_PALETTE': 'NNS_GfdAllocLnkPlttVram', 'FREE_PALETTE': 'NNS_GfdFreeLnkPlttVram',
}
OFFSETS = {
    'LAND_CACHE': '(long)&((LandDataManager*)0)->prepared',
    'CHUNK_SIZE': 'sizeof(PCPreparedChunk)',
    'CHUNK_STAGE': '(long)&((PCPreparedChunk*)0)->stage',
    'CHUNK_AREA': '(long)&((PCPreparedChunk*)0)->area',
    'CHUNK_MODEL': '(long)&((PCPreparedChunk*)0)->map.mapRenderObj.resMdl',
    'CHUNK_PROPS': '(long)&((PCPreparedChunk*)0)->map.mapPropManager',
    'AREA_RESOURCES': '(long)&((PCPreparedArea*)0)->resources',
    'AREA_REFERENCES': '(long)&((PCPreparedArea*)0)->references',
}


def debug_offsets(binary):
    # Inspect the binary's types without launching or attaching to a process.
    args = ['gdb', '-q', '-batch', str(binary)]
    for expression in OFFSETS.values():
        args += ['-ex', 'p ' + expression]
    output = subprocess.check_output(args, text=True)
    values = re.findall(r'^\$\d+ = (\d+)$', output, re.MULTILINE)
    if len(values) != len(OFFSETS):
        raise RuntimeError('Missing scenery debug types; use an unstripped Linux build')
    return dict(zip(OFFSETS, values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--baseline', type=Path, help='optional older binary for comparison captures')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(x for x in commands if x['file'].endswith('/port/field_visuals.c'))
    flags = shlex.split(entry['command'])
    flags = flags[:flags.index('-MD')]
    index = flags.index('-include')
    del flags[index:index + 2]
    flags.remove('-fpch-preprocess')
    flags += ['-D_GNU_SOURCE', '-include', str(ROOT / 'include/pch/global_pch.h')]
    for label, binary in [('before', args.baseline), ('after', build / 'main')]:
        if binary is None:
            continue
        binary = binary.resolve()
        symbols = {}
        for line in subprocess.check_output(['nm', '-n', str(binary)], text=True).splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1] == 'T':
                symbols[parts[2]] = int(parts[0], 16)
        defines = [f'-DOFF_{key}={symbols[name]}' for key, name in SYMBOLS.items()
                   if name in symbols]
        if label == 'after':
            missing = set(SYMBOLS.values()) - symbols.keys()
            if missing:
                raise RuntimeError('Missing native test symbols: ' + ', '.join(sorted(missing)))
            defines += ['-DCHECK_RESOURCES']
            defines += [f'-D{key}={value}' for key, value in debug_offsets(binary).items()]
        with tempfile.TemporaryDirectory(prefix='platinum-visible-areas-') as directory:
            temp = Path(directory)
            runtime = temp / 'game'
            shutil.copytree(build / 'pokeplatinum', runtime)
            shutil.copy2(binary, runtime / 'main')
            library = temp / 'check.so'
            subprocess.run(flags + defines + ['-fPIC', '-shared', '-I/usr/include/SDL2',
                           str(ROOT / 'tools/scripts/visible_area_resources_check.c'),
                           '-ldl', '-o', str(library)], cwd=build, check=True)
            (runtime / 'sim_config.ini').write_text(
                '[General]\nInternalResolutionScale=2\nWindowWidth=1280\nWindowHeight=552\n'
                'ScreenLayout=top_only\nVsyncInterval=0\nCapFrameRate=false\n')
            env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
                       LD_PRELOAD=str(library))
            print('Checking ' + label, flush=True)
            result = subprocess.run(['./main'], cwd=runtime, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=240)
            (output / (label + '.log')).write_text(result.stdout)
            for capture in runtime.glob('capture-*.ppm'):
                shutil.copy2(capture, output / (label + '-' + capture.name))
            if result.returncode or result.stdout.count('VISIBLE_SCENE,') != 9:
                raise RuntimeError(f'{label} failed ({result.returncode}):\n{result.stdout[-4000:]}')
            if label == 'after' and result.stdout.count('CHECK_SCENE,') != 9:
                raise RuntimeError('Missing resource validation results')
            print(f'{label}: 9 scenes passed', flush=True)


if __name__ == '__main__':
    main()
