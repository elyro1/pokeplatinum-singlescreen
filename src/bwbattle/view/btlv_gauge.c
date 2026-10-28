// The battle view's gauges. The name is the ROM's string, from GFL_HeapAllocate's asserts. The names are ours; swan has
// none for this file

#include "battle/btlv_gauge.h"
#include "types.h"
#include "battle/btl_main.h"
#include "battle/btl_pokeparam.h"
#include "battle/btlv_effect.h"
#include "constants/battle.h"
#include "constants/pokemon.h"
#include "constants/sound.h"
#include "constants/species.h"
#include "gfl/arc.h"
#include "gfl/arc_util.h"
#include "gfl/bmp.h"
#include "gfl/clact.h"
#include "gfl/heap.h"
#include "gfl/key.h"
#include "gfl/net.h"
#include "gfl/sound.h"
#include "gfl/std.h"
#include "gfl/str.h"
#include "gfl/tcb.h"
#include "nitro/fx.h"
#include "nitro/gx.h"
#include "nitro/math.h"
#include "pml/personal.h"
#include "pml/poke_party.h"
#include "save/pokedex.h"
#include "system/app_menu_common.h"
#include "system/gf_font.h"
#include "system/hp_gauge.h"
#include "system/palanm.h"
#include "system/printsys.h"
#include "system/str_tool.h"

// A gauge's animated value before its first step
#define GAUGE_ANIM_START ((s32)0x80000000)

// Tiles of the gauges' characters, as byte offsets
#define GAUGE_TILE_DIGITS 0x520
#define GAUGE_TILE_BLANK 0x660

// Archive 11's palettes of the gauges and of the rotation's back mons, one apart in each version
#ifdef BLACK2
#define GAUGE_PALETTE_FILE 0x1af
#define GAUGE_PALETTE_FILE_BACK 0x1b1
#else
#define GAUGE_PALETTE_FILE 0x1ae
#define GAUGE_PALETTE_FILE_BACK 0x1b0
#endif

// The status icon's sequence for no status
#define GAUGE_STATUS_NONE 8

// What a gauge shows of its mon, cleared when a mon is set, 0x44 bytes
typedef struct {
    u32 type;       // 0x00  the battle's style, BTL_STYLE_*
    u32 side;       // 0x04  1 for the opponent's
    s32 hp;         // 0x08
    s32 maxHp;      // 0x0c
    s32 hpAnim;     // 0x10  the HP shown, in 1/256 when maxHp is below the bar's 48 pixels
    s32 exp;        // 0x14  since the level started
    s32 expMax;     // 0x18  the level's EXP
    s32 nextExpMax; // 0x1c  the next level's, after a level up
    s32 expDelta;   // 0x20  the EXP to take away, so negated
    s32 expAnim;    // 0x24
    s32 hpDelta;    // 0x28  the HP to take away
    u8 level;       // 0x2c
    u8 sex;         // 0x2d  2 for no sex mark
    u8 unk2e;
    u8 caught; // 0x2f
    // 0x30
    u32 hpMoving : 1;     // bit 0
    u32 expMoving : 1;    // bit 1
    u32 levelUp : 1;      // bit 2
    u32 shown : 1;        // bit 3
    u32 levelUpSeq : 4;   // bits 4-7
    u32 expSeFrames : 8;  // bits 8-15: frames since the EXP sound started
    u32 slideFrames : 4;  // bits 16-19
    u32 shaking : 1;      // bit 20
    u32 blinkCount : 5;   // bits 21-25: the lost HP's blinks left
    u32 blinking : 1;     // bit 26
    u32 changed : 1;      // bit 27
    u32 expSe : 1;        // bit 28: the EXP sound plays
    u32 visible : 1;      // bit 29
    u32 statusHidden : 1; // bit 30
    u32 unk30_31 : 1;
    u32 hpSpeed;    // 0x34  0 to change at once
    s32 hpDots;     // 0x38  the HP bar's pixels
    u32 status;     // 0x3c  the status icon's sequence, in mode 2
    TCB *slideTask; // 0x40
#ifdef BWBATTLE
    BOOL hpSyncMoving;
    BOOL hpSyncDamage;
    BOOL hpSyncFlash;
#endif
} BtlvGaugeMon;

// A battler's gauge, 0x84 bytes
typedef struct {
    ClActor *nameActor;   // 0x00  the box with the name, sex, level and caught mark
    ClActor *hpNumActor;  // 0x04
    ClActor *hpBarActor;  // 0x08
    ClActor *damageActor; // 0x0c  the HP the bar lost, which blinks
    ClActor *expActor;    // 0x10  the player's side in single and double battles only
    ClActor *statusActor; // 0x14
    u32 nameChars;        // 0x18
    u32 nameCellAnims;    // 0x1c
    u32 hpNumChars;       // 0x20
    u32 hpNumCellAnims;   // 0x24
    u32 hpBarChars;       // 0x28
    u32 hpBarCellAnims;   // 0x2c
    u32 damageChars;      // 0x30
    u32 damageCellAnims;  // 0x34
    u32 expChars;         // 0x38
    u32 expCellAnims;     // 0x3c
    BtlvGaugeMon mon;     // 0x40
} BtlvGaugeEntry;

// The gauges' work, 0x46c bytes
struct BtlvGauge {
    ClActUnit *unit;           // 0x000
    ArcTool *arc;              // 0x004  archive 11
    Font *font;                // 0x008
    TCB *flashTask;            // 0x00c
    void *charFile;            // 0x010
    u8 *chars;                 // 0x014  the tiles the gauges are drawn from
    u32 palettes[6];           // 0x018  by view position, the positions from 2 on taking 0 and 1's
    u32 statusChars;           // 0x030
    u32 statusCellAnims;       // 0x034
    u32 statusPalette;         // 0x038
    u32 statusPaletteDark;     // 0x03c  for the rotation's back mons
    BtlvGaugeEntry entries[8]; // 0x040  by view position
    // 0x460
    u32 unk460_0 : 1;
    u32 bgmFading : 1;     // bit 1
    u32 pinch : 1;         // bit 2: the pinch music plays
    u32 bgmReplayed : 1;   // bit 3
    u32 showNumbers : 1;   // bit 4: the player's HP numbers instead of the bar, in triple and rotation battles
    u32 shakeAngle : 16;   // bits 5-20
    u32 noPinchBgm : 1;    // bit 21
    u32 toggleRequest : 1; // bit 22
    u32 loaded : 1;        // bit 23
    u32 mode : 2;          // bits 24-25
    u32 unk460_26 : 6;
    u32 bgm;       // 0x464  the battle's music
    HeapID heapId; // 0x468
}; // 0x46c

// The red flash of the gauges losing HP
typedef struct {
    BtlvGauge *gauge;
    u32 seq;
    u32 paletteMask;
    u32 count;
} BtlvGaugeFlashTask;

static void BtlvGauge_LoadResources(BtlvGauge *gauge);
static void BtlvGauge_FreeResources(BtlvGauge *gauge);
static u32 BtlvGauge_BackPaletteFile(void);
static void BtlvGauge_CreateActors(BtlvGauge *gauge, u32 type, int index);
static void BtlvGauge_DeleteActors(BtlvGauge *gauge, int index);
static void BtlvGauge_Show(BtlvGauge *gauge, u32 type, int index, PartyPkm *pkm);
static void BtlvGauge_CalcHpDots(BtlvGaugeEntry *entry);
static void BtlvGauge_SetHpTarget(BtlvGauge *gauge, BtlvGaugeEntry *entry, s32 hp);
static void BtlvGauge_UpdateHp(BtlvGauge *gauge, int index);
static void BtlvGauge_SetExpTarget(BtlvGaugeEntry *entry, s32 exp);
static void BtlvGauge_UpdateExp(BtlvGauge *gauge, int index);
static s32 BtlvGauge_StepGauge(s32 max, s32 cur, s32 delta, s32 *anim, u8 size, u16 speed);
static u8 BtlvGauge_CalcBarTiles(s32 max, s32 cur, s32 delta, s32 *anim, u8 *tiles, u8 size);
static s32 BtlvGauge_DotDistance(s32 value, s32 delta, s32 max, u8 size);
static void BtlvGauge_DrawName(BtlvGauge *gauge, BtlvGaugeEntry *entry, PartyPkm *pkm);
static void BtlvGauge_DrawSex(BtlvGauge *gauge, BtlvGaugeEntry *entry);
static void BtlvGauge_DrawBar(BtlvGauge *gauge, int index, int bar);
static void BtlvGauge_DrawHpNumbers(BtlvGauge *gauge, BtlvGaugeEntry *entry, s32 hp);
static void BtlvGauge_DrawLevel(BtlvGauge *gauge, BtlvGaugeEntry *entry);
static void BtlvGauge_DrawCaughtMark(BtlvGauge *gauge, BtlvGaugeEntry *entry);
static void BtlvGauge_DrawDamage(BtlvGauge *gauge, BtlvGaugeEntry *entry, s32 dots);
static void BtlvGauge_UpdateLevelUp(BtlvGauge *gauge, int index);
static void BtlvGauge_UpdateShake(BtlvGauge *gauge, int index);
static void BtlvGauge_ReadStatus(BtlvGauge *gauge, int index, u32 *color, u32 *status, BOOL fromActor);
static void BtlvGauge_InitStatusOnly(BtlvGauge *gauge);
static void BtlvGauge_UpdatePinchBgm(BtlvGauge *gauge);
static void BtlvGauge_SlideInTask(TCB *tcb, void *data);
static void BtlvGauge_FlashTask(TCB *tcb, void *data);
static void BtlvGauge_PauseBgm(BOOL paused);
static u32 BtlvGauge_CalcFill(u32 value, u32 max, u32 width);
static void BtlvGauge_ShowHpParts(BtlvGauge *gauge, int index, BOOL visible);
static void BtlvGauge_UpdateDamageBlink(BtlvGaugeEntry *entry, BOOL showNumbers, int index);

