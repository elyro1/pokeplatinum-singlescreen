#!/usr/bin/env python3
"""Crash the game on purpose (PLATINUM_DEBUG_CRASH) and check log.txt: the startup header and settings, crash and
stall reports, the settings being logged again after a change, the exit signal, and symbolize_crash.py.

Runs the real game headless in scratch run dirs, so the build needs the extracted ROM assets in <build>/pokeplatinum.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
COPIED = ('main', 'sim_config.ini', 'sim_config_prj.ini', 'gameplay_config.ini', 'imgui.ini')
# Files the game writes; never link them back into the real game folder
SKIPPED = re.compile(r'(save|log|DbgLog|autosave|profile|field_lights)', re.IGNORECASE)


def make_run_dir(game, parent, name):
    run = parent / name
    run.mkdir()
    for entry in game.iterdir():
        if entry.name in COPIED or SKIPPED.search(entry.name) or entry.suffix in ('.ini', '.bak'):
            continue
        (run / entry.name).symlink_to(entry)
    for name in COPIED:
        if (game / name).exists():
            shutil.copy2(game / name, run / name)
    set_key(run / 'sim_config.ini', 'General', 'CapFrameRate', 'true')
    return run


def set_key(path, section, key, value):
    """Sets a key in an ini section, adding the section when missing."""
    lines = path.read_text().splitlines() if path.exists() else []
    out, current, done = [], None, False
    for line in lines:
        header = re.match(r'\[(.+)\]', line.strip())
        if header:
            if current == section and not done:
                out.append(f'{key}={value}')
                done = True
            current = header.group(1)
        elif current == section and line.split('=', 1)[0].strip() == key:
            if not done:
                out.append(f'{key}={value}')
                done = True
            continue
        out.append(line)
    if not done:
        if current != section:
            out.append(f'[{section}]')
        out.append(f'{key}={value}')
    path.write_text('\n'.join(out) + '\n')


def run_game(run, crash, timeout=60, during=None):
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy', PLATINUM_DEBUG_CRASH=crash,
               PLATINUM_FIXED_CLOCK='2026-06-01T14:00')
    env.pop('PLATINUM_AUTOPILOT', None)
    with open(run / 'stderr.txt', 'w') as stderr:
        process = subprocess.Popen(['./main'], cwd=run, env=env, stdout=subprocess.DEVNULL, stderr=stderr)
        if during:
            during(run)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # The game ignores SIGTERM
            process.kill()
            process.wait()
    return process.returncode, (run / 'log.txt').read_text(errors='replace') if (run / 'log.txt').exists() else ''


def report(log, kind):
    start = log.find(f'=== {kind}')
    assert start >= 0, f'no {kind} report in log.txt:\n{log[-3000:]}'
    end = log.find('=== end of', start)
    assert end >= 0, f'unfinished {kind} report:\n{log[start:]}'
    return log[start:end]


def symbolize(run):
    return subprocess.run([sys.executable, str(ROOT / 'tools/scripts/symbolize_crash.py'), str(run / 'log.txt'),
                           '--binary', str(run / 'main')], capture_output=True, text=True, check=True)


def check_header(log):
    assert re.search(r'^\[log\] .*binary [0-9a-f]{16,}', log, re.MULTILINE), 'no build line at the top of log.txt'
    assert '[log] renderer: ' in log, 'no renderer line'
    settings = log[log.index('[log] effective settings (at startup):'):log.index('[log] end of settings')]
    for key in ('StallWatchdogSeconds=', 'WalkThroughWalls=', 'PLATINUM_DEBUG_CRASH'):
        assert key in settings, f'{key} missing from the logged settings'


def check_context(text, frame):
    assert f'game: frame {frame},' in text, f'wrong game frame in:\n{text}'
    assert re.search(r'application: \S+\(\+0x[0-9a-f]+\)', text), f'no application in:\n{text}'
    assert 'build: ' in text and 'binary ' in text


def test_segv(parent):
    run = make_run_dir(GAME, parent, 'segv')
    code, log = run_game(run, 'segv:120')
    assert code == -signal.SIGSEGV, f'exit {code}, expected SIGSEGV'
    check_header(log)
    text = report(log, 'CRASH: SIGSEGV')
    assert 'at address 0x10' in text and '(game thread)' in text, text
    check_context(text, 120)
    assert 'SIGSEGV' in (run / 'stderr.txt').read_text(), 'the report did not reach stderr'
    symbolized = symbolize(run).stdout
    assert re.search(r'-> CrashContext_DebugCrash at src/port/debug/crash_context\.c:\d+', symbolized), symbolized
    assert re.search(r'-> NitroMain at src/main\.c:\d+', symbolized), 'the stack does not reach NitroMain'
    assert re.search(r'application: .*-> \w+', symbolized), 'the application was not symbolized'

    # A second start keeps the first log
    run_game(run, 'segv:30')
    assert 'CRASH: SIGSEGV' in (run / 'log.prev.txt').read_text(), 'log.prev.txt does not hold the previous log'
    print('segv: report, context, stderr copy, symbolizer and log.prev.txt passed')


def test_null_call(parent):
    run = make_run_dir(GAME, parent, 'nullcall')
    code, log = run_game(run, 'nullcall:90')
    assert code == -signal.SIGSEGV, f'exit {code}'
    text = report(log, 'CRASH: SIGSEGV')
    assert 'called through a bad pointer from:' in text, text
    assert '-> CrashContext_DebugCrash' in symbolize(run).stdout
    print('nullcall: caller found through the null call')


def test_abort(parent):
    run = make_run_dir(GAME, parent, 'abort')
    code, log = run_game(run, 'abort:90')
    assert code == -signal.SIGABRT, f'exit {code}'
    text = report(log, 'CRASH: SIGABRT')
    check_context(text, 90)
    assert 'PLATINUM_DEBUG_CRASH: triggering at frame 90' in log, 'stderr before the crash is missing from the log'
    assert re.search(r'-> CrashContext_DebugCrash', symbolize(run).stdout)
    print('abort: report and the output before it passed')


def test_stall(parent):
    run = make_run_dir(GAME, parent, 'stall')
    set_key(run / 'sim_config.ini', 'Diagnostics', 'StallWatchdogSeconds', '2')
    code, log = run_game(run, 'stall:90', timeout=25)
    assert code == -signal.SIGKILL, f'the game exited by itself ({code})'
    text = report(log, 'STALL: no frame for 2 s')
    assert 'game thread stack' in text, text
    check_context(text, 90)
    assert re.search(r'=== stall over: frames resumed after about \d+ s', log), 'no "stall over" line'
    assert '-> CrashContext_DebugCrash' in symbolize(run).stdout, 'the stall stack does not show the stalling function'
    print('stall: report with the game thread stack, and recovery, passed')


def test_settings_change(parent):
    run = make_run_dir(GAME, parent, 'settings')

    def change(run):
        deadline = time.time() + 30
        while time.time() < deadline and '[log] renderer' not in (
                (run / 'log.txt').read_text(errors='replace') if (run / 'log.txt').exists() else ''):
            time.sleep(0.2)
        time.sleep(1)
        set_key(run / 'sim_config_prj.ini', 'Developer', 'Autosave', 'false')

    code, log = run_game(run, 'segv:900', during=change)
    assert code == -signal.SIGSEGV, f'exit {code}'
    assert log.count('[log] effective settings (after a settings change):') == 1, \
        'the settings were not logged once after the change'
    assert '[log] effective settings (at startup):' in log
    print('settings: logged again once after a change')


def test_disabled(parent):
    run = make_run_dir(GAME, parent, 'disabled')
    for key in ('WriteLogFile', 'CrashHandler'):
        set_key(run / 'sim_config.ini', 'Diagnostics', key, 'false')
    code, log = run_game(run, 'segv:60')
    assert code == -signal.SIGSEGV, f'exit {code}'
    assert not (run / 'log.txt').exists(), 'log.txt written with WriteLogFile=false'
    assert '=== CRASH' not in (run / 'stderr.txt').read_text(), 'crash report with CrashHandler=false'
    print('disabled: no log and no report with both settings off')


def main():
    global GAME
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--keep', action='store_true', help='keep the run dirs')
    args = parser.parse_args()
    GAME = args.build.resolve() / 'pokeplatinum'
    if not (GAME / 'main').exists() or not (GAME / 'data').exists():
        sys.exit(f'{GAME} has no game with extracted assets; start the game there once first')
    parent = Path(tempfile.mkdtemp(prefix='crash-report-'))
    try:
        for test in (test_segv, test_null_call, test_abort, test_stall, test_settings_change, test_disabled):
            test(parent)
    finally:
        if args.keep:
            print(f'run dirs kept in {parent}')
        else:
            shutil.rmtree(parent)
    print('Crash reports: crash, null call, abort, stall, settings and disabled cases passed.')


if __name__ == '__main__':
    main()
