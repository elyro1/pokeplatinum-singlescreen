// The battle effect manager: it creates the battle view's 3D stage and field, its camera, the Pokémon sprites, the cell
// actors, the gauges, the timer and the BG, runs them each frame, starts the effect scripts of btlv_effvm.c, queueing
// those started while another runs, and keeps the tasks of the view's effects. The name is the ROM's string, from
// GFL_HeapAllocate's asserts. BtlvEffect_Create is swan's name; the rest are ours

#include "battle/btlv_effect.h"
#include "types.h"
#include "battle/btlv_bg3d.h"
#include "shim/bw_shim.h"
#include "battle/btl_main.h"
#include "battle/btl_pokeparam.h"
#include "battle/btl_setup.h"
#include "battle/btlv_b_gauge.h"
#include "battle/btlv_bg.h"
#include "battle/btlv_camera.h"
#include "battle/btlv_clact.h"
#include "battle/btlv_effvm.h"
#include "battle/btlv_field.h"
#include "battle/btlv_gauge.h"
#include "battle/btlv_mcss.h"
#include "battle/btlv_stage.h"
#include "battle/btlv_timer.h"
#include "field/field_light_status.h"
#include "gfl/arc.h"
#include "gfl/bg_sys.h"
#include "gfl/g3d.h"
#include "gfl/heap.h"
#include "gfl/key.h"
#include "gfl/particle.h"
#include "gfl/random.h"
#include "gfl/sound.h"
#include "gfl/tcb.h"
#include "gfl/touchpanel.h"
#include "nitro/fx.h"
#include "nitro/gx.h"
#include "nnsys/g3d.h"
#include "nnsys/gfd.h"
#include "pml/waza.h"
#include "save/player_info.h"
#include "system/mcss.h"
#include "system/palanm.h"
#include "system/vm.h"

// Built by BtlvEffect_Create (swan's name), 0x34 bytes, copied into the work by the init
struct BtlvEffectSetup {
    u32 battleStyle;           // 0x00
    u32 battleType;            // 0x04
    BtlFieldEnv fieldEnv;      // 0x08
    u16 trainerTypes[4];       // 0x18
    BOOL multi;                // 0x20
    BtlvScu *scu;              // 0x24
    BtlMainModule *mainModule; // 0x28
    u16 unk2C;                 // 0x2c  func_ov167_0219c988's value; 1 and 2 are special
    u16 unk2E;                 // 0x2e  the scripted rules' unk24, read when unk2C != 0
    u32 unk30;                 // 0x30  func_ov167_0219c99c's value
};

typedef void (*BtlvEffectTaskCallback)(TCB *task);

#define BTLV_EFFECT_TASK_MAX 32

// The battle effect work, 0x254 bytes, allocated by the init and pointed to by sBtlvEffect
struct BtlvEffect {
    TCBManager *tcbManager;                                     // 0x000
    void *tcbManagerBuffer;                                     // 0x004
    TCB *tasks[BTLV_EFFECT_TASK_MAX];                           // 0x008
    BtlvEffectTaskCallback taskCallbacks[BTLV_EFFECT_TASK_MAX]; // 0x088
    u32 taskGroups[BTLV_EFFECT_TASK_MAX];                       // 0x108
    VM *effvm;                                                  // 0x188
    PaletteFade *palFade;                                       // 0x18c
    BtlvMcss *mcss;                                             // 0x190
    BtlvStage *stage;                                           // 0x194
    BtlvField *field;                                           // 0x198
    BtlvCamera *camera;                                         // 0x19c
    BtlvClact *clact;                                           // 0x1a0
    BtlvGauge *gauge;                                           // 0x1a4
    BtlvBGauge *ballGauges[2];                                  // 0x1a8  by side
    BtlvTimer *timer;                                           // 0x1b0
    BtlvBg *bg;                                                 // 0x1b4
    TCB *vblankTask;                                            // 0x1b8
    BtlvEffectSetup setup;                                      // 0x1bc
    BOOL executing;                                             // 0x1f0
    // 0x1f4
    u32 damageTasks : 8; // bits 0-7: one per view position
    u32 unkTasks8 : 8;   // bits 8-15: one per view position
    u32 rotateTasks : 2; // bits 16-17: one per side
    u32 state : 8;       // bits 18-25
    u32 unk1F4_26 : 1;   // bit 26
    u32 : 5;
    HeapID heapId;       // 0x1f8
    BOOL bgmChanged;     // 0x1fc
    s32 trainers[8];     // 0x200  by pos - 8, -1 when none
    u32 abilities[8];    // 0x220  by view position
    u32 idleEffectMode;  // 0x240
    u32 idleEffectSeq;   // 0x244
    s32 idleEffectTimer; // 0x248
    s32 idleEffectWait;  // 0x24c
    s32 idleEffectLast;  // 0x250
}; // 0x254

// An entry of file 0 of archive 151, by the field environment's bgType; the terrain picks the field and the stage
typedef struct {
    u8 light;        // 0x00  the zone's light and time of day light the battle
    u8 seasonal;     // 0x01  the season picks the variant
    u8 fieldIds[20]; // 0x02
    u8 stageIds[20]; // 0x16
    u8 unk2A[2];
} BtlvEffectBgData;

// The colors FieldLightStatus_Get fills in. The frame of BtlvEffect_Init gives them 4 more bytes, which nothing reads
typedef struct {
    GXRgb colors[8];
    u32 unk10;
} BtlvEffectLightColors;

// An effect started while another one runs, which waits in a task for its turn
typedef struct {
    u32 unk00;
    u32 atkPos;
    u32 defPos;
    u32 effNo;
    BtlvEffvmParam *param; // freed by BtlvEffect_DelayedMoveTaskEnd
} BtlvEffectQueued;

// The flashes of a damaged Pokémon, a blink task
typedef struct {
    u32 pos;     // 0x00
    u32 seq;     // 0x04
    u32 count;   // 0x08  blinks left
    u32 wait;    // 0x0c
    u32 color;   // 0x10
    BOOL hidden; // 0x14  the sprite is hidden, so only the timing runs
} BtlvEffectDamage;

// The change of a Pokémon's sprite
typedef struct {
    u32 viewPos;       // 0x0
    u32 seq;           // 0x4
    MCSSLoadInfo info; // 0x8
} BtlvEffectChange;

// The work of the rotation task that BtlvEffect_StartRotation starts, 0x10 bytes
typedef struct {
    u32 seq;  // 0x0
    u8 arg4;  // 0x4  mode != 3
    u32 side; // 0x8
    u32 argC; // 0xc
} BtlvEffectRotateWork;

static u32 BtlvEffect_GetStudioMode(BtlMainModule *mainModule);
static u32 BtlvEffect_GetStudioParam(BtlMainModule *mainModule);
static void BtlvEffect_Stop(void);
static void BtlvEffect_GetZoomCameraPosTarget(u32 viewPos, VecFx32 *camPos, VecFx32 *camTarget);
static int BtlvEffect_FindTask(TCB *tcb);
static int BtlvEffect_GetFreeTaskSlot(void);
static void BtlvEffect_EndAllTasks(void);
static void BtlvEffect_VBlankTask(TCB *tcb, void *data);
static void BtlvEffect_BlinkTask(TCB *tcb, void *data);
static void BtlvEffect_DamageTaskEnd(TCB *tcb);
static void BtlvEffect_SubstituteTask(TCB *tcb, void *data);
static void BtlvEffect_Unk8TaskEnd(TCB *tcb);
static void BtlvEffect_RotateTask(TCB *tcb, void *data);
static void BtlvEffect_RotateTaskEnd(TCB *tcb);
static void BtlvEffect_DelayedMoveTask(TCB *tcb, void *data);
static void BtlvEffect_DelayedMoveTaskEnd(TCB *tcb);
static void BtlvEffect_UpdateIdleEffect(void);

// The idle effects BtlvEffect_UpdateIdleEffect picks from: in mode 3 of a battle of type 0x400, and in the others
static u32 data_ov168_021f3f50[] = { 0x24a, 0x24b };
static u32 data_ov168_021f3f58[] = { 0x235, 0x24c, 0x24d, 0x24e, 0x24f, 0x250 };

