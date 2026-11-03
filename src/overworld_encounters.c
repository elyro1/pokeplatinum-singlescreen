#include "overworld_encounters.h"

#include <string.h>

#include "constants/field/map.h"
#include "constants/field/map_load.h"
#include "generated/movement_types.h"

#include "field/field_system.h"
#include "overlay005/land_data.h"
#include "overlay006/wild_encounters.h"

#include "field_system.h"
#include "heap.h"
#include "inlines.h"
#include "map_header.h"
#include "map_header_data.h"
#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "math_util.h"
#include "overworld_pokemon.h"
#include "party.h"
#include "player_avatar.h"
#include "pokeradar.h"
#include "special_encounter.h"
#include "system_flags.h"
#include "terrain_collision_manager.h"
#include "unk_02054884.h"
#include "unk_020655F4.h"

#ifdef SDK_PORT
#include "port/sim_config_prj.h"
#include "port/follower_assets.h"
#endif
#ifndef SDK_BUILD_ARM
#include <simulator/sim.h>
#endif

// Half extents of the visible field in tiles from the player, with slack for
// the perspective camera. Sprites leave only beyond the view plus a margin.
#define VIEW_HALF_HEIGHT 7
#define CULL_MARGIN      2
// Encounter tables of connected zones, so their visible grass is populated
// before the player enters. The player's own zone uses the map header data.
#define ZONE_TABLE_CACHE_SIZE 4

typedef struct WildSprite {
    OverworldPokemonHandle handle;
    OverworldEncounterPokemon encounter;
    int anchorX, anchorZ;
    int wanderTicks;
    int age;
    // The zone whose encounter table generated this sprite. It keeps wandering
    // within that zone after the player crosses into a connected one.
    u16 ownerMap;
    u8 habitat;
    BOOL stepping;
} WildSprite;

typedef struct ZoneTable {
    int mapID;
    WildEncounters encounters;
} ZoneTable;

typedef struct OverworldEncounterManager {
    WildSprite sprites[OVERWORLD_ENCOUNTER_CAPACITY];
    int mapID;
    int spawnTicks;
    u32 context;
    ZoneTable tables[ZONE_TABLE_CACHE_SIZE];
    int nextTable;
} OverworldEncounterManager;

static int FrameTicks(int ticks)
{
#ifdef SDK_PORT
    return SIM_Config_prj_GetConfig()->enable60fps ? ticks * 2 : ticks;
#else
    return ticks;
#endif
}

static int Abs(int value)
{
    return value < 0 ? -value : value;
}

static int ViewHalfWidth(void)
{
#ifndef SDK_BUILD_ARM
    // The port widens the field (up to 448 dots) only in the top-only layout.
    if (SIM_GetConfigPtr()->screenLayout == SIM_CONFIG_SCREEN_LAYOUT_TOP_ONLY) {
        return 448 / 32 + 1;
    }
#endif
    return 256 / 32 + 1;
}

// Whether visible encounters may exist and spawn. Each spawn additionally needs
// its tile's zone to have wild encounters (see ZoneEncounters).
static BOOL Active(FieldSystem *field)
{
#ifdef SDK_PORT
    if (!SIM_Config_prj_GetConfig()->overworldEncounters || !FollowerAssets_Ready()
        || SIM_Config_prj_GetConfig()->disableRandomEncounters) {
        return FALSE;
    }
#endif
    if (field->overworldPokemon == NULL || field->playerAvatar == NULL || field->mapObjMan == NULL
        || !MapObjectMan_IsDrawInitialized(field->mapObjMan)
        || field->mapLoadType != MAP_LOAD_TYPE_OVERWORLD
        || PlayerAvatar_MapDistortionState(field->playerAvatar) != AVATAR_DISTORTION_STATE_NONE
        || SystemFlag_CheckInPalPark(SaveData_GetVarsFlags(field->saveData))) {
        return FALSE;
    }
    int state = PlayerAvatar_GetPlayerState(field->playerAvatar);
    return state == PLAYER_AVATAR_WALKING || state == PLAYER_AVATAR_CYCLING || state == PLAYER_AVATAR_SURFING;
}

