// The battle view's Pokémon sprites, drawn by MCSS. The name is the ROM's string, from GFL_HeapAllocate's asserts. The
// names are ours; swan has none for this file

#include "battle/btlv_mcss.h"
#include "types.h"
#include "battle/btl_main.h"
#include "battle/btlv_effect.h"
#include "battle/btlv_effvm.h"
#include "battle/btlv_gauge.h"
#include "battle/btlv_stage.h"
#include "constants/battle.h"
#include "constants/pokemon.h"
#include "constants/sound.h"
#include "constants/species.h"
#include "gfl/heap.h"
#include "gfl/random.h"
#include "gfl/sound.h"
#include "gfl/tcb.h"
#include "nitro/fx.h"
#include "nitro/gx.h"
#include "nnsys/g2d.h"
#include "pml/personal.h"
#include "pml/poke_graphic.h"
#include "pml/poke_party.h"
#include "system/hp_gauge.h"
#include "system/mcss.h"

// The data of the Pokémon a sprite shows, copied whole when a Pokémon transforms
typedef struct {
    u32 species;     // 0x00
    u32 form;        // 0x04
    u32 personality; // 0x08
    u16 weight;      // 0x0c
    u16 hpColor;     // 0x0e  HPGauge_GetColor's
} BtlvMcssMon;

// A sprite, 0x5c bytes
typedef struct {
    MCSS *mcss;         // 0x00
    MCSSLoadInfo info;  // 0x04
    TCB *idleTask;      // 0x28  BtlvMcss_IdleTask, the idle animation
    BtlvMcssMon mon;    // 0x2c
    u32 ball;           // 0x3c
    u32 flags;          // 0x40  BTLV_MCSS_FLAG_*
    BOOL animSet;       // 0x44  an animation was set and hasn't ended
    s32 pos;            // 0x48  0xff for none
    u32 frozen : 1;     // 0x4c  the animation was stopped for good (freeze)
    u32 statusAnim : 1; //       a status condition's animation runs
    u32 flat : 1;       //       BtlvMcss.flat when added
    u32 idleCount : 2;  //       the chance of an idle animation when the animation ends goes up as it counts down
    u32 hidden : 1;     //       hidden by BtlvMcss_SetVanish's mode 3, shown back by mode 4
    u32 moved : 1;      //       BtlvMcss_ResetPosition put the sprite back
    u32 fadeDone : 1;   //       the palette fade has ended
    u32 fading : 1;     //       BtlvMcss_StartPaletteFade started a palette fade
    u32 changed : 1;    //       BtlvMcss_ChangePokemon changed the Pokémon
    u32 vanished : 1;   //       saved by BtlvMcss_SaveVanish
    u32 animEnded : 1;  //       restart the animation next frame
    u32 shadow : 1;     //       BtlvMcss.shadows when added
    s32 statusEvy;      // 0x50  the status condition's palette blend, swinging between 0 and 12
    s32 statusEvyStep;  // 0x54
    s32 statusTimer;    // 0x58  frames until the next step
} BtlvMcssEntry;

// The sprites' work, 0x5b4 bytes. Each kind of task keeps a mask of the positions it runs on, and a counter by position
// that a new task of the kind bumps, so that an older task on the position stops
struct BtlvMcss {
    TCBManager *tcbMgr;                       // 0x000
    MCSSSystem *mcssSys;                      // 0x004
    BtlvMcssEntry entries[BTLV_MCSS_POS_MAX]; // 0x008  slots, found by their pos
    u32 triple : 1;                           // 0x510
    u32 rotation : 1;                         //
    u32 rotating : 1;                         //        BtlvMcss_StartRotation's task runs
    u32 shadows : 1;                          //
    u32 moveTasks;                            // 0x514  the position, a circle or a shake
    u32 unk518Tasks;                          // 0x518  func_0201abb8's vector
    u32 unk51cTasks;                          // 0x51c  func_0201abf0's vector
    u32 scaleTasks;                           // 0x520
    u32 unk524Tasks;                          // 0x524  func_0201ac40's vector
    u32 blinkTasks;                           // 0x528
    u32 alphaTasks;                           // 0x52c
    u32 levelTasks;                           // 0x530  func_0201af54's level
    u32 flat;                                 // 0x534
    fx32 scaleNear;                           // 0x538  the even positions' scale
    fx32 scaleFar;                            // 0x53c  the odd positions'
    HeapID heapId;                            // 0x540
    u8 moveSeq[BTLV_MCSS_POS_MAX];            // 0x542
    u8 unk518Seq[BTLV_MCSS_POS_MAX];          // 0x550
    u8 scaleSeq[BTLV_MCSS_POS_MAX];           // 0x55e
    u8 unk524Seq[BTLV_MCSS_POS_MAX];          // 0x56c
    u8 unk51cSeq[BTLV_MCSS_POS_MAX];          // 0x57a
    u8 blinkSeq[BTLV_MCSS_POS_MAX];           // 0x588
    u8 alphaSeq[BTLV_MCSS_POS_MAX];           // 0x596
    u8 levelSeq[BTLV_MCSS_POS_MAX];           // 0x5a4
};

// A move of a value of a sprite toward a goal, or a swing, or a blink, 0x54 bytes
typedef struct {
    BtlvMcss *work;       // 0x00
    s32 pos;              // 0x04
    VecFx32 value;        // 0x08
    BtlvEffToolMove move; // 0x14
    u32 seq;              // 0x50
} BtlvMcssMove;

// The rotation of a side in a rotation battle, 0x24 bytes
typedef struct {
    BtlvMcss *work; // 0x00
    u32 seq : 31;   // 0x04
    u32 playSe : 1; //
    s32 side;       // 0x08
    s32 dir;        // 0x0c
    s32 angle[3];   // 0x10
    s32 frames;     // 0x1c
    s32 speed;      // 0x20
} BtlvMcssRotate;

// The idle animation of a sprite, 0xc bytes
typedef struct {
    s32 seq;   // 0x0
    s32 index; // 0x4
    s32 wait;  // 0x8
} BtlvMcssIdle;

// The shadow of a species' sprite, where it differs
typedef struct {
    u16 species : 14;
    u16 enemy : 1;  // the front sprite's, otherwise the back sprite's
    u16 shadow : 1; // whether it has one
    s16 x;
    s16 z;
    u16 unk6;
} BtlvMcssShadow;

static void BtlvMcss_SetAnimStop(BtlvMcss *work, int pos, int mode);
static BOOL BtlvMcss_IsAnimStopped(BtlvMcss *work, int pos);
static void BtlvMcss_SetPaletteBlend(BtlvMcss *work, int pos, u8 evy, GXRgb color);
static void BtlvMcss_ResetPaletteBlend(BtlvMcss *work, int pos);
static void BtlvMcss_SetLevel(BtlvMcss *work, int pos, int level);
static void BtlvMcss_SetupTrainerLoadInfo(u32 trainerType, MCSSLoadInfo *info, int pos);
static void BtlvMcss_UpdateScale(BtlvMcss *work, int pos);
static void BtlvMcss_StartMoveTask(BtlvMcss *work, int pos, int type, VecFx32 *start, VecFx32 *end, s32 frames,
                                   s32 wait, s32 count, TCBFunc func, void (*endFunc)(TCB *tcb), BOOL mirror, u8 seq);
