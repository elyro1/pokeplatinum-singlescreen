#include "types.h"
#include "constants/pokemon.h"
#include "nnsys/g2d.h"
#include "pml/poke_graphic.h"
#include "pml/poke_party.h"
#include "system/mcss.h"

// The functions that fill in the files of a Pokémon's or a trainer's sprite for MCSS, and Spinda's spots. They follow
// mcss.c and are taken as a file of their own, whose name is a guess: the `.data` has mcss.c's name before this
// file's table of spots, where a file's literals come after its tables. SetupPokemonLoaderByBoxData,
// SetupPokemonLoaderFSTool and GetTrainerSpriteResIDs are swan's names

// A pixel of a spot in the sprite's 64x64 body, from the spot's place in the personality value
typedef struct {
    u8 x;
    u8 y;
} SpindaSpotPixel;

#define SPINDA_SPOT_COUNT 4
#define SPINDA_SPOT_END 0xff

// The bin file of Spinda's sprite in the Pokémon graphics archive
#define POKEGRA_SPINDA_BIN 6548

// The sprite's rows are 128 bytes, and its second frame is 40 rows below the first
#define SPINDA_ROW_SIZE 0x80
#define SPINDA_FRAME_ROWS 40

static void SetupPokemonLoaderByBoxData(BoxPkm *pkm, MCSSLoadInfo *info, u32 dir);
static BOOL func_0201c198(const MCSSLoadInfo *info, MCSSLoad *load, u32 personality);

static const SpindaSpotPixel sMcssSpindaSpot0[] = {
    { 11, 7 },  { 12, 7 },  { 13, 7 },  { 14, 7 },  { 10, 8 },  { 11, 8 },  { 12, 8 },  { 13, 8 },      { 14, 8 },
    { 15, 8 },  { 9, 9 },   { 10, 9 },  { 11, 9 },  { 12, 9 },  { 13, 9 },  { 14, 9 },  { 15, 9 },      { 16, 9 },
    { 9, 10 },  { 10, 10 }, { 11, 10 }, { 12, 10 }, { 13, 10 }, { 14, 10 }, { 15, 10 }, { 16, 10 },     { 9, 11 },
    { 10, 11 }, { 11, 11 }, { 12, 11 }, { 13, 11 }, { 14, 11 }, { 15, 11 }, { 16, 11 }, { 9, 12 },      { 10, 12 },
    { 11, 12 }, { 12, 12 }, { 13, 12 }, { 14, 12 }, { 15, 12 }, { 16, 12 }, { 10, 13 }, { 11, 13 },     { 12, 13 },
    { 13, 13 }, { 14, 13 }, { 15, 13 }, { 11, 14 }, { 12, 14 }, { 13, 14 }, { 14, 14 }, { 0xff, 0xff },
};

static const SpindaSpotPixel sMcssSpindaSpot1[] = {
    { 35, 9 },  { 36, 9 },  { 37, 9 },  { 38, 9 },  { 34, 10 }, { 35, 10 }, { 36, 10 }, { 37, 10 },     { 38, 10 },
    { 39, 10 }, { 33, 11 }, { 34, 11 }, { 35, 11 }, { 36, 11 }, { 37, 11 }, { 38, 11 }, { 39, 11 },     { 40, 11 },
    { 33, 12 }, { 34, 12 }, { 35, 12 }, { 36, 12 }, { 37, 12 }, { 38, 12 }, { 39, 12 }, { 40, 12 },     { 33, 13 },
    { 34, 13 }, { 35, 13 }, { 36, 13 }, { 37, 13 }, { 38, 13 }, { 39, 13 }, { 40, 13 }, { 33, 14 },     { 34, 14 },
    { 35, 14 }, { 36, 14 }, { 37, 14 }, { 38, 14 }, { 39, 14 }, { 40, 14 }, { 34, 15 }, { 35, 15 },     { 36, 15 },
    { 37, 15 }, { 38, 15 }, { 39, 15 }, { 35, 16 }, { 36, 16 }, { 37, 16 }, { 38, 16 }, { 0xff, 0xff },
};

