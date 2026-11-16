#!/usr/bin/env python3
"""Run the game once in a throwaway directory and save screenshots (PNG) and state dumps (JSON).

Loads a save slot, optionally warps or starts a debug battle, presses keys, takes shots and quits. The run directory
symlinks the build's assets, gets its own copies of main, the save and fresh ini files, and is deleted afterwards.
Nothing in the build's own pokeplatinum/ directory is written. See docs/scene_runs.md.

In a build configured with -Dautoplay_tests=true, steps wait for game events (field control, the battle menu, a map)
through the test bridge. Other builds fall back to fixed frame numbers through PLATINUM_AUTOPILOT.
"""
import argparse
import configparser
import datetime
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Frame mode: keys for a save that resumes in the field: title screen, Continue, then close the journal.
BOOT_KEYS = [(300, 'Return'), (360, 'Return'), (600, 'z'), (760, 'z'), (950, 'x'), (1050, 'x'), (1150, 'x')]

# Frame mode: rough frames with CapFrameRate=true and the default 60Fps, measured with a save that resumes outdoors.
MILESTONES = {
    'title': 330,
    'field': 1300,  # Field control after the boot keys; a --map warp has landed by about 1200
    'battle': 1300,  # Debug battle: the player's send-out (--wild/--trainer)
    'battle_menu': 1800,  # Command menu of a debug battle (up from ~1500; BW2 visuals and long intros need longer)
}

BUTTONS = {
    'a': 'z', 'b': 'x', 'x': 'c', 'y': 'v', 'start': 'Return', 'select': 'Right_Shift', 'l': 'q', 'r': 'e',
    'up': 'Up', 'down': 'Down', 'left': 'Left', 'right': 'Right', 'tab': 'Tab', 'run': 'Left_Shift',
}

# Bridge mode: DS pad bits (PAD_BUTTON_*); running is B held.
PAD = {
    'a': 0x1, 'b': 0x2, 'select': 0x4, 'start': 0x8, 'right': 0x10, 'left': 0x20, 'up': 0x40, 'down': 0x80,
    'r': 0x100, 'l': 0x200, 'x': 0x400, 'y': 0x800, 'run': 0x2,
}

# battle.menu values (BattleSubscreen activeMenuConfigIndex) of the Fight/Bag/Pokémon/Run menu in its variants
# (first battle, Safari, Pal Park, partner slot); 11 is move selection. See BattleDisplay's command setup.
BATTLE_COMMAND_MENUS = range(1, 11)

# Written fresh for each run; --user-ini starts from the build's own files instead.
BASE_INI = {
    'sim_config.ini': {
        'General': {
            'InternalResolutionScale': '2', 'WindowWidth': '1024', 'WindowHeight': '768', 'FullScreen': 'false',
            'ScreenLayout': 'top_only', 'VsyncInterval': '0', 'CapFrameRate': 'true', 'MasterVolume': '0',
        },
        'Diagnostics': {'WriteLogFile': 'true', 'CrashHandler': 'true'},
    },
    'sim_config_prj.ini': {
        'General': {'60Fps': 'true', 'EnableAsserts': 'true', 'BreakDebuggerOnAssert': 'false'},
        'Developer': {'Autosave': 'false'},
        'Cheats': {'WalkThroughWalls': 'false', 'DisableRandomEncounters': 'true'},
    },
}

# Applied on top of everything, --user-ini included, so shot timings and collisions mean something.
SAFETY = {
    'sim_config.ini': {'General': {'CapFrameRate': 'true', 'FullScreen': 'false'}},
    'sim_config_prj.ini': {'Developer': {'Autosave': 'false'}, 'Cheats': {'WalkThroughWalls': 'false'}},
}

WIDE = {'sim_config.ini': {'General': {'WindowWidth': '2160', 'WindowHeight': '1000'}}}

INI_FILES = {'sim': 'sim_config.ini', 'prj': 'sim_config_prj.ini', 'gameplay': 'gameplay_config.ini'}

# The game writes these in its working directory; never link them to the build's copies.
PRIVATE = re.compile(r'(?i)^(main|.*\.ini|.*save.*\.(bin|bak)|log.*\.txt|dbglog\.txt|autosave|crash.*|story-test\.ppm)$')

