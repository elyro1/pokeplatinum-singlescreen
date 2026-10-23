#!/usr/bin/env python3
"""Extend the normal story route through Fantina's Relic Badge in Platinum."""
from first_gym import LEFT, UP, StoryFailure, main
from second_gym import Controller as SecondGymController


class Controller(SecondGymController):
    max_frames = 500000
    timeout = 5400
    target = 'Relic Badge'
    final_checkpoint = 'relic-badge'
    checkpoints = SecondGymController.checkpoints + ['forest-badge', 'cut', 'jupiter', 'bicycle', 'hearthome']

    def chapters(self):
        return super().chapters() + [('cut', self.prepare_cut),
                                     ('jupiter', self.galactic_eterna),
                                     ('bicycle', self.get_bicycle),
                                     ('hearthome', self.reach_hearthome),
                                     ('relic-badge', self.third_gym)]

    def prepare_cut(self):
        yield from self.warp('ETERNA_CITY')
        yield from self.warp('ETERNA_CITY_UNDERGROUND_MAN_HOUSE')
        yield from self.talk(1, 'receive Explorer Kit')
        if not self.s.get('explorer_kit'):
            raise StoryFailure('Underground Man did not give Explorer Kit')
        yield from self.warp('ETERNA_CITY')
        yield from self.travel(303, 524, 'meet rival at Eterna statue',
                               until=lambda: self.var('VAR_ETERNA_CITY_STATE') >= 1)
        yield from self.wait(lambda: self.idle() and self.var('VAR_ETERNA_CITY_STATE') >= 1,
                             'finish Eterna statue scene', progress=True)
        yield from self.walk(304, 524, 'meet Cynthia for Cut',
                             until=lambda: self.var('VAR_ETERNA_CITY_STATE') >= 2)
        yield from self.wait(lambda: self.idle() and self.var('VAR_ETERNA_CITY_STATE') >= 2,
                             'receive Cut from Cynthia', progress=True)
        yield from self.save('cut')

    def galactic_eterna(self):
        yield from self.talk(10000, 'cut tree outside Galactic building')
        yield from self.warp('TEAM_GALACTIC_ETERNA_BUILDING_1F')
        yield from self.warp('TEAM_GALACTIC_ETERNA_BUILDING_2F', index=1)
        yield from self.warp('TEAM_GALACTIC_ETERNA_BUILDING_3F', index=1)
        yield from self.warp('TEAM_GALACTIC_ETERNA_BUILDING_4F', index=1)
        yield from self.talk(2, 'defeat Commander Jupiter')
        if self.var('VAR_ETERNA_CITY_STATE') < 3:
            raise StoryFailure('Jupiter battle did not rescue bicycle shop owner')
        yield from self.save('jupiter')

    def get_bicycle(self):
        for destination, index in (('TEAM_GALACTIC_ETERNA_BUILDING_3F', 1),
                                   ('TEAM_GALACTIC_ETERNA_BUILDING_2F', 1),
                                   ('TEAM_GALACTIC_ETERNA_BUILDING_1F', 1),
                                   ('ETERNA_CITY', 0)):
            yield from self.warp(destination, index=index)
        # Cut trees regrow when this exterior is reloaded. Leave through the fence.
        yield from self.talk(10000, 'cut exit tree at Galactic building')
        # Approach the shop; ordinary dialogue may also award Cynthia's Egg.
        yield from self.walk(312, 540, 'approach bicycle shop')
        yield from self.warp('CYCLE_SHOP')
        yield from self.talk(1, 'receive bicycle from rescued owner')
        if not self.s.get('bicycle'):
            raise StoryFailure('bicycle shop did not give bicycle')
        yield from self.warp('ETERNA_CITY')
        yield from self.save('bicycle')

    def bicycle_pocket_controls(self):
        # Pocket-switch animation also reads Left/Right. Wait until the item
        # list is ready so repeated presses cannot keep restarting the switch.
        ready = self.s.get('app_state') == 1 and self.s.get('menu') is not None
        return (self.pulse(LEFT) if ready else 0), None

    def ride_bicycle(self):
        # Select Bag, cycle its ordinary item pockets until Bicycle is visible,
        # then select Bicycle > Use. No registered-item or avatar-state writes.
        yield from self.open_pause_menu('BAG')
        yield from self.wait(lambda: self.s.get('app_name') == 'BagApplication_Main',
                             'open Bag for bicycle')
        self.menu_target = 'BICYCLE'
        yield from self.wait(lambda: self.s.get('menu') and any(
            label.upper() == 'BICYCLE' for label in self.s['menu']['choices']),
            'find bicycle in Key Items', limit=2000,
            controls=self.bicycle_pocket_controls)
        yield from self.wait(lambda: self.s.get('avatar_state') == 1 and self.idle(),
                             'ride bicycle', progress=True)
        self.menu_target = None

    def reach_hearthome(self):
        yield from self.ride_bicycle()
        yield from self.warp('ROUTE_206_CYCLING_ROAD_NORTH_GATE')
        yield from self.warp('ROUTE_206')
        # Route 206 has lower-level doors as well as the raised Cycling Road.
        yield from self.warp('ROUTE_206_CYCLING_ROAD_SOUTH_GATE', index=2)
        yield from self.warp('ROUTE_206', index=1)
        yield from self.travel(340, 712, 'meet counterpart on Route 207',
                               until=lambda: self.var('VAR_ROUTE_207_COUNTERPART_TRIGGER_STATE') >= 1)
        yield from self.wait(lambda: self.idle() and self.var('VAR_ROUTE_207_COUNTERPART_TRIGGER_STATE') >= 1,
                             'receive Vs Seeker and Dowsing Machine', progress=True)
        yield from self.warp('MT_CORONET_1F_SOUTH')
        yield from self.warp('ROUTE_208')
        yield from self.warp('ROUTE_208_GATE_TO_HEARTHOME_CITY')
        yield from self.warp('HEARTHOME_CITY')
        yield from self.travel(462, 727, 'meet Keira and Buneary',
                               until=lambda: self.var('VAR_HEARTHOME_CITY_STATE') >= 1)
        yield from self.wait(lambda: self.idle() and self.var('VAR_HEARTHOME_CITY_STATE') >= 1,
                             'finish Buneary scene', progress=True)
        yield from self.warp('CONTEST_HALL_LOBBY')
        yield from self.wait(lambda: self.idle() and self.var('VAR_CONTEST_HALL_LOBBY_STATE') >= 1,
                             'visit Mom at Contest Hall', progress=True)
        yield from self.talk(11, 'send Fantina to her gym')
        if not self.s.get('fantina_ready'):
            raise StoryFailure('Fantina did not return to gym')
        yield from self.warp('HEARTHOME_CITY')
        yield from self.warp('HEARTHOME_CITY_POKECENTER_1F')
        yield from self.walk(8, 6, 'heal before third gym')
        for _ in range(8):
            yield from self.tick(UP)
        yield from self.wait(lambda: self.s.get('field', {}).get('busy', False), 'ask Hearthome nurse to heal', limit=240)
        yield from self.wait(self.idle, 'finish Hearthome healing')
        yield from self.warp('HEARTHOME_CITY')
        yield from self.save('hearthome')

    @staticmethod
    def clue_door_index(clue, room):
        x, z, symbol = clue
        base, count = (0, 3) if room == 1 else (3, 5)
        if not base <= symbol < base + count:
            raise StoryFailure(f'invalid Hearthome room {room} clue: {clue}')
        return symbol - base

    def third_gym(self):
        yield from self.warp('HEARTHOME_CITY_GYM_ENTRANCE_ROOM')
        yield from self.wait(lambda: self.idle() and self.var('VAR_HAS_ENTERED_HEARTHOME_GYM_BEFORE'),
                             'hear Hearthome gym instructions', progress=True)
        yield from self.warp('HEARTHOME_CITY_GYM_TRAINER_ROOM_1')
        for room, destination in ((1, 'HEARTHOME_CITY_GYM_TRAINER_ROOM_2'),
                                  (2, 'HEARTHOME_CITY_GYM_LEADER_ROOM')):
            yield from self.wait(lambda: bool(self.s.get('hearthome_clue')) and self.idle(),
                                 f'observe gym room {room} floor clue')
            clue = self.s['hearthome_clue']
            index = self.clue_door_index(clue, room)
            field = self.s['field']
            if (field['x'], field['z']) != tuple(clue[:2]):
                yield from self.walk(*clue[:2], f'inspect gym room {room} clue', adjacent=True)
            yield from self.warp(destination, index=index)
        yield from self.talk(1, 'defeat Fantina')
        if not self.s.get('relic_badge'):
            raise StoryFailure('Fantina battle ended without Relic Badge')
        yield from self.tick(shot=1)
        yield from self.save('relic-badge')

    def verify_completion(self):
        super().verify_completion()
        if not self.s.get('relic_badge') or not self.s.get('bicycle'):
            raise StoryFailure('reloaded save lost Relic Badge or bicycle')


if __name__ == '__main__':
    raise SystemExit(main(Controller))
