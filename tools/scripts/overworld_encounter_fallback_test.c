#include <assert.h>
#include <stdio.h>
#include "port/sim_config_prj.h"
#include "constants/field/map_load.h"

#include "../../src/overlay005/field_control.c"

static SIM_Config_prj_type config;
static BOOL assetsReady, hasEncounters = TRUE, radarActive, inPalPark;
static int randomCalls, radarCalls;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &config; }
int FollowerAssets_Ready(void) { return assetsReady; }
int PlayerAvatar_GetXPos(PlayerAvatar *const avatar) { return 0; }
int PlayerAvatar_GetZPos(PlayerAvatar *const avatar) { return 0; }
int PlayerAvatar_GetPlayerState(PlayerAvatar *avatar) { return PLAYER_AVATAR_WALKING; }
enum AvatarDistortionState PlayerAvatar_MapDistortionState(PlayerAvatar *const avatar) { return AVATAR_DISTORTION_STATE_NONE; }
int MapObjectMan_IsDrawInitialized(const MapObjectManager *manager) { return TRUE; }
BOOL MapHeader_HasWildEncounters(enum MapHeaderID id) { return hasEncounters; }
VarsFlags *SaveData_GetVarsFlags(SaveData *saveData) { return NULL; }
BOOL SystemFlag_CheckInPalPark(VarsFlags *flags) { return inPalPark; }
BOOL GetRadarChainActive(const RadarChain *chain) { return radarActive; }
BOOL WildEncounters_TryWildEncounter(FieldSystem *field) { randomCalls++; return TRUE; }
BOOL WildEncounters_TryRadarEncounter(FieldSystem *field) { radarCalls++; return TRUE; }
BOOL CatchingShow_CheckWildEncounter(FieldSystem *field, int x, int y) { return FALSE; }
FieldBattleDTO *CatchingShow_GetBattleDataTransfer(FieldSystem *field) { return NULL; }
void Encounter_NewVsPalParkTransfer(FieldSystem *field, FieldBattleDTO *dto) { assert(0); }

int main(void)
{
    Location location = { 0 };
    FieldSystem field = { .location = &location, .mapLoadType = MAP_LOAD_TYPE_OVERWORLD,
        .playerAvatar = (PlayerAvatar *)1, .mapObjMan = (MapObjectManager *)1,
        .overworldPokemon = (struct OverworldPokemonManager *)1 };
    config.overworldEncounters = TRUE;
    assetsReady = FALSE;
    assert(Field_CheckWildEncounter(&field) && randomCalls == 1 && radarCalls == 0);
    assetsReady = TRUE;
    config.overworldEncounters = FALSE;
    assert(Field_CheckWildEncounter(&field) && randomCalls == 2);
    config.overworldEncounters = TRUE;
    assert(!Field_CheckWildEncounter(&field) && randomCalls == 2);
    radarActive = TRUE;
    assert(Field_CheckWildEncounter(&field) && radarCalls == 1);
    field.mapLoadType = MAP_LOAD_TYPE_UNDERGROUND;
    assert(Field_CheckWildEncounter(&field) && randomCalls == 3);
    hasEncounters = FALSE;
    assert(!Field_CheckWildEncounter(&field) && randomCalls == 3 && radarCalls == 1);
    puts("Encounter fallback: missing assets, disabled mode, visible mode, Radar, and other map types passed.");
}