# Words that start an "=" condition rather than name a shot ("--shot map=3" waits for map 3).
CONDITION_KEYS = {'map', 'tile', 'x', 'z', 'frame'}

# Milestones only the bridge can see
BRIDGE_MILESTONES = ('menu',)


class SceneError(Exception):
    pass


# ---------------------------------------------------------------- conditions


def lookup(state, path):
    value = state
    for part in path.split('.'):
        if isinstance(value, dict):
            value = value.get(part)
        elif isinstance(value, list) and part.lstrip('-').isdigit() and -len(value) <= int(part) < len(value):
            value = value[int(part)]
        else:
            return None
    return value


def field_ready(state):
    f = state.get('field')
    return bool(f) and not f['busy'] and not f['moving'] and not state.get('menu') and not state.get('battle')


class Condition:
    """When a step happens: a frame number, a milestone or a state test, optionally +N frames later.

    frame is the fixed frame for frame mode (None when only the bridge can tell); test(state, ctx) is for bridge mode.
    """

    def __init__(self, text, milestones=MILESTONES):
        self.label = text
        m = re.fullmatch(r'(.+?)([+-]\d+)?', text.strip())
        if not m:
            raise SceneError(f'bad condition {text!r}')
        base, offset = m.group(1), int(m.group(2) or 0)
        self.offset = offset
        self.frame = None
        if base.isdigit():
            self.frame = int(base) + offset
            n = int(base)
            self.test = lambda s, ctx: s['frame'] >= n
        elif base in milestones or base in BRIDGE_MILESTONES:
            self.frame = milestones[base] + offset if base in milestones else None
            self.test = getattr(self, '_' + base)
        else:
            m = re.fullmatch(r'([A-Za-z_][A-Za-z0-9_.]*)\s*(!=|=|~)\s*(.+)', base)
            if not m:
                raise SceneError(f'bad condition {base!r}: use a frame, a milestone ({", ".join(milestones)}), '
                                 'map=N, tile=X,Z, app~Name, text~words, menu~choice or a state path like field.x=12')
            self.test = self._compare(*m.groups())

    # Milestones in bridge mode
    @staticmethod
    def _title(s, ctx):
        return 'Title' in s.get('app_name', '')

    @staticmethod
    def _field(s, ctx):
        # With --map, the debug warp lands after the field first shows; wait for it.
        return field_ready(s) and (ctx.get('map') is None or s['field']['map'] == ctx['map'])

    @staticmethod
    def _battle(s, ctx):
        return 'Battle' in s.get('app_name', '') and not s.get('field')

    @staticmethod
    def _menu(s, ctx):
        return bool(s.get('menu'))

    @staticmethod
    def _battle_menu(s, ctx):
        return bool(s.get('battle')) and s['battle']['menu'] in BATTLE_COMMAND_MENUS

    @staticmethod
    def _compare(path, op, want):
        want = want.strip().strip('"\'')
        if path == 'tile':
            x, _, z = want.partition(',')
            if not (x.strip().isdigit() and z.strip().isdigit()):
                raise SceneError(f'bad tile {want!r}: use tile=X,Z')
            target = (int(x), int(z))
            return lambda s, ctx: bool(s.get('field')) and ((s['field']['x'], s['field']['z']) == target) == (op != '!=')
        path = {'map': 'field.map', 'x': 'field.x', 'z': 'field.z', 'app': 'app_name',
                'menu': 'menu.choices'}.get(path, path)
        if path == 'field.map' and not want.lstrip('-').isdigit():
            name = want.upper()
            if name not in MAP_HEADERS:
                raise SceneError(f'unknown map {want!r}: use a header number or a MAP_HEADER_ name without the prefix')
            want = str(MAP_HEADERS[name])

        def test(s, ctx):
            value = lookup(s, path)
            if op == '~':
                text = ' '.join(map(str, value)) if isinstance(value, list) else str(value or '')
                return want.lower() in text.lower()
            same = str(value).lower() == want.lower() or (isinstance(value, bool) and str(int(value)) == want)
            return same == (op == '=')
        return test


