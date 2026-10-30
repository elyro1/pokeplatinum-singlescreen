#include "overworld_pokemon.h"

#include <nitro.h>
#include <string.h>

#include "constants/field/map_load.h"
#include "generated/movement_types.h"
#include "generated/object_events_gfx.h"

#include "field/field_system.h"
#include "overlay005/ov5_021EB1A0.h"

#include "field_system.h"
#include "following_pokemon_interaction.h"
#include "heap.h"
#include "map_header.h"
#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "pokemon_mood.h"
#include "sound_playback.h"
#include "system_flags.h"
#include "terrain_collision_manager.h"
#include "unk_020655F4.h"
#include "vars_flags.h"
#ifdef SDK_PORT
#include "port/sim_config_prj.h"
#include "port/hgss_assets.h"
#endif

#define FOLLOW_TRAIL_LENGTH       32
#define FOLLOW_BLOCKED_FRAMES     60
#define FOLLOW_PARTY_CHECK_FRAMES 32
#define AMBIENT_OBJECT_RESERVE    4

typedef struct {
    OverworldPokemonHandle handle;
    MapObject *object;
    OverworldPokemonAppearance appearance;
    u16 ownerMap;
    u16 ownerID;
    enum OverworldPokemonRole role;
    BOOL recalling;
    int effectTicks, effectDuration;
} PokemonActor;

typedef struct {
    s16 x, y, z;
    u16 action;
    BOOL onBridge;
    BOOL jump;
} FollowStep;

typedef struct {
    BOOL stepping;
} FollowMovement;

typedef struct OverworldPokemonManager {
    PokemonActor actors[OVERWORLD_POKEMON_MAX_ACTORS];
    OverworldPokemonHandle follower;
    FollowStep trail[FOLLOW_TRAIL_LENGTH];
    int trailHead;
    int trailCount;
    int playerX, playerZ;
    BOOL playerOnBridge;
    int blockedFrames;
    int partyCheckFrames;
    int cryCooldown;
    int partySlot;
    u32 personality;
    u16 graphicsID;
    OverworldPokemonAppearance selected;
    BOOL wasSuspended;
    BOOL recoveryPending;
    BOOL waitingForExitStep;
} OverworldPokemonManager;

static PokemonActor *FindActor(FieldSystem *fieldSystem, OverworldPokemonHandle handle)
{
    if (fieldSystem == NULL || fieldSystem->overworldPokemon == NULL || handle == OVERWORLD_POKEMON_NO_HANDLE) {
        return NULL;
    }
    for (int i = 0; i < OVERWORLD_POKEMON_MAX_ACTORS; i++) {
        PokemonActor *actor = &fieldSystem->overworldPokemon->actors[i];
        if (actor->handle == handle) {
            return actor;
        }
    }
    return NULL;
}

static int EffectDuration(void)
{
#ifdef SDK_PORT
    return SIM_Config_prj_GetConfig()->enable60fps ? 16 : 8;
#else
    return 8;
#endif
}

fx32 OverworldPokemon_GetVisualScale(MapObject *object)
{
    PokemonActor *actor = FindActor(MapObject_FieldSystem(object), MapObject_GetPokemonActorHandle(object));
    if (actor == NULL || actor->effectDuration == 0) {
        return FX32_ONE;
    }
    int ticks = actor->recalling ? actor->effectTicks : actor->effectDuration - actor->effectTicks;
    return FX32_ONE * ticks / actor->effectDuration;
}

static void RecallFollower(FieldSystem *fieldSystem)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    PokemonActor *actor = FindActor(fieldSystem, manager->follower);
    if (actor != NULL) {
        actor->recalling = TRUE;
        actor->effectTicks = actor->effectDuration = EffectDuration();
        FollowingPokemon_StartBallEffect(actor->object, FALSE);
        // Retain only the visual for the short recall. Cutscene actors must
        // be able to walk through its tile immediately, including mid-jump.
        MapObject_SetStatusFlagOn(actor->object, MAP_OBJ_STATUS_18);
        MapObject_SetPauseMovementOn(actor->object);
        manager->follower = 0;
    }
}

static void UpdateEffects(FieldSystem *fieldSystem, BOOL allowed)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    for (int i = 0; i < OVERWORLD_POKEMON_MAX_ACTORS; i++) {
        PokemonActor *actor = &manager->actors[i];
        if (actor->handle == 0 || actor->effectTicks == 0) {
            continue;
        }
        actor->effectTicks--;
        if (actor->recalling && (actor->effectTicks == 0 || allowed)) {
            OverworldPokemon_Despawn(fieldSystem, actor->handle);
        }
    }
}