// Picked by func_ov167_0219c9b0 when func_ov167_0219c988 is 2
static u8 data_ov168_021f3f70[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1,
};

static BtlvEffect *sBtlvEffect;

static u32 BtlvEffect_GetStudioMode(BtlMainModule *mainModule) {
    if (mainModule != NULL) {
        return func_ov167_0219c988(mainModule);
    }
    return 0;
}

static u32 BtlvEffect_GetStudioParam(BtlMainModule *mainModule) {
    if (mainModule != NULL) {
        return func_ov167_0219c99c(mainModule);
    }
    return 0;
}

BtlvEffectSetup *BtlvEffect_Create(u32 battleStyle, u32 battleType, const BtlFieldEnv *fieldEnv, BOOL multi,
                                   const u16 *trainerTypes, BtlMainModule *mainModule, BtlvScu *scu, HeapID heapId) {
    BtlvEffectSetup *setup =
        GFL_HeapAllocate(HEAPID_TAIL(heapId), sizeof(BtlvEffectSetup), FALSE, "btlv_effect.c", 0x107);
    int i;

    setup->battleStyle = battleStyle;
    setup->battleType = battleType;
    setup->fieldEnv = *fieldEnv;
    setup->multi = multi;
    setup->scu = scu;
    setup->mainModule = mainModule;
    setup->unk30 = BtlvEffect_GetStudioParam(mainModule);
    setup->unk2C = BtlvEffect_GetStudioMode(mainModule);
    if (setup->unk2C != 0) {
        setup->unk2E = func_ov167_0219e39c(mainModule)->unk24;
    }
    for (i = 0; i < 4; i++) {
        setup->trainerTypes[i] = trainerTypes[i];
    }
    return setup;
}

BtlvEffectSetup *BtlvEffect_CreateSetup(BtlMainModule *mainModule, BtlvScu *scu, HeapID heapId) {
    u16 trainerTypes[4];
    u32 i;

    for (i = 0; i < 4; i++) {
        trainerTypes[i] = 0;
    }
    if (func_ov167_0219beec(mainModule) == TRUE) {
        u8 side = func_ov167_0219c850(mainModule);
        u8 playerId = GetPlayerClientID(mainModule);
        u8 partnerId = func_ov167_0219c86c(mainModule);

        trainerTypes[side * 2] = func_ov167_0219d938(mainModule, playerId);
        if (partnerId != 4) {
            trainerTypes[(side ^ 1) * 2] = func_ov167_0219d938(mainModule, partnerId);
        }
        trainerTypes[1] = func_ov167_0219d938(mainModule, func_ov167_0219c8b8(mainModule, 0));
        trainerTypes[3] = func_ov167_0219d938(mainModule, func_ov167_0219c8b8(mainModule, 1));
    } else {
        trainerTypes[0] = func_ov167_0219d938(mainModule, GetPlayerClientID(mainModule));
        trainerTypes[1] = func_ov167_0219d938(mainModule, func_ov167_0219c8b8(mainModule, 0));
        trainerTypes[2] = 0;
        trainerTypes[3] = 0;
    }
    return BtlvEffect_Create(BtlSetup_GetBattleStyle(mainModule), BtlSetup_GetBattleType(mainModule),
                             &GetFieldEffectData(mainModule)->env, func_ov167_0219beec(mainModule), trainerTypes,
                             mainModule, scu, heapId);
}

// NONMATCHING: 2 bytes. The seasonal test is `cmp r0, #0` where the game has `lsls r0, r0, #1` after the `ldrb`
// (the flags come from the expression's last operation; only `seasonal * 2`, `<< 1` or `+ seasonal` give that, and
// none is natural; s8, char, BOOL8, bitfields of every width, packed u16/u32 fields, masks and casts don't).
// `setup` must not be `const`: with it, the loads from `setup` move above the stack-argument stores of
// BtlvStage_Create and FieldLightStatus_Get, 34 bytes.
void BtlvEffect_Init(BtlvEffectSetup *setup, Font *font, HeapID heapId) {
    BtlvEffectBgData *bgData;
    u8 season;
    int i;

    sBtlvEffect = GFL_HeapAllocate(heapId, sizeof(BtlvEffect), TRUE, "btlv_effect.c", 0x15c);
    sBtlvEffect->setup = *setup;
    sBtlvEffect->heapId = heapId;
    sBtlvEffect->tcbManagerBuffer =
        GFL_HeapAllocate(heapId, GFL_TCBMgrCalcAllocSize(BTLV_EFFECT_TASK_MAX), TRUE, "btlv_effect.c", 0x162);
    sBtlvEffect->tcbManager = GFL_TCBMgrCreate(BTLV_EFFECT_TASK_MAX, sBtlvEffect->tcbManagerBuffer);
    sBtlvEffect->effvm = BtlvEffvm_Create(sBtlvEffect->tcbManager, heapId);
    sBtlvEffect->palFade = PaletteFade_Create(heapId);
    PaletteFade_SetTransferAll(sBtlvEffect->palFade, TRUE);
    PaletteFade_AllocBuffer(sBtlvEffect->palFade, 0, 0x200, heapId);
    PaletteFade_AllocBuffer(sBtlvEffect->palFade, 1, 0x1e0, heapId);
    PaletteFade_AllocBuffer(sBtlvEffect->palFade, 2, 0x200, heapId);
    PaletteFade_AllocBuffer(sBtlvEffect->palFade, 3, 0x1e0, heapId);
    sBtlvEffect->mcss = BtlvMcss_Create(setup->battleStyle, sBtlvEffect->tcbManager, heapId);
    if (BtlvEffect_GetStudioMode(setup->mainModule) == 2) {
        BtlvMcss_SetShadows(sBtlvEffect->mcss, data_ov168_021f3f70[func_ov167_0219c9b0(setup->mainModule)]);
    } else {
        BtlvMcss_SetShadows(sBtlvEffect->mcss, 1);
    }

    bgData = GFL_ArcSysReadHeapNew(0x97, 0, HEAPID_TAIL(sBtlvEffect->heapId));
    season = 0;
    if (bgData[setup->fieldEnv.bgType].seasonal) {
        season = setup->fieldEnv.season;
    }
    sBtlvEffect->stage =
        BtlvStage_Create(setup->battleStyle, bgData[setup->fieldEnv.bgType].stageIds[setup->fieldEnv.terrain], season,
                         heapId, setup->unk2C);
    if (sBtlvEffect->setup.mainModule != NULL) {
        sBtlvEffect->field = BtlvField_Create(BtlSetup_IsBattleType(sBtlvEffect->setup.mainModule, 0x400),
                                              bgData[setup->fieldEnv.bgType].fieldIds[setup->fieldEnv.terrain], season,
                                              heapId, setup->unk2C, setup->unk2E, setup->unk30);
    } else {
        sBtlvEffect->field = BtlvField_Create(FALSE, bgData[setup->fieldEnv.bgType].fieldIds[setup->fieldEnv.terrain],
                                              season, heapId, setup->unk2C, setup->unk2E, setup->unk30);
    }
    if (bgData[setup->fieldEnv.bgType].light) {
        Light light;
        BtlvEffectLightColors colors;

        FieldLightStatus_Get(setup->fieldEnv.zoneId, setup->fieldEnv.hour, setup->fieldEnv.minute,
                             setup->fieldEnv.weather, setup->fieldEnv.season, colors.colors, sBtlvEffect->heapId);
        light.color = colors.colors[0];
        light.direction.x = 0;
        light.direction.y = -FX16_ONE;
        light.direction.z = 0;
        GFL_G3DSysLightSet(0, &light);
    }
    GFL_HeapFree(bgData);

    sBtlvEffect->camera = BtlvCamera_Create(sBtlvEffect->tcbManager, heapId);
    sBtlvEffect->clact =
        BtlvClact_Create(sBtlvEffect->tcbManager, heapId, BtlvEffect_GetStudioMode(sBtlvEffect->setup.mainModule));
    sBtlvEffect->gauge = BtlvGauge_Create(font, setup->unk2C, heapId);
    sBtlvEffect->timer = BtlvTimer_Create(heapId);
    sBtlvEffect->bg = BtlvBg_Create(sBtlvEffect->tcbManager, heapId);
    if (BtlvEffect_GetStudioMode(sBtlvEffect->setup.mainModule) == 1) {
        BtlvClact_SetGauge(sBtlvEffect->clact, func_ov167_0219e39c(sBtlvEffect->setup.mainModule)->turnLimit, 0);
    }
    BtlvMcss_SetFlatAll(sBtlvEffect->mcss);
    func_0204f918(heapId);
    for (i = 0; i < 8; i++) {
        sBtlvEffect->trainers[i] = -1;
    }
    if (sBtlvEffect->setup.mainModule != NULL) {
        if (BtlSetup_IsBattleType(sBtlvEffect->setup.mainModule, 0x800)) {
            sBtlvEffect->idleEffectWait = 300;
        } else {
            sBtlvEffect->idleEffectWait = 900;
        }
    }
    sBtlvEffect->vblankTask = GFL_VBlankTCBAdd(BtlvEffect_VBlankTask, NULL, 1);
    PokeVoice_AllocMulti(2, sBtlvEffect->heapId);
}