static void BtlvMcss_MoveTask_Position(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Position(TCB *tcb);
static void BtlvMcss_MoveTask_Vec518(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Vec518(TCB *tcb);
static void BtlvMcss_MoveTask_Scale(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Scale(TCB *tcb);
static void BtlvMcss_MoveTask_Vec524(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Vec524(TCB *tcb);
static void BtlvMcss_MoveTask_Vec51c(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Vec51c(TCB *tcb);
static void BtlvMcss_MoveTask_Blink(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Blink(TCB *tcb);
static void BtlvMcss_MoveTask_Alpha(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Alpha(TCB *tcb);
static void BtlvMcss_MoveTask_Circle(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Circle(TCB *tcb);
static void BtlvMcss_MoveTask_Shake(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Shake(TCB *tcb);
static void BtlvMcss_MoveTask_Level(TCB *tcb, void *data);
static void BtlvMcss_MoveTaskEnd_Level(TCB *tcb);
static void BtlvMcss_IdleTask(TCB *tcb, void *data);
static void BtlvMcss_RotationTask(TCB *tcb, void *data);
static void BtlvMcss_RotationTaskEnd(TCB *tcb);
static void BtlvMcss_OnAnimationEnd(u32 index, fx32 frame);
static BOOL BtlvMcss_IdleNodeCallback(u32 param, const NNSG2dMultiCellHierarchyData *node,
                                      NNSG2dCellAnimation *cellAnim, u16 nodeIdx);
static void BtlvMcss_OnSetAnimationEnd(u32 index, fx32 frame);
static void BtlvMcss_GetDefaultPosImpl(BtlvMcss *work, VecFx32 *out, int pos);
static fx32 BtlvMcss_GetDefaultScaleImpl(BtlvMcss *work, int pos, BOOL flat);
static int BtlvMcss_GetIndex(BtlvMcss *work, int pos);
static BOOL BtlvMcss_GetShadowOffset(u32 species, int pos, VecFx32 *offset, u16 *unk6, BOOL *shadow);

// The cries' pan, the sprites' scale and their positions, by position: singles from 0, doubles, triples and rotation
// from 2, and the trainers from 8, as BtlvMcss_PlayCry, BtlvMcss_GetDefaultScaleImpl and BtlvMcss_GetDefaultPosImpl
// pick them. The declaration order lays them out as in the ROM
static const s32 data_ov168_021f31ec[] = { 20, 107 };
static const fx32 data_ov168_021f31f4[] = { 0x1030, 0x11bf };
static const fx32 data_ov168_021f31fc[] = { 0x11b8, 0x118e, 0xe6a, 0x1320 };
static const s32 data_ov168_021f320c[] = { 12, 115, 28, 99 };
static const fx32 data_ov168_021f324c[] = { 0x1030, 0x1300, 0xf00, 0x1300, 0xd00, 0x1300 };
static const s32 data_ov168_021f321c[] = { 12, 115, 28, 99, 20, 107 };
static const VecFx32 data_ov168_021f32ac[] = {
    { -0x1b00, 0x666, 0x6600 },
    { 0x26cd, 0x666, -0xa600 },
    { 0x2800, 0x666, 0x7a00 },
    { -0x2333, 0x666, -0xb900 },
};
static const fx32 data_ov168_021f3234[] = { 0x1040, 0x109c, 0xd80, 0x1440, 0xc00, 0x1610 };
static const fx32 data_ov168_021f3264[] = { 0x11f0, 0x11f0, 0x1270, 0x110e, 0xec4, 0x13f0 };
static const VecFx32 data_ov168_021f327c[] = {
    { 0x800, 0x666, 0x7000 },
    { 0x4cd, 0x666, -0xa000 },
};
static const s32 data_ov168_021f3294[] = { 0, 127, 20, 107, 40, 87 };
static const VecFx32 data_ov168_021f33cc[] = {
    { 0, 0x666, 0x7000 },       { 0, 0x666, -0x8000 },     { -0x454c, 0x666, 0xe801 },
    { 0x454a, 0x666, -0xf801 }, { 0x454a, 0x666, 0xe801 }, { -0x454c, 0x666, -0xf801 },
};
static const VecFx32 data_ov168_021f333c[] = {
    { 0x800, 0, 0x7000 },       { 0, 0x666, -0xc000 }, { 0, 0, 0x8800 },
    { 0x26cd, 0x666, -0xc000 }, { 0x3585, 0, 0x9000 }, { -0x2333, 0x666, -0xc000 },
};
static const VecFx32 data_ov168_021f3384[] = {
    { -0x4000, 0x666, 0x7000 }, { 0x4800, 0x666, -0xc000 }, { 0xc00, 0x666, 0x4c00 },
    { 0xbcd, 0x666, -0x9000 },  { 0x5300, 0x666, 0x6300 },  { -0x4633, 0x666, -0xc000 },
};

static const BtlvMcssShadow data_ov168_021f3414[] = {
    { SPECIES_RATTATA, 1, 1, 0x0, -0x1200, 0xee00 },       { SPECIES_PARASECT, 0, 1, -0x400, -0x400, 0xfc00 },
    { SPECIES_DIGLETT, 1, 0, -0x400, -0x400, 0xfc00 },     { SPECIES_DUGTRIO, 1, 0, -0x400, -0x400, 0xfc00 },
    { SPECIES_GROWLITHE, 1, 1, 0x0, -0x1200, 0xee00 },     { SPECIES_RHYHORN, 0, 1, -0x400, -0x600, 0xf400 },
    { SPECIES_TAUROS, 1, 1, 0x0, -0x1200, 0xee00 },        { SPECIES_SPINARAK, 1, 1, 0x0, -0x1200, 0xee00 },
    { SPECIES_SKARMORY, 1, 1, -0x400, -0x600, 0xfe00 },    { SPECIES_ENTEI, 0, 1, -0x400, -0x900, 0xf400 },
    { SPECIES_GROVYLE, 1, 1, -0x400, -0xc00, 0x100 },      { SPECIES_SCEPTILE, 1, 1, -0x400, -0xc00, 0x100 },
    { SPECIES_SCEPTILE, 0, 1, -0x400, -0xa00, 0xf000 },    { SPECIES_BLAZIKEN, 1, 1, -0x800, -0x1400, 0x100 },
    { SPECIES_MIGHTYENA, 1, 1, 0x0, -0x1200, 0xee00 },     { SPECIES_MIGHTYENA, 0, 1, -0x400, -0x900, 0xf400 },
    { SPECIES_LINOONE, 1, 1, 0x0, -0x1200, 0xee00 },       { SPECIES_SURSKIT, 1, 1, -0x400, -0x1800, 0xf800 },
    { SPECIES_ARON, 1, 1, 0x0, -0x1200, 0xf000 },          { SPECIES_LAIRON, 1, 1, 0x0, -0x1200, 0xee00 },
    { SPECIES_SWALOT, 1, 1, 0x0, -0x1200, 0xee00 },        { SPECIES_ABSOL, 1, 1, 0x0, -0x1a00, 0xee00 },
    { SPECIES_SALAMENCE, 0, 1, -0x400, -0xa00, 0xf000 },   { SPECIES_METAGROSS, 1, 1, -0x800, -0x1400, 0x100 },
    { SPECIES_GROTLE, 1, 1, 0x0, -0x1600, 0xee00 },        { SPECIES_GROTLE, 0, 1, -0x400, -0x900, 0xf400 },
    { SPECIES_TORTERRA, 1, 1, 0x0, -0x1600, 0xee00 },      { SPECIES_TORTERRA, 0, 1, -0x400, -0x900, 0xf400 },
    { SPECIES_INFERNAPE, 1, 1, -0x800, -0x1400, 0x100 },   { SPECIES_HIPPOWDON, 1, 0, -0x400, -0x400, 0xfc00 },
    { SPECIES_HIPPOWDON, 0, 0, -0x400, -0x400, 0xfc00 },   { SPECIES_ELECTIVIRE, 1, 1, -0x400, -0xc00, 0x100 },
    { SPECIES_MAGMORTAR, 1, 1, -0x400, -0xc00, 0x100 },    { SPECIES_DIALGA, 1, 1, -0x400, -0x1000, 0xfc00 },
    { SPECIES_HEATRAN, 1, 1, 0x0, -0x2000, 0xf800 },       { SPECIES_HEATRAN, 0, 1, -0x400, -0x900, 0xf400 },
    { SPECIES_SAMUROTT, 1, 1, -0x400, -0x1000, 0xfa00 },   { SPECIES_SAMUROTT, 0, 1, -0x400, -0x900, 0xf400 },
    { SPECIES_LIEPARD, 1, 1, -0x400, -0x1000, 0xf800 },    { SPECIES_UNFEZANT, 1, 1, -0x400, -0xc00, 0x100 },
    { SPECIES_ZOROARK, 1, 1, -0x800, -0x1400, 0x100 },     { SPECIES_GALVANTULA, 1, 1, -0x400, -0x1000, 0xfc00 },
    { SPECIES_HAXORUS, 1, 1, -0x400, -0x1c00, 0xf800 },    { SPECIES_MIENFOO, 1, 1, 0x0, -0x1200, 0xee00 },
    { SPECIES_BOUFFALANT, 1, 1, -0x400, -0x1600, 0xfa00 }, { SPECIES_RESHIRAM, 1, 1, -0x400, -0xc00, 0x100 },
    { SPECIES_RESHIRAM, 0, 1, -0x400, -0xa00, 0xf600 },    { SPECIES_ZEKROM, 1, 1, -0x400, -0xc00, 0x100 },
};

// The frames an idle animation waits for
static u8 data_ov168_021f4184[] = { 0x30, 0x38, 0x40 };

BtlvMcss *BtlvMcss_Create(u32 battleStyle, TCBManager *tcbMgr, HeapID heapId) {
    BtlvMcss *work = GFL_HeapAllocate(heapId, sizeof(BtlvMcss), TRUE, "btlv_mcss.c", 0x186);
    int i;

    work->mcssSys = MCSSSys_Create(BTLV_MCSS_POS_MAX, heapId);
    work->tcbMgr = tcbMgr;
    work->heapId = heapId;
    func_0201b28c(work->mcssSys, 0x100);
    func_0201b290(work->mcssSys, TRUE);
    work->flat = TRUE;
    work->scaleNear = FX32_ONE;
    work->scaleFar = FX32_ONE;
    work->triple = battleStyle == BTL_STYLE_TRIPLE;
    work->rotation = battleStyle == BTL_STYLE_ROTATION;
    for (i = 0; i < BTLV_MCSS_POS_MAX; i++) {
        work->entries[i].pos = 0xff;
    }
    return work;
}

void BtlvMcss_Delete(BtlvMcss *work) {
    BtlvMcssPos pos;

    for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_MAX; pos++) {
        if (BtlvMcss_Exists(work, pos)) {
            BtlvMcss_Remove(work, pos);
        }
    }
    MCSSSys_Free(work->mcssSys);
    GFL_HeapFree(work);
}

void BtlvMcss_Main(BtlvMcss *work) {
    BtlvMcssPos pos;
    int index;
    u32 unk;
    u32 status;

    for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_TRAINER; pos++) {
        if (BtlvMcss_Exists(work, pos)) {
            int i = BtlvMcss_GetIndex(work, pos);

#ifdef BWBATTLE
            // Scripted sprite animations keep Quick pacing; the normal living sprite keeps Smooth timing.
            MCSS_SetSkipAnimationTick(work->entries[i].mcss,
                                      !BtlvEffect_ShouldTickIdle() && !work->entries[i].animSet);
#endif
            if (work->entries[i].animEnded) {
                work->entries[i].animEnded = FALSE;
                MCSS_RestartAnimation(work->entries[i].mcss);
            }
        }
    }
    MCSSSys_Update(work->mcssSys);
    if (!work->rotating) {
        for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_TRAINER; pos++) {
            BOOL step;

            if (BtlvMcss_Exists(work, pos)
#ifdef BWBATTLE
                && BtlvEffect_ShouldTickIdle()
#endif
                && GFL_RandomMTRange(100) == 0 &&
                !(BtlvEffect_PosBit(pos) & work->blinkTasks) && !BtlvMcss_IsAnimStopped(work, pos)) {
                BtlvMcss_Blink(work, pos, 2, 4, 1);
            }
            if (!BtlvMcss_Exists(work, pos)) {
                continue;
            }
            index = BtlvMcss_GetIndex(work, pos);
            step = FALSE;
            if (BtlvEffect_IsBusy() && work->entries[index].fading) {
                work->entries[index].fading = FALSE;
                work->entries[index].fadeDone = TRUE;
            }
            if (BtlvEffect_IsBusy() && work->entries[index].fadeDone) {
                continue;
            }
            work->entries[index].fadeDone = FALSE;
            if (BtlvMcss_GetFlags(work, pos) & BTLV_MCSS_FLAG_SUBSTITUTE) {
                continue;
            }
            if (!BtlvEffect_GetGaugeStatus(pos, &unk, &status)) {
                continue;
            }
            if (work->entries[index].statusAnim == TRUE) {
                if (work->entries[index].statusEvyStep == 0) {
                    work->entries[index].statusEvyStep = 1;
                }
                if (work->entries[index].statusTimer++ > 24) {
                    step = TRUE;
                    work->entries[index].statusTimer = 0;
                    work->entries[index].statusEvy += work->entries[index].statusEvyStep;
                    if (work->entries[index].statusEvy == 0 || work->entries[index].statusEvy == 12) {
                        work->entries[index].statusEvyStep *= -1;
                    }
                }
            }
            switch (status) {
            case 3:
                BtlvMcss_ResetPaletteBlend(work, pos);
                BtlvMcss_SetAnimStop(work, pos, 3);
                BtlvMcss_SetAnimPause(work, pos, 3);
                BtlvMcss_SetAnimSpeed(work, pos, 0x555);
                work->entries[index].statusAnim = TRUE;
                break;
            case 1:
                if (step == TRUE) {
                    BtlvMcss_SetPaletteBlend(work, pos, work->entries[index].statusEvy, GX_RGB(15, 15, 0));
                }
                BtlvMcss_SetAnimStop(work, pos, 4);
                BtlvMcss_SetAnimPause(work, pos, 3);
                work->entries[index].statusAnim = TRUE;
                break;
            case 2:
                if (step == TRUE) {
                    BtlvMcss_SetPaletteBlend(work, pos, 8, GX_RGB(15, 15, 31));
                }
                BtlvMcss_SetAnimStop(work, pos, 4);
                BtlvMcss_SetAnimPause(work, pos, 2);
                work->entries[index].statusAnim = TRUE;
                break;
            case 5:
                if (step == TRUE) {
                    BtlvMcss_SetPaletteBlend(work, pos, work->entries[index].statusEvy, GX_RGB(15, 0, 0));
                }
                BtlvMcss_SetAnimStop(work, pos, 4);
                BtlvMcss_SetAnimPause(work, pos, 3);
                work->entries[index].statusAnim = TRUE;
                break;
            case 4:
            case 7:
                if (step == TRUE) {
                    BtlvMcss_SetPaletteBlend(work, pos, work->entries[index].statusEvy, GX_RGB(15, 0, 15));
                }
                BtlvMcss_SetAnimStop(work, pos, 4);
                BtlvMcss_SetAnimPause(work, pos, 3);
                work->entries[index].statusAnim = TRUE;
                break;
            case 8:
            default:
                if (work->entries[index].statusAnim == TRUE) {
                    BtlvMcss_ResetPaletteBlend(work, pos);
                    BtlvMcss_SetAnimStop(work, pos, 4);
                    BtlvMcss_SetAnimPause(work, pos, 3);
                    work->entries[index].statusAnim = FALSE;
                    work->entries[index].statusEvyStep = 0;
                    work->entries[index].statusEvy = 0;
                    work->entries[index].statusTimer = 0;
                }
                break;
            }
            if (status == 8) {
                if (unk == 2 || unk == 3) {
                    BtlvMcss_SetAnimSpeed(work, pos, 0x555);
                } else {
                    BtlvMcss_SetAnimSpeed(work, pos, FX32_ONE);
                }
            }
        }
    }
}

void BtlvMcss_Draw(BtlvMcss *work) {
    MCSSSys_Draw(work->mcssSys);
}

void BtlvMcss_AddPokemon(BtlvMcss *work, PartyPkm *pkm, int pos) {
    int index;
    BOOL shadow;
    u16 unk6;
    VecFx32 pos3d;
    VecFx32 offset;
    u32 hp;

    for (index = 0; index < BTLV_MCSS_POS_MAX; index++) {
        if (work->entries[index].mcss == NULL) {
            break;
        }
    }
    if (pos >= BTLV_MCSS_POS_MAX || index >= BTLV_MCSS_POS_MAX) {
        return;
    }
    work->entries[index].flags = 0;
    work->entries[index].frozen = FALSE;
    work->entries[index].statusAnim = FALSE;
    work->entries[index].hidden = FALSE;
    work->entries[index].flat = work->flat;
    work->entries[index].shadow = work->shadows;
    work->entries[index].pos = pos;
    work->entries[index].mon.personality = PokeParty_GetParam(pkm, PKM_PARAM_PID, NULL);
    work->entries[index].ball = PokeParty_GetParam(pkm, PKM_PARAM_POKEBALL, NULL);
    if (PokeParty_IsRare(pkm) == TRUE) {
        work->entries[index].flags |= BTLV_MCSS_FLAG_RARE;
    }
    if (PokeParty_GetParam(pkm, PKM_PARAM_N_POKEMON, NULL) == TRUE) {
        work->entries[index].flags |= BTLV_MCSS_FLAG_N_POKEMON;
    }
    if (PML_GetPokestarFameLevel(PokeParty_GetParam(pkm, PKM_PARAM_POKESTAR_FAME, NULL)) >= 4) {
        work->entries[index].flags |= BTLV_MCSS_FLAG_FAMOUS;
    }
    BtlvMcss_SetupPokemonLoadInfo(work, pkm, &work->entries[index].info, pos);
    BtlvMcss_GetDefaultPosImpl(work, &pos3d, pos);
    work->entries[index].mcss = MCSSSys_Add(work->mcssSys, pos3d.x, pos3d.y, pos3d.z, &work->entries[index].info);
    BtlvMcss_SetUnkB270(work, pos, TRUE);
    work->entries[index].mon.species = PokeParty_GetParam(pkm, PKM_PARAM_SPECIES, NULL);
    work->entries[index].mon.form = PokeParty_GetParam(pkm, PKM_PARAM_FORM, NULL);
    work->entries[index].mon.weight =
        PML_PersonalGetParamSingle(work->entries[index].mon.species, work->entries[index].mon.form, PERSONAL_WEIGHT);
    hp = PokeParty_GetParam(pkm, PKM_PARAM_HP, NULL);
    work->entries[index].mon.hpColor = HPGauge_GetColor(hp, PokeParty_GetParam(pkm, PKM_PARAM_MAX_HP, NULL));
    shadow = FALSE;
    if (!BtlvMcss_GetShadowOffset(work->entries[index].mon.species, pos, &offset, &unk6, &shadow)) {
        offset.x = -0x400;
        offset.y = 0;
        offset.z = -0x600;
        unk6 = 0xf800;
        shadow = TRUE;
    }
    if (!shadow) {
        func_0201aeb0(work->entries[index].mcss, 0);
    } else {
        func_0201ac70(work->entries[index].mcss, &offset);
        func_0201ac64(work->entries[index].mcss, unk6);
    }
    work->entries[index].idleCount = 3;
    BtlvMcss_UpdateScale(work, pos);
    MCSS_SetAnimationEndCallback(work->entries[index].mcss, index, BtlvMcss_OnAnimationEnd, 1);
}

void BtlvMcss_AddTrainer(BtlvMcss *work, u32 trainerType, int pos) {
    int index;
    MCSSLoadInfo info;
    VecFx32 pos3d;

    for (index = 0; index < BTLV_MCSS_POS_MAX; index++) {
        if (work->entries[index].mcss == NULL) {
            break;
        }
    }
    if (pos < BTLV_MCSS_POS_MAX && index < BTLV_MCSS_POS_MAX) {
        work->entries[index].flat = TRUE;
        work->entries[index].pos = pos;
        work->entries[index].shadow = work->shadows;
        BtlvMcss_SetupTrainerLoadInfo(trainerType, &info, pos);
        BtlvMcss_GetDefaultPosImpl(work, &pos3d, pos);
        work->entries[index].mcss = MCSSSys_Add(work->mcssSys, pos3d.x, pos3d.y, pos3d.z, &info);
        BtlvMcss_UpdateScale(work, pos);
        BtlvMcss_SetShadowVanish(work, pos, work->entries[index].shadow ^ 1);
    }
}

#ifdef BWBATTLE
void BtlvMcss_SetTrainerBackSprite(BtlvMcss *work, int pos, BwTrainerBack trainer) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index != -1 && work->entries[index].mcss != NULL) {
        MCSS_SetTrainerBackSprite(work->entries[index].mcss, trainer);
    }
}

void BtlvMcss_SetImage(BtlvMcss *work, int pos, const u8 *image, u32 width, u32 height, int anchorX, int anchorY,
    const u16 *colors, fx32 scale) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index != -1 && work->entries[index].mcss != NULL) {
        MCSS_SetImage(work->entries[index].mcss, image, width, height, anchorX, anchorY, colors, scale);
    }
}
#endif

void BtlvMcss_Remove(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    MCSSSys_Remove(work->mcssSys, work->entries[index].mcss);
    work->entries[index].mcss = NULL;
    if (work->entries[index].idleTask != NULL) {
        GFL_HeapFree(GFL_TCBGetData(work->entries[index].idleTask));
        GFL_TCBRemove(work->entries[index].idleTask);
        work->entries[index].idleTask = NULL;
    }
}

void BtlvMcss_SetSideScale(BtlvMcss *work, fx32 scaleNear, fx32 scaleFar) {
    work->scaleNear = scaleNear;
    work->scaleFar = scaleFar;
}

void BtlvMcss_SetPosition(BtlvMcss *work, int pos, fx32 x, fx32 y, fx32 z) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 pos3d;

    pos3d.x = x;
    pos3d.y = y;
    pos3d.z = z;
    MCSS_SetPosition(work->entries[index].mcss, &pos3d);
}

void BtlvMcss_SetFlatAll(BtlvMcss *work) {
    int pos;
    int index;

    func_0201aacc(work->mcssSys);
    work->flat = TRUE;
    for (pos = 0; pos < BTLV_MCSS_POS_MAX; pos++) {
        index = BtlvMcss_GetIndex(work, pos);
        if (index != -1) {
            work->entries[index].flat = TRUE;
            BtlvMcss_UpdateScale(work, pos);
        }
    }
}

void BtlvMcss_ClearFlatAll(BtlvMcss *work) {
    int pos;
    int index;

    func_0201aadc(work->mcssSys);
    work->flat = FALSE;
    for (pos = 0; pos < BTLV_MCSS_POS_MAX; pos++) {
        index = BtlvMcss_GetIndex(work, pos);
        if (index != -1) {
            work->entries[index].flat = FALSE;
            BtlvMcss_UpdateScale(work, pos);
        }
    }
}

void BtlvMcss_SetFlat(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index != -1) {
        work->entries[index].flat = TRUE;
        func_0201aae8(work->entries[index].mcss);
        BtlvMcss_UpdateScale(work, pos);
    }
}

