#include "port/debug_field.h"

#include <nitro.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "field/field_system.h"

#include "encounter.h"
#include "game_options.h"
#include "save_player.h"
#include "party.h"
#include "pokemon.h"
#include "field_task.h"

#include "poketch.h"

static FieldSystem *s_fieldSystem = NULL;

FieldSystem *DEBUG_GetFieldSystem()
{
    return s_fieldSystem;
}

void DEBUG_SetFieldSystem(FieldSystem *fieldSystem)
{
    s_fieldSystem = fieldSystem;
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

void DEBUG_FieldFrame(FieldSystem *fieldSystem)
{
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