void OverworldPokemon_Init(FieldSystem *fieldSystem)
{
    GF_ASSERT(fieldSystem->overworldPokemon == NULL);
    OverworldPokemonManager *manager = Heap_Alloc(HEAP_ID_FIELD2, sizeof(OverworldPokemonManager));
    memset(manager, 0, sizeof(*manager));
    manager->graphicsID = OVERWORLD_POKEMON_INVALID_GFX;
    manager->wasSuspended = TRUE;
    fieldSystem->overworldPokemon = manager;
}

void OverworldPokemon_Free(FieldSystem *fieldSystem)
{
    FollowingPokemon_CancelInteraction(fieldSystem);
    if (fieldSystem->overworldPokemon != NULL) {
        Heap_Free(fieldSystem->overworldPokemon);
        fieldSystem->overworldPokemon = NULL;
    }
}

MapObject *OverworldPokemon_GetObject(FieldSystem *fieldSystem, OverworldPokemonHandle handle)
{
    PokemonActor *actor = FindActor(fieldSystem, handle);
    return actor != NULL ? actor->object : NULL;
}

OverworldPokemonHandle OverworldPokemon_Spawn(FieldSystem *fieldSystem, const OverworldPokemonSpawn *spawn)
{
    if (fieldSystem == NULL || spawn == NULL || fieldSystem->overworldPokemon == NULL
        || fieldSystem->mapObjMan == NULL || !MapObjectMan_IsDrawInitialized(fieldSystem->mapObjMan)
        || spawn->direction < DIR_NORTH || spawn->direction > DIR_EAST
        || spawn->role < OVERWORLD_POKEMON_WORLD || spawn->role > OVERWORLD_POKEMON_WILD
        || spawn->movementType < MOVEMENT_TYPE_NONE || spawn->movementType >= MAX_MOVEMENT_TYPE
        || spawn->movementType == MOVEMENT_TYPE_PLAYER || spawn->movementType == MOVEMENT_TYPE_FOLLOW_PLAYER
        || spawn->movementType == MOVEMENT_TYPE_FOLLOW_PARTNER_TRAINER
        || ((spawn->movementType == MOVEMENT_TYPE_FOLLOW_PARTY_POKEMON) != (spawn->role == OVERWORLD_POKEMON_PARTY_FOLLOWER))
        || spawn->rangeX < -1 || spawn->rangeZ < -1) {
        return OVERWORLD_POKEMON_NO_HANDLE;
    }
    // Tile centers must fit the engine's signed 20.12 world coordinates.
    if (spawn->ownerMap >= MAP_HEADER_COUNT || spawn->x < 0 || spawn->x > 0x7FFF
        || spawn->z < 0 || spawn->z > 0x7FFF || spawn->y < -0xFFFF || spawn->y > 0xFFFF
        || spawn->rangeX > 0x7FFF || spawn->rangeZ > 0x7FFF
        || (spawn->movementType >= MOVEMENT_TYPE_BERRY_SOIL && spawn->movementType != MOVEMENT_TYPE_FOLLOW_PARTY_POKEMON)) {
        return OVERWORLD_POKEMON_NO_HANDLE;
    }

    u16 graphicsID = OverworldPokemonAssets_Resolve(&spawn->appearance);
    if (graphicsID == OVERWORLD_POKEMON_INVALID_GFX) {
        return OVERWORLD_POKEMON_NO_HANDLE;
    }

    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    PokemonActor *slot = NULL;
    int slotIndex = 0;
    for (int i = 0; i < OVERWORLD_POKEMON_MAX_ACTORS; i++) {
        PokemonActor *actor = &manager->actors[i];
        if (actor->handle != 0 && actor->role == spawn->role
            && actor->ownerMap == spawn->ownerMap && actor->ownerID == spawn->ownerID) {
            return actor->handle;
        }
        if (slot == NULL && actor->handle == 0) {
            slot = actor;
            slotIndex = i;
        }
    }
    if (slot == NULL || (spawn->role == OVERWORLD_POKEMON_PARTY_FOLLOWER && manager->follower != 0)) {
        return OVERWORLD_POKEMON_NO_HANDLE;
    }

    int index = 0, used = 0;
    MapObject *object;
    while (MapObjectMan_FindObjectWithStatus(fieldSystem->mapObjMan, &object, &index, MAP_OBJ_STATUS_0)) {
        used++;
    }
    int reserve = spawn->role == OVERWORLD_POKEMON_PARTY_FOLLOWER ? 0 : AMBIENT_OBJECT_RESERVE;
    if (MapObjectMan_GetMaxObjects(fieldSystem->mapObjMan) - used <= reserve
        || fieldSystem->nextPokemonActorHandle == 0xFFFFFFFF) {
        return OVERWORLD_POKEMON_NO_HANDLE;
    }

    OverworldPokemonHandle handle = ++fieldSystem->nextPokemonActorHandle;
    ObjectEvent event;
    memset(&event, 0, sizeof(event));
    event.localID = 0xFF00 + slotIndex;
    event.graphicsID = graphicsID;
    event.movementType = spawn->movementType;
    event.dir = spawn->direction;
    event.movementRangeX = spawn->rangeX;
    event.movementRangeZ = spawn->rangeZ;
    event.x = spawn->x;
    event.y = spawn->y * (8 * FX32_ONE);
    event.z = spawn->z;
    object = MapObjectMan_AddPokemonActor(fieldSystem->mapObjMan, &event, spawn->ownerMap, handle);
    if (object == NULL) {
        return OVERWORLD_POKEMON_NO_HANDLE;
    }

    slot->handle = handle;
    slot->object = object;
    slot->appearance = spawn->appearance;
    slot->ownerMap = spawn->ownerMap;
    slot->ownerID = spawn->ownerID;
    slot->role = spawn->role;
    // These actors outlive connected-zone event replacement; full map teardown
    // destroys them normally. They are excluded from the object save snapshot.
    MapObject_SetFlagIsPersistent(object, TRUE);
    MapObject_SetDynamicHeightCalculationEnabled(object, TRUE);
    MapObject_RecalculateObjectHeight(object);
    if (spawn->role == OVERWORLD_POKEMON_PARTY_FOLLOWER) {
        manager->follower = handle;
        if (!manager->waitingForExitStep) {
            slot->effectTicks = slot->effectDuration = EffectDuration();
            FollowingPokemon_StartBallEffect(object, TRUE);
        }
    }
    return handle;
}

