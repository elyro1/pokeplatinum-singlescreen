#!/usr/bin/env python3
"""Play a fresh game through the Coal Badge using the opt-in Linux test bridge.

The runner uses only the Python standard library. Runtime assets are copied;
player saves are never used, and settings are imported only when requested.
See docs/autoplay_testing.md.
"""
import argparse
import heapq
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
A, B, START, RIGHT, LEFT, UP, DOWN, X = 1, 2, 8, 16, 32, 64, 128, 1024
# Spawned followers and wild sprites use local IDs from 0xFF00 upward.
POKEMON_ACTOR_LOCAL_ID = 0xFF00
POKEMON_ACTOR_STEP_COST = 8
DIRECTIONS = [(0, -1, UP), (0, 1, DOWN), (-1, 0, LEFT), (1, 0, RIGHT)]


class StoryFailure(RuntimeError):
    pass


def enum_values(path):
    return {name: int(value) for name, value in
            re.findall(r'^\s*(\w+)\s*=\s*(\d+),', path.read_text(), re.M)}


def load_type_chart():
    types = {name: i for i, name in enumerate((ROOT / 'generated/pokemon_types.txt').read_text().splitlines())}
    source = (ROOT / 'src/battle/battle_lib.c').read_text().split('static const u8 sTypeMatchupMultipliers[][3] = {', 1)[1].split('};', 1)[0]
    factors = {'NOT_VERY_EFF': .5, 'SUPER_EFF': 2, 'IMMUNE': 0}
    return {(types[a], types[d]): factors[factor] for a, d, factor in
            re.findall(r'{ (TYPE_\w+), (TYPE_\w+), TYPE_MULTI_(\w+) }', source)}