static const SpindaSpotPixel sMcssSpindaSpot2[] = {
    { 14, 25 }, { 15, 25 }, { 16, 25 }, { 13, 26 }, { 14, 26 }, { 15, 26 }, { 16, 26 },     { 17, 26 }, { 12, 27 },
    { 13, 27 }, { 14, 27 }, { 15, 27 }, { 16, 27 }, { 17, 27 }, { 18, 27 }, { 12, 28 },     { 13, 28 }, { 14, 28 },
    { 15, 28 }, { 16, 28 }, { 17, 28 }, { 18, 28 }, { 12, 29 }, { 13, 29 }, { 14, 29 },     { 15, 29 }, { 16, 29 },
    { 17, 29 }, { 18, 29 }, { 12, 30 }, { 13, 30 }, { 14, 30 }, { 15, 30 }, { 16, 30 },     { 17, 30 }, { 18, 30 },
    { 12, 31 }, { 13, 31 }, { 14, 31 }, { 15, 31 }, { 16, 31 }, { 17, 31 }, { 18, 31 },     { 13, 32 }, { 14, 32 },
    { 15, 32 }, { 16, 32 }, { 17, 32 }, { 14, 33 }, { 15, 33 }, { 16, 33 }, { 0xff, 0xff },
};

static const SpindaSpotPixel sMcssSpindaSpot3[] = {
    { 27, 25 }, { 28, 25 }, { 29, 25 },     { 25, 26 }, { 26, 26 }, { 27, 26 }, { 28, 26 }, { 29, 26 }, { 30, 26 },
    { 31, 26 }, { 24, 27 }, { 25, 27 },     { 26, 27 }, { 27, 27 }, { 28, 27 }, { 29, 27 }, { 30, 27 }, { 31, 27 },
    { 32, 27 }, { 24, 28 }, { 25, 28 },     { 26, 28 }, { 27, 28 }, { 28, 28 }, { 29, 28 }, { 30, 28 }, { 31, 28 },
    { 32, 28 }, { 24, 29 }, { 25, 29 },     { 26, 29 }, { 27, 29 }, { 28, 29 }, { 29, 29 }, { 30, 29 }, { 31, 29 },
    { 32, 29 }, { 24, 30 }, { 25, 30 },     { 26, 30 }, { 27, 30 }, { 28, 30 }, { 29, 30 }, { 30, 30 }, { 31, 30 },
    { 32, 30 }, { 24, 31 }, { 25, 31 },     { 26, 31 }, { 27, 31 }, { 28, 31 }, { 29, 31 }, { 30, 31 }, { 31, 31 },
    { 32, 31 }, { 24, 32 }, { 25, 32 },     { 26, 32 }, { 27, 32 }, { 28, 32 }, { 29, 32 }, { 30, 32 }, { 31, 32 },
    { 32, 32 }, { 25, 33 }, { 26, 33 },     { 27, 33 }, { 28, 33 }, { 29, 33 }, { 30, 33 }, { 31, 33 }, { 27, 34 },
    { 28, 34 }, { 29, 34 }, { 0xff, 0xff },
};

static const SpindaSpotPixel *sMcssSpindaSpots[SPINDA_SPOT_COUNT] = {
    sMcssSpindaSpot0,
    sMcssSpindaSpot1,
    sMcssSpindaSpot2,
    sMcssSpindaSpot3,
};

void func_0201bfdc(PartyPkm *pkm, MCSSLoadInfo *info, u32 dir) {
    SetupPokemonLoaderByBoxData(func_0201d624(pkm), info, dir);
}

static void SetupPokemonLoaderByBoxData(BoxPkm *pkm, MCSSLoadInfo *info, u32 dir) {
    u32 species = PML_PkmGetParam(pkm, PKM_PARAM_SPECIES, NULL);
    u32 form = PML_PkmGetParam(pkm, PKM_PARAM_FORM, NULL);
    u32 sex = PML_PkmGetParam(pkm, PKM_PARAM_SEX, NULL);
    BOOL rare = PML_PkmIsRare(pkm);

    SetupPokemonLoaderFSTool(species, form, sex, rare, PML_PkmGetParam(pkm, PKM_PARAM_IS_EGG, NULL), info, dir);
}