BOOL OverworldEncounters_ReplaceRandom(FieldSystem *field)
{
#ifdef SDK_PORT
    return Active(field) && MapHeader_HasWildEncounters(field->location->mapHeaderID);
#else
    return FALSE;
#endif
}

static BOOL Paused(FieldSystem *field)
{
    return field->processManager->pause || FieldSystem_IsRunningTask(field)
        || field->followingPokemonSuppression[FOLLOWING_POKEMON_SUPPRESS_SCRIPT]
        || field->followingPokemonSuppression[FOLLOWING_POKEMON_SUPPRESS_FIELD_MOVE];
}

static void RemoveSprite(FieldSystem *field, WildSprite *sprite)
{
    OverworldPokemon_Despawn(field, sprite->handle);
    memset(sprite, 0, sizeof(*sprite));
}

void OverworldEncounters_Clear(FieldSystem *field)
{
    OverworldEncounterManager *manager = field->overworldEncounters;
    if (manager != NULL) {
        for (int i = 0; i < OVERWORLD_ENCOUNTER_CAPACITY; i++) {
            RemoveSprite(field, &manager->sprites[i]);
        }
        manager->spawnTicks = FrameTicks(15);
    }
}

void OverworldEncounters_Free(FieldSystem *field)
{
    // The map-object manager owns object destruction. Its objects may already
    // be gone here; the service owns only these runtime encounter records.
    if (field->overworldEncounters != NULL) {
        Heap_Free(field->overworldEncounters);
        field->overworldEncounters = NULL;
    }
}

// Battle types baked into stored encounters. Surfing is not part of it: each
// sprite keeps its own land/water habitat when the player gets on or off water.
static u32 Context(FieldSystem *field)
{
    return (SystemFlag_CheckHasPartner(SaveData_GetVarsFlags(field->saveData)) ? 1 : 0)
        | (SystemFlag_CheckSafariGameActive(SaveData_GetVarsFlags(field->saveData)) ? 2 : 0);
}

// Never sample an unloaded neighboring zone using another zone's encounter
// table, and keep sprites within the zone that generated them.
static BOOL LoadedTile(FieldSystem *field, int x, int z, u16 mapID)
{
    int localX = x - LandDataManager_GetOffsetTileX(field->landDataMan);
    int localZ = z - LandDataManager_GetOffsetTileZ(field->landDataMan);
    if (x < 0 || z < 0 || x > 0x7FFF || z > 0x7FFF || localX < 0 || localZ < 0
        || localX >= MapMatrix_GetWidth(field->mapMatrix) * MAP_TILES_COUNT_X
        || localZ >= MapMatrix_GetHeight(field->mapMatrix) * MAP_TILES_COUNT_Z
        || !LandDataManager_CanEnterTile(field->landDataMan, x, z)
        || !LandDataManager_GetRelativeLoadedMapsQuadrantOfTile(field->landDataMan, x, z, NULL)) {
        return FALSE;
    }
    if (MapHeader_IsOnMainMatrix(mapID)) {
        if (MapMatrix_GetMapHeaderIDAtCoords(field->mapMatrix, localX / MAP_TILES_COUNT_X, localZ / MAP_TILES_COUNT_Z) != mapID) {
            return FALSE;
        }
    }
    return TRUE;
}

// The zone a tile belongs to. Off the main matrix zones are not connected, so
// every tile counts as the player's zone.
static u16 TileZone(FieldSystem *field, int x, int z)
{
    int localX = x - LandDataManager_GetOffsetTileX(field->landDataMan);
    int localZ = z - LandDataManager_GetOffsetTileZ(field->landDataMan);
    if (!MapHeader_IsOnMainMatrix(field->location->mapHeaderID) || localX < 0 || localZ < 0
        || localX >= MapMatrix_GetWidth(field->mapMatrix) * MAP_TILES_COUNT_X
        || localZ >= MapMatrix_GetHeight(field->mapMatrix) * MAP_TILES_COUNT_Z) {
        return field->location->mapHeaderID;
    }
    return MapMatrix_GetMapHeaderIDAtCoords(field->mapMatrix, localX / MAP_TILES_COUNT_X, localZ / MAP_TILES_COUNT_Z);
}

