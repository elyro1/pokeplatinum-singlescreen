#!/usr/bin/env python3
"""Measure the cost of each field graphics effect at night on fixed views (Linux).

Each scene runs day with every effect, night with none, night with every
effect, then night with every effect but one, for each effect in turn. A
copied save and assets are used; VSync and frame caps are off and glFinish
runs after presentation, so the values are frame costs, not gameplay FPS."""
import argparse
import csv
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
# Effects, in the bit order field_night_benchmark.c applies them.
EFFECTS = ('night lighting', 'night glows', 'bloom', 'soft shadows', 'cast shadows', 'water',
           'weather', 'particles')
ALL = (1 << len(EFFECTS)) - 1
MODES = [('day', 3, ALL), ('night, no effects', 1, 0), ('night, all effects', 1, ALL)] + \
    [(f'night, no {name}', 1, ALL & ~(1 << bit)) for bit, name in enumerate(EFFECTS)]
# Scenes: (name, map header, x, z). Each stresses different effects.
SCENES = [
    ('Jubilife', 3, 174, 752),         # Street lamps, Center, characters.
    ('Eterna', 65, 318, 522),          # Windows, trees, forest particles.
    ('Canalave canal', 33, 40, 736),   # Water reflections.
    ('Cycling Road', 350, 303, 600),   # Downlights over the road.
    ('Route 215 rain', 382, 600, 600), # Wet ground, puddles, mist.
]
SYMBOLS = {
    'FIELD': 'DEBUG_GetFieldSystem', 'BUSY': 'FieldSystem_IsRunningTask',
    'CONFIG': 'SIM_Config_prj_GetConfig', 'RESET': 'FieldVisuals_ResetTimePreview',
    'CYCLE': 'FieldVisuals_CycleTimePreview', 'WARP': 'GUI_MapJump_JumpToMap',
}


def stats(values):
    if not values:
        return None
    ordered = sorted(values)
    return {'median_ms': statistics.median(ordered),
            'p95_ms': ordered[int((len(ordered) - 1) * .95)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scales', type=int, nargs='+', default=[3, 10])
    parser.add_argument('--scenes', nargs='+', help='scene names to run (default: all)')
    parser.add_argument('--repeats', type=int, default=2,
                        help='times each scene runs the whole mode list, interleaved (default 2)')
    parser.add_argument('--width', type=int, default=3840)
    parser.add_argument('--height', type=int, default=2068)
    args = parser.parse_args()
    busy = subprocess.run(['pgrep', '-x', 'main'], stdout=subprocess.PIPE, text=True).stdout.split()
    if busy:
        print(f'warning: other game processes are running ({", ".join(busy)}); they share the GPU '
              'and make the results unreliable', flush=True)
    build = args.build.resolve()
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(x for x in commands if x['file'].endswith('/port/visuals/field_visuals.c'))
    flags = shlex.split(entry['command'])
    flags = flags[:flags.index('-MD')]
    index = flags.index('-include')
    del flags[index:index + 2]
    flags.remove('-fpch-preprocess')
    flags += ['-D_GNU_SOURCE', '-include', str(ROOT / 'include/pch/global_pch.h')]
    results = []
    with tempfile.TemporaryDirectory(prefix='platinum-night-bench-') as directory:
        temp = Path(directory)
        runtime = temp / 'game'
        shutil.copytree(build / 'pokeplatinum', runtime)
        for version, binary in [('before', args.baseline), ('after', build / 'main')]:
            if binary is None:
                continue
            binary = binary.resolve()
            symbols = {}
            for line in subprocess.check_output(['nm', '-n', str(binary)], text=True).splitlines():
                parts = line.split()
                if len(parts) == 3 and parts[1] == 'T':
                    symbols[parts[2]] = int(parts[0], 16)
            defines = [f'-DOFF_{key}={symbols[name]}' for key, name in SYMBOLS.items()]
            library = temp / f'{version}.so'
            subprocess.run(flags + defines + ['-fPIC', '-shared', '-I/usr/include/SDL2',
                           str(ROOT / 'tools/scripts/field_night_benchmark.c'), '-ldl',
                           '-o', str(library)], cwd=build, check=True)
            shutil.copy2(binary, runtime / 'main')
            for scale in args.scales:
                for scene, map_id, x, z in SCENES:
                    if args.scenes and scene not in args.scenes:
                        continue
                    (runtime / 'sim_config.ini').write_text(
                        f'[General]\nInternalResolutionScale={scale}\nWindowWidth={args.width}\n'
                        f'WindowHeight={args.height}\nScreenLayout=top_only\n'
                        'VsyncInterval=0\nCapFrameRate=false\n')
                    trace = temp / 'trace.csv'
                    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
                               LD_PRELOAD=str(library), PLATINUM_FRAME_TRACE=str(trace),
                               PLATINUM_GPU_TRACE='1', NIGHT_MAP=str(map_id), NIGHT_X=str(x), NIGHT_Z=str(z),
                               NIGHT_MODES=','.join(f'{preview}:{mask}' for _, preview, mask in MODES * args.repeats))
                    run = subprocess.run(['./main'], cwd=runtime, env=env, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, errors='replace', timeout=600)
                    if run.returncode:
                        raise RuntimeError(run.stdout[-4000:])
                    with trace.open() as stream:
                        frames = {int(row['frame']): row for row in csv.DictReader(stream)}
                    lines = run.stdout.splitlines()
                    for index, (mode, _, _) in enumerate(MODES):
                        # Every repeat of this mode, pooled.
                        phases = {str(index + repeat * len(MODES)) for repeat in range(args.repeats)}
                        samples = [line.split(',') for line in lines
                                   if line.startswith('NIGHT_SAMPLE,') and line.split(',')[1] in phases]
                        frame_ids = {int(sample[2]) for sample in samples}
                        rows = [frames[frame] for frame in frame_ids if frame in frames]
                        gpu = [int(row['gpu_ns']) / 1e6 for row in rows
                               if int(row['gpu_ns']) > 0 and int(row['gpu_result_frame']) in frame_ids]
                        warm_ids = {int(line.split(',')[2]) for line in lines
                                    if line.startswith('NIGHT_WARM,') and line.split(',')[1] in phases}
                        warm = [frames[frame] for frame in warm_ids if frame in frames]
                        result = dict(version=version, scale=scale, scene=scene, mode=mode,
                                      samples=len(rows), valid_gpu_samples=len(gpu),
                                      draws=statistics.median(int(s[3]) for s in samples),
                                      cpu=stats([int(r['work_ns']) / 1e6 for r in rows]),
                                      full=stats([int(r['full_ns']) / 1e6 for r in rows]), gpu=stats(gpu),
                                      warm_full=stats([int(r['full_ns']) / 1e6 for r in warm]),
                                      warm_peak_ms=max(int(r['full_ns']) / 1e6 for r in warm),
                                      steady_peak_ms=max(int(r['full_ns']) / 1e6 for r in rows))
                        # Each repeat's GPU median: repeats that disagree by more than an
                        # effect's cost mean something else disturbed the run.
                        result['gpu_by_repeat'] = []
                        for repeat in range(args.repeats):
                            phase = str(index + repeat * len(MODES))
                            ids = {int(line.split(',')[2]) for line in lines
                                   if line.startswith('NIGHT_SAMPLE,') and line.split(',')[1] == phase}
                            values = sorted(int(frames[i]['gpu_ns']) / 1e6 for i in ids
                                            if i in frames and int(frames[i]['gpu_ns']) > 0)
                            result['gpu_by_repeat'].append(statistics.median(values) if values else None)
                        print(result, flush=True)
                        results.append(result)
                        args.output.write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
