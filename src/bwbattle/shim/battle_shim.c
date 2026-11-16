#include "bwbattle.h"

#include <string.h>

#include "types.h"
#include "battle/btl_main.h"
#include "battle/btl_pokeparam.h"
#include "battle/btl_setup.h"
#include "battle/btl_string.h"
#include "battle/btlv.h"
#include "battle/btlv_timer.h"
#include "battle/trainer_data.h"
#include "constants/battle.h"
#include "constants/arc.h"
#include "constants/pokemon.h"
#include "gfl/arc.h"
#include "gfl/heap.h"
#include "field/field_light_status.h"
#include "gfl/str.h"
#include "pml/personal.h"
#include "pml/poke_party.h"
#include "save/player_info.h"
#include "save/pokedex.h"
#include "shim/bw_shim.h"

// BW2's battle state as the view reads it (overlay 167's btl_main.c and btl_pokeparam.c), from what the Platinum side
// sets with BwBattle_SetSetup and the BwBattleMon records it hands the view.
//
// The view only reads these through functions, so its handles point at bwbattle's own records: a BtlMainModule * is
// sState, and a BattleMon *, PartyPkm * or BoxPkm * is a BwBattleMon *. The trainers' PlayerInfo * is the client ID plus
// one, never dereferenced.

typedef struct {
    BwBattleSetup setup;
    BtlFieldSituation field;
} BwShimBattleState;

static BwShimBattleState sState;

#define MAIN(mainModule) ((BwShimBattleState *)(mainModule))
#define MON(mon) ((const BwBattleMon *)(mon))

void BwBattle_SetSetup(const BwBattleSetup *setup) {
    sState.setup = *setup;
    memset(&sState.field, 0, sizeof(sState.field));
    sState.field.env.bgType = setup->bgType;
    sState.field.env.terrain = setup->terrain;
    sState.field.env.weather = setup->weather;
    sState.field.env.season = setup->season;
    sState.field.env.zoneId = setup->zoneId;
    sState.field.env.hour = setup->hour;
    sState.field.env.minute = setup->minute;
    sState.field.unk1a = setup->multi;
}

BtlMainModule *BwShim_GetMainModule(void) {
    return (BtlMainModule *)&sState;
}

// btl_main.c

u32 BtlSetup_GetBattleStyle(BtlMainModule *mainModule) {
    return MAIN(mainModule)->setup.battleStyle;
}

u32 BtlSetup_GetBattleType(BtlMainModule *mainModule) {
    return MAIN(mainModule)->setup.battleType;
}

u32 BtlSetup_IsBattleType(BtlMainModule *mainModule, u32 flag) {
    return (MAIN(mainModule)->setup.typeFlags & flag) != 0;
}

BtlFieldSituation *GetFieldEffectData(BtlMainModule *mainModule) {
    return &MAIN(mainModule)->field;
}

u8 GetPlayerClientID(BtlMainModule *mainModule) {
    return MAIN(mainModule)->setup.playerClientId;
}

// A multi battle
BOOL func_ov167_0219beec(BtlMainModule *mainModule) {
    return MAIN(mainModule)->setup.multi;
}

BOOL func_ov167_0219bf70(BtlMainModule *mainModule, BattleMon *mon) {
    (void)mainModule;
    return BwPlat_IsCaught(MON(mon)->species);
}

// The player's side
u8 func_ov167_0219c850(BtlMainModule *mainModule) {
    return MAIN(mainModule)->setup.playerSide;
}

// The player's partner's client
u8 func_ov167_0219c86c(BtlMainModule *mainModule) {
    return MAIN(mainModule)->setup.partnerClientId;
}

// An opponent's client, the first or the second
u8 func_ov167_0219c8b8(BtlMainModule *mainModule, u8 other) {
    return MAIN(mainModule)->setup.enemyClientIds[other & 1];
}

// The Pokéstar Studios mode and its parameter, which are BW2's movie shoots; the view's studio displays stay off
u32 func_ov167_0219c988(BtlMainModule *mainModule) {
    (void)mainModule;
    return 0;
}

u32 func_ov167_0219c99c(BtlMainModule *mainModule) {
    (void)mainModule;
    return 0;
}

u8 func_ov167_0219c9b0(BtlMainModule *mainModule) {
    (void)mainModule;
    return 0;
}

BtlScriptedRules *func_ov167_0219e39c(BtlMainModule *mainModule) {
    (void)mainModule;
    return NULL;
}

// Chatot's recorded cry of a position's client
BOOL func_ov167_0219d228(BtlMainModule *mainModule, u32 index, void **out) {
    (void)mainModule;
    (void)index;
    (void)out;
    return FALSE;
}

// A client's trainer class
u32 func_ov167_0219d938(BtlMainModule *mainModule, u8 clientId) {
    return MAIN(mainModule)->setup.trainerClasses[clientId & 3];
}

PlayerInfo *func_ov167_0219d97c(BtlMainModule *mainModule, u8 clientId) {
    (void)mainModule;
    return (PlayerInfo *)(uintptr_t)(clientId + 1);
}

