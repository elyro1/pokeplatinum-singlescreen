#include "bwbattle.h"

#include "types.h"
#include "constants/arc.h"
#include "gfl/arc.h"
#include "gfl/arc_util.h"
#include "battle/btlv_b_gauge.h"
#include "battle/btlv_effect.h"
#include "battle/btlv_gauge.h"
#include "battle/btlv_mcss.h"
#include "gfl/clact.h"
#include "gfl/g3d.h"
#include "gfl/gx_layers.h"
#include "gfl/heap.h"
#include "shim/bw_shim.h"
#include "system/palanm.h"
#include "host/bw_ability_popup.h"

// Runs BW2's battle view inside Platinum's battle, in place of btlv_core.c, which set it up for BW2's battle client.
// Platinum's battle owns the screens: it has set up VRAM, the 3D engine and its sprites, so the view only adds its
// own state on top (GFL_G3DSysCreateShared) and keeps to the OAM entries left to it.

// The OAM entries the view's 2D actors (gauges, effect sprites) use, after the ones Platinum's battle keeps. The
// first four of the OBJ extension's are left out: something of Platinum's battle hides them every frame (attributes
// 0x00c0), which took the first actor drawn off the screen
#define BW_VIEW_OAM_START (BW_OBJ_EXT_FIRST_OAM + 4)
#define BW_VIEW_OAM_COUNT (128 - BW_VIEW_OAM_START)

BtlMainModule *BwShim_GetMainModule(void);

// BW2's battle setup (btlv_core.c), with the main engine's OAM moved after Platinum's
static const ClActSysSetup sClActSetup = {
    0, 0, 0, 512, BW_VIEW_OAM_START, BW_VIEW_OAM_COUNT, 4, 124, 0, 56, 48, 48, 48, 16, 16,
};

static const BGSysVRAMConfig sVRAMConfig = {
    GX_VRAM_BG_128_D,  GX_VRAM_BGEXTPLTT_NONE,  GX_VRAM_SUB_BG_128_C,       GX_VRAM_SUB_BGEXTPLTT_NONE,
    GX_VRAM_OBJ_64_E,  GX_VRAM_OBJEXTPLTT_NONE, GX_VRAM_SUB_OBJ_16_I,       GX_VRAM_SUB_OBJEXTPLTT_NONE,
    GX_VRAM_TEX_01_AB, GX_VRAM_TEXPLTT_01_FG,   GX_OBJVRAMMODE_CHAR_1D_64K, GX_OBJVRAMMODE_CHAR_1D_32K,
};

// BW2's moves: Platinum's are the same numbers below Platinum's last
#define BW_MOVE_COUNT 560

// BW2's effects after its moves: the files of archive 66 from BTLV_EFFECT_FIRST (btlv_effvm.c)
#define BTLV_EFFECT_FIRST_ID 0x231
#define BW_EFFECT_COUNT 115

static BOOL sViewActive;

// The images that stand in for trainers' sprites, by trainer position, kept for BW2's effects that add the trainer
// later (the partner's send-out, 0x233)
#define BW_TRAINER_IMAGE_FIRST BTLV_MCSS_POS_TRAINER
#define BW_TRAINER_IMAGES (BTLV_MCSS_POS_MAX - BTLV_MCSS_POS_TRAINER)
#define BW_TRAINER_IMAGE_MAX_SIZE (96 * 96 / 2)

typedef struct {
    bool set;
    u16 width;
    u16 height;
    int anchorX;
    int anchorY;
    u8 image[BW_TRAINER_IMAGE_MAX_SIZE];
    u16 colors[16];
    fx32 scale;
} BwTrainerImage;

static BwTrainerImage sTrainerImages[BW_TRAINER_IMAGES];
static BwTrainerBack sTrainerBacks[BW_TRAINER_IMAGES];

