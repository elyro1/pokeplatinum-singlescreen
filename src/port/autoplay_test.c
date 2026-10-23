// Opt-in Linux test bridge. The controller owns progression; this file only
// observes state, supplies normal input, and installs the one allowed fixture.
#define _POSIX_C_SOURCE 200809L
#include <nitro.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The game's precompiled header includes libc before this translation unit's
// POSIX feature macro; declare these two POSIX interfaces explicitly here.
extern FILE *fdopen(int fd, const char *mode);
extern int fileno(FILE *stream);

#include "constants/charcode.h"
#include "constants/field/dynamic_map_features.h"

#include "battle/battle_context.h"
#include "battle/battle_lib.h"
#include "battle/battle_system.h"
#include "field/field_system.h"
#include "overlay008/gym_features.h"
#include "port/autoplay_test.h"
#include "port/debug_field.h"
#include "port/gameplay_config.h"
#include "savedata/save_table.h"

#include "bag.h"
#include "dynamic_map_features.h"
#include "map_header.h"
#include "map_object.h"
#include "party.h"
#include "pc_boxes.h"
#include "persisted_map_features.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata_misc.h"
#include "trainer_info.h"
#include "vars_flags.h"

extern void SIM_Autopilot_RequestShot(const char *path);

static FILE *sRead, *sWrite;
static unsigned sFrame, sMenuFrame, sBattleFrame;
static int sEnabled = -1, sTouchX = -1, sTouchY = -1, sFixture;
static char sText[1024], sMenu[4096], sBattle[4096];
static unsigned sTextSerial;
static long long sApp;
static int sAppState;
static int sBattles, sLosses;
static unsigned sUIFrame;
static char sUI[256];
static char sPartyOrder[64];
static unsigned sPartyOrderFrame;

void AutoplayTest_PartyOrder(const u8 *slots)
{
    if (sEnabled != 1) {
        return;
    }
    sPartyOrderFrame = sFrame;
    snprintf(sPartyOrder, sizeof(sPartyOrder), "[%u,%u,%u,%u,%u,%u]", slots[0], slots[1], slots[2], slots[3], slots[4], slots[5]);
}

void AutoplayTest_UI(const char *name, int state, int cursor, int detail)
{
    if (sEnabled != 1) {
        return;
    }
    sUIFrame = sFrame;
    snprintf(sUI, sizeof(sUI), "{\"name\":\"%s\",\"state\":%d,\"cursor\":%d,\"detail\":%d}", name, state, cursor, detail);
}

void AutoplayTest_Result(int result, int battleType)
{
    if (sEnabled != 1 || (battleType & BATTLE_TYPE_CATCH_TUTORIAL)) {
        return;
    }
    sBattles++;
    if (result == BATTLE_RESULT_LOSE || result == BATTLE_RESULT_DRAW) {
        sLosses++;
    }
}

void AutoplayTest_App(const ApplicationManager *app)
{
    if (sEnabled != 1) {
        return;
    }
    sApp = (long long)(uintptr_t)app->template.main - (long long)(uintptr_t)AutoplayTest_ReadPad;
    sAppState = app->procState;
}

static void Fail(const char *reason)
{
    fprintf(stderr, "[story-test] %s\n", reason);
    exit(2);
}

static void Decode(const String *text, char *out, size_t capacity)
{
    size_t n = 0;
    if (text != NULL && text != STRING_LIST_TERMINATOR) {
        for (unsigned i = 0; i < text->size && n + 1 < capacity; i++) {
            u16 c = text->data[i];
            char a = ' ';
            if (c >= CHAR_A && c <= CHAR_Z) {
                a = 'A' + c - CHAR_A;
            } else if (c >= CHAR_a && c <= CHAR_z) {
                a = 'a' + c - CHAR_a;
            } else if (c >= CHAR_0 && c <= CHAR_9) {
                a = '0' + c - CHAR_0;
            } else if (c == CHAR_EXCLAMATION) {
                a = '!';
            } else if (c == CHAR_QUESTION) {
                a = '?';
            } else if (c == CHAR_COMMA) {
                a = ',';
            } else if (c == CHAR_PERIOD) {
                a = '.';
            } else if (c == CHAR_EOS) {
                break;
            }
            // Whitespace also safely replaces format codes and punctuation.
            if (a != ' ' || n == 0 || out[n - 1] != ' ') {
                out[n++] = a;
            }
        }
    }
    out[n] = 0;
}