BtlvGauge *BtlvGauge_Create(Font *font, u32 mode, HeapID heapId) {
    BtlvGauge *gauge = GFL_HeapAllocate(heapId, sizeof(BtlvGauge), TRUE, "btlv_gauge.c", 340);
    BtlvGaugeFlashTask *flash;

    gauge->heapId = heapId;
    gauge->arc = GFL_ArcSysCreateFileHandle(11, gauge->heapId);
    gauge->unit = func_0204bf1c(40, 0, gauge->heapId);
    gauge->font = font;
    gauge->bgm = GFL_SndBGMGetID();
    flash = GFL_HeapAllocate(heapId, sizeof(BtlvGaugeFlashTask), TRUE, "btlv_gauge.c", 355);
    flash->gauge = gauge;
    gauge->flashTask = GFL_TCBMgrAddTask(BtlvEffect_GetTCBManager(), BtlvGauge_FlashTask, flash, 0);
    gauge->mode = mode;
    if (mode != 2) {
        BtlvGauge_LoadResources(gauge);
    } else {
        BtlvGauge_InitStatusOnly(gauge);
    }
    return gauge;
}

void BtlvGauge_Delete(BtlvGauge *gauge) {
    BtlvGauge_FreeResources(gauge);
    if (gauge->pinch) {
        GFL_SndBGMPop();
        BtlvGauge_PauseBgm(FALSE);
    }
    func_0204bf98(gauge->unit);
    GFL_ArcToolFree(gauge->arc);
    GFL_HeapFree(GFL_TCBGetData(gauge->flashTask));
    GFL_TCBRemove(gauge->flashTask);
    GFL_HeapFree(gauge);
}

void BtlvGauge_Update(BtlvGauge *gauge) {
    int i;

    GCTX_HIDGetPressedKeys();
    if (gauge->toggleRequest) {
        u32 rule = BtlvEffect_GetBattleStyle();

        gauge->toggleRequest = FALSE;
        if (rule != BTL_STYLE_SINGLE && rule != BTL_STYLE_DOUBLE) {
            BOOL moving = FALSE;

            for (i = 0; i < 8; i++) {
                if (gauge->entries[i].mon.hpMoving) {
                    moving = TRUE;
                    break;
                }
            }
            if (!moving) {
                gauge->showNumbers ^= 1;
                for (i = 0; i < 8; i++) {
                    if (!(i & 1) && gauge->entries[i].mon.visible == TRUE && gauge->entries[i].mon.shown) {
                        BtlvGauge_ShowHpParts(gauge, i, TRUE);
                        BtlvGauge_StepGauge(gauge->entries[i].mon.maxHp, gauge->entries[i].mon.hp, 0,
                                            &gauge->entries[i].mon.hpAnim, 6, 1);
                        BtlvGauge_DrawBar(gauge, i, 0);
                    }
                }
            }
        }
    }
    for (i = 0; i < 8; i++) {
        if (gauge->entries[i].mon.hpMoving) {
            BtlvGauge_UpdateHp(gauge, i);
        }
        if (gauge->entries[i].mon.expMoving) {
            if (gauge->entries[i].mon.expSeFrames == 0) {
                GFL_SndSEPlay(SEQ_SE_EXP);
            }
            BtlvGauge_UpdateExp(gauge, i);
        }
        if (gauge->entries[i].mon.expSe) {
            if (gauge->entries[i].mon.expSeFrames < 16) {
                gauge->entries[i].mon.expSeFrames++;
            }
            if (!gauge->entries[i].mon.expMoving && gauge->entries[i].mon.expSeFrames == 16) {
                GFL_SndStop();
                gauge->entries[i].mon.expSeFrames++;
                gauge->entries[i].mon.expSe = FALSE;
            }
        }
        if (gauge->entries[i].mon.levelUp) {
            BtlvGauge_UpdateLevelUp(gauge, i);
        }
        if (gauge->entries[i].mon.shaking) {
            BtlvGauge_UpdateShake(gauge, i);
        }
        BtlvGauge_UpdateDamageBlink(&gauge->entries[i], gauge->showNumbers, i);
    }
    if (!gauge->noPinchBgm && gauge->mode == 0) {
        BtlvGauge_UpdatePinchBgm(gauge);
    }
}

void BtlvGauge_SetBattleMon(BtlvGauge *gauge, BtlMainModule *mainModule, BattleMon *mon, u32 type, int index) {
    u32 status;

    MIi_CpuClear16(0, &gauge->entries[index].mon, sizeof(BtlvGaugeMon));
    gauge->entries[index].mon.hp = GetBattleMonStat(mon, BATTLEMON_HP);
    gauge->entries[index].mon.maxHp = GetBattleMonStat(mon, 14);
    gauge->entries[index].mon.hpAnim = GAUGE_ANIM_START;
    gauge->entries[index].mon.hpDelta = 0;
    gauge->entries[index].mon.level = GetBattleMonStat(mon, BATTLEMON_LEVEL);
    gauge->entries[index].mon.type = type;
    gauge->entries[index].mon.side = index & 1;
    gauge->entries[index].mon.changed = TRUE;
    if (gauge->entries[index].mon.level < 100) {
        u32 base = PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                                       GetBattleMonStat(mon, BATTLEMON_LEVEL));

        gauge->entries[index].mon.exp = GetBattleMonStat(mon, 20) - base;
        gauge->entries[index].mon.expMax =
            PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                                GetBattleMonStat(mon, BATTLEMON_LEVEL) + 1) -
            base;
    } else {
        gauge->entries[index].mon.exp = 0;
        gauge->entries[index].mon.expMax = 0;
    }
    gauge->entries[index].mon.expAnim = GAUGE_ANIM_START;
    gauge->entries[index].mon.expDelta = 0;
    gauge->entries[index].mon.unk2e = 0;
    gauge->entries[index].mon.caught = func_ov167_0219bf70(mainModule, mon);
    if (PokeParty_GetParam(func_ov167_021bb064(mon), PKM_PARAM_SHOW_SEX, NULL) == 0) {
        gauge->entries[index].mon.sex = 2;
    } else {
        gauge->entries[index].mon.sex = PokeParty_GetSex(func_ov167_021bb064(mon));
    }
    if (Condition_IsBadlyPoisoned(GetConditionContinuationParam(mon, CONDITION_POISON)) == TRUE &&
        GetBattleMonStatus(mon) == CONDITION_POISON) {
        status = 6;
    } else {
        status = GetBattleMonStatus(mon);
    }
    BtlvGauge_SetStatus(gauge, status, index);
    BtlvGauge_SetPos(gauge, index, NULL);
    BtlvGauge_ShowHpParts(gauge, index, TRUE);
    BtlvGauge_CalcHpDots(&gauge->entries[index]);
    BtlvGauge_Show(gauge, type, index, func_ov167_021bb064(mon));
}

void BtlvGauge_SetPartyPkm(BtlvGauge *gauge, PokeDexSave *pokedex, PartyPkm *pkm, u32 type, int index) {
    u16 species;

    MIi_CpuClear16(0, &gauge->entries[index].mon, sizeof(BtlvGaugeMon));
    gauge->entries[index].mon.hp = PokeParty_GetParam(pkm, PKM_PARAM_HP, NULL);
    gauge->entries[index].mon.maxHp = PokeParty_GetParam(pkm, PKM_PARAM_MAX_HP, NULL);
    gauge->entries[index].mon.hpAnim = GAUGE_ANIM_START;
    gauge->entries[index].mon.hpDelta = 0;
    gauge->entries[index].mon.level = PokeParty_GetParam(pkm, PKM_PARAM_LEVEL, NULL);
    gauge->entries[index].mon.type = type;
    gauge->entries[index].mon.side = index & 1;
    gauge->entries[index].mon.changed = TRUE;
    if (gauge->entries[index].mon.level < 100) {
        u32 monsno = PokeParty_GetParam(pkm, PKM_PARAM_SPECIES, NULL);
        u32 form = PokeParty_GetParam(pkm, PKM_PARAM_FORM, NULL);
        u32 base = PML_UtilGetPkmLvExp(monsno, form, gauge->entries[index].mon.level);

        gauge->entries[index].mon.exp = PokeParty_GetParam(pkm, PKM_PARAM_EXP, NULL) - base;
        gauge->entries[index].mon.expMax =
            PML_UtilGetPkmLvExp(monsno, form, gauge->entries[index].mon.level + 1) - base;
    } else {
        gauge->entries[index].mon.exp = 0;
        gauge->entries[index].mon.expMax = 0;
    }
    gauge->entries[index].mon.expAnim = GAUGE_ANIM_START;
    gauge->entries[index].mon.expDelta = 0;
    gauge->entries[index].mon.unk2e = 0;
    species = PokeParty_GetParam(pkm, PKM_PARAM_SPECIES, NULL);
    gauge->entries[index].mon.caught = PokeDex_IsCaught(pokedex, species);
    if (species != SPECIES_NIDORAN_M && species != SPECIES_NIDORAN_F) {
        gauge->entries[index].mon.sex = PokeParty_GetSex(pkm);
    } else {
        gauge->entries[index].mon.sex = 2;
    }
    BtlvGauge_SetStatus(gauge, GetStatusCond(pkm), index);
    BtlvGauge_SetPos(gauge, index, NULL);
    BtlvGauge_ShowHpParts(gauge, index, TRUE);
    BtlvGauge_CalcHpDots(&gauge->entries[index]);
    BtlvGauge_Show(gauge, type, index, pkm);
}

u32 BtlvGauge_PaletteFile(void) {
    return GAUGE_PALETTE_FILE;
}