// BW2's idle camera is on (BwBattleView_SetIdleCamera)
static bool sIdleCamera;
// Which sides have a ball gauge (BtlvEffect_AddBallGauge), which BW2 deletes without clearing
static BOOL sBallGauges[2];

bool BwBattleView_Create(const BwBattleSetup *setup) {
    BtlvEffectSetup *effectSetup;

    if (sViewActive || !BwBattle_Init()) {
        return false;
    }
    BwBattle_SetSetup(setup);
    GFL_G3DSysCreateShared(HEAPID_BATTLE);
    ClActSys_Create(&sClActSetup, &sVRAMConfig, HEAPID_BATTLE);
    // The view's 2D actors (gauges, effect sprites) draw through libntr's OBJ extension, with their own characters,
    // palettes and OAM entries beside Platinum's (BW_OBJ_EXT_* in bwbattle_compat.h)
    SIM_SetObjExtension(BW_OBJ_EXT_FIRST_OAM, BW_OBJ_EXT_CHAR_OFFSET);
    effectSetup = BtlvEffect_CreateSetup(BwShim_GetMainModule(), NULL, HEAPID_BATTLE);
    BtlvEffect_Init(effectSetup, NULL, HEAPID_BATTLE);
    GFL_HeapFree(effectSetup);
    sViewActive = TRUE;
    BwAbilityPopup_Init();
    sIdleCamera = false;
    MI_CpuClear8(sTrainerImages, sizeof(sTrainerImages));
    MI_CpuClear8(sTrainerBacks, sizeof(sTrainerBacks));
    BwShim_Log("view created: style %u type %u bg %u terrain %u", setup->battleStyle, setup->battleType,
               setup->bgType, setup->terrain);
    return true;
}

void BwBattleView_Delete(void) {
    if (!sViewActive) {
        return;
    }
    BwBattleView_HideBallGauge(0);
    BwBattleView_HideBallGauge(1);
    BwAbilityPopup_Delete();
    BtlvEffect_Exit();
    func_0204b758();
    SIM_SetObjExtension(128, 0);
    GFL_G3DSysFreeShared();
    sViewActive = FALSE;
}

bool BwBattleView_IsActive(void) {
    return sViewActive;
}

// Updates the view and draws its 3D scene; Platinum's draw task calls it in place of drawing its battle sprites.
// BW2's main loop swapped the 3D buffers that BtlvEffect_Main requests (GFL_G3DSysCheckSwapBuffers); here Platinum's
// draw task requests the swap, so BW2's request is dropped
void BwBattleView_Main(unsigned int frames, unsigned int idleFrames) {
    if (sViewActive) {
        // Platinum's frame has set the 3D engine's camera and projection since BW2's last, and BW2's effects project
        // positions with them (BtlvEffvm_ProjectToScreen) on the frames it doesn't draw too
        GFL_G3DSysReapplyState();
        // BW2's frames before the one drawn, each with the V-blank tasks a frame of BW2's runs
        for (unsigned int i = 1; i < frames; i++) {
            BOOL tickIdle = i < idleFrames;

            // The idle camera uses the effect VM and its tasks, so its entire script must keep the ambient clock.
            if (tickIdle || !sIdleCamera) {
                BtlvEffect_Step(tickIdle);
            }
            BwBattle_VBlank();
        }
        BtlvEffect_Main();
        for (unsigned int i = 0; i < frames; i++) {
            BwAbilityPopup_Step();
        }
        BwAbilityPopup_Draw();
        GFL_G3DSysClearSwapRequest();
    }
}

bool BwBattleView_ShowAbilityPopup(unsigned int viewPos, const unsigned short *nickname, const unsigned short *ability) {
    return sViewActive && BwAbilityPopup_Show(viewPos & 1, nickname, ability);
}

bool BwBattleView_IsAbilityPopupBusy(void) {
    return sViewActive && BwAbilityPopup_IsBusy();
}