void BtlvEffect_Exit(void) {
    if (sBtlvEffect != NULL) {
        PokeVoice_FreeMulti();
        BtlvEffect_EndAllTasks();
        PaletteFade_FreeBuffer(sBtlvEffect->palFade, 0);
        PaletteFade_FreeBuffer(sBtlvEffect->palFade, 1);
        PaletteFade_FreeBuffer(sBtlvEffect->palFade, 2);
        PaletteFade_FreeBuffer(sBtlvEffect->palFade, 3);
        PaletteFade_Free(sBtlvEffect->palFade);
        BtlvMcss_Delete(sBtlvEffect->mcss);
        BtlvStage_Delete(sBtlvEffect->stage);
        BtlvField_Delete(sBtlvEffect->field);
        BtlvCamera_Delete(sBtlvEffect->camera);
        BtlvClact_Delete(sBtlvEffect->clact);
        BtlvGauge_Delete(sBtlvEffect->gauge);
        BtlvTimer_Delete(sBtlvEffect->timer);
        BtlvBg_Delete(sBtlvEffect->bg);
        BtlvBg3d_Free();
        func_0204fb4c();
        BtlvEffvm_Delete(sBtlvEffect->effvm);
        GFL_TCBRemove(sBtlvEffect->vblankTask);
        func_0203a610(sBtlvEffect->tcbManager);
        GFL_HeapFree(sBtlvEffect->tcbManagerBuffer);
        GFL_HeapFree(sBtlvEffect);
        sBtlvEffect = NULL;
    }
}

#ifdef BWBATTLE
static BOOL sTickIdle = TRUE;

BOOL BtlvEffect_ShouldTickIdle(void) {
    return sTickIdle;
}

BOOL BtlvEffect_StartFastWildSendOut(void) {
    if (BtlvEffect_IsBusy()) {
        return FALSE;
    }
    BtlvEffect_Start(0x234);
    BtlvEffvm_OverlapSendOutCry(sBtlvEffect->effvm);
    BtlvEffvm_PrefixWildShinies(sBtlvEffect->effvm);
    return TRUE;
}

BOOL BtlvEffect_StartFastSendOut(int viewPos, u16 effect) {
    if (BtlvEffect_IsBusy()) {
        return FALSE;
    }
    if (viewPos < 0) {
        BtlvEffect_Start(effect);
    } else {
        BtlvEffect_StartPos(viewPos, effect);
    }
    BtlvEffvm_OverlapSendOutCry(sBtlvEffect->effvm);
    return TRUE;
}

// bwbattle: a frame of the view without its 3D drawing (BtlvEffect_Main's first part, and the particles' update of
// its last), for a frame of the port that has to run more than one of BW2's
static void BtlvEffect_UpdateFrame(void) {
    sBtlvEffect->executing = BtlvEffvm_Main(sBtlvEffect->effvm);
    BtlvEffect_UpdateIdleEffect();
    GFL_TCBMgrUpdate(sBtlvEffect->tcbManager);
    BtlvMcss_Main(sBtlvEffect->mcss);
    BtlvStage_Main(sBtlvEffect->stage);
    BtlvField_Main(sBtlvEffect->field);
    BtlvCamera_Main(sBtlvEffect->camera);
    BtlvGauge_Update(sBtlvEffect->gauge);
    BtlvClact_Draw(sBtlvEffect->clact);
}

void BtlvEffect_Step(BOOL tickIdle) {
    if (sBtlvEffect != NULL) {
        sTickIdle = tickIdle;
        BtlvEffect_UpdateFrame();
        if (func_0204fffc() != 0) {
            func_02050044();
        }
        sTickIdle = TRUE;
    }
}
#endif

void BtlvEffect_Main(void) {
    if (sBtlvEffect != NULL) {
#ifdef BWBATTLE
        BtlvEffect_UpdateFrame();
#else
        sBtlvEffect->executing = BtlvEffvm_Main(sBtlvEffect->effvm);
        BtlvEffect_UpdateIdleEffect();
        GFL_TCBMgrUpdate(sBtlvEffect->tcbManager);
        BtlvMcss_Main(sBtlvEffect->mcss);
        BtlvStage_Main(sBtlvEffect->stage);
        BtlvField_Main(sBtlvEffect->field);
        BtlvCamera_Main(sBtlvEffect->camera);
        BtlvGauge_Update(sBtlvEffect->gauge);
        BtlvClact_Draw(sBtlvEffect->clact);
#endif
        GFL_G3DSysReset();
        GFL_G3DSysMtxViewFlush();
        BtlvBg3d_Draw(FALSE);
        BtlvStage_Draw(sBtlvEffect->stage);
        BtlvField_Draw(sBtlvEffect->field);
        BtlvMcss_Draw(sBtlvEffect->mcss);
        // The game reads the polygon list RAM count before each of these and drops it, likely what is left of a debug
        // print of the counts
        reg_G3X_RAM_COUNT;
        func_0204f954();
        reg_G3X_RAM_COUNT;
        BtlvBg3d_Draw(TRUE);
        GFL_G3DSysReqSwapBuffers();
    }
}

void BtlvEffect_Start(u32 effNo) {
    if (!BtlvEffect_IsBusy()) {
        BtlvEffvm_Start(sBtlvEffect->effvm, 0xff, 0xff, effNo, NULL);
        sBtlvEffect->executing = TRUE;
    } else {
        BtlvEffectQueued *queued =
            GFL_HeapAllocate(HEAPID_TAIL(sBtlvEffect->heapId), sizeof(BtlvEffectQueued), TRUE, "btlv_effect.c", 0x281);

        queued->atkPos = 0xff;
        queued->defPos = 0xff;
        queued->effNo = effNo;
        BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_DelayedMoveTask, queued, 0),
                           BtlvEffect_DelayedMoveTaskEnd, 0);
    }
}

void BtlvEffect_StartPos(u32 pos, u32 effNo) {
    if (!BtlvEffect_IsBusy()) {
        BtlvEffvm_Start(sBtlvEffect->effvm, pos, 0xff, effNo, NULL);
        sBtlvEffect->executing = TRUE;
    } else {
        BtlvEffectQueued *queued =
            GFL_HeapAllocate(sBtlvEffect->heapId, sizeof(BtlvEffectQueued), TRUE, "btlv_effect.c", 0x29d);

        queued->atkPos = pos;
        queued->defPos = 0xff;
        queued->effNo = effNo;
        BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_DelayedMoveTask, queued, 0),
                           BtlvEffect_DelayedMoveTaskEnd, 0);
    }
}

void BtlvEffect_StartAtkDef(u32 atkPos, u32 defPos, u32 effNo) {
    if (!BtlvEffect_IsBusy()) {
        BtlvEffvm_Start(sBtlvEffect->effvm, atkPos, defPos, effNo, NULL);
        sBtlvEffect->executing = TRUE;
    } else {
        BtlvEffectQueued *queued =
            GFL_HeapAllocate(sBtlvEffect->heapId, sizeof(BtlvEffectQueued), TRUE, "btlv_effect.c", 0x2ba);

        queued->atkPos = atkPos;
        queued->defPos = defPos;
        queued->effNo = effNo;
        BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_DelayedMoveTask, queued, 0),
                           BtlvEffect_DelayedMoveTaskEnd, 0);
    }
}

