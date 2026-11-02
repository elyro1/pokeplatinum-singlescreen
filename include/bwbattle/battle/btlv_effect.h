#ifndef POKEBW2_BATTLE_BTLV_EFFECT_H
#define POKEBW2_BATTLE_BTLV_EFFECT_H

// Overlay 168's btlv_effect.c (named by its string), the battle effect manager: it creates the battle view's 3D stage,
// field, camera, Pokémon sprites, cell actors, gauges, timer and BG, runs them each frame, starts the effect scripts of
// btlv_effvm.c and keeps the tasks of the view's effects. BtlvEffect_Create is swan's name; the rest are ours

#include "types.h"
#include "battle/btl_setup.h"
#include "battle/btlv_b_gauge.h"
#include "gfl/heap.h"
#include "gfl/tcb.h"
#include "nitro/fx.h"
#include "struct_decls.h"

// The move effect func_ov167_021d3094 starts, which BtlvEffect_StartMove reads
typedef struct BtlvMoveEffectParam {
    u16 move;          // 0x00
    u32 attackerPos;   // 0x04
    u32 targetPos;     // 0x08
    u8 unk0C;          // 0x0C arg5 of func_ov167_021d3094
    u8 unk0D;          // 0x0D its arg6
    s32 unk10;         // 0x10 its arg4, PML_MoveGetParam(move, 27); BtlvEffect_StartMove keeps a byte of it
} BtlvMoveEffectParam; // size 0x14

// A value moving toward a goal, or swinging back and forth, by a step every few frames. The effect tools' work,
// 0x3c bytes, stepped by btlv_effect.c's tools
typedef struct {
    s32 type;          // 0x00  0: jump to the end, 1 and 4: move to the end, 2 and 3: swing, back to the start
    VecFx32 start;     // 0x04
    VecFx32 end;       // 0x10
    VecFx32 step;      // 0x1c
    s32 stepTime;      // 0x28  frames until the swing turns
    s32 stepTimeReset; // 0x2c
    s32 wait;          // 0x30  frames until the next step
    s32 waitReset;     // 0x34
    s32 count;         // 0x38  the swing's turns left
} BtlvEffToolMove;

// A fade of 3D textures' palettes toward a color, which BtlvEffect_UpdateTexPaletteFade (btlv_effect.c) steps; the
// stage and the field each have one
typedef struct {
    void **resources; // 0x0  the G3D resources whose palettes fade
    void **palettes;  // 0x4  a copy of each one's palette data, the fade's source
    u8 active;        // 0x8
    u8 count;         // 0x9  of resources
    u8 evy;           // 0xa  the current blend, stepped by one toward targetEvy
    u8 targetEvy;     // 0xb
    u8 wait;          // 0xc  frames left until the next step
    u8 waitFrames;    // 0xd
    u16 color;        // 0xe
} BtlvTexPaletteFade;

// The setup that the init copies, and the init and end of the view
BtlvEffectSetup *BtlvEffect_Create(u32 battleStyle, u32 battleType, const BtlFieldEnv *fieldEnv, BOOL multi,
                                   const u16 *trainerTypes, BtlMainModule *mainModule, BtlvScu *scu, HeapID heapId);
BtlvEffectSetup *BtlvEffect_CreateSetup(BtlMainModule *mainModule, BtlvScu *scu, HeapID heapId);
void BtlvEffect_Init(BtlvEffectSetup *setup, Font *font, HeapID heapId);
void BtlvEffect_Exit(void);
void BtlvEffect_Main(void);
#ifdef BWBATTLE
// bwbattle: a frame of the view's logic, without drawing (BtlvEffect_Main draws the frame after it)
void BtlvEffect_Step(BOOL tickIdle);
BOOL BtlvEffect_ShouldTickIdle(void);
BOOL BtlvEffect_StartFastWildSendOut(void);
BOOL BtlvEffect_StartFastSendOut(int viewPos, u16 effect);
#endif