void BwBattleView_SetAbilityPopupHeld(unsigned int viewPos, bool held) {
    if (sViewActive) {
        BwAbilityPopup_SetHeld(viewPos & 1, held);
    }
}

void BwBattleView_AddMon(unsigned int viewPos, const BwBattleMon *mon) {
    if (sViewActive) {
        BtlvEffect_AddPokemon((PartyPkm *)mon, viewPos);
    }
}

void BwBattleView_RemoveMon(unsigned int viewPos) {
    // An effect may have removed it already: BW2's faint does (BtlvEffvm_CmdGaugeHide)
    if (sViewActive && BtlvMcss_Exists(BtlvEffect_GetMcss(), viewPos)) {
        BtlvEffect_DelPokemon(viewPos);
    }
}

void BwBattleView_SetMonVisible(unsigned int viewPos, bool visible) {
    if (sViewActive) {
        BtlvMcss_SetVanish(BtlvEffect_GetMcss(), viewPos, visible ? 0 : 1);
    }
}

// Archive 11's message window frame and palette, as btlv_scu.c loads them
#define BW_MSG_WINDOW_ARC 11
#define BW_MSG_WINDOW_CHAR_FILE 0x164
#define BW_MSG_WINDOW_PLTT_FILE 0x165

bool BwBattleView_GetMessageWindow(unsigned char tiles[BW_MSG_WINDOW_TILES * 32], unsigned short colors[16]) {
    NNSG2dCharacterData *character;
    NNSG2dPaletteData *palette;
    void *charFile;
    void *plttFile;
    bool loaded = false;

    if (!sViewActive) {
        return false;
    }
    charFile = GFL_G2DIOReadBGNCGR(BW_MSG_WINDOW_ARC, BW_MSG_WINDOW_CHAR_FILE, FALSE, &character, HEAPID_BATTLE);
    plttFile = GFL_G2DIOReadNCLR(BW_MSG_WINDOW_ARC, BW_MSG_WINDOW_PLTT_FILE, &palette, HEAPID_BATTLE);
    if (charFile != NULL && plttFile != NULL && character->szByte >= BW_MSG_WINDOW_TILES * 32) {
        MI_CpuCopy8(character->pRawData, tiles, BW_MSG_WINDOW_TILES * 32);
        MI_CpuCopy8(palette->pRawData, colors, 16 * sizeof(u16));
        loaded = true;
    }
    if (charFile != NULL) {
        GFL_HeapFree(charFile);
    }
    if (plttFile != NULL) {
        GFL_HeapFree(plttFile);
    }
    return loaded;
}

void BwBattleView_SetObjExtension(bool on) {
    if (sViewActive) {
        SIM_SetObjExtension(on ? BW_OBJ_EXT_FIRST_OAM : 128, on ? BW_OBJ_EXT_CHAR_OFFSET : 0);
        // Off, the view's OAM entries go too; its next frame writes them again
        if (!on) {
            GXOamAttr *oam = (GXOamAttr *)HW_OAM;

            for (int i = BW_VIEW_OAM_START; i < 128; i++) {
                // Not drawn: no affine, and its disable bit
                oam[i].attr0 = (oam[i].attr0 & ~0x300) | 0x200;
            }
        }
    }
}

void BwBattleView_SetIdleCamera(bool on) {
    if (!sViewActive || (on && sIdleCamera)) {
        return;
    }
    sIdleCamera = on;
    // As BW2's command screen does (btlv_input.c), and its standby screen after it
    if (on) {
        BtlvEffect_SetIdleEffectMode(2);
    } else {
        // Command selection must reset the camera even if an item already stopped the idle effects:
        // the item's script locks camera commands until the reset script (0x236) clears the lock.
        BtlvEffect_StopIdleEffect();
    }
}

bool BwBattleView_IsIdleCameraActive(void) {
    return sViewActive && sIdleCamera;
}