void BtlvEffect_StartMove(const BtlvMoveEffectParam *param) {
    if (!BtlvEffect_IsBusy()) {
        BtlvEffvmParam vmParam;

        BtlvEffvmParam_Clear(&vmParam);
        vmParam.unk0 = param->unk10;
        vmParam.variant = param->unk0C;
        vmParam.unk2 = param->unk0D;
        BtlvEffvm_Start(sBtlvEffect->effvm, param->attackerPos, param->targetPos, param->move, &vmParam);
        sBtlvEffect->executing = TRUE;
    } else {
        BtlvEffectQueued *queued =
            GFL_HeapAllocate(sBtlvEffect->heapId, sizeof(BtlvEffectQueued), TRUE, "btlv_effect.c", 0x2dc);

        queued->param = GFL_HeapAllocate(sBtlvEffect->heapId, sizeof(BtlvEffvmParam), TRUE, "btlv_effect.c", 0x2dd);
        queued->atkPos = param->attackerPos;
        queued->defPos = param->targetPos;
        queued->effNo = param->move;
        queued->param->unk0 = param->unk10;
        queued->param->variant = param->unk0C;
        queued->param->unk2 = param->unk0D;
        BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_DelayedMoveTask, queued, 0),
                           BtlvEffect_DelayedMoveTaskEnd, 0);
    }
}

static void BtlvEffect_Stop(void) {
    BtlvEffvm_Stop(sBtlvEffect->effvm);
    sBtlvEffect->executing = FALSE;
}

void BtlvEffect_Resume(void) {
    BtlvEffvm_Resume(sBtlvEffect->effvm);
    sBtlvEffect->executing = TRUE;
}

void BtlvEffect_StartDamage(u32 pos, u16 move) {
    BtlvEffectDamage *damage =
        GFL_HeapAllocate(HEAPID_TAIL(sBtlvEffect->heapId), sizeof(BtlvEffectDamage), FALSE, "btlv_effect.c", 0x30c);

    damage->seq = 0;
    damage->pos = pos;
    damage->count = 3;
    damage->wait = 0;
    damage->hidden = BtlvMcss_GetVanish(sBtlvEffect->mcss, pos);
    if (PML_MoveGetCategory(move) == MOVE_CATEGORY_PHYSICAL) {
        damage->color = GX_RGB(2, 2, 2);
    } else {
        damage->color = GX_RGB(24, 24, 24);
    }
    sBtlvEffect->damageTasks |= BtlvEffect_PosBit(pos);
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_BlinkTask, damage, 0),
                       BtlvEffect_DamageTaskEnd, 0);
}

void BtlvEffect_StartEffect23B(u8 viewPos) {
    BtlvEffect_StartPos(viewPos, 0x23b);
    BtlvEffect_SetAbility(viewPos, 0);
}

void BtlvEffect_StartEffect23A(u8 pos, u16 itemNo, u8 arg2, u32 arg3, u32 arg4) {
    BtlvEffvmParam vmParam;

    BtlvEffvmParam_Clear(&vmParam);
    vmParam.unk3 = arg2;
    vmParam.unk8 = arg4;
    vmParam.unk4 = arg3;
    vmParam.itemNo = itemNo;
    BtlvEffvm_Start(sBtlvEffect->effvm, 0, pos, 0x23a, &vmParam);
    sBtlvEffect->executing = TRUE;
}

void BtlvEffect_StartEffect23D(u8 pos, u16 itemNo) {
    BtlvEffvmParam vmParam;

    BtlvEffvmParam_Clear(&vmParam);
    vmParam.itemNo = itemNo;
    BtlvEffvm_Start(sBtlvEffect->effvm, 0, pos, 0x23d, &vmParam);
    sBtlvEffect->executing = TRUE;
}

void BtlvEffect_StartChangeSprite(PartyPkm *pkm, u32 viewPos) {
    BtlvEffectChange *change =
        GFL_HeapAllocate(HEAPID_TAIL(sBtlvEffect->heapId), sizeof(BtlvEffectChange), FALSE, "btlv_effect.c", 0x36a);

    change->seq = 0;
    change->viewPos = viewPos;
    BtlvMcss_ChangePokemon(sBtlvEffect->mcss, viewPos, pkm);
    BtlvMcss_SetupPokemonLoadInfo(sBtlvEffect->mcss, pkm, &change->info, viewPos);
    sBtlvEffect->unkTasks8 |= BtlvEffect_PosBit(viewPos);
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_SubstituteTask, change, 0),
                       BtlvEffect_Unk8TaskEnd, 0);
}

void BtlvEffect_ChangeSprite(PartyPkm *pkm, u32 viewPos) {
    MCSSLoadInfo info;

    BtlvMcss_ChangePokemon(sBtlvEffect->mcss, viewPos, pkm);
    BtlvMcss_SetupPokemonLoadInfo(sBtlvEffect->mcss, pkm, &info, viewPos);
    BtlvMcss_ReloadSprite(sBtlvEffect->mcss, viewPos, &info);
}

void BtlvEffect_StartEffect285(u8 pos) {
    BtlvEffect_StartPos(pos, 0x285);
}

void BtlvEffect_StartEffect286(u8 pos) {
    BtlvEffect_StartPos(pos, 0x286);
}

void BtlvEffect_StartEffect26E(u8 pos) {
    BtlvEffect_StartPos(pos, 0x26e);
}

void BtlvEffect_StartEffect26F(u8 pos) {
    BtlvEffect_StartPos(pos, 0x26f);
}

BOOL BtlvEffect_IsBusy(void) {
    if ((sBtlvEffect->executing | sBtlvEffect->damageTasks | sBtlvEffect->unkTasks8 | sBtlvEffect->rotateTasks) != 0) {
        return TRUE;
    }
    return FALSE;
}

void BtlvEffect_AddPokemon(PartyPkm *pkm, u32 viewPos) {
    BtlvMcss_AddPokemon(sBtlvEffect->mcss, pkm, viewPos);
}

void BtlvEffect_DelPokemon(u32 viewPos) {
    BtlvMcss_Remove(sBtlvEffect->mcss, viewPos);
}

BOOL BtlvEffect_CheckExist(int pos) {
    if (pos < 8) {
        return BtlvMcss_Exists(sBtlvEffect->mcss, pos);
    }
    return sBtlvEffect->trainers[pos - 8] != -1;
}

void BtlvEffect_SetTrainer(s32 trainerType, u32 pos, fx32 x, fx32 y, fx32 z) {
    switch (sBtlvEffect->setup.unk2C) {
    case 1:
        if (trainerType == 0 || trainerType == 1) {
            trainerType = 0;
            if (getTrainerGender(func_ov167_0219d97c(sBtlvEffect->setup.mainModule, 0))) {
                trainerType = 1;
            }
        }
        break;
    case 2:
        if (trainerType == 0 || trainerType == 1) {
            trainerType = func_ov167_0219d938(sBtlvEffect->setup.mainModule, 0);
        }
        break;
    }
    if (!(pos & 1)) {
        switch (trainerType) {
        case 0:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType = 0xb4;
            }
            break;
        case 1:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType = 0xb5;
            }
            break;
        case 0xb6:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType = 0xb8;
            }
            break;
        case 0xb7:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType = 0xb9;
            }
            break;
        }
    } else {
        switch (trainerType) {
        case 0:
        case 1:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType += 0xb4;
            }
            break;
        case 0xb6:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType = 0xb8;
            }
            break;
        case 0xb7:
            if (BtlvEffect_GetUnk2E4()) {
                trainerType = 0xb9;
            }
            break;
        }
    }
    BtlvMcss_AddTrainer(sBtlvEffect->mcss, trainerType, pos);
    if (x != 0 || y != 0 || z != 0) {
        BtlvMcss_SetPosition(sBtlvEffect->mcss, pos, x, y, z);
    }