// The encounter table of a zone, or NULL when it has no wild encounters (such
// as a town). Neighbor tables are read once and kept in a small cache.
static WildEncounters *ZoneEncounters(FieldSystem *field, OverworldEncounterManager *manager, u16 mapID)
{
    if (mapID == MAP_HEADER_EVERYWHERE || mapID >= MAP_HEADER_COUNT || !MapHeader_HasWildEncounters(mapID)) {
        return NULL;
    }
    if (mapID == field->location->mapHeaderID) {
        return (WildEncounters *)MapHeaderData_GetWildEncounters(field);
    }
    for (int i = 0; i < ZONE_TABLE_CACHE_SIZE; i++) {
        if (manager->tables[i].mapID == mapID) {
            return &manager->tables[i].encounters;
        }
    }
    ZoneTable *table = &manager->tables[manager->nextTable];
    manager->nextTable = (manager->nextTable + 1) % ZONE_TABLE_CACHE_SIZE;
    MapHeaderData_LoadWildEncounters(&table->encounters, mapID);
    table->mapID = mapID;
    return &table->encounters;
}

static BOOL HabitatTile(FieldSystem *field, int x, int z, BOOL water, u16 mapID)
{
    if (!LoadedTile(field, x, z, mapID)) {
        return FALSE;
    }
    u8 tile = TerrainCollisionManager_GetTileBehavior(field, x, z);
    return TileBehavior_HasEncounters(tile) && TileBehavior_IsSurfable(tile) == water
        && !TileBehavior_IsWaterfall(tile) && !TileBehavior_IsBridge(tile) && !TileBehavior_IsBridgeStart(tile);
}

static BOOL Repelled(FieldSystem *field, const WildSprite *sprite)
{
    if (SpecialEncounter_RepelStepsEmpty(SaveData_GetSpecialEncounters(field->saveData))) {
        return FALSE;
    }
    Pokemon *lead = Party_FindFirstEligibleBattler(SaveData_GetParty(field->saveData));
    return Pokemon_GetValue((Pokemon *)&sprite->encounter.mons[0], MON_DATA_LEVEL, NULL)
        < Pokemon_GetValue(lead, MON_DATA_LEVEL, NULL);
}

static void Wander(FieldSystem *field, WildSprite *sprite, MapObject *object)
{
    if (sprite->stepping) {
        if (!sub_020656AC(object)) {
            return;
        }
        sprite->stepping = FALSE;
    }
    if (MapObject_IsMovementPaused(object) || --sprite->wanderTicks > 0) {
        return;
    }
    sprite->wanderTicks = FrameTicks(30 + LCRNG_RandMod(60));
    int dir = LCRNG_RandMod(4);
    int x = MapObject_GetX(object) + MapObject_GetDxFromDir(dir);
    int z = MapObject_GetZ(object) + MapObject_GetDzFromDir(dir);
    BOOL water = TileBehavior_IsSurfable(sprite->habitat);
    if (Abs(x - sprite->anchorX) > 2 || Abs(z - sprite->anchorZ) > 2
        || !HabitatTile(field, x, z, water, sprite->ownerMap)
        || TerrainCollisionManager_GetTileBehavior(field, x, z) != sprite->habitat
        || sub_02063E94(object, x, MapObject_GetY(object), z, dir) != MAP_OBJ_COLLISION_NONE) {
        MapObject_TryFace(object, dir);
        return;
    }
    LocalMapObj_SetAnimationCode(object, MOVEMENT_ACTION_WALK_SLOW_NORTH + dir);
    sprite->stepping = TRUE;
}