static void BtlvGauge_LoadResources(BtlvGauge *gauge) {
    int i;
    ArcTool *arc;
    NNSG2dCharacterData *character;
    int start;
    int end;
    u32 type;

    gauge->loaded = TRUE;
    for (i = 0; i < 6; i++) {
        gauge->palettes[i] =
            func_0204bba0(gauge->arc, BtlvGauge_PaletteFile(), CLACT_VRAM_MAIN, i * 0x20, gauge->heapId);
        PaletteFade_LoadFromVRAM(BtlvEffect_GetPaletteFade(), PALFADE_VRAM_MAIN_OBJ,
                                 func_0204bdc0(gauge->palettes[i], FALSE) / 2, 0x20);
    }
    arc = GFL_ArcSysCreateFileHandle(getUINarcIdx(), HEAPID_TAIL(gauge->heapId));
    gauge->statusChars = func_0204b81c(arc, func_0202d8b4(), FALSE, CLACT_VRAM_MAIN, gauge->heapId);
    gauge->statusCellAnims = func_0204bde0(arc, func_0202d8b8(1), func_0202d8bc(1), gauge->heapId);
    gauge->statusPalette = func_0204bba0(arc, func_0202d8b0(), CLACT_VRAM_MAIN, 0x100, gauge->heapId);
    gauge->statusPaletteDark = func_0204bba0(arc, func_0202d8b0(), CLACT_VRAM_MAIN, 0x120, gauge->heapId);
    PaletteFade_LoadFromVRAM(BtlvEffect_GetPaletteFade(), PALFADE_VRAM_MAIN_OBJ,
                             func_0204bdc0(gauge->statusPalette, FALSE) / 2, 0x20);
    PaletteFade_LoadFromVRAM(BtlvEffect_GetPaletteFade(), PALFADE_VRAM_MAIN_OBJ,
                             func_0204bdc0(gauge->statusPaletteDark, FALSE) / 2, 0x20);
    PaletteFade_BlendPalettes(BtlvEffect_GetPaletteFade(), PALFADE_BUFFER_MAIN_OBJ, 1 << 9, 8, 0);
    GFL_ArcToolFree(arc);
    gauge->charFile = GFL_G2DIOReadOBJNCGRArc(gauge->arc, 0x1b2, FALSE, &character, gauge->heapId);
    gauge->chars = character->pRawData;
    switch (BtlvEffect_GetBattleStyle()) {
    case BTL_STYLE_SINGLE:
        start = 0;
        end = 1;
        type = BTL_STYLE_SINGLE;
        break;
    case BTL_STYLE_DOUBLE:
        start = 2;
        end = 5;
        type = BTL_STYLE_DOUBLE;
        break;
    case BTL_STYLE_TRIPLE:
    default:
        start = 2;
        end = 7;
        type = BTL_STYLE_TRIPLE;
        break;
    case BTL_STYLE_ROTATION:
        start = 2;
        end = 7;
        type = BTL_STYLE_ROTATION;
        break;
    }
    for (i = start; i <= end; i++) {
        ClActorPos pos = { 128, 0 };

        if (i & 1) {
            pos.x *= -1;
        }
        BtlvGauge_CreateActors(gauge, type, i);
        BtlvGauge_SetPos(gauge, i, &pos);
    }
}

static void BtlvGauge_FreeResources(BtlvGauge *gauge) {
    BtlvGaugePos pos;
    int i;

    if (gauge->loaded == TRUE) {
        for (pos = 0; pos < BTLV_GAUGE_POS_MAX; pos++) {
            BtlvGauge_Hide(gauge, pos);
            BtlvGauge_DeleteActors(gauge, pos);
        }
        for (i = 0; i < 6; i++) {
            func_0204bcd0(gauge->palettes[i]);
        }
        func_0204b98c(gauge->statusChars);
        func_0204be64(gauge->statusCellAnims);
        func_0204bcd0(gauge->statusPalette);
        func_0204bcd0(gauge->statusPaletteDark);
        GFL_HeapFree(gauge->charFile);
        gauge->loaded = FALSE;
    }
}

static u32 BtlvGauge_BackPaletteFile(void) {
    return GAUGE_PALETTE_FILE_BACK;
}

static void BtlvGauge_CreateActors(BtlvGauge *gauge, u32 type, int index) {
    // The gauges' priorities by view position: in single and double battles, triple and rotation
    static const u8 data_ov168_021f3d8c[6] = { 0, 0, 3, 3, 0, 0 };
    static const u8 data_ov168_021f3d92[8] = { 3, 0, 6, 6, 3, 3, 0, 0 };
    static const u8 data_ov168_021f3d9a[8] = { 3, 0, 3, 3, 6, 6, 0, 0 };
    u32 charsFile;
    u32 cellFile;
    u32 animFile;

    gauge->entries[index].mon.type = type;
    switch (type) {
    case BTL_STYLE_SINGLE:
    case BTL_STYLE_DOUBLE:
    default:
        charsFile = index & 1 ? 0x1b3 : 0x1b6;
        cellFile = index & 1 ? 0x1b4 : 0x1b7;
        animFile = index & 1 ? 0x1b5 : 0x1b8;
        break;
    case BTL_STYLE_TRIPLE:
    case BTL_STYLE_ROTATION:
        charsFile = index & 1 ? 0x1b9 : 0x1bc;
        cellFile = index & 1 ? 0x1ba : 0x1bd;
        animFile = index & 1 ? 0x1bb : 0x1be;
        break;
    case 4:
        break;
    }
    if (type == BTL_STYLE_ROTATION) {
        int slot = index;
        u32 paletteFile;

        if (index >= 2) {
            slot = index - 2;
        }
        paletteFile = index < 4 ? BtlvGauge_PaletteFile() : BtlvGauge_BackPaletteFile();
        func_0204bcd0(gauge->palettes[slot]);
        gauge->palettes[slot] =
            func_0204bbb8(gauge->arc, paletteFile, CLACT_VRAM_MAIN, slot * 0x20, 0, 1, gauge->heapId);
        PaletteFade_LoadFromVRAM(BtlvEffect_GetPaletteFade(), PALFADE_VRAM_MAIN_OBJ,
                                 func_0204bdc0(gauge->palettes[slot], FALSE) / 2, 0x20);
    }
    gauge->entries[index].nameChars = func_0204b81c(gauge->arc, charsFile, FALSE, CLACT_VRAM_MAIN, gauge->heapId);
    gauge->entries[index].nameCellAnims = func_0204bde0(gauge->arc, cellFile, animFile, gauge->heapId);
    gauge->entries[index].hpNumChars = func_0204b81c(gauge->arc, 0x1c8, FALSE, CLACT_VRAM_MAIN, gauge->heapId);
    gauge->entries[index].hpNumCellAnims = func_0204bde0(gauge->arc, 0x1c9, 0x1ca, gauge->heapId);
    gauge->entries[index].hpBarChars = func_0204b81c(gauge->arc, 0x1bf, FALSE, CLACT_VRAM_MAIN, gauge->heapId);
    gauge->entries[index].hpBarCellAnims = func_0204bde0(gauge->arc, 0x1c0, 0x1c1, gauge->heapId);
    gauge->entries[index].damageChars = func_0204b81c(gauge->arc, 0x1c2, FALSE, CLACT_VRAM_MAIN, gauge->heapId);
    gauge->entries[index].damageCellAnims = func_0204bde0(gauge->arc, 0x1c3, 0x1c1, gauge->heapId);
    if (!(index & 1)) {
        gauge->entries[index].expChars = func_0204b81c(gauge->arc, 0x1c5, FALSE, CLACT_VRAM_MAIN, gauge->heapId);
        gauge->entries[index].expCellAnims = func_0204bde0(gauge->arc, 0x1c6, 0x1c7, gauge->heapId);
    }
    {
        ClActorSetup setup = { 0, 0, 0, 0, 1 };
        int slot = index;
        u8 priority;

        if (index >= 2) {
            slot = index - 2;
        }
        if (type == BTL_STYLE_TRIPLE) {
            priority = data_ov168_021f3d92[index];
        } else if (type == BTL_STYLE_ROTATION) {
            priority = data_ov168_021f3d9a[index];
        } else {
            priority = data_ov168_021f3d8c[index];
        }
        setup.priority = priority + 2;
        gauge->entries[index].nameActor =
            func_0204c040(gauge->unit, gauge->entries[index].nameChars, gauge->palettes[slot],
                          gauge->entries[index].nameCellAnims, &setup, CLACT_SURFACE_MAIN, gauge->heapId);
        setup.priority = priority + 1;
        gauge->entries[index].hpNumActor =
            func_0204c040(gauge->unit, gauge->entries[index].hpNumChars, gauge->palettes[slot],
                          gauge->entries[index].hpNumCellAnims, &setup, CLACT_SURFACE_MAIN, gauge->heapId);
        gauge->entries[index].hpBarActor =
            func_0204c040(gauge->unit, gauge->entries[index].hpBarChars, gauge->palettes[slot],
                          gauge->entries[index].hpBarCellAnims, &setup, CLACT_SURFACE_MAIN, gauge->heapId);
        if (BtlvEffect_GetBattleStyle() == BTL_STYLE_ROTATION && index >= 4) {
            gauge->entries[index].statusActor =
                func_0204c040(gauge->unit, gauge->statusChars, gauge->statusPaletteDark, gauge->statusCellAnims, &setup,
                              CLACT_SURFACE_MAIN, gauge->heapId);
        } else {
            gauge->entries[index].statusActor =
                func_0204c040(gauge->unit, gauge->statusChars, gauge->statusPalette, gauge->statusCellAnims, &setup,
                              CLACT_SURFACE_MAIN, gauge->heapId);
        }
        func_0204c520(gauge->entries[index].statusActor, TRUE);
        setup.priority = priority;
        gauge->entries[index].damageActor =
            func_0204c040(gauge->unit, gauge->entries[index].damageChars, gauge->palettes[slot],
                          gauge->entries[index].damageCellAnims, &setup, CLACT_SURFACE_MAIN, gauge->heapId);
        func_0204c124(gauge->entries[index].damageActor, FALSE);
        if (!(index & 1) && type != BTL_STYLE_TRIPLE && type != BTL_STYLE_ROTATION) {
            gauge->entries[index].expActor =
                func_0204c040(gauge->unit, gauge->entries[index].expChars, gauge->palettes[slot],
                              gauge->entries[index].expCellAnims, &setup, CLACT_SURFACE_MAIN, gauge->heapId);
        }
    }
}