// Effects started by number, at positions or for a move; one started while another runs waits in a task
void BtlvEffect_Start(u32 effNo);
void BtlvEffect_StartPos(u32 pos, u32 effNo);
void BtlvEffect_StartAtkDef(u32 atkPos, u32 defPos, u32 effNo);
void BtlvEffect_StartMove(const BtlvMoveEffectParam *param);
void BtlvEffect_Resume(void);
void BtlvEffect_StartDamage(u32 pos, u16 move);
void BtlvEffect_StartEffect23B(u8 viewPos);
void BtlvEffect_StartEffect23A(u8 pos, u16 itemNo, u8 arg2, u32 arg3, u32 arg4);
void BtlvEffect_StartEffect23D(u8 pos, u16 itemNo);
void BtlvEffect_StartChangeSprite(PartyPkm *pkm, u32 viewPos);
void BtlvEffect_ChangeSprite(PartyPkm *pkm, u32 viewPos);
void BtlvEffect_StartEffect285(u8 pos);
void BtlvEffect_StartEffect286(u8 pos);
void BtlvEffect_StartEffect26E(u8 pos);
void BtlvEffect_StartEffect26F(u8 pos);
BOOL BtlvEffect_IsBusy(void);

// The Pokémon and the trainers, by position; the trainers' from 8 on
void BtlvEffect_AddPokemon(PartyPkm *pkm, u32 viewPos);
void BtlvEffect_DelPokemon(u32 viewPos);
BOOL BtlvEffect_CheckExist(int pos);
void BtlvEffect_SetTrainer(s32 trainerType, u32 pos, fx32 x, fx32 y, fx32 z);
void BtlvEffect_DelTrainer(u32 pos);
void BtlvEffect_SetPokeAnimeSpeed(u8 viewPos, fx32 speed);

// The gauges and the ball gauges
void BtlvEffect_AddGauge(BtlMainModule *mainModule, BattleMon *mon, u32 viewPos);
void BtlvEffect_AddGaugeByPkm(PokeDexSave *pokedex, PartyPkm *pkm, u32 viewPos);
void BtlvEffect_DelGauge(u32 viewPos);
void BtlvEffect_CalcGaugeHP(u32 viewPos, s32 hp);
void BtlvEffect_CalcGaugeHPAtOnce(u32 viewPos, s32 hp);
void BtlvEffect_CalcGaugeExp(u8 pos, s32 exp, BattleMon *mon);
void BtlvEffect_CalcGaugeExpLevelUp(u8 pos, BattleMon *mon);
void BtlvEffect_SetGaugeFlag(void);
BOOL BtlvEffect_CheckExecuteGauge(void);
void BtlvEffect_SetGaugeDrawEnableBySide(BOOL visible, u32 side);
void BtlvEffect_SetGaugeDrawEnable(BOOL visible, u32 viewPos);
void BtlvEffect_SetGaugeStatus(u32 status, u8 pos);
void BtlvEffect_GaugeAction(u32 viewPos);
BOOL BtlvEffect_CheckGaugeExist(u8 pos);
BOOL BtlvEffect_GetGaugeStatus(u32 viewPos, u32 *color, u32 *status);
void BtlvEffect_AddBallGauge(const BtlvBGaugeParam *param);
void BtlvEffect_DelBallGauge(u32 side);
BOOL BtlvEffect_IsBallGaugeBusy(u32 side);

// target: 0 the stage, 1 the field, 2 both, 3 the palettes, 4 all
void BtlvEffect_StartPaletteFade(u32 target, u8 evy, u8 targetEvy, u8 wait, u16 color);
BOOL BtlvEffect_IsPaletteFading(u32 target);
// target: 0 the stage, 1 the field, 2 BG 3, 3 and 4 the stage's sides
void BtlvEffect_SetVanish(u32 target, BOOL hide);
// Turns a side of a rotation battle; mode 1 does nothing
void BtlvEffect_StartRotation(u8 mode, u32 side, u32 arg2);
void BtlvEffect_SwapPokemon(u8 pos1, u8 pos2);
s32 BtlvEffect_GetTrainerIndex(u32 pos);

