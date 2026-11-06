#include "port/bw_mon_sprites.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "constants/pokemon.h"
#include "constants/species.h"
#include "generated/genders.h"

#include "bwbattle/bwbattle.h"
#include "port/sim_config_prj.h"

#include "heap.h"
#include "narc.h"
#include "pokemon_icon.h"

#include "res/pokemon/pl_poke_icon.naix"

#define ARC_POKEGRA  BWBATTLE_ASSET_DIR "/a/0/0/4"
#define ARC_ICONS    BWBATTLE_ASSET_DIR "/a/0/0/7"
#define ARC_PERSONAL BWBATTLE_ASSET_DIR "/a/0/1/6"

#define PLATINUM_ICONS "poketool/icongra/pl_poke_icon.narc"

// A species' files in the sprite archive (bwbattle/system/pokegra.c), and those after the species'
#define POKEGRA_FILE_BACK    9
#define POKEGRA_FILE_PALETTE 18
#define POKEGRA_FILE_COUNT   20
#define POKEGRA_EGG          683
#define POKEGRA_FORM_FILES   0x3584
#define POKEGRA_FORM_PALETTES 0x3AB9

// The icon archive and table (PokeParty_GetIconIndex and PokeIcon_GetPalette, in BW2's ARM9)
#define ICON_FILE_FIRST_SPECIES 8
#define ICON_FILE_FORMS         0x562
#define ICON_FILE_EGG           0x2AB
#define ICON_PALETTE_EGG        0x2AD
#define ICON_PALETTE_FORMS      0x2AE

// The fields of a species' personal data (bwbattle/pml/personal.h)
#define PERSONAL_FORM_SPRITE_OFFSET 30
#define PERSONAL_FORM_COUNT         32
#define PERSONAL_FLAGS              33
#define PERSONAL_PALETTE_FORMS_FLAG 0x80

// Graphics files: where the data starts
#define NCGR_FIELD_MAPPING 0x20
#define NCGR_PIXELS        0x30
#define NCLR_COLORS        0x28

#define MAPPING_1D_32K 0x10

#define PLATINUM_ICON_FILE_COUNT 547
#define ICON_PALETTES_SIZE       (3 * PALETTE_SIZE_BYTES)
#define ICON_FILE_NONE           0xFFFF

static const u8 sIconPalettes[] = {
#include "bw_icon_palettes.inc"
};

typedef struct {
    void *data;
    unsigned size;
} CachedIcon;

static CachedIcon sIcons[PLATINUM_ICON_FILE_COUNT];
static u16 sIconFiles[PLATINUM_ICON_FILE_COUNT];
static bool sIconFilesReady;
static signed char sReady = -1;
static bool sIconsActive;
static bool sIconsLatched;

static bool Exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int BwMonSprites_Ready(void)
{
    if (sReady < 0) {
        sReady = Exists(ARC_POKEGRA) && Exists(ARC_ICONS) && Exists(ARC_PERSONAL);
    }
    return sReady;
}

void BwMonSprites_Reload(void)
{
    sReady = -1;
    sIconsLatched = false;
    sIconFilesReady = false;
    for (int i = 0; i < PLATINUM_ICON_FILE_COUNT; i++) {
        free(sIcons[i].data);
        sIcons[i].data = NULL;
    }
}

BOOL BwMonSprites_Enabled(void)
{
    return SIM_Config_prj_GetConfig()->bwMonSprites && BwMonSprites_Ready();
}

typedef struct {
    unsigned spriteOffset;
    unsigned formCount;
    bool paletteForms;
} FormInfo;