#ifdef BWBATTLE
    // bwbattle: an image the host keeps for the place stands in for the trainer's sprite (BwBattleView_SetTrainerImage)
    BwView_OnTrainerAdded(pos);
#endif
    sBtlvEffect->trainers[pos - 8] = pos;
}

void BtlvEffect_DelTrainer(u32 pos) {
    if (sBtlvEffect->trainers[pos - 8] != -1) {
        BtlvMcss_Remove(sBtlvEffect->mcss, pos);
        sBtlvEffect->trainers[pos - 8] = -1;
    }
}

void BtlvEffect_SetPokeAnimeSpeed(u8 viewPos, fx32 speed) {
    BtlvMcss_SetAnimSpeed(sBtlvEffect->mcss, viewPos, speed);
}

void BtlvEffect_AddGauge(BtlMainModule *mainModule, BattleMon *mon, u32 viewPos) {
    switch (sBtlvEffect->setup.battleStyle) {
    case 2:
        BtlvGauge_SetBattleMon(sBtlvEffect->gauge, mainModule, mon, 2, viewPos);
        break;
    case 3:
        BtlvGauge_SetBattleMon(sBtlvEffect->gauge, mainModule, mon, 3, viewPos);
        break;
    default:
        BtlvGauge_SetBattleMon(sBtlvEffect->gauge, mainModule, mon, 0, viewPos);
        break;
    }
}

void BtlvEffect_AddGaugeByPkm(PokeDexSave *pokedex, PartyPkm *pkm, u32 viewPos) {
    switch (sBtlvEffect->setup.battleStyle) {
    case 2:
        BtlvGauge_SetPartyPkm(sBtlvEffect->gauge, pokedex, pkm, 2, viewPos);
        break;
    case 3:
        BtlvGauge_SetPartyPkm(sBtlvEffect->gauge, pokedex, pkm, 3, viewPos);
        break;
    default:
        BtlvGauge_SetPartyPkm(sBtlvEffect->gauge, pokedex, pkm, 0, viewPos);
        break;
    }
}

void BtlvEffect_DelGauge(u32 viewPos) {
    BtlvGauge_Hide(sBtlvEffect->gauge, viewPos);
}

void BtlvEffect_CalcGaugeHP(u32 viewPos, s32 hp) {
    BtlvGauge_StartHpChange(sBtlvEffect->gauge, viewPos, hp);
}

void BtlvEffect_CalcGaugeHPAtOnce(u32 viewPos, s32 hp) {
    BtlvGauge_SetHpAtOnce(sBtlvEffect->gauge, viewPos, hp);
}

void BtlvEffect_CalcGaugeExp(u8 pos, s32 exp, BattleMon *mon) {
    int level = GetBattleMonStat(mon, BATTLEMON_LEVEL);

    if (level < 100) {
        BtlvGauge_StartExpGain(sBtlvEffect->gauge, pos, exp);
    }
}

void BtlvEffect_CalcGaugeExpLevelUp(u8 pos, BattleMon *mon) {
    BtlvGauge_StartLevelUp(sBtlvEffect->gauge, mon, pos);
}

void BtlvEffect_SetGaugeFlag(void) {
    BtlvGauge_RequestNumberToggle(sBtlvEffect->gauge);
}

BOOL BtlvEffect_CheckExecuteGauge(void) {
    return BtlvGauge_IsBusy(sBtlvEffect->gauge);
}

void BtlvEffect_SetGaugeDrawEnableBySide(BOOL visible, u32 side) {
    BtlvGauge_SetSideVisible(sBtlvEffect->gauge, visible, side);
}

void BtlvEffect_SetGaugeDrawEnable(BOOL visible, u32 viewPos) {
    BtlvGauge_SetVisible(sBtlvEffect->gauge, visible, viewPos);
}

void BtlvEffect_SetGaugeStatus(u32 status, u8 pos) {
    BtlvGauge_SetStatus(sBtlvEffect->gauge, status, pos);
}

void BtlvEffect_GaugeAction(u32 viewPos) {
    BtlvGauge_StartShake(sBtlvEffect->gauge, viewPos);
}

BOOL BtlvEffect_CheckGaugeExist(u8 pos) {
    return BtlvGauge_IsShown(sBtlvEffect->gauge, pos);
}

BOOL BtlvEffect_GetGaugeStatus(u32 viewPos, u32 *color, u32 *status) {
    return BtlvGauge_GetStatus(sBtlvEffect->gauge, viewPos, color, status);
}

void BtlvEffect_AddBallGauge(const BtlvBGaugeParam *param) {
    sBtlvEffect->ballGauges[param->side] = BtlvBGauge_Create(param, sBtlvEffect->heapId);
}

void BtlvEffect_DelBallGauge(u32 side) {
    BtlvBGauge_Delete(sBtlvEffect->ballGauges[side]);
}

BOOL BtlvEffect_IsBallGaugeBusy(u32 side) {
    return BtlvBGauge_IsBusy(sBtlvEffect->ballGauges[side]);
}

// target: 0 the stage, 1 the field, 2 both, 3 the palettes, 4 all
void BtlvEffect_StartPaletteFade(u32 target, u8 evy, u8 targetEvy, u8 wait, u16 color) {
    if (target == 0 || target == 2 || target == 4) {
        BtlvStage_StartPaletteFade(sBtlvEffect->stage, evy, targetEvy, wait, color);
    }
    if (target == 1 || target == 2 || target == 4) {
        BtlvField_StartPaletteFade(sBtlvEffect->field, evy, targetEvy, wait, color);
    }
    if (target == 3 || target == 4) {
        PaletteFade_StartFade(sBtlvEffect->palFade, 1, 0xff00, wait, evy, targetEvy, color, sBtlvEffect->tcbManager);
    }
}

BOOL BtlvEffect_IsPaletteFading(u32 target) {
    BOOL stage = FALSE;
    BOOL field = FALSE;
    BOOL palette = FALSE;

    if (target == 0 || target == 2 || target == 4) {
        stage = BtlvStage_IsPaletteFading(sBtlvEffect->stage);
    }
    if (target == 1 || target == 2 || target == 4) {
        field = BtlvField_IsPaletteFading(sBtlvEffect->field);
    }
    if (target == 3 || target == 4) {
        palette = PaletteFade_GetActiveMask(sBtlvEffect->palFade) != 0;
    }
    if (stage == TRUE || field == TRUE || palette == TRUE) {
        return TRUE;
    }
    return FALSE;
}

// target: 0 the stage, 1 the field, 2 BG 3, 3 and 4 the stage's sides
void BtlvEffect_SetVanish(u32 target, BOOL hide) {
    switch (target) {
    case 0:
        BtlvStage_SetVanish(sBtlvEffect->stage, 2, hide);
        break;
    case 1:
        BtlvField_SetHidden(sBtlvEffect->field, hide);
        break;
    case 2:
        GFL_BGSysSetBGEnabled(3, hide ^ 1);
        break;
    case 3:
        BtlvStage_SetVanish(sBtlvEffect->stage, 0, hide);
        break;
    case 4:
        BtlvStage_SetVanish(sBtlvEffect->stage, 1, hide);
        break;
    }
}

void BtlvEffect_StartRotation(u8 mode, u32 side, u32 arg2) {
    BtlvEffectRotateWork *work =
        GFL_HeapAllocate(HEAPID_TAIL(sBtlvEffect->heapId), sizeof(BtlvEffectRotateWork), FALSE, "btlv_effect.c", 0x602);

    // BUG: mode 1 starts no task, so nothing frees its work
    if (mode != 1) {
        work->seq = 0;
        work->arg4 = mode != 3;
        work->side = side;
        work->argC = arg2;
        sBtlvEffect->rotateTasks |= BtlvEffect_PosBit(side);
        BtlvEffect_AddTask(GFL_TCBMgrAddTask(sBtlvEffect->tcbManager, BtlvEffect_RotateTask, work, 0),
                           BtlvEffect_RotateTaskEnd, 0);
#ifdef BUGFIX
    } else {
        GFL_HeapFree(work);
#endif
    }
}

void BtlvEffect_SwapPokemon(u8 pos1, u8 pos2) {
    BtlvMcss_SwapPositions(sBtlvEffect->mcss, pos1, pos2);
}