def load_map_headers(build):
    names = {}
    path = build / 'generated' / 'map_headers.h'
    if path.exists():
        for m in re.finditer(r'#define MAP_HEADER_(\w+)\s+(\d+)', path.read_text()):
            names[m.group(1)] = int(m.group(2))
    return names


def load_heap_names():
    names, n = {}, 0
    text = (ROOT / 'include' / 'constants' / 'heap.h').read_text()
    body = text[text.index('enum HeapID {'):]
    body = body[:body.index('};')]
    for m in re.finditer(r'(HEAP_ID_\w+)(?:\s*=\s*(\w+))?\s*,', body):
        n = int(m.group(2), 0) if m.group(2) else n
        names[n] = m.group(1)[len('HEAP_ID_'):]
        n += 1
    return names


MAP_HEADERS = {}

# ---------------------------------------------------------------- steps


class Step:
    def __init__(self, kind, cond, name=None, key=None, hold=4):
        self.kind, self.cond, self.name, self.key, self.hold = kind, cond, name, key, hold


def split_name(text, default_from_cond=True):
    """'name=COND' -> (name, COND); a bare condition gets a name made from it."""
    name, sep, rest = text.partition('=')
    if sep and re.fullmatch(r'[A-Za-z0-9_-]+', name) and name not in CONDITION_KEYS and rest:
        return name, rest
    return re.sub(r'[^a-z0-9]+', '_', text.lower()).strip('_') or 'shot', text


def parse_step(kind, text):
    if kind in ('shot', 'dump'):
        name, cond = split_name(text)
        return Step(kind, Condition(cond), name=name)
    if kind == 'key':
        parts = text.rsplit(':', 2)
        if len(parts) == 3 and parts[2].isdigit():
            cond, key, hold = parts[0], parts[1], int(parts[2])
        else:
            parts = text.rsplit(':', 1)
            if len(parts) != 2:
                raise SceneError(f'bad key {text!r}: use WHEN:KEY[:HOLD]')
            cond, key, hold = parts[0], parts[1], 4
        return Step('key', Condition(cond), key=key, hold=hold)
    return Step(kind, Condition(text))  # wait, expect


class StepAction(argparse.Action):
    """Keeps --key/--shot/--dump/--wait/--expect in command-line order: in bridge mode they run in that order."""

    def __call__(self, parser, namespace, values, option_string=None):
        steps = getattr(namespace, 'steps', None) or []
        steps.append((self.const, values))
        namespace.steps = steps


# ---------------------------------------------------------------- ini files


def parse_set(text):
    """'prj.General.BW2BattleVisuals=true' -> (file, section, key, value)."""
    m = re.fullmatch(r'(sim|prj|gameplay)\.([A-Za-z]+)\.([A-Za-z0-9_]+)=(.*)', text)
    if not m:
        raise SceneError(f'bad --set {text!r}: use sim|prj|gameplay.Section.Key=value')
    short, section, key, value = m.groups()
    return INI_FILES[short], section, key, value


def merge(dst, src):
    for name, sections in src.items():
        for section, keys in sections.items():
            dst.setdefault(name, {}).setdefault(section, {}).update(keys)


def read_ini(text):
    parser = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(';',))
    parser.optionxform = str
    parser.read_string(text)
    return {s: dict(parser[s]) for s in parser.sections()}


def write_ini(path, sections):
    path.write_text(''.join(f'[{s}]\n' + ''.join(f'{k}={v}\n' for k, v in keys.items()) + '\n'
                            for s, keys in sections.items()))


def build_ini(args, runtime_src):
    inis = {}
    if args.user_ini:
        for name in INI_FILES.values():
            if (runtime_src / name).exists():
                inis[name] = read_ini((runtime_src / name).read_text(errors='replace'))
    else:
        merge(inis, BASE_INI)
    if args.wide:
        merge(inis, WIDE)
    for name, section, key, value in args.set:
        merge(inis, {name: {section: {key: value}}})
    merge(inis, SAFETY)
    return inis


def effective_settings(run, env):
    """The game's own view of all three ini files (main --print-config), as {file: {section: {key: value}}}."""
    out = subprocess.run(['./main', '--print-config'], cwd=run, env=env, capture_output=True, text=True,
                         timeout=30).stdout
    return out, parse_print_config(out)