void OverworldPokemon_OnObjectDeleted(MapObject *object)
{
    OverworldPokemonHandle handle = MapObject_GetPokemonActorHandle(object);
    if (handle == 0) {
        return;
    }
    FieldSystem *fieldSystem = MapObject_FieldSystem(object);
    PokemonActor *actor = FindActor(fieldSystem, handle);
    if (actor != NULL) {
        if (fieldSystem->overworldPokemon->follower == handle) {
            fieldSystem->overworldPokemon->follower = 0;
        }
        memset(actor, 0, sizeof(*actor));
    }
}

void OverworldPokemon_Despawn(FieldSystem *fieldSystem, OverworldPokemonHandle handle)
{
    MapObject *object = OverworldPokemon_GetObject(fieldSystem, handle);
    if (object != NULL) {
        MapObject_Delete(object);
    }
}

BOOL OverworldPokemon_MakeRoom(FieldSystem *fieldSystem)
{
    if (fieldSystem->overworldPokemon == NULL) {
        return FALSE;
    }
    // A visual-only recall never takes priority over an authored story actor.
    for (int i = 0; i < OVERWORLD_POKEMON_MAX_ACTORS; i++) {
        PokemonActor *actor = &fieldSystem->overworldPokemon->actors[i];
        if (actor->handle != 0 && actor->recalling) {
            OverworldPokemon_Despawn(fieldSystem, actor->handle);
            return TRUE;
        }
    }
    // Authored story objects have priority over optional ambient actors. Work
    // backwards so the oldest authored runtime actors are retained longest.
    for (int i = OVERWORLD_POKEMON_MAX_ACTORS - 1; i >= 0; i--) {
        PokemonActor *actor = &fieldSystem->overworldPokemon->actors[i];
        if (actor->handle != 0 && actor->role != OVERWORLD_POKEMON_PARTY_FOLLOWER) {
            OverworldPokemon_Despawn(fieldSystem, actor->handle);
            return TRUE;
        }
    }
    if (fieldSystem->overworldPokemon->follower != 0) {
        OverworldPokemon_Despawn(fieldSystem, fieldSystem->overworldPokemon->follower);
        return TRUE;
    }
    return FALSE;
}

BOOL OverworldPokemon_SetAppearance(FieldSystem *fieldSystem, OverworldPokemonHandle handle, const OverworldPokemonAppearance *appearance)
{
    PokemonActor *actor = FindActor(fieldSystem, handle);
    u16 graphicsID = OverworldPokemonAssets_Resolve(appearance);
    if (actor == NULL || graphicsID == OVERWORLD_POKEMON_INVALID_GFX) {
        return FALSE;
    }
    if (MapObject_GetGraphicsID(actor->object) != graphicsID) {
        sub_02061AD4(actor->object, graphicsID);
    }
    actor->appearance = *appearance;
    return TRUE;
}

