#!/usr/bin/env python3
"""Play a fresh game through Gardenia and verify the Forest Badge after reload.

This separate scenario extends the first-gym route, sharing its input driver,
fixture, navigation, isolation, and evidence collection.
"""
from first_gym import Controller as FirstGymController, StoryFailure, UP, main


class Controller(FirstGymController):
    max_frames = 350000
    timeout = 3600
    target = 'Forest Badge'
    final_checkpoint = 'forest-badge'
    checkpoints = FirstGymController.checkpoints + ['coal-badge', 'ravaged', 'floaroma', 'mars', 'windworks', 'eterna']

    def idle(self):
        return bool(self.s.get('field')) and not self.s['field']['busy']

    def chapters(self):
        return super().chapters() + [('ravaged', self.ravaged),
                                     ('floaroma', self.floaroma),
                                     ('mars', self.windworks),
                                     ('windworks', self.leave_windworks),
                                     ('eterna', self.eterna),
                                     ('forest-badge', self.second_gym)]

    def ravaged(self):
        yield from self.warp('OREBURGH_CITY')
        yield from self.warp('OREBURGH_GATE_1F')
        yield from self.warp('ROUTE_203')
        yield from self.travel(174, 758, 'return to Jubilife', arrival_map='JUBILIFE_CITY')
        yield from self.travel(174, 743, 'help Rowan against Team Galactic',
                               until=lambda: self.var('VAR_JUBILIFE_CITY_STATE') >= 4)
        yield from self.wait(lambda: self.idle() and self.var('VAR_JUBILIFE_CITY_STATE') >= 4,
                             'finish Jubilife tag battle', progress=True)
        yield from self.travel(171, 705, 'reach Ravaged Path', arrival_map='ROUTE_204_SOUTH')
        yield from self.warp('RAVAGED_PATH')
        yield from self.save('ravaged')

    def floaroma(self):
        # Pick up the Potion in the approach, then use Bibarel's Rock Smash.
        if any(o[3] == 7251 for o in self.s['field']['objects']):
            yield from self.talk(7251, 'pick up Ravaged Path Potion')
        yield from self.talk(10001, 'smash Ravaged Path boulder')
        yield from self.warp('ROUTE_204_NORTH')
        yield from self.travel(175, 663, 'reach Floaroma Town', arrival_map='FLOAROMA_TOWN')
        yield from self.save('floaroma')

    def windworks(self):
        yield from self.travel(239, 658, 'reach Valley Windworks', arrival_map='VALLEY_WINDWORKS_OUTSIDE')
        yield from self.talk(3, 'defeat Windworks door guard')
        yield from self.travel(180, 661, 'return to Floaroma for Works Key', arrival_map='FLOAROMA_TOWN')
        # The southwestern entrance is available before Surf; the northern pair is not.
        yield from self.warp('FLOAROMA_MEADOW', index=2)
        yield from self.walk(12, 48, 'rescue honey seller',
                             until=lambda: self.var('VAR_FLOAROMA_MEADOW_STATE') >= 1)
        yield from self.wait(lambda: self.idle() and self.var('VAR_FLOAROMA_MEADOW_STATE') >= 1,
                             'receive Works Key', progress=True)
        yield from self.warp('FLOAROMA_TOWN', index=2)
        yield from self.travel(243, 655, 'return to Windworks door', arrival_map='VALLEY_WINDWORKS_OUTSIDE')
        yield from self.walk(243, 655, 'stand at locked Windworks door')
        # This is a background event, rather than an NPC object.
        for _ in range(8):
            yield from self.tick(UP)
        yield from self.wait(lambda: self.s.get('field', {}).get('busy', False), 'unlock Windworks door', limit=240)
        yield from self.wait(self.idle, 'use Works Key')
        yield from self.warp('VALLEY_WINDWORKS_BUILDING')
        yield from self.walk(19, 7, 'challenge Commander Mars',
                             until=lambda: self.var('VAR_VALLEY_WINDWORKS_STATE') >= 2)
        yield from self.wait(lambda: self.idle() and self.var('VAR_VALLEY_WINDWORKS_STATE') >= 2,
                             'rescue Windworks scientist', progress=True)
        yield from self.save('mars')

    def leave_windworks(self):
        yield from self.warp('VALLEY_WINDWORKS_OUTSIDE')
        yield from self.wait(lambda: self.idle() and self.var('VAR_VALLEY_WINDWORKS_LOOKER_STATE') >= 2,
                             'finish Looker Windworks investigation', progress=True)
        yield from self.save('windworks')

    def eterna(self):
        yield from self.travel(217, 646, 'cross reopened Route 205 bridge', arrival_map='ROUTE_205_SOUTH')
        yield from self.warp('ETERNA_FOREST')
        yield from self.warp('ROUTE_205_NORTH')
        yield from self.travel(300, 534, 'reach Eterna City', arrival_map='ETERNA_CITY')
        yield from self.wait(self.idle, 'finish Eterna arrival', progress=True)
        yield from self.warp('ETERNA_CITY_POKECENTER_1F')
        yield from self.walk(8, 6, 'heal before second gym')
        for _ in range(8):
            yield from self.tick(UP)
        yield from self.wait(lambda: self.s.get('field', {}).get('busy', False), 'ask Eterna nurse to heal', limit=240)
        yield from self.wait(self.idle, 'finish Eterna healing')
        yield from self.warp('ETERNA_CITY')
        yield from self.save('eterna')

    def second_gym(self):
        if any(o[3] == 24 for o in self.s['field']['objects']):
            yield from self.talk(24, 'meet Gardenia outside gym')
        yield from self.warp('ETERNA_CITY_GYM')
        for count, script in enumerate((5, 6, 7), 1):
            yield from self.talk(script, f'defeat Eterna gym trainer {count}')
            yield from self.wait(lambda: self.idle() and self.var('VAR_ETERNA_GYM_TRAINERS_BEATEN') >= count,
                                 f'finish gym clock rotation {count}', progress=True)
            # Rotating hands change traversability; forget failed edges from the old layout.
            self.blocked.clear()
        yield from self.talk(4, 'defeat Gardenia')
        if not self.s.get('forest_badge'):
            raise StoryFailure('Gardenia battle ended without Forest Badge')
        yield from self.tick(shot=1)
        yield from self.save('forest-badge')

    def verify_completion(self):
        super().verify_completion()
        if not self.s.get('forest_badge'):
            raise StoryFailure('reloaded save lost Forest Badge')


if __name__ == '__main__':
    raise SystemExit(main(Controller))