static bool ReadFormInfo(u32 species, FormInfo *info)
{
    unsigned size;
    u8 *personal = BwBattle_ReadMember(ARC_PERSONAL, species, &size);

    if (!personal) {
        return false;
    }
    bool valid = size > PERSONAL_FLAGS;
    if (valid) {
        info->spriteOffset = personal[PERSONAL_FORM_SPRITE_OFFSET] | personal[PERSONAL_FORM_SPRITE_OFFSET + 1] << 8;
        info->formCount = personal[PERSONAL_FORM_COUNT];
        info->paletteForms = (personal[PERSONAL_FLAGS] & PERSONAL_PALETTE_FORMS_FLAG) != 0;
    }
    free(personal);
    return valid;
}

// Platinum's and BW2's forms are in the same order, but BW2 has no ??? type Arceus, which is Platinum's form 9
static u32 BwForm(u32 species, u32 form)
{
    if (species == SPECIES_ARCEUS && form >= 9) {
        return form == 9 ? 0 : form - 1;
    }
    return form;
}

static bool MemberExists(const char *path, unsigned member)
{
    void *data = BwBattle_ReadMember(path, member, NULL);

    free(data);
    return data != NULL;
}

BOOL BwMonSprites_BuildTemplate(PokemonSpriteTemplate *spriteTemplate, u16 species, u8 gender, u8 face, u8 shiny, u8 form, u32 personality)
{
    if (!BwMonSprites_Ready()) {
        return FALSE;
    }

    u32 file;
    u32 palette = 0;

    if (species == SPECIES_EGG) {
        // Platinum's form 1 is Manaphy's egg
        file = (POKEGRA_EGG + (form != 0)) * POKEGRA_FILE_COUNT;
    } else if (species == SPECIES_NONE || (species > NATIONAL_DEX_COUNT && species != SPECIES_HYDREIGON)) {
        return FALSE;
    } else {
        file = species * POKEGRA_FILE_COUNT;
        form = species == SPECIES_HYDREIGON ? 0 : Pokemon_SanitizeFormId(species, form);
        u32 bwForm = BwForm(species, form);

        if (bwForm != 0) {
            FormInfo info;

            if (!ReadFormInfo(species, &info)) {
                return FALSE;
            }
            if (bwForm < info.formCount) {
                if (info.paletteForms) {
                    palette = POKEGRA_FORM_PALETTES + (info.spriteOffset + bwForm - 1) * 2 + shiny;
                } else {
                    file = POKEGRA_FORM_FILES + (info.spriteOffset + bwForm - 1) * POKEGRA_FILE_COUNT;
                }
            }
        }
    }

    u32 character = file + (face == FACE_FRONT ? 0 : POKEGRA_FILE_BACK);

    if (!MemberExists(ARC_POKEGRA, character)) {
        return FALSE;
    }
    if (gender == GENDER_FEMALE && MemberExists(ARC_POKEGRA, character + 1)) {
        character++;
    }
    if (palette == 0) {
        palette = file + POKEGRA_FILE_PALETTE + shiny;
    }

    memset(spriteTemplate, 0, sizeof(*spriteTemplate));
    spriteTemplate->narcID = MON_SPRITE_NARC_BW;
    spriteTemplate->character = character;
    spriteTemplate->palette = palette;
    if (species == SPECIES_SPINDA && face == FACE_FRONT) {
        spriteTemplate->spindaSpots = TRUE;
        spriteTemplate->personality = personality;
    }
    return TRUE;
}

BOOL BwMonSprites_BuildTemplateFromBoxMon(PokemonSpriteTemplate *spriteTemplate, BoxPokemon *mon, u8 face)
{
    BOOL reencrypt = BoxPokemon_EnterDecryptionContext(mon);
    u16 species = BoxPokemon_GetValue(mon, MON_DATA_SPECIES_OR_EGG, NULL);
    u8 gender = BoxPokemon_GetGender(mon);
    u8 shiny = Pokemon_IsPersonalityShiny(BoxPokemon_GetValue(mon, MON_DATA_OT_ID, NULL), BoxPokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL));
    u32 personality = BoxPokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
    u8 form;

    if (species == SPECIES_EGG) {
        form = BoxPokemon_GetValue(mon, MON_DATA_SPECIES, NULL) == SPECIES_MANAPHY;
    } else {
        form = BoxPokemon_GetValue(mon, MON_DATA_FORM, NULL);
    }
    BoxPokemon_ExitDecryptionContext(mon, reencrypt);
    return BwMonSprites_BuildTemplate(spriteTemplate, species, gender, face, shiny, form, personality);
}