u32 getTrainerGender(PlayerInfo *info) {
    if ((uintptr_t)info == sState.setup.playerClientId + 1u) {
        return sState.setup.playerFemale;
    }
    return 0;
}

// Whether the battle can be run from and whether the camera may idle (BtlvEffect_*): BW2 answers by battle mode, and
// for its wild and trainer battles both are TRUE
BOOL func_ov167_0219db08(BtlMainModule *mainModule) {
    (void)mainModule;
    return FALSE;
}

BOOL func_ov167_0219db28(BtlMainModule *mainModule) {
    switch (BtlSetup_GetBattleType(mainModule)) {
    case 0:
    case 1:
        return TRUE;
    default:
        return FALSE;
    }
}

// btl_pokeparam.c

u16 GetBattleMonSpecies(const BattleMon *mon) {
    return MON(mon)->species;
}

u32 GetBattleMonStat(const BattleMon *mon, u32 stat) {
    switch (stat) {
    case BATTLEMON_HP:
        return MON(mon)->hp;
    case 14: // The maximum HP
        return MON(mon)->maxHp;
    case BATTLEMON_LEVEL:
        return MON(mon)->level;
    case BATTLEMON_GENDER:
        return MON(mon)->sex;
    case BATTLEMON_FORM:
        return MON(mon)->form;
    case 20: // The experience points
        return MON(mon)->exp;
    default:
        BwShim_Log("GetBattleMonStat: stat %u", stat);
        return 0;
    }
}

u32 GetBattleMonStatus(BattleMon *mon) {
    return MON(mon)->status;
}

BattleConditionCont GetConditionContinuationParam(BattleMon *mon, u32 index) {
    BattleConditionCont cont;

    memset(&cont, 0, sizeof(cont));
    if (index == CONDITION_POISON && MON(mon)->badlyPoisoned) {
        cont.common.type = 1;
        cont.common.turns = 15;
    }
    return cont;
}

BOOL Condition_IsBadlyPoisoned(BattleConditionCont cont) {
    return cont.common.type == 1 && cont.common.turns == 15;
}

PartyPkm *func_ov167_021bb064(const BattleMon *mon) {
    return (PartyPkm *)mon;
}

// The party Pokémon (system/poke_tool.c)

u32 PokeParty_GetParam(const PartyPkm *pkm, PkmField param, void *buffer) {
    const BwBattleMon *mon = MON(pkm);

    // The PKM_PARAM_* values are defines, not members of PkmField
    switch ((u32)param) {
    case PKM_PARAM_PID:
        return mon->personality;
    case PKM_PARAM_SPECIES:
        return mon->species;
    case PKM_PARAM_FORM:
        return mon->form;
    case PKM_PARAM_EXP:
        return mon->exp;
    case PKM_PARAM_LEVEL:
        return mon->level;
    case PKM_PARAM_HP:
        return mon->hp;
    case PKM_PARAM_MAX_HP:
        return mon->maxHp;
    case PKM_PARAM_IS_EGG:
        return mon->isEgg;
    case PKM_PARAM_POKEBALL:
        return mon->ball;
    case PKM_PARAM_SEX:
        return mon->sex;
    case PKM_PARAM_SHOW_SEX:
        return mon->showSex;
    case PKM_PARAM_STATUS:
        return mon->status;
    case PKM_PARAM_N_POKEMON:
    case PKM_PARAM_POKESTAR_FAME:
        return 0;
    case PKM_PARAM_NICKNAME:
        if (buffer != NULL && mon->nickname != NULL) {
            GFL_StrBufLoadString(buffer, mon->nickname);
        }
        return 0;
    default:
        BwShim_Log("PokeParty_GetParam: param %u", param);
        return 0;
    }
}

u32 PML_PkmGetParam(BoxPkm *pkm, u32 param, void *buffer) {
    return PokeParty_GetParam((const PartyPkm *)pkm, param, buffer);
}

BoxPkm *func_0201d624(PartyPkm *pkm) {
    return (BoxPkm *)pkm;
}

BOOL PokeParty_IsRare(PartyPkm *pkm) {
    return MON(pkm)->shiny;
}

BOOL PML_PkmIsRare(BoxPkm *pkm) {
    return MON(pkm)->shiny;
}

u8 PokeParty_GetSex(PartyPkm *pkm) {
    return MON(pkm)->sex;
}

u32 GetStatusCond(PartyPkm *pkm) {
    return MON(pkm)->status;
}

int PML_GetPokestarFameLevel(u8 fame) {
    (void)fame;
    return 0;
}

BOOL PokeDex_IsCaught(PokeDexSave *pokedex, u16 species) {
    (void)pokedex;
    return BwPlat_IsCaught(species);
}

u32 PML_UtilGetPkmLvExp(u16 species, u16 form, u16 level) {
    return BwPlat_GetLevelExp(species, form, level > 100 ? 100 : (u8)level);
}

s32 PML_MoveGetCategory(u16 move) {
    return BwPlat_GetMoveCategory(move);
}

