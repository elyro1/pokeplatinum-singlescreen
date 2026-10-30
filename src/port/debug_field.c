#include "port/debug_field.h"

#include <nitro.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "field/field_system.h"
#include "field/field_system_sub2_t.h"
#include "overlay005/map_name_popup.h"
#include "overlay005/save_info_window.h"

#include "encounter.h"
#include "game_options.h"
#include "save_player.h"
#include "party.h"
#include "pokemon.h"
#include "field_task.h"
#include "field_map_change.h"
#include "generated/movement_types.h"
#include "map_object.h"
#include "map_object_move.h"
#include "overworld_pokemon.h"
#include "player_avatar.h"

#include "poketch.h"

static FieldSystem *s_fieldSystem = NULL;
static OverworldPokemonHandle sDebugPokemon[32];
static int sDebugPokemonFrame;
static BOOL sDebugMapTriggered;
static int sDebugMapFrames;
static BOOL sDebugPokemonSaved;

FieldSystem *DEBUG_GetFieldSystem()
{
    return s_fieldSystem;
}

void DEBUG_SetFieldSystem(FieldSystem *fieldSystem)
{
    s_fieldSystem = fieldSystem;
    memset(sDebugPokemon, 0, sizeof(sDebugPokemon));
    sDebugPokemonFrame = 0;
    sDebugMapTriggered = FALSE;
    sDebugMapFrames = 0;
    sDebugPokemonSaved = FALSE;
}

BOOL DEBUG_CanUseFieldTools(void)
{
    return s_fieldSystem != NULL && s_fieldSystem->processManager != NULL
        && s_fieldSystem->saveData != NULL && s_fieldSystem->runningFieldMap
        && !s_fieldSystem->processManager->pause && !FieldSystem_IsRunningTask(s_fieldSystem);
}

int DEBUG_GetPoketchAppCount(void)
{
    if (s_fieldSystem == NULL || !s_fieldSystem->runningFieldMap) {
        return -1;
    }

    Poketch *poketch = SaveData_GetPoketch(s_fieldSystem->saveData);
    return Poketch_IsEnabled(poketch) ? poketch->appCount : -1;
}

BOOL DEBUG_UnlockAllPoketchApps(void)
{
    if (DEBUG_GetPoketchAppCount() < 0) {
        return FALSE;
    }

    Poketch *poketch = SaveData_GetPoketch(s_fieldSystem->saveData);
    for (int appID = 0; appID < POKETCH_APPID_MAX; appID++) {
        Poketch_RegisterApp(poketch, appID);
    }

    return TRUE;
}

// PLATINUM_DEBUG_WILD=<species>:<level> starts a wild battle against that Pokemon once the field has been under the
// player's control for a few seconds (the map name pop-up has to be gone), for testing battles without walking into
// grass (docs/bwbattle.md). PLATINUM_DEBUG_TRAINER=<id>[,<id>[,<partner>]] starts a trainer battle instead: the same ID
// twice is that trainer's double battle, two IDs a tag battle, and a third the partner who fights beside the player; the
// battle style turns to Set. PLATINUM_DEBUG_LEVEL_UP=1 also leaves the first Pokemon in the
// party one experience point short of its next level, so that the battle's experience levels it up, and
// PLATINUM_DEBUG_MOVE=<move> puts that move in its first move slot. PLATINUM_DEBUG_WILD_KIND=safari or palpark makes
// the wild battle the Safari Zone's or Pal Park's
#define DEBUG_WILD_DELAY_FRAMES 180
static u16 sDebugWildSpecies;
static u8 sDebugWildLevel;
static int sDebugTrainers[3];

static BOOL FieldTask_DebugWildBattle(FieldTask *task)
{
    int *state = FieldTask_GetState(task);

    if (*state == 0) {
        (*state)++;
        if (sDebugTrainers[0] != 0) {
            Encounter_NewVsTrainer(task, sDebugTrainers[0], sDebugTrainers[1], sDebugTrainers[2], HEAP_ID_FIELD2, NULL);
        } else {
            const char *kind = getenv("PLATINUM_DEBUG_WILD_KIND");

            if (kind != NULL && (strcmp(kind, "safari") == 0 || strcmp(kind, "palpark") == 0)) {
                Encounter_NewDebugVsSpeciesAtLevel(task, sDebugWildSpecies, sDebugWildLevel, kind[0] == 'p');
            } else {
                Encounter_NewVsSpeciesAtLevel(task, sDebugWildSpecies, sDebugWildLevel, NULL, FALSE);
            }
        }
        return FALSE;
    }

    return TRUE;
}