static void BtlvGauge_DeleteActors(BtlvGauge *gauge, int index) {
    if (gauge->entries[index].nameActor != NULL) {
        func_0204b98c(gauge->entries[index].nameChars);
        func_0204b98c(gauge->entries[index].hpNumChars);
        func_0204b98c(gauge->entries[index].hpBarChars);
        func_0204b98c(gauge->entries[index].damageChars);
        func_0204be64(gauge->entries[index].nameCellAnims);
        func_0204be64(gauge->entries[index].hpNumCellAnims);
        func_0204be64(gauge->entries[index].hpBarCellAnims);
        func_0204be64(gauge->entries[index].damageCellAnims);
        func_0204c108(gauge->entries[index].nameActor);
        func_0204c108(gauge->entries[index].hpNumActor);
        func_0204c108(gauge->entries[index].hpBarActor);
        func_0204c108(gauge->entries[index].damageActor);
        func_0204c108(gauge->entries[index].statusActor);
        gauge->entries[index].nameActor = NULL;
        gauge->entries[index].hpNumActor = NULL;
        gauge->entries[index].hpBarActor = NULL;
        gauge->entries[index].damageActor = NULL;
        gauge->entries[index].statusActor = NULL;
        if (!(index & 1)) {
            func_0204b98c(gauge->entries[index].expChars);
            func_0204be64(gauge->entries[index].expCellAnims);
        }
        if (gauge->entries[index].expActor != NULL) {
            func_0204c108(gauge->entries[index].expActor);
            gauge->entries[index].expActor = NULL;
        }
    }
}

static void BtlvGauge_Show(BtlvGauge *gauge, u32 type, int index, PartyPkm *pkm) {
    BtlvGauge_StepGauge(gauge->entries[index].mon.maxHp, gauge->entries[index].mon.hp, 0,
                        &gauge->entries[index].mon.hpAnim, 6, 1);
    BtlvGauge_DrawBar(gauge, index, 0);
    if (gauge->entries[index].expActor != NULL) {
        BtlvGauge_StepGauge(gauge->entries[index].mon.expMax, gauge->entries[index].mon.exp, 0,
                            &gauge->entries[index].mon.expAnim, 10, 1);
        BtlvGauge_DrawBar(gauge, index, 1);
    }
    BtlvGauge_DrawName(gauge, &gauge->entries[index], pkm);
    BtlvGauge_DrawSex(gauge, &gauge->entries[index]);
    BtlvGauge_DrawHpNumbers(gauge, &gauge->entries[index], gauge->entries[index].mon.hp);
    BtlvGauge_DrawLevel(gauge, &gauge->entries[index]);
    if (gauge->entries[index].mon.side && !BtlvEffect_GetBattleType()) {
        BtlvGauge_DrawCaughtMark(gauge, &gauge->entries[index]);
    }
    gauge->entries[index].mon.shown = TRUE;
    gauge->entries[index].mon.slideFrames = 2;
    gauge->entries[index].mon.slideTask =
        GFL_TCBMgrAddTask(BtlvEffect_GetTCBManager(), BtlvGauge_SlideInTask, &gauge->entries[index], 0);
}

static void BtlvGauge_CalcHpDots(BtlvGaugeEntry *entry) {
    if (entry->mon.maxHp < 48) {
        entry->mon.hpDots = BtlvGauge_CalcFill(entry->mon.hp << 8, entry->mon.maxHp, 48) >> 8;
    } else {
        entry->mon.hpDots = BtlvGauge_CalcFill(entry->mon.hp, entry->mon.maxHp, 48);
    }
}

void BtlvGauge_Hide(BtlvGauge *gauge, int index) {
    if (gauge->entries[index].mon.shown) {
        ClActorPos pos = { 128, 0 };

        if (index & 1) {
            pos.x *= -1;
        }
        BtlvGauge_SetPos(gauge, index, &pos);
        gauge->entries[index].mon.shown = FALSE;
    }
    if (gauge->entries[index].mon.slideTask != NULL) {
        GFL_TCBRemove(gauge->entries[index].mon.slideTask);
        gauge->entries[index].mon.slideTask = NULL;
    }
}

void BtlvGauge_SetPos(BtlvGauge *gauge, int index, const ClActorPos *offset) {
    ClActorPos pos;
    ClActorPos hpBarOffsets[2] = { { 8, 7 }, { 0, 7 } };
    ClActorPos statusOffsets[2] = { { -30, 8 }, { -38, 8 } };
    // bwbattle: Platinum's taller name font needs more room below the upper player's EXP bar in doubles.
    ClActorPos basePositions[6] = { { 216, 120 }, { 44, 40 }, { 216, 94 }, { 48, 28 }, { 220, 128 }, { 44, 52 } };
    ClActorPos triplePositions[8] = {
        { 216, 120 }, { 44, 40 }, { 216, 98 }, { 48, 20 }, { 220, 117 }, { 44, 39 }, { 224, 136 }, { 40, 58 },
    };
    ClActorPos rotationPositions[8] = {
        { 216, 120 }, { 44, 40 }, { 220, 117 }, { 44, 39 }, { 216, 98 }, { 48, 20 }, { 224, 136 }, { 40, 58 },
    };
    int x;
    int y;
    int side;

    if (gauge->entries[index].mon.type == BTL_STYLE_TRIPLE) {
        x = triplePositions[index].x;
        y = triplePositions[index].y;
    } else if (gauge->entries[index].mon.type == BTL_STYLE_ROTATION) {
        x = rotationPositions[index].x;
        y = rotationPositions[index].y;
    } else {
        x = basePositions[index].x;
        y = basePositions[index].y;
    }
    if (offset != NULL) {
        x += offset->x;
        y += offset->y;
    }
    pos.x = x;
    pos.y = y;
    if (gauge->entries[index].nameActor != NULL) {
        func_0204c140(gauge->entries[index].nameActor, &pos, CLACT_SURFACE_MAIN);
    }
    side = index & 1;
    if (!side && gauge->entries[index].mon.type == BTL_STYLE_SINGLE && gauge->entries[index].hpNumActor != NULL) {
        pos.x = x + 16;
        pos.y = y + 13;
        func_0204c140(gauge->entries[index].hpNumActor, &pos, CLACT_SURFACE_MAIN);
    } else if (!side && gauge->entries[index].mon.type != BTL_STYLE_DOUBLE &&
               gauge->entries[index].hpNumActor != NULL) {
        pos.x = x + 16;
        pos.y = y + 7;
        func_0204c140(gauge->entries[index].hpNumActor, &pos, CLACT_SURFACE_MAIN);
    }
    if (gauge->entries[index].hpBarActor != NULL) {
        pos.x = x + hpBarOffsets[side].x;
        pos.y = y + hpBarOffsets[side].y;
        func_0204c140(gauge->entries[index].hpBarActor, &pos, CLACT_SURFACE_MAIN);
    }
    if (gauge->entries[index].statusActor != NULL) {
        pos.x = x + statusOffsets[side].x;
        pos.y = y + statusOffsets[side].y;
        func_0204c140(gauge->entries[index].statusActor, &pos, CLACT_SURFACE_MAIN);
    }
    if (gauge->entries[index].expActor != NULL) {
        pos.x = x + 8;
        pos.y = y + 21;
        func_0204c140(gauge->entries[index].expActor, &pos, CLACT_SURFACE_MAIN);
    }
}

void BtlvGauge_StartHpChange(BtlvGauge *gauge, int index, s32 hp) {
    BtlvGauge_SetHpTarget(gauge, &gauge->entries[index], hp);
    gauge->entries[index].mon.hpSpeed = 1;
}

void BtlvGauge_SetHpAtOnce(BtlvGauge *gauge, int index, s32 hp) {
    BtlvGauge_SetHpTarget(gauge, &gauge->entries[index], hp);
    gauge->entries[index].mon.hpSpeed = 0;
}

#ifdef BWBATTLE
void BtlvGauge_SyncHp(BtlvGauge *gauge, int index, s32 hp, BOOL moving) {
    BtlvGaugeEntry *entry = &gauge->entries[index];

    hp = hp < 0 ? 0 : (hp > entry->mon.maxHp ? entry->mon.maxHp : hp);
    if (!entry->mon.hpSyncMoving) {
        entry->mon.hpSyncDamage = FALSE;
        entry->mon.blinking = FALSE;
        entry->mon.blinkCount = 0;
        if (entry->damageActor != NULL) {
            func_0204c124(entry->damageActor, FALSE);
        }
    }
    // Keep the damage strip's origin at the HP before the hit, across all samples.
    if (hp < entry->mon.hp && !entry->mon.hpSyncDamage) {
        BtlvGauge_SetHpTarget(gauge, entry, hp);
        entry->mon.hpSyncDamage = TRUE;
        entry->mon.hpSyncFlash = TRUE;
    }
    entry->mon.hpDelta = entry->mon.hp - hp;
    entry->mon.hpAnim = GAUGE_ANIM_START;
    BtlvGauge_StepGauge(entry->mon.maxHp, entry->mon.hp, entry->mon.hpDelta, &entry->mon.hpAnim, 6, 0);
    BtlvGauge_DrawBar(gauge, index, 0);
    BtlvGauge_DrawHpNumbers(gauge, entry, hp);
    entry->mon.hp = hp;
    entry->mon.hpDelta = 0;
    // Platinum owns the drain; treating each sample as a BW drain restarts its flashes.
    entry->mon.hpMoving = FALSE;
    entry->mon.hpSyncMoving = moving;
    if (!moving && entry->mon.hpSyncDamage) {
        entry->mon.blinkCount = 24;
        entry->mon.blinking = TRUE;
    }
}

