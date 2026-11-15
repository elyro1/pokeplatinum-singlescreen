#!/usr/bin/env python3
"""Exercise real lighting data and GPU rendering (SDL offscreen / Mesa works)."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def color(value):
    return value['red'] | (value['green'] << 5) | (value['blue'] << 10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    args = parser.parse_args()
    build = args.build.resolve()
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(x for x in commands if x['file'].endswith('/port/field_visuals.c'))
    flags = shlex.split(entry['command'])
    # Reuse this build's generated headers, target defines, and PCH.
    flags = flags[:flags.index('-MD')]
    with tempfile.TemporaryDirectory(prefix='field-visuals-') as directory:
        temp = Path(directory)
        fixtures = []
        for archive in range(4):
            data = json.loads((ROOT / f'res/field/lighting/lighting_set_{archive:03}.json').read_text())
            rows = []
            for item in data:
                mask = sum(1 << i for i, light in enumerate(item['lights']) if light['enabled'])
                colors = ','.join(str(color(light['color'])) for light in item['lights'])
                vectors = ','.join('{' + ','.join(str(round(light['direction'][axis] * 4096))
                                               for axis in ('x', 'y', 'z')) + '}' for light in item['lights'])
                materials = ','.join(str(color(item[name])) for name in
                                     ('diffuseColor', 'ambientColor', 'specularColor', 'emissionColor'))
                rows.append(f'{{{item["endTime"]},{mask},{{{colors}}},{{{vectors}}},{materials}}}')
            fixtures.append(rows)
        (temp / 'lighting_fixtures.h').write_text(
            'static const AreaLightTemplate fixtures[4][15] = {\n' +
            ',\n'.join('{' + ',\n'.join(rows) + '}' for rows in fixtures) +
            '};\nstatic const int counts[] = {' + ','.join(str(len(rows)) for rows in fixtures) + '};\n')
        test_object = temp / 'test.o'
        subprocess.run(flags + [f'-I{temp}', '-c', str(ROOT / 'tools/scripts/field_visuals_test.c'),
                                '-o', str(test_object)], cwd=build, check=True)
        executable = temp / 'lighting-test'
        subprocess.run([flags[0], str(test_object),
                        str(build / 'main.p/src_port_field_visuals.c.o'),
                        str(build / 'main.p/src_port_field_lights.c.o'),
                        str(build / 'main.p/src_port_sim_config_prj.c.o'),
                        str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_config_ini.c.o'),
                        str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_config_config_check.c.o'),
                        '-lm', '-o', str(executable)], check=True)
        subprocess.run([str(executable)], cwd=temp, check=True)

        gpu = temp / 'gpu-test'
        sdl = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'], text=True))
        subprocess.run(['g++', '-std=c++11', f'-I{ROOT / "subprojects/libntr/include"}',
                        str(ROOT / 'tools/scripts/field_visuals_gpu_test.cpp'),
                        str(build / 'subprojects/libntr/libraries/sim/libsim.a.p/src_glad.c.o'),
                        *sdl, '-ldl', '-o', str(gpu)], check=True)
        env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get('SDL_VIDEODRIVER', 'offscreen'))
        subprocess.run([str(gpu), str(ROOT / 'subprojects/libntr/libraries/sim/src/shaders')],
                       cwd=temp, env=env, check=True)


if __name__ == '__main__':
    main()
