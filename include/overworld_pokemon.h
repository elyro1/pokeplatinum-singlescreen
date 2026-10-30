#ifndef POKEPLATINUM_OVERWORLD_POKEMON_H
#define POKEPLATINUM_OVERWORLD_POKEMON_H

#include "struct_decls/map_object.h"
#include "struct_decls/map_object_manager.h"

#include "field/field_system_decl.h"

#include "overworld_pokemon_assets.h"

typedef u32 OverworldPokemonHandle;
#define OVERWORLD_POKEMON_NO_HANDLE  0
#define OVERWORLD_POKEMON_MAX_ACTORS 33

enum OverworldPokemonRole {
    OVERWORLD_POKEMON_WORLD,
    OVERWORLD_POKEMON_TEMPORARY,
    OVERWORLD_POKEMON_PARTY_FOLLOWER,
    OVERWORLD_POKEMON_WILD
};

enum FollowingPokemonSuppression {
    FOLLOWING_POKEMON_SUPPRESS_SCRIPT,
    FOLLOWING_POKEMON_SUPPRESS_FIELD_MOVE,
    FOLLOWING_POKEMON_SUPPRESS_USER,
    FOLLOWING_POKEMON_SUPPRESS_COUNT
};

typedef struct OverworldPokemonSpawn {
    OverworldPokemonAppearance appearance;
    u16 ownerMap;
    u16 ownerID;
    int x, y, z;
    int direction;
    int movementType;
    int rangeX, rangeZ;
    enum OverworldPokemonRole role;
} OverworldPokemonSpawn;

// World/temporary actors are runtime objects. Their caller owns reconstruction
// after full map unload or save/load; ordinary battle/menu returns retain them.
OverworldPokemonHandle OverworldPokemon_Spawn(FieldSystem *fieldSystem, const OverworldPokemonSpawn *spawn);
void OverworldPokemon_Despawn(FieldSystem *fieldSystem, OverworldPokemonHandle handle);
MapObject *OverworldPokemon_GetObject(FieldSystem *fieldSystem, OverworldPokemonHandle handle);
BOOL OverworldPokemon_SetAppearance(FieldSystem *fieldSystem, OverworldPokemonHandle handle, const OverworldPokemonAppearance *appearance);
OverworldPokemonHandle FollowingPokemon_GetHandle(FieldSystem *fieldSystem);
// Also works while the walking follower is absent (Surf, scripts, map load).
int FollowingPokemon_SelectAppearance(FieldSystem *fieldSystem, OverworldPokemonAppearance *appearance, u32 *personality);
void FollowingPokemon_OnSurfBegin(FieldSystem *fieldSystem);
void FollowingPokemon_OnDoorExit(FieldSystem *fieldSystem);
BOOL FollowingPokemon_Suppress(FieldSystem *fieldSystem, enum FollowingPokemonSuppression reason);
void FollowingPokemon_Resume(FieldSystem *fieldSystem, enum FollowingPokemonSuppression reason);

// Field/map-object engine integration. These never set the story partner flag.
void OverworldPokemon_Init(FieldSystem *fieldSystem);
void OverworldPokemon_Free(FieldSystem *fieldSystem);
void OverworldPokemon_OnObjectDeleted(MapObject *object);
BOOL OverworldPokemon_MakeRoom(FieldSystem *fieldSystem);
void OverworldPokemon_Update(FieldSystem *fieldSystem);
void OverworldPokemon_OnFieldExit(FieldSystem *fieldSystem);
fx32 OverworldPokemon_GetVisualScale(MapObject *object);
BOOL OverworldPokemon_TryInteract(FieldSystem *fieldSystem, MapObject *object);
BOOL FollowingPokemon_IgnoreCollision(const MapObject *a, const MapObject *b);
void FollowingPokemon_MovementInit(MapObject *object);
void FollowingPokemon_MovementUpdate(MapObject *object);

#endif