void AutoplayTest_Text(const String *text)
{
    if (sEnabled != 1) {
        return;
    }
    char decoded[1024];
    Decode(text, decoded, sizeof(decoded));
    // Exclude short HUD labels, which would hide the current dialogue.
    if (strlen(decoded) >= 20 && strcmp(decoded, sText) != 0) {
        strcpy(sText, decoded);
        sTextSerial++;
    }
}

void AutoplayTest_Menu(int kind, int cursor, int count, const StringList *choices)
{
    if (sEnabled != 1) {
        return;
    }
    sMenuFrame = sFrame;
    int n = snprintf(sMenu, sizeof(sMenu), "{\"kind\":%d,\"cursor\":%d,\"choices\":[", kind, cursor);
    for (int i = 0; i < count && i < 16; i++) {
        char label[128];
        Decode(choices[i].entry, label, sizeof(label));
        n += snprintf(sMenu + n, sizeof(sMenu) - n, "%s\"%s\"", i ? "," : "", label);
    }
    snprintf(sMenu + n, sizeof(sMenu) - n, "]}");
}

void AutoplayTest_Battle(BattleSubscreen *subscreen)
{
    if (sEnabled != 1) {
        return;
    }
    sBattleFrame = sFrame;
    BattleSystem *sys = subscreen->battleSys;
    BattleContext *ctx = BattleSystem_GetBattleContext(sys);
    int attacker = BattleSystem_GetBattlerOfType(sys, subscreen->battlerType);
    int defender = attacker ^ 1;
    for (int i = 0; i < BattleSystem_GetMaxBattlers(sys); i++) {
        if (BattleSystem_GetBattlerSide(sys, i) != BattleSystem_GetBattlerSide(sys, attacker)
            && ctx->battleMons[i].curHP > 0) {
            defender = i;
            break;
        }
    }
    BattleMon *a = &ctx->battleMons[attacker], *d = &ctx->battleMons[defender];
    u8 invalid = BattleSystem_CheckInvalidMoves(sys, ctx, attacker, 0, CHECK_INVALID_ALL);
    int n = snprintf(sBattle, sizeof(sBattle), "{\"menu\":%d,\"cursor\":[%d,%d,%d],\"type\":%u,\"invalid\":%u,"
                                               "\"attacker\":[%u,%d,%u,%u,%u,%u],\"defender\":[%u,%d,%u,%u,%u,%u],\"moves\":[",
        subscreen->activeMenuConfigIndex,
        subscreen->cursor.x,
        subscreen->cursor.y,
        subscreen->cursor.isActive,
        BattleSystem_GetBattleType(sys),
        invalid,
        a->level,
        a->curHP,
        a->attack,
        a->spAttack,
        a->type1,
        a->type2,
        d->species,
        d->curHP,
        d->defense,
        d->spDefense,
        d->type1,
        d->type2);
    for (int i = 0; i < 4; i++) {
        const MoveTable *move = &ctx->aiContext.moveTable[a->moves[i]];
        u32 flags = 0;
        // This helper reads battle state and writes only its output mask; it
        // does not sample RNG or alter the combatants.
        BattleSystem_CalcEffectiveness(ctx, a->moves[i], 0, a->ability, d->ability, Battler_HeldItemEffect(ctx, defender), d->type1, d->type2, &flags);
        n += snprintf(sBattle + n, sizeof(sBattle) - n, "%s[%u,%u,%u,%u,%u,%u,%u]", i ? "," : "", a->moves[i], a->ppCur[i], move->power, move->type, move->class, move->accuracy, !!(flags & MOVE_STATUS_INEFFECTIVE));
    }
    u8 byType[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    BattleSystem_SetBattlersByType(sys, byType);
    int targetSlot = -1;
    for (int i = 0; i < MAX_BATTLERS; i++) {
        if (byType[2 + i] == defender) {
            targetSlot = i;
        }
    }
    snprintf(sBattle + n, sizeof(sBattle) - n, "],\"target_slot\":%d,\"active_species\":%u}", targetSlot, a->species);
}

static void InstallFixture(FieldSystem *field)
{
    if (sFixture || field == NULL || !field->runningFieldMap) {
        Fail("fixture requested outside initial starter setup");
    }
    Party *party = SaveData_GetParty(field->saveData);
    if (!SaveData_IsNewGameData(field->saveData) || Party_GetCurrentCount(party) != 1
        || TrainerInfo_HasBadge(SaveData_GetTrainerInfo(field->saveData), 0)) {
        Fail("fixture requires one starter and no Coal Badge");
    }
    const int species[] = { SPECIES_MEW, SPECIES_BIBAREL, SPECIES_TROPIUS };
    const int moves[3][4] = {
        { MOVE_PSYCHIC, MOVE_AURA_SPHERE, MOVE_FLAMETHROWER, MOVE_THUNDERBOLT },
        { MOVE_SURF, MOVE_WATERFALL, MOVE_ROCK_CLIMB, MOVE_ROCK_SMASH },
        { MOVE_CUT, MOVE_FLY, MOVE_STRENGTH, MOVE_DEFOG }
    };
    TrainerInfo *trainer = SaveData_GetTrainerInfo(field->saveData);
    Party_Init(party);
    for (int i = 0; i < 3; i++) {
        Pokemon mon;
        // Fixed personality/IVs, and the current player's OT for obedience.
        Pokemon_InitWith(&mon, species[i], i == 0 ? 100 : 20, 31, TRUE, 0, OTID_SET, TrainerInfo_ID(trainer));
        Pokemon_SetValue(&mon, MON_DATA_OT_NAME, TrainerInfo_Name(trainer));
        u8 gender = TrainerInfo_Gender(trainer);
        Pokemon_SetValue(&mon, MON_DATA_OT_GENDER, &gender);
        for (int j = 0; j < 4; j++) {
            Pokemon_ResetMoveSlot(&mon, moves[i][j], j);
        }
        Party_AddPokemon(party, &mon);
    }
    sFixture = 1;
    fprintf(stderr, "[story-test] installed Mew/Bibarel/Tropius fixture\n");
}

static void Snapshot(void)
{
    FieldSystem *f = DEBUG_GetFieldSystem();
    fprintf(sWrite, "{\"protocol\":1,\"frame\":%u,\"app\":%lld,\"app_state\":%d,\"fixture\":%d,\"battles\":%d,\"losses\":%d,\"text_id\":%u,\"text\":\"%s\",\"menu\":%s,\"battle\":%s", sFrame, sApp, sAppState, sFixture, sBattles, sLosses, sTextSerial, sText, sMenuFrame + 1 >= sFrame && sMenu[0] ? sMenu : "null", sBattleFrame + 1 >= sFrame && sBattle[0] ? sBattle : "null");
    if (f != NULL && f->saveData != NULL) {
        fprintf(sWrite, ",\"ui\":%s", sUIFrame + 1 >= sFrame && sUI[0] ? sUI : "null");
        fprintf(sWrite, ",\"battle_order\":%s", sPartyOrderFrame + 1 >= sFrame && sPartyOrder[0] ? sPartyOrder : "null");
        fprintf(sWrite, ",\"battle_items_prohibited\":%d", GameplayConfig_Get()->prohibitBattleItems);
        Bag *bag = SaveData_GetBag(f->saveData);
        const BagItem *pockets[] = { bag->items, bag->keyItems, bag->tmHms, bag->mail, bag->medicine, bag->berries, bag->pokeballs, bag->battleItems };
        const int sizes[] = { ITEM_POCKET_SIZE, KEY_ITEM_POCKET_SIZE, TMHM_POCKET_SIZE, MAIL_POCKET_SIZE, MEDICINE_POCKET_SIZE, BERRY_POCKET_SIZE, POKEBALL_POCKET_SIZE, BATTLE_ITEM_POCKET_SIZE };
        fprintf(sWrite, ",\"money\":%u,\"inventory\":[", TrainerInfo_Money(SaveData_GetTrainerInfo(f->saveData)));
        int firstItem = 1;
        for (int p = 0; p < 8; p++) {
            for (int i = 0; i < sizes[p]; i++) {
                if (pockets[p][i].item && pockets[p][i].quantity) {
                    fprintf(sWrite, "%s[%u,%u]", firstItem ? "" : ",", pockets[p][i].item, pockets[p][i].quantity);
                    firstItem = 0;
                }
            }
        }
        PCBoxes *boxes = SaveData_GetPCBoxes(f->saveData);
        fprintf(sWrite, "],\"boxed\":[");
        int firstBoxMon = 1;
        for (int box = 0; box < MAX_PC_BOXES; box++) {
            for (int slot = 0; slot < MAX_MONS_PER_BOX; slot++) {
                u32 species = PCBoxes_GetBoxMonData(boxes, box, slot, MON_DATA_SPECIES, NULL);
                if (species) {
                    fprintf(sWrite, "%s[%d,%d,%u,%u]", firstBoxMon ? "" : ",", box, slot, species, PCBoxes_GetBoxMonData(boxes, box, slot, MON_DATA_PERSONALITY, NULL));
                    firstBoxMon = 0;
                }
            }
        }
        fprintf(sWrite, "]");
        Party *party = SaveData_GetParty(f->saveData);
        int count = Party_GetCurrentCount(party);
        TrainerInfo *trainer = SaveData_GetTrainerInfo(f->saveData);
        fprintf(sWrite, ",\"party\":%d,\"badge\":%d", count, TrainerInfo_HasBadge(trainer, 0));
        fprintf(sWrite, ",\"forest_badge\":%d,\"relic_badge\":%d", TrainerInfo_HasBadge(trainer, 1), TrainerInfo_HasBadge(trainer, 4));
        fprintf(sWrite, ",\"mons\":[");
        for (int i = 0; i < count; i++) {
            Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
            fprintf(sWrite, "%s[%u,%u,%u,%u,%u,%u]", i ? "," : "", Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL), Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL), Pokemon_GetValue(mon, MON_DATA_HP, NULL), Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL), Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL), Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL));
        }
        fprintf(sWrite, "]");
        VarsFlags *vars = SaveData_GetVarsFlags(f->saveData);
        fprintf(sWrite, ",\"vars\":[");
        for (int i = 0; i < NUM_VARS; i++) {
            fprintf(sWrite, "%s%u", i ? "," : "", vars->vars[i]);
        }
        fprintf(sWrite, "],\"roark_returned\":%d", VarsFlags_CheckFlag(vars, FLAG_ROARK_RETURNED_TO_OREBURGH_GYM));
        fprintf(sWrite, ",\"bicycle\":%d,\"explorer_kit\":%d,\"fantina_ready\":%d", VarsFlags_CheckFlag(vars, FLAG_RECEIVED_BICYCLE), VarsFlags_CheckFlag(vars, FLAG_RECEIVED_EXPLORER_KIT), VarsFlags_CheckFlag(vars, FLAG_HIDE_HEARTHOME_CITY_GYM_GUIDE));
        if (f->runningFieldMap && f->playerAvatar != NULL && f->mapObjMan != NULL) {
            int x = PlayerAvatar_GetXPos(f->playerAvatar), z = PlayerAvatar_GetZPos(f->playerAvatar);
            int clockState = -1;
            if (f->location->mapHeaderID == MAP_HEADER_ETERNA_CITY_GYM) {
                PersistedMapFeatures *features = MiscSaveBlock_GetPersistedMapFeatures(f->saveData);
                if (PersistedMapFeatures_GetID(features) == DYNAMIC_MAP_FEATURES_ETERNA_GYM) {
                    const EternaGymClockPersistedFeature *clock = PersistedMapFeatures_GetBuffer(features, DYNAMIC_MAP_FEATURES_ETERNA_GYM);
                    clockState = clock->state;
                    fprintf(sWrite, ",\"eterna_clock\":%d", clockState);
                }
            }
            fprintf(sWrite, ",\"avatar_state\":%d", PlayerAvatar_GetPlayerState(f->playerAvatar));
            if (f->location->mapHeaderID == MAP_HEADER_HEARTHOME_CITY_GYM_TRAINER_ROOM_1 || f->location->mapHeaderID == MAP_HEADER_HEARTHOME_CITY_GYM_TRAINER_ROOM_2) {
                PersistedMapFeatures *features = MiscSaveBlock_GetPersistedMapFeatures(f->saveData);
                if (PersistedMapFeatures_GetID(features) == DYNAMIC_MAP_FEATURES_HEARTHOME_GYM) {
                    const HearthomeGymPersistedFeatures *gym = PersistedMapFeatures_GetBuffer(features, DYNAMIC_MAP_FEATURES_HEARTHOME_GYM);
                    if (gym->initialized) {
                        fprintf(sWrite, ",\"hearthome_clue\":[%d,%d,%d]", gym->clueX, gym->clueZ, gym->correctDoorID);
                    }
                }
            }
            fprintf(sWrite, ",\"field\":{\"map\":%d,\"matrix\":%u,\"x\":%d,\"z\":%d,\"dir\":%d,\"busy\":%d,\"moving\":%d,\"objects\":[", f->location->mapHeaderID, MapHeader_GetMapMatrixID(f->location->mapHeaderID), x, z, PlayerAvatar_GetFacingDir(f->playerAvatar), FieldSystem_IsRunningTask(f) || f->processManager->pause, PlayerAvatar_GetMoveState(f->playerAvatar));
            MapObject *obj;
            int index = 0, first = 1;
            while (MapObjectMan_FindObjectWithStatus(f->mapObjMan, &obj, &index, 1)) {
                fprintf(sWrite, "%s[%u,%d,%d,%u]", first ? "" : ",", MapObject_GetLocalID(obj), MapObject_GetX(obj), MapObject_GetZ(obj), MapObject_GetScript(obj));
                first = 0;
            }
            fprintf(sWrite, "]");
            fprintf(sWrite, ",\"height\":%d", PlayerAvatar_GetPos(f->playerAvatar)->y);
            static int lastMap = -1, lastX = -1, lastZ = -1, lastBusy = -1, lastClock = -1;
            int busy = FieldSystem_IsRunningTask(f) || f->processManager->pause;
            if (f->terrainCollisionMan != NULL && (lastMap != f->location->mapHeaderID || x != lastX || z != lastZ || busy != lastBusy || clockState != lastClock)) {
                fprintf(sWrite, ",\"grid\":\"");
                for (int dz = -8; dz <= 8; dz++) {
                    for (int dx = -8; dx <= 8; dx++) {
                        u16 attr = 0xffff;
                        if (x + dx >= 0 && z + dz >= 0) {
                            f->terrainCollisionMan->getTileAttributes(f, x + dx, z + dz, &attr);
                            if (attr != 0xffff && f->location->mapHeaderID == MAP_HEADER_ETERNA_CITY_GYM) {
                                BOOL collision;
                                if (DynamicMapFeatures_CheckCollision(f, x + dx, z + dz, PlayerAvatar_GetPos(f->playerAvatar)->y, &collision) && collision) {
                                    attr |= 0x8000;
                                }
                            }
                        }
                        fprintf(sWrite, "%04x", attr);
                    }
                }
                fprintf(sWrite, "\"");
                lastMap = f->location->mapHeaderID;
                lastX = x;
                lastZ = z;
                lastBusy = busy;
                lastClock = clockState;
            }
            fprintf(sWrite, "}");
        }
    }
    fprintf(sWrite, "}\n");
    if (fflush(sWrite) != 0) {
        Fail("controller disconnected");
    }
}

