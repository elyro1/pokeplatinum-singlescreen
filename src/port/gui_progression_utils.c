#include <nitro.h>

#include "gui_progression_utils.h"
#include "port/debug_field.h"
#include "field/field_system.h"
#include "generated/badges.h"
#include "pokedex.h"
#include "save_player.h"
#include "system_flags.h"
#include "trainer_info.h"
#include "vars_flags.h"

static SaveData *Progression_Save(void)
{
    return DEBUG_CanUseFieldTools() ? DEBUG_GetFieldSystem()->saveData : NULL;
}

int Progression_GetBadge(int badge)
{
    SaveData *save = Progression_Save();
    return save && badge >= 0 && badge < MAX_BADGES && TrainerInfo_HasBadge(SaveData_GetTrainerInfo(save), badge);
}

void Progression_GiveBadge(int badge)
{
    SaveData *save = Progression_Save();
    if (save && badge >= 0 && badge < MAX_BADGES) TrainerInfo_SetBadge(SaveData_GetTrainerInfo(save), badge);
}

int Progression_HasDex(int national)
{
    SaveData *save = Progression_Save();
    if (!save) return FALSE;
    Pokedex *dex = SaveData_GetPokedex(save);
    return national ? Pokedex_IsNationalDexObtained(dex) : Pokedex_IsObtained(dex);
}

void Progression_GiveDex(int national)
{
    SaveData *save = Progression_Save();
    if (!save) return;
    Pokedex *dex = SaveData_GetPokedex(save);
    Pokedex_ObtainPokedex(dex);
    if (national) {
        Pokedex_ObtainNationalDex(dex);
        TrainerInfo_GiveNationalDex(SaveData_GetTrainerInfo(save));
    }
}

int Progression_IsGameCompleted(void)
{
    SaveData *save = Progression_Save();
    return save && SystemFlag_CheckGameCompleted(SaveData_GetVarsFlags(save));
}

void Progression_SetGameCompleted(void)
{
    SaveData *save = Progression_Save();
    if (save) SystemFlag_SetGameCompleted(SaveData_GetVarsFlags(save));
}

void Progression_UnlockFlyLocations(void)
{
    static const u8 locations[] = {
        FIRST_ARRIVAL_TWINLEAF_TOWN, FIRST_ARRIVAL_SANDGEM_TOWN, FIRST_ARRIVAL_FLOAROMA_TOWN,
        FIRST_ARRIVAL_SOLACEON_TOWN, FIRST_ARRIVAL_CELESTIC_TOWN, FIRST_ARRIVAL_SURVIVAL_AREA,
        FIRST_ARRIVAL_RESORT_AREA, FIRST_ARRIVAL_JUBILIFE_CITY, FIRST_ARRIVAL_CANALAVE_CITY,
        FIRST_ARRIVAL_OREBURGH_CITY, FIRST_ARRIVAL_ETERNA_CITY, FIRST_ARRIVAL_HEARTHOME_CITY,
        FIRST_ARRIVAL_PASTORIA_CITY, FIRST_ARRIVAL_VEILSTONE_CITY, FIRST_ARRIVAL_SUNYSHORE_CITY,
        FIRST_ARRIVAL_SNOWPOINT_CITY, FIRST_ARRIVAL_FIGHT_AREA, FIRST_ARRIVAL_POKE_PARK_FRONT_GATE,
        FIRST_ARRIVAL_OUTSIDE_VICTORY_ROAD, FIRST_ARRIVAL_POKEMON_LEAGUE,
    };
    SaveData *save = Progression_Save();
    if (!save) return;
    VarsFlags *flags = SaveData_GetVarsFlags(save);
    for (int i = 0; i < NELEMS(locations); i++) {
        SystemFlag_HandleFirstArrivalToZone(flags, HANDLE_FLAG_SET, locations[i]);
    }
}

int Progression_FlagLimit(void) { return NUM_FLAGS; }
int Progression_VarStart(void) { return VARS_START; }
int Progression_VarEnd(void) { return VARS_END; }

int Progression_GetFlag(int id)
{
    SaveData *save = Progression_Save();
    return save && id > 0 && id < NUM_FLAGS && VarsFlags_CheckFlag(SaveData_GetVarsFlags(save), id);
}

void Progression_SetFlag(int id, int value)
{
    SaveData *save = Progression_Save();
    if (!save || id <= 0 || id >= NUM_FLAGS) return;
    if (value) VarsFlags_SetFlag(SaveData_GetVarsFlags(save), id);
    else VarsFlags_ClearFlag(SaveData_GetVarsFlags(save), id);
}

int Progression_GetVar(int id)
{
    SaveData *save = Progression_Save();
    if (!save || id < VARS_START || id >= VARS_END) return 0;
    return *VarsFlags_GetVarAddress(SaveData_GetVarsFlags(save), id);
}

void Progression_SetVar(int id, int value)
{
    SaveData *save = Progression_Save();
    if (!save || id < VARS_START || id >= VARS_END || value < 0 || value > 65535) return;
    *VarsFlags_GetVarAddress(SaveData_GetVarsFlags(save), id) = value;
}