void BwBattleView_SetObjFade(unsigned int level, unsigned short color) {
    PaletteFade *fade;
    u16 *colors;

    if (!sViewActive) {
        return;
    }
    fade = BtlvEffect_GetPaletteFade();
    PaletteFade_BlendBuffer(fade, PALFADE_BUFFER_MAIN_OBJ, 0, HW_OBJ_PLTT_SIZE / sizeof(u16), level > 16 ? 16 : level,
                            color);
    colors = PaletteFade_GetFadedBuffer(fade, PALFADE_BUFFER_MAIN_OBJ);
    if (colors != NULL) {
        DC_FlushRange(colors, HW_OBJ_PLTT_SIZE);
        GX_LoadOBJPltt(colors, BW_OBJ_EXT_PLTT_OFFSET, HW_OBJ_PLTT_SIZE);
    }
}


void BwView_OnTrainerAdded(u32 viewPos) {
    BwTrainerImage *entry;

    if (viewPos < BW_TRAINER_IMAGE_FIRST || viewPos >= BW_TRAINER_IMAGE_FIRST + BW_TRAINER_IMAGES) {
        return;
    }
    entry = &sTrainerImages[viewPos - BW_TRAINER_IMAGE_FIRST];
    if (sTrainerBacks[viewPos - BW_TRAINER_IMAGE_FIRST] != BW_TRAINER_BACK_NONE) {
        BtlvMcss_SetTrainerBackSprite(BtlvEffect_GetMcss(), viewPos, sTrainerBacks[viewPos - BW_TRAINER_IMAGE_FIRST]);
    }
    if (entry->set) {
        BtlvMcss_SetImage(BtlvEffect_GetMcss(), viewPos, entry->image, entry->width, entry->height,
            entry->anchorX, entry->anchorY, entry->colors, entry->scale);
    }
}

void BwBattleView_SetTrainerBackSprite(unsigned int viewPos, BwTrainerBack trainer) {
    if (sViewActive && viewPos >= BW_TRAINER_IMAGE_FIRST && viewPos < BW_TRAINER_IMAGE_FIRST + BW_TRAINER_IMAGES
        && !(viewPos & 1)) {
        sTrainerBacks[viewPos - BW_TRAINER_IMAGE_FIRST] = trainer;
    }
}

void BwBattleView_SetTrainerImage(unsigned int viewPos, const unsigned char *image, unsigned int width,
                                  unsigned int height, const unsigned short *colors, unsigned int scalePercent) {
    BwTrainerImage *entry;

    if (!sViewActive || viewPos < BW_TRAINER_IMAGE_FIRST || viewPos >= BW_TRAINER_IMAGE_FIRST + BW_TRAINER_IMAGES
        || width * height / 2 > BW_TRAINER_IMAGE_MAX_SIZE) {
        return;
    }
    entry = &sTrainerImages[viewPos - BW_TRAINER_IMAGE_FIRST];
    entry->set = true;
    entry->width = width;
    entry->height = height;
    entry->anchorX = width / 2;
    // Front trainers stand at their feet; back sprites are centered upper-body portraits.
    entry->anchorY = (viewPos & 1) ? height : height / 2;
    MI_CpuCopy8(image, entry->image, width * height / 2);
    MI_CpuCopy16(colors, entry->colors, sizeof(entry->colors));
    entry->scale = (fx32)(FX32_ONE * scalePercent / 100);
    BtlvMcss_SetImage(BtlvEffect_GetMcss(), viewPos, image, width, height, entry->anchorX, entry->anchorY,
        colors, entry->scale);
}

void BwBattleView_UpdateTrainerImage(unsigned int viewPos, const unsigned char *image, int anchorX, int anchorY) {
    BwTrainerImage *entry;

    if (!sViewActive || viewPos < BW_TRAINER_IMAGE_FIRST || viewPos >= BW_TRAINER_IMAGE_FIRST + BW_TRAINER_IMAGES) {
        return;
    }
    entry = &sTrainerImages[viewPos - BW_TRAINER_IMAGE_FIRST];
    if (!entry->set) {
        return;
    }
    entry->anchorX = anchorX;
    entry->anchorY = anchorY;
    MI_CpuCopy8(image, entry->image, entry->width * entry->height / 2);
    BtlvMcss_SetImage(BtlvEffect_GetMcss(), viewPos, entry->image, entry->width, entry->height, anchorX, anchorY,
        entry->colors, entry->scale);
}