void SetupPokemonLoaderFSTool(u32 species, u32 form, u32 sex, BOOL rare, BOOL egg, MCSSLoadInfo *info, u32 dir) {
    info->arcId = GetPokemonGraphicsARCID();
    info->character = GetPokemonCharacterDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->palette = GetPokemonPaletteDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->cells = GetPokemonCellsDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->cellAnime = GetPokemonCellAnimeDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->multiCells = GetPokemonMultiCellsDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->multiCellAnime = GetPokemonMultiCellAnimeDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->bin = GetPokemonBinFileDataNo(info->arcId, species, form, sex, rare, dir, egg);
    info->unk20 = 0;
}

void GetTrainerSpriteResIDs(u32 trainerType, MCSSLoadInfo *info, BOOL back) {
    info->arcId = GetTrainerSpriteARCID(back);
    info->character = GetTrSpriteCharacter2DatID(trainerType, back);
    info->palette = GetTrSpritePaletteDatID(trainerType, back);
    info->cells = GetTrSpriteCellDatID(trainerType, back);
    info->cellAnime = GetTrSpriteCellAnmDatID(trainerType, back);
    info->multiCells = GetTrSpriteMultiCellDatID(trainerType, back);
    info->multiCellAnime = GetTrSpriteMultiCellAnmDatID(trainerType, back);
    info->bin = GetTrSpriteBinDatID(trainerType, back);
    info->unk20 = 1;
}

MCSS *func_0201c14c(MCSSSystem *system, PartyPkm *pkm, u32 dir, fx32 x, fx32 y, fx32 z) {
    MCSSLoadInfo info;

    func_0201c188(system, PokeParty_GetParam(pkm, PKM_PARAM_PID, NULL));
    func_0201bfdc(pkm, &info, dir);
    return MCSSSys_Add(system, x, y, z, &info);
}

void func_0201c188(MCSSSystem *system, u32 personality) {
    MCSSSys_SetLoadCallback(system, func_0201c198, personality);
}

static BOOL func_0201c198(const MCSSLoadInfo *info, MCSSLoad *load, u32 personality) {
    if (info->bin == POKEGRA_SPINDA_BIN) {
        MCSS_DrawSpindaSpots(load->chars->pRawData, personality);
    }
    return TRUE;
}

// bwbattle: for Platinum's screens (port/bw_mon_anim.c)
void BwBattle_DrawMcssSpindaSpots(unsigned char *pixels, unsigned int personality) {
    MCSS_DrawSpindaSpots(pixels, personality);
}

void MCSS_DrawSpindaSpots(void *chars, u32 personality) {
    u8 *pixels = chars;
    const SpindaSpotPixel *spot;
    int i;
    int k;
    u8 j;

    for (i = 0; i < SPINDA_SPOT_COUNT; i++) {
        spot = sMcssSpindaSpots[i];
        j = 0;
        while (spot[j].x != SPINDA_SPOT_END) {
            u8 x = (personality & 0x0f) - 8 + spot[j].x;
            u8 y = ((personality & 0xf0) >> 4) - 8 + spot[j].y;
            int offsets[2];

            offsets[0] = x / 2 + y * SPINDA_ROW_SIZE;
            offsets[1] = x / 2 + (y + SPINDA_FRAME_ROWS) * SPINDA_ROW_SIZE;
            for (k = 0; k < 2; k++) {
                if (x & 1) {
                    if ((pixels[offsets[k]] & 0xf0) >= 0x10 && (pixels[offsets[k]] & 0xf0) <= 0x30) {
                        pixels[offsets[k]] += 0x50;
                    }
                } else {
                    if ((pixels[offsets[k]] & 0x0f) >= 0x01 && (pixels[offsets[k]] & 0x0f) <= 0x03) {
                        pixels[offsets[k]] += 0x05;
                    }
                }
            }
            j++;
        }
        personality >>= 8;
    }
}

// bwbattle: BW2's func_0201c290, which restarts a sprite's animation each time it ends, is left out. It passed the
// MCSS pointer through the SDK's u32 callback parameter, which a 64-bit pointer doesn't fit, and only screens outside
// the battle use it