OverworldPokemonHandle FollowingPokemon_GetHandle(FieldSystem *fieldSystem)
{
    return fieldSystem->overworldPokemon != NULL ? fieldSystem->overworldPokemon->follower : 0;
}

BOOL FollowingPokemon_Suppress(FieldSystem *fieldSystem, enum FollowingPokemonSuppression reason)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    if (manager == NULL || reason < 0 || reason >= FOLLOWING_POKEMON_SUPPRESS_COUNT
        || fieldSystem->followingPokemonSuppression[reason] == 0xFFFF) {
        return FALSE;
    }
    fieldSystem->followingPokemonSuppression[reason]++;
    RecallFollower(fieldSystem);
    manager->wasSuspended = TRUE;
    return TRUE;
}

void FollowingPokemon_Resume(FieldSystem *fieldSystem, enum FollowingPokemonSuppression reason)
{
    if (fieldSystem != NULL && reason >= 0 && reason < FOLLOWING_POKEMON_SUPPRESS_COUNT && fieldSystem->followingPokemonSuppression[reason] != 0) {
        fieldSystem->followingPokemonSuppression[reason]--;
    }
}

static BOOL FollowerAllowed(FieldSystem *fieldSystem)
{
#ifdef SDK_PORT
    if (!SIM_Config_prj_GetConfig()->pokemonFollower || !HgssAssets_Ready()) return FALSE;
#endif
    MapObject *player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
    BOOL sliding = TileBehavior_IsIce(MapObject_GetCurrTileBehavior(player))
        || TileBehavior_IsIce(MapObject_GetPrevTileBehavior(player));
    if (fieldSystem->mapLoadType != MAP_LOAD_TYPE_OVERWORLD
        || PlayerAvatar_GetPlayerState(fieldSystem->playerAvatar) != PLAYER_AVATAR_WALKING
        || ((fieldSystem->playerAvatar->movementFlag & AVATAR_MOVE_FORCED) && !sliding)
        || PlayerAvatar_MapDistortionState(fieldSystem->playerAvatar) != AVATAR_DISTORTION_STATE_NONE
        || MapHeader_IsAmitySquare(fieldSystem->location->mapHeaderID)
        || MapHeader_IsUnionRoom(fieldSystem->location->mapHeaderID)
        || SystemFlag_CheckSafariGameActive(SaveData_GetVarsFlags(fieldSystem->saveData))
        || SystemFlag_CheckInPalPark(SaveData_GetVarsFlags(fieldSystem->saveData))
        || SystemFlag_CheckHasPartner(SaveData_GetVarsFlags(fieldSystem->saveData))) {
        return FALSE;
    }
    u32 graphicsID = MapObject_GetGraphicsID(PlayerAvatar_GetMapObject(fieldSystem->playerAvatar));
    if (graphicsID != OBJ_EVENT_GFX_PLAYER_M && graphicsID != OBJ_EVENT_GFX_PLAYER_F
        && graphicsID != OBJ_EVENT_GFX_DP_PLAYER_M && graphicsID != OBJ_EVENT_GFX_DP_PLAYER_F) {
        return FALSE;
    }
    for (int i = 0; i < FOLLOWING_POKEMON_SUPPRESS_COUNT; i++) {
        if (fieldSystem->followingPokemonSuppression[i] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static void ResetTrail(FieldSystem *fieldSystem)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    manager->trailHead = manager->trailCount = manager->blockedFrames = 0;
    manager->playerX = PlayerAvatar_GetXPos(fieldSystem->playerAvatar);
    manager->playerZ = PlayerAvatar_GetZPos(fieldSystem->playerAvatar);
    manager->playerOnBridge = MapObject_IsStatusOnElevatedBridge(PlayerAvatar_GetMapObject(fieldSystem->playerAvatar));
    manager->recoveryPending = FALSE;
}

void FollowingPokemon_OnDoorExit(FieldSystem *fieldSystem)
{
    FollowingPokemon_CancelInteraction(fieldSystem);
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    if (manager != NULL) {
        OverworldPokemon_Despawn(fieldSystem, manager->follower);
        manager->waitingForExitStep = TRUE;
        ResetTrail(fieldSystem);
    }
}

int FollowingPokemon_SelectAppearance(FieldSystem *fieldSystem, OverworldPokemonAppearance *appearance, u32 *personality)
{
    Party *party = SaveData_GetParty(fieldSystem->saveData);
    memset(appearance, 0, sizeof(*appearance));
    *personality = 0;
    for (int i = 0; i < Party_GetCurrentCount(party); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        if (Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL) || Pokemon_GetValue(mon, MON_DATA_HP, NULL) == 0) {
            continue;
        }
        appearance->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
        appearance->form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
        appearance->gender = Pokemon_GetGender(mon);
        appearance->shiny = Pokemon_IsShiny(mon);
        *personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
        return i;
    }
    return -1;
}

void FollowingPokemon_OnSurfBegin(FieldSystem *fieldSystem)
{
    FollowingPokemon_CancelInteraction(fieldSystem);
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    if (manager == NULL) {
        return;
    }
    // Transfer presentation to the mount atomically, including a follower
    // already recalling for a script. World Pokemon remain independent.
    for (int i = 0; i < OVERWORLD_POKEMON_MAX_ACTORS; i++) {
        PokemonActor *actor = &manager->actors[i];
        if (actor->handle != 0 && actor->role == OVERWORLD_POKEMON_PARTY_FOLLOWER) {
            OverworldPokemon_Despawn(fieldSystem, actor->handle);
        }
    }
    manager->wasSuspended = TRUE;
    manager->waitingForExitStep = FALSE;
    ResetTrail(fieldSystem);
}

static void RefreshParty(FieldSystem *fieldSystem)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    OverworldPokemonAppearance appearance;
    u32 personality;
    manager->partySlot = FollowingPokemon_SelectAppearance(fieldSystem, &appearance, &personality);
    u16 graphicsID = OverworldPokemonAssets_Resolve(&appearance);
    if (manager->graphicsID != graphicsID || manager->personality != personality) {
        OverworldPokemon_Despawn(fieldSystem, manager->follower);
        manager->graphicsID = graphicsID;
        manager->personality = personality;
        manager->selected = appearance;
        ResetTrail(fieldSystem);
    }
}

