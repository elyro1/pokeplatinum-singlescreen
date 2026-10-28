#ifndef BWBATTLE_BW_ADAPTER_H
#define BWBATTLE_BW_ADAPTER_H

// Platinum's battle showing itself through BW2's view (src/bwbattle/adapter, docs/bwbattle.md). This header is
// Platinum's side: BW2's files never include it.

#include "battle/battle_system.h"
#include "struct_defs/move_animation.h"

// Whether battles use BW2's view: the setting is on and the BW2 assets are there
BOOL BwAdapter_IsEnabled(void);
// Whether this battle runs BW2's view (BwAdapter_BattleCreate succeeded)
BOOL BwAdapter_IsActive(void);
// Whether this battle uses the shortened opening presentation.
BOOL BwAdapter_FastIntro(void);
// Remains true after the opening, for first-turn scheduling.
BOOL BwAdapter_UsedFastIntro(void);
void BwAdapter_EndIntro(void);
// Whether the view draws the battle now: active and not waiting for Platinum (BwAdapter_SetSuspended)
BOOL BwAdapter_IsDrawing(void);

// From battle_main.c: once the battle's screens are set up, and before they are freed
void BwAdapter_BattleCreate(BattleSystem *battleSys);
void BwAdapter_BattleDelete(BattleSystem *battleSys);
// From the display commands that show a battler's Pokémon (battle_display.c): the sprite they set up, and the party
// slot of the Pokémon, -1 if the command doesn't say (the party's lead is taken)
void BwAdapter_SetBattlerMon(BattleSystem *battleSys, int battler, u16 species, u8 form, u32 personality, u8 gender,
    BOOL shiny, int partySlot);
// From the sprite commands around a substitute (battle_display.c): Platinum shows the Pokémon's own sprite until it
// refreshes it
void BwAdapter_OnRestoreSprite(BattleSystem *battleSys, int battler);
void BwAdapter_OnRefreshSprite(BattleSystem *battleSys, int battler);
// From the display commands of the battle start, send-outs, recalls and trainers (battle_display.c): they queue BW2's
// effects for them, one after another. BwAdapter_IsBusy is TRUE while the queue plays, and the battle script's
// Wait waits for it as for the display commands
void BwAdapter_OnSetEncounter(BattleSystem *battleSys, int battler);
void BwAdapter_OnTrainerEncounter(BattleSystem *battleSys, int battler, int trainerClass);
void BwAdapter_OnThrowTrainerBall(BattleSystem *battleSys, int battler, int ballTypeIn);
// battleStart: the battle start's send-out (ShowEncounter) rather than a switch (ShowPokemon)
void BwAdapter_OnShowMon(BattleSystem *battleSys, int battler, BOOL battleStart);
void BwAdapter_OnReturnMon(BattleSystem *battleSys, int battler);
// Styles Platinum's textbox as BW2's message window (bw_battle_view.c) after Platinum loaded its palettes, before they
// show; the view does it each frame too
void BwAdapter_StyleTextbox(BattleSystem *battleSys);
// The turn's commands being chosen (battle_controller_player.c): BW2's idle camera plays meanwhile, and its camera
// goes back once they are
void BwAdapter_OnCommandSelection(BattleSystem *battleSys, BOOL selecting);
// A Pokémon fainting (Task_PlayFaintingSequence): BW2's faint, whose sound stands in for Platinum's
void BwAdapter_OnFaint(BattleSystem *battleSys, int battler);
void BwAdapter_OnTrainerSlideIn(BattleSystem *battleSys, int battler, int trainerClass);
void BwAdapter_OnTrainerSlideOut(BattleSystem *battleSys, int battler);
// From the battle-start party gauge commands: BW2's balls show in place of Platinum's. Mid-battle gauges are skipped.
void BwAdapter_OnShowPartyGauge(BattleSystem *battleSys, int battler, const u8 *status);
void BwAdapter_OnHidePartyGauge(BattleSystem *battleSys, int battler);
BOOL BwAdapter_IsBusy(void);
enum BwAbilityMessage {
    BW_ABILITY_MESSAGE_NONE,
    BW_ABILITY_MESSAGE_TEXT,
    BW_ABILITY_MESSAGE_POPUP_ONLY,
};
// Queues the ability banner and rewrites redundant announcements to their effect text, for BW2 presentation only.
enum BwAbilityMessage BwAdapter_PrepareAbilityMessage(BattleSystem *battleSys, BattleMessage *message);
BOOL BwAdapter_IsAbilityPopupBusy(void);
void BwAdapter_ReleaseAbilityPopups(void);
// An activation with no resulting text (for example Intimidate against an already minimal Attack stat).
void BwAdapter_OnAbilityActivated(BattleSystem *battleSys, int battler, int ability);
// From the battle script's catch task: plays BW2's ball throw at a wild Pokémon in place of Platinum's, with the
// shakes (up to 3) and the outcome Platinum worked out, or the ball a trainer blocks. TRUE if the view took it; the
// task then waits while BwAdapter_IsBusy. BwAdapter_CanStartCatch says beforehand whether it will
BOOL BwAdapter_CanStartCatch(void);
BOOL BwAdapter_StartCatch(BattleSystem *battleSys, int battler, u16 ball, int shakes, BOOL caught, BOOL blocked);
// After the message of a catch: BW2's effect, which waits there, plays to its end (BwAdapter_IsBusy meanwhile)
void BwAdapter_FinishCatch(void);
// From BattleDisplay_PlayMoveAnimation: plays a move's animation with BW2's effects, TRUE if the view took it. The
// task that waits for the animation then waits with BwAdapter_UpdateMoveAnimation, TRUE while it plays
BOOL BwAdapter_StartMoveAnimation(BattleSystem *battleSys, MoveAnimation *animation);
BOOL BwAdapter_IsMoveAnimationPending(void);
BOOL BwAdapter_UpdateMoveAnimation(void);
// From BattleSystem_SetupBattleScreen: Platinum's screen without its terrain (the caught Pokémon on the nickname
// prompt) is Platinum's to draw, and the view waits until the battle screen is set up again
void BwAdapter_SetSuspended(BOOL suspended);
// From the battle's draw task. Returns TRUE if BW2's view drew the 3D scene in place of Platinum's sprites
BOOL BwAdapter_Draw(BattleSystem *battleSys);
void BwAdapter_VBlank(BattleSystem *battleSys);

#endif // BWBATTLE_BW_ADAPTER_H