def parse_print_config(out):
    texts, env_vars, current = {}, {}, None
    for line in out.splitlines():
        if line.startswith('; '):
            m = re.fullmatch(r'; (\S+\.ini)', line.strip())
            current = m.group(1) if m else 'environment'
            texts.setdefault(current, '')
        elif current == 'environment' and '=' in line:
            key, _, value = line.partition('=')
            env_vars[key.strip()] = value.strip()
        elif current and line.strip():
            texts[current] += line + '\n'
    settings = {name: read_ini(text) for name, text in texts.items() if name != 'environment'}
    settings['environment'] = env_vars
    return settings


# ---------------------------------------------------------------- frame mode (PLATINUM_AUTOPILOT)


def autopilot_lines(args, steps):
    events = []
    if not args.no_boot:
        events += [(f, 'key', k, 4) for f, k in BOOT_KEYS]
    for step in steps:
        if step.cond.frame is None:
            raise SceneError(f'{step.cond.label!r} needs the test bridge: configure the build with '
                             '-Dautoplay_tests=true, or use a frame or milestone')
        if step.kind == 'key':
            events.append((step.cond.frame, 'key', BUTTONS.get(step.key.lower(), step.key), step.hold))
        elif step.kind == 'shot':
            events.append((step.cond.frame, 'shot', f'{step.name}.ppm', None))
        elif step.kind in ('dump', 'expect'):
            raise SceneError(f'--{step.kind} needs the test bridge: configure the build with -Dautoplay_tests=true')
    end = max([f for f, *_ in events] + [args.until.frame if args.until else 0])
    events.append((end + 2, 'quit', None, None))
    events.sort(key=lambda e: e[0])  # Stable: lines run in file order and wait for their frame
    lines = []
    for frame, kind, arg, hold in events:
        if kind == 'key':
            lines.append(f'{frame} key {arg} {hold}')
        elif kind == 'shot':
            lines.append(f'{frame} shot {arg}')
        else:
            lines.append(f'{frame} quit')
    return lines, end + 2


def run_frame_mode(args, run, env, steps, out):
    lines, last_frame = autopilot_lines(args, steps)
    (run / 'autopilot.txt').write_text('\n'.join(lines) + '\n')
    env = dict(env, PLATINUM_AUTOPILOT=str(run / 'autopilot.txt'))
    # Roughly real time at 60 FPS, plus loading slack; the game ignores SIGTERM, so kill it.
    timeout = last_frame / 60 * 2 + 60
    try:
        proc = subprocess.run(['./main'], cwd=run, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout)
        output, code = proc.stdout.decode(errors='replace'), proc.returncode
    except subprocess.TimeoutExpired as e:
        output, code = (e.stdout or b'').decode(errors='replace'), 'timeout'
    (out / 'output.txt').write_text(output)
    problems = [] if code == 0 else [f'game ended with {code}; see {out / "output.txt"}']
    return output, problems


# ---------------------------------------------------------------- bridge mode


