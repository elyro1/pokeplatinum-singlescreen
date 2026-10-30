#include "overworld_pokemon_assets.h"

#include <nitro.h>
#include <string.h>
#ifdef SDK_PORT
#include "port/hgss_assets.h"
#endif

#include "constants/map_object.h"
#include "constants/species.h"
#include "generated/genders.h"

typedef struct {
    u32 offset;
    u32 size;
    BOOL alternateBob;
    BOOL large;
} PokemonTextureEntry;

typedef struct {
    u16 base;
    u8 maxForm;
    u8 hasFemale;
} PokemonSpeciesEntry;

#include "overworld_pokemon_data.inc"

// HG's eight textures are two frames per direction. Palette 0 is normal,
// palette 1 is shiny. A distinct sequence avoids mutating shared palette data.
typedef struct {
    u32 count;
    u16 frames[9];
    u8 textures[9];
    u8 palettes[9];
} PokemonFrameSequence;

static const PokemonFrameSequence sNormalSequence = {
    9, { 0, 10, 20, 25, 35, 40, 50, 60, 70 }, { 0, 1, 2, 3, 2, 4, 5, 6, 7 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};
static const PokemonFrameSequence sShinySequence = {
    9, { 0, 10, 20, 25, 35, 40, 50, 60, 70 }, { 0, 1, 2, 3, 2, 4, 5, 6, 7 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1 }
};

u16 OverworldPokemonAssets_Resolve(const OverworldPokemonAppearance *appearance)
{
    if (appearance == NULL || appearance->species == SPECIES_NONE || appearance->species > SPECIES_ARCEUS) {
        return OVERWORLD_POKEMON_INVALID_GFX;
    }

    const PokemonSpeciesEntry *entry = &sPokemonSpecies[appearance->species];
    int variant = entry->base;

    if (entry->hasFemale) {
        variant += appearance->gender == GENDER_FEMALE;
    } else if (appearance->form <= entry->maxForm) {
        variant += appearance->form;
    }

    return OVERWORLD_POKEMON_GFX(variant, appearance->shiny != FALSE);
}

BOOL OverworldPokemonAssets_IsGraphicsID(u32 graphicsID)
{
    return graphicsID >= OVERWORLD_POKEMON_GFX_BASE
        && graphicsID < OVERWORLD_POKEMON_GFX_BASE + OVERWORLD_POKEMON_VARIANTS * 2;
}

BOOL OverworldPokemonAssets_IsLarge(u32 graphicsID)
{
    return OverworldPokemonAssets_IsGraphicsID(graphicsID)
        && sPokemonTextures[(graphicsID - OVERWORLD_POKEMON_GFX_BASE) / 2].large;
}

int OverworldPokemonAssets_GetBobOffset(u32 graphicsID, int direction, int frame)
{
    if (!OverworldPokemonAssets_IsGraphicsID(graphicsID) || direction < DIR_NORTH || direction > DIR_EAST) {
        return 0;
    }
    // Match HG/SS's two-unit dip within each facing's 20-frame loop.
    // The alternate shape flag changes which texture receives that offset.
    frame %= 20;
    const PokemonTextureEntry *entry = &sPokemonTextures[(graphicsID - OVERWORLD_POKEMON_GFX_BASE) / 2];
    BOOL dip;
    if (entry->alternateBob) {
        dip = direction == DIR_SOUTH ? (frame < 5 || frame >= 15) : frame < 10;
    } else {
        dip = (frame >= 5 && frame < 10) || frame >= 15;
    }
    return dip ? -2 : 0;
}

const void *OverworldPokemonAssets_GetResource(u32 resourceID, u32 *size)
{
    if (resourceID >= OVERWORLD_POKEMON_RESOURCE_BASE && resourceID < OVERWORLD_POKEMON_SEQUENCE_NORMAL) {
        const PokemonTextureEntry *entry = &sPokemonTextures[resourceID - OVERWORLD_POKEMON_RESOURCE_BASE];
        *size = entry->size;
        // Textures are read from the player's HG/SS extraction on desktop.
        return NULL;
    }

    if (resourceID == OVERWORLD_POKEMON_SEQUENCE_NORMAL || resourceID == OVERWORLD_POKEMON_SEQUENCE_SHINY) {
        *size = sizeof(PokemonFrameSequence);
        return resourceID == OVERWORLD_POKEMON_SEQUENCE_NORMAL ? &sNormalSequence : &sShinySequence;
    }

    *size = 0;
    return NULL;
}

u32 OverworldPokemonAssets_GetResourceSize(u32 resourceID)
{
    u32 size;
    OverworldPokemonAssets_GetResource(resourceID, &size);
    return size;
}

BOOL OverworldPokemonAssets_ReadResource(u32 resourceID, void *dest)
{
#ifdef SDK_PORT
    if (resourceID >= OVERWORLD_POKEMON_RESOURCE_BASE && resourceID < OVERWORLD_POKEMON_SEQUENCE_NORMAL) {
        const PokemonTextureEntry *entry = &sPokemonTextures[resourceID - OVERWORLD_POKEMON_RESOURCE_BASE];
        return HgssAssets_ReadTexture(entry->offset, entry->size, dest);
    }
#endif
    u32 size;
    const void *embedded = OverworldPokemonAssets_GetResource(resourceID, &size);
    if (embedded != NULL) {
        memcpy(dest, embedded, size);
        return TRUE;
    }
    return FALSE;
}