// The file's pixels are the four OBJs that a DS sprite is made of, one after the other. This puts them where the
// whole sprite's tiles go, 12 tiles wide (bwbattle/system/pokegra.c, PokeGra_CellCharsToImage)
static void CellCharsToTiles(const u8 *chars, u8 *tiles)
{
    u32 offset = 0;

    for (int i = 0; i < 8; i++, offset += 0x100) {
        memcpy(tiles + i * 0x180, chars + offset, 0x100);
    }
    for (int i = 0; i < 8; i++, offset += 0x80) {
        memcpy(tiles + (i * 12 + 8) * 32, chars + offset, 0x80);
    }
    for (int i = 0; i < 4; i++, offset += 0x100) {
        memcpy(tiles + i * 0x180 + 0xC00, chars + offset, 0x100);
    }
    for (int i = 0; i < 4; i++, offset += 0x80) {
        memcpy(tiles + (i * 12 + 104) * 32, chars + offset, 0x80);
    }
}

BOOL BwMonSprites_LoadImage(const PokemonSpriteTemplate *spriteTemplate, u8 *image)
{
    unsigned size;
    u8 *file = BwBattle_ReadMember(ARC_POKEGRA, spriteTemplate->character, &size);
    u8 *tiles = malloc(BW_MON_SPRITE_IMAGE_SIZE);
    BOOL loaded = FALSE;

    if (file && tiles && size >= NCGR_PIXELS + BW_MON_SPRITE_IMAGE_SIZE && !memcmp(file, "RGCN", 4)) {
        CellCharsToTiles(file + NCGR_PIXELS, tiles);
        if (spriteTemplate->spindaSpots) {
            BwBattle_DrawSpindaSpots(tiles, spriteTemplate->personality);
        }
        for (int row = 0; row < BW_MON_SPRITE_SIZE; row++) {
            for (int tileX = 0; tileX < BW_MON_SPRITE_SIZE / 8; tileX++) {
                const int tile = row / 8 * 12 + tileX;

                memcpy(image + row * (BW_MON_SPRITE_SIZE / 2) + tileX * 4, tiles + tile * 32 + (row % 8) * 4, 4);
            }
        }
        loaded = TRUE;
    }
    free(tiles);
    free(file);
    return loaded;
}

BOOL BwMonSprites_LoadPalette(const PokemonSpriteTemplate *spriteTemplate, u16 *palette)
{
    unsigned size;
    u8 *file = BwBattle_ReadMember(ARC_POKEGRA, spriteTemplate->palette, &size);
    BOOL loaded = file && size >= NCLR_COLORS + PALETTE_SIZE_BYTES;

    if (loaded) {
        memcpy(palette, file + NCLR_COLORS, PALETTE_SIZE_BYTES);
    }
    free(file);
    return loaded;
}

BOOL BwMonSprites_ArtBounds(const PokemonSpriteTemplate *spriteTemplate, int *minX, int *minY, int *maxX, int *maxY)
{
    u8 *image = malloc(BW_MON_SPRITE_IMAGE_SIZE);
    BOOL found = FALSE;

    if (image && BwMonSprites_LoadImage(spriteTemplate, image)) {
        *minX = *minY = BW_MON_SPRITE_SIZE;
        *maxX = *maxY = -1;
        for (int y = 0; y < BW_MON_SPRITE_SIZE; y++) {
            for (int x = 0; x < BW_MON_SPRITE_SIZE; x++) {
                const u8 byte = image[y * (BW_MON_SPRITE_SIZE / 2) + x / 2];

                if ((x & 1) ? byte >> 4 : byte & 0xF) {
                    *minX = x < *minX ? x : *minX;
                    *maxX = x > *maxX ? x : *maxX;
                    *minY = y < *minY ? y : *minY;
                    *maxY = y > *maxY ? y : *maxY;
                }
            }
        }
        found = *maxX >= 0;
    }
    free(image);
    return found;
}

