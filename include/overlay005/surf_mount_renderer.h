#ifndef POKEPLATINUM_SURF_MOUNT_RENDERER_H
#define POKEPLATINUM_SURF_MOUNT_RENDERER_H

#include "struct_decls/map_object.h"

#include "overlay005/field_effect_manager.h"

#include "overworld_anim_manager.h"

// Shared private-texture billboard used by field-move mounts.
typedef struct SurfPokemonVisual PokemonMountVisual;
PokemonMountVisual *PokemonMountVisual_New(MapObject *player, u16 graphicsID, BOOL reflection);
void PokemonMountVisual_Free(PokemonMountVisual *visual);
fx32 PokemonMountVisual_RiderHeight(const PokemonMountVisual *visual);
void PokemonMountVisual_Tick(PokemonMountVisual *visual, BOOL paused);
void PokemonMountVisual_Render(PokemonMountVisual *visual, const VecFx32 *position, int direction, BOOL visible);

void *SurfMountRenderer_New(FieldEffectManager *fieldEffMan);
void SurfMountRenderer_Free(void *context);
void SurfMountRenderer_SelectPokemon(FieldSystem *fieldSystem, int partySlot);
OverworldAnimManager *SurfMountRenderer_HandleSurfBegin(MapObject *playerMapObj, int tileX, int tileZ, int dir, BOOL syncPos);
void SurfMountRenderer_SetSyncPos(OverworldAnimManager *animMan, BOOL syncPos);
// Borrowed for this render only; mount replacement can delete the billboard.
Billboard *SurfMountRenderer_GetReflection(FieldSystem *fieldSystem);

#endif // POKEPLATINUM_SURF_MOUNT_RENDERER_H