// The timer
void BtlvEffect_CreateTimer(u16 gameLimitTime, u16 cmdLimitTime);
void BtlvEffect_SetTimerVisible(int which, BOOL visible, BOOL restart);
BOOL BtlvEffect_IsTimeUp(int which);

BOOL BtlvEffect_CheckExistPokemon(u8 pos);
void BtlvEffect_ZoomCamera(u32 pos, u32 mode, int frames, int wait, int brakeFrames);

// The work and its parts
BtlvEffect *BtlvEffect_GetWork(void);
BtlvCamera *BtlvEffect_GetCamera(void);
BtlvMcss *BtlvEffect_GetMcss(void);
BtlvStage *BtlvEffect_GetStage(void);
BtlvGauge *BtlvEffect_GetGauge(void);
VM *BtlvEffect_GetEffvm(void);
TCBManager *BtlvEffect_GetTCBManager(void);
PaletteFade *BtlvEffect_GetPaletteFade(void);
BtlvClact *BtlvEffect_GetClact(void);
BtlvBg *BtlvEffect_GetBg(void);
u32 BtlvEffect_GetBattleStyle(void); // the battle style
u32 BtlvEffect_GetBattleType(void);  // the battle type
BOOL BtlvEffect_IsMulti(void);
u16 BtlvEffect_GetTrainerType(u32 pos);
BtlMainModule *BtlvEffect_GetMainModule(void);
BtlvScu *BtlvEffect_GetScu(void);

// The BGM, with the gauges' pinch music
BOOL BtlvEffect_IsPinchBgm(void);
void BtlvEffect_PlayBgm(u32 bgm);
void BtlvEffect_SetNoPinchBgm(BOOL noPinchBgm);
void BtlvEffect_ReserveBgm(u32 bgm);
void BtlvEffect_ReloadField(s32 fieldParam);
BOOL BtlvEffect_IsBgmChanged(void); // whether the BGM was changed
void BtlvEffect_PlayBgmNoPinch(u32 bgm);

void BtlvEffect_SetState(u32 state);
u32 BtlvEffect_GetState(void);
void BtlvEffect_SetFlag26(u32 arg0);
u32 BtlvEffect_GetFlag26(void);
BOOL BtlvEffect_GetUnk2E4(void);
BOOL BtlvEffect_GetUnk304(void);
void BtlvEffect_SetPokemonCheck(u32 arg0);

// The effect's task slots: a task with its end function and group, its end, and the end of a group's tasks
void BtlvEffect_AddTask(TCB *tcb, void (*endFunc)(TCB *tcb), u32 group);
void BtlvEffect_EndTask(TCB *tcb);
void BtlvEffect_EndTaskGroup(u32 group);

// The effects played while the battle waits for input
void BtlvEffect_SetIdleEffectMode(u32 mode);
void BtlvEffect_StopIdleEffect(void);
void BtlvEffect_RestartIdleEffect(u32 mode);

// The abilities shown, by view position
void BtlvEffect_SetAbility(u32 viewPos, u32 ability);
u32 BtlvEffect_GetAbility(u8 viewPos);
void BtlvEffect_ReleaseVoices(void);
void BtlvEffect_ClearVoices(void);

// The effect tools at 0x021e0b44-0x021e0d54
void BtlvEffTool_CalcStep(fx32 start, fx32 end, fx32 *step, fx32 frames);
void BtlvEffTool_CalcStepVec(const VecFx32 *start, const VecFx32 *end, VecFx32 *step, fx32 frames);
void BtlvEffTool_Step(fx32 *value, fx32 *step, const fx32 *end, BOOL *done);
BOOL BtlvEffTool_Move(BtlvEffToolMove *move, VecFx32 *value);
u32 BtlvEffect_PosBit(u32 bit);
void BtlvEffect_UpdateTexPaletteFade(BtlvTexPaletteFade *fade);

#endif // POKEBW2_BATTLE_BTLV_EFFECT_H