void BwMonSprites_CenterArtOffset(const PokemonSpriteTemplate *spriteTemplate, BOOL flipped, int *xOffset, int *yOffset)
{
    int minX, minY, maxX, maxY;

    *xOffset = *yOffset = 0;
    if (BwMonSprites_ArtBounds(spriteTemplate, &minX, &minY, &maxX, &maxY)) {
        const int fromCenter = BW_MON_SPRITE_SIZE / 2 - (minX + maxX + 1) / 2;

        *xOffset = flipped ? -fromCenter : fromCenter;
        *yOffset = BW_MON_SPRITE_SIZE / 2 - (minY + maxY + 1) / 2;
    }
}

// The last row of a species' art in its Platinum sprite's first frame
static int PlatinumArtBottom(u16 species, enum HeapID heapID)
{
    PokemonSpriteTemplate platinum;
    NNSG2dCharacterData *charData;
    int bottom = MON_SPRITE_FRAME_HEIGHT - 1;

    BuildPokemonSpriteTemplate(&platinum, species, Pokemon_GetGenderOf(species, 0), FACE_FRONT, FALSE, 0, 0);

    void *file = NARC_AllocAndReadWholeMemberByIndexPair(platinum.narcID, platinum.character, heapID);

    NNS_G2dGetUnpackedCharacterData(file, &charData);
    PokemonSprite_Decrypt(charData->pRawData, platinum.narcID);

    // The file holds two frames side by side
    const u8 *rows = charData->pRawData;
    const int rowBytes = MON_SPRITE_FRAME_WIDTH;
    bool found = false;

    for (; bottom > 0 && !found; bottom--) {
        for (int x = 0; x < MON_SPRITE_FRAME_WIDTH / 2 && !found; x++) {
            found = rows[bottom * rowBytes + x] != 0;
        }
    }
    Heap_Free(file);
    return found ? bottom + 1 : MON_SPRITE_FRAME_HEIGHT - 1;
}

int BwMonSprites_FeetOffset(const PokemonSpriteTemplate *spriteTemplate, u16 species, enum HeapID heapID)
{
    int minX, minY, maxX, maxY;

    if (!BwMonSprites_ArtBounds(spriteTemplate, &minX, &minY, &maxX, &maxY)) {
        return 0;
    }
    // Both frames are around the same center: Platinum's is 80 high, BW2's 96
    return (PlatinumArtBottom(species, heapID) - MON_SPRITE_FRAME_HEIGHT / 2) - (maxY - BW_MON_SPRITE_SIZE / 2);
}

// Which BW2 file goes with each of Platinum's icon files, found by asking Platinum's own table for every Pokémon
static void PrepareIconFiles(void)
{
    if (sIconFilesReady) {
        return;
    }
    memset(sIconFiles, 0xFF, sizeof(sIconFiles));

    for (u32 species = 0; species <= NATIONAL_DEX_COUNT; species++) {
        FormInfo info = { 0 };
        const bool hasInfo = ReadFormInfo(species, &info);

        for (u32 form = 0; form < (hasInfo && info.formCount ? info.formCount : 1); form++) {
            const u32 member = PokeIconSpriteIndex(species, FALSE, form);
            u32 file = species * 2 + ICON_FILE_FIRST_SPECIES;

            if (form != 0 && !info.paletteForms) {
                file = (info.spriteOffset + form - 1) * 2 + ICON_FILE_FORMS;
            }
            if (member < PLATINUM_ICON_FILE_COUNT && sIconFiles[member] == ICON_FILE_NONE) {
                sIconFiles[member] = file;
            }
        }
    }
    sIconFiles[PokeIconSpriteIndex(SPECIES_NONE, TRUE, 0)] = ICON_FILE_EGG * 2 + ICON_FILE_FIRST_SPECIES;
    sIconFiles[PokeIconSpriteIndex(SPECIES_MANAPHY, TRUE, 0)] = (ICON_FILE_EGG + 1) * 2 + ICON_FILE_FIRST_SPECIES;
    sIconFilesReady = true;
}