// Item numbers of the balls are the same in both games; BW2's Dream Ball is the only one Platinum lacks
u16 PML_ItemGetMonsBallID(u16 item) {
    return item <= 16 ? item : (item == 576 ? 25 : 0);
}

// BW2's personal data, extracted with the sprites: pokegra.c reads where a species' forms' sprites are in the
// sprite archive (system/poke_personal.c)
u32 PML_PersonalGetParamSingle(u16 species, u16 form, u32 param) {
    static BOOL sOpened;
    static u16 sSpecies = 0xffff, sForm;
    static PersonalData sData;

    if (!sOpened) {
        PML_PersonalArcInit(HEAPID_SYSTEM);
        sOpened = TRUE;
    }
    if (species != sSpecies || form != sForm) {
        PML_PersonalFetchBW2(species, form, &sData);
        sSpecies = species;
        sForm = form;
    }
    return PML_PersonalGetParam(&sData, param);
}

// The lights of the arena (system/field_light_status.c): BW2 takes them from the weather's battle lights, or else
// the zone's or its area's field lights, by time of day. Platinum's places have no BW2 zone, so outdoors is BW2's
// usual outdoor day (file 0 of the field lights), with BW2's spring hours: Platinum has no seasons
#define ARCID_LIGHTS_FIELD_DEFAULT 0

typedef struct {
    u16 period; // The day period whose first hour the setting ends in, and the minutes after it
    s16 minute;
    u8 unk04[4];
    GXRgb lightColors[4];
    VecFx16 lightDirs[4];
    GXRgb diffuse;
    GXRgb ambient;
    GXRgb specular;
    GXRgb emission;
    u8 unk30[4];
} BwFieldLightData;

static const u8 sBwLightChangeHours[] = { 5, 10, 17, 20, 0 };
static const u32 sBwWeatherLightIds[] = { 0xffff, 8, 7, 9, 8, 8, 6, 0, 8, 1, 4, 2, 9, 5, 3 };

void FieldLightStatus_Get(u32 zoneId, u32 hour, u32 minute, u32 weather, u32 season, GXRgb *colors, HeapID heapId) {
    BwFieldLightData *data;
    u32 arcId = ARCID_LIGHTS_FIELD;
    u32 fileId = ARCID_LIGHTS_FIELD_DEFAULT;
    u32 count;
    int seconds = minute * 60 + hour * 3600;
    int prev = 0;
    u32 i;

    (void)zoneId;
    (void)season;
    if (weather != 0 && weather < NELEMS(sBwWeatherLightIds) && sBwWeatherLightIds[weather] != 0xffff) {
        arcId = ARCID_LIGHTS_BATTLE;
        fileId = sBwWeatherLightIds[weather];
    }
    count = GFL_ArcSysGetDataLength(arcId, fileId) / sizeof(BwFieldLightData);
    data = GFL_ArcSysReadHeapNew(arcId, fileId, HEAPID_TAIL(heapId));
    for (i = 0; i < count; i++) {
        int end = sBwLightChangeHours[data[i].period < NELEMS(sBwLightChangeHours) ? data[i].period : 0] * 3600
            + data[i].minute * 60;

        if (prev <= seconds && end > seconds) {
            break;
        }
        prev = end;
    }
    {
        const BwFieldLightData *entry = &data[i % count];

        colors[0] = entry->lightColors[0];
        colors[1] = entry->lightColors[1];
        colors[2] = entry->lightColors[2];
        colors[3] = entry->lightColors[3];
        colors[4] = entry->diffuse;
        colors[5] = entry->ambient;
        colors[6] = entry->specular;
        colors[7] = entry->emission;
    }
    GFL_HeapFree(data);
}

// The battle's string of a message ID, for the effect actors' numbers (btl_string.c)
void func_ov167_021d4ec0(StrBuf *strbuf, u16 message, u32 count, ...) {
    (void)message;
    (void)count;
    BW_SHIM_TODO();
    GFL_StrBufClear(strbuf);
}

// The scene's message window, which an effect hides (btlv_scu.c). Platinum's textbox stays
void func_ov167_021d0ff8(const BtlvScu *scu) {
    (void)scu;
}

// BW2's battle timer for timed link battles; Platinum's battles have none
BtlvTimer *BtlvTimer_Create(HeapID heapId) {
    (void)heapId;
    return NULL;
}

void BtlvTimer_Delete(BtlvTimer *timer) {
    (void)timer;
}

void BtlvTimer_Start(BtlvTimer *timer, s32 battleLimit, s32 commandLimit) {
    (void)timer;
    (void)battleLimit;
    (void)commandLimit;
}

void BtlvTimer_SetVisible(BtlvTimer *timer, int which, BOOL visible, BOOL restart) {
    (void)timer;
    (void)which;
    (void)visible;
    (void)restart;
}

BOOL BtlvTimer_IsTimeUp(BtlvTimer *timer, int which) {
    (void)timer;
    (void)which;
    return FALSE;
}