static BOOL Spawn(FieldSystem *field, WildSprite *sprite, int slot)
{
    MapObject *player = PlayerAvatar_GetMapObject(field->playerAvatar);
    BOOL water = PlayerAvatar_GetPlayerState(field->playerAvatar) == PLAYER_AVATAR_SURFING;
    if (MapObject_IsStatusOnElevatedBridge(player)) {
        return FALSE;
    }
    for (int attempt = 0; attempt < 12; attempt++) {
        int dx = (int)LCRNG_RandMod(15) - 7;
        int dz = (int)LCRNG_RandMod(9) - 4;
        int facing = PlayerAvatar_GetFacingDir(field->playerAvatar);
        int close = water ? 3 : 1;
        // Avoid materializing a battle directly in the player's running line.
        if ((Abs(dx) <= close && Abs(dz) <= close)
            || (dx == 0 && dz * MapObject_GetDzFromDir(facing) > 0)
            || (dz == 0 && dx * MapObject_GetDxFromDir(facing) > 0)) {
            continue;
        }
        int x = MapObject_GetX(player) + dx, z = MapObject_GetZ(player) + dz;
        // Tiles across a zone border spawn from that zone's table, so grass
        // in sight is populated whichever zone the player stands in.
        u16 zone = TileZone(field, x, z);
        WildEncounters *table = ZoneEncounters(field, field->overworldEncounters, zone);
        if (table == NULL || !HabitatTile(field, x, z, water, zone)
            || TerrainCollisionManager_WillMapObjectCollide(field, MapObject_GetPos(player), x, z, NULL)
            || sub_02063F00(player, x, MapObject_GetY(player), z)) {
            continue;
        }
        // Player collision exempts followers and wild sprites; explicitly
        // reserve all actors' current/previous tiles before materializing.
        BOOL occupied = FALSE;
        int index = 0;
        MapObject *other;
        while (MapObjectMan_FindObjectWithStatus(field->mapObjMan, &other, &index, MAP_OBJ_STATUS_0)) {
            if (Abs(MapObject_GetY(other) - MapObject_GetY(player)) < 2
                && ((MapObject_GetX(other) == x && MapObject_GetZ(other) == z)
                    || (MapObject_GetXPrev(other) == x && MapObject_GetZPrev(other) == z))) {
                occupied = TRUE;
            }
        }
        if (occupied) {
            continue;
        }
        if (!WildEncounters_GenerateVisiblePokemon(field, x, z, zone, table, &sprite->encounter)) {
            return FALSE;
        }
        // A roamer is a single persistent Pokemon, never multiple copies of it.
        if (sprite->encounter.battleType & BATTLE_TYPE_ROAMER) {
            for (int i = 0; i < OVERWORLD_ENCOUNTER_CAPACITY; i++) {
                WildSprite *other = &field->overworldEncounters->sprites[i];
                if (other->handle != 0 && (other->encounter.battleType & BATTLE_TYPE_ROAMER)) {
                    return FALSE;
                }
            }
        }
        Pokemon *mon = &sprite->encounter.mons[0];
        OverworldPokemonSpawn spawn = {
            .appearance = {
                .species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL),
                .form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL),
                .gender = Pokemon_GetGender(mon),
                .shiny = Pokemon_IsShiny(mon),
            },
            .ownerMap = zone,
            .ownerID = slot,
            .x = x,
            .y = MapObject_GetY(player),
            .z = z,
            .direction = LCRNG_RandMod(4),
            .movementType = MOVEMENT_TYPE_NONE,
            .rangeX = 2,
            .rangeZ = 2,
            .role = OVERWORLD_POKEMON_WILD,
        };
        sprite->handle = OverworldPokemon_Spawn(field, &spawn);
        if (sprite->handle == 0) {
            return FALSE;
        }
        sprite->anchorX = x;
        sprite->anchorZ = z;
        sprite->ownerMap = zone;
        sprite->habitat = TerrainCollisionManager_GetTileBehavior(field, x, z);
        sprite->wanderTicks = FrameTicks(30 + LCRNG_RandMod(60));
        return TRUE;
    }
    return FALSE;
}