// Development fixture: PLATINUM_DEBUG_OVERWORLD_POKEMON=16 (maximum 32).
// Adjacent copies deliberately mix normal/shiny palettes and independent
// movement. No fixture actor is written to the save's object list.
static void DebugPokemonFrame(FieldSystem *fieldSystem)
{
    const char *setting = getenv("PLATINUM_DEBUG_OVERWORLD_POKEMON");
    if (setting == NULL || !fieldSystem->runningFieldMap || fieldSystem->processManager->pause
        || FieldSystem_IsRunningTask(fieldSystem) || fieldSystem->playerAvatar == NULL) {
        return;
    }
    int count = atoi(setting);
    if (count <= 0) {
        return;
    }
    if (count > NELEMS(sDebugPokemon)) {
        count = NELEMS(sDebugPokemon);
    }
    if (++sDebugPokemonFrame % 30 != 0) {
        return;
    }

    static const u16 species[] = { 1, 25, 133, 152, 155, 158, 252, 255, 258, 387, 390, 393, 445, 448, 487, 493 };
    MapObject *player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
    int spawned = 0, active = 0;
    for (int i = 0; i < count; i++) {
        if (OverworldPokemon_GetObject(fieldSystem, sDebugPokemon[i]) != NULL) {
            active++;
            continue;
        }
        sDebugPokemon[i] = 0;
        for (int dz = -4; dz <= 4 && sDebugPokemon[i] == 0; dz++) {
            for (int dx = -5; dx <= 5 && sDebugPokemon[i] == 0; dx++) {
                if (abs(dx) + abs(dz) < 2) {
                    continue;
                }
                int x = MapObject_GetX(player) + dx, z = MapObject_GetZ(player) + dz;
                int dir = GetDirectionBetweenPoints(MapObject_GetX(player), MapObject_GetZ(player), x, z);
                if (x < 0 || z < 0 || sub_02063E94(player, x, MapObject_GetY(player), z, dir) != 0) {
                    continue;
                }
                OverworldPokemonSpawn spawn = {
                    .appearance = { species[(i / 2) % NELEMS(species)], 0, i % 3, i & 1 },
                    .ownerMap = fieldSystem->location->mapHeaderID, .ownerID = i,
                    .x = x, .y = MapObject_GetY(player), .z = z, .direction = i % 4,
                    .movementType = MOVEMENT_TYPE_LOOK_AROUND, .rangeX = 0, .rangeZ = 0,
                    .role = OVERWORLD_POKEMON_TEMPORARY
                };
                sDebugPokemon[i] = OverworldPokemon_Spawn(fieldSystem, &spawn);
                if (sDebugPokemon[i] != 0) {
                    active++;
                    spawned++;
                }
            }
        }
    }
    if (spawned != 0) {
        MapObjectSave snapshot[64];
        MapObjectMan_SaveAll(fieldSystem, fieldSystem->mapObjMan, snapshot, NELEMS(snapshot));
        for (int i = 0; i < NELEMS(snapshot); i++) {
            if (!(snapshot[i].status & MAP_OBJ_STATUS_0)) {
                continue;
            }
            for (int actor = -1; actor < count; actor++) {
                OverworldPokemonHandle handle = actor == -1 ? FollowingPokemon_GetHandle(fieldSystem) : sDebugPokemon[actor];
                MapObject *object = OverworldPokemon_GetObject(fieldSystem, handle);
                if (object != NULL && snapshot[i].localID == (u8)MapObject_GetLocalID(object)
                    && snapshot[i].mapID == MapObject_GetMapHeaderID(object)
                    && snapshot[i].graphicsID == MapObject_GetGraphicsID(object)
                    && snapshot[i].x == MapObject_GetX(object) && snapshot[i].z == MapObject_GetZ(object)) {
                    fprintf(stderr, "[overworld-pokemon] ERROR: runtime Pokémon leaked into save snapshot\n");
                    abort();
                }
            }
        }
        fprintf(stderr, "[overworld-pokemon] fixture=%d/%d follower=%u map=%d\n", active, count,
            FollowingPokemon_GetHandle(fieldSystem), fieldSystem->location->mapHeaderID);
    }
    if (getenv("PLATINUM_DEBUG_FOLLOWER_TRACE") != NULL && sDebugPokemonFrame % 60 == 0) {
        MapObject *follower = OverworldPokemon_GetObject(fieldSystem, FollowingPokemon_GetHandle(fieldSystem));
        fprintf(stderr, "[overworld-pokemon] map=%d player=%d,%d follower=%d,%d gfx=%u\n", fieldSystem->location->mapHeaderID, MapObject_GetX(player), MapObject_GetZ(player),
            follower != NULL ? MapObject_GetX(follower) : -1, follower != NULL ? MapObject_GetZ(follower) : -1,
            follower != NULL ? MapObject_GetGraphicsID(follower) : 0);
    }
    const char *saveAfter = getenv("PLATINUM_DEBUG_FOLLOWER_SAVE");
    if (!sDebugPokemonSaved && saveAfter != NULL && sDebugPokemonFrame >= atoi(saveAfter)
        && sDebugPokemonFrame >= 180 && FollowingPokemon_GetHandle(fieldSystem) != 0 && !MapObject_IsMoving(player)) {
        sDebugPokemonSaved = TRUE;
        fprintf(stderr, "[overworld-pokemon] saved=%d map=%d follower=%u\n", FieldSystem_Save(fieldSystem),
            fieldSystem->location->mapHeaderID, FollowingPokemon_GetHandle(fieldSystem));
    }
}