void BtlvMcss_ClearFlat(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index != -1) {
        work->entries[index].flat = FALSE;
        func_0201aaf8(work->entries[index].mcss);
        BtlvMcss_UpdateScale(work, pos);
    }
}

static void BtlvMcss_SetAnimStop(BtlvMcss *work, int pos, int mode) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    if (mode == 4) {
        func_0201ac9c(work->entries[index].mcss);
        work->entries[index].frozen = FALSE;
    }
    if (work->entries[index].frozen) {
        return;
    }
    if (mode == 2) {
        func_0201acb0(work->entries[index].mcss);
    } else if (mode == 1) {
        func_0201ac8c(work->entries[index].mcss);
    } else if (mode == 3) {
        func_0201ac8c(work->entries[index].mcss);
        work->entries[index].frozen = TRUE;
    } else {
        func_0201ac9c(work->entries[index].mcss);
    }
}

static BOOL BtlvMcss_IsAnimStopped(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return FALSE;
    }
    if (work->entries[index].mcss == NULL) {
        return FALSE;
    }
    return func_0201acd4(work->entries[index].mcss);
}

void BtlvMcss_SetAnimPause(BtlvMcss *work, int pos, int mode) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    if (mode == 1) {
        MCSS_PauseAnimation(work->entries[index].mcss);
    } else if (mode == 2) {
        func_0201ad04(work->entries[index].mcss);
    } else if (mode == 3) {
        func_0201ad4c(work->entries[index].mcss);
    } else {
        MCSS_ResumeAnimation(work->entries[index].mcss);
    }
}