s32 BtlvEffect_GetTrainerIndex(u32 pos) {
    return sBtlvEffect->trainers[pos - 8];
}

void BtlvEffect_CreateTimer(u16 gameLimitTime, u16 cmdLimitTime) {
    BtlvTimer_Start(sBtlvEffect->timer, gameLimitTime, cmdLimitTime);
}

void BtlvEffect_SetTimerVisible(int which, BOOL visible, BOOL restart) {
    BtlvTimer_SetVisible(sBtlvEffect->timer, which, visible, restart);
}

BOOL BtlvEffect_IsTimeUp(int which) {
    return BtlvTimer_IsTimeUp(sBtlvEffect->timer, which);
}

BOOL BtlvEffect_CheckExistPokemon(u8 pos) {
    return BtlvMcss_GetVanish(sBtlvEffect->mcss, pos);
}

void BtlvEffect_ZoomCamera(u32 pos, u32 mode, int frames, int wait, int brakeFrames) {
    VecFx32 camPos;
    VecFx32 camTarget;

    if (pos & 1) {
        BtlvEffect_GetZoomCameraPosTarget(pos, &camPos, &camTarget);
        switch (mode) {
        case 0:
            BtlvCamera_SetPosTarget(sBtlvEffect->camera, &camPos, &camTarget);
            break;
        case 1:
            BtlvCamera_MoveTo(sBtlvEffect->camera, &camPos, &camTarget, frames, wait, brakeFrames);
            break;
        }
    } else {
        switch (sBtlvEffect->setup.battleStyle) {
        case 0:
            BtlvEffect_Start(0x23f);
            break;
        case 1:
            if (pos == 2) {
                BtlvEffect_Start(0x240);
            } else {
                BtlvEffect_Start(0x241);
            }
            break;
        case 2:
            if (pos == 2) {
                BtlvEffect_Start(0x242);
            } else if (pos == 4) {
                BtlvEffect_Start(0x23f);
            } else {
                BtlvEffect_Start(0x243);
            }
            break;
        case 3:
            BtlvEffect_Start(0x245);
            break;
        }
    }
}

static void BtlvEffect_GetZoomCameraPosTarget(u32 viewPos, VecFx32 *camPos, VecFx32 *camTarget) {
    BtlvMcss_GetDefaultPos(sBtlvEffect->mcss, camTarget, viewPos);
    camPos->x = camTarget->x + 0x4b33;
    camPos->y = camTarget->y + 0x2299;
    camPos->z = camTarget->z + 0x99cd;
    camTarget->y += FX32_CONST(2);
}

BtlvEffect *BtlvEffect_GetWork(void) {
    return sBtlvEffect;
}

BtlvCamera *BtlvEffect_GetCamera(void) {
    return sBtlvEffect->camera;
}

BtlvMcss *BtlvEffect_GetMcss(void) {
    return sBtlvEffect->mcss;
}

BtlvStage *BtlvEffect_GetStage(void) {
    return sBtlvEffect->stage;
}

BtlvGauge *BtlvEffect_GetGauge(void) {
    return sBtlvEffect->gauge;
}

VM *BtlvEffect_GetEffvm(void) {
    return sBtlvEffect->effvm;
}

TCBManager *BtlvEffect_GetTCBManager(void) {
    return sBtlvEffect->tcbManager;
}

PaletteFade *BtlvEffect_GetPaletteFade(void) {
    return sBtlvEffect->palFade;
}

BtlvClact *BtlvEffect_GetClact(void) {
    return sBtlvEffect->clact;
}

BtlvBg *BtlvEffect_GetBg(void) {
    return sBtlvEffect->bg;
}

u32 BtlvEffect_GetBattleStyle(void) {
    return sBtlvEffect->setup.battleStyle;
}

u32 BtlvEffect_GetBattleType(void) {
    return sBtlvEffect->setup.battleType;
}

BOOL BtlvEffect_IsMulti(void) {
    return sBtlvEffect->setup.multi;
}

u16 BtlvEffect_GetTrainerType(u32 pos) {
    return sBtlvEffect->setup.trainerTypes[pos];
}

BtlMainModule *BtlvEffect_GetMainModule(void) {
    return sBtlvEffect->setup.mainModule;
}

BtlvScu *BtlvEffect_GetScu(void) {
    return sBtlvEffect->setup.scu;
}

BOOL BtlvEffect_IsPinchBgm(void) {
    return BtlvGauge_IsPinch(sBtlvEffect->gauge);
}

void BtlvEffect_PlayBgm(u32 bgm) {
    if (BtlvGauge_IsPinch(sBtlvEffect->gauge)) {
        BtlvGauge_SetPinch(sBtlvEffect->gauge, FALSE);
        GFL_SndBGMPop();
    }
    BtlvGauge_SetNoPinchBgm(sBtlvEffect->gauge, TRUE);
    GFL_SndBGMPlay(bgm, 0xffff);
}

void BtlvEffect_SetNoPinchBgm(BOOL noPinchBgm) {
    BtlvGauge_SetNoPinchBgm(sBtlvEffect->gauge, noPinchBgm);
}

void BtlvEffect_ReserveBgm(u32 bgm) {
    sBtlvEffect->bgmChanged = TRUE;
    BtlvGauge_SetBgm(sBtlvEffect->gauge, bgm);
    if (!BtlvGauge_IsPinch(sBtlvEffect->gauge)) {
        BtlvGauge_SetBgmReplayed(sBtlvEffect->gauge, TRUE);
    }
}

void BtlvEffect_ReloadField(s32 fieldParam) {
    BtlvField_Reload(BtlvEffect_GetWork()->field, fieldParam);
}

BOOL BtlvEffect_IsBgmChanged(void) {
    return sBtlvEffect->bgmChanged;
}

void BtlvEffect_PlayBgmNoPinch(u32 bgm) {
    if (BtlvGauge_IsPinch(sBtlvEffect->gauge) == TRUE) {
        GFL_SndBGMPop();
    }
    BtlvGauge_SetNoPinchBgm(sBtlvEffect->gauge, TRUE);
    BtlvGauge_SetPinch(sBtlvEffect->gauge, FALSE);
    GFL_SndBGMPlay(bgm, 0xffff);
}

void BtlvEffect_SetState(u32 state) {
    sBtlvEffect->state = state;
}

u32 BtlvEffect_GetState(void) {
    return sBtlvEffect->state;
}

void BtlvEffect_SetFlag26(u32 arg0) {
    sBtlvEffect->unk1F4_26 = arg0;
}

u32 BtlvEffect_GetFlag26(void) {
    return sBtlvEffect->unk1F4_26;
}

BOOL BtlvEffect_GetUnk2E4(void) {
    BOOL result = FALSE;

    if (sBtlvEffect->setup.mainModule != NULL) {
        result = func_ov167_0219db08(sBtlvEffect->setup.mainModule);
    }
    return result;
}

BOOL BtlvEffect_GetUnk304(void) {
    BOOL result = FALSE;

    if (sBtlvEffect->setup.mainModule != NULL) {
        result = func_ov167_0219db28(sBtlvEffect->setup.mainModule);
    }
    return result;
}

void BtlvEffect_SetPokemonCheck(u32 arg0) {
    BtlvMcssPos pos;

    for (pos = BTLV_MCSS_POS_FIRST; pos < BTLV_MCSS_POS_TRAINER; pos++) {
        if (BtlvMcss_Exists(sBtlvEffect->mcss, pos)) {
            BtlvMcss_SetUnkB270(sBtlvEffect->mcss, pos, arg0);
        }
    }
}

void BtlvEffect_AddTask(TCB *tcb, void (*endFunc)(TCB *tcb), u32 group) {
    int slot = BtlvEffect_GetFreeTaskSlot();

    sBtlvEffect->tasks[slot] = tcb;
    sBtlvEffect->taskCallbacks[slot] = endFunc;
    sBtlvEffect->taskGroups[slot] = group;
}

static int BtlvEffect_FindTask(TCB *tcb) {
    int i;

    for (i = 0; i < BTLV_EFFECT_TASK_MAX; i++) {
        if (sBtlvEffect->tasks[i] == tcb) {
            break;
        }
    }
    return i;
}