static BOOL SpawnFollowerAtTile(FieldSystem *fieldSystem, int x, int y, int z, int facing)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    OverworldPokemonSpawn spawn = {
        .appearance = manager->selected,
        .ownerMap = fieldSystem->location->mapHeaderID,
        .ownerID = 0xFFFF,
        .x = x,
        .y = y,
        .z = z,
        .direction = facing,
        .movementType = MOVEMENT_TYPE_FOLLOW_PARTY_POKEMON,
        .rangeX = -1,
        .rangeZ = -1,
        .role = OVERWORLD_POKEMON_PARTY_FOLLOWER
    };
    if (OverworldPokemon_Spawn(fieldSystem, &spawn) == 0) {
        return FALSE;
    }
    ResetTrail(fieldSystem);
    return TRUE;
}

static BOOL SpawnFollower(FieldSystem *fieldSystem)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    MapObject *player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
    if (manager->graphicsID == OVERWORLD_POKEMON_INVALID_GFX || MapObject_IsMoving(player)) {
        return FALSE;
    }
    int facing = PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar);
    int dirs[] = { Direction_GetOpposite(facing), DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST };
    for (int i = 0; i < NELEMS(dirs); i++) {
        int x = MapObject_GetX(player) + MapObject_GetDxFromDir(dirs[i]);
        int z = MapObject_GetZ(player) + MapObject_GetDzFromDir(dirs[i]);
        if (x < 0 || z < 0 || x > 0x7FFF || z > 0x7FFF) {
            continue;
        }
        u8 tile = TerrainCollisionManager_GetTileBehavior(fieldSystem, x, z);
        if (TileBehavior_IsDoor(tile) || TileBehavior_IsWarpPanel(tile)
            || TileBehavior_IsWarpNorth(tile) || TileBehavior_IsWarpSouth(tile)
            || TileBehavior_IsWarpEast(tile) || TileBehavior_IsWarpWest(tile)
            || TileBehavior_IsWarpStairsEast(tile) || TileBehavior_IsWarpStairsWest(tile)) {
            continue;
        }
        // Do not materialize through walls, on water, or on another actor.
        if (sub_02063E94(player, x, MapObject_GetY(player), z, dirs[i]) != MAP_OBJ_COLLISION_NONE) {
            continue;
        }
        return SpawnFollowerAtTile(fieldSystem, x, MapObject_GetY(player), z, facing);
    }
    return FALSE;
}

