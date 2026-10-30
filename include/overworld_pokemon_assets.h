#ifndef POKEPLATINUM_OVERWORLD_POKEMON_ASSETS_H
#define POKEPLATINUM_OVERWORLD_POKEMON_ASSETS_H

#include <nitro/types.h>

// A separate graphics namespace preserves every existing Platinum event ID.
#define OVERWORLD_POKEMON_GFX_BASE          0x4000
#define OVERWORLD_POKEMON_VARIANTS          566
#define OVERWORLD_POKEMON_GFX(index, shiny) (OVERWORLD_POKEMON_GFX_BASE + (index) * 2 + (shiny))
#define OVERWORLD_POKEMON_RESOURCE_BASE     0x10000
#define OVERWORLD_POKEMON_SEQUENCE_NORMAL   (OVERWORLD_POKEMON_RESOURCE_BASE + OVERWORLD_POKEMON_VARIANTS)
#define OVERWORLD_POKEMON_SEQUENCE_SHINY    (OVERWORLD_POKEMON_SEQUENCE_NORMAL + 1)
#define OVERWORLD_POKEMON_INVALID_GFX       0xFFFF

typedef struct OverworldPokemonAppearance {
    u16 species;
    u8 form;
    u8 gender;
    BOOL shiny;
} OverworldPokemonAppearance;

u16 OverworldPokemonAssets_Resolve(const OverworldPokemonAppearance *appearance);
BOOL OverworldPokemonAssets_IsGraphicsID(u32 graphicsID);
BOOL OverworldPokemonAssets_IsLarge(u32 graphicsID);
// Visual offset in world units; frame is relative to the facing's loop.
int OverworldPokemonAssets_GetBobOffset(u32 graphicsID, int direction, int frame);
// Frame sequences are embedded; texture callers use ReadResource on all targets.
const void *OverworldPokemonAssets_GetResource(u32 resourceID, u32 *size);
u32 OverworldPokemonAssets_GetResourceSize(u32 resourceID);
BOOL OverworldPokemonAssets_ReadResource(u32 resourceID, void *dest);

#endif