BOOL BtlvMcss_GetVanish(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return FALSE;
    }
    if (work->entries[index].mcss == NULL) {
        return FALSE;
    }
    return func_0201ad70(work->entries[index].mcss);
}

void BtlvMcss_SetVanish(BtlvMcss *work, int pos, int mode) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    switch (mode) {
    case 2:
        func_0201ada0(work->entries[index].mcss);
        break;
    case 1:
        MCSS_Hide(work->entries[index].mcss);
        break;
    case 0:
        MCSS_Show(work->entries[index].mcss);
        break;
    case 3:
        if (func_0201ad70(work->entries[index].mcss)) {
            work->entries[index].hidden = TRUE;
        }
        MCSS_Hide(work->entries[index].mcss);
        break;
    case 4:
        if (work->entries[index].hidden) {
            work->entries[index].hidden = FALSE;
        } else {
            MCSS_Show(work->entries[index].mcss);
        }
        break;
    }
}

void BtlvMcss_GetDefaultPos(BtlvMcss *work, VecFx32 *out, int pos) {
    BtlvMcss_GetDefaultPosImpl(work, out, pos);
}

void BtlvMcss_GetDefaultPosForStyle(BtlvMcss *work, VecFx32 *out, int pos, u32 battleStyle) {
    const VecFx32 *src;

    switch (pos) {
    case 0:
    case 1:
        src = &data_ov168_021f327c[pos];
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        if (battleStyle == BTL_STYLE_ROTATION) {
            src = &data_ov168_021f33cc[pos - 2];
        } else if (battleStyle == BTL_STYLE_TRIPLE) {
            src = &data_ov168_021f3384[pos - 2];
        } else {
            src = &data_ov168_021f32ac[pos - 2];
        }
        break;
    case 6:
    case 7:
        if (battleStyle == BTL_STYLE_ROTATION) {
            src = &data_ov168_021f33cc[pos - 2];
        } else {
            src = &data_ov168_021f3384[pos - 2];
        }
        break;
    }
    out->x = src->x;
    out->y = src->y;
    out->z = src->z;
}

fx32 BtlvMcss_GetDefaultScale(BtlvMcss *work, int pos, BOOL flat) {
    return BtlvMcss_GetDefaultScaleImpl(work, pos, flat);
}

void BtlvMcss_SetShadows(BtlvMcss *work, BOOL shadows) {
    if (work != NULL) {
        work->shadows = shadows;
    }
}

void BtlvMcss_SetShadowVanish(BtlvMcss *work, int pos, u8 value) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    if (!work->shadows) {
        value = 1;
    }
    func_0201aecc(work->entries[index].mcss, value);
}

void BtlvMcss_SetAnimSpeed(BtlvMcss *work, int pos, fx32 speed) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201afa0(work->entries[index].mcss, speed);
}

void BtlvMcss_MovePosition(BtlvMcss *work, int pos, int type, VecFx32 *dest, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    MCSS_GetPosition(work->entries[index].mcss, &start);
    if (type == 1) {
        if (pos & 1) {
            dest->x = start.x - dest->x;
        } else {
            dest->x += start.x;
        }
        dest->y += start.y;
        dest->z += start.z;
    }
    if (type == 5) {
        BtlvMcss_GetDefaultPosImpl(work, dest, pos);
        type = 1;
    }
    if (type == 6) {
        BtlvMcss_GetDefaultPosImpl(work, dest, pos);
        type = 0;
    }
    if (type == 0) {
        MCSS_SetPosition(work->entries[index].mcss, dest);
        return;
    }
    work->moveSeq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, dest, frames, wait, count, BtlvMcss_MoveTask_Position,
                           BtlvMcss_MoveTaskEnd_Position, TRUE, work->moveSeq[pos]);
    work->moveTasks |= BtlvEffect_PosBit(pos);
}

void BtlvMcss_MoveVec518(BtlvMcss *work, int pos, int type, VecFx32 *dest, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201aba0(work->entries[index].mcss, &start);
    work->unk518Seq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, dest, frames, wait, count, BtlvMcss_MoveTask_Vec518,
                           BtlvMcss_MoveTaskEnd_Vec518, FALSE, work->unk518Seq[pos]);
    work->unk518Tasks |= BtlvEffect_PosBit(pos);
}

void BtlvMcss_MoveScale(BtlvMcss *work, int pos, int type, VecFx32 *dest, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201ab70(work->entries[index].mcss, &start);
    work->scaleSeq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, dest, frames, wait, count, BtlvMcss_MoveTask_Scale,
                           BtlvMcss_MoveTaskEnd_Scale, FALSE, work->scaleSeq[pos]);
    work->scaleTasks |= BtlvEffect_PosBit(pos);
}

// BUG: the start is func_0201aba0's vector, where the task steps func_0201ac28's
void BtlvMcss_MoveVec524(BtlvMcss *work, int pos, int type, VecFx32 *dest, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201aba0(work->entries[index].mcss, &start);
    work->unk524Seq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, dest, frames, wait, count, BtlvMcss_MoveTask_Vec524,
                           BtlvMcss_MoveTaskEnd_Vec524, FALSE, work->unk524Seq[pos]);
    work->unk524Tasks |= BtlvEffect_PosBit(pos);
}

void BtlvMcss_MoveVec51c(BtlvMcss *work, int pos, int type, VecFx32 *dest, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201abd4(work->entries[index].mcss, &start);
    work->unk51cSeq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, dest, frames, wait, count, BtlvMcss_MoveTask_Vec51c,
                           BtlvMcss_MoveTaskEnd_Vec51c, TRUE, work->unk51cSeq[pos]);
    work->unk51cTasks |= BtlvEffect_PosBit(pos);
}

void BtlvMcss_SwapPositions(BtlvMcss *work, int pos1, int pos2) {
    int pos[2];
    int index[2];
    VecFx32 dest;
    int i;

    pos[0] = pos1;
    pos[1] = pos2;
    for (i = 0; i < 2; i++) {
        index[i] = BtlvMcss_GetIndex(work, pos[i]);
        if (BtlvMcss_Exists(work, pos[i])) {
            BtlvMcss_GetDefaultPosImpl(work, &dest, pos[i ^ 1]);
            BtlvMcss_MovePosition(work, pos[i], 0, &dest, 0, 0, 0);
            BtlvGauge_HideStatus(BtlvEffect_GetGauge(), pos[i]);
            if (work->entries[index[i]].moved) {
                work->entries[index[i]].moved = FALSE;
                BtlvMcss_SetVanish(work, pos[i], 0);
            }
        }
    }
    for (i = 0; i < 2; i++) {
        if (index[i] != -1) {
            work->entries[index[i]].pos = pos[i ^ 1];
        }
    }
}

void BtlvMcss_Blink(BtlvMcss *work, int pos, int type, s32 wait, s32 count) {
    BtlvMcssMove *task;

    switch (type) {
    case 3:
        BtlvMcss_SetAnimStop(work, pos, 1);
        break;
    case 4:
        BtlvMcss_SetAnimStop(work, pos, 0);
        break;
    case 2:
        task = GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvMcssMove), FALSE, "btlv_mcss.c", 0x62f);
        task->work = work;
        task->pos = pos;
        task->move.type = type;
        task->move.wait = 0;
        task->move.waitReset = wait;
        task->move.count = count * 2;
        work->blinkSeq[pos]++;
        task->seq = work->blinkSeq[pos];
        BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, BtlvMcss_MoveTask_Blink, task, 0),
                           BtlvMcss_MoveTaskEnd_Blink, 2);
        work->blinkTasks |= BtlvEffect_PosBit(pos);
        break;
    }
}