void BtlvEffect_EndTask(TCB *tcb) {
    int slot = BtlvEffect_FindTask(tcb);
    void *data;

    if (tcb != NULL) {
        data = GFL_TCBGetData(tcb);
        if (sBtlvEffect->taskCallbacks[slot] != NULL) {
            sBtlvEffect->taskCallbacks[slot](tcb);
        }
        if (data != NULL) {
            GFL_HeapFree(data);
        }
        GFL_TCBRemove(tcb);
        sBtlvEffect->tasks[slot] = NULL;
        sBtlvEffect->taskCallbacks[slot] = NULL;
    }
}

void BtlvEffect_EndTaskGroup(u32 group) {
    int i;

    for (i = 0; i < BTLV_EFFECT_TASK_MAX; i++) {
        if (sBtlvEffect->tasks[i] != NULL && group == sBtlvEffect->taskGroups[i]) {
            BtlvEffect_EndTask(sBtlvEffect->tasks[i]);
        }
    }
}

void BtlvEffect_SetIdleEffectMode(u32 mode) {
    sBtlvEffect->idleEffectMode = mode;
    if (sBtlvEffect->idleEffectMode != 2) {
        sBtlvEffect->idleEffectSeq = 0;
        sBtlvEffect->idleEffectTimer = 0;
    }
    if (mode == 3) {
        BtlvMcss_SaveVanish(sBtlvEffect->mcss);
    }
}

void BtlvEffect_StopIdleEffect(void) {
    BtlvEffect_Stop();
    if (sBtlvEffect->idleEffectMode == 3 && BtlSetup_IsBattleType(sBtlvEffect->setup.mainModule, 0x400)) {
        BtlvEffect_Start(0x251);
    } else {
        BtlvEffect_Start(0x236);
    }
    sBtlvEffect->idleEffectMode = 0;
}

void BtlvEffect_RestartIdleEffect(u32 mode) {
    BtlvEffect_Stop();
    sBtlvEffect->idleEffectMode = mode;
    sBtlvEffect->idleEffectSeq = 0;
    sBtlvEffect->idleEffectTimer = 0;
}

void BtlvEffect_SetAbility(u32 viewPos, u32 ability) {
    sBtlvEffect->abilities[viewPos] = ability;
}

u32 BtlvEffect_GetAbility(u8 viewPos) {
    return sBtlvEffect->abilities[viewPos];
}

void BtlvEffect_ReleaseVoices(void) {
    BtlvEffvm_ReleaseVoices(sBtlvEffect->effvm);
}

void BtlvEffect_ClearVoices(void) {
    BtlvEffvm_ClearLastEffect(sBtlvEffect->effvm);
}

static int BtlvEffect_GetFreeTaskSlot(void) {
    int i;

    for (i = 0; i < BTLV_EFFECT_TASK_MAX; i++) {
        if (sBtlvEffect->tasks[i] == NULL) {
            break;
        }
    }
    if (i == BTLV_EFFECT_TASK_MAX) {
        BtlvEffect_EndTask(sBtlvEffect->tasks[0]);
        i = 0;
    }
    return i;
}

static void BtlvEffect_EndAllTasks(void) {
    int i;

    for (i = 0; i < BTLV_EFFECT_TASK_MAX; i++) {
        if (sBtlvEffect->tasks[i] != NULL) {
            BtlvEffect_EndTask(sBtlvEffect->tasks[i]);
        }
    }
}

static void BtlvEffect_VBlankTask(TCB *tcb, void *data) {
    func_0204b7c8();
    // bwbattle: only the OBJ palettes are transferred, to the OBJ extension's (PaletteFade_Transfer); the BG palettes
    // are Platinum's, whose textbox and menus share the screen
    PaletteFade_Transfer(sBtlvEffect->palFade);
}

static void BtlvEffect_BlinkTask(TCB *tcb, void *data) {
    BtlvEffectDamage *work = data;

    if (work->wait != 0) {
        work->wait--;
        return;
    }
    switch (work->seq) {
    case 0:
        if (!work->hidden) {
            BtlvMcss_StartPaletteFade(sBtlvEffect->mcss, work->pos, 16, 16, 0, work->color);
        }
        work->wait = 3;
        work->seq = 1;
        break;
    case 1:
        if (!work->hidden) {
            BtlvMcss_SetVanish(sBtlvEffect->mcss, work->pos, TRUE);
        }
        work->wait = 3;
        work->seq = 2;
        break;
    case 2:
        work->seq = 0;
        if (!work->hidden) {
            BtlvMcss_SetVanish(sBtlvEffect->mcss, work->pos, FALSE);
            BtlvMcss_StartPaletteFade(sBtlvEffect->mcss, work->pos, 0, 0, 0, 0x7fff);
        }
        work->wait = 3;
        if (--work->count == 0) {
            BtlvEffect_EndTask(tcb);
        }
        break;
    }
}

static void BtlvEffect_DamageTaskEnd(TCB *tcb) {
    u32 *viewPos = GFL_TCBGetData(tcb);

    sBtlvEffect->damageTasks &= BtlvEffect_PosBit(*viewPos) ^ 0xffffffff;
}

static void BtlvEffect_SubstituteTask(TCB *tcb, void *data) {
    BtlvEffectChange *work = data;

    switch (work->seq) {
    case 0:
        BtlvEffvm_Start(sBtlvEffect->effvm, work->viewPos, 0xff, 0x253, NULL);
        sBtlvEffect->executing = TRUE;
        work->seq++;
        break;
    case 1:
        if (sBtlvEffect->executing) {
            break;
        }
        if (BtlvMcss_GetFlags(sBtlvEffect->mcss, work->viewPos) & 1) {
            BtlvEffvm_Start(sBtlvEffect->effvm, work->viewPos, 0xff, 0x264, NULL);
            sBtlvEffect->executing = TRUE;
        }
        work->seq++;
        break;
    case 2:
        if (sBtlvEffect->executing) {
            break;
        }
        BtlvMcss_MoveAlpha(sBtlvEffect->mcss, work->viewPos, 0, 16, 0, 0, 0);
        BtlvMcss_MoveLevel(sBtlvEffect->mcss, work->viewPos, 1, 8, 8, 1, 0);
        BtlvEffvm_PlaySEAt(sBtlvEffect->effvm, 0x5aa, 1, 14, -500, 127, 0, 0, 0);
        BtlvEffvm_SlideSE(sBtlvEffect->effvm, 1, 0, 1, 127, 0, 30, 10, 0, 0);
        work->seq++;
        break;
    case 3:
        if (!BtlvMcss_IsBusy(sBtlvEffect->mcss, work->viewPos)) {
            BtlvMcss_ReloadSprite(sBtlvEffect->mcss, work->viewPos, &work->info);
            work->seq++;
        }
        break;
    case 4:
        BtlvMcss_MoveLevel(sBtlvEffect->mcss, work->viewPos, 1, 0, 8, 1, 0);
        work->seq++;
        break;
    case 5:
        if (BtlvMcss_IsBusy(sBtlvEffect->mcss, work->viewPos)) {
            break;
        }
        BtlvEffvm_PlaySEAt(sBtlvEffect->effvm, 0x560, 1, 14, 0, 0, 0, 0, 0);
        BtlvMcss_MoveAlpha(sBtlvEffect->mcss, work->viewPos, 0, 31, 0, 0, 0);
        if (BtlvMcss_GetFlags(sBtlvEffect->mcss, work->viewPos) & 1) {
            BtlvEffvm_Start(sBtlvEffect->effvm, work->viewPos, 0xff, 0x265, NULL);
            sBtlvEffect->executing = TRUE;
        }
        work->seq++;
        break;
    case 6:
        if (sBtlvEffect->executing) {
            break;
        }
        BtlvEffvm_Start(sBtlvEffect->effvm, work->viewPos, 0xff, 0x254, NULL);
        sBtlvEffect->executing = TRUE;
        work->seq++;
        break;
    case 7:
        if (!sBtlvEffect->executing) {
            BtlvEffect_EndTask(tcb);
        }
        break;
    }
}

