#!/usr/bin/env python3
"""Tests for scene.py. Pure unit tests by default; --live also runs short scenes against --build."""
import argparse
import os
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scene  # noqa: E402

BUILD = None


def args_for(*pairs):
    a = argparse.Namespace(no_boot=False, until=None, user_ini=False, wide=False, set=[], visible=False, map=None,
                           wild=None, trainer=None, clock=None, asan=False, env=[])
    for k, v in pairs:
        setattr(a, k, v)
    return a


def field_state(**field):
    f = dict(map=3, x=174, z=752, busy=0, moving=0)
    f.update(field)
    return dict(frame=500, app_name='FieldMap_Main', menu=None, battle=None, text='', field=f)


class ConditionTests(unittest.TestCase):
    def test_frames_and_milestones(self):
        self.assertEqual(scene.Condition('1234').frame, 1234)
        self.assertEqual(scene.Condition('field').frame, scene.MILESTONES['field'])
        self.assertEqual(scene.Condition('field+90').frame, scene.MILESTONES['field'] + 90)
        self.assertEqual(scene.Condition('battle_menu-30').frame, scene.MILESTONES['battle_menu'] - 30)
        self.assertIsNone(scene.Condition('menu').frame)
        self.assertIsNone(scene.Condition('map=3').frame)
        for bad in ('feild', 'field+x', ''):
            with self.assertRaises(scene.SceneError):
                scene.Condition(bad)

    def test_field_waits_for_the_warp_target(self):
        cond = scene.Condition('field')
        self.assertTrue(cond.test(field_state(), {'map': None}))
        self.assertTrue(cond.test(field_state(), {'map': 3}))
        self.assertFalse(cond.test(field_state(map=411), {'map': 3}))
        self.assertFalse(cond.test(field_state(busy=1), {'map': None}))
        self.assertFalse(cond.test(dict(field_state(), menu={'choices': ['BAG']}), {'map': None}))

    def test_battle_menu(self):
        cond = scene.Condition('battle_menu')
        state = dict(frame=900, app_name='ApplicationMain_Battle', battle={'menu': 1})
        self.assertTrue(cond.test(state, {}))
        state['battle'] = {'menu': 11}  # Move selection
        self.assertFalse(cond.test(state, {}))

    def test_state_comparisons(self):
        s = field_state()
        s['menu'] = {'choices': ['POK DEX', 'BAG']}
        s['text'] = 'A wild PIKACHU appeared!'
        scene.MAP_HEADERS['JUBILIFE_CITY'] = 3
        for text, want in (('map=3', True), ('map=jubilife_city', True), ('map!=3', False), ('tile=174,752', True),
                           ('tile=1,1', False), ('field.x=174', True), ('menu~bag', True), ('menu~save', False),
                           ('text~pikachu', True), ('app~FieldMap', True), ('field.busy=0', True)):
            self.assertEqual(scene.Condition(text).test(s, {}), want, text)
        with self.assertRaises(scene.SceneError):
            scene.Condition('map=NOWHERE_TOWN')


class StepTests(unittest.TestCase):
    def test_names(self):
        self.assertEqual(scene.split_name('field+60'), ('field_60', 'field+60'))
        self.assertEqual(scene.split_name('bag=1500'), ('bag', '1500'))
        self.assertEqual(scene.split_name('map=3'), ('map_3', 'map=3'))  # A condition, not a shot named "map"
        self.assertEqual(scene.split_name('city=map=3'), ('city', 'map=3'))
        self.assertEqual(scene.split_name('moves=battle.menu=11'), ('moves', 'battle.menu=11'))

    def test_keys(self):
        step = scene.parse_step('key', '1500:a')
        self.assertEqual((step.cond.frame, step.key, step.hold), (1500, 'a', 4))
        step = scene.parse_step('key', 'field:run:200')
        self.assertEqual((step.cond.label, step.key, step.hold), ('field', 'run', 200))
        step = scene.parse_step('key', 'tile=3,4:up')
        self.assertEqual((step.cond.label, step.key), ('tile=3,4', 'up'))

    def test_steps_keep_command_line_order(self):
        args = scene.build_parser().parse_args(['--shot', 'a=field', '--key', 'field:x', '--dump', 'b=menu',
                                                '--expect', 'map=3'])
        self.assertEqual([k for k, _ in args.steps], ['shot', 'key', 'dump', 'expect'])

    def test_set_names_the_section(self):
        self.assertEqual(scene.parse_set('prj.General.BW2BattleVisuals=true'),
                         ('sim_config_prj.ini', 'General', 'BW2BattleVisuals', 'true'))
        with self.assertRaises(scene.SceneError):
            scene.parse_set('BW2BattleVisuals=true')


class FrameModeTests(unittest.TestCase):
    def test_autopilot_is_sorted_and_quits_last(self):
        steps = [scene.parse_step('shot', 'late=2000'), scene.parse_step('shot', 'early=100'),
                 scene.parse_step('key', '1500:a')]
        lines, last = scene.autopilot_lines(args_for(), steps)
        frames = [int(l.split()[0]) for l in lines]
        self.assertEqual(frames, sorted(frames))
        self.assertTrue(lines[-1].endswith('quit'))
        self.assertEqual(last, 2002)
        self.assertIn('100 shot early.ppm', lines)
        self.assertIn('1500 key z 4', lines)

    def test_bridge_only_steps_are_rejected(self):
        for kind, text in (('shot', 'map=3'), ('dump', 'field'), ('expect', 'tile=1,1')):
            with self.assertRaises(scene.SceneError):
                scene.autopilot_lines(args_for(), [scene.parse_step(kind, text)])