// bwbattle: shows the experience points of the level at once, as BtlvGauge_SetHpAtOnce the HP (the adapter follows
// Platinum's healthbox with them)
void BtlvGauge_SetExpAtOnce(BtlvGauge *gauge, int index, s32 exp) {
    BtlvGaugeEntry *entry = &gauge->entries[index];

    if (entry->expActor == NULL) {
        return;
    }
    entry->mon.exp = exp < 0 ? 0 : (exp > entry->mon.expMax ? entry->mon.expMax : exp);
    entry->mon.expDelta = 0;
    entry->mon.expAnim = GAUGE_ANIM_START;
    entry->mon.expMoving = FALSE;
    // The bar's dots from the value, as BtlvGauge_Show has them before it draws
    BtlvGauge_StepGauge(entry->mon.expMax, entry->mon.exp, 0, &entry->mon.expAnim, 10, 1);
    BtlvGauge_DrawBar(gauge, index, 1);
}
#endif

void BtlvGauge_StartExpGain(BtlvGauge *gauge, int index, s32 exp) {
    gauge->entries[index].mon.expSeFrames = 0;
    gauge->entries[index].mon.expSe = TRUE;
    BtlvGauge_SetExpTarget(&gauge->entries[index], exp);
}

void BtlvGauge_StartLevelUp(BtlvGauge *gauge, BattleMon *mon, int index) {
    s32 exp = gauge->entries[index].mon.expMax - gauge->entries[index].mon.exp;

    gauge->entries[index].mon.hp = GetBattleMonStat(mon, BATTLEMON_HP);
    gauge->entries[index].mon.maxHp = GetBattleMonStat(mon, 14);
    gauge->entries[index].mon.hpAnim = GAUGE_ANIM_START;
    gauge->entries[index].mon.hpDelta = 0;
    gauge->entries[index].mon.level = GetBattleMonStat(mon, BATTLEMON_LEVEL);
    if (gauge->entries[index].mon.level < 100) {
        u32 next = PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                                       GetBattleMonStat(mon, BATTLEMON_LEVEL) + 1);

        gauge->entries[index].mon.nextExpMax =
            next - PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                                       GetBattleMonStat(mon, BATTLEMON_LEVEL));
    } else {
        u32 next = PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                                       GetBattleMonStat(mon, BATTLEMON_LEVEL));

        gauge->entries[index].mon.nextExpMax =
            next - PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                                       GetBattleMonStat(mon, BATTLEMON_LEVEL) - 1);
    }
    // Computed and dropped
    PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                        GetBattleMonStat(mon, BATTLEMON_LEVEL));
    PML_UtilGetPkmLvExp(GetBattleMonSpecies(mon), GetBattleMonStat(mon, BATTLEMON_FORM),
                        GetBattleMonStat(mon, BATTLEMON_LEVEL) - 1);
    gauge->entries[index].mon.levelUpSeq = 0;
    gauge->entries[index].mon.levelUp = TRUE;
    gauge->entries[index].mon.expSeFrames = 0;
    gauge->entries[index].mon.expSe = TRUE;
    BtlvGauge_SetExpTarget(&gauge->entries[index], exp);
}

BOOL BtlvGauge_IsBusy(BtlvGauge *gauge) {
    int i;

    for (i = 0; i < 8; i++) {
        if (gauge->entries[i].mon.hpMoving || gauge->entries[i].mon.expMoving || gauge->entries[i].mon.levelUp ||
            gauge->entries[i].mon.expSe) {
            return TRUE;
        }
    }
    return FALSE;
}

void BtlvGauge_SetSideVisible(BtlvGauge *gauge, BOOL visible, u32 side) {
    BtlvGaugePos i;

    for (i = 0; i < BTLV_GAUGE_POS_MAX; i++) {
        if ((side == 2 || side == gauge->entries[i].mon.side) && gauge->entries[i].mon.shown) {
            BtlvGauge_SetVisible(gauge, visible, i);
        }
    }
}

void BtlvGauge_SetVisible(BtlvGauge *gauge, BOOL visible, int index) {
    gauge->entries[index].mon.visible = visible;
    if (gauge->entries[index].nameActor != NULL) {
        func_0204c124(gauge->entries[index].nameActor, visible);
        BtlvGauge_ShowHpParts(gauge, index, visible);
    }
    if (gauge->entries[index].expActor != NULL) {
        func_0204c124(gauge->entries[index].expActor, visible);
    }
    if (gauge->entries[index].statusActor != NULL && func_0204c4a0(gauge->entries[index].statusActor) != 0) {
        func_0204c124(gauge->entries[index].statusActor, visible);
    }
}

BOOL BtlvGauge_IsShown(BtlvGauge *gauge, int index) {
    if (gauge->entries[index].mon.shown) {
        return TRUE;
    }
    return FALSE;
}

BOOL BtlvGauge_CheckChanged(BtlvGauge *gauge, int index) {
    BOOL changed;

    if (gauge->entries[index].nameActor != NULL) {
        changed = gauge->entries[index].mon.changed;
        gauge->entries[index].mon.changed = FALSE;
    } else {
        changed = TRUE;
    }
    return changed;
}

static void BtlvGauge_SetHpTarget(BtlvGauge *gauge, BtlvGaugeEntry *entry, s32 hp) {
    ClActorPos pos;

    entry->mon.hpAnim = GAUGE_ANIM_START;
    entry->mon.hpDelta = entry->mon.hp - hp;
    if (entry->mon.hp < 0) {
        entry->mon.hp = 0;
    }
    if (entry->mon.hp > entry->mon.maxHp) {
        entry->mon.hp = entry->mon.maxHp;
    }
    entry->mon.hpMoving = TRUE;
    if (entry->mon.hpDelta > 0) {
        BtlvGauge_CalcHpDots(entry);
        if (entry->hpBarActor != NULL) {
            func_0204c178(entry->hpBarActor, &pos, CLACT_SURFACE_MAIN);
        }
        pos.x -= 48 - entry->mon.hpDots;
        if (entry->damageActor != NULL) {
            func_0204c140(entry->damageActor, &pos, CLACT_SURFACE_MAIN);
        }
        if (entry->damageActor != NULL) {
            func_0204c124(entry->damageActor, TRUE);
        }
        BtlvGauge_DrawDamage(gauge, entry, entry->mon.hpDots);
    }
}

static void BtlvGauge_UpdateHp(BtlvGauge *gauge, int index) {
    BtlvGaugeEntry *entry = &gauge->entries[index];
    s32 hp = BtlvGauge_StepGauge(entry->mon.maxHp, entry->mon.hp, entry->mon.hpDelta, &entry->mon.hpAnim, 6,
                                 entry->mon.hpSpeed);

    BtlvGauge_DrawBar(gauge, index, 0);
    if (hp == -1) {
        entry->mon.hp -= entry->mon.hpDelta;
        BtlvGauge_DrawHpNumbers(gauge, entry, entry->mon.hp);
        entry->mon.hpMoving = FALSE;
        entry->mon.hpAnim = 0;
        if (entry->mon.hpDelta > 0) {
            entry->mon.blinkCount = 18;
            entry->mon.blinking = TRUE;
        }
    } else {
        BtlvGauge_DrawHpNumbers(gauge, entry, hp);
    }
}

static void BtlvGauge_UpdateDamageBlink(BtlvGaugeEntry *entry, BOOL showNumbers, int index) {
    if (entry->damageActor == NULL || !entry->mon.blinking) {
        return;
    }
    if (entry->mon.blinkCount == 0 || !entry->mon.shown || !entry->mon.visible) {
        func_0204c124(entry->damageActor, FALSE);
        entry->mon.blinking = FALSE;
        return;
    }
#ifdef BWBATTLE
    // Damage feedback follows the ambient clock, even when quick effects take extra steps.
    if (!BtlvEffect_ShouldTickIdle()) {
        return;
    }
    func_0204c124(entry->damageActor, (entry->mon.blinkCount >> 2) & 1);
#else
    func_0204c124(entry->damageActor, entry->mon.blinkCount & 1);
#endif
    entry->mon.blinkCount--;
    if (showNumbers && !(index & 1)) {
        func_0204c124(entry->damageActor, FALSE);
    }
}

static void BtlvGauge_SetExpTarget(BtlvGaugeEntry *entry, s32 exp) {
    entry->mon.expAnim = GAUGE_ANIM_START;
    if (entry->mon.exp + exp < 0) {
        exp -= entry->mon.exp + exp;
    }
    if (entry->mon.exp + exp > entry->mon.expMax) {
        exp -= entry->mon.exp + exp - entry->mon.expMax;
    }
    entry->mon.expDelta = -exp;
    if (entry->mon.exp < 0) {
        entry->mon.exp = 0;
    }
    if (entry->mon.exp > entry->mon.expMax) {
        entry->mon.exp = entry->mon.expMax;
    }
    entry->mon.expMoving = TRUE;
}