void OverworldPokemon_Update(FieldSystem *fieldSystem)
{
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    if (manager == NULL || fieldSystem->playerAvatar == NULL) {
        return;
    }
    if (manager->cryCooldown > 0) {
        manager->cryCooldown--;
    }
    BOOL allowed = FollowerAllowed(fieldSystem);
    UpdateEffects(fieldSystem, allowed);
    if (!allowed) {
        RecallFollower(fieldSystem);
        manager->wasSuspended = TRUE;
        return;
    }
    if (manager->waitingForExitStep) {
        MapObject *player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
        int x = manager->playerX, z = manager->playerZ;
        int dx = MapObject_GetX(player) - x, dz = MapObject_GetZ(player) - z;
        if (fieldSystem->processManager->pause || FieldSystem_IsRunningTask(fieldSystem)) {
            // The automatic step out of the doorway is not the reveal step.
            ResetTrail(fieldSystem);
            return;
        }
        if (dx == 0 && dz == 0) {
            return;
        }
        // HG/SS hides the follower until the next player step. Materialize on
        // the vacated exit tile, never on the closed door or an adjacent tile.
        // A teleport is not a step and must establish a new origin instead.
        if ((dx == 0 && (dz == -1 || dz == 1)) || (dz == 0 && (dx == -1 || dx == 1))) {
            RefreshParty(fieldSystem);
            if (SpawnFollowerAtTile(fieldSystem, x, MapObject_GetYPrev(player), z,
                    PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar))) {
                manager->waitingForExitStep = FALSE;
                manager->wasSuspended = FALSE;
                manager->partyCheckFrames = FOLLOW_PARTY_CHECK_FRAMES;
            }
        }
        ResetTrail(fieldSystem);
        return;
    }
    // Menus and encounter intros use field tasks while the map is still
    // visible. Keep the existing actor and trail; map-object movement pauses
    // with the rest of the field. Refresh party changes when control returns.
    // A missing follower must still be reconstructed during a task so it is
    // present when the field fades back in after a battle/application.
    if (fieldSystem->processManager->pause || FieldSystem_IsRunningTask(fieldSystem)) {
        manager->partyCheckFrames = 0;
        if (manager->follower != 0) {
            // Native tasks can move the player without recording a trail.
            // Recover from the final position when control returns.
            if (PlayerAvatar_GetXPos(fieldSystem->playerAvatar) != manager->playerX
                || PlayerAvatar_GetZPos(fieldSystem->playerAvatar) != manager->playerZ) {
                manager->recoveryPending = TRUE;
            }
            return;
        }
    }
    if (manager->wasSuspended) {
        manager->partyCheckFrames = 0;
        manager->wasSuspended = FALSE;
        ResetTrail(fieldSystem);
    }
    if (manager->partyCheckFrames-- <= 0) {
        RefreshParty(fieldSystem);
        manager->partyCheckFrames = FOLLOW_PARTY_CHECK_FRAMES;
    }
    if (manager->recoveryPending) {
        OverworldPokemon_Despawn(fieldSystem, manager->follower);
        ResetTrail(fieldSystem);
    }
    if (manager->follower == 0) {
        SpawnFollower(fieldSystem);
        return;
    }

    MapObject *player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
    int x = MapObject_GetX(player), z = MapObject_GetZ(player);
    if (x == manager->playerX && z == manager->playerZ) {
        return;
    }
    int dx = x - manager->playerX, dz = z - manager->playerZ;
    u32 playerAction = PlayerAvatar_GetMovementAction(fieldSystem->playerAvatar);
    BOOL farJump = playerAction >= MOVEMENT_ACTION_JUMP_FAR_NORTH && playerAction <= MOVEMENT_ACTION_JUMP_FAR_EAST;
    BOOL jump = farJump || (playerAction >= MOVEMENT_ACTION_JUMP_NEAR_FAST_NORTH && playerAction <= MOVEMENT_ACTION_JUMP_NEAR_FAST_EAST);
    // A far jump advances logical coordinates once at takeoff and again
    // halfway through its arc. Never record the intermediate ledge tile.
    if (farJump && ((dx == 0 && (dz == -1 || dz == 1)) || (dz == 0 && (dx == -1 || dx == 1)))) {
        return;
    }
    // Warps/forced repositioning invalidate the path rather than making the
    // follower walk through whatever happens to lie between the two points.
    if (manager->trailCount > FOLLOW_TRAIL_LENGTH - (jump ? 2 : 1) || (dx != 0 && dz != 0)
        || dx < -2 || dx > 2 || dz < -2 || dz > 2
        || (!jump && (dx < -1 || dx > 1 || dz < -1 || dz > 1))) {
        manager->recoveryPending = TRUE;
        return;
    }
    if (manager->partySlot >= 0) {
        PokemonMood_StepTowardsNeutral(Party_GetPokemonBySlotIndex(SaveData_GetParty(fieldSystem->saveData), manager->partySlot));
    }
    FollowStep *step = &manager->trail[(manager->trailHead + manager->trailCount) % FOLLOW_TRAIL_LENGTH];
    step->x = manager->playerX;
    step->z = manager->playerZ;
    step->y = MapObject_GetYPrev(player);
    step->onBridge = manager->playerOnBridge;
    // Coordinates advance when the player's action starts. This action moves
    // out of the recorded tile; the previous action moved into it.
    step->action = playerAction;
    step->jump = FALSE;
    manager->trailCount++;
    if (jump) {
        // Follow to the takeoff tile, then replay the validated jump even if
        // the player stops on landing instead of vacating another tile.
        step = &manager->trail[(manager->trailHead + manager->trailCount) % FOLLOW_TRAIL_LENGTH];
        *step = (FollowStep) {
            .x = x, .y = MapObject_GetY(player), .z = z,
            .action = playerAction,
            .onBridge = MapObject_IsStatusOnElevatedBridge(player),
            .jump = TRUE,
        };
        manager->trailCount++;
    }
    manager->playerX = x;
    manager->playerZ = z;
    manager->playerOnBridge = MapObject_IsStatusOnElevatedBridge(player);
}

