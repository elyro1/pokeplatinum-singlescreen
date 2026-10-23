#!/usr/bin/env python3
"""Focused checks for battle choices, navigation, and save isolation."""
import tempfile
from pathlib import Path
import unittest

from first_gym import Controller, DOWN, LEFT, RIGHT, StoryFailure, load_type_chart, prepare_runtime


def battle():
    return dict(attacker=[50, 150, 100, 100, 14, 14],
                defender=[1, 200, 100, 100, 17, 17], invalid=0,
                moves=[[94, 10, 90, 14, 1, 100, 0],
                       [396, 20, 90, 1, 1, 0, 0],
                       [85, 15, 95, 13, 1, 100, 0],
                       [53, 15, 95, 10, 1, 100, 0]])


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.bot = Controller.__new__(Controller)
        self.bot.type_chart = load_type_chart()
        self.bot.blocked = set()
        self.bot.tiles = {}
        self.bot.dynamic_layouts = {}
        self.bot.heights = {}

    def test_bridge_height_change_discards_failed_edges_from_other_level(self):
        self.bot.observe(dict(field=dict(map=1, matrix=0, height=0, moving=0, busy=0)))
        self.bot.blocked = {(0, (1, 1), (2, 1)), (5, (1, 1), (2, 1))}
        self.bot.observe(dict(field=dict(map=1, matrix=0, height=64*4096, moving=0, busy=0)))
        self.assertEqual(self.bot.blocked, {(5, (1, 1), (2, 1))})

    def test_small_bridge_surface_offset_preserves_failed_railing_edges(self):
        self.bot.observe(dict(field=dict(map=1, matrix=0, height=262144, moving=0, busy=0)))
        edge = (0, (615, 590), (616, 590))
        self.bot.blocked.add(edge)
        self.bot.observe(dict(field=dict(map=1, matrix=0, height=263808, moving=0, busy=0)))
        self.assertIn(edge, self.bot.blocked)

    def test_dark_immunity_and_super_effective_coverage(self):
        self.assertEqual(self.bot.best_move(battle()), 1)

    def test_ground_immunity_and_dual_type_effectiveness(self):
        state = battle()
        state['defender'][4:] = [4, 5]  # Ground/Rock: Fighting 2x; Electric 0x.
        self.assertEqual(self.bot.best_move(state), 1)

    def test_exhausted_and_disabled_moves_are_excluded(self):
        state = battle()
        state['moves'][1][1] = 0
        state['invalid'] = 1 << 2
        self.assertEqual(self.bot.best_move(state), 3)

    def test_engine_reported_immunity_is_respected(self):
        state = battle()
        state['moves'][1][6] = 1
        self.assertNotEqual(self.bot.best_move(state), 1)

    def test_reliable_knockout_and_pp_conservation(self):
        state = battle()
        state['defender'] = [1, 1, 100, 100, 0, 0]
        self.assertEqual(self.bot.best_move(state), 1)
        state['moves'][1][5] = 50
        self.assertNotEqual(self.bot.best_move(state), 1)

    def test_no_legal_move_fails_instead_of_selecting_invalid_slot(self):
        state = battle()
        state['invalid'] = 15
        with self.assertRaises(StoryFailure):
            self.bot.best_move(state)

    def test_route_detours_around_collision_and_npcs(self):
        for x in range(5):
            for z in range(3):
                self.bot.tiles[1, x, z] = 0
        self.bot.tiles[1, 1, 0] = 0x8000
        field = dict(map=1, x=0, z=0, objects=[[255, 0, 0, 0], [0, 0, 1, 1]])
        # Both immediate exits are blocked until the NPC moves.
        with self.assertRaises(StoryFailure):
            self.bot.path_step(field, (4, 0))
        field['objects'] = [[255, 0, 0, 0]]
        self.assertEqual(self.bot.path_step(field, (4, 0)), DOWN)

    def test_discovered_directional_collision_is_not_retried(self):
        for x in range(3):
            for z in range(3):
                self.bot.tiles[1, x, z] = 0
        field = dict(map=1, x=0, z=0, objects=[])
        self.assertEqual(self.bot.path_step(field, (2, 0)), RIGHT)
        self.bot.blocked.add((1, (0, 0), (1, 0)))
        self.assertEqual(self.bot.path_step(field, (2, 0)), DOWN)

    def test_clear_ground_is_preferred_to_short_grass_cut(self):
        for x in range(3):
            for z in range(2):
                self.bot.tiles[1, x, z] = 0
        self.bot.tiles[1, 1, 0] = 2  # Tall grass, still normally passable.
        self.assertEqual(self.bot.path_step(dict(map=1, x=0, z=0, objects=[]), (2, 0)), DOWN)

    def test_unexplored_frontier_is_preferred_to_known_dead_end(self):
        for x in range(1, 6):
            for z in range(1, 6):
                self.bot.tiles[1, x, z] = 0x8000 if x == 5 or z in (1, 5) else 0
        # The goal is beyond the east wall. Explore west to find a way around,
        # rather than oscillating between the closest known floor tiles.
        self.assertEqual(self.bot.path_step(dict(map=1, x=4, z=3, objects=[]), (10, 3)), LEFT)

    def test_adjacent_outdoor_headers_share_their_matrix_cache(self):
        for x in range(3):
            self.bot.tiles[0, x, 0] = 0
        self.assertEqual(self.bot.path_step(dict(map=342, matrix=0, x=0, z=0, objects=[]), (2, 0)), RIGHT)

    def test_door_approach_replans_after_crossing_an_outdoor_header(self):
        self.bot.maps = {'MAP_HEADER_ROUTE_215_GATE_TO_VEILSTONE_CITY': 999}
        self.bot.names = {1: 'ROUTE_215'}
        self.bot.s = dict(frame=1, field=dict(map=1, matrix=0, x=576, z=603, busy=0))
        calls = []
        def walk(x, z, label):
            calls.append((x, z))
            self.bot.s['frame'] += 1
            self.bot.s['field'].update(map=2 if len(calls) == 1 else 999, x=575, z=598)
            yield from ()
        self.bot.walk = walk
        with self.assertRaises(StopIteration):
            next(self.bot.warp('ROUTE_215_GATE_TO_VEILSTONE_CITY'))
        self.assertEqual(calls, [(671, 598), (671, 598)])

    def test_unloaded_tiles_do_not_erase_observed_collision(self):
        self.bot.tiles[0, 1, 0] = 0x8000
        self.bot.observe(dict(field=dict(map=342, matrix=0, x=0, z=0, grid='ffff' * 289)))
        self.assertEqual(self.bot.tiles[0, 1, 0], 0x8000)

    def test_direction_is_not_replanned_during_a_step(self):
        self.bot.start_at = 'parcel'
        self.bot.s = dict(frame=1, field=dict(map=1, x=0, z=0, busy=0, moving=1, objects=[]))
        from first_gym import B
        self.assertEqual(next(self.bot.walk(2, 0)), (B, -1, -1, 0, 0))

    def test_approach_tracks_the_current_position_of_a_wandering_npc(self):
        self.bot.start_at = 'parcel'
        self.bot.s = dict(frame=1, field=dict(map=1, x=0, z=0, busy=0, moving=0,
                                            objects=[[20, 1, 0, 14]]))
        # Its old position was five tiles east; it is now beside the player.
        action = next(self.bot.walk(5, 0, adjacent=True, follow=(20, 14)))
        self.assertEqual(action, (0, -1, -1, 0, 0))

    def test_steering_is_held_through_turn_then_released_during_step(self):
        from first_gym import B
        self.bot.start_at = 'bicycle'
        for x in range(3):
            self.bot.tiles[1, x, 0] = 0
        state = dict(frame=1, field=dict(map=1, x=0, z=0, busy=0, moving=0, objects=[]))
        self.bot.s = state
        walk = self.bot.walk(2, 0)
        self.assertEqual(next(walk), (RIGHT | B, -1, -1, 0, 0))
        state['field']['moving'] = 2
        self.assertEqual(walk.send(state), (RIGHT | B, -1, -1, 0, 0))
        state['field']['moving'] = 1
        self.assertEqual(walk.send(state), (B, -1, -1, 0, 0))

    def test_story_completion_stops_navigation_after_scripted_repositioning(self):
        self.bot.s = dict(frame=1, field=dict(map=1, x=114, z=854, busy=1, moving=0))
        walk = self.bot.walk(112, 857, until=lambda: True)
        with self.assertRaises(StopIteration):
            next(walk)

    def test_dialogue_deadline_tracks_story_progress(self):
        self.bot.start_at = 'oreburgh'
        self.bot.s = dict(frame=0, text_id=0)
        wait = self.bot.wait(lambda: self.bot.s['frame'] >= 30,
                             'battle and badge', limit=10, progress=True)
        next(wait)
        for frame in range(1, 31):
            state = dict(frame=frame, text_id=frame // 8)
            if frame == 30:
                with self.assertRaises(StopIteration):
                    wait.send(state)
            else:
                wait.send(state)

    def test_dialogue_stall_and_progress_loop_remain_bounded(self):
        self.bot.start_at = 'oreburgh'
        for advancing, deadline in ((False, 10), (True, 50)):
            self.bot.s = dict(frame=0, text_id=0)
            wait = self.bot.wait(lambda: False, 'stuck dialogue', limit=10, progress=True)
            next(wait)
            for frame in range(1, deadline):
                wait.send(dict(frame=frame, text_id=frame if advancing else 0))
            with self.assertRaises(StoryFailure):
                wait.send(dict(frame=deadline, text_id=deadline if advancing else 0))

    def test_double_battle_targets_the_observed_opponent(self):
        from first_gym import A, UP
        self.bot.s = dict(frame=8, battle=dict(menu=12, target_slot=3, cursor=[1, 0, 1]))
        self.assertEqual(self.bot.auto(), (LEFT, None))
        self.bot.s['battle']['target_slot'] = 1
        self.bot.s['battle']['cursor'] = [1, 1, 1]
        self.assertEqual(self.bot.auto(), (UP, None))
        self.bot.s['battle']['cursor'] = [1, 0, 1]
        self.assertEqual(self.bot.auto(), (A, None))

    def test_second_gym_requires_both_badges_after_reload(self):
        from second_gym import Controller as SecondGymController
        bot = SecondGymController.__new__(SecondGymController)
        bot.s = dict(badge=1, forest_badge=0, roark_returned=1,
                     mons=[[151, 100], [400, 20], [357, 20]])
        with self.assertRaises(StoryFailure):
            bot.verify_completion()
        bot.s['forest_badge'] = 1
        bot.s['mons'][1][1] = 23  # Normal double-battle EXP is preserved.
        bot.verify_completion()

    def test_second_scenario_preserves_first_gym_route_prefix(self):
        from second_gym import Controller as SecondGymController
        first = Controller.__new__(Controller).chapters()
        second = SecondGymController.__new__(SecondGymController).chapters()
        self.assertEqual([name for name, _ in second[:len(first)]],
                         [name for name, _ in first])
        self.assertEqual(second[-1][0], 'forest-badge')

    def test_fixture_preserves_story_gifts(self):
        self.bot.s = dict(mons=[[151, 100], [400, 23], [357, 20], [175, 1]])
        self.assertTrue(self.bot.has_fixture())
        self.bot.s['mons'][0][0] = 175
        self.assertFalse(self.bot.has_fixture())

    def test_checkpoint_waits_for_arrival_step_before_opening_menu(self):
        from first_gym import X
        self.bot.start_at = 'ravaged'
        self.bot.s = dict(frame=1, field=dict(busy=0, moving=1))
        save = self.bot.save('floaroma')
        self.assertEqual(next(save), (0, -1, -1, 0, 0))
        self.assertEqual(save.send(dict(frame=2, field=dict(busy=0, moving=0))),
                         (X, -1, -1, 0, 0))

    def test_save_does_not_confirm_before_pause_menu_is_observed(self):
        from first_gym import X
        self.bot.start_at = 'mars'
        self.bot.s = dict(frame=1, text_id=7, field=dict(busy=0, moving=0))
        save = self.bot.save('mars')
        self.assertEqual(next(save), (X, -1, -1, 0, 0))
        self.assertEqual(save.send(dict(frame=2, text_id=7, field=dict(busy=1, moving=0))),
                         (0, -1, -1, 0, 0))

    def test_save_finishes_deferred_scene_and_retries_menu(self):
        from first_gym import A, X
        self.bot.start_at = 'windworks'
        self.bot.s = dict(frame=1, field=dict(busy=0, moving=0))
        save = self.bot.save('windworks')
        self.assertEqual(next(save), (X, -1, -1, 0, 0))
        # The scene takes priority over X; keep advancing it.
        self.assertEqual(save.send(dict(frame=2, text_id=1, field=dict(busy=1, moving=0))),
                         (A, -1, -1, 0, 0))
        # Ordinary control returned, but no menu opened: retry X.
        self.assertEqual(save.send(dict(frame=8, text_id=2, field=dict(busy=0, moving=0))),
                         (X, -1, -1, 0, 0))

    def test_navigation_holds_run_without_adding_cancel_to_dialogue(self):
        from first_gym import A, B
        self.bot.start_at = 'forest-badge'
        for x in range(3):
            self.bot.tiles[1, x, 0] = 0
        self.bot.s = dict(frame=8, field=dict(map=1, x=0, z=0, busy=0, moving=0, objects=[]))
        self.assertEqual(next(self.bot.walk(2, 0)), (RIGHT | B, -1, -1, 0, 0))
        self.assertEqual(self.bot.auto(), (A, None))

    def test_third_gym_extends_second_route_and_checks_relic_badge(self):
        from second_gym import Controller as SecondGymController
        from third_gym import Controller as ThirdGymController
        bot = ThirdGymController.__new__(ThirdGymController)
        prefix = SecondGymController.__new__(SecondGymController).chapters()
        self.assertEqual([n for n, _ in bot.chapters()[:len(prefix)]], [n for n, _ in prefix])
        bot.s = dict(badge=1, forest_badge=1, relic_badge=0, bicycle=1, roark_returned=1,
                     mons=[[151, 100], [400, 20], [357, 20]])
        with self.assertRaises(StoryFailure):
            bot.verify_completion()
        bot.s['relic_badge'] = 1
        bot.verify_completion()

    def test_hearthome_clues_select_every_randomized_door(self):
        from third_gym import Controller as ThirdGymController
        for room, base, count in ((1, 0, 3), (2, 3, 5)):
            for index in range(count):
                self.assertEqual(ThirdGymController.clue_door_index([5, 6, base + index], room), index)
            with self.assertRaises(StoryFailure):
                ThirdGymController.clue_door_index([5, 6, 8], room)

    def test_bag_opening_waits_for_choices_before_confirming(self):
        from first_gym import X
        self.bot.start_at = 'bicycle'
        self.bot.s = dict(frame=1, text_id=3, field=dict(busy=0, moving=0))
        menu = self.bot.open_pause_menu('BAG')
        self.assertEqual(next(menu), (X, -1, -1, 0, 0))
        self.assertEqual(menu.send(dict(frame=2, text_id=3, field=dict(busy=1, moving=0))),
                         (0, -1, -1, 0, 0))
        with self.assertRaises(StopIteration):
            menu.send(dict(frame=3, text_id=3, field=dict(busy=1, moving=0),
                           menu=dict(cursor=0, choices=['POKEDEX', 'BAG', 'SAVE'])))
        self.assertEqual(self.bot.menu_target, 'BAG')

    def test_clock_rotation_discards_remote_stale_tiles_and_failed_edges(self):
        self.bot.dynamic_layouts[113] = 3
        self.bot.tiles[113, 11, 19] = 0x8000  # Beyond the new local window.
        self.bot.tiles[0, 300, 530] = 0
        self.bot.blocked.add((113, (11, 18), (11, 19)))
        self.bot.observe(dict(eterna_clock=4, field=dict(map=1, matrix=113,
                              x=11, z=4, grid='0000' * 289)))
        self.assertNotIn((113, 11, 19), self.bot.tiles)
        self.assertEqual(self.bot.tiles[113, 11, 5], 0)
        self.assertEqual(self.bot.tiles[0, 300, 530], 0)
        self.assertFalse(self.bot.blocked)
        self.bot.tiles[113, 11, 19] = 0
        self.bot.observe(dict(eterna_clock=4, field=dict(map=1, matrix=113, x=11, z=5)))
        self.assertIn((113, 11, 19), self.bot.tiles)

    def test_bicycle_pocket_scan_waits_for_switch_animation(self):
        from third_gym import Controller as ThirdGymController
        bot = ThirdGymController.__new__(ThirdGymController)
        bot.s = dict(frame=8, app_state=2, menu=dict(choices=['Honey']))
        self.assertEqual(bot.bicycle_pocket_controls(), (0, None))
        bot.s['app_state'] = 1
        self.assertEqual(bot.bicycle_pocket_controls(), (LEFT, None))
        bot.s['menu'] = None
        self.assertEqual(bot.bicycle_pocket_controls(), (0, None))

    def test_runtime_copies_assets_and_excludes_all_player_saves(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build, output = root / 'build', root / 'output'
            source = build / 'pokeplatinum'
            source.mkdir(parents=True)
            output.mkdir()
            for name in ('save.bin', 'save1.bin', 'Startsave2.bin', 'asave2.bin',
                         'sim_config.ini', 'header.bin'):
                (source / name).write_text('original')
            (build / 'main').write_text('test binary')
            (source / 'assets').mkdir()
            (source / 'assets/save.bin').write_text('legitimate asset')
            runtime = prepare_runtime(build, output)
            self.assertEqual((runtime / 'assets/save.bin').read_text(), 'legitimate asset')
            self.assertEqual((runtime / 'header.bin').read_text(), 'original')
            self.assertFalse(list(runtime.glob('*save*.bin')))
            self.assertEqual((source / 'save1.bin').read_text(), 'original')
            (runtime / 'header.bin').write_text('changed')
            self.assertEqual((source / 'header.bin').read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