def app_names(exe):
    """Relative function offsets the bridge reports as "app" -> function names, or None without the bridge."""
    table = {}
    for line in subprocess.run(['nm', '-n', str(exe)], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ('T', 't'):
            table[parts[2]] = int(parts[0], 16)
    base = table.get('AutoplayTest_ReadPad')
    if base is None:
        return None
    return {address - base: name for name, address in table.items()}


class Bridge:
    """Drives the game frame by frame over PLATINUM_STORY_TEST_FD (src/port/debug/autoplay_test.c)."""

    BOOT_PULSE = 30  # Frames between boot presses

    def __init__(self, args, run, env, steps, out, names, settings):
        self.args, self.run, self.env, self.steps, self.out = args, run, env, steps, out
        self.names, self.settings = names, settings
        self.heap_names = load_heap_names()
        self.map_names = {v: k for k, v in MAP_HEADERS.items()}
        self.ctx = {'map': int(args.map.split(':')[0]) if args.map else None}
        self.held = []  # [pad bits, last frame]
        self.pending_shot = None  # (name, frame) requested, waiting for story-test.ppm
        self.shots, self.problems = [], []
        self.trace = (out / 'trace.jsonl').open('w') if args.trace else None

    def describe(self, s):
        f = s.get('field') or {}
        bits = [f"frame {s['frame']}", f"app {s.get('app_name')}"]
        if f:
            bits.append(f"map {f.get('map')} ({self.map_names.get(f.get('map'), '?')}) tile {f.get('x')},{f.get('z')}"
                        f"{' busy' if f.get('busy') else ''}")
        if s.get('menu'):
            bits.append(f"menu {s['menu'].get('choices')}")
        if s.get('battle'):
            bits.append(f"battle menu {s['battle'].get('menu')}")
        if s.get('text'):
            bits.append(f"text {s['text'][:60]!r}")
        return ', '.join(bits)

    def dump(self, name, s, step=None):
        state = dict(s)
        state.pop('grid', None)  # Collision grid near the player, only sent when it changes
        f = state.get('field')
        if f:
            state['field'] = dict(f, map_name=self.map_names.get(f.get('map')))
        state['heaps'] = [dict(id=h[0], name=self.heap_names.get(h[0], '?'), size=h[1], free=h[2], used=h[1] - h[2])
                          for h in s.get('heaps', [])]
        doc = dict(name=name, frame=s['frame'], step=step, state=state, settings=self.settings)
        path = self.out / f'{name}.json'
        path.write_text(json.dumps(doc, indent=1) + '\n')
        return path

    def boot_pad(self, s):
        # Title screen and Continue menu take A; the journal closes with B. Stops once the field has shown.
        if self.args.no_boot or self.booted or s['frame'] % self.BOOT_PULSE:
            return 0
        if s.get('field') or s.get('battle'):
            self.booted = True
            return 0
        return PAD['b'] if 'Journal' in s.get('app_name', '') else PAD['a']

    def collect_shot(self, final=False):
        if not self.pending_shot:
            return
        ppm = self.run / 'story-test.ppm'
        if ppm.exists():
            name, frame = self.pending_shot
            ppm.rename(self.run / f'{name}.ppm')
            self.shots.append((name, frame))
            self.pending_shot = None
        elif final:
            self.problems.append(f'shot {self.pending_shot[0]} was requested but never written')

    def play(self):
        parent, child = socket.socketpair()
        parent.settimeout(60)
        env = dict(self.env, PLATINUM_STORY_TEST_FD=str(child.fileno()))
        log = (self.out / 'output.txt').open('w')
        proc = subprocess.Popen(['./main'], cwd=self.run, env=env, pass_fds=(child.fileno(),), stdout=log,
                                stderr=subprocess.STDOUT)
        child.close()
        stream = parent.makefile('rb')
        start = time.monotonic()
        self.booted = False
        index, history, scan, wait_from, last_fire, fire_at, state = 0, [], 0, 0, 0, None, None
        done_frame = None
        try:
            while True:
                line = stream.readline()
                if not line:
                    raise SceneError(f'game exited (code {proc.poll()}); see {self.out / "output.txt"}')
                state = json.loads(line)
                state['app_name'] = self.names.get(state.get('app'), str(state.get('app')))
                frame = state['frame']
                if self.trace:
                    self.trace.write(json.dumps(state, separators=(',', ':')) + '\n')
                self.collect_shot()
                history.append(state)
                shot = 0
                # Steps run in order. Each one looks for its condition from the frame where the previous one's
                # condition held, so "--key field:x --shot menu=field+40" both count from the same field frame.
                while index < len(self.steps):
                    step = self.steps[index]
                    if fire_at is None:
                        if step.kind == 'expect':
                            if not step.cond.test(state, self.ctx):
                                self.dump('expect_failed', state, step.cond.label)
                                raise SceneError(f'--expect {step.cond.label} is false at {self.describe(state)}')
                            index, wait_from = index + 1, frame
                            continue
                        found = next((i for i in range(scan, len(history)) if step.cond.test(history[i], self.ctx)),
                                     None)
                        if found is None:
                            scan = len(history)
                            if frame - wait_from > self.args.wait_limit:
                                self.dump('wait_failed', state, step.cond.label)
                                raise SceneError(f'waited {self.args.wait_limit} frames for {step.cond.label!r} '
                                                 f'(step {index + 1}); now at {self.describe(state)}')
                            break
                        history, scan = history[found:], 0
                        if step.cond.offset < 0:
                            raise SceneError(f'{step.cond.label!r}: negative offsets only work with fixed frames')
                        fire_at = max(history[0]['frame'] + step.cond.offset, last_fire)
                    if frame < fire_at:
                        break
                    if step.kind == 'shot' and self.pending_shot:
                        break  # One shot file at a time; takes the next frame
                    if step.kind == 'key':
                        bits = 0
                        for name in step.key.lower().split('+'):
                            if name not in PAD:
                                raise SceneError(f'key {step.key!r}: the bridge takes DS buttons '
                                                 f'({", ".join(PAD)}), joined with + for several')
                            bits |= PAD[name]
                        self.held.append([bits, frame + step.hold - 1])
                    elif step.kind == 'shot':
                        shot = 1
                        self.pending_shot = (step.name, frame)
                        self.dump(step.name, state, step.cond.label)
                    elif step.kind == 'dump':
                        self.dump(step.name, state, step.cond.label)
                    print(f'scene: {step.kind} {step.name or step.key or step.cond.label} at {self.describe(state)}',
                          file=sys.stderr)
                    index, fire_at, last_fire, wait_from = index + 1, None, frame, frame
                if index >= len(self.steps) and not self.pending_shot and not self.held:
                    until = self.args.until
                    if until is None or until.test(state, self.ctx):
                        done_frame = done_frame or frame
                        if frame >= done_frame + 2:
                            break
                pad = self.boot_pad(state)
                for held in self.held:
                    pad |= held[0]
                self.held = [h for h in self.held if h[1] > frame]
                if time.monotonic() - start > self.args.timeout:
                    raise SceneError(f'whole run took over {self.args.timeout} s; now at {self.describe(state)}')
                os.write(parent.fileno(), f'{pad} -1 -1 0 {shot}\n'.encode())
        except SceneError as e:
            self.problems.append(str(e))
            # A shot of where it got stuck
            try:
                os.write(parent.fileno(), b'0 -1 -1 0 1\n')
                for _ in range(3):
                    stream.readline()
                    os.write(parent.fileno(), b'0 -1 -1 0 0\n')
                self.pending_shot = ('failed', state['frame'] if state else 0)
            except OSError:
                pass
        except (socket.timeout, TimeoutError):
            self.problems.append(f'game stopped answering; see {self.out / "output.txt"}')
        finally:
            proc.kill()  # The game ignores SIGTERM
            proc.wait()
            stream.close()
            parent.close()
            log.close()
            if self.trace:
                self.trace.close()
        self.collect_shot(final=True)
        return (self.out / 'output.txt').read_text(errors='replace'), self.problems


# ---------------------------------------------------------------- run setup


def game_env(args):
    # Drop inherited debug switches so a stray export can't change the scene.
    env = {k: v for k, v in os.environ.items() if not k.startswith('PLATINUM_') and k != 'LD_PRELOAD'}
    env['SDL_AUDIODRIVER'] = 'dummy'
    if not args.visible:
        env['SDL_VIDEODRIVER'] = 'offscreen'
    if args.map:
        env['PLATINUM_DEBUG_MAP'] = args.map
    if args.wild:
        env['PLATINUM_DEBUG_WILD'] = args.wild
    if args.trainer:
        env['PLATINUM_DEBUG_TRAINER'] = args.trainer
    if args.clock:
        clock = args.clock
        if re.fullmatch(r'\d{1,2}:\d{2}', clock):
            clock = f'2026-06-01T{int(clock.split(":")[0]):02d}:{clock.split(":")[1]}'
        env['PLATINUM_FIXED_CLOCK'] = clock
    if args.asan:
        env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    for item in args.env:
        key, sep, value = item.partition('=')
        if not sep:
            raise SceneError(f'bad --env {item!r}: use NAME=value')
        env[key] = value
    return env


def resolve_save(spec, runtime_src):
    if spec.lower() == 'none':
        return None
    if spec.isdigit():
        path = runtime_src / f'save{spec}.bin'
    else:
        path = Path(spec)
        if not path.exists() and (runtime_src / spec).exists():
            path = runtime_src / spec
    if not path.is_file():
        raise SceneError(f'save {spec!r} not found (looked for {path})')
    return path


def prepare_run_dir(run, runtime_src, exe, save, inis):
    for entry in runtime_src.iterdir():
        if PRIVATE.match(entry.name):
            continue
        (run / entry.name).symlink_to(entry.resolve())
    (run / 'autosave').mkdir()
    shutil.copy2(exe, run / 'main')
    if save is not None:
        shutil.copy2(save, run / 'save1.bin')
    for name, sections in inis.items():
        write_ini(run / name, sections)


def ppm_to_png(src, dst):
    data = src.read_bytes()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not m:
        raise SceneError(f'{src.name} is not a P6 PPM')
    width, height = int(m.group(1)), int(m.group(2))
    pixels = data[m.end():m.end() + width * height * 3]
    stride = width * 3
    raw = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))

    dst.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                    + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog='WHEN is a frame, a milestone (title, field, battle, battle_menu, menu in '
                                'bridge mode), or with the bridge map=N, tile=X,Z, app~Name, text~words, '
                                'menu~choice or a state path (field.x=12); +N waits N more frames.')
    p.add_argument('--build', type=Path, default=ROOT / 'build_linux', help='build dir with pokeplatinum/ (assets)')
    p.add_argument('--asan', action='store_true', help='run build_linux_asan/main with build_linux assets')
    p.add_argument('--exe', type=Path, help='game executable to copy instead of <build>/main')
    p.add_argument('--save', default='1', help='slot number of <build>/pokeplatinum/saveN.bin, a file name there, '
                   'a path, or none (default: 1)')
    p.add_argument('--map', help='PLATINUM_DEBUG_MAP header:x:z')
    p.add_argument('--wild', help='PLATINUM_DEBUG_WILD species:level')
    p.add_argument('--trainer', help='PLATINUM_DEBUG_TRAINER id[,id]')
    p.add_argument('--clock', help='HH:MM or YYYY-MM-DDTHH:MM for PLATINUM_FIXED_CLOCK')
    p.add_argument('--wide', action='store_true', help='2160x1000 window (widescreen layouts)')
    p.add_argument('--set', action='append', default=[], type=lambda s: parse_set(s),
                   help='ini override, e.g. prj.General.BW2BattleVisuals=true (repeatable)')
    p.add_argument('--user-ini', action='store_true', help="start from the build's own ini files instead of defaults")
    p.add_argument('--env', action='append', default=[], help='extra environment variable NAME=value (repeatable)')
    p.add_argument('--shot', action=StepAction, const='shot', dest='steps', default=[],
                   help='[name=]WHEN: screenshot (and, with the bridge, a state dump)')
    p.add_argument('--key', action=StepAction, const='key', dest='steps',
                   help='WHEN:KEY[:HOLD]; KEY is a DS button (a b x y l r start select up down left right), run, '
                   'or in frame mode tab or an SDL key name')
    p.add_argument('--dump', action=StepAction, const='dump', dest='steps', help='[name=]WHEN: state dump only (bridge)')
    p.add_argument('--wait', action=StepAction, const='wait', dest='steps', help='WHEN: wait for it (bridge)')
    p.add_argument('--expect', action=StepAction, const='expect', dest='steps',
                   help='CONDITION that must hold when the previous step has happened, or the run fails (bridge)')
    p.add_argument('--until', type=lambda s: Condition(s), help='keep running until WHEN')
    p.add_argument('--wait-limit', type=int, default=3600, help='bridge frames a step may wait (default 3600)')
    p.add_argument('--timeout', type=int, default=600, help='bridge mode: whole-run seconds (default 600)')
    p.add_argument('--frames', action='store_true', help='use fixed frames (PLATINUM_AUTOPILOT) even with the bridge')
    p.add_argument('--trace', action='store_true', help='bridge mode: write every frame\'s state to trace.jsonl')
    p.add_argument('--no-boot', action='store_true', help="don't press through the title screen")
    p.add_argument('--out', type=Path, help='where results go (default <build>/scene-shots/<time>)')
    p.add_argument('--keep', action='store_true', help='keep the run dir (path is printed)')
    p.add_argument('--keep-ppm', action='store_true', help='also keep the raw PPM shots')
    p.add_argument('--visible', action='store_true', help='open a real window instead of the offscreen driver')
    p.add_argument('--strict-config', action='store_true', help='fail when the game reports an ignored ini setting')
    p.add_argument('--dry-run', action='store_true', help='print the plan, env and inis, run nothing')
    return p


