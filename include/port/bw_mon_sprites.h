#ifndef POKEPLATINUM_PORT_BW_MON_SPRITES_H
#define POKEPLATINUM_PORT_BW_MON_SPRITES_H

#ifdef __cplusplus
extern "C" {
#endif

// The BW sprites setting: the summary screen's and the Pokédex's Pokémon, and every Pokémon icon, drawn with BW2's
// art from the player's extraction. Nothing from BW2 is compiled in. Battles and cutscenes keep Platinum's sprites.

// Whether the extraction has everything the setting needs, and looks again after an extraction
int BwMonSprites_Ready(void);
void BwMonSprites_Reload(void);

#ifdef __cplusplus
}
#else

#include <nitro.h>

#include "pokemon.h"
#include "pokemon_sprite.h"

// In a PokemonSpriteTemplate: the sprite comes from BW2's archive, and character and palette are files in that archive
#define MON_SPRITE_NARC_BW 0xFFFF

// BW2's sprites are 96x96. Platinum's are 80x80 and two frames, BW2's a single frame
#define BW_MON_SPRITE_SIZE       96
#define BW_MON_SPRITE_IMAGE_SIZE (BW_MON_SPRITE_SIZE * BW_MON_SPRITE_SIZE / 2)

// The setting, when the extraction allows it
BOOL BwMonSprites_Enabled(void);

// Makes a template for BW2's art. FALSE, leaving the template alone, for a Pokémon that BW2 has no art for
BOOL BwMonSprites_BuildTemplate(PokemonSpriteTemplate *spriteTemplate, u16 species, u8 gender, u8 face, u8 shiny, u8 form, u32 personality);
BOOL BwMonSprites_BuildTemplateFromBoxMon(PokemonSpriteTemplate *spriteTemplate, BoxPokemon *mon, u8 face);

// A template's image: BW_MON_SPRITE_IMAGE_SIZE bytes of rows of 4bpp pixels, left pixel in the low nibble
BOOL BwMonSprites_LoadImage(const PokemonSpriteTemplate *spriteTemplate, u8 *image);
BOOL BwMonSprites_LoadPalette(const PokemonSpriteTemplate *spriteTemplate, u16 *palette);
// The rectangle of a template's art, inclusive. FALSE if it has none
BOOL BwMonSprites_ArtBounds(const PokemonSpriteTemplate *spriteTemplate, int *minX, int *minY, int *maxX, int *maxY);
// How far to move a sprite to put its art's center on the sprite's center. BW2's art isn't placed evenly in its frame
void BwMonSprites_CenterArtOffset(const PokemonSpriteTemplate *spriteTemplate, BOOL flipped, int *xOffset, int *yOffset);
// How far to move a sprite to put its art's bottom where the bottom of the species' Platinum art would be, for a screen
// whose positions are made for Platinum's art
int BwMonSprites_FeetOffset(const PokemonSpriteTemplate *spriteTemplate, u16 species, enum HeapID heapID);

// Whether the icons are BW2's now. Follows the setting each time the icons' palette is loaded, so that a screen's
// palette, tiles and palette numbers all agree when the setting changes with the screen open
BOOL BwMonIcons_Active(void);
u8 BwMonIcons_PaletteIndex(u32 species, u32 form, u32 isEgg);

#endif // __cplusplus

#endif // POKEPLATINUM_PORT_BW_MON_SPRITES_H