void BtlvMcss_MoveAlpha(BtlvMcss *work, int pos, int type, int alpha, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;
    VecFx32 end;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    start.x = FX32_CONST(func_0201ae88(work->entries[index].mcss));
    start.y = 0;
    start.z = 0;
    end.x = FX32_CONST(alpha);
    end.y = 0;
    end.z = 0;
    work->alphaSeq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, &end, frames, wait, count, BtlvMcss_MoveTask_Alpha,
                           BtlvMcss_MoveTaskEnd_Alpha, FALSE, work->alphaSeq[pos]);
    work->alphaTasks |= BtlvEffect_PosBit(pos);
}

void BtlvMcss_MoveCircle(BtlvMcss *work, BtlvMcssCircle *param) {
    BtlvMcssCircle *task =
        GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvMcssCircle), FALSE, "btlv_mcss.c", 0x671);

    task->work = work;
    task->pos = param->pos;
    task->mode = param->mode;
    task->start = param->start;
    task->radius1 = param->radius1;
    task->radius2 = param->radius2;
    task->frames = param->frames;
    task->wait = param->wait;
    task->count = param->count;
    task->turnWait = param->turnWait;
    task->waitCount = 0;
    task->turnWaitLeft = 0;
    // Mirrored for the enemy side, but for the starts 2 and 3 on the y-z and x-y planes
    if (task->pos & 1) {
        if (!((task->mode == 0 || task->mode == 1 || task->mode == 4 || task->mode == 5) &&
              (param->start == 2 || param->start == 3))) {
            task->start ^= 1;
        }
    }
    task->angle = (task->mode & 1) ? 0x10000 : 0;
    task->speed = 0x10000 / param->frames;
    work->moveSeq[task->pos]++;
    task->seq = work->moveSeq[task->pos];
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, BtlvMcss_MoveTask_Circle, task, 0), BtlvMcss_MoveTaskEnd_Circle,
                       2);
    work->moveTasks |= BtlvEffect_PosBit(task->pos);
}

void BtlvMcss_Shake(BtlvMcss *work, BtlvMcssShake *param) {
    BtlvMcssShake *task =
        GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvMcssShake), FALSE, "btlv_mcss.c", 0x6aa);
    int index = BtlvMcss_GetIndex(work, param->pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    *task = *param;
    task->work = work;
    MCSS_GetPosition(work->entries[index].mcss, &task->start);
    work->moveSeq[task->pos]++;
    task->seq = work->moveSeq[task->pos];
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, BtlvMcss_MoveTask_Shake, task, 0), BtlvMcss_MoveTaskEnd_Shake,
                       2);
    work->moveTasks |= BtlvEffect_PosBit(task->pos);
}

void BtlvMcss_MoveLevel(BtlvMcss *work, int pos, int type, int level, s32 frames, s32 wait, s32 count) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 start;
    VecFx32 end;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    start.x = FX32_CONST(func_0201af44(work->entries[index].mcss));
    start.y = 0;
    start.z = 0;
    end.x = FX32_CONST(level);
    end.y = 0;
    end.z = 0;
    work->levelSeq[pos]++;
    BtlvMcss_StartMoveTask(work, pos, type, &start, &end, frames, wait, count, BtlvMcss_MoveTask_Level,
                           BtlvMcss_MoveTaskEnd_Level, FALSE, work->levelSeq[pos]);
    work->levelTasks |= BtlvEffect_PosBit(pos);
}

BOOL BtlvMcss_IsBusy(BtlvMcss *work, int pos) {
    BOOL fading = FALSE;

    if (BtlvMcss_Exists(work, pos)) {
        fading = func_0201aee8(work->entries[BtlvMcss_GetIndex(work, pos)].mcss);
    }
    if ((BtlvEffect_PosBit(pos) & work->moveTasks) || (BtlvEffect_PosBit(pos) & work->unk518Tasks) ||
        (BtlvEffect_PosBit(pos) & work->unk524Tasks) || (BtlvEffect_PosBit(pos) & work->scaleTasks) ||
        (BtlvEffect_PosBit(pos) & work->unk51cTasks) || (BtlvEffect_PosBit(pos) & work->blinkTasks) ||
        (BtlvEffect_PosBit(pos) & work->alphaTasks) || (BtlvEffect_PosBit(pos) & work->levelTasks) || work->rotating ||
        fading) {
        return TRUE;
    }
    return FALSE;
}

BOOL BtlvMcss_IsAnyBusy(BtlvMcss *work) {
    BtlvMcssPos pos;

    for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_TRAINER; pos++) {
        if (BtlvMcss_Exists(work, pos) && BtlvMcss_IsBusy(work, pos)) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL BtlvMcss_Exists(BtlvMcss *work, int pos) {
    if (BtlvMcss_GetIndex(work, pos) != -1) {
        return TRUE;
    }
    return FALSE;
}

void BtlvMcss_StartPaletteFade(BtlvMcss *work, int pos, u8 startEvy, u8 endEvy, u8 wait, u32 color) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    // The callers pass the wait as a u8, and func_0201ae2c takes it signed
    func_0201ae2c(work->entries[index].mcss, startEvy, endEvy, (s8)wait, color);
    work->entries[index].fading = TRUE;
}

static void BtlvMcss_SetPaletteBlend(BtlvMcss *work, int pos, u8 evy, GXRgb color) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201afb0(work->mcssSys, work->entries[index].mcss, evy, color);
}

static void BtlvMcss_ResetPaletteBlend(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201b128(work->mcssSys, work->entries[index].mcss);
}

void BtlvMcss_ChangePokemon(BtlvMcss *work, int pos, PartyPkm *pkm) {
    int index = BtlvMcss_GetIndex(work, pos);
    u32 hp;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    work->entries[index].mon.species = PokeParty_GetParam(pkm, PKM_PARAM_SPECIES, NULL);
    work->entries[index].mon.form = PokeParty_GetParam(pkm, PKM_PARAM_FORM, NULL);
    work->entries[index].mon.weight =
        PML_PersonalGetParamSingle(work->entries[index].mon.species, work->entries[index].mon.form, PERSONAL_WEIGHT);
    work->entries[index].mon.personality = PokeParty_GetParam(pkm, PKM_PARAM_PID, NULL);
    work->entries[index].ball = PokeParty_GetParam(pkm, PKM_PARAM_POKEBALL, NULL);
    if (PokeParty_IsRare(pkm) == TRUE) {
        work->entries[index].flags |= BTLV_MCSS_FLAG_RARE;
    }
    if (PokeParty_GetParam(pkm, PKM_PARAM_N_POKEMON, NULL) == TRUE) {
        work->entries[index].flags |= BTLV_MCSS_FLAG_N_POKEMON;
    }
    hp = PokeParty_GetParam(pkm, PKM_PARAM_HP, NULL);
    work->entries[index].mon.hpColor = HPGauge_GetColor(hp, PokeParty_GetParam(pkm, PKM_PARAM_MAX_HP, NULL));
    work->entries[index].changed = TRUE;
}

u16 BtlvMcss_GetWeight(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return 0;
    }
    if (work->entries[index].mcss == NULL) {
        return 0;
    }
    return work->entries[index].mon.weight;
}

u32 BtlvMcss_GetFlags(BtlvMcss *work, int pos) {
    int index;

    if (pos == 0xff) {
        return 0;
    }
    index = BtlvMcss_GetIndex(work, pos);
    if (index == -1) {
        return 0;
    }
    if (work->entries[index].mcss == NULL) {
        return 0;
    }
    return work->entries[index].flags;
}

BOOL BtlvMcss_IsNoBounce(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return FALSE;
    }
    if (work->entries[index].mcss == NULL) {
        return FALSE;
    }
    if (PML_PersonalGetParamSingle(work->entries[index].mon.species, work->entries[index].mon.form,
                                   PERSONAL_NO_BOUNCE)) {
        return TRUE;
    }
    return FALSE;
}

u8 BtlvMcss_GetUnk(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return 0;
    }
    if (work->entries[index].mcss == NULL) {
        return 0;
    }
    return func_0201ae00(work->entries[index].mcss);
}

void BtlvMcss_SetSubstitute(BtlvMcss *work, int pos, BOOL substitute, BOOL flag) {
    int index = BtlvMcss_GetIndex(work, pos);
    MCSSLoadInfo info;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    if (!substitute) {
        func_0201c188(work->mcssSys, work->entries[index].mon.personality);
        func_02019bd8(work->mcssSys, work->entries[index].mcss, &work->entries[index].info);
        MCSS_SetAnimationEndCallback(work->entries[index].mcss, index, BtlvMcss_OnAnimationEnd, 1);
        if (flag) {
            work->entries[index].flags &= ~BTLV_MCSS_FLAG_UNK0;
        }
        work->entries[index].flags &= ~BTLV_MCSS_FLAG_SUBSTITUTE;
    } else {
        info.arcId = GetPokemonGraphicsARCID();
        info.character = (pos & 1) ? 0x3aac : 0x3ab2;
        info.palette = 0x3ab8;
        info.cells = (pos & 1) ? 0x3aad : 0x3ab3;
        info.cellAnime = (pos & 1) ? 0x3aae : 0x3ab4;
        info.multiCells = (pos & 1) ? 0x3aaf : 0x3ab5;
        info.multiCellAnime = (pos & 1) ? 0x3ab0 : 0x3ab6;
        info.bin = (pos & 1) ? 0x3ab1 : 0x3ab7;
        info.unk20 = 0;
        func_02019bd8(work->mcssSys, work->entries[index].mcss, &info);
        BtlvMcss_ResetPaletteBlend(work, pos);
        if (flag) {
            work->entries[index].flags |= BTLV_MCSS_FLAG_UNK0;
        }
        work->entries[index].flags |= BTLV_MCSS_FLAG_SUBSTITUTE;
    }
}