class SetupTests(unittest.TestCase):
    def test_safety_wins_over_set(self):
        a = args_for(('set', [('sim_config_prj.ini', 'Cheats', 'WalkThroughWalls', 'true'),
                              ('sim_config.ini', 'General', 'CapFrameRate', 'false')]), ('wide', True))
        inis = scene.build_ini(a, Path('/nonexistent'))
        self.assertEqual(inis['sim_config_prj.ini']['Cheats']['WalkThroughWalls'], 'false')
        self.assertEqual(inis['sim_config.ini']['General']['CapFrameRate'], 'true')
        self.assertEqual(inis['sim_config.ini']['General']['WindowWidth'], '2160')

    def test_env_drops_inherited_debug_switches(self):
        os.environ['PLATINUM_DEBUG_WILD'] = '1:1'
        try:
            env = scene.game_env(args_for(('clock', '7:05')))
        finally:
            del os.environ['PLATINUM_DEBUG_WILD']
        self.assertNotIn('PLATINUM_DEBUG_WILD', env)
        self.assertEqual(env['PLATINUM_FIXED_CLOCK'], '2026-06-01T07:05')

    def test_private_files_are_not_linked(self):
        for name in ('main', 'save1.bin', 'aasave1.bin', 'save2.bak', 'sim_config.ini', 'imgui.ini', 'log.txt',
                     'log.prev.txt', 'DbgLog.txt', 'autosave', 'story-test.ppm'):
            self.assertTrue(scene.PRIVATE.match(name), name)
        for name in ('arc', 'data', 'bw2', 'field_lights.txt', 'firmware.bin', 'header.bin'):
            self.assertFalse(scene.PRIVATE.match(name), name)

    def test_print_config(self):
        out = ('; sim_config.ini\n[General]\nCapFrameRate=true                 ; sim_config.ini:3\n\n'
               '; gameplay_config.ini\n[Gameplay]\nshinyRolls=16   ; default (no gameplay_config.ini)\n\n'
               '; Environment variables (docs/environment_variables.md)\nPLATINUM_DEBUG_MAP=3:174:752\n')
        settings = scene.parse_print_config(out)
        self.assertEqual(settings['sim_config.ini']['General']['CapFrameRate'], 'true')
        self.assertEqual(settings['gameplay_config.ini']['Gameplay']['shinyRolls'], '16')
        self.assertEqual(settings['environment'], {'PLATINUM_DEBUG_MAP': '3:174:752'})

    def test_heap_names(self):
        names = scene.load_heap_names()
        self.assertEqual(names[0], 'SYSTEM')
        self.assertEqual(names[3], 'APPLICATION')

    def test_ppm_to_png(self):
        with tempfile.TemporaryDirectory() as d:
            src, dst = Path(d) / 'a.ppm', Path(d) / 'a.png'
            src.write_bytes(b'P6\n2 1\n255\n' + bytes([255, 0, 0, 0, 0, 255]))
            scene.ppm_to_png(src, dst)
            data = dst.read_bytes()
            self.assertTrue(data.startswith(b'\x89PNG'))
            idat = data[data.index(b'IDAT') + 4:]
            self.assertEqual(zlib.decompressobj().decompress(idat)[:7], bytes([0, 255, 0, 0, 0, 0, 255]))


class LiveTests(unittest.TestCase):
    def run_scene(self, *argv):
        if BUILD is None:
            self.skipTest('pass --live to run the game')
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        out = Path(d.name) / 'out'
        proc = subprocess.run([sys.executable, str(Path(scene.__file__)), '--build', str(BUILD), '--clock', '12:00',
                               '--out', str(out), *argv], capture_output=True, text=True, timeout=300)
        return proc, out

    def test_field_shot_in_frame_mode(self):
        proc, out = self.run_scene('--frames', '--shot', 'field')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertTrue((out / 'field.png').stat().st_size > 1000)

    def test_bridge_warp_menu_and_dump(self):
        if BUILD is not None and scene.app_names(BUILD / 'main') is None:
            self.skipTest('build has no test bridge (-Dautoplay_tests=true)')
        proc, out = self.run_scene('--map', '3:174:752', '--shot', 'field', '--expect', 'tile=174,752',
                                   '--key', 'field:x', '--dump', 'xmenu=menu~bag')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertTrue((out / 'field.png').stat().st_size > 1000)
        import json
        doc = json.loads((out / 'xmenu.json').read_text())
        self.assertEqual(doc['state']['field']['map_name'], 'JUBILIFE_CITY')
        self.assertTrue(any(h['name'] == 'APPLICATION' for h in doc['state']['heaps']))
        self.assertEqual(doc['settings']['sim_config.ini']['General']['CapFrameRate'], 'true')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[2] / 'build_linux')
    p.add_argument('--live', action='store_true')
    opts, rest = p.parse_known_args()
    if opts.live:
        BUILD = opts.build.resolve()
    unittest.main(argv=[sys.argv[0]] + rest)