static void BtlvEffect_Unk8TaskEnd(TCB *tcb) {
    u32 *viewPos = GFL_TCBGetData(tcb);

    sBtlvEffect->unkTasks8 &= BtlvEffect_PosBit(*viewPos) ^ 0xffffffff;
}

static void BtlvEffect_RotateTask(TCB *tcb, void *data) {
    BtlvEffectRotateWork *work = data;

    switch (work->seq) {
    case 0:
        if ((sBtlvEffect->damageTasks | sBtlvEffect->executing | sBtlvEffect->unkTasks8) != 0) {
            break;
        }
        work->seq++;
        // fallthrough
    case 1:
        BtlvMcss_StartRotation(sBtlvEffect->mcss, work->side, work->arg4, work->argC);
        work->seq++;
        break;
    case 2:
        if (!BtlvStage_IsAnimating(sBtlvEffect->stage) && !BtlvMcss_IsAnyBusy(sBtlvEffect->mcss)) {
            BtlvEffect_EndTask(tcb);
        }
        break;
    }
}

static void BtlvEffect_RotateTaskEnd(TCB *tcb) {
    BtlvEffectRotateWork *work = GFL_TCBGetData(tcb);

    sBtlvEffect->rotateTasks &= BtlvEffect_PosBit(work->side) ^ 0xffffffff;
}

static void BtlvEffect_DelayedMoveTask(TCB *tcb, void *data) {
    BtlvEffectQueued *work = data;

    if (!BtlvEffect_IsBusy()) {
        BtlvEffvm_Start(sBtlvEffect->effvm, work->atkPos, work->defPos, work->effNo, work->param);
        sBtlvEffect->executing = TRUE;
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvEffect_DelayedMoveTaskEnd(TCB *tcb) {
    BtlvEffectQueued *work = GFL_TCBGetData(tcb);

    if (work->param != NULL) {
        GFL_HeapFree(work->param);
    }
}

static void BtlvEffect_UpdateIdleEffect(void) {
    u32 keys = GCTX_HIDGetPressedKeys();
    BOOL touched = func_0203da48();
    s32 pick;
    u32 effect;

    if (sBtlvEffect->idleEffectMode == 0) {
        return;
    }
    if (sBtlvEffect->idleEffectMode != 2 && sBtlvEffect->idleEffectMode != 3 && (keys != 0 || touched)) {
        BtlvEffect_Stop();
        BtlvEffect_Start(0x252);
        sBtlvEffect->idleEffectSeq = 0;
        sBtlvEffect->idleEffectTimer = 0;
    }
    switch (sBtlvEffect->idleEffectSeq) {
    case 0:
        if (sBtlvEffect->idleEffectTimer < sBtlvEffect->idleEffectWait) {
            sBtlvEffect->idleEffectTimer++;
            return;
        }
        sBtlvEffect->idleEffectSeq = 1;
        // fallthrough
    case 1:
        if (BtlvEffect_IsBusy()) {
            break;
        }
        if (sBtlvEffect->idleEffectMode == 3 && BtlSetup_IsBattleType(sBtlvEffect->setup.mainModule, 0x400)) {
            do {
                pick = GFL_RandomMTRange(2);
            } while (sBtlvEffect->idleEffectLast == pick);
            sBtlvEffect->idleEffectLast = pick;
            effect = data_ov168_021f3f50[sBtlvEffect->idleEffectLast];
        } else {
            do {
                pick = GFL_RandomMTRange(6);
            } while (sBtlvEffect->idleEffectLast == pick);
            sBtlvEffect->idleEffectLast = pick;
            effect = data_ov168_021f3f58[sBtlvEffect->idleEffectLast];
        }
        BtlvEffect_Start(effect);
        sBtlvEffect->executing = TRUE;
        break;
    }
}

void BtlvEffTool_CalcStep(fx32 start, fx32 end, fx32 *step, fx32 frames) {
    *step = 0;
    if (end - start != 0) {
        if (frames == 0) {
            frames = FX32_ONE;
        }
        *step = FX_Div(end - start, frames);
        if (*step == 0) {
            if (end > start) {
                *step = 1;
            } else {
                *step = -1;
            }
        }
    }
}

void BtlvEffTool_CalcStepVec(const VecFx32 *start, const VecFx32 *end, VecFx32 *step, fx32 frames) {
    step->x = 0;
    step->y = 0;
    step->z = 0;
    if (frames == 0) {
        frames = FX32_ONE;
    }
    if (end->x - start->x != 0) {
        step->x = FX_Div(end->x - start->x, frames);
        if (step->x == 0) {
            if (end->x > start->x) {
                step->x = 1;
            } else {
                step->x = -1;
            }
        }
    }
    if (end->y - start->y != 0) {
        step->y = FX_Div(end->y - start->y, frames);
        if (step->y == 0) {
            if (end->y > start->y) {
                step->y = 1;
            } else {
                step->y = -1;
            }
        }
    }
    if (end->z - start->z != 0) {
        step->z = FX_Div(end->z - start->z, frames);
        if (step->z == 0) {
            if (end->z > start->z) {
                step->z = 1;
            } else {
                step->z = -1;
            }
        }
    }
}

void BtlvEffTool_Step(fx32 *value, fx32 *step, const fx32 *end, BOOL *done) {
    *value += *step;
    if (*step < 0) {
        if (*value <= *end) {
            *value = *end;
        } else if (done != NULL) {
            *done = FALSE;
        }
    } else {
        if (*value >= *end) {
            *value = *end;
        } else if (done != NULL) {
            *done = FALSE;
        }
    }
}

BOOL BtlvEffTool_Move(BtlvEffToolMove *move, VecFx32 *value) {
    BOOL done = TRUE;

    switch (move->type) {
    case 0:
        value->x = move->end.x;
        value->y = move->end.y;
        value->z = move->end.z;
        break;
    case 1:
    case 4:
        if (move->wait == 0) {
            move->wait = move->waitReset;
            BtlvEffTool_Step(&value->x, &move->step.x, &move->end.x, &done);
            BtlvEffTool_Step(&value->y, &move->step.y, &move->end.y, &done);
            BtlvEffTool_Step(&value->z, &move->step.z, &move->end.z, &done);
        } else {
            move->wait--;
            done = FALSE;
        }
        break;
    case 2:
    case 3:
        if (move->wait == 0) {
            move->wait = move->waitReset;
            value->x += move->step.x;
            value->y += move->step.y;
            value->z += move->step.z;
            if (--move->stepTime == 0) {
                move->count--;
                move->stepTime = move->stepTimeReset;
                if (move->type == 2 || (move->type == 3 && (move->count & 1))) {
                    move->step.x *= -1;
                    move->step.y *= -1;
                    move->step.z *= -1;
                }
            }
        } else {
            move->wait--;
        }
        if (move->count != 0) {
            done = FALSE;
        } else {
            value->x = move->start.x;
            value->y = move->start.y;
            value->z = move->start.z;
        }
        break;
    }
    return done;
}

u32 BtlvEffect_PosBit(u32 bit) {
    return 1 << bit;
}

void BtlvEffect_UpdateTexPaletteFade(BtlvTexPaletteFade *fade) {
    int i;
    NNSG3dResTex *tex;
    u32 size;
    u32 addr;

    if (fade->active) {
        if (fade->wait == 0) {
            fade->wait = fade->waitFrames;
            for (i = 0; i < fade->count; i++) {
                tex = NNS_G3DResGetTexBlock(GFL_G3DResGetResData(fade->resources[i]));
                size = tex->plttInfo.sizePltt << 3;
                addr = NNS_GfdGetPlttKeyAddr(tex->plttInfo.vramKey);
                BlendColors((u16 *)((u8 *)tex + tex->plttInfo.ofsPlttData), fade->palettes[i], size / sizeof(u16),
                            fade->evy, fade->color);
                NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_3D_TEX_PLTT, addr, fade->palettes[i], size);
            }
            if (fade->evy == fade->targetEvy) {
                fade->active = FALSE;
            } else if (fade->evy > fade->targetEvy) {
                fade->evy--;
            } else {
                fade->evy++;
            }
        } else {
            fade->wait--;
        }
    }
}