static void BtlvMcss_SetLevel(BtlvMcss *work, int pos, int level) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    func_0201c188(work->mcssSys, work->entries[index].mon.personality);
    func_0201af54(work->mcssSys, work->entries[index].mcss, level);
}

void BtlvMcss_Transform(BtlvMcss *work, int targetPos, int pos) {
    int target = BtlvMcss_GetIndex(work, targetPos);
    int index = BtlvMcss_GetIndex(work, pos);
    MCSSLoadInfo *dst;

    work->entries[index].info.arcId = work->entries[target].info.arcId;
    work->entries[index].info.palette = work->entries[target].info.palette;
    // The other side's sprites, front for back
    if (targetPos & 1) {
        work->entries[index].info.character = work->entries[target].info.character + 9;
        work->entries[index].info.cells = work->entries[target].info.cells + 9;
        work->entries[index].info.cellAnime = work->entries[target].info.cellAnime + 9;
        work->entries[index].info.multiCells = work->entries[target].info.multiCells + 9;
        work->entries[index].info.multiCellAnime = work->entries[target].info.multiCellAnime + 9;
        work->entries[index].info.bin = work->entries[target].info.bin + 9;
    } else {
        work->entries[index].info.character = work->entries[target].info.character - 9;
        work->entries[index].info.cells = work->entries[target].info.cells - 9;
        work->entries[index].info.cellAnime = work->entries[target].info.cellAnime - 9;
        work->entries[index].info.multiCells = work->entries[target].info.multiCells - 9;
        work->entries[index].info.multiCellAnime = work->entries[target].info.multiCellAnime - 9;
        work->entries[index].info.bin = work->entries[target].info.bin - 9;
    }
    work->entries[index].mon = work->entries[target].mon;
    dst = &work->entries[index].info;
    func_02019bd8(work->mcssSys, work->entries[index].mcss, dst);
}

void BtlvMcss_ReloadSprite(BtlvMcss *work, int pos, const MCSSLoadInfo *info) {
    int index = BtlvMcss_GetIndex(work, pos);
    MCSSLoadInfo *dst = &work->entries[index].info;

    *dst = *info;
    func_0201c188(work->mcssSys, work->entries[index].mon.personality);
    func_02019bd8(work->mcssSys, work->entries[index].mcss, dst);
}

u32 BtlvMcss_PlayCry(BtlvMcss *work, int pos, s32 speed, int volume, int arg4, int arg5, int arg6) {
    int index = BtlvMcss_GetIndex(work, pos);
    s32 pan;
    BtlMainModule *mainModule;
    PokeVoiceChatterInfo chatter;
    PokeVoiceChatterInfo *chatterInfo;
    BOOL flag;
    u32 handle;

    if (index == -1) {
        return 0;
    }
    if (work->entries[index].mcss == NULL) {
        return 0;
    }
    switch (pos) {
    case 0:
    case 1:
        pan = data_ov168_021f31ec[pos];
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        if (work->rotation) {
            pan = data_ov168_021f321c[pos - 2];
        } else if (work->triple) {
            pan = data_ov168_021f3294[pos - 2];
        } else {
            pan = data_ov168_021f320c[pos - 2];
        }
        break;
    case 6:
    case 7:
        if (work->rotation) {
            pan = data_ov168_021f321c[pos - 2];
        } else {
            pan = data_ov168_021f3294[pos - 2];
        }
        break;
    }
    mainModule = BtlvEffect_GetMainModule();
    chatterInfo = &chatter;
    flag = FALSE;
    if (mainModule != NULL) {
        if (!func_ov167_0219d228(mainModule, pos, &chatterInfo->chatter)) {
            chatterInfo = NULL;
        }
    } else {
        chatterInfo = NULL;
    }
    if (arg4 != 0 || arg5 != 0) {
        flag = TRUE;
    }
    // By the HP: lower in the yellow
    if (speed == (s32)0x80000000) {
        speed = work->entries[index].mon.hpColor == HP_GAUGE_COLOR_YELLOW ? -0xa00 : 0;
    }
    handle = PokeVoice_Play(work->entries[index].mon.species, work->entries[index].mon.form, (u8)pan, flag, arg4, arg5,
                            arg6, chatterInfo);
    PokeVoice_AdjustVolume(handle, volume);
    PokeVoice_AdjustSpeed(handle, speed);
    return handle;
}

void BtlvMcss_StartRotation(BtlvMcss *work, int side, int dir, BOOL playSe) {
    BtlvMcssRotate *task =
        GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvMcssRotate), FALSE, "btlv_mcss.c", 0x94a);

    task->work = work;
    task->seq = 0;
    task->playSe = playSe;
    task->side = side;
    task->dir = dir;
    work->rotating = TRUE;
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, BtlvMcss_RotationTask, task, 0), BtlvMcss_RotationTaskEnd, 2);
}

u32 BtlvMcss_GetBall(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return 0;
    }
    if (work->entries[index].mcss == NULL) {
        return 0;
    }
    return work->entries[index].ball;
}

void BtlvMcss_SetupPokemonLoadInfo(BtlvMcss *work, PartyPkm *pkm, MCSSLoadInfo *info, int pos) {
    BOOL back = TRUE;

    if (pos & 1) {
        back = FALSE;
    }
    func_0201bfdc(pkm, info, back);
    func_0201c188(work->mcssSys, PokeParty_GetParam(pkm, PKM_PARAM_PID, NULL));
}

BOOL BtlvMcss_SetAnimation(BtlvMcss *work, int pos, int anim) {
    BOOL set = FALSE;
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return set;
    }
    if (work->entries[index].mcss == NULL) {
        return set;
    }
    if (anim < func_0201afa8(work->entries[index].mcss)) {
        MCSS_SetAnimation(work->entries[index].mcss, anim);
        set = TRUE;
        work->entries[index].animSet = set;
    }
    return set;
}

void BtlvMcss_WatchAnimationEnd(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    MCSS_SetAnimationEndCallback(work->entries[index].mcss, index, BtlvMcss_OnSetAnimationEnd, 1);
}

BOOL BtlvMcss_IsAnimationSet(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return FALSE;
    }
    if (work->entries[index].mcss == NULL) {
        return FALSE;
    }
    return work->entries[index].animSet;
}

void BtlvMcss_ResetPosition(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 home;
    VecFx32 cur;
    VecFx32 offset;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    BtlvMcss_GetDefaultPosImpl(work, &home, pos);
    MCSS_GetPosition(work->entries[index].mcss, &cur);
    func_0201ab3c(work->entries[index].mcss, &offset);
    work->entries[index].moved = FALSE;
    if (home.x != cur.x || home.y != cur.y || home.z != cur.z || offset.x != 0 || offset.y != 0 || offset.z != 0) {
        work->entries[index].moved = TRUE;
        offset.x = 0;
        offset.y = 0;
        offset.z = 0;
        MCSS_SetPosition(work->entries[index].mcss, &home);
        func_0201ab54(work->entries[index].mcss, &offset);
        MCSS_Hide(work->entries[index].mcss);
    }
}

void BtlvMcss_SetUnkB270(BtlvMcss *work, int pos, BOOL check) {
    BOOL flag = FALSE;
    int index = BtlvMcss_GetIndex(work, pos);

    if (index == -1) {
        return;
    }
    if (work->entries[index].mcss == NULL) {
        return;
    }
    if (check == TRUE) {
        switch (BtlvEffect_GetBattleStyle()) {
        case 0:
            break;
        case 1:
            switch (pos) {
            case 3:
            case 4:
                flag = TRUE;
                break;
            }
            break;
        case 2:
            switch (pos) {
            case 2:
            case 5:
            case 6:
                flag = TRUE;
                break;
            }
            break;
        case 3:
            break;
        }
    }
    func_0201b270(work->entries[index].mcss, flag);
}

BOOL BtlvMcss_CheckChanged(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);
    BOOL changed;

    if (index == -1) {
        return FALSE;
    }
    if (work->entries[index].mcss == NULL) {
        return FALSE;
    }
    changed = work->entries[index].changed ? TRUE : FALSE;
    work->entries[index].changed = FALSE;
    return changed;
}

void BtlvMcss_SaveVanish(BtlvMcss *work) {
    BtlvMcssPos pos;
    int index;

    for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_TRAINER; pos++) {
        if (BtlvMcss_Exists(work, pos)) {
            index = BtlvMcss_GetIndex(work, pos);
            work->entries[index].vanished = BtlvMcss_GetVanish(work, pos);
        }
    }
}

void BtlvMcss_RestoreVanish(BtlvMcss *work) {
    BtlvMcssPos pos;
    int index;

    for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_TRAINER; pos++) {
        if (BtlvMcss_Exists(work, pos)) {
            index = BtlvMcss_GetIndex(work, pos);
            BtlvMcss_SetVanish(work, pos, work->entries[index].vanished);
            work->entries[index].hidden = FALSE;
        }
    }
}

static void BtlvMcss_SetupTrainerLoadInfo(u32 trainerType, MCSSLoadInfo *info, int pos) {
    BOOL back = TRUE;

    if (pos & 1) {
        back = FALSE;
    }
    GetTrainerSpriteResIDs(trainerType, info, back);
}

static void BtlvMcss_UpdateScale(BtlvMcss *work, int pos) {
    int index = BtlvMcss_GetIndex(work, pos);
    VecFx32 scale;
    fx32 value;

    if (index == -1 || work->entries[index].mcss == NULL) {
        return;
    }
    value = BtlvMcss_GetDefaultScaleImpl(work, pos, work->entries[index].flat);
    if (pos & 1) {
        value = FX_Mul(value, work->scaleFar);
    } else {
        value = FX_Mul(value, work->scaleNear);
    }
    scale.x = value;
    scale.y = value;
    scale.z = FX32_ONE;
    MCSS_SetScale(work->entries[index].mcss, &scale);
    value = BtlvMcss_GetDefaultScaleImpl(work, pos, FALSE);
    scale.x = value;
    scale.y = value;
    scale.z = FX32_ONE;
    func_0201ac0c(work->entries[index].mcss, &scale);
}