u32 AutoplayTest_ReadPad(u32 physicalPad)
{
    if (sEnabled == -1) {
        const char *fdText = getenv("PLATINUM_STORY_TEST_FD");
        sEnabled = fdText != NULL;
        if (sEnabled) {
            char *end;
            long fd = strtol(fdText, &end, 10);
            if (*end || fd < 3) {
                Fail("invalid controller descriptor");
            }
            sRead = fdopen(dup(fd), "r");
            sWrite = fdopen(dup(fd), "w");
            if (!sRead || !sWrite) {
                Fail("cannot open controller descriptor");
            }
            setvbuf(sRead, NULL, _IONBF, 0);
        }
    }
    if (!sEnabled) {
        return physicalPad;
    }
    sFrame++;
    Snapshot();
    struct pollfd wait = { fileno(sRead), POLLIN, 0 };
    if (poll(&wait, 1, 30000) <= 0) {
        Fail("controller timed out");
    }
    char line[256], extra;
    unsigned pad;
    int fixture, shot;
    if (!fgets(line, sizeof(line), sRead)
        || sscanf(line, "%u %d %d %d %d %c", &pad, &sTouchX, &sTouchY, &fixture, &shot, &extra) != 5
        || pad > 0xfff || sTouchX < -1 || sTouchX > 255 || sTouchY < -1 || sTouchY > 191
        || (fixture != 0 && fixture != 1) || (shot != 0 && shot != 1)) {
        Fail("invalid controller command");
    }
    if (fixture) {
        InstallFixture(DEBUG_GetFieldSystem());
    }
    if (shot) {
        SIM_Autopilot_RequestShot("story-test.ppm");
    }
    return pad;
}

void AutoplayTest_Touch(TPData *touch)
{
    if (sEnabled != 1) {
        return;
    }
    touch->touch = sTouchX >= 0 && sTouchY >= 0;
    touch->validity = TP_VALIDITY_VALID;
    touch->x = touch->touch ? sTouchX : 0;
    touch->y = touch->touch ? sTouchY : 0;
}