class Controller:
    def __init__(self, build, start_at=None):
        self.maps = enum_values(build / 'generated/map_headers.h')
        self.names = {v: k.removeprefix('MAP_HEADER_') for k, v in self.maps.items()}
        self.vars = enum_values(build / 'generated/vars_flags.h')
        self.s = {}
        self.tiles = {}
        self.blocked = set()
        self.dynamic_layouts = {}
        self.heights = {}
        self.objective = 'new-game setup'
        self.start_at = start_at
        self.checkpoint = None
        self.menu_target = None
        self.reloading = False
        self.type_chart = load_type_chart()

    def has_fixture(self):
        mons = self.s.get('mons', [])
        # Normal story gifts (such as Cynthia's Egg) may join after setup.
        return (len(mons) >= 3 and [mon[0] for mon in mons[:3]] == [151, 400, 357]
                and mons[0][1] == 100 and all(mon[1] >= 20 for mon in mons[1:3]))

    def map_id(self, name):
        return self.maps['MAP_HEADER_' + name]

    def var(self, name):
        return self.s.get('vars', [])[self.vars[name] - self.vars['VARS_START']]

    def observe(self, state):
        self.s = state
        if state.get('losses'):
            raise StoryFailure('battle loss or draw')
        f = state.get('field')
        if f and 'height' in f and not f.get('moving') and not f.get('busy'):
            matrix = f.get('matrix', f['map'])
            # World tiles are 16 units, with 12 fractional bits. Bridge models
            # have small surface offsets that must not count as another floor.
            layer = round(f['height'] / (16 * 4096))
            previous = self.heights.get(matrix)
            if previous is not None and previous != layer:
                # A railing blocks movement on a bridge but not below it.
                # Failed edges learned on another height must be tried again.
                self.blocked = {edge for edge in self.blocked if edge[0] != matrix}
            self.heights[matrix] = layer
        if f and 'eterna_clock' in state:
            matrix = f.get('matrix', f['map'])
            if self.dynamic_layouts.get(matrix) != state['eterna_clock']:
                # A rotation changes tiles outside the current observation window.
                self.tiles = {key: value for key, value in self.tiles.items() if key[0] != matrix}
                self.blocked = {edge for edge in self.blocked if edge[0] != matrix}
                self.dynamic_layouts[matrix] = state['eterna_clock']
        if f and 'grid' in f:
            for i in range(17 * 17):
                attr = int(f['grid'][i * 4:i * 4 + 4], 16)
                if attr != 0xffff:
                    self.tiles[f.get('matrix', f['map']), f['x'] + i % 17 - 8, f['z'] + i // 17 - 8] = attr

    def tick(self, keys=0, touch=None, fixture=0, shot=0):
        if not self.start_at and not self.reloading and self.s.get('party') == 1 and not self.s.get('fixture') and self.s.get('field') and not self.s.get('battle'):
            fixture = 1
        state = yield (keys, *(touch or (-1, -1)), fixture, shot)
        self.observe(state)

    def pulse(self, key=A):
        return (key if self.s['frame'] % 8 != 7 else 0) if key == A else (key if self.s['frame'] % 8 == 0 else 0)

    def auto(self):
        if self.s.get('app_name') == 'JournalController_Main':
            return self.pulse(B), None
        menu = self.s.get('menu')
        if menu:
            labels = menu['choices']
            target = 0
            if self.menu_target:
                target = next((i for i, label in enumerate(labels) if label.upper() == self.menu_target), 0)
            # Pick a provided rival name, rather than opening another keyboard.
            if labels and ('NEW NAME' in labels[0].upper()):
                target = 1
            # Refuse nicknames; this test uses fixed species names.
            if 'nickname' in self.s.get('text', '').lower() and len(labels) == 2:
                target = 1
            if menu['cursor'] != target:
                return self.pulse(DOWN if menu['cursor'] < target else UP), None
            return self.pulse(), None
        battle = self.s.get('battle')
        if battle:
            # Keep HM coverage and the lead fighter as helpers gain normal EXP.
            if battle['menu'] in (14, 17):
                return self.pulse(B), None
            # Action menus: choose Fight using ordinary touch input. The
            # single-screen port maps these DS coordinates to its visible UI.
            if battle['menu'] in (3, 4, 5, 6):
                return 0, (160, 160) if self.s['frame'] % 8 == 0 else None
            if battle['menu'] == 12 and battle.get('target_slot', -1) >= 0:
                slot = battle['target_slot']
                x, y = [(0, 1), (1, 0), (1, 1), (0, 0)][slot]
                cx, cy, active = battle['cursor']
                if not active:
                    return self.pulse(), None
                key = (RIGHT if cx < x else LEFT) if cx != x else ((DOWN if cy < y else UP) if cy != y else A)
                return self.pulse(key), None
            if battle['menu'] == 11:
                slot = self.best_move(battle)
                return 0, (48 + (slot % 2) * 72, 160 + (slot // 2) * 16) if self.s['frame'] % 8 == 0 else None
            return self.pulse(), None
        return self.pulse(), None

    def best_move(self, battle):
        # Deterministic expected damage; no use of the game RNG.
        scores = []
        level, hp, attack, special, type1, type2 = battle['attacker']
        species, target_hp, defense, sp_defense, target_type1, target_type2 = battle['defender']
        for i, (move, pp, power, kind, category, accuracy, immune) in enumerate(battle['moves']):
            if not move or not pp or battle['invalid'] & (1 << i) or not power or immune:
                scores.append(-1)
                continue
            damage = ((2 * level / 5 + 2) * power *
                      (attack / max(1, defense) if category == 0 else special / max(1, sp_defense)) / 50 + 2)
            damage *= 1.5 if kind in (type1, type2) else 1
            for target_type in {target_type1, target_type2}:
                damage *= self.type_chart.get((kind, target_type), 1)
            # Prefer reliable minimum-roll KOs, then expected damage. Remaining
            # PP breaks ties so one move is not needlessly exhausted.
            ko = damage * .85 >= target_hp
            scores.append((10000 if ko else min(damage, target_hp)) * (accuracy or 100) / 100 + pp / 1000)
        if max(scores) < 0:
            legal = [i for i, m in enumerate(battle['moves']) if m[0] and m[1] and not battle['invalid'] & (1 << i)]
            if not legal:
                raise StoryFailure('move menu has no legal moves')
            return legal[0]
        return max(range(4), key=lambda i: scores[i])

    def wait(self, predicate, label, limit=12000, auto=True, progress=False, controls=None):
        self.objective = label
        start = last_progress = self.s['frame']
        last_event = None
        while not predicate():
            # Battles, badge fanfares, and item gifts share one NPC interaction.
            # Give each observable story event its own stall budget, while a
            # separate hard bound still catches loops that keep printing text.
            field = self.s.get('field', {})
            event = (self.s.get('text_id'), self.s.get('battles'),
                     field.get('map'), field.get('x'), field.get('z'))
            if progress and event != last_event:
                last_progress, last_event = self.s['frame'], event
            if (self.s['frame'] - last_progress >= limit
                    or self.s['frame'] - start >= limit * (5 if progress else 1)):
                raise StoryFailure(f'timeout: {label}')
            keys, touch = controls() if controls else (self.auto() if auto else (0, None))
            yield from self.tick(keys, touch)

    def walk(self, x, z, label=None, limit=20000, adjacent=False, follow=None, until=None):
        self.objective = label or f'walk to {x},{z}'
        start = last_progress = self.s['frame']
        last_event = None
        source_map = self.s['field']['map']
        attempted, stuck, no_path = None, 0, 0
        while True:
            if until and until():
                return
            f = self.s.get('field')
            if f and f['map'] != source_map:
                return
            if f and follow:
                obj = next((o for o in f['objects'] if (o[0], o[3]) == follow), None)
                if obj is None:
                    raise StoryFailure(f'object disappeared while approaching: {self.objective}')
                x, z = obj[1:3]
            if f and not f['busy'] and not f['moving'] and ((abs(f['x']-x) + abs(f['z']-z) == 1) if adjacent else (f['x'], f['z']) == (x, z)):
                yield from self.tick()
                return
            event = (self.s.get('text_id'), self.s.get('battles'),
                     (f['x'], f['z']) if f else None)
            if event != last_event:
                last_progress, last_event = self.s['frame'], event
            if self.s['frame'] - last_progress > limit or self.s['frame'] - start > limit * 5:
                raise StoryFailure(f'timeout: {self.objective}')
            if not f or f['busy'] or self.s.get('battle') or self.s.get('menu'):
                keys, touch = self.auto()
                yield from self.tick(keys, touch)
                continue
            if f['moving']:
                # Keep steering through a turn animation. Cycling Road forces
                # downhill motion if direction is released before the step.
                turn_key = attempted[3] if f['moving'] == 2 and attempted else 0
                yield from self.tick(turn_key | B)
                continue
            try:
                key = self.path_step(f, (x, z))
            except StoryFailure:
                no_path += 1
                if no_path > 240:
                    raise
                yield from self.tick()
                continue
            no_path = 0
            attempt = (f.get('matrix', f['map']), f['x'], f['z'], key)
            stuck = stuck + 1 if attempt == attempted else 0
            attempted = attempt
            if stuck >= 4:
                dx, dz, _ = next(d for d in DIRECTIONS if d[2] == key)
                self.blocked.add((f.get('matrix', f['map']), (f['x'], f['z']), (f['x']+dx, f['z']+dz)))
                stuck = 0
                continue
            yield from self.tick(key | B)

    def path_step(self, f, goal):
        start = f['x'], f['z']
        others = [o for o in f['objects'] if (o[1], o[2]) != start]
        # The player walks through followers and wild sprites. A follower only
        # moves after the player does, so it must not close a narrow corridor.
        actors = {(o[1], o[2]) for o in others if o[0] >= POKEMON_ACTOR_LOCAL_ID}
        obstacles = {(o[1], o[2]) for o in others if o[0] < POKEMON_ACTOR_LOCAL_ID}
        queue = [(0, 0, start, 0)]
        seen = set()
        frontier = None
        while queue:
            _, cost, p, first = heapq.heappop(queue)
            if p in seen:
                continue
            seen.add(p)
            distance = abs(p[0] - goal[0]) + abs(p[1] - goal[1])
            if p == goal or (goal in obstacles and distance == 1):
                return first
            if first and any((f.get('matrix', f['map']), p[0]+dx, p[1]+dz) not in self.tiles
                             for dx, dz, _ in DIRECTIONS):
                score = cost + distance * 1.2
                if frontier is None or score < frontier[0]:
                    frontier = score, first
            for dx, dz, key in DIRECTIONS:
                q = p[0] + dx, p[1] + dz
                attr = self.tiles.get((f.get('matrix', f['map']), *q), 0xffff)
                if q in seen or q in obstacles or (attr & 0x8000 and not (q == goal and attr & 0xff == 0x69)) or (f.get('matrix', f['map']), p, q) in self.blocked:
                    continue
                # Water is inaccessible on the first-gym route.
                if attr & 0xff in (0x10, 0x13, 0x15):
                    continue
                heuristic = abs(q[0] - goal[0]) + abs(q[1] - goal[1])
                step_cost = 5 if attr & 0xff in (2, 3) else 1
                # Prefer a short detour: touching a wild sprite starts a battle.
                step_cost += POKEMON_ACTOR_STEP_COST if q in actors else 0
                heapq.heappush(queue, (cost + step_cost + heuristic, cost + step_cost, q, first or key))
        if frontier:
            return frontier[1]
        raise StoryFailure(f'no path from {start} toward {goal}')

    def warp(self, destination, index=0):
        if self.s.get('field', {}).get('map') == self.map_id(destination):
            return
        f = self.s['field']
        name = self.names[f['map']].lower()
        events = json.loads((ROOT / f'res/field/events/events_{name}.json').read_text())
        warps = [w for w in events['warp_events'] if w['dest_header_id'] == 'MAP_HEADER_' + destination]
        if not warps:
            raise StoryFailure(f'no doorway from {name} to {destination}')
        warp = warps[index]
        self.objective = f'enter {destination}'
        # An overworld path can cross neighboring headers while exploring a
        # detour. Keep approaching the door across the shared map matrix.
        matrix = f.get('matrix', f['map'])
        approach_start = self.s['frame']
        while self.s['field']['map'] != self.map_id(destination):
            f = self.s['field']
            if f.get('matrix', f['map']) != matrix:
                raise StoryFailure(f'unexpected map while approaching {destination}')
            if self.s['frame'] - approach_start > 100000:
                raise StoryFailure(f'timeout approaching {destination}')
            yield from self.walk(warp['x'], warp['z'], self.objective)
            f = self.s['field']
            if (f['x'], f['z']) == (warp['x'], warp['z']):
                break
        start = self.s['frame']
        while self.s.get('field', {}).get('map') != self.map_id(destination):
            if self.s['frame'] - start > 3000:
                raise StoryFailure(f'timeout entering {destination}')
            f = self.s.get('field')
            if f and not f['busy']:
                behavior = self.tiles.get((f.get('matrix', f['map']), warp['x'], warp['z']), 0) & 0xff
                key = {0x5e: RIGHT, 0x5f: LEFT, 0x62: RIGHT, 0x63: LEFT,
                       0x64: UP, 0x65: DOWN, 0x69: UP, 0x6c: RIGHT,
                       0x6d: LEFT, 0x6e: UP, 0x6f: DOWN}.get(behavior, DOWN)
                yield from self.tick(key | B)
            else:
                keys, touch = self.auto()
                yield from self.tick(keys, touch)
        yield from self.wait(lambda: not self.s.get('field', {}).get('busy', True),
                             f'finish entering {destination}')

    def travel(self, x, z, label, arrival_map=None, until=None):
        while True:
            if until and until():
                return
            if arrival_map and self.s.get('field', {}).get('map') == self.map_id(arrival_map):
                return
            yield from self.walk(x, z, label, until=until)
            f = self.s.get('field')
            if f and (f['x'], f['z']) == (x, z):
                return

    def talk(self, script, label):
        f = self.s['field']
        objects = [o for o in f['objects'] if o[3] == script and o[0] != 255]
        if not objects:
            raise StoryFailure(f'missing object for {label}, script {script}')
        obj = min(objects, key=lambda o: abs(o[1]-f['x']) + abs(o[2]-f['z']))
        identity = obj[0], obj[3]
        yield from self.walk(obj[1], obj[2], label, adjacent=True, follow=identity)
        self.objective = label + ': interact'
        start = self.s['frame']
        while not self.s.get('field', {}).get('busy', False):
            if self.s['frame'] - start > 600:
                raise StoryFailure(f'timeout interacting with {label}')
            f = self.s['field']
            obj = next((o for o in f['objects'] if (o[0], o[3]) == identity), None)
            if obj is None:
                raise StoryFailure(f'object disappeared before interaction: {label}')
            dx, dz = obj[1]-f['x'], obj[2]-f['z']
            if abs(dx) + abs(dz) != 1:
                yield from self.walk(obj[1], obj[2], label, adjacent=True, follow=identity)
            elif f['moving']:
                yield from self.tick()
            else:
                direction, key = next((i, key) for i, (sx, sz, key) in enumerate(DIRECTIONS) if (sx, sz) == (dx, dz))
                yield from self.tick(key if f['dir'] != direction else self.pulse())
        yield from self.wait(lambda: self.s.get('field') and not self.s['field']['busy'], label + ': dialogue', progress=True)

    def opening(self):
        # Initial boot is observed through application identity and menu labels.
        while not self.s.get('field'):
            if self.s['frame'] > 12000:
                raise StoryFailure('new-game setup timed out')
            if self.s.get('app_name') == 'NamingScreen_Main':
                key = START if self.s['frame'] % 16 == 0 else A if self.s['frame'] % 16 == 8 else 0
                yield from self.tick(key | B)
            else:
                keys, touch = self.auto()
                yield from self.tick(keys, touch)
        yield from self.wait(lambda: not self.s.get('field', {}).get('busy', True), 'finish opening TV scene')
        yield from self.warp('TWINLEAF_TOWN_PLAYER_HOUSE_1F')
        yield from self.warp('TWINLEAF_TOWN')
        yield from self.warp('TWINLEAF_TOWN_RIVAL_HOUSE_1F')
        yield from self.warp('TWINLEAF_TOWN_RIVAL_HOUSE_2F')
        yield from self.wait(lambda: self.var('VAR_RIVAL_HOUSE_STATE') == 1, 'meet rival upstairs')
        yield from self.warp('TWINLEAF_TOWN_RIVAL_HOUSE_1F')
        yield from self.warp('TWINLEAF_TOWN')
        yield from self.travel(112, 857, 'meet Rowan on Route 201', until=lambda: self.var('VAR_FOLLOWER_RIVAL_STATE') == 1)
        yield from self.wait(lambda: self.var('VAR_FOLLOWER_RIVAL_STATE') == 1 and not self.s.get('field', {}).get('busy', True), 'receive permission to choose starter')
        yield from self.talk(13, 'choose starter from briefcase')
        yield from self.wait(lambda: self.var('VAR_PLAYER_HOUSE_STATE') >= 4 and not self.s.get('field', {}).get('busy', True), 'receive running shoes')
        yield from self.save('starter')

    def lake_and_lab(self):
        yield from self.warp('TWINLEAF_TOWN')
        yield from self.travel(112, 857, 'meet rival for Lake Verity', until=lambda: self.var('VAR_FOLLOWER_RIVAL_STATE') == 3)
        yield from self.travel(80, 844, 'visit Lake Verity', arrival_map='LAKE_VERITY_LOW_WATER')
        yield from self.wait(lambda: self.var('VAR_VISITED_LAKE_VERITY_WITH_RIVAL') == 1 and not self.s.get('field', {}).get('busy', True), 'watch Cyrus scene at Lake Verity')
        yield from self.warp('VERITY_LAKEFRONT')
        yield from self.travel(168, 843, 'visit Rowan laboratory', arrival_map='SANDGEM_TOWN_POKEMON_RESEARCH_LAB')
        yield from self.wait(lambda: self.var('VAR_SANDGEM_TOWN_LAB_STATE') >= 1 and not self.s.get('field', {}).get('busy', True), 'receive Pokedex')
        yield from self.warp('SANDGEM_TOWN')
        yield from self.wait(lambda: self.var('VAR_SANDGEM_TOWN_STATE') >= 2 and not self.s.get('field', {}).get('busy', True), 'finish Sandgem tour')
        yield from self.travel(116, 886, 'return home for parcel', arrival_map='TWINLEAF_TOWN')
        yield from self.warp('TWINLEAF_TOWN_PLAYER_HOUSE_1F')
        yield from self.talk(4, 'tell Mom about the adventure')
        yield from self.wait(lambda: self.var('VAR_PLAYER_HOUSE_STATE') >= 6 and not self.s.get('field', {}).get('busy', True), 'receive journal and parcel')
        yield from self.save('parcel')

    def reach_jubilife(self):
        yield from self.warp('TWINLEAF_TOWN')
        yield from self.travel(112, 857, 'leave Twinleaf toward Sandgem')
        yield from self.travel(112, 852, 'take Route 201 east')
        yield from self.travel(174, 843, 'return to Sandgem', arrival_map='SANDGEM_TOWN')
        yield from self.travel(180, 827, 'watch catching tutorial', until=lambda: self.var('VAR_ROUTE_202_STATE') >= 1)
        yield from self.wait(lambda: self.var('VAR_ROUTE_202_STATE') >= 1 and not self.s.get('field', {}).get('busy', True), 'finish catching tutorial')
        yield from self.travel(174, 776, 'reach Jubilife City', until=lambda: self.var('VAR_JUBILIFE_CITY_STATE') >= 1)
        yield from self.wait(lambda: self.var('VAR_JUBILIFE_CITY_STATE') >= 1 and not self.s.get('field', {}).get('busy', True), 'meet Looker in Jubilife')
        yield from self.warp('TRAINERS_SCHOOL')
        yield from self.talk(1, 'deliver parcel to rival')
        yield from self.warp('JUBILIFE_CITY')
        yield from self.save('school')

    def poketch(self):
        yield from self.walk(174, 776, 'start Poketch campaign')
        yield from self.wait(lambda: self.var('VAR_POKETCH_CAMPAIGN_STATE') >= 2 and not self.s.get('field', {}).get('busy', True), 'hear Poketch campaign rules')
        for script in (14, 15, 16):
            yield from self.talk(script, f'answer clown {script-13} quiz')
        yield from self.talk(18, 'exchange coupons for Poketch')
        if self.var('VAR_JUBILIFE_CITY_STATE') != 2:
            raise StoryFailure('Poketch campaign did not complete')
        yield from self.save('jubilife')

    def oreburgh(self):
        yield from self.travel(200, 758, 'battle rival on Route 203', until=lambda: self.var('VAR_ROUTE_203_RIVAL_STATE') >= 1)
        yield from self.warp('OREBURGH_GATE_1F')
        yield from self.warp('OREBURGH_CITY')
        yield from self.warp('OREBURGH_MINE_B1F')
        yield from self.warp('OREBURGH_MINE_B2F')
        yield from self.talk(1, 'meet Roark in the mine')
        if not self.s['roark_returned']:
            raise StoryFailure('Roark did not return to the gym')
        yield from self.warp('OREBURGH_MINE_B1F')
        yield from self.warp('OREBURGH_CITY')
        yield from self.warp('OREBURGH_CITY_POKECENTER_1F')
        # Nurse interaction is from across the counter, so use an authored tile.
        yield from self.walk(8, 6, 'heal before first gym')
        for _ in range(8):
            yield from self.tick(UP)
        yield from self.wait(lambda: self.s.get('field', {}).get('busy', False), 'ask nurse to heal', limit=240)
        yield from self.wait(lambda: self.s.get('field') and not self.s['field']['busy'], 'finish healing')
        yield from self.warp('OREBURGH_CITY')
        yield from self.save('oreburgh')

    def gym(self):
        yield from self.warp('OREBURGH_CITY_GYM')
        yield from self.talk(1, 'defeat Roark')
        if not self.s.get('badge'):
            raise StoryFailure('Roark battle ended without Coal Badge')
        yield from self.tick(shot=1)
        yield from self.save('coal-badge')

    def open_pause_menu(self, target, label=None):
        yield from self.wait(lambda: bool(self.s.get('field')) and not self.s['field']['busy']
                             and not self.s['field']['moving'], f'settle before opening {target}',
                             limit=1000, auto=False)
        self.objective = label or f'open {target} menu'
        self.menu_target = target
        requested_text = self.s.get('text_id')
        yield from self.tick(X)
        def open_menu():
            nonlocal requested_text
            field = self.s.get('field', {})
            if self.s.get('menu'):
                return self.auto()
            if field and not field['busy'] and not field['moving']:
                requested_text = self.s.get('text_id')
                return self.pulse(X), None
            # Do not confirm before the pause menu has supplied its choices.
            # A deferred story scene identifies itself by a new text event.
            if self.s.get('text_id') != requested_text or self.s.get('battle'):
                return self.auto()
            return 0, None
        yield from self.wait(lambda: self.s.get('menu') and any(choice.upper() == target for choice in self.s['menu']['choices']),
                             self.objective, progress=True, controls=open_menu)

    def save(self, name):
        yield from self.open_pause_menu('SAVE', f'save checkpoint: {name}')
        text_id = self.s['text_id']
        yield from self.wait(lambda: self.s['text_id'] != text_id and 'saved the game' in self.s['text'].lower(), self.objective, limit=2000)
        self.menu_target = None
        yield from self.wait(lambda: self.s.get('field') and not self.s['field']['busy'], f'finish save: {name}', limit=1000)
        self.checkpoint = name
        yield from self.tick()

    def chapters(self):
        return [('starter', self.opening), ('parcel', self.lake_and_lab),
                ('school', self.reach_jubilife), ('jubilife', self.poketch),
                ('oreburgh', self.oreburgh), ('coal-badge', self.gym)]

    checkpoints = ['starter', 'parcel', 'school', 'jubilife', 'oreburgh']
    max_frames = 180000
    timeout = 1800
    target = 'Coal Badge'
    final_checkpoint = 'coal-badge'

    def verify_completion(self):
        if not self.s.get('badge') or not self.s.get('roark_returned') or not self.has_fixture():
            raise StoryFailure('reloaded save lost badge or test party')

    def scenario(self):
        self.observe((yield (0, -1, -1, 0, 0)))
        chapters = self.chapters()
        if self.start_at:
            yield from self.wait(lambda: bool(self.s.get('field')), 'load diagnostic checkpoint')
            yield from self.wait(lambda: not self.s.get('field', {}).get('busy', True), 'finish checkpoint load')
            if not self.has_fixture():
                raise StoryFailure('checkpoint does not contain the test fixture')
        run = self.start_at is None
        for name, chapter in chapters:
            if run:
                yield from chapter()
            if name == self.start_at:
                run = True
        self.objective = f'reload {self.target} save'
        self.reloading = True
        self.observe((yield ('reload',)))
        yield from self.wait(lambda: bool(self.s.get('field')), self.objective)
        yield from self.wait(lambda: not self.s.get('field', {}).get('busy', True), 'verify reloaded save')
        self.verify_completion()



def symbols(binary):
    table = {}
    for line in subprocess.check_output(['nm', '-n', str(binary)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ('T', 't'):
            table[parts[2]] = int(parts[0], 16)
    if 'AutoplayTest_ReadPad' not in table:
        raise StoryFailure('build does not enable -Dautoplay_tests=true')
    base = table['AutoplayTest_ReadPad']
    return {address - base: name for name, address in table.items()}


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def prepare_runtime(build, output, configs=None):
    runtime = output / 'game'
    source = build / 'pokeplatinum'
    def ignore(directory, names):
        if Path(directory) != source:
            return []  # Nested assets may legitimately be named save.bin.
        return [n for n in names if n.lower().endswith('.ini')
                or (n.lower().endswith('.bin') and 'save' in n.lower()) or n == 'main']
    shutil.copytree(source, runtime, ignore=ignore)
    shutil.copy2(build / 'main', runtime / 'main')
    (runtime / 'sim_config.ini').write_text(
        '[General]\nInternalResolutionScale=1\nWindowWidth=512\nWindowHeight=384\n'
        'ScreenLayout=top_only\nVsyncInterval=0\nCapFrameRate=false\n')
    (runtime / 'sim_config_prj.ini').write_text(
        '[General]\n60Fps=true\nBattlePacing=Smooth\nBattleUiSpeedFix=false\nBW2BattleVisuals=false\n'
        'EnableAsserts=true\nBreakDebuggerOnAssert=false\n')
    for name, contents in (configs or {}).items():
        (runtime / name).write_bytes(contents)
    return runtime


def main(controller_class=Controller):
    parser = argparse.ArgumentParser(description=f'Play a fresh game through the {controller_class.target} using the isolated Linux test bridge.')
    parser.add_argument('--build', type=Path, default=ROOT / 'build_linux')
    parser.add_argument('--output', type=Path, required=True, help='new directory for isolated runtime and evidence')
    parser.add_argument('--max-frames', type=int, default=controller_class.max_frames)
    parser.add_argument('--timeout', type=int, default=controller_class.timeout, help='whole-run wall-clock watchdog in seconds')
    parser.add_argument('--visible', action='store_true')
    parser.add_argument('--sim-config', type=Path, help='TAB display settings INI to copy into the isolated runtime')
    parser.add_argument('--project-config', type=Path, help='TAB project settings INI (battle view, speed, etc.) to copy into the isolated runtime')
    parser.add_argument('--gameplay-config', type=Path, help='TAB gameplay rules INI to copy into the isolated runtime')
    parser.add_argument('--checkpoint', type=Path, help='ordinary saved checkpoint for a focused diagnostic run')
    parser.add_argument('--start-after', choices=controller_class.checkpoints, help='chapter completed by the checkpoint')
    args = parser.parse_args()
    if bool(args.checkpoint) != bool(args.start_after):
        parser.error('--checkpoint and --start-after must be used together')
    configs, config_sources = {}, {}
    for option, name in (('sim_config', 'sim_config.ini'),
                         ('project_config', 'sim_config_prj.ini'),
                         ('gameplay_config', 'gameplay_config.ini')):
        path = getattr(args, option)
        if path is not None:
            try:
                path = path.resolve()
                contents = path.read_bytes()
            except OSError as error:
                parser.error(f'--{option.replace("_", "-")}: {error}')
            configs[name] = contents
            config_sources[name] = dict(source=str(path), sha256=hashlib.sha256(contents).hexdigest())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build.resolve()
    names = symbols(build / 'main')
    runtime = prepare_runtime(build, output, configs)
    binary_hash = file_hash(runtime / 'main')
    if args.checkpoint:
        shutil.copy2(args.checkpoint.resolve(), runtime / 'save1.bin')
    controller = controller_class(build, args.start_after)
    runner = controller.scenario()
    next(runner)
    env = {k: v for k, v in os.environ.items() if not k.startswith('PLATINUM_') and k != 'LD_PRELOAD'}
    env.update(SDL_AUDIODRIVER='dummy')
    if not args.visible:
        env['SDL_VIDEODRIVER'] = 'offscreen'
    start = time.monotonic()
    status, reason, state = 'failed', 'game did not produce telemetry', {}
    with (output / 'game.log').open('w') as log, (output / 'trace.jsonl').open('w') as trace:
        def launch():
            parent, child = socket.socketpair()
            parent.settimeout(30)
            child_env = dict(env, PLATINUM_STORY_TEST_FD=str(child.fileno()))
            proc = subprocess.Popen(['./main'], cwd=runtime, env=child_env,
                                    pass_fds=(child.fileno(),), stdout=log, stderr=log)
            child.close()
            return proc, parent, parent.makefile('rb')

        def stop(proc, parent, stream):
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            stream.close()
            parent.close()

        proc, parent, stream = launch()
        saved_checkpoint = None
        total_frames, route_battles, fixture_seen = 0, 0, False
        last_text, last_objective, last_map, last_app = None, None, None, None
        try:
            while True:
                line = stream.readline()
                if not line:
                    raise StoryFailure(f'game disconnected (exit {proc.poll()})')
                state = json.loads(line)
                if state.get('protocol') != 1:
                    raise StoryFailure('unsupported bridge protocol')
                total_frames += 1
                route_battles = max(route_battles, state.get('battles', 0))
                fixture_seen = fixture_seen or bool(state.get('fixture'))
                state['app_name'] = names.get(state['app'], str(state['app']))
                f = state.get('field', {})
                if (state['text_id'] != last_text or controller.objective != last_objective
                        or f.get('map') != last_map or state['app_name'] != last_app):
                    print(state['frame'], controller.objective, state['app_name'],
                          controller.names.get(f.get('map')), f.get('x'), f.get('z'),
                          state.get('text'), state.get('menu'), flush=True)
                    last_text, last_objective, last_map, last_app = (state['text_id'], controller.objective,
                                                                   f.get('map'), state['app_name'])
                try:
                    command = runner.send(state)
                except StopIteration:
                    status, reason = 'passed', getattr(controller, 'success_reason', f'{controller.target} earned and save reloaded')
                    break
                if controller.checkpoint != saved_checkpoint:
                    checkpoint = runtime / 'save1.bin'
                    if not checkpoint.exists() or checkpoint.stat().st_size < 100000:
                        raise StoryFailure('save dialogue completed without a valid save file')
                    shutil.copy2(checkpoint, output / f'checkpoint-{controller.checkpoint}.bin')
                    if controller.checkpoint == controller.final_checkpoint and (runtime / 'story-test.ppm').exists():
                        shutil.copy2(runtime / 'story-test.ppm', output / f'{controller.final_checkpoint}.ppm')
                    saved_checkpoint = controller.checkpoint
                record = dict(state=state, objective=controller.objective, command=command)
                trace.write(json.dumps(record, separators=(',', ':')) + '\n')
                if (output / 'cancel').exists():
                    raise StoryFailure('run cancelled through its output directory')
                if total_frames > args.max_frames or time.monotonic() - start > args.timeout:
                    raise StoryFailure('whole-run watchdog expired')
                if command == ('reload',):
                    stop(proc, parent, stream)
                    proc, parent, stream = launch()
                    continue
                os.write(parent.fileno(), (' '.join(map(str, command)) + '\n').encode())
        except Exception as error:
            reason = str(error)
            import traceback
            (output / 'controller-error.txt').write_text(traceback.format_exc())
            # Ask for a diagnostic screenshot, then let a few frames present it.
            try:
                os.write(parent.fileno(), b'0 -1 -1 0 1\n')
                for _ in range(4):
                    stream.readline()
                    os.write(parent.fileno(), b'0 -1 -1 0 0\n')
            except OSError:
                pass
        finally:
            stop(proc, parent, stream)
    report = dict(status=status, reason=reason, fresh_start=args.checkpoint is None,
                  build=str(build), binary_sha256=binary_hash,
                  checkpoint_source=str(args.checkpoint) if args.checkpoint else None,
                  custom_configs=config_sources,
                  objective=controller.objective, last_state=state, frames=total_frames,
                  battles=route_battles, fixture_installed=fixture_seen,
                  elapsed_seconds=round(time.monotonic() - start, 2))
    if hasattr(controller, 'checks'):
        report['checks'] = controller.checks
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{status}: {reason}; evidence: {output}', flush=True)
    return 0 if status == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
