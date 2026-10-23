#!/usr/bin/env python3
"""Normal-input system interaction checks, independently resumable after Fantina."""
from first_gym import A, B, DOWN, LEFT, RIGHT, UP, ROOT, DIRECTIONS, StoryFailure, main
from third_gym import Controller as ThirdGymController


class Controller(ThirdGymController):
    target = 'interaction checks'
    success_reason = 'interaction checks passed and save reloaded'
    final_checkpoint = 'interactions'
    max_frames = 700000
    timeout = 7200
    checkpoints = ThirdGymController.checkpoints + ['relic-badge', 'menus', 'supplies', 'wild', 'pc', 'solaceon', 'route215', 'veilstone']

    def __init__(self, build, start_at=None):
        super().__init__(build, start_at)
        self.checks = {}
        self.items = {name: i for i, name in enumerate((ROOT / 'generated/items.txt').read_text().splitlines())}
        self.battle_action = None

    def chapters(self):
        return super().chapters() + [('menus', self.start_menu_checks),
                                     ('supplies', self.buy_supplies),
                                     ('wild', self.wild_checks),
                                     ('pc', self.pc_checks),
                                     ('solaceon', self.reach_solaceon),
                                     ('route215', self.reach_route215),
                                     ('veilstone', self.reach_veilstone),
                                     ('interactions', self.mall_checks)]

    def ui(self, name):
        ui = self.s.get('ui')
        return ui if ui and ui['name'] == name else {}

    def count(self, item):
        return dict(self.s.get('inventory', [])).get(self.items[item], 0)

    def checked(self, name, **evidence):
        self.checks[name] = dict(frame=self.s['frame'], **evidence)

    def touch_button(self, x, y):
        return 0, (x, y) if self.s['frame'] % 8 == 0 else None

    def auto(self):
        action = getattr(self, 'battle_action', None)
        if self.s.get('battle') and self.s['battle']['type'] & 1:
            return super().auto()
        if action:
            mode, slot = action
            bag = self.ui('battle_bag')
            if bag:
                if bag['state'] == 1:
                    return self.touch_button(192 if mode == 'catch' else 64, 40)
                if bag['state'] == 2:
                    return self.touch_button(64, 32)
                if bag['state'] == 3:
                    return self.touch_button(100, 170)
                return self.pulse(), None
            party = self.ui('battle_party')
            if party:
                if party['cursor'] == 0:
                    order = self.s.get('battle_order')
                    if not order or slot not in order:
                        return 0, None
                    displayed = order.index(slot)
                    return self.touch_button(64 + (displayed % 2) * 128, 24 + (displayed // 2) * 48 + (displayed % 2) * 8)
                if party['cursor'] == 1:
                    return self.touch_button(100, 72)
                return self.pulse(), None
            battle = self.s.get('battle')
            if battle and battle['menu'] in (1, 2, 3, 4):
                if mode == 'wait':
                    return 0, None
                return self.touch_button(160, 176) if mode == 'switch' else self.touch_button(208, 160)
        return super().auto()

    def battle_ready(self):
        return bool(self.s.get('battle')) and self.s['battle']['menu'] in (1, 2, 3, 4) and not self.ui('battle_party') and not self.ui('battle_bag')

    def cancel_controls(self):
        return self.pulse(B), None

    def menu_only_controls(self):
        # Advance PC dialogue with separated edges. Menu construction is
        # observed before input, so named choices take priority as they appear.
        if self.s.get('menu'):
            return self.auto()
        return A if self.s['frame'] % 32 == 0 else 0, None

    def field_ready(self):
        return self.s.get('app_name') == 'FieldMap_Main' and self.idle() and not self.s.get('menu')

    def return_to_field(self, label):
        self.menu_target = None
        yield from self.wait(self.field_ready, label, controls=self.cancel_controls, progress=True)

    def leave_gym(self):
        if self.s['field']['map'] == self.map_id('HEARTHOME_CITY_GYM_LEADER_ROOM'):
            yield from self.warp('HEARTHOME_CITY_GYM_ENTRANCE_ROOM')
        if self.s['field']['map'] == self.map_id('HEARTHOME_CITY_GYM_ENTRANCE_ROOM'):
            yield from self.warp('HEARTHOME_CITY')

    def start_menu_checks(self):
        yield from self.leave_gym()
        yield from self.open_pause_menu('BAG')
        choices = list(self.s['menu']['choices'])
        yield from self.return_to_field('close start menu inventory')
        for choice in choices:
            if choice.upper() == 'SAVE':
                continue  # The ordinary save at the end of this chapter tests it.
            yield from self.open_pause_menu(choice.upper())
            if choice.upper() == 'QUIT':
                yield from self.wait(lambda: self.s.get('menu') and len(self.s['menu']['choices']) == 2,
                                     'open Quit confirmation')
                yield from self.return_to_field('cancel Quit confirmation')
                app = 'FieldMap_Main'
            elif choice.upper() == 'POK TCH':
                yield from self.wait(lambda: bool(self.ui('poketch')), 'open Poketch from start menu')
                # Allow the switch to the watch screen to finish before closing.
                yield from self.wait(lambda: self.ui('poketch').get('state') == 19,
                                     'display Poketch', auto=False, limit=1000)
                app = 'Poketch'
                yield from self.return_to_field('close Poketch')
            else:
                expected = {'POK DEX': 'PokedexMain_Main', 'POK MON': 'PartyMenu_Main',
                            'BAG': 'BagApplication_Main', 'OPTIONS': 'OptionsMenu_Main'}.get(choice.upper(), 'TrainerCaseApp_Main')
                yield from self.wait(lambda: self.s.get('app_name') == expected,
                                     f'open start menu feature: {choice}')
                app = self.s['app_name']
                # Let each app finish its initial visual setup.
                for _ in range(60):
                    yield from self.tick()
                yield from self.return_to_field(f'close start menu feature: {choice}')
            self.checked('menu:' + choice, application=app)
        yield from self.save('menus')
        self.checked('menu:SAVE')

    def approach(self, script, label, counter=False):
        f = self.s['field']
        obj = min((o for o in f['objects'] if o[3] == script and o[0] != 255),
                  key=lambda o: abs(o[1]-f['x']) + abs(o[2]-f['z']))
        if counter:
            yield from self.walk(obj[1], obj[2] + 2, label)
        else:
            yield from self.walk(obj[1], obj[2], label, adjacent=True, follow=(obj[0], obj[3]))
        yield from self.wait(lambda: bool(self.s.get('menu')) or self.s['field']['busy'], label + ': interact',
                             controls=self.auto_interact(obj[0], obj[3]))

    def auto_interact(self, local_id, script):
        def controls():
            f = self.s['field']
            obj = next(o for o in f['objects'] if (o[0], o[3]) == (local_id, script))
            dx, dz = obj[1]-f['x'], obj[2]-f['z']
            dx, dz = (0 if dx == 0 else (1 if dx > 0 else -1)), (0 if dz == 0 else (1 if dz > 0 else -1))
            direction, key = next((i, k) for i, (sx, sz, k) in enumerate(DIRECTIONS) if (sx, sz) == (dx, dz))
            return (key if f['dir'] != direction else self.pulse()), None
        return controls

    def shop_controls(self, quantity):
        def controls():
            ui = self.ui('shop')
            if ui.get('state') == 6:
                cursor = ui['cursor']
                return self.pulse(UP if cursor < quantity else DOWN) if cursor != quantity else self.pulse(), None
            if ui.get('state') in (3, 5, 7):
                return 0, None
            return self.auto()
        return controls

    def buy(self, label, item, quantity):
        before, money = self.count(item), self.s['money']
        self.menu_target = label.upper()
        yield from self.wait(lambda: self.count(item) >= before + quantity,
                             f'buy {quantity} {label}', controls=self.shop_controls(quantity), progress=True)
        if self.s['money'] >= money:
            raise StoryFailure('purchase did not deduct money')
        self.checked('purchase:' + label, before=before, after=self.count(item), money_before=money, money_after=self.s['money'])

    def buy_supplies(self):
        yield from self.warp('HEARTHOME_CITY_MART')
        self.menu_target = 'BUY'
        yield from self.approach(1, 'speak to Hearthome common vendor', counter=True)
        yield from self.buy('Pok Ball', 'ITEM_POKE_BALL', 15)
        yield from self.buy('Potion', 'ITEM_POTION', 3)
        yield from self.return_to_field('leave Hearthome shop menus')
        yield from self.warp('HEARTHOME_CITY')
        yield from self.save('supplies')

    def wild_checks(self):
        yield from self.warp('ROUTE_208_GATE_TO_HEARTHOME_CITY')
        yield from self.warp('ROUTE_208')
        self.battle_action = ('wait', 0)
        yield from self.walk(424, 724, 'approach Route 208 grass', until=lambda: bool(self.s.get('battle')))
        yield from self.seek_wild()
        yield from self.switch_in_battle(1)
        self.checked('battle:switch-out', species=400)
        yield from self.switch_in_battle(0)
        self.checked('battle:switch-back', species=151)
        self.checked('battle:medicine', status='skipped', reason='prohibited by game configuration')
        self.battle_action = None
        battles = self.s['battles']
        yield from self.wait(self.field_ready, 'defeat wild Pokémon', progress=True)
        if self.s['battles'] <= battles:
            raise StoryFailure('wild battle did not finish')
        self.checked('battle:wild-victory')
        yield from self.seek_wild()
        count, balls = self.s['party'], self.count('ITEM_POKE_BALL')
        self.battle_action = ('catch', 0)
        yield from self.wait(lambda: self.s['party'] == count + 1, 'catch wild Pokémon', progress=True)
        self.battle_action = None
        self.menu_target = 'NO'
        yield from self.wait(self.field_ready, 'finish capture and decline nickname', progress=True)
        self.menu_target = None
        if self.count('ITEM_POKE_BALL') >= balls:
            raise StoryFailure('capture did not consume a Poké Ball')
        self.checked('battle:catch', pokemon=self.s['mons'][-1], balls_used=balls-self.count('ITEM_POKE_BALL'))
        yield from self.warp('ROUTE_208_GATE_TO_HEARTHOME_CITY')
        yield from self.warp('HEARTHOME_CITY')
        yield from self.save('wild')

    def seek_wild(self):
        self.battle_action = ('wait', 0)
        self.objective = 'seek ordinary wild encounter'
        start = self.s['frame']
        while True:
            if self.s.get('battle'):
                if self.s['battle']['type'] & 1:
                    yield from self.wait(self.field_ready, 'finish incidental trainer battle', progress=True)
                    continue
                break
            f = self.s['field']
            grass = [(x, z) for (matrix, x, z), attr in self.tiles.items()
                     if matrix == f.get('matrix', f['map']) and attr & 0xff in (2, 3) and not attr & 0x8000]
            if not grass:
                raise StoryFailure('no observed grass for wild encounter')
            grass.sort(key=lambda p: abs(p[0]-f['x']) + abs(p[1]-f['z']))
            goal = next((p for p in grass if p != (f['x'], f['z'])), grass[0])
            yield from self.walk(*goal, 'seek ordinary wild encounter', until=lambda: bool(self.s.get('battle')))
            if self.s['frame'] - start > 12000:
                raise StoryFailure('no wild encounter within search budget')
        yield from self.wait(self.battle_ready, 'wait for wild battle actions', progress=True)
        if self.s['battle']['type'] & 1:
            raise StoryFailure('encounter is a trainer battle, not a wild Pokémon')

    def switch_in_battle(self, slot):
        self.battle_action = ('switch', slot)
        species = self.s['mons'][slot][0]
        yield from self.wait(lambda: self.battle_ready() and self.s['battle'].get('active_species') == species,
                             f'switch to party slot {slot}', progress=True)
        self.battle_action = ('wait', 0)

    def pc_checks(self):
        yield from self.warp('HEARTHOME_CITY_POKECENTER_1F')
        f = self.s['field']
        computers = [(x, z) for (matrix, x, z), attr in self.tiles.items()
                     if matrix == f.get('matrix', f['map']) and attr & 0xff == 0x83]
        if not computers:
            # Walk toward the upper wall to observe tiles beyond the entrance.
            yield from self.walk(13, 6, 'approach Pokémon Center PC')
            f = self.s['field']
            computers = [(x, z) for (matrix, x, z), attr in self.tiles.items()
                         if matrix == f.get('matrix', f['map']) and attr & 0xff == 0x83]
        if not computers:
            raise StoryFailure('Pokémon Center PC tile not observed')
        x, z = min(computers, key=lambda p: abs(p[0]-f['x']) + abs(p[1]-f['z']))
        yield from self.walk(x, z+1, 'stand at Pokémon Center PC')
        for _ in range(8):
            yield from self.tick(UP)
        self.menu_target = None
        yield from self.wait(lambda: self.s['field']['busy'], 'boot Pokémon Center PC',
                             controls=lambda: (self.pulse(), None), limit=240)
        yield from self.wait(lambda: self.s.get('menu') and any('DEPOSIT' in v for v in self.s['menu']['choices']),
                             'access Pokémon storage PC', controls=self.menu_only_controls)
        count = self.s['party']
        original = self.s['mons'][-1][:]
        if count < 4 or original[5]:
            raise StoryFailure('PC round trip requires a caught Pokémon at end of party')
        baseline = list(self.s['boxed'])
        self.menu_target = 'DEPOSIT POK MON'
        yield from self.wait(lambda: bool(self.ui('storage')), 'open Deposit Pokémon', controls=self.menu_only_controls)
        self.menu_target = None
        yield from self.wait(lambda: self.s['party'] == count-1 and len(self.s['boxed']) == len(baseline)+1,
                             'deposit caught Pokémon', controls=self.storage_select(count-1), progress=True)
        deposited = next(mon for mon in self.s['boxed'] if mon not in baseline)
        if deposited[2:] != [original[0], original[4]]:
            raise StoryFailure('PC deposited the wrong Pokémon')
        self.checked('pc:deposit', pokemon=deposited)
        yield from self.leave_storage()
        self.menu_target = None
        yield from self.wait(lambda: self.s.get('menu') and any('WITHDRAW' in v for v in self.s['menu']['choices']),
                             'return to Pokémon storage choices', controls=self.menu_only_controls)
        self.menu_target = 'WITHDRAW POK MON'
        yield from self.wait(lambda: bool(self.ui('storage')), 'open Withdraw Pokémon', controls=self.menu_only_controls)
        self.menu_target = None
        yield from self.wait(lambda: self.s['party'] == count and len(self.s['boxed']) == len(baseline),
                             'withdraw deposited Pokémon', controls=self.storage_select(deposited[1], boxed=True), progress=True)
        if self.s['mons'][-1][0] != original[0] or self.s['mons'][-1][4] != original[4]:
            raise StoryFailure('withdrawn Pokémon identity changed')
        self.checked('pc:withdraw', pokemon=self.s['mons'][-1])
        yield from self.leave_storage()
        yield from self.return_to_field('log off PC')
        yield from self.warp('HEARTHOME_CITY')
        yield from self.save('pc')

    def storage_select(self, target, boxed=False):
        def controls():
            selector = self.ui('storage_box')
            if selector:
                return self.pulse() if selector['state'] == 3 else 0, None
            menu = self.ui('storage_menu')
            if menu:
                # Deposit/Withdraw modes put their operation first. Never
                # select Release or another operation on an unexpected menu.
                expected = 38 if boxed else 39  # enum BoxMenuItem
                if menu['detail'] == expected:
                    return self.pulse(), None
                return self.pulse(UP), None
            ui = self.ui('storage')
            if not ui or ui['state'] != 0:
                return 0, None
            if ui['detail'] != (0 if boxed else 1):
                raise StoryFailure('unexpected storage cursor location')
            cursor = ui['cursor']
            if cursor == target:
                return self.pulse(), None
            columns = 6 if boxed else 2
            key = (DOWN if cursor // columns < target // columns else UP) if cursor // columns != target // columns else (RIGHT if cursor < target else LEFT)
            return self.pulse(key), None
        return controls

    def leave_storage(self):
        def controls():
            menu = self.ui('storage_menu')
            if menu:
                # Deposit mode asks "Continue operations?": choose No.
                return self.pulse() if menu['detail'] == 55 else self.pulse(DOWN), None
            return self.pulse(B), None
        yield from self.wait(lambda: self.s.get('app_name') == 'FieldMap_Main',
                             'close storage application', controls=controls, progress=True)

    def reach_solaceon(self):
        yield from self.warp('ROUTE_209_GATE_TO_HEARTHOME_CITY')
        yield from self.warp('ROUTE_209')
        yield from self.travel(560, 662, 'travel north to Solaceon', arrival_map='SOLACEON_TOWN')
        yield from self.save('solaceon')

    def reach_route215(self):
        yield from self.travel(570, 602, 'take Route 210 north', arrival_map='ROUTE_210_SOUTH')
        yield from self.travel(608, 600, 'take Route 215 east', arrival_map='ROUTE_215')
        yield from self.save('route215')

    def reach_veilstone(self):
        # Leave the raised western path by its stairs, then cross beneath the
        # north/south bridge. The flat grid alone cannot distinguish its levels.
        for x, z in ((587, 590), (600, 590)):
            yield from self.travel(x, z, 'cross beneath Route 215 western bridge')
        yield from self.warp('ROUTE_215_GATE_TO_VEILSTONE_CITY')
        yield from self.warp('VEILSTONE_CITY')
        yield from self.save('veilstone')

    def mall_checks(self):
        yield from self.warp('VEILSTONE_STORE_1F')
        self.menu_target = 'BUY'
        yield from self.approach(4, 'speak to department store vendor', counter=True)
        yield from self.buy('Potion', 'ITEM_POTION', 2)
        yield from self.return_to_field('close department store purchase')
        self.checked('mall:purchase')
        yield from self.warp('VEILSTONE_CITY')
        self.expected_final = self.saved_state()
        yield from self.tick(shot=1)
        yield from self.save('interactions')

    def saved_state(self):
        return dict(party=[(mon[0], mon[4]) for mon in self.s['mons']],
                    boxed=self.s['boxed'], inventory=self.s['inventory'], money=self.s['money'])

    def verify_completion(self):
        super().verify_completion()
        if self.count('ITEM_POTION') < 2:
            raise StoryFailure('reloaded save lost department store purchase')
        if self.saved_state() != self.expected_final:
            raise StoryFailure('reloaded save changed party, boxes, items, or money')
        self.checked('save:reload')


if __name__ == '__main__':
    raise SystemExit(main(Controller))