static void BtlvMcss_StartMoveTask(BtlvMcss *work, int pos, int type, VecFx32 *start, VecFx32 *end, s32 frames,
                                   s32 wait, s32 count, TCBFunc func, void (*endFunc)(TCB *tcb), BOOL mirror, u8 seq) {
    BtlvMcssMove *task = GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvMcssMove), FALSE, "btlv_mcss.c", 0xaa6);

    task->work = work;
    task->pos = pos;
    task->move.type = type;
    task->move.stepTime = frames;
    task->move.stepTimeReset = frames;
    task->move.wait = 0;
    task->move.waitReset = wait;
    task->move.count = count * 2;
    task->move.start.x = start->x;
    task->move.start.y = start->y;
    task->move.start.z = start->z;
    task->move.end.x = end->x;
    task->move.end.y = end->y;
    task->move.end.z = end->z;
    task->value.x = start->x;
    task->value.y = start->y;
    task->value.z = start->z;
    task->seq = seq;
    switch (type) {
    case 1:
    case 4:
        BtlvEffTool_CalcStepVec(&task->move.start, end, &task->move.step, FX32_CONST(frames));
        break;
    case 3:
        task->move.count *= 2;
    case 2:
        task->move.step.x = FX_Div(end->x, FX32_CONST(frames));
        task->move.step.y = FX_Div(end->y, FX32_CONST(frames));
        task->move.step.z = FX_Div(end->z, FX32_CONST(frames));
        // Mirrored for the enemy side
        if ((pos & 1) && mirror) {
            task->move.step.x *= -1;
            task->move.step.z *= -1;
        }
        break;
    }
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, func, task, 0), endFunc, 2);
}