BOOL BwMonIcons_Active(void)
{
    if (!sIconsLatched) {
        sIconsActive = BwMonSprites_Enabled();
        sIconsLatched = true;
    }
    return sIconsActive;
}

u8 BwMonIcons_PaletteIndex(u32 species, u32 form, u32 isEgg)
{
    u32 entry = species;

    if (isEgg == TRUE) {
        entry = ICON_PALETTE_EGG + (species == SPECIES_MANAPHY);
    } else if (species > NATIONAL_DEX_COUNT) {
        entry = SPECIES_NONE;
    } else {
        form = Pokemon_SanitizeFormId(species, form);
        if (form != 0) {
            FormInfo info;

            if (ReadFormInfo(species, &info) && !info.paletteForms && form < info.formCount) {
                entry = info.spriteOffset + form + ICON_PALETTE_FORMS;
            }
        }
    }
    return entry < NELEMS(sIconPalettes) ? sIconPalettes[entry] : 0;
}

// Platinum's palette file, with BW2's three palettes in place of its first three, and the rest of it as it was
static void *MakeIconPalette(unsigned *size)
{
    u8 *platinum = BwBattle_ReadMember(PLATINUM_ICONS, shared_pals_NCLR, size);
    unsigned bwSize;
    u8 *bw = BwBattle_ReadMember(ARC_ICONS, 0, &bwSize);

    if (platinum && bw && *size >= NCLR_COLORS + ICON_PALETTES_SIZE && bwSize >= NCLR_COLORS + ICON_PALETTES_SIZE) {
        memcpy(platinum + NCLR_COLORS, bw + NCLR_COLORS, ICON_PALETTES_SIZE);
        free(bw);
        return platinum;
    }
    free(platinum);
    free(bw);
    return NULL;
}

// BW2's icons have the same size and tile order as Platinum's, and use Platinum's cells and animations. Only the file
// says a different mapping, which would make a screen load them as if for BW2's cells
static void *MakeIconTiles(unsigned file, unsigned *size)
{
    u8 *icon = BwBattle_ReadMember(ARC_ICONS, file, size);

    if (icon && (*size <= NCGR_FIELD_MAPPING + 4 || memcmp(icon, "RGCN", 4))) {
        free(icon);
        return NULL;
    }
    if (icon) {
        icon[NCGR_FIELD_MAPPING] = MAPPING_1D_32K;
        icon[NCGR_FIELD_MAPPING + 1] = 0;
        icon[NCGR_FIELD_MAPPING + 2] = 0;
        icon[NCGR_FIELD_MAPPING + 3] = 0;
    }
    return icon;
}

const void *BwMonIcons_Resource(unsigned member, unsigned *size)
{
    if (member == shared_pals_NCLR) {
        sIconsLatched = false;
    }
    if (!BwMonIcons_Active() || member >= PLATINUM_ICON_FILE_COUNT) {
        return NULL;
    }

    CachedIcon *cached = &sIcons[member];

    if (!cached->data) {
        if (member == shared_pals_NCLR) {
            cached->data = MakeIconPalette(&cached->size);
        } else {
            PrepareIconFiles();
            if (sIconFiles[member] != ICON_FILE_NONE) {
                cached->data = MakeIconTiles(sIconFiles[member], &cached->size);
            }
        }
    }
    if (!cached->data) {
        return NULL;
    }
    *size = cached->size;
    return cached->data;
}