void OverworldPokemon_OnFieldExit(FieldSystem *fieldSystem)
{
    FollowingPokemon_CancelInteraction(fieldSystem);
    if (fieldSystem->overworldPokemon != NULL) {
        // Teardown cannot leave visual-only recall objects referencing field
        // resources that are about to be freed.
        for (int i = 0; i < OVERWORLD_POKEMON_MAX_ACTORS; i++) {
            PokemonActor *actor = &fieldSystem->overworldPokemon->actors[i];
            if (actor->role == OVERWORLD_POKEMON_PARTY_FOLLOWER && actor->handle != 0) {
                OverworldPokemon_Despawn(fieldSystem, actor->handle);
            }
        }
        fieldSystem->overworldPokemon->wasSuspended = TRUE;
        fieldSystem->overworldPokemon->waitingForExitStep = FALSE;
    }
}

BOOL FollowingPokemon_IgnoreCollision(const MapObject *a, const MapObject *b)
{
    int aType = MapObject_GetMovementType(a), bType = MapObject_GetMovementType(b);
    PokemonActor *aActor = FindActor(MapObject_FieldSystem(a), MapObject_GetPokemonActorHandle(a));
    PokemonActor *bActor = FindActor(MapObject_FieldSystem(b), MapObject_GetPokemonActorHandle(b));
    BOOL aWild = aActor != NULL && aActor->role == OVERWORLD_POKEMON_WILD;
    BOOL bWild = bActor != NULL && bActor->role == OVERWORLD_POKEMON_WILD;
    // Contact encounters must be walkable. Story actors may also pass wild
    // sprites; wild sprites still respect other wild sprites and authored NPCs.
    if ((aWild && bType == MOVEMENT_TYPE_PLAYER) || (bWild && !aWild)) {
        return TRUE;
    }
    return (aType == MOVEMENT_TYPE_PLAYER && bType == MOVEMENT_TYPE_FOLLOW_PARTY_POKEMON)
        || (bType == MOVEMENT_TYPE_PLAYER && aType == MOVEMENT_TYPE_FOLLOW_PARTY_POKEMON);
}

BOOL OverworldPokemon_TryInteract(FieldSystem *fieldSystem, MapObject *object)
{
    PokemonActor *actor = FindActor(fieldSystem, MapObject_GetPokemonActorHandle(object));
    if (actor == NULL) {
        return FALSE;
    }
    if (actor->role == OVERWORLD_POKEMON_PARTY_FOLLOWER) {
        OverworldPokemonHandle handle = actor->handle;
        RefreshParty(fieldSystem);
        if (FollowingPokemon_GetHandle(fieldSystem) != handle) {
            return TRUE;
        }
        return FollowingPokemon_StartInteraction(fieldSystem, actor->handle, fieldSystem->overworldPokemon->partySlot);
    }
    if (fieldSystem->overworldPokemon->cryCooldown == 0) {
        MapObject_TryFace(object, Direction_GetOpposite(PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar)));
        Sound_PlayPokemonCry(actor->appearance.species, actor->appearance.form);
        fieldSystem->overworldPokemon->cryCooldown = 30;
    }
    return TRUE;
}

void FollowingPokemon_MovementInit(MapObject *object)
{
    // The action executor owns movementData while walking/jumping. The
    // controller state must live in the movement-type work area instead.
    sub_02062A54(object, sizeof(FollowMovement));
    sub_02062A0C(object, 0);
    sub_02062D10(object);
    sub_02062D80(object, TRUE);
}