static void BtlvMcss_MoveTask_Position(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    VecFx32 value;
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->moveSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    MCSS_GetPosition(work->entries[index].mcss, &value);
    done = BtlvEffTool_Move(&task->move, &value);
    MCSS_SetPosition(work->entries[index].mcss, &value);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Position(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->moveSeq[task->pos]) {
        task->work->moveTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Vec518(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    VecFx32 value;
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->unk518Seq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    func_0201aba0(work->entries[index].mcss, &value);
    done = BtlvEffTool_Move(&task->move, &value);
    func_0201abb8(work->entries[index].mcss, &value);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Vec518(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->unk518Seq[task->pos]) {
        task->work->unk518Tasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Scale(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    VecFx32 value;
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->scaleSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    func_0201ab70(work->entries[index].mcss, &value);
    done = BtlvEffTool_Move(&task->move, &value);
    MCSS_SetScale(work->entries[index].mcss, &value);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Scale(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->scaleSeq[task->pos]) {
        task->work->scaleTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Vec524(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    VecFx32 value;
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->unk524Seq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    func_0201ac28(work->entries[index].mcss, &value);
    done = BtlvEffTool_Move(&task->move, &value);
    func_0201ac40(work->entries[index].mcss, &value);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Vec524(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->unk524Seq[task->pos]) {
        task->work->unk524Tasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Vec51c(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    VecFx32 value;
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->unk51cSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    func_0201abd4(work->entries[index].mcss, &value);
    done = BtlvEffTool_Move(&task->move, &value);
    func_0201abf0(work->entries[index].mcss, &value);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Vec51c(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->unk51cSeq[task->pos]) {
        task->work->unk51cTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Blink(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;

    if (!BtlvMcss_Exists(work, task->pos) || task->seq != work->blinkSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->move.wait == 0) {
        task->move.wait = task->move.waitReset;
        BtlvMcss_SetAnimStop(task->work, task->pos, 2);
        if (--task->move.count == 0) {
            BtlvMcss_SetAnimStop(task->work, task->pos, 0);
            BtlvEffect_EndTask(tcb);
        }
    } else {
        task->move.wait--;
    }
}

static void BtlvMcss_MoveTaskEnd_Blink(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->blinkSeq[task->pos]) {
        task->work->blinkTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Alpha(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->alphaSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    done = BtlvEffTool_Move(&task->move, &task->value);
    MCSS_SetAlpha(work->entries[index].mcss, task->value.x >> FX32_SHIFT);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Alpha(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->alphaSeq[task->pos]) {
        task->work->alphaTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Circle(TCB *tcb, void *data) {
    BtlvMcssCircle *task = data;
    BtlvMcss *work = task->work;
    VecFx32 offset = { 0, 0, 0 };
    int index = BtlvMcss_GetIndex(work, task->pos);
    int idx;
    fx32 a;
    fx32 b;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->moveSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->turnWaitLeft == 0) {
        if (task->waitCount == task->wait) {
            task->waitCount = 0;
            if (task->mode & 1) {
                task->angle -= task->speed;
            } else {
                task->angle += task->speed;
            }
            if (task->angle & 0xffff0000) {
                task->angle = (u16)task->angle;
                task->count--;
                task->turnWaitLeft = task->turnWait;
            }
            if (task->count != 0) {
                switch (task->start) {
                case 0:
                    idx = (u16)(task->angle + 0x4000);
                    a = -FX_Mul(FX_SinIdx(idx), task->radius1);
                    b = -FX_Mul(FX_CosIdx(idx), task->radius2);
                    a += task->radius1;
                    break;
                case 1:
                    idx = (u16)(task->angle + 0x4000);
                    a = FX_Mul(FX_SinIdx(idx), task->radius1) - task->radius1;
                    b = FX_Mul(FX_CosIdx(idx), task->radius2);
                    break;
                case 2:
                    idx = task->angle;
                    a = -FX_Mul(FX_SinIdx(idx), task->radius1);
                    b = -FX_Mul(FX_CosIdx(idx), task->radius2);
                    b += task->radius2;
                    break;
                case 3:
                    idx = task->angle;
                    a = FX_Mul(FX_SinIdx(idx), task->radius1);
                    b = FX_Mul(FX_CosIdx(idx), task->radius2) - task->radius2;
                    break;
                }
                switch ((task->mode & 7) >> 1) {
                case 0:
                default:
                    offset.z = a;
                    offset.y = b;
                    break;
                case 1:
                    offset.x = a;
                    offset.z = b;
                    break;
                case 2:
                    offset.x = a;
                    offset.y = b;
                    break;
                }
            }
            func_0201ab54(work->entries[index].mcss, &offset);
        } else {
            task->waitCount++;
        }
    } else {
        task->turnWaitLeft--;
    }
    if (task->count == 0) {
        BtlvEffect_EndTask(tcb);
    }
}

// NONMATCHING: loads the task's position before its work, where the original loads the work first
static void BtlvMcss_MoveTaskEnd_Circle(TCB *tcb) {
    BtlvMcssCircle *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->moveSeq[task->pos]) {
        task->work->moveTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Shake(TCB *tcb, void *data) {
    BtlvMcssShake *task = data;
    BtlvMcss *work = task->work;
    VecFx32 offset = { 0, 0, 0 };
    int index = BtlvMcss_GetIndex(work, task->pos);
    fx16 sin;
    fx32 value;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->moveSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    task->angle += task->speed;
    sin = FX_SinIdx((task->angle & 0xffff000) >> FX32_SHIFT);
    value = FX_F32_TO_FX32(FX_FX16_TO_F32(sin));
    value = FX_Mul(value, task->amplitude);
    if (task->axis) {
        offset.y = value;
    } else {
        offset.x = value;
    }
    func_0201ab54(work->entries[index].mcss, &offset);
    if (--task->frames == 0) {
        offset.x = 0;
        offset.y = 0;
        offset.z = 0;
        func_0201ab54(work->entries[index].mcss, &offset);
        BtlvEffect_EndTask(tcb);
    }
}

// NONMATCHING: loads the task's position before its work, where the original loads the work first
static void BtlvMcss_MoveTaskEnd_Shake(TCB *tcb) {
    BtlvMcssShake *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->moveSeq[task->pos]) {
        task->work->moveTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_MoveTask_Level(TCB *tcb, void *data) {
    BtlvMcssMove *task = data;
    BtlvMcss *work = task->work;
    int index = BtlvMcss_GetIndex(work, task->pos);
    BOOL done;

    if (index == -1) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    if (task->seq != work->levelSeq[task->pos]) {
        BtlvEffect_EndTask(tcb);
        return;
    }
    done = BtlvEffTool_Move(&task->move, &task->value);
    BtlvMcss_SetLevel(work, task->pos, task->value.x >> FX32_SHIFT);
    if (done == TRUE) {
        MCSS_SetAnimationEndCallback(work->entries[index].mcss, index, BtlvMcss_OnAnimationEnd, 1);
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvMcss_MoveTaskEnd_Level(TCB *tcb) {
    BtlvMcssMove *task = GFL_TCBGetData(tcb);

    if (task->seq == task->work->levelSeq[task->pos]) {
        task->work->levelTasks &= BtlvEffect_PosBit(task->pos) ^ 0xffffffff;
    }
}

static void BtlvMcss_IdleTask(TCB *tcb, void *data) {
    BtlvMcssIdle *idle = data;
    BtlvMcss *work = BtlvEffect_GetMcss();
    int index = idle->index;

    if (index == -1) {
        GFL_HeapFree(idle);
        GFL_TCBRemove(tcb);
        return;
    }
    if (work->entries[index].mcss == NULL) {
        GFL_HeapFree(idle);
        GFL_TCBRemove(tcb);
        return;
    }
#ifdef BWBATTLE
    if (!BtlvEffect_ShouldTickIdle()) {
        return;
    }
#endif
    switch (idle->seq) {
    case 0:
        func_0201adc4(work->entries[index].mcss)->animCtrl.bActive = FALSE;
        func_0201ae1c(work->entries[index].mcss, index | 0x80000000, BtlvMcss_IdleNodeCallback);
        idle->seq++;
        idle->wait = data_ov168_021f4184[GFL_RandomMTRange(3)];
        break;
    case 1:
        if (--idle->wait == 0 || work->unk51cTasks != 0 || !BtlvEffvm_GetScriptKind(BtlvEffect_GetEffvm())) {
            MCSS_SetAnimation(work->entries[index].mcss, 0);
            func_0201adc4(work->entries[index].mcss)->animCtrl.bActive = TRUE;
            func_0201ae1c(work->entries[index].mcss, index, BtlvMcss_IdleNodeCallback);
            idle->seq++;
        }
        break;
    case 2:
        MCSS_SetAnimationEndCallback(work->entries[index].mcss, index, BtlvMcss_OnAnimationEnd, 1);
        GFL_HeapFree(idle);
        GFL_TCBRemove(tcb);
        work->entries[index].idleTask = NULL;
        break;
    }
}

static void BtlvMcss_RotationTask(TCB *tcb, void *data) {
    BtlvMcssRotate *task = data;
    int positions[2][3] = {
        { 2, 4, 6 },
        { 3, 5, 7 },
    };
    s32 angles[2][3] = {
        { 0x8000, 0xd555, 0x2aab },
        { 0, 0x5555, 0xaaaa },
    };
    int dests[2][2][3] = {
        {
            { 4, 6, 2 },
            { 5, 7, 3 },
        },
        {
            { 6, 2, 4 },
            { 7, 3, 5 },
        },
    };
    VecFx32 pos3d;
    VecFx32 dest;
    int index[3];
    int i;

    switch (task->seq) {
    case 0:
        for (i = 0; i < 3; i++) {
            if (task->work->entries[positions[task->side][i]].idleTask != NULL) {
                break;
            }
        }
        if (i != 3) {
            break;
        }
        for (i = 0; i < 3; i++) {
            task->angle[i] = angles[task->side][i];
        }
        BtlvStage_StartAnimation(BtlvEffect_GetStage(), task->side, 0, task->dir == 0 ? FX32_ONE : -FX32_ONE, 60);
        task->speed = task->dir == 0 ? 0x16c : -0x16c;
        task->frames = 60;
        if (task->playSe) {
            GFL_SEPlayKeepVol(SEQ_SE_FLD_59, 2);
        }
        task->seq++;
        break;
    case 1:
        for (i = 0; i < 3; i++) {
            if (BtlvMcss_Exists(task->work, positions[task->side][i])) {
                pos3d.x = FX_Mul(FX_SinIdx((u16)task->angle[i]), FX32_CONST(5));
                pos3d.y = FX32_CONST(0.4);
                pos3d.z = task->side == 0 ? FX32_CONST(12) : FX32_CONST(-13);
                pos3d.z += FX_Mul(FX_CosIdx((u16)task->angle[i]), FX32_CONST(5));
                BtlvMcss_MovePosition(task->work, positions[task->side][i], 0, &pos3d, 0, 0, 0);
            }
            task->angle[i] += task->speed;
        }
        if (--task->frames == 0) {
            for (i = 0; i < 3; i++) {
                if (BtlvMcss_Exists(task->work, positions[task->side][i])) {
                    BtlvMcss_GetDefaultPosImpl(task->work, &dest, dests[task->dir][task->side][i]);
                    BtlvMcss_MovePosition(task->work, positions[task->side][i], 0, &dest, 0, 0, 0);
                }
            }
            task->seq++;
        }
        break;
    case 2:
        if (task->work->moveTasks != 0) {
            break;
        }
        for (i = 0; i < 3; i++) {
            index[i] = BtlvMcss_GetIndex(task->work, positions[task->side][i]);
            BtlvGauge_HideStatus(BtlvEffect_GetGauge(), positions[task->side][i]);
        }
        for (i = 0; i < 3; i++) {
            if (index[i] != -1) {
                task->work->entries[index[i]].pos = dests[task->dir][task->side][i];
            }
        }
        if (task->playSe) {
            GFL_SndPlayerStop(2);
            GFL_SEPlayKeepVol(SEQ_SE_ROTATION_B, 1);
        }
        BtlvEffect_EndTask(tcb);
        break;
    }
}

static void BtlvMcss_RotationTaskEnd(TCB *tcb) {
    BtlvMcssRotate *task = GFL_TCBGetData(tcb);

    task->work->rotating = FALSE;
}

static void BtlvMcss_OnAnimationEnd(u32 index, fx32 frame) {
    BtlvMcss *work = BtlvEffect_GetMcss();
    BtlvMcssIdle *idle;
    u32 count;
    u64 value;

    if (index == -1) {
        return;
    }
    if (work->entries[index].mcss == NULL) {
        return;
    }
    if (!BtlvEffvm_GetScriptKind(BtlvEffect_GetEffvm())) {
        return;
    }
    if (work->rotating) {
        return;
    }
    if (func_0201add0(work->entries[index].mcss) == 0xff) {
        work->entries[index].animEnded = TRUE;
        return;
    }
    count = work->entries[index].idleCount;
    work->entries[index].idleCount = count - 1;
    value = GFL_RandomMT();
    value *= count;
    value >>= 32;
    if ((u32)value == 0) {
        work->entries[index].idleCount = 3;
        idle = GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvMcssIdle), TRUE, "btlv_mcss.c", 0xdb6);
        idle->index = index;
        if (func_0201adc8(work->entries[index].mcss) > 1) {
            MCSS_SetAnimation(work->entries[index].mcss, 1);
        }
        work->entries[index].idleTask = GFL_TCBMgrAddTask(work->tcbMgr, BtlvMcss_IdleTask, idle, 0);
    }
}

static BOOL BtlvMcss_IdleNodeCallback(u32 param, const NNSG2dMultiCellHierarchyData *node,
                                      NNSG2dCellAnimation *cellAnim, u16 nodeIdx) {
    BtlvMcss *work = BtlvEffect_GetMcss();
    u16 index = param;
    BOOL found;
    int count;
    int i;

    if (index == -1) {
        return FALSE;
    }
    if (work->entries[index].mcss == NULL) {
        return FALSE;
    }
    if (param & 0x80000000) {
        found = FALSE;
        count = func_0201add0(work->entries[index].mcss);
        for (i = 0; i < count; i++) {
            if (nodeIdx == func_0201add8(work->entries[index].mcss, i)) {
                found = TRUE;
            }
        }
        if (!found) {
            cellAnim->animCtrl.bActive = FALSE;
        } else {
            cellAnim->animCtrl.bActive = TRUE;
        }
    } else {
        cellAnim->animCtrl.bActive = TRUE;
    }
    return TRUE;
}

static void BtlvMcss_OnSetAnimationEnd(u32 index, fx32 frame) {
    BtlvMcss *work = BtlvEffect_GetMcss();

    work->entries[index].animSet = FALSE;
}

static void BtlvMcss_GetDefaultPosImpl(BtlvMcss *work, VecFx32 *out, int pos) {
    const VecFx32 *src;

    switch (pos) {
    case 0:
    case 1:
        src = &data_ov168_021f327c[pos];
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        if (work->rotation) {
            src = &data_ov168_021f33cc[pos - 2];
        } else if (work->triple) {
            src = &data_ov168_021f3384[pos - 2];
        } else {
            src = &data_ov168_021f32ac[pos - 2];
        }
        break;
    case 6:
    case 7:
        if (work->rotation) {
            src = &data_ov168_021f33cc[pos - 2];
        } else {
            src = &data_ov168_021f3384[pos - 2];
        }
        break;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
        src = &data_ov168_021f333c[pos - 8];
        break;
    }
    out->x = src->x;
    out->y = src->y;
    out->z = src->z;
}

static fx32 BtlvMcss_GetDefaultScaleImpl(BtlvMcss *work, int pos, BOOL flat) {
    if (!flat) {
        switch (pos) {
        case 0:
        case 1:
            return data_ov168_021f31f4[pos];
        case 2:
        case 3:
        case 4:
        case 5:
            if (work->rotation) {
                return data_ov168_021f3234[pos - 2];
            }
            if (work->triple) {
                return data_ov168_021f3264[pos - 2];
            }
            return data_ov168_021f31fc[pos - 2];
        case 6:
        case 7:
            if (work->rotation) {
                return data_ov168_021f3234[pos - 2];
            }
            return data_ov168_021f3264[pos - 2];
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            return data_ov168_021f324c[pos - 8];
        }
        // BUG: Other positions fall off the end without a return value
#ifdef BUGFIX
        return FX32_ONE;
#endif
    } else if (pos < 8) {
        if (pos & 1) {
            return FX32_CONST(16);
        }
        return FX32_CONST(32);
    } else {
        return FX32_CONST(16);
    }
}

static int BtlvMcss_GetIndex(BtlvMcss *work, int pos) {
    int i;

    for (i = 0; i < BTLV_MCSS_POS_MAX; i++) {
        if (work->entries[i].mcss != NULL && pos == work->entries[i].pos) {
            return i;
        }
    }
    return -1;
}

static BOOL BtlvMcss_GetShadowOffset(u32 species, int pos, VecFx32 *offset, u16 *unk6, BOOL *shadow) {
    BOOL found = FALSE;
    u32 i;

    for (i = 0; i < NELEMS(data_ov168_021f3414); i++) {
        if (species == data_ov168_021f3414[i].species && (pos & 1) == data_ov168_021f3414[i].enemy) {
            offset->x = data_ov168_021f3414[i].x;
            offset->y = 0;
            offset->z = data_ov168_021f3414[i].z;
            *unk6 = data_ov168_021f3414[i].unk6;
            *shadow = data_ov168_021f3414[i].shadow;
            found = TRUE;
            break;
        }
    }
    return found;
}
