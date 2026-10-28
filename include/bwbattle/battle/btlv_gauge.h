#ifndef POKEBW2_BATTLE_BTLV_GAUGE_H
#define POKEBW2_BATTLE_BTLV_GAUGE_H

// Overlay 168's btlv_gauge.c (named by its string), the battle view's gauges: each battler's name box with its level,
// HP bar and numbers, EXP bar, status icon and caught mark, and the pinch music. The names are ours; swan has none for
// this file

#include "types.h"
#include "gfl/clact.h"
#include "gfl/heap.h"
#include "struct_decls.h"

// The battle view's positions, which index the gauges: 0 and 1 in single battles, 2 to 5 in double battles and 2 to 7
// in triple and rotation battles, the player's even. An enum, which MWCC doesn't propagate constants into, so some
// loops over the gauges test their bound before the first pass
typedef enum {
    BTLV_GAUGE_POS_MAX = 8,
} BtlvGaugePos;

// mode 2 shows no gauges and only keeps the statuses
BtlvGauge *BtlvGauge_Create(Font *font, u32 mode, HeapID heapId);
void BtlvGauge_Delete(BtlvGauge *gauge);
void BtlvGauge_Update(BtlvGauge *gauge);
// type is the battle's style, BTL_STYLE_*; index the gauge, by view position, even for the player's side
void BtlvGauge_SetBattleMon(BtlvGauge *gauge, BtlMainModule *mainModule, BattleMon *mon, u32 type, int index);
void BtlvGauge_SetPartyPkm(BtlvGauge *gauge, PokeDexSave *pokedex, PartyPkm *pkm, u32 type, int index);
u32 BtlvGauge_PaletteFile(void);
void BtlvGauge_Hide(BtlvGauge *gauge, int index);
void BtlvGauge_SetPos(BtlvGauge *gauge, int index, const ClActorPos *offset);
void BtlvGauge_StartHpChange(BtlvGauge *gauge, int index, s32 hp);
void BtlvGauge_SetHpAtOnce(BtlvGauge *gauge, int index, s32 hp);
void BtlvGauge_StartExpGain(BtlvGauge *gauge, int index, s32 exp);
#ifdef BWBATTLE
// Mirror an already animated healthbox, with one damage blink when its drain finishes.
void BtlvGauge_SyncHp(BtlvGauge *gauge, int index, s32 hp, BOOL moving);
void BtlvGauge_SetExpAtOnce(BtlvGauge *gauge, int index, s32 exp);
#endif
void BtlvGauge_StartLevelUp(BtlvGauge *gauge, BattleMon *mon, int index);
BOOL BtlvGauge_IsBusy(BtlvGauge *gauge);
// side 2 is both sides
void BtlvGauge_SetSideVisible(BtlvGauge *gauge, BOOL visible, u32 side);
void BtlvGauge_SetVisible(BtlvGauge *gauge, BOOL visible, int index);
BOOL BtlvGauge_IsShown(BtlvGauge *gauge, int index);
BOOL BtlvGauge_CheckChanged(BtlvGauge *gauge, int index);
void BtlvGauge_SetStatus(BtlvGauge *gauge, u32 status, int index);
// index 8 stops every gauge's shake without starting one
void BtlvGauge_StartShake(BtlvGauge *gauge, int index);
BOOL BtlvGauge_IsPinch(BtlvGauge *gauge);
void BtlvGauge_SetPinch(BtlvGauge *gauge, BOOL pinch);
void BtlvGauge_SetBgm(BtlvGauge *gauge, u32 bgm);
void BtlvGauge_SetBgmReplayed(BtlvGauge *gauge, BOOL replayed);
void BtlvGauge_SetNoPinchBgm(BtlvGauge *gauge, BOOL noPinchBgm);
BOOL BtlvGauge_GetStatus(BtlvGauge *gauge, int index, u32 *color, u32 *status);
void BtlvGauge_RequestNumberToggle(BtlvGauge *gauge);
void BtlvGauge_HideStatus(BtlvGauge *gauge, int index);

#endif // POKEBW2_BATTLE_BTLV_GAUGE_H