void FollowingPokemon_MovementUpdate(MapObject *object)
{
    FieldSystem *fieldSystem = MapObject_FieldSystem(object);
    OverworldPokemonManager *manager = fieldSystem->overworldPokemon;
    FollowMovement *movement = sub_02062A78(object);
    if (manager == NULL || manager->follower != MapObject_GetPokemonActorHandle(object)) {
        return;
    }
    if (movement->stepping) {
        if (!sub_020658DC(object)) {
            return;
        }
        movement->stepping = FALSE;
        sub_02062D10(object);
    }
    sub_02062D80(object, TRUE);
    // MapObject_Move still needs END_MOVEMENT for terrain/shadow cleanup
    // after this controller returns, even if we start the next queued action.
    while (manager->trailCount > 0) {
        FollowStep *step = &manager->trail[manager->trailHead];
        int dx = step->x - MapObject_GetX(object), dz = step->z - MapObject_GetZ(object);
        if (dx == 0 && dz == 0) {
            manager->trailHead = (manager->trailHead + 1) % FOLLOW_TRAIL_LENGTH;
            manager->trailCount--;
            continue;
        }
        int distance = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
        if ((dx != 0 && dz != 0) || distance > 2) {
            manager->recoveryPending = TRUE;
            return;
        }
        int dir = GetDirectionBetweenPoints(MapObject_GetX(object), MapObject_GetZ(object), step->x, step->z);
        // The player already validated this ledge crossing. Walking terrain
        // checks can reject its height change; still check landing occupants.
        u32 collision = step->jump
            ? (sub_02063F00(object, step->x, step->y, step->z) ? MAP_OBJ_COLLISION_2 : MAP_OBJ_COLLISION_NONE)
            : sub_02063E94(object, step->x, step->y, step->z, dir);
        if (collision & (MAP_OBJ_COLLISION_WILL_COLLIDE | MAP_OBJ_COLLISION_2)) {
            if (++manager->blockedFrames >= FOLLOW_BLOCKED_FRAMES) {
                manager->recoveryPending = TRUE;
            }
            return;
        }
        manager->blockedFrames = 0;
        enum MovementAction action = MOVEMENT_ACTION_WALK_NORMAL_NORTH;
        if (step->jump) {
            action = distance == 2 ? MOVEMENT_ACTION_JUMP_FAR_NORTH : MOVEMENT_ACTION_JUMP_NEAR_FAST_NORTH;
        } else if (distance != 1) {
            manager->recoveryPending = TRUE;
            return;
        } else if (step->action >= MOVEMENT_ACTION_RUN_NORTH && step->action <= MOVEMENT_ACTION_RUN_EAST) {
            // Match running's speed/duration using an NPC animation state.
            // RUN selects player-only state 9, outside this sprite renderer.
            action = MOVEMENT_ACTION_WALK_FAST_NORTH;
        } else if (step->action >= MOVEMENT_ACTION_WALK_FAST_NORTH && step->action <= MOVEMENT_ACTION_WALK_FAST_EAST) {
            action = MOVEMENT_ACTION_WALK_FAST_NORTH;
        } else if (step->action >= MOVEMENT_ACTION_WALK_FASTER_NORTH && step->action <= MOVEMENT_ACTION_WALK_FASTER_EAST) {
            action = MOVEMENT_ACTION_WALK_FASTER_NORTH;
        } else {
            static const u16 walkBases[] = {
                MOVEMENT_ACTION_WALK_SLOWER_NORTH, MOVEMENT_ACTION_WALK_SLOW_NORTH,
                MOVEMENT_ACTION_WALK_SLIGHTLY_FAST_NORTH, MOVEMENT_ACTION_WALK_SLIGHTLY_FASTER_NORTH,
                MOVEMENT_ACTION_WALK_FASTEST_NORTH, MOVEMENT_ACTION_WALK_EVER_SO_SLIGHTLY_FAST_NORTH
            };
            for (int i = 0; i < NELEMS(walkBases); i++) {
                if (step->action >= walkBases[i] && step->action <= walkBases[i] + DIR_EAST) {
                    action = walkBases[i];
                    break;
                }
            }
        }
        MapObject_SetElevatedBridgeStatus(object, step->onBridge);
        sub_02065668(object, action + dir);
        sub_02062D04(object);
        movement->stepping = TRUE;
        manager->trailHead = (manager->trailHead + 1) % FOLLOW_TRAIL_LENGTH;
        manager->trailCount--;
        sub_020658DC(object);
        return;
    }
}