void DEBUG_FieldFrame(FieldSystem *fieldSystem)
{
    // Optional destination for reproducible follower/transition tests. Like the
    // map-jump debug panel, this changes runtime position only until saving.
    const char *mapSetting = getenv("PLATINUM_DEBUG_MAP");
    if (!sDebugMapTriggered && mapSetting != NULL && fieldSystem->runningFieldMap
        && !fieldSystem->processManager->pause && !FieldSystem_IsRunningTask(fieldSystem)
        && ++sDebugMapFrames >= 30 && fieldSystem->unk_04->mapPopup->task == NULL) {
        int map, x, z;
        sDebugMapTriggered = TRUE;
        if (sscanf(mapSetting, "%d:%d:%d", &map, &x, &z) == 3
            && map > 0 && map < MAP_HEADER_COUNT && x >= 0 && z >= 0 && x <= 0x7FFF && z <= 0x7FFF) {
            FieldTask_StartMapChangeFly(fieldSystem, map, -1, x, z, DIR_SOUTH);
            return;
        }
    }
    DebugPokemonFrame(fieldSystem);
    static BOOL sTriggered;
    static int sControlFrames;

    if (sTriggered || fieldSystem->processManager->pause || !fieldSystem->runningFieldMap
        || FieldSystem_IsRunningTask(fieldSystem)) {
        sControlFrames = 0;
        return;
    }
    if (++sControlFrames < DEBUG_WILD_DELAY_FRAMES) {
        return;
    }

    const char *wild = getenv("PLATINUM_DEBUG_WILD");
    const char *trainer = getenv("PLATINUM_DEBUG_TRAINER");
    unsigned species, level;
    int trainers[3] = { 0, 0, 0 };

    sTriggered = TRUE;
    if (trainer != NULL && sscanf(trainer, "%d,%d,%d", &trainers[0], &trainers[1], &trainers[2]) >= 1 && trainers[0] > 0) {
        sDebugTrainers[0] = trainers[0];
        sDebugTrainers[1] = trainers[1];
        sDebugTrainers[2] = trainers[2];
        // The Set style, so that a test's presses don't answer the switch prompt
        Options_SetBattleStyle(SaveData_GetOptions(fieldSystem->saveData), OPTIONS_BATTLE_STYLE_SET);
    } else if (wild == NULL || sscanf(wild, "%u:%u", &species, &level) != 2 || species == 0 || level == 0 || level > 100) {
        return;
    } else {
        sDebugWildSpecies = species;
        sDebugWildLevel = level;
    }
    if (getenv("PLATINUM_DEBUG_LEVEL_UP") != NULL) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(SaveData_GetParty(fieldSystem->saveData), 0);
        u32 exp = Pokemon_GetValue(mon, MON_DATA_EXPERIENCE, NULL) + Pokemon_GetExpToNextLevel(mon) - 1;

        if (Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL) < MAX_POKEMON_LEVEL) {
            Pokemon_SetValue(mon, MON_DATA_EXPERIENCE, &exp);
        }
    }
    const char *move = getenv("PLATINUM_DEBUG_MOVE");
    unsigned moveID;

    if (move != NULL && sscanf(move, "%u", &moveID) == 1 && moveID > 0 && moveID <= NUM_VALID_MOVES) {
        Pokemon_ResetMoveSlot(Party_GetPokemonBySlotIndex(SaveData_GetParty(fieldSystem->saveData), 0), moveID, 0);
    }
    FieldSystem_CreateTask(fieldSystem, FieldTask_DebugWildBattle, NULL);
}
