#ifndef POKEPLATINUM_AUTOPLAY_TEST_H
#define POKEPLATINUM_AUTOPLAY_TEST_H

// These observations and input overrides exist only in an opt-in test build.
// The simulator's independent PLATINUM_AUTOPILOT facility is untouched.
#ifdef PLATINUM_AUTOPLAY_TEST
#include "battle/battle_subscreen.h"

#include "overlay_manager.h"
#include "string_gf.h"
#include "string_list.h"

u32 AutoplayTest_ReadPad(u32 physicalPad);
void AutoplayTest_Touch(TPData *touch);
void AutoplayTest_Text(const String *text);
void AutoplayTest_Menu(int kind, int cursor, int count, const StringList *choices);
void AutoplayTest_Battle(BattleSubscreen *subscreen);
void AutoplayTest_App(const ApplicationManager *app);
void AutoplayTest_Result(int result, int battleType);
void AutoplayTest_UI(const char *name, int state, int cursor, int detail);
void AutoplayTest_PartyOrder(const u8 *slots);
#else
#define AutoplayTest_ReadPad(pad)                       (pad)
#define AutoplayTest_Touch(touch)                       ((void)0)
#define AutoplayTest_Text(text)                         ((void)0)
#define AutoplayTest_Menu(kind, cursor, count, choices) ((void)0)
#define AutoplayTest_Battle(subscreen)                  ((void)0)
#define AutoplayTest_App(app)                           ((void)0)
#define AutoplayTest_Result(result, battleType)         ((void)0)
#define AutoplayTest_UI(name, state, cursor, detail)    ((void)0)
#define AutoplayTest_PartyOrder(slots)                  ((void)0)
#endif

#endif