bool BwBattleView_HasSubstitute(unsigned int viewPos, bool *shown) {
    u32 flags = 0;

    if (sViewActive && BtlvMcss_Exists(BtlvEffect_GetMcss(), viewPos)) {
        flags = BtlvMcss_GetFlags(BtlvEffect_GetMcss(), viewPos);
    }
    *shown = (flags & BTLV_MCSS_FLAG_SUBSTITUTE) != 0;
    return (flags & BTLV_MCSS_FLAG_UNK0) != 0;
}

void BwBattleView_SetSubstitute(unsigned int viewPos, bool substitute) {
    if (sViewActive && BtlvMcss_Exists(BtlvEffect_GetMcss(), viewPos)) {
        BtlvMcss_SetSubstitute(BtlvEffect_GetMcss(), viewPos, substitute, TRUE);
    }
}

void BwBattleView_SetTrainer(unsigned int viewPos, unsigned int trainerClass, bool animate) {
    if (sViewActive && viewPos >= BTLV_MCSS_POS_TRAINER && viewPos < BTLV_MCSS_POS_MAX) {
        BtlvEffect_DelTrainer(viewPos);
        BtlvEffect_SetTrainer(trainerClass, viewPos, 0, 0, 0);
        // As BW2's battle start holds the trainer still until the throw (btlv_scu.c)
        if (!animate) {
            BtlvEffect_SetPokeAnimeSpeed(viewPos, 0);
        }
    }
}

void BwBattleView_RemoveTrainer(unsigned int viewPos) {
    if (sViewActive && viewPos >= BTLV_MCSS_POS_TRAINER && viewPos < BTLV_MCSS_POS_MAX) {
        BtlvEffect_DelTrainer(viewPos);
    }
}

void BwBattleView_SetViewWidth(unsigned int width) {
    GFL_G3DSysSetViewWidth(width);
}

// The gauges (btlv_gauge.c), which follow Platinum's healthboxes
void BwBattleView_ShowGauge(unsigned int viewPos, const BwBattleMon *mon) {
    if (sViewActive) {
        BtlvEffect_DelGauge(viewPos);
        BtlvEffect_AddGauge(BwShim_GetMainModule(), (BattleMon *)mon, viewPos);
    }
}

void BwBattleView_HideGauge(unsigned int viewPos) {
    if (sViewActive) {
        BtlvEffect_DelGauge(viewPos);
    }
}

void BwBattleView_SetGaugeVisible(unsigned int viewPos, bool visible) {
    if (sViewActive) {
        BtlvEffect_SetGaugeDrawEnable(visible, viewPos);
    }
}

void BwBattleView_SetGaugeHp(unsigned int viewPos, int hp, bool moving) {
    if (sViewActive) {
        BtlvGauge_SyncHp(BtlvEffect_GetGauge(), viewPos, hp, moving);
    }
}

void BwBattleView_SetGaugeExp(unsigned int viewPos, int exp) {
    if (sViewActive) {
        BtlvGauge_SetExpAtOnce(BtlvEffect_GetGauge(), viewPos, exp);
    }
}

void BwBattleView_SetGaugeStatus(unsigned int viewPos, unsigned char status) {
    if (sViewActive) {
        BtlvGauge_SetStatus(BtlvEffect_GetGauge(), status, viewPos);
    }
}

