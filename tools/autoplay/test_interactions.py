"""Checks for interaction decisions that previously selected the wrong action."""
import unittest

from first_gym import A, UP
from interactions import Controller


class InteractionTests(unittest.TestCase):
    def setUp(self):
        self.bot = Controller.__new__(Controller)
        self.bot.menu_target = None
        self.bot.battle_action = ('switch', 0)
        self.bot.s = dict(frame=8, battle=None, ui=None)

    def test_switch_back_uses_displayed_party_order(self):
        self.bot.s.update(ui=dict(name='battle_party', state=1, cursor=0, detail=0),
                          battle_order=[1, 0, 2, 3, 4, 5])
        self.assertEqual(self.bot.auto(), (0, (192, 32)))
        self.bot.s['battle_order'] = [0, 1, 2, 3, 4, 5]
        self.assertEqual(self.bot.auto(), (0, (64, 24)))

    def test_party_selection_waits_for_order_observation(self):
        self.bot.s['ui'] = dict(name='battle_party', state=1, cursor=0, detail=0)
        self.assertEqual(self.bot.auto(), (0, None))

    def test_capture_selects_balls_and_use_confirmation(self):
        self.bot.battle_action = ('catch', 0)
        self.bot.s['ui'] = dict(name='battle_bag', state=1, cursor=0, detail=0)
        self.assertEqual(self.bot.auto(), (0, (192, 40)))
        self.bot.s['ui']['state'] = 3
        self.assertEqual(self.bot.auto(), (0, (100, 170)))

    def test_wait_policy_does_not_attack_at_first_action_menu(self):
        self.bot.battle_action = ('wait', 0)
        self.bot.s['battle'] = dict(menu=1, type=0)
        self.assertEqual(self.bot.auto(), (0, None))

    def test_quantity_selection_waits_until_requested_amount(self):
        self.bot.s['ui'] = dict(name='shop', state=6, cursor=1, detail=4)
        controls = self.bot.shop_controls(15)
        self.assertEqual(controls(), (UP, None))
        self.bot.s['ui']['cursor'] = 15
        self.assertEqual(controls(), (A, None))

    def test_storage_never_confirms_release(self):
        self.bot.s['ui'] = dict(name='storage_menu', state=4, cursor=5, detail=42)
        self.assertEqual(self.bot.storage_select(4)(), (UP, None))
        self.bot.s['ui']['detail'] = 39
        self.assertEqual(self.bot.storage_select(4)(), (A, None))

    def test_shop_does_not_confirm_before_quantity_selection_is_ready(self):
        self.bot.s['ui'] = dict(name='shop', state=5, cursor=1, detail=4)
        self.assertEqual(self.bot.shop_controls(15)(), (0, None))
        self.bot.s['ui']['state'] = 7
        self.assertEqual(self.bot.shop_controls(15)(), (0, None))


if __name__ == '__main__':
    unittest.main()