static BOOL Offscreen(FieldSystem *field, MapObject *object, int margin)
{
    int dx = Abs(MapObject_GetX(object) - PlayerAvatar_GetXPos(field->playerAvatar));
    int dz = Abs(MapObject_GetZ(object) - PlayerAvatar_GetZPos(field->playerAvatar));
    return dx > ViewHalfWidth() + margin || dz > VIEW_HALF_HEIGHT + margin;
}

// At the spawn limit, new spawns get room by retiring the farthest offscreen
// sprite of another zone (left behind, or populating a neighbor's grass).
static int FindForeignSprite(FieldSystem *field, OverworldEncounterManager *manager)
{
    int best = -1, bestDistance = -1;
    for (int i = 0; i < OVERWORLD_ENCOUNTER_CAPACITY; i++) {
        WildSprite *sprite = &manager->sprites[i];
        MapObject *object = OverworldPokemon_GetObject(field, sprite->handle);
        if (object == NULL || sprite->ownerMap == field->location->mapHeaderID || !Offscreen(field, object, 0)) {
            continue;
        }
        int distance = Abs(MapObject_GetX(object) - PlayerAvatar_GetXPos(field->playerAvatar))
            + Abs(MapObject_GetZ(object) - PlayerAvatar_GetZPos(field->playerAvatar));
        if (distance > bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void OverworldEncounters_Update(FieldSystem *field)
{
    if (!Active(field)) {
        OverworldEncounters_Clear(field);
        return;
    }
    // Radar continues to use its original shaking-patch encounters. Clear
    // ordinary sprites while it is active so they cannot interrupt a chain.
    if (GetRadarChainActive(field->chain)) {
        OverworldEncounters_Clear(field);
        return;
    }
    OverworldEncounterManager *manager = field->overworldEncounters;
    if (manager == NULL) {
        manager = Heap_Alloc(HEAP_ID_FIELD2, sizeof(*manager));
        if (manager == NULL) {
            return;
        }
        memset(manager, 0, sizeof(*manager));
        field->overworldEncounters = manager;
        manager->mapID = -1;
        for (int i = 0; i < ZONE_TABLE_CACHE_SIZE; i++) {
            manager->tables[i].mapID = -1;
        }
        manager->context = Context(field);
    }
    if (manager->context != Context(field)) {
        OverworldEncounters_Clear(field);
        manager->context = Context(field);
    }
    if (manager->mapID != field->location->mapHeaderID) {
        // Zone ownership of tiles is only known on the main matrix; elsewhere
        // a zone change still starts over.
        if (!MapHeader_IsOnMainMatrix(field->location->mapHeaderID)) {
            OverworldEncounters_Clear(field);
        }
        // Roamers relocate on every zone change, so a sprite left behind no
        // longer matches the roamer's saved route. Ordinary sprites remain.
        for (int i = 0; i < OVERWORLD_ENCOUNTER_CAPACITY; i++) {
            WildSprite *sprite = &manager->sprites[i];
            if (sprite->handle != 0 && (sprite->encounter.battleType & BATTLE_TYPE_ROAMER)) {
                RemoveSprite(field, sprite);
            }
        }
        manager->mapID = field->location->mapHeaderID;
    }
    if (Paused(field)) {
        return;
    }
    int count = 0, freeSlot = -1;
    for (int i = 0; i < OVERWORLD_ENCOUNTER_CAPACITY; i++) {
        WildSprite *sprite = &manager->sprites[i];
        MapObject *object = OverworldPokemon_GetObject(field, sprite->handle);
        if (object != NULL) {
            int dx = Abs(MapObject_GetX(object) - PlayerAvatar_GetXPos(field->playerAvatar));
            int dz = Abs(MapObject_GetZ(object) - PlayerAvatar_GetZPos(field->playerAvatar));
            sprite->age++;
            if (Offscreen(field, object, CULL_MARGIN)
                || !LandDataManager_GetRelativeLoadedMapsQuadrantOfTile(field->landDataMan, MapObject_GetX(object), MapObject_GetZ(object), NULL)
                || (sprite->age > FrameTicks(45 * 30) && !Pokemon_IsShiny(&sprite->encounter.mons[0]) && dx + dz > 2)) {
                RemoveSprite(field, sprite);
                object = NULL;
            }
        }
        if (object == NULL) {
            // Includes actors reclaimed by authored map events.
            memset(sprite, 0, sizeof(*sprite));
            freeSlot = i;
        } else {
            count++;
            Wander(field, sprite, object);
        }
    }
    int spawnLimit = OVERWORLD_ENCOUNTER_DEFAULT_COUNT;
#ifdef SDK_PORT
    spawnLimit = SIM_Config_prj_GetConfig()->overworldEncounterCount;
#endif
    // At the limit, a new spawn takes over an offscreen sprite of another zone.
    // Above it (the limit was lowered) or at 0, nothing new appears.
    int retire = -1;
    if (count >= spawnLimit) {
        retire = spawnLimit > 0 && count == spawnLimit ? FindForeignSprite(field, manager) : -1;
        if (retire < 0) {
            return;
        }
    }
    if (--manager->spawnTicks > 0) {
        return;
    }
    if (freeSlot < 0) {
        if (retire < 0) {
            return;
        }
        RemoveSprite(field, &manager->sprites[retire]);
        freeSlot = retire;
        retire = -1;
    }
    // Retire only once the replacement exists, so failed spawn attempts do not
    // drain the sprites the player may walk back to.
    if (Spawn(field, &manager->sprites[freeSlot], freeSlot) && retire >= 0) {
        RemoveSprite(field, &manager->sprites[retire]);
    }
    manager->spawnTicks = FrameTicks(count < spawnLimit - 2 ? 15 : 90);
}

BOOL OverworldEncounters_TryContact(FieldSystem *field)
{
    OverworldEncounterManager *manager = field->overworldEncounters;
    if (manager == NULL || !Active(field) || Paused(field) || GetRadarChainActive(field->chain)
        || manager->mapID != field->location->mapHeaderID || manager->context != Context(field)) {
        return FALSE;
    }
    int moveState = PlayerAvatar_GetPlayerMoveState(field->playerAvatar);
    if (moveState != PLAYER_MOVE_STATE_NONE && moveState != PLAYER_MOVE_STATE_END) {
        return FALSE;
    }
    MapObject *player = PlayerAvatar_GetMapObject(field->playerAvatar);
    for (int i = 0; i < OVERWORLD_ENCOUNTER_CAPACITY; i++) {
        WildSprite *sprite = &manager->sprites[i];
        MapObject *object = OverworldPokemon_GetObject(field, sprite->handle);
        // Sprites only walk within their own zone, so contact always happens
        // there; this keeps battle backgrounds and met locations correct.
        if (object == NULL || sprite->ownerMap != field->location->mapHeaderID || Repelled(field, sprite)
            || MapObject_IsStatusOnElevatedBridge(player) != MapObject_IsStatusOnElevatedBridge(object)
            || Abs(MapObject_GetY(player) - MapObject_GetY(object)) >= 2) {
            continue;
        }
        int x = MapObject_GetX(player), z = MapObject_GetZ(player);
        if ((MapObject_GetX(object) == x && MapObject_GetZ(object) == z)
            || (sprite->stepping && MapObject_GetXPrev(object) == x && MapObject_GetZPrev(object) == z)) {
            // The battle function copies opponents before this record is
            // cleared. Clearing all spawns also invalidates stale roamers.
            WildEncounters_StartVisibleBattle(field, &sprite->encounter);
            OverworldEncounters_Clear(field);
            return TRUE;
        }
    }
    return FALSE;
}