def main(argv=None):
    p = build_parser()
    try:
        args = p.parse_args(argv)
        build = args.build.resolve()
        MAP_HEADERS.update(load_map_headers(build))
        runtime_src = build / 'pokeplatinum'
        if args.asan and args.exe is None:
            args.exe = ROOT / 'build_linux_asan' / 'main'
        exe = (args.exe or build / 'main').resolve()
        steps = [parse_step(kind, text) for kind, text in args.steps]
        if not steps and not args.until:
            raise SceneError('nothing to do: pass --shot, --dump or --until')
        names = [s.name for s in steps if s.name]
        if len(names) != len(set(names)):
            raise SceneError('shot and dump names must be unique; use name=WHEN')
        bridge_names = None if args.frames or not exe.is_file() else app_names(exe)
        env = game_env(args)
        inis = build_ini(args, runtime_src)
        if args.dry_run:
            if bridge_names is None:
                print('frame mode\n' + '\n'.join(autopilot_lines(args, steps)[0]))
            else:
                print('bridge mode\n' + '\n'.join(f'{s.kind} {s.name or s.key or ""} when {s.cond.label}'
                                                   for s in steps))
            print({k: v for k, v in env.items() if k.startswith(('PLATINUM_', 'SDL_', 'ASAN'))})
            for name, sections in inis.items():
                print(f'--- {name}\n' + ''.join(f'[{s}] ' + ' '.join(f'{k}={v}' for k, v in keys_.items()) + '\n'
                                                for s, keys_ in sections.items()), end='')
            return 0
        if bridge_names is None:
            autopilot_lines(args, steps)  # Reject bridge-only steps before starting
        if not runtime_src.is_dir():
            raise SceneError(f'{runtime_src} missing: build first and run the game once to extract the ROM')
        if not exe.is_file():
            raise SceneError(f'{exe} missing: build it first')
        save = resolve_save(args.save, runtime_src)
    except SceneError as e:
        p.error(str(e))

    out = (args.out or build / 'scene-shots' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
    out.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='platinum-scene-'))
    try:
        prepare_run_dir(run, runtime_src, exe, save, inis)
        config_text, settings = effective_settings(run, env)
        (out / 'settings.ini').write_text(config_text)
        if bridge_names is None:
            print('scene: frame mode' + ('' if args.frames else ' (no test bridge in this build)'), file=sys.stderr)
            output, problems = run_frame_mode(args, run, env, steps, out)
            shots = [(s.name, s.cond.frame) for s in steps if s.kind == 'shot']
        else:
            bridge = Bridge(args, run, env, steps, out, bridge_names, settings)
            output, problems = bridge.play()
            shots = bridge.shots
        if (run / 'log.txt').exists():
            shutil.copy2(run / 'log.txt', out / 'log.txt')

        written = []
        for name, frame in shots:
            ppm = run / f'{name}.ppm'
            if not ppm.exists():
                problems.append(f'no shot for {name} (frame {frame}): game quit or crashed first?')
                continue
            ppm_to_png(ppm, out / f'{name}.png')
            if args.keep_ppm:
                shutil.copy2(ppm, out / ppm.name)
            written.append(out / f'{name}.png')

        config_warnings = sorted({l for l in output.splitlines() if l.startswith('[config]')})
        for line in config_warnings:
            print(line, file=sys.stderr)
        for path in written:
            print(path)
        for path in sorted(out.glob('*.json')):
            print(path)
        for problem in problems:
            print(f'scene: {problem}', file=sys.stderr)
        failed = bool(problems) or (bool(config_warnings) and args.strict_config)
        return 1 if failed else 0
    finally:
        if args.keep:
            print(f'scene: run dir kept at {run}', file=sys.stderr)
        else:
            shutil.rmtree(run, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
