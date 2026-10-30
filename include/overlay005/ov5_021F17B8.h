#ifndef POKEPLATINUM_OV5_021F17B8_H
#define POKEPLATINUM_OV5_021F17B8_H

#include <nitro/fx/fx.h>

#include "struct_decls/map_object.h"

#include "overlay005/field_effect_manager.h"

#include "billboard.h"
#include "overworld_anim_manager.h"

void *ov5_021F17B8(FieldEffectManager *param0);
void ov5_021F17D4(void *param0);
void ov5_021F1800(MapObject *param0, int param1);
// Borrow the source texture/VRAM; the owner deletes this before its source.
Billboard *PokemonReflection_New(FieldEffectManager *manager, const BillboardResources *resources, const VecFx32 *position);
BOOL PokemonReflection_IsSurface(u8 tile);
void PokemonReflection_Update(Billboard *reflection, const Billboard *source, const VecFx32 *position, BOOL large);
void PokemonReflection_SetRiderLayer(Billboard *rider, Billboard *mount, const MtxFx43 *view, const MtxFx44 *projection);
BOOL PokemonReflection_GetWaterPosition(FieldSystem *fieldSystem, const Billboard *source, const VecFx32 *anchor, VecFx32 *position);
OverworldAnimManager *ov5_021F1BA4(FieldEffectManager *param0, const BillboardResources *param1, const Billboard *param2, const VecFx32 *param3, int param4, int param5);

#endif // POKEPLATINUM_OV5_021F17B8_H