static void BtlvGauge_UpdateExp(BtlvGauge *gauge, int index) {
    BtlvGaugeEntry *entry = &gauge->entries[index];
    s32 dots = BtlvGauge_DotDistance(entry->mon.exp, entry->mon.expDelta, entry->mon.expMax, 10);
    s32 speed;
    s32 exp;

    if (dots == 0) {
        dots = 1;
    }
    speed = entry->mon.expDelta / dots;
    if (speed < 0) {
        speed = -speed;
    }
    if (speed == 0) {
        speed = 1;
    }
    exp = BtlvGauge_StepGauge(entry->mon.expMax, entry->mon.exp, entry->mon.expDelta, &entry->mon.expAnim, 10, speed);
    BtlvGauge_DrawBar(gauge, index, 1);
    if (exp == -1) {
        entry->mon.exp -= entry->mon.expDelta;
        entry->mon.expMoving = FALSE;
        entry->mon.expAnim = 0;
    }
}

// Moves anim a step from cur toward cur - delta, returning the value it shows or -1 once there. A gauge size tiles
// long with fewer points than pixels counts in 1/256 and moves a pixel a step, the others speed points
static s32 BtlvGauge_StepGauge(s32 max, s32 cur, s32 delta, s32 *anim, u8 size, u16 speed) {
    s32 target;
    s32 value;
    u8 dots = size * 8;

    if (*anim == GAUGE_ANIM_START) {
        if (max < dots) {
            *anim = cur << 8;
        } else {
            *anim = cur;
        }
    }
    target = cur - delta;
    if (target < 0) {
        target = 0;
    } else if (target > max) {
        target = max;
    }
    if (max < dots) {
        if (target == *anim >> 8 && (*anim & 0xff) == 0) {
            return -1;
        }
    } else {
        if (target == *anim) {
            return -1;
        }
    }
    if (max < dots) {
        s32 step = (max << 8) / dots;

        if (delta < 0) {
            *anim += step;
            value = *anim >> 8;
            if (value >= target || speed == 0) {
                *anim = target << 8;
                value = target;
            }
        } else {
            *anim -= step;
            value = *anim >> 8;
            if ((*anim & 0xff) > 0) {
                value++;
            }
            if (value <= target || speed == 0) {
                *anim = target << 8;
                value = target;
            }
        }
    } else {
        if (delta < 0) {
            *anim += speed;
            if (*anim > target || speed == 0) {
                *anim = target;
            }
        } else {
            *anim -= speed;
            if (*anim < target || speed == 0) {
                *anim = target;
            }
        }
        value = *anim;
    }
    return value;
}

// Fills tiles with the pixels of each of a bar's size tiles for anim, returning the bar's pixels
static u8 BtlvGauge_CalcBarTiles(s32 max, s32 cur, s32 delta, s32 *anim, u8 *tiles, u8 size) {
    s32 target = cur - delta;
    int i;
    u32 dots;
    u32 fill;
    u32 rest;

    if (target < 0) {
        target = 0;
    } else if (target > max) {
        target = max;
    }
    dots = size * 8;
    for (i = 0; i < size; i++) {
        tiles[i] = 0;
    }
#ifdef BWBATTLE
    // bwbattle: a level 100 Pokémon's EXP bar has no EXP to fill (a max of 0), which the DS's division by 0 gave an
    // empty bar and the PC's traps on
    if (max <= 0) {
        return 0;
    }
#endif
    if (max < dots) {
        rest = *anim * dots / max >> 8;
    } else {
        rest = *anim * dots / max;
    }
    fill = rest;
    if (rest == 0 && target > 0) {
        tiles[0] = 1;
        fill = 1;
    } else {
        for (i = 0; i < size; i++) {
            if (rest >= 8) {
                tiles[i] = 8;
                rest -= 8;
            } else {
                tiles[i] = rest;
                break;
            }
        }
    }
    return fill;
}

// The pixels a bar of size tiles moves going from value to value - delta
static s32 BtlvGauge_DotDistance(s32 value, s32 delta, s32 max, u8 size) {
    s32 target;
    u8 dots = size * 8;
    s8 now;
    s8 next;
    s8 diff;

    target = value - delta;
    if (target < 0) {
        target = 0;
    } else if (target > max) {
        target = max;
    }
#ifdef BWBATTLE
    // bwbattle: see BtlvGauge_CalcBarTiles
    if (max <= 0) {
        return 0;
    }
#endif
    now = value * dots / max;
    next = target * dots / max;
    diff = now - next;
    return MATH_ABS(diff);
}