// The party's balls at a trainer battle's start and when a trainer sends out the next Pokémon (btlv_b_gauge.c)
void BwBattleView_ShowBallGauge(unsigned int side, const unsigned char balls[6]) {
    BtlvBGaugeParam param;

    if (!sViewActive || side > 1) {
        return;
    }
    BwBattleView_HideBallGauge(side);
    MI_CpuClear8(&param, sizeof(param));
    param.side = side;
    for (int i = 0; i < 6; i++) {
        param.balls[i] = balls[i];
    }
    BtlvEffect_AddBallGauge(&param);
    sBallGauges[side] = TRUE;
}

void BwBattleView_HideBallGauge(unsigned int side) {
    if (sViewActive && side <= 1 && sBallGauges[side]) {
        BtlvEffect_DelBallGauge(side);
        sBallGauges[side] = FALSE;
    }
}

// A move's target class in BW2's move data (ARCID_WAZAINFO, MOVE_PARAM_TARGET), which the effect scripts branch on
#define BW_MOVE_DATA_SIZE 0x24
#define BW_MOVE_DATA_TARGET 20

static u8 BwView_MoveTarget(u16 move) {
    u8 data[BW_MOVE_DATA_SIZE];

    GFL_ArcSysRead(data, ARCID_WAZAINFO, move);
    return data[BW_MOVE_DATA_TARGET];
}

bool BwBattleView_StartMove(unsigned int attackerPos, unsigned int defenderPos, unsigned short move) {
    BtlvMoveEffectParam param;

    if (!sViewActive || move == 0 || move >= BW_MOVE_COUNT) {
        return false;
    }
    param.move = move;
    param.attackerPos = attackerPos;
    param.targetPos = defenderPos;
    param.unk0C = 0;
    param.unk0D = 0;
    param.unk10 = BwView_MoveTarget(move);
    BtlvEffect_StartMove(&param);
    return true;
}

// BW2's ball throws at a wild Pokémon (btl_client.c): the throw, the shakes and the catch or the escape, and a ball
// a trainer blocks
bool BwBattleView_StartCatch(unsigned int viewPos, unsigned short ball, unsigned int shakes, bool caught) {
    if (!sViewActive) {
        return false;
    }
    BtlvEffect_StartEffect23A(viewPos, ball, shakes, caught, FALSE);
    return true;
}

// A caught Pokémon's effect waits after the catch until the message and the jingle are over (btl_client.c)
void BwBattleView_ResumeEffect(void) {
    if (sViewActive) {
        BtlvEffect_Resume();
    }
}

bool BwBattleView_StartBallBlocked(unsigned int viewPos, unsigned short ball) {
    if (!sViewActive) {
        return false;
    }
    BtlvEffect_StartEffect23D(viewPos, ball);
    return true;
}

bool BwBattleView_IsEffectBusy(void) {
    return sViewActive && BtlvEffect_IsBusy();
}

// Execute the setup and final state of an opening effect without showing its camera travel.
// Bound the loop in case an effect needs input or an external event; the normal queue then waits for it.
void BwBattleView_FinishOpeningEffect(void) {
    for (unsigned int i = 0; i < 1200 && BwBattleView_IsEffectBusy(); i++) {
        BtlvEffect_Step(FALSE);
        BwBattle_VBlank();
    }
}

bool BwBattleView_StartEffect(int viewPos, unsigned short effect) {
    if (!sViewActive || effect < BTLV_EFFECT_FIRST_ID || effect >= BTLV_EFFECT_FIRST_ID + BW_EFFECT_COUNT) {
        return false;
    }
    if (viewPos < 0) {
        BtlvEffect_Start(effect);
    } else {
        BtlvEffect_StartPos(viewPos, effect);
    }
    return true;
}

bool BwBattleView_StartFastWildSendOut(void) {
    return sViewActive && BtlvEffect_StartFastWildSendOut();
}

bool BwBattleView_StartFastSendOut(int viewPos, unsigned short effect) {
    return sViewActive && BtlvEffect_StartFastSendOut(viewPos, effect);
}