static void BtlvGauge_DrawName(BtlvGauge *gauge, BtlvGaugeEntry *entry, PartyPkm *pkm) {
    StrBuf *name = GFL_StrBufCreate(12, HEAPID_TAIL(gauge->heapId));
    GFLBitmap *bitmap = GFL_BitmapCreate(8, 2, 0x20, HEAPID_TAIL(gauge->heapId));
    u8 letter;
    u8 shadow;
    u8 background;
    NNSG2dImageProxy proxy;
    u8 *pixels;
    void *vram = G2_GetOBJCharPtr();

    GFL_BitmapFill(bitmap, 0);
    PokeParty_GetParam(pkm, PKM_PARAM_NICKNAME, name);
    GFL_TextRndGetGlobalColors(&letter, &shadow, &background);
    GFL_TextRndUpdateColorIndexLUT(1, 4, 0);
    if ((s32)GFL_FontGetBlockWidth(name, gauge->font, 0) > 48) {
        GFL_TextRendererDrawToBitmap(bitmap, 2, 5, name, gauge->font);
    } else {
        GFL_TextRendererDrawToBitmap(bitmap, 8, 5, name, gauge->font);
    }
    GFL_TextRndUpdateColorIndexLUT(letter, shadow, background);
    if (entry->nameActor != NULL) {
        pixels = GFL_BitmapGetPixelData(bitmap);
        func_0204c40c(entry->nameActor, &proxy);
        if (entry->mon.side) {
            MI_CpuCopy16(
                pixels, (void *)((uintptr_t)vram + 0x40 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]), 0xc0);
            MI_CpuCopy16(pixels + 0x100,
                         (void *)((uintptr_t)vram + 0x140 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0xc0);
            MI_CpuCopy16(pixels + 0xc0,
                         (void *)((uintptr_t)vram + 0x400 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x40);
            MI_CpuCopy16(pixels + 0x1c0,
                         (void *)((uintptr_t)vram + 0x500 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x40);
        } else {
            MI_CpuCopy16(
                pixels, (void *)((uintptr_t)vram + 0x20 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]), 0xe0);
            MI_CpuCopy16(pixels + 0x100,
                         (void *)((uintptr_t)vram + 0x120 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0xe0);
            MI_CpuCopy16(pixels + 0xe0,
                         (void *)((uintptr_t)vram + 0x400 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
            MI_CpuCopy16(pixels + 0x1e0,
                         (void *)((uintptr_t)vram + 0x500 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
        }
    }
    GFL_StrBufFree(name);
    GFL_BitmapFree(bitmap);
}

static void BtlvGauge_DrawSex(BtlvGauge *gauge, BtlvGaugeEntry *entry) {
    NNSG2dImageProxy proxy;
    void *vram = G2_GetOBJCharPtr();
    u32 top = entry->mon.sex * 0x40 + 0x380;
    u32 bottom = entry->mon.sex * 0x40 + 0x3a0;

    if (entry->mon.sex == 2) {
        top = GAUGE_TILE_BLANK;
        bottom = GAUGE_TILE_BLANK;
    }
    if (entry->nameActor != NULL) {
        func_0204c40c(entry->nameActor, &proxy);
        if (entry->mon.side) {
            MI_CpuCopy16(gauge->chars + top,
                         (void *)((uintptr_t)vram + 0x440 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
            MI_CpuCopy16(gauge->chars + bottom,
                         (void *)((uintptr_t)vram + 0x540 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
        } else {
            MI_CpuCopy16(gauge->chars + top,
                         (void *)((uintptr_t)vram + 0x420 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
            MI_CpuCopy16(gauge->chars + bottom,
                         (void *)((uintptr_t)vram + 0x520 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
        }
    }
}

// bar 0 is the HP bar, 1 the EXP bar
static void BtlvGauge_DrawBar(BtlvGauge *gauge, int index, int bar) {
    BtlvGaugeEntry *entry = &gauge->entries[index];
    u8 dots;
    u8 tiles[10];
    NNSG2dImageProxy proxy;
    u32 color;
    s32 hp;
    u8 i;
    void *vram = G2_GetOBJCharPtr();

    if (entry->hpBarActor == NULL) {
        return;
    }
    switch (bar) {
    case 0:
        if (gauge->showNumbers && !(index & 1)) {
            return;
        }
        func_0204c40c(entry->hpBarActor, &proxy);
        dots =
            BtlvGauge_CalcBarTiles(entry->mon.maxHp, entry->mon.hp, entry->mon.hpDelta, &entry->mon.hpAnim, tiles, 6);
        hp = entry->mon.hpAnim;
        if (entry->mon.maxHp < 48) {
            hp >>= 8;
        }
        switch (HPGauge_GetColor(hp, entry->mon.maxHp)) {
        case HP_GAUGE_COLOR_GREEN:
            color = 0;
            break;
        case HP_GAUGE_COLOR_YELLOW:
            color = 0x120;
            break;
        case HP_GAUGE_COLOR_RED:
        default:
            color = 0x240;
            break;
        }
        for (i = 0; i < 6; i++) {
            MI_CpuCopy16(
                gauge->chars + (color + tiles[i] * 0x20),
                (void *)((uintptr_t)vram + (i + 1) * 0x20 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                0x20);
        }
        if (entry->mon.hpDelta > 0) {
            BtlvGauge_DrawDamage(gauge, entry, dots);
        }
        break;
    case 1:
        if (entry->expActor == NULL) {
            return;
        }
        func_0204c40c(entry->expActor, &proxy);
        BtlvGauge_CalcBarTiles(entry->mon.expMax, entry->mon.exp, entry->mon.expDelta, &entry->mon.expAnim, tiles, 10);
        for (i = 0; i < 10; i++) {
            MI_CpuCopy16(
                gauge->chars + (tiles[i] * 0x20 + 0x400),
                (void *)((uintptr_t)vram + (i + 1) * 0x20 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                0x20);
        }
        break;
    }
}

static void BtlvGauge_DrawHpNumbers(BtlvGauge *gauge, BtlvGaugeEntry *entry, s32 hp) {
    u16 hpTiles[3];
    u16 maxTiles[3];
    NNSG2dImageProxy proxy;
    int i;
    s32 maxHp;
    int n;
    void *vram = G2_GetOBJCharPtr();

    if (entry->hpNumActor == NULL) {
        return;
    }
    func_0204c40c(entry->hpNumActor, &proxy);
    for (i = 0; i < 3; i++) {
        hpTiles[i] = GAUGE_TILE_BLANK;
        maxTiles[i] = GAUGE_TILE_BLANK;
    }
    if (hp >= 100) {
        hpTiles[0] = hp / 100 * 0x20 + GAUGE_TILE_DIGITS;
        hp %= 100;
    }
    if (hp >= 10 || hpTiles[0] != GAUGE_TILE_BLANK) {
        hpTiles[1] = hp / 10 * 0x20 + GAUGE_TILE_DIGITS;
        hp %= 10;
    }
    hpTiles[2] = hp * 0x20 + GAUGE_TILE_DIGITS;
    maxHp = entry->mon.maxHp;
    n = 0;
    if (maxHp >= 100) {
        maxTiles[n] = maxHp / 100 * 0x20 + GAUGE_TILE_DIGITS;
        maxHp %= 100;
        n++;
    }
    if (maxHp >= 10 || n != 0) {
        maxTiles[n] = maxHp / 10 * 0x20 + GAUGE_TILE_DIGITS;
        maxHp %= 10;
        n++;
    }
    maxTiles[n] = maxHp * 0x20 + GAUGE_TILE_DIGITS;
    for (i = 0; i < 3; i++) {
        MI_CpuCopy16(gauge->chars + hpTiles[i],
                     (void *)((uintptr_t)vram + i * 0x20 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                     0x20);
        MI_CpuCopy16(gauge->chars + maxTiles[i],
                     (void *)((uintptr_t)vram + (i + 4) * 0x20 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                     0x20);
    }
}

static void BtlvGauge_DrawLevel(BtlvGauge *gauge, BtlvGaugeEntry *entry) {
    StrBuf *str = GFL_StrBufCreate(4, HEAPID_TAIL(gauge->heapId));
    GFLBitmap *bitmap = GFL_BitmapCreate(3, 2, 0x20, HEAPID_TAIL(gauge->heapId));
    u8 letter;
    u8 shadow;
    u8 background;
    NNSG2dImageProxy proxy;
    u8 *pixels;
    void *vram = G2_GetOBJCharPtr();

    GFL_BitmapFill(bitmap, 0);
    if (entry->nameActor != NULL) {
        GFL_WordSetFormatNumber(str, entry->mon.level, 3, 0, FALSE);
        GFL_TextRndGetGlobalColors(&letter, &shadow, &background);
        GFL_TextRndUpdateColorIndexLUT(1, 4, 0);
        GFL_TextRendererDrawToBitmap(bitmap, 0, 5, str, gauge->font);
        GFL_TextRndUpdateColorIndexLUT(letter, shadow, background);
        pixels = GFL_BitmapGetPixelData(bitmap);
        func_0204c40c(entry->nameActor, &proxy);
        if (entry->mon.side) {
            MI_CpuCopy16(pixels,
                         (void *)((uintptr_t)vram + 0x480 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x60);
            MI_CpuCopy16(pixels + 0x60,
                         (void *)((uintptr_t)vram + 0x580 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x60);
        } else {
            MI_CpuCopy16(pixels,
                         (void *)((uintptr_t)vram + 0x460 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x60);
            MI_CpuCopy16(pixels + 0x60,
                         (void *)((uintptr_t)vram + 0x560 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x60);
        }
    }
    GFL_StrBufFree(str);
    GFL_BitmapFree(bitmap);
}

static void BtlvGauge_DrawCaughtMark(BtlvGauge *gauge, BtlvGaugeEntry *entry) {
    NNSG2dImageProxy proxy;
    void *vram = G2_GetOBJCharPtr();
    u32 tile = 0x360;

    if (entry->nameActor != NULL) {
        if (!entry->mon.caught) {
            tile = GAUGE_TILE_BLANK;
        }
        func_0204c40c(entry->nameActor, &proxy);
        MI_CpuCopy16(gauge->chars + tile,
                     (void *)((uintptr_t)vram + 0x120 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]), 0x20);
    }
}

// The HP bar's lost part, from the bar's dots before the hit down to dots
static void BtlvGauge_DrawDamage(BtlvGauge *gauge, BtlvGaugeEntry *entry, s32 dots) {
    u8 tiles[6];
    NNSG2dImageProxy proxy;
    s32 lost = entry->mon.hpDots - dots;
    int i;
    void *vram = G2_GetOBJCharPtr();

    if (entry->damageActor != NULL) {
        for (i = 0; i < 6; i++) {
            tiles[i] = 8;
        }
        for (i = 5; i >= 0; i--) {
            if (lost >= 8) {
                tiles[i] = 0;
                lost -= 8;
            } else {
                tiles[i] = 8 - lost;
                break;
            }
        }
        func_0204c40c(entry->damageActor, &proxy);
        for (i = 0; i < 6; i++) {
            MI_CpuCopy16(gauge->chars + (tiles[i] * 0x20 + 0x6a0),
                         (void *)((uintptr_t)vram + i * 0x20 + proxy.vramLocation.baseAddrOfVram[NNS_G2D_VRAM_TYPE_2DMAIN]),
                         0x20);
        }
    }
}

static void BtlvGauge_UpdateLevelUp(BtlvGauge *gauge, int index) {
    BtlvGaugeEntry *entry = &gauge->entries[index];

    if (!entry->mon.expMoving) {
        switch (entry->mon.levelUpSeq) {
        case 0:
            GFL_SndSEPlay(SEQ_SE_EXPMAX);
            entry->mon.levelUpSeq++;
            break;
        case 1:
            entry->mon.levelUpSeq++;
            break;
        case 2:
            entry->mon.exp = 0;
            entry->mon.expMax = entry->mon.nextExpMax;
            entry->mon.expAnim = GAUGE_ANIM_START;
            entry->mon.expDelta = 0;
            BtlvGauge_StepGauge(entry->mon.maxHp, entry->mon.hp, 0, &entry->mon.hpAnim, 6, 1);
            BtlvGauge_StepGauge(entry->mon.expMax, entry->mon.exp, 0, &entry->mon.expAnim, 10, 1);
            BtlvGauge_DrawBar(gauge, index, 0);
            BtlvGauge_DrawBar(gauge, index, 1);
            BtlvGauge_DrawHpNumbers(gauge, entry, entry->mon.hp);
            BtlvGauge_DrawLevel(gauge, entry);
            entry->mon.levelUp = FALSE;
            break;
        }
    }
}

static void BtlvGauge_UpdateShake(BtlvGauge *gauge, int index) {
    ClActorPos offset;

    offset.x = -16;
    offset.y = FX_SinIdx(gauge->shakeAngle) / 0x800;
    gauge->shakeAngle += 0x400;
    BtlvGauge_SetPos(gauge, index, &offset);
}

void BtlvGauge_SetStatus(BtlvGauge *gauge, u32 status, int index) {
    const u32 sequences[7] = { GAUGE_STATUS_NONE, 1, 3, 2, 5, 4, 7 };

    if (gauge->mode == 2) {
        gauge->entries[index].mon.status = sequences[status];
        return;
    }
    if (gauge->entries[index].statusActor != NULL) {
        if (status == 0) {
            func_0204c124(gauge->entries[index].statusActor, FALSE);
            func_0204c488(gauge->entries[index].statusActor, 0);
        } else {
            func_0204c124(gauge->entries[index].statusActor, TRUE);
            func_0204c488(gauge->entries[index].statusActor, sequences[status]);
        }
    }
}

void BtlvGauge_StartShake(BtlvGauge *gauge, int index) {
    int i;

    for (i = 0; i < 8; i++) {
        if (gauge->entries[i].nameActor != NULL && gauge->entries[i].mon.shaking) {
            ClActorPos pos = { -16, 0 };

            gauge->entries[i].mon.shaking = FALSE;
            BtlvGauge_SetPos(gauge, i, &pos);
        }
    }
    if (index != 8) {
        gauge->entries[index].mon.shaking = TRUE;
        gauge->shakeAngle = 0;
    }
}

BOOL BtlvGauge_IsPinch(BtlvGauge *gauge) {
    return gauge->pinch;
}

void BtlvGauge_SetPinch(BtlvGauge *gauge, BOOL pinch) {
    gauge->pinch = pinch;
}

void BtlvGauge_SetBgm(BtlvGauge *gauge, u32 bgm) {
    gauge->bgm = bgm;
}

void BtlvGauge_SetBgmReplayed(BtlvGauge *gauge, BOOL replayed) {
    gauge->bgmReplayed = replayed;
}

void BtlvGauge_SetNoPinchBgm(BtlvGauge *gauge, BOOL noPinchBgm) {
    gauge->noPinchBgm = noPinchBgm;
}

BOOL BtlvGauge_GetStatus(BtlvGauge *gauge, int index, u32 *color, u32 *status) {
    if (gauge->mode == 2 && gauge->entries[index].mon.shown) {
        BtlvGauge_ReadStatus(gauge, index, color, status, FALSE);
        return TRUE;
    }
    if (gauge->entries[index].nameActor == NULL || !gauge->entries[index].mon.shown ||
        gauge->entries[index].mon.statusHidden) {
        *color = 0;
        *status = GAUGE_STATUS_NONE;
        return FALSE;
    }
    BtlvGauge_ReadStatus(gauge, index, color, status, TRUE);
    return TRUE;
}

static void BtlvGauge_ReadStatus(BtlvGauge *gauge, int index, u32 *color, u32 *status, BOOL fromActor) {
    if (fromActor == FALSE) {
        *status = gauge->entries[index].mon.status;
    } else {
        *status = func_0204c4a0(gauge->entries[index].statusActor);
    }
    *color = HPGauge_GetColor(gauge->entries[index].mon.hp, gauge->entries[index].mon.maxHp);
    if (*status == 0) {
        *status = GAUGE_STATUS_NONE;
    }
}

static void BtlvGauge_InitStatusOnly(BtlvGauge *gauge) {
    gauge->entries[0].mon.status = GAUGE_STATUS_NONE;
    gauge->entries[1].mon.status = GAUGE_STATUS_NONE;
}

void BtlvGauge_RequestNumberToggle(BtlvGauge *gauge) {
    gauge->toggleRequest = TRUE;
}

void BtlvGauge_HideStatus(BtlvGauge *gauge, int index) {
    if (gauge->entries[index].nameActor != NULL) {
        gauge->entries[index].mon.statusHidden = TRUE;
    }
}

static void BtlvGauge_UpdatePinchBgm(BtlvGauge *gauge) {
    if (gauge->bgmFading) {
        if (GFL_SndBGMIsFading()) {
            return;
        }
        gauge->bgmFading = FALSE;
        if (gauge->pinch) {
            GFL_SndBGMPop();
            if (!gauge->bgmReplayed && BtlvEffect_IsBgmChanged()) {
                GFL_SndBGMPlay(gauge->bgm, 0xffff);
                gauge->bgmReplayed = TRUE;
            } else {
                BtlvGauge_PauseBgm(FALSE);
            }
            GFL_SndBGMFadeIn(24);
        } else {
            BtlvGauge_PauseBgm(TRUE);
            GFL_SndBGMPush();
            GFL_SndBGMPlay(SEQ_BGM_BATTLEPINCH, 0xffff);
        }
        gauge->pinch ^= 1;
    } else {
        BOOL busy = FALSE;
        BOOL low = FALSE;
        int i;

        for (i = 0; i < 8; i++) {
            if (gauge->entries[i].mon.hpMoving || gauge->entries[i].mon.expMoving || gauge->entries[i].mon.levelUp) {
                busy = TRUE;
            } else if (gauge->entries[i].mon.side == 0 && gauge->entries[i].mon.shown &&
                       gauge->entries[i].mon.type != 4 &&
                       HPGauge_GetColor(gauge->entries[i].mon.hp, gauge->entries[i].mon.maxHp) == HP_GAUGE_COLOR_RED) {
                low = TRUE;
            }
        }
        if (gauge->pinch || GFL_SndBGMGetID() == gauge->bgm) {
            if (!busy && ((gauge->pinch && !low) || (!gauge->pinch && low == TRUE))) {
                gauge->bgmFading = TRUE;
                GFL_SndBGMFadeOut(8);
            }
        }
    }
}

static void BtlvGauge_SlideInTask(TCB *tcb, void *data) {
    BtlvGaugeEntry *entry = data;
    ClActorPos pos;
    int dx;

    if (entry->mon.side) {
        dx = 8;
    } else {
        dx = -8;
    }
    if (entry->nameActor != NULL) {
        func_0204c178(entry->nameActor, &pos, CLACT_SURFACE_MAIN);
        pos.x += dx;
        func_0204c140(entry->nameActor, &pos, CLACT_SURFACE_MAIN);
        func_0204c178(entry->hpBarActor, &pos, CLACT_SURFACE_MAIN);
        pos.x += dx;
        func_0204c140(entry->hpBarActor, &pos, CLACT_SURFACE_MAIN);
        func_0204c178(entry->statusActor, &pos, CLACT_SURFACE_MAIN);
        pos.x += dx;
        func_0204c140(entry->statusActor, &pos, CLACT_SURFACE_MAIN);
        if (!entry->mon.side) {
            func_0204c178(entry->hpNumActor, &pos, CLACT_SURFACE_MAIN);
            pos.x += dx;
            func_0204c140(entry->hpNumActor, &pos, CLACT_SURFACE_MAIN);
        }
    }
    if (entry->expActor != NULL) {
        func_0204c178(entry->expActor, &pos, CLACT_SURFACE_MAIN);
        pos.x += dx;
        func_0204c140(entry->expActor, &pos, CLACT_SURFACE_MAIN);
    }
    entry->mon.slideFrames--;
    if (entry->mon.slideFrames == 0) {
        GFL_TCBRemove(tcb);
        entry->mon.slideTask = NULL;
    }
}

static void BtlvGauge_FlashTask(TCB *tcb, void *data) {
    BtlvGaugeFlashTask *work = data;
    const u32 masks[8] = { 1 << 0, 1 << 1, 1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5 };
    int i;

#ifdef BWBATTLE
    // HP samples do not run BW's drain task. Latch one flash per hit instead of
    // relying on hpMoving, and blend on the ambient clock in both pacing modes.
    if (!BtlvEffect_ShouldTickIdle()
        || (PaletteFade_GetActiveMask(BtlvEffect_GetPaletteFade()) & (1 << PALFADE_BUFFER_MAIN_OBJ))) {
        return;
    }
    if (work->seq == 0) {
        work->paletteMask = 0;
        for (i = 0; i < 8; i++) {
            BtlvGaugeMon *mon = &work->gauge->entries[i].mon;
            if (mon->hpSyncFlash || (mon->hpMoving && mon->hpDelta > 0)) {
                work->paletteMask |= masks[i];
                mon->hpSyncFlash = FALSE;
            }
        }
        if (work->paletteMask == 0) {
            return;
        }
        work->count = 0;
        work->seq = 1;
    }
    if (work->seq == 1) {
        // Three red pulses, each eight 60 Hz ticks, including a return to the original palette.
        u32 phase = (work->count + 1) % 8;
        u8 fraction = phase <= 4 ? phase * 2 : (8 - phase) * 2;
        PaletteFade_BlendPalettes(BtlvEffect_GetPaletteFade(), PALFADE_BUFFER_MAIN_OBJ, work->paletteMask,
                                 fraction, 0x10);
        if (++work->count == 24) {
            work->seq = 2;
        }
    } else {
        for (i = 0; i < 8; i++) {
            if (work->gauge->entries[i].mon.hpMoving) {
                return;
            }
        }
        work->seq = 0;
    }
#else
    switch (work->seq) {
    case 0:
        work->paletteMask = 0;
        for (i = 0; i < 8; i++) {
            if (work->gauge->entries[i].mon.hpMoving && work->gauge->entries[i].mon.hpDelta > 0) {
                work->paletteMask |= masks[i];
            }
        }
        if (work->paletteMask != 0) {
            PaletteFade_StartFade(BtlvEffect_GetPaletteFade(), 1 << PALFADE_BUFFER_MAIN_OBJ, work->paletteMask, 0, 0, 8,
                                  0x10, BtlvEffect_GetTCBManager());
            work->seq = 1;
        }
        break;
    case 1:
        if (PaletteFade_GetActiveMask(BtlvEffect_GetPaletteFade()) == 0) {
            PaletteFade_StartFade(BtlvEffect_GetPaletteFade(), 1 << PALFADE_BUFFER_MAIN_OBJ, work->paletteMask, 0, 8, 0,
                                  0x10, BtlvEffect_GetTCBManager());
            work->seq = 2;
        }
        break;
    case 2:
        if (PaletteFade_GetActiveMask(BtlvEffect_GetPaletteFade()) == 0) {
            if (++work->count == 3) {
                work->count = 0;
                work->seq = 3;
            } else {
                work->seq = 0;
            }
        }
        break;
    case 3:
        for (i = 0; i < 8; i++) {
            if (work->gauge->entries[i].mon.hpMoving) {
                break;
            }
        }
        if (i == 8) {
            work->seq = 0;
        }
        break;
    }
#endif
}

static void BtlvGauge_PauseBgm(BOOL paused) {
    if (func_02011844() != TRUE) {
        GFL_SndBGMSetPaused(paused);
    }
}

static u32 BtlvGauge_CalcFill(u32 value, u32 max, u32 width) {
#ifdef BWBATTLE
    // bwbattle: see BtlvGauge_CalcBarTiles
    if (max == 0) {
        return 0;
    }
#endif
    u32 fill = value * width / max;

    if (fill == 0 && value != 0) {
        fill = 1;
    }
    return fill;
}

static void BtlvGauge_ShowHpParts(BtlvGauge *gauge, int index, BOOL visible) {
    if ((gauge->entries[index].mon.type == BTL_STYLE_TRIPLE || gauge->entries[index].mon.type == BTL_STYLE_ROTATION) &&
        visible == TRUE && !(index & 1)) {
        if (gauge->entries[index].hpNumActor != NULL) {
            func_0204c124(gauge->entries[index].hpNumActor, gauge->showNumbers);
            func_0204c124(gauge->entries[index].hpBarActor, !gauge->showNumbers);
        }
    } else if (gauge->entries[index].hpNumActor != NULL) {
        func_0204c124(gauge->entries[index].hpNumActor, visible);
        func_0204c124(gauge->entries[index].hpBarActor, visible);
#ifdef BWBATTLE
    } else if (gauge->entries[index].hpBarActor != NULL) {
        // bwbattle: an opponent's gauge has no numbers, and its bar stayed when the gauge was hidden
        func_0204c124(gauge->entries[index].hpBarActor, visible);
#endif
    }
}
