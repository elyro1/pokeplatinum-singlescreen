#include "applications/pokedex/pokedex_pages.h"

#include <nitro.h>
#include <string.h>

#include "constants/charcode.h"
#include "constants/narc.h"
#include "constants/species.h"
#include "generated/abilities.h"
#include "generated/evolution_methods.h"
#include "generated/moves.h"

#include "applications/pokedex/footprint.h"
#include "applications/pokedex/infomain.h"
#include "applications/pokedex/ov21_021D4340.h"
#include "applications/pokedex/pokedex_app.h"
#include "applications/pokedex/pokedex_data_manager.h"
#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_graphics_manager.h"
#include "applications/pokedex/pokedex_main.h"
#include "applications/pokedex/pokedex_reference.h"
#include "applications/pokedex/pokedex_sort.h"
#include "applications/pokedex/pokedex_sort_data.h"
#include "applications/pokedex/pokedex_tabs.h"
#include "applications/pokedex/pokedex_text.h"
#include "applications/pokedex/pokedex_wide.h"
#include "applications/pokedex/struct_ov21_021D4340_1.h"
#include "applications/pokedex/struct_ov21_021D4558.h"
#include "applications/pokedex/struct_ov21_021D4660.h"
#include "applications/pokedex/struct_ov21_021E68F4.h"

#include "bg_window.h"
#include "char_transfer.h"
#include "font.h"
#include "graphics.h"
#include "heap.h"
#include "message.h"
#include "move_table.h"
#include "narc.h"
#include "pltt_transfer.h"
#include "pokedex.h"
#include "pokedex_data_index.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_sprite.h"
#include "render_oam.h"
#include "sound_playback.h"
#include "sprite.h"
#include "sprite_resource.h"
#include "sprite_transfer.h"
#include "sprite_util.h"
#include "string_gf.h"
#include "system.h"
#include "text.h"

#include "res/graphics/pokedex/zukan.naix"
#include "res/text/bank/pokedex.h"

// Layout, in view coordinates.
#define PANEL_TOP      20
#define TITLE_HEIGHT   16
#define BODY_TOP       (PANEL_TOP + TITLE_HEIGHT)
#define ROW_TOP        (BODY_TOP + 3)
#define ROW_HEIGHT     16
#define VISIBLE_ROWS   9
#define SPRITE_BOX_H   92
#define SPRITE_FRAME_SIZE 80
#define NAME_BOX_TOP   (PANEL_TOP + SPRITE_BOX_H + 4)
#define SCREEN_BOTTOM  190
#define EVO_ROW_HEIGHT 30
#define EVO_ROWS       5

#define TYPE_ICON_RESOURCE_ID 9300
#define ICON_PALETTE_RESOURCE_ID 9301
#define ICON_BYTES (32 * 32 / 2)
#define TYPE_ICON_WIDTH 48
#define MAX_TYPE_SPRITES (2 + VISIBLE_ROWS)
#define MAX_MOVE_ROWS (POKEDEX_MAX_MOVE_ENTRIES + POKEDEX_MOVE_SOURCE_COUNT)

#define TEXT_DARK   TEXT_COLOR(PDW_TEXT, PDW_TEXT_SHADOW, 0)
#define TEXT_LIGHT  TEXT_COLOR(PDW_WHITE, PDW_RED_DARK, 0)
#define TEXT_HEADER TEXT_COLOR(PDW_TEXT, PDW_CREAM, 0)

enum MoveRowKind {
    MOVE_ROW_HEADER,
    MOVE_ROW_ENTRY,
};

typedef struct MoveRow {
    u8 kind;
    u8 source;
    u16 entry;
} MoveRow;

typedef struct PageData {
    // First: the info page is main screen 2, which the entry's other screens address
    // as Platinum's info screen state (playCry, above all).
    InfoMainState infoState;
    PokedexSortData *sortData;
    int kind;
    int species;
    int form;
    int scroll;
    int numMoveRows;
    PokedexEvoTable *evoTable;
    PokedexMoveList *moves;
    MoveRow moveRows[MAX_MOVE_ROWS];
    PokedexEvoChain chain;
} PageData;

enum PageFade {
    PAGE_FADE_NONE,
    PAGE_FADE_IN,
    PAGE_FADE_OUT,
};

typedef struct PageGraphics {
    int width;
    int left;
    int leftX;
    int leftW;
    int rightX;
    int rightW;
    PokedexCanvas *canvas;
    Window leftWindow;
    Window rightWindow;
    SpriteResource *typeResources[4];
    Sprite *typeSprites[MAX_TYPE_SPRITES];
    NARC *iconNarc;
    CharTransferAllocation iconVram;
    SpriteResource *iconPalette;
    u32 iconPaletteOffset;
    int iconCount;
    u16 iconSpecies[POKEDEX_MAX_EVO_NODES];
    s16 iconX[POKEDEX_MAX_EVO_NODES];
    s16 iconY[POKEDEX_MAX_EVO_NODES];
    int drawnSpecies;
    int drawnScroll;
    u32 drawnHeightBank;
    enum PageFade fade;
    BOOL spritesShown;
} PageGraphics;

static const u16 sStatLabels[] = {
    pl_msg_pokedex_wide_stat_hp,
    pl_msg_pokedex_wide_stat_atk,
    pl_msg_pokedex_wide_stat_def,
    pl_msg_pokedex_wide_stat_spatk,
    pl_msg_pokedex_wide_stat_spdef,
    pl_msg_pokedex_wide_stat_speed,
};

static const u8 sStatParams[] = {
    SPECIES_DATA_BASE_HP,
    SPECIES_DATA_BASE_ATK,
    SPECIES_DATA_BASE_DEF,
    SPECIES_DATA_BASE_SP_ATK,
    SPECIES_DATA_BASE_SP_DEF,
    SPECIES_DATA_BASE_SPEED,
};

static const u16 sMoveSourceTitles[POKEDEX_MOVE_SOURCE_COUNT] = {
    pl_msg_pokedex_wide_moves_level,
    pl_msg_pokedex_wide_moves_tm,
    pl_msg_pokedex_wide_moves_tutor,
    pl_msg_pokedex_wide_moves_egg,
};

// Data

static void LoadSpeciesData(PageData *data, enum HeapID heapID)
{
    data->species = PokedexSort_CurrentSpecies(data->sortData);
    data->form = PokedexSort_DefaultForm(data->sortData, data->species);
    data->scroll = 0;

    if (data->kind == POKEDEX_WIDE_PAGE_MOVES) {
        PokedexReference_LoadMoves(data->evoTable, data->species, data->form, data->moves, heapID);

        data->numMoveRows = 0;

        for (int source = 0; source < POKEDEX_MOVE_SOURCE_COUNT; source++) {
            if (data->moves->sourceCount[source] == 0) {
                continue;
            }

            data->moveRows[data->numMoveRows].kind = MOVE_ROW_HEADER;
            data->moveRows[data->numMoveRows].source = source;
            data->numMoveRows++;

            for (int i = 0; i < data->moves->sourceCount[source]; i++) {
                data->moveRows[data->numMoveRows].kind = MOVE_ROW_ENTRY;
                data->moveRows[data->numMoveRows].source = source;
                data->moveRows[data->numMoveRows].entry = data->moves->sourceStart[source] + i;
                data->numMoveRows++;
            }
        }
    } else if (data->kind == POKEDEX_WIDE_PAGE_EVOS) {
        PokedexReference_LoadEvoChain(data->evoTable, data->species, &data->chain);
    }
}

static BOOL PageData_Init(PokedexDataManager *dataMan, void *pageData)
{
    PageData *data = pageData;

    if (data->kind == POKEDEX_WIDE_PAGE_MOVES || data->kind == POKEDEX_WIDE_PAGE_EVOS) {
        data->evoTable = PokedexReference_NewEvoTable(dataMan->heapID);
    }

    if (data->kind == POKEDEX_WIDE_PAGE_INFO) {
        PokedexSort_SetCurrentSpecies(data->sortData, PokedexSort_CurrentSpecies(data->sortData));
        PokedexSort_SetBootMode(data->sortData, PokedexSort_IsNationalDex(data->sortData));
    }

    if (data->kind == POKEDEX_WIDE_PAGE_MOVES) {
        data->moves = Heap_Alloc(dataMan->heapID, sizeof(PokedexMoveList));
    }

    LoadSpeciesData(data, dataMan->heapID);
    return TRUE;
}

static BOOL StepSpecies(PageData *data, int step)
{
    const int before = PokedexSort_CurrentStatusIndex(data->sortData);

    PokedexSort_TakeStep_Loop(data->sortData, step);

    return PokedexSort_CurrentStatusIndex(data->sortData) != before;
}

static BOOL PageData_Main(PokedexDataManager *dataMan, void *pageData)
{
    PageData *data = pageData;

    if (dataMan->exit == TRUE) {
        return TRUE;
    }

    if (dataMan->unchanged == TRUE) {
        return FALSE;
    }

    // The info screen's sub screen steps through the species on this page.
    if (data->kind == POKEDEX_WIDE_PAGE_INFO) {
        if (data->species != PokedexSort_CurrentSpecies(data->sortData)) {
            LoadSpeciesData(data, dataMan->heapID);
        }

        if (data->infoState.playCry) {
            Sound_StopPokemonCries(0);
            Sound_PlayPokemonCryEx(POKECRY_POKEDEX, data->species, 0x1ff, 0x1ff, 0x1ff, data->form);
            data->infoState.playCry = FALSE;
        }

        return FALSE;
    }

    int step = 0;

    if (JOY_REPEAT(PAD_BUTTON_L)) {
        step = -1;
    } else if (JOY_REPEAT(PAD_BUTTON_R)) {
        step = 1;
    } else if (data->kind == POKEDEX_WIDE_PAGE_MOVES && JOY_REPEAT(PAD_KEY_UP | PAD_KEY_DOWN)) {
        const int maxScroll = data->numMoveRows > VISIBLE_ROWS ? data->numMoveRows - VISIBLE_ROWS : 0;
        const int scroll = data->scroll + (JOY_REPEAT(PAD_KEY_UP) ? -1 : 1);

        if (scroll >= 0 && scroll <= maxScroll) {
            data->scroll = scroll;
            Sound_PlayEffect(SEQ_SE_DP_SELECT5_sseq);
        }
    } else if (JOY_REPEAT(PAD_KEY_UP)) {
        step = -1;
    } else if (JOY_REPEAT(PAD_KEY_DOWN)) {
        step = 1;
    }

    if (step != 0 && StepSpecies(data, step)) {
        LoadSpeciesData(data, dataMan->heapID);
        Sound_PlayEffect(SEQ_SE_DP_DENSI06_sseq);
    }

    return FALSE;
}

static BOOL PageData_Exit(PokedexDataManager *dataMan, void *pageData)
{
    PageData *data = pageData;

    if (data->kind == POKEDEX_WIDE_PAGE_INFO) {
        Sound_StopPokemonCries(0);
    }

    if (data->evoTable) {
        PokedexReference_FreeEvoTable(data->evoTable);
        data->evoTable = NULL;
    }

    if (data->moves) {
        Heap_Free(data->moves);
        data->moves = NULL;
    }

    // The list resumes at the entry shown here.
    PokedexSort_UpdateCurrentValues(data->sortData);
    return TRUE;
}

// Graphics helpers

// With room to append to (String_Concat stays within a string's capacity).
static String *GetMessage(u32 bank, u32 entry, enum HeapID heapID)
{
    MessageLoader *loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, bank, heapID);
    String *string = String_Init(64, heapID);

    MessageLoader_GetString(loader, entry, string);
    MessageLoader_Free(loader);
    return string;
}

// Prints at view coordinates within a window; align: 0 left, 1 right, 2 center.
static void PrintAt(Window *window, enum Font font, const String *string, int x, int y, int align, TextColor color)
{
    const int width = Font_CalcStringWidth(font, string, 0);

    if (align == 1) {
        x -= width;
    } else if (align == 2) {
        x -= width / 2;
    }

    x -= window->tilemapLeft * 8;
    y -= window->tilemapTop * 8;

    if (x < 0) {
        x = 0;
    }

    if (y >= 0) {
        Text_AddPrinterWithParamsAndColor(window, font, string, x, y, TEXT_SPEED_NO_TRANSFER, color, NULL);
    }
}

static void PrintMessage(Window *window, enum Font font, u32 bank, u32 entry, int x, int y, int align, TextColor color, enum HeapID heapID)
{
    String *string = GetMessage(bank, entry, heapID);

    PrintAt(window, font, string, x, y, align, color);
    String_Free(string);
}

static void PrintNumber(Window *window, enum Font font, int value, int digits, int x, int y, int align, TextColor color, enum HeapID heapID)
{
    String *string = String_Init(8, heapID);

    String_FormatInt(string, value, digits, PADDING_MODE_NONE, CHARSET_MODE_EN);
    PrintAt(window, font, string, x, y, align, color);
    String_Free(string);
}

// Cuts a string short, ending it with an ellipsis, until it is at most maxWidth dots wide.
static void FitString(String *string, enum Font font, int maxWidth)
{
    if ((int)Font_CalcStringWidth(font, string, 0) <= maxWidth) {
        return;
    }

    for (int length = string->size - 1; length >= 0; length--) {
        // No space before the ellipsis.
        if (length > 0 && string->data[length - 1] == CHAR_SPACE) {
            continue;
        }

        string->data[length] = CHAR_ELLIPSIS;
        string->data[length + 1] = CHAR_EOS;
        string->size = length + 1;

        if ((int)Font_CalcStringWidth(font, string, 0) <= maxWidth) {
            return;
        }
    }

    String_Clear(string);
}

static void LoadTypeIcons(PageGraphics *graphics, PokedexGraphicData *graphicData, enum HeapID heapID)
{
    NARC *narc = PokedexGraphics_GetNARC(graphicData);

    graphics->typeResources[SPRITE_RESOURCE_CHAR] = SpriteResourceCollection_AddTilesFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_CHAR], narc, type_icons_NCGR_lz, TRUE, TYPE_ICON_RESOURCE_ID, NNS_G2D_VRAM_TYPE_2DMAIN, heapID);
    SpriteTransfer_RequestCharAtEnd(graphics->typeResources[SPRITE_RESOURCE_CHAR]);
    SpriteResource_ReleaseData(graphics->typeResources[SPRITE_RESOURCE_CHAR]);

    graphics->typeResources[SPRITE_RESOURCE_PLTT] = SpriteResourceCollection_AddPaletteFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], narc, type_icons_NCLR, FALSE, TYPE_ICON_RESOURCE_ID, NNS_G2D_VRAM_TYPE_2DMAIN, 5, heapID);
    SpriteTransfer_RequestPlttFreeSpace(graphics->typeResources[SPRITE_RESOURCE_PLTT]);
    SpriteResource_ReleaseData(graphics->typeResources[SPRITE_RESOURCE_PLTT]);

    graphics->typeResources[SPRITE_RESOURCE_CELL] = SpriteResourceCollection_AddFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_CELL], narc, type_icons_cell_NCER_lz, TRUE, TYPE_ICON_RESOURCE_ID, SPRITE_RESOURCE_CELL, heapID);
    graphics->typeResources[SPRITE_RESOURCE_ANIM] = SpriteResourceCollection_AddFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_ANIM], narc, type_icons_anim_NANR_lz, TRUE, TYPE_ICON_RESOURCE_ID, SPRITE_RESOURCE_ANIM, heapID);

    SpriteResourcesHeader header;
    SpriteListTemplate template;

    SpriteResourcesHeader_Init(&header, TYPE_ICON_RESOURCE_ID, TYPE_ICON_RESOURCE_ID, TYPE_ICON_RESOURCE_ID, TYPE_ICON_RESOURCE_ID, 0xffffffff, 0xffffffff, FALSE, 0, graphicData->spriteResourceCollection[SPRITE_RESOURCE_CHAR], graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], graphicData->spriteResourceCollection[SPRITE_RESOURCE_CELL], graphicData->spriteResourceCollection[SPRITE_RESOURCE_ANIM], NULL, NULL);

    template.list = graphicData->spriteList;
    template.resourceData = &header;
    template.priority = 32;
    template.vramType = NNS_G2D_VRAM_TYPE_2DMAIN;
    template.heapID = heapID;
    template.position.x = 0;
    template.position.y = 0;
    template.position.z = 0;

    for (int i = 0; i < MAX_TYPE_SPRITES; i++) {
        graphics->typeSprites[i] = SpriteList_Add(&template);
        Sprite_SetDrawFlag(graphics->typeSprites[i], FALSE);
    }
}

static void FreeTypeIcons(PageGraphics *graphics, PokedexGraphicData *graphicData)
{
    for (int i = 0; i < MAX_TYPE_SPRITES; i++) {
        Sprite_Delete(graphics->typeSprites[i]);
    }

    SpriteTransfer_ResetCharTransfer(graphics->typeResources[SPRITE_RESOURCE_CHAR]);
    SpriteTransfer_ResetPlttTransfer(graphics->typeResources[SPRITE_RESOURCE_PLTT]);

    for (int i = 0; i < 4; i++) {
        SpriteResourceCollection_Remove(graphicData->spriteResourceCollection[i], graphics->typeResources[i]);
    }
}

// The type badge (TYPE_ICON_WIDTH x 16, times scale) whose top-left is at these view coordinates.
static void ShowScaledTypeIcon(PageGraphics *graphics, int index, int type, int x, int y, fx32 scale)
{
    Sprite *sprite = graphics->typeSprites[index];
    VecFx32 position = {
        x * FX32_ONE + TYPE_ICON_WIDTH / 2 * scale - graphics->left * FX32_ONE,
        y * FX32_ONE + 8 * scale,
        0,
    };
    const VecFx32 scales = { scale, scale, FX32_ONE };

    Sprite_SetPosition(sprite, &position);
    Sprite_SetAffineScaleEx(sprite, &scales, scale == FX32_ONE ? AFFINE_OVERWRITE_MODE_NONE : AFFINE_OVERWRITE_MODE_NORMAL);
    Sprite_SetAnim(sprite, PokedexGraphics_GetAnimIDfromType(type));
    Sprite_SetDrawFlag(sprite, graphics->spritesShown);
}

static void ShowTypeIcon(PageGraphics *graphics, int index, int type, int x, int y)
{
    ShowScaledTypeIcon(graphics, index, type, x, y, FX32_ONE);
}

static void HideTypeIcons(PageGraphics *graphics)
{
    for (int i = 0; i < MAX_TYPE_SPRITES; i++) {
        Sprite_SetDrawFlag(graphics->typeSprites[i], FALSE);
    }
}

static void SetupIcons(PageGraphics *graphics, PokedexGraphicData *graphicData, enum HeapID heapID)
{
    graphics->iconNarc = NARC_ctor(NARC_INDEX_POKETOOL__ICONGRA__PL_POKE_ICON, heapID);

    BOOL allocated = CharTransfer_AllocRange(ICON_BYTES * POKEDEX_MAX_EVO_NODES, TRUE, NNS_G2D_VRAM_TYPE_2DMAIN, &graphics->iconVram);
    GF_ASSERT(allocated);

    graphics->iconPalette = SpriteResourceCollection_AddPaletteFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], graphics->iconNarc, PokeIconPalettesFileIndex(), FALSE, ICON_PALETTE_RESOURCE_ID, NNS_G2D_VRAM_TYPE_2DMAIN, 3, heapID);
    SpriteTransfer_RequestPlttFreeSpace(graphics->iconPalette);
    SpriteResource_ReleaseData(graphics->iconPalette);
    graphics->iconPaletteOffset = PlttTransfer_GetPlttOffset(SpriteTransfer_GetPaletteProxy(graphics->iconPalette, NULL), NNS_G2D_VRAM_TYPE_2DMAIN);
}

static void FreeIcons(PageGraphics *graphics, PokedexGraphicData *graphicData)
{
    SpriteTransfer_ResetPlttTransfer(graphics->iconPalette);
    SpriteResourceCollection_Remove(graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], graphics->iconPalette);
    CharTransfer_ClearRange(&graphics->iconVram);
    NARC_dtor(graphics->iconNarc);
}

static void LoadIcon(PageGraphics *graphics, int slot, int species, enum HeapID heapID)
{
    void *file = NARC_AllocAndReadWholeMember(graphics->iconNarc, PokeIconSpriteIndex(species, FALSE, 0), heapID);
    NNSG2dCharacterData *charData;

    if (NNS_G2dGetUnpackedCharacterData(file, &charData)) {
        DC_FlushRange(charData->pRawData, ICON_BYTES);
        GX_LoadOBJ(charData->pRawData, graphics->iconVram.offset + slot * ICON_BYTES, ICON_BYTES);
    }

    Heap_Free(file);
}

static void EnterIconOams(PageGraphics *graphics)
{
    GXOamAttr oams[POKEDEX_MAX_EVO_NODES];
    int count = 0;

    if (!graphics->spritesShown) {
        return;
    }

    for (int slot = 0; slot < graphics->iconCount; slot++) {
        if (graphics->iconSpecies[slot] == SPECIES_NONE) {
            continue;
        }

        const u32 charName = graphics->iconVram.offset / 128 + slot * (ICON_BYTES / 128);
        const u32 palette = graphics->iconPaletteOffset + PokeIconPaletteIndex(graphics->iconSpecies[slot], 0, FALSE);

        oams[count].attr0 = graphics->iconY[slot] & 0xFF;
        oams[count].attr1 = ((graphics->iconX[slot] - graphics->left) & 0x1FF) | (2 << 14);
        oams[count].attr2 = (charName & 0x3FF) | (1 << 10) | ((palette & 0xF) << 12);
        oams[count]._3 = 0;
        count++;
    }

    if (count > 0) {
        NNS_G2dEntryOamManagerOam(RenderOam_GetScreenOam(DS_SCREEN_MAIN), oams, count);
    }
}

// Drawing

static void DrawFrame(PageGraphics *graphics, const PageData *data)
{
    PokedexCanvas *canvas = graphics->canvas;
    const int nameBoxH = SCREEN_BOTTOM - NAME_BOX_TOP;
    const int bodyH = SCREEN_BOTTOM - BODY_TOP;

    PokedexCanvas_Stripes(canvas, 0, 0, canvas->width, canvas->height, PDW_OLIVE, PDW_OLIVE_LIGHT);

    // Sprite box.
    PokedexCanvas_Panel(canvas, graphics->leftX, PANEL_TOP, graphics->leftW, SPRITE_BOX_H, PDW_CREAM, PDW_DARK);
    PokedexCanvas_Stripes(canvas, graphics->leftX + 3, PANEL_TOP + 3, graphics->leftW - 6, SPRITE_BOX_H - 6, PDW_CREAM_LIGHT, PDW_CREAM);

    // Name box with the number strip.
    PokedexCanvas_Panel(canvas, graphics->leftX, NAME_BOX_TOP, graphics->leftW, nameBoxH, PDW_CREAM_LIGHT, PDW_DARK);
    PokedexCanvas_Fill(canvas, graphics->leftX + 1, NAME_BOX_TOP + 1, graphics->leftW - 2, 15, PDW_GOLD);
    PokedexCanvas_Fill(canvas, graphics->leftX + 1, NAME_BOX_TOP + 16, graphics->leftW - 2, 1, PDW_GOLD_DARK);
    PokedexCanvas_Fill(canvas, graphics->leftX + 6, NAME_BOX_TOP + 35, graphics->leftW - 12, 1, PDW_GREY);

    if (PokedexSort_CurrentCaughtStatus(data->sortData) == CS_CAUGHT) {
        PokedexCanvas_Ball(canvas, graphics->leftX + graphics->leftW - 6 - POKEDEX_CANVAS_BALL_SIZE, NAME_BOX_TOP + 3);
    }

    // Page panel: title strip and body.
    PokedexCanvas_Panel(canvas, graphics->rightX, PANEL_TOP, graphics->rightW, TITLE_HEIGHT + 1, PDW_RED, PDW_RED_DARK);
    PokedexCanvas_Fill(canvas, graphics->rightX + 2, PANEL_TOP + 1, graphics->rightW - 4, 1, PDW_WHITE);
    PokedexCanvas_Panel(canvas, graphics->rightX, BODY_TOP, graphics->rightW, bodyH, PDW_CREAM_LIGHT, PDW_DARK);
}

static void DrawRowLines(PageGraphics *graphics, int rows)
{
    for (int row = 0; row < rows - 1; row++) {
        PokedexCanvas_Fill(graphics->canvas, graphics->rightX + 4, ROW_TOP + (row + 1) * ROW_HEIGHT - 1, graphics->rightW - 8, 1, PDW_GREY);
    }
}

static void DrawLeftText(PageGraphics *graphics, const PageData *data, enum HeapID heapID)
{
    Window *window = &graphics->leftWindow;
    const int species = data->species;
    const int dexNumber = PokedexSort_IsNationalDex(data->sortData) ? species : Pokemon_SinnohDexNumber(species);
    String *string = GetMessage(TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_number, heapID);
    String *number = String_Init(8, heapID);

    String_FormatInt(number, dexNumber, 3, PADDING_MODE_ZEROES, CHARSET_MODE_EN);
    String_Concat(string, number);
    PrintAt(window, FONT_SYSTEM, string, graphics->leftX + 6, NAME_BOX_TOP, 0, TEXT_HEADER);
    String_Free(number);
    String_Free(string);

    PrintMessage(window, FONT_SYSTEM, TEXT_BANK_SPECIES_NAME, species, graphics->leftX + 6, NAME_BOX_TOP + 18, 0, TEXT_DARK, heapID);
}

#define TYPE_ICON_GAP 2

static void ShowSpeciesTypes(PageGraphics *graphics, const PageData *data)
{
    const int type1 = SpeciesData_GetFormValue(data->species, data->form, SPECIES_DATA_TYPE_1);
    const int type2 = SpeciesData_GetFormValue(data->species, data->form, SPECIES_DATA_TYPE_2);
    const int x = graphics->leftX + 6;
    const int y = NAME_BOX_TOP + 42;

    if (type2 == type1) {
        ShowTypeIcon(graphics, 0, type1, x, y);
        return;
    }

    // Two badges side by side, as far in as the box allows, shrunk if they don't fit.
    // A badge is two sprites (32 and 16 dots) scaled about its center; at 7/8 both
    // land on whole dots, where other scales leave a seam between them.
    const int inner = graphics->leftW - 2;
    fx32 scale = FX32_ONE;

    if (2 * TYPE_ICON_WIDTH + TYPE_ICON_GAP > inner) {
        scale = FX32_ONE * 7 / 8;
    }

    const int badgeW = TYPE_ICON_WIDTH * scale / FX32_ONE;
    const int total = 2 * badgeW + TYPE_ICON_GAP;
    const int pairX = total <= graphics->leftW - 12 ? x : graphics->leftX + 1 + (inner - total) / 2;
    const int scaledY = y + (16 - 16 * scale / FX32_ONE) / 2;

    ShowScaledTypeIcon(graphics, 0, type1, pairX, scaledY, scale);
    ShowScaledTypeIcon(graphics, 1, type2, pairX + badgeW + TYPE_ICON_GAP, scaledY, scale);
}

static int RowY(int row)
{
    return ROW_TOP + row * ROW_HEIGHT;
}

static u8 StatBarColor(int value)
{
    if (value < 60) {
        return PDW_RED;
    }

    if (value < 90) {
        return PDW_ORANGE;
    }

    if (value < 120) {
        return PDW_GOLD;
    }

    return PDW_GREEN;
}

// The value column, wide enough for "720".
#define ABILITY_VALUE_COLUMN_W 24

static void DrawStatsPage(PageGraphics *graphics, const PageData *data, enum HeapID heapID)
{
    Window *window = &graphics->rightWindow;
    const int labelX = graphics->rightX + 6;
    const int valueRight = labelX + 80;
    const int barX = valueRight + 6;
    const int barW = graphics->rightX + graphics->rightW - 8 - barX;
    int total = 0;

    DrawRowLines(graphics, VISIBLE_ROWS);

    for (int stat = 0; stat < (int)NELEMS(sStatParams) + 1; stat++) {
        const int y = RowY(stat);
        int value;

        if (stat < (int)NELEMS(sStatParams)) {
            value = SpeciesData_GetFormValue(data->species, data->form, sStatParams[stat]);
            total += value;
            PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, sStatLabels[stat], labelX, y, 0, TEXT_DARK, heapID);
        } else {
            value = total;
            PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_stat_total, labelX, y, 0, TEXT_DARK, heapID);
        }

        PrintNumber(window, FONT_SYSTEM, value, 3, valueRight, y, 1, TEXT_DARK, heapID);

        if (barW > 8) {
            const int maxValue = stat < (int)NELEMS(sStatParams) ? 200 : 720;
            int length = barW * value / maxValue;

            length = length > barW ? barW : length < 2 ? 2 : length;
            PokedexCanvas_Fill(graphics->canvas, barX, y + 5, barW, 6, PDW_GREY);
            PokedexCanvas_Fill(graphics->canvas, barX, y + 5, length, 6, StatBarColor(stat < (int)NELEMS(sStatParams) ? value : value / 6));
            PokedexCanvas_Fill(graphics->canvas, barX, y + 5, length, 1, PDW_WHITE);
        }
    }

    const int abilityRow = NELEMS(sStatParams) + 1;
    const int ability1 = SpeciesData_GetFormValue(data->species, data->form, SPECIES_DATA_ABILITY_1);
    const int ability2 = SpeciesData_GetFormValue(data->species, data->form, SPECIES_DATA_ABILITY_2);

    String *label = GetMessage(TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_abilities, heapID);
    const int labelW = Font_CalcStringWidth(FONT_SYSTEM, label, 0);

    PrintAt(window, FONT_SYSTEM, label, labelX, RowY(abilityRow), 0, TEXT_DARK);
    String_Free(label);

    // The names span the value and bar columns: from where the values start, or just
    // past the label if it reaches further.
    const int abilityX = labelX + labelW + 6 > valueRight - ABILITY_VALUE_COLUMN_W ? labelX + labelW + 6 : valueRight - ABILITY_VALUE_COLUMN_W;
    const int abilityW = graphics->rightX + graphics->rightW - 6 - abilityX;

    for (int i = 0; i < 2; i++) {
        const int ability = i == 0 ? ability1 : ability2;

        if (i > 0 && (ability == ABILITY_NONE || ability == ability1)) {
            break;
        }

        String *name = GetMessage(TEXT_BANK_ABILITY_NAMES, ability, heapID);

        FitString(name, FONT_SYSTEM, abilityW);
        PrintAt(window, FONT_SYSTEM, name, abilityX, RowY(abilityRow + i), 0, TEXT_DARK);
        String_Free(name);
    }

    PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_title_stats, graphics->rightX + 8, PANEL_TOP, 0, TEXT_LIGHT, heapID);
}

typedef struct MoveColumns {
    int prefixX;
    int nameX;
    int typeX;
    int powRight;
    int accRight;
    int ppRight;
    BOOL numbers;
    BOOL types;
} MoveColumns;

// The longest move names ("Thunderpunch") are about 70 dots.
#define MOVE_NAME_ROOM 74

static void GetMoveColumns(const PageGraphics *graphics, MoveColumns *columns)
{
    const int right = graphics->rightX + graphics->rightW - 10;

    columns->prefixX = graphics->rightX + 6;
    columns->nameX = graphics->rightX + 40;
    columns->ppRight = right;
    columns->accRight = right - 22;
    columns->powRight = right - 50;
    columns->typeX = columns->powRight - 28 - TYPE_ICON_WIDTH;
    columns->numbers = columns->typeX >= columns->nameX + MOVE_NAME_ROOM;

    if (!columns->numbers) {
        columns->typeX = right - TYPE_ICON_WIDTH;
    }

    // Narrow screens keep the room for the names.
    columns->types = columns->typeX >= columns->nameX + MOVE_NAME_ROOM;
}

static void DrawMovesPage(PageGraphics *graphics, const PageData *data, enum HeapID heapID)
{
    Window *window = &graphics->rightWindow;
    MoveColumns columns;
    String *string = String_Init(16, heapID);
    MessageLoader *moveNames = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_MOVE_NAMES, heapID);

    GetMoveColumns(graphics, &columns);
    PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_title_moves, graphics->rightX + 8, PANEL_TOP, 0, TEXT_LIGHT, heapID);

    if (columns.numbers) {
        PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_pow, columns.powRight, PANEL_TOP, 1, TEXT_LIGHT, heapID);
        PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_acc, columns.accRight, PANEL_TOP, 1, TEXT_LIGHT, heapID);
        PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_pp, columns.ppRight, PANEL_TOP, 1, TEXT_LIGHT, heapID);
    }

    const int rows = data->numMoveRows - data->scroll < VISIBLE_ROWS ? data->numMoveRows - data->scroll : VISIBLE_ROWS;

    DrawRowLines(graphics, rows);

    for (int row = 0; row < rows; row++) {
        const MoveRow *moveRow = &data->moveRows[data->scroll + row];
        const int y = RowY(row);

        if (moveRow->kind == MOVE_ROW_HEADER) {
            PokedexCanvas_Fill(graphics->canvas, graphics->rightX + 2, y, graphics->rightW - 4, ROW_HEIGHT - 1, PDW_GOLD);
            PokedexCanvas_Fill(graphics->canvas, graphics->rightX + 2, y + ROW_HEIGHT - 2, graphics->rightW - 4, 1, PDW_GOLD_DARK);
            PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, sMoveSourceTitles[moveRow->source], columns.prefixX, y, 0, TEXT_HEADER, heapID);
            continue;
        }

        const PokedexMoveEntry *entry = &data->moves->entries[moveRow->entry];

        switch (entry->source) {
        case POKEDEX_MOVE_LEVEL:
            PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_lv, columns.prefixX, y, 0, TEXT_COLOR(PDW_RED_DARK, PDW_TEXT_SHADOW, 0), heapID);
            PrintNumber(window, FONT_SYSTEM, entry->value, 3, columns.nameX - 4, y, 1, TEXT_COLOR(PDW_RED_DARK, PDW_TEXT_SHADOW, 0), heapID);
            break;
        case POKEDEX_MOVE_TM: {
            const BOOL hm = entry->value > 92;
            String *prefix = GetMessage(TEXT_BANK_POKEDEX, hm ? pl_msg_pokedex_wide_hm : pl_msg_pokedex_wide_tm, heapID);

            String_FormatInt(string, hm ? entry->value - 92 : entry->value, 2, PADDING_MODE_ZEROES, CHARSET_MODE_EN);
            String_Concat(prefix, string);
            PrintAt(window, FONT_SYSTEM, prefix, columns.prefixX, y, 0, TEXT_COLOR(PDW_ORANGE, PDW_TEXT_SHADOW, 0));
            String_Free(prefix);
            break;
        }
        default:
            break;
        }

        const int nameX = entry->source == POKEDEX_MOVE_LEVEL || entry->source == POKEDEX_MOVE_TM ? columns.nameX : columns.prefixX;
        const int nameEnd = columns.types ? columns.typeX - 2 : columns.ppRight;

        MessageLoader_GetString(moveNames, entry->move, string);
        FitString(string, FONT_SYSTEM, nameEnd - nameX);
        PrintAt(window, FONT_SYSTEM, string, nameX, y, 0, TEXT_DARK);

        if (columns.types) {
            ShowTypeIcon(graphics, 2 + row, MoveTable_LoadParam(entry->move, MOVEATTRIBUTE_TYPE), columns.typeX, y);
        }

        if (columns.numbers) {
            const int power = MoveTable_LoadParam(entry->move, MOVEATTRIBUTE_POWER);
            const int accuracy = MoveTable_LoadParam(entry->move, MOVEATTRIBUTE_ACCURACY);

            if (power > 1) {
                PrintNumber(window, FONT_SYSTEM, power, 3, columns.powRight, y, 1, TEXT_DARK, heapID);
            } else {
                PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_dash, columns.powRight, y, 1, TEXT_DARK, heapID);
            }

            if (accuracy > 0) {
                PrintNumber(window, FONT_SYSTEM, accuracy, 3, columns.accRight, y, 1, TEXT_DARK, heapID);
            } else {
                PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_dash, columns.accRight, y, 1, TEXT_DARK, heapID);
            }

            PrintNumber(window, FONT_SYSTEM, MoveTable_LoadParam(entry->move, MOVEATTRIBUTE_PP), 2, columns.ppRight, y, 1, TEXT_DARK, heapID);
        }
    }

    // Scroll position.
    if (data->numMoveRows > VISIBLE_ROWS) {
        const int trackX = graphics->rightX + graphics->rightW - 5;
        const int trackY = ROW_TOP;
        const int trackH = VISIBLE_ROWS * ROW_HEIGHT - 2;
        const int thumbH = trackH * VISIBLE_ROWS / data->numMoveRows;
        const int thumbY = trackY + (trackH - thumbH) * data->scroll / (data->numMoveRows - VISIBLE_ROWS);

        PokedexCanvas_Fill(graphics->canvas, trackX, trackY, 2, trackH, PDW_GREY);
        PokedexCanvas_Fill(graphics->canvas, trackX, thumbY, 2, thumbH, PDW_RED);
    }

    MessageLoader_Free(moveNames);
    String_Free(string);
}

// Evolutions
//
// A method is a short run of glyphs and text ("[stone] Water Stone", "[heart][moon]"),
// so that even Eevee's eight Pokémon fit two to a row.

enum EvoGlyph {
    EVO_GLYPH_NONE,
    EVO_GLYPH_HEART, // friendship
    EVO_GLYPH_SUN,
    EVO_GLYPH_MOON,
    EVO_GLYPH_TRADE,
    EVO_GLYPH_STONE, // using an item
    EVO_GLYPH_HELD, // holding an item
    EVO_GLYPH_MOVE, // knowing a move
    EVO_GLYPH_PIN, // a place
    EVO_GLYPH_SPARKLE, // beauty
    EVO_GLYPH_DICE, // personality value
    EVO_GLYPH_MALE,
    EVO_GLYPH_FEMALE,
    EVO_GLYPH_BALL, // a party member, or Shedinja's spare slot and Poké Ball
    EVO_GLYPH_GT,
    EVO_GLYPH_EQ,
    EVO_GLYPH_LT,
    EVO_GLYPH_COUNT,
};

#define EVO_GLYPH_ROWS 10

typedef struct EvoGlyphArt {
    const char *rows[EVO_GLYPH_ROWS];
    s8 dy; // from the top of the text line
} EvoGlyphArt;

static const EvoGlyphArt sEvoGlyphs[EVO_GLYPH_COUNT] = {
    [EVO_GLYPH_HEART] = { { ".dd...dd.", "drrd.drrd", "drwrdrrrd", "drrrrrrrd", "drrrrrrRd", ".drrrrRd.", "..drrRd..", "...dRd...", "....d...." }, 3 },
    [EVO_GLYPH_SUN] = { { "....o....", ".o.....o.", "...ddd...", "..dyyyd..", "oodyyydoo", "..dyyyd..", "...ddd...", ".o.....o.", "....o...." }, 3 },
    [EVO_GLYPH_MOON] = { { "...ddd...", ".ddyyd...", ".dyyd....", "dyyyd....", "dyyyd....", "dyyyd....", ".dyyyd...", ".ddyyydd.", "...ddd..." }, 3 },
    [EVO_GLYPH_TRADE] = { { ".....d...", "ddddddd..", "drrrrrrd.", "ddddddd..", ".....d...", "...d.....", "..ddddddd", ".dooooood", "..ddddddd", "...d....." }, 2 },
    [EVO_GLYPH_STONE] = { { "...ddd...", "..dwgod..", ".dwgooYd.", "dwgoooYYd", "dgooooYYd", ".doooYYd.", "..doYYd..", "...ddd..." }, 3 },
    [EVO_GLYPH_HELD] = { { "...ddd...", "..d...d..", ".ddddddd.", "dwwwwwwgd", "dwwwwwwgd", "dwwwwwwgd", "dgggggggd", ".ddddddd." }, 3 },
    [EVO_GLYPH_MOVE] = { { "..ddddd..", ".dggwggd.", "dggwwwggd", "dgwwdwwgd", "dgwd.dwgd", "dgwwdwwgd", "dggwwwggd", ".dgggggd.", "..ddddd.." }, 3 },
    [EVO_GLYPH_PIN] = { { "..ddddd..", ".drrrrrd.", "drrwwwrRd", "drwwwwwRd", "drrwwwrRd", ".drrrrRd.", "..drrRd..", "...drd...", "....d...." }, 3 },
    [EVO_GLYPH_SPARKLE] = { { "....d....", "...dwd...", "...dwd...", ".ddwwwdd.", "dwwwwwwwd", ".ddwwwdd.", "...dwd...", "...dwd...", "....d...." }, 3 },
    [EVO_GLYPH_DICE] = { { ".ddddddd.", "dwwwwwwgd", "dwdwwwdgd", "dwwwwwwgd", "dwwwdwwgd", "dwwwwwwgd", "dwdwwwdgd", "dgggggggd", ".ddddddd." }, 3 },
    [EVO_GLYPH_MALE] = { { ".....dddd", ".......dd", "......d.d", "..dddd...", ".dd..dd..", ".d....d..", ".dd..dd..", "..dddd..." }, 4 },
    [EVO_GLYPH_FEMALE] = { { "..ddddd..", ".dd...dd.", ".d.....d.", ".dd...dd.", "..ddddd..", "....d....", "..ddddd..", "....d...." }, 3 },
    [EVO_GLYPH_BALL] = { { "..ddddd..", ".drrrrrd.", "drwrrrrRd", "drrrdrrRd", "ddddwdddd", "dwwwdwwgd", "dwwwwwwgd", ".dwwwwgd.", "..ddddd.." }, 3 },
    [EVO_GLYPH_GT] = { { "Gs....", "sGGs..", ".ssGGs", ".GGsss", "Gsss..", "ss...." }, 5 },
    [EVO_GLYPH_EQ] = { { "GGGGGs", "ssssss", "GGGGGs", "ssssss" }, 6 },
    [EVO_GLYPH_LT] = { { "....Gs", "..GGss", "GGsss.", "ssGGs.", "..ssGs", "....ss" }, 5 },
};

#define EVO_METHOD_MAX_PARTS 5
#define EVO_PART_GAP         3

typedef struct EvoMethod {
    int count;
    u8 glyph[EVO_METHOD_MAX_PARTS];
    String *text[EVO_METHOD_MAX_PARTS];
} EvoMethod;

static int GlyphWidth(enum EvoGlyph glyph)
{
    int width = 0;

    for (int row = 0; row < EVO_GLYPH_ROWS && sEvoGlyphs[glyph].rows[row]; row++) {
        const int length = strlen(sEvoGlyphs[glyph].rows[row]);

        width = length > width ? length : width;
    }

    return width;
}

static void AddGlyph(EvoMethod *method, enum EvoGlyph glyph)
{
    if (method->count < EVO_METHOD_MAX_PARTS) {
        method->glyph[method->count] = glyph;
        method->text[method->count] = NULL;
        method->count++;
    }
}

static void AddText(EvoMethod *method, u32 bank, u32 entry, enum HeapID heapID)
{
    if (method->count < EVO_METHOD_MAX_PARTS) {
        method->glyph[method->count] = EVO_GLYPH_NONE;
        method->text[method->count] = GetMessage(bank, entry, heapID);
        method->count++;
    }
}

static void AddLevel(EvoMethod *method, int level, enum HeapID heapID)
{
    String *number = String_Init(8, heapID);

    AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_level, heapID);
    String_FormatInt(number, level, 3, PADDING_MODE_NONE, CHARSET_MODE_EN);
    String_Concat(method->text[method->count - 1], number);
    String_Free(number);
}

static void FreeMethod(EvoMethod *method)
{
    for (int i = 0; i < method->count; i++) {
        if (method->text[i]) {
            String_Free(method->text[i]);
        }
    }

    method->count = 0;
}

static void FormatEvolutionMethod(EvoMethod *method, const PokedexEvoNode *node, enum HeapID heapID)
{
    method->count = 0;

    switch (node->method) {
    case EVO_LEVEL:
    case EVO_LEVEL_NINJASK:
        AddLevel(method, node->param, heapID);
        break;
    case EVO_LEVEL_SHEDINJA:
        AddLevel(method, node->param, heapID);
        AddGlyph(method, EVO_GLYPH_BALL);
        break;
    case EVO_LEVEL_HAPPINESS:
        AddGlyph(method, EVO_GLYPH_HEART);
        break;
    case EVO_LEVEL_HAPPINESS_DAY:
        AddGlyph(method, EVO_GLYPH_HEART);
        AddGlyph(method, EVO_GLYPH_SUN);
        break;
    case EVO_LEVEL_HAPPINESS_NIGHT:
        AddGlyph(method, EVO_GLYPH_HEART);
        AddGlyph(method, EVO_GLYPH_MOON);
        break;
    case EVO_TRADE:
        AddGlyph(method, EVO_GLYPH_TRADE);
        break;
    case EVO_TRADE_WITH_HELD_ITEM:
        AddGlyph(method, EVO_GLYPH_TRADE);
        AddGlyph(method, EVO_GLYPH_HELD);
        AddText(method, TEXT_BANK_ITEM_NAMES, node->param, heapID);
        break;
    case EVO_USE_ITEM:
    case EVO_USE_ITEM_MALE:
    case EVO_USE_ITEM_FEMALE:
        AddGlyph(method, EVO_GLYPH_STONE);
        AddText(method, TEXT_BANK_ITEM_NAMES, node->param, heapID);

        if (node->method != EVO_USE_ITEM) {
            AddGlyph(method, node->method == EVO_USE_ITEM_MALE ? EVO_GLYPH_MALE : EVO_GLYPH_FEMALE);
        }
        break;
    case EVO_LEVEL_ATK_GT_DEF:
    case EVO_LEVEL_ATK_EQ_DEF:
    case EVO_LEVEL_ATK_LT_DEF:
        AddLevel(method, node->param, heapID);
        AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_atk, heapID);
        AddGlyph(method, node->method == EVO_LEVEL_ATK_GT_DEF ? EVO_GLYPH_GT : node->method == EVO_LEVEL_ATK_EQ_DEF ? EVO_GLYPH_EQ : EVO_GLYPH_LT);
        AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_def, heapID);
        break;
    case EVO_LEVEL_PID_LOW:
    case EVO_LEVEL_PID_HIGH:
        AddLevel(method, node->param, heapID);
        AddGlyph(method, EVO_GLYPH_DICE);
        break;
    case EVO_LEVEL_BEAUTY:
        AddGlyph(method, EVO_GLYPH_SPARKLE);
        AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_beauty, heapID);
        break;
    case EVO_LEVEL_WITH_HELD_ITEM_DAY:
    case EVO_LEVEL_WITH_HELD_ITEM_NIGHT:
        AddGlyph(method, EVO_GLYPH_HELD);
        AddText(method, TEXT_BANK_ITEM_NAMES, node->param, heapID);
        AddGlyph(method, node->method == EVO_LEVEL_WITH_HELD_ITEM_DAY ? EVO_GLYPH_SUN : EVO_GLYPH_MOON);
        break;
    case EVO_LEVEL_KNOW_MOVE:
        AddGlyph(method, EVO_GLYPH_MOVE);
        AddText(method, TEXT_BANK_MOVE_NAMES, node->param, heapID);
        break;
    case EVO_LEVEL_SPECIES_IN_PARTY:
        AddGlyph(method, EVO_GLYPH_BALL);
        AddText(method, TEXT_BANK_SPECIES_NAME, node->param, heapID);
        break;
    case EVO_LEVEL_MALE:
    case EVO_LEVEL_FEMALE:
        AddLevel(method, node->param, heapID);
        AddGlyph(method, node->method == EVO_LEVEL_MALE ? EVO_GLYPH_MALE : EVO_GLYPH_FEMALE);
        break;
    case EVO_LEVEL_MAGNETIC_FIELD:
        AddGlyph(method, EVO_GLYPH_PIN);
        AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_coronet, heapID);
        break;
    case EVO_LEVEL_MOSS_ROCK:
        AddGlyph(method, EVO_GLYPH_PIN);
        AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_moss, heapID);
        break;
    case EVO_LEVEL_ICE_ROCK:
        AddGlyph(method, EVO_GLYPH_PIN);
        AddText(method, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_evo_ice, heapID);
        break;
    default:
        break;
    }
}

static int PartWidth(const EvoMethod *method, int i, enum Font font)
{
    return method->text[i] ? (int)Font_CalcStringWidth(font, method->text[i], 0) : GlyphWidth(method->glyph[i]);
}

// Prints a method from x, cutting its longest text short to stay within maxWidth.
static void DrawMethod(PageGraphics *graphics, EvoMethod *method, int x, int y, int maxWidth, TextColor color)
{
    const enum Font font = FONT_SYSTEM;
    int width = 0;
    int longest = -1;

    for (int i = 0; i < method->count; i++) {
        width += PartWidth(method, i, font) + (i > 0 ? EVO_PART_GAP : 0);

        if (method->text[i] && (longest < 0 || PartWidth(method, i, font) > PartWidth(method, longest, font))) {
            longest = i;
        }
    }

    if (width > maxWidth && longest >= 0) {
        FitString(method->text[longest], font, PartWidth(method, longest, font) - (width - maxWidth));
    }

    for (int i = 0; i < method->count; i++) {
        if (method->text[i]) {
            PrintAt(&graphics->rightWindow, font, method->text[i], x, y, 0, color);
        } else {
            const EvoGlyphArt *art = &sEvoGlyphs[method->glyph[i]];
            int rows = 0;

            while (rows < EVO_GLYPH_ROWS && art->rows[rows]) {
                rows++;
            }

            PokedexCanvas_Pattern(graphics->canvas, x, y + art->dy, art->rows, rows);
        }

        x += PartWidth(method, i, font) + EVO_PART_GAP;
    }
}

enum EvoLayout {
    EVO_LAYOUT_LIST, // one Pokémon a row, indented by stage
    EVO_LAYOUT_GRID, // the first stage across the top, the rest in columns below it
    EVO_LAYOUT_COMPACT, // a grid too narrow for names: one line a Pokémon, no icons
};

// Where the name and method start in a row, right of the 32-dot icon.
#define EVO_TEXT_X        32
#define EVO_METHOD_Y      14
#define EVO_MIN_TEXT      72
#define EVO_COMPACT_ROW_H 18

static void DrawEvoName(PageGraphics *graphics, const PageData *data, const PokedexEvoNode *node, int x, int y, int maxWidth, enum HeapID heapID)
{
    const BOOL seen = Pokedex_HasSeenSpecies(data->sortData->pokedex, node->species);
    String *name = seen ? GetMessage(TEXT_BANK_SPECIES_NAME, node->species, heapID) : GetMessage(TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_unknown, heapID);

    FitString(name, FONT_SYSTEM, maxWidth);
    PrintAt(&graphics->rightWindow, FONT_SYSTEM, name, x, y, 0, TEXT_DARK);
    String_Free(name);
}

// A 32x32 icon with its top-left at x, y - 4, or a question mark when unseen.
static void AddEvoIcon(PageGraphics *graphics, const PageData *data, const PokedexEvoNode *node, int x, int y, enum HeapID heapID)
{
    if (Pokedex_HasSeenSpecies(data->sortData->pokedex, node->species)) {
        graphics->iconSpecies[graphics->iconCount] = node->species;
        graphics->iconX[graphics->iconCount] = x;
        graphics->iconY[graphics->iconCount] = y - 4;
        LoadIcon(graphics, graphics->iconCount, node->species, heapID);
    } else {
        graphics->iconSpecies[graphics->iconCount] = SPECIES_NONE;
        PokedexCanvas_Question(graphics->canvas, x + 11, y + 7, PDW_GREY, PDW_WHITE);
    }

    graphics->iconCount++;
}

// A row with the icon on the left and the name over the method.
static void DrawEvoCell(PageGraphics *graphics, const PageData *data, const PokedexEvoNode *node, int x, int y, int width, enum HeapID heapID)
{
    const int textW = width - EVO_TEXT_X - 4;

    if (node->species == data->species) {
        PokedexCanvas_Panel(graphics->canvas, x, y, width, EVO_ROW_HEIGHT - 2, PDW_CREAM, PDW_ORANGE);
    }

    AddEvoIcon(graphics, data, node, x, y, heapID);
    DrawEvoName(graphics, data, node, x + EVO_TEXT_X, y, textW, heapID);

    if (node->parent >= 0) {
        EvoMethod method;

        FormatEvolutionMethod(&method, node, heapID);
        DrawMethod(graphics, &method, x + EVO_TEXT_X, y + EVO_METHOD_Y, textW, TEXT_COLOR(PDW_GREEN, PDW_TEXT_SHADOW, 0));
        FreeMethod(&method);
    }
}

static void DrawEvosPage(PageGraphics *graphics, const PageData *data, enum HeapID heapID)
{
    const PokedexEvoChain *chain = &data->chain;
    const int left = graphics->rightX + 4;
    const int width = graphics->rightW - 8;

    PrintMessage(&graphics->rightWindow, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_title_evos, graphics->rightX + 8, PANEL_TOP, 0, TEXT_LIGHT, heapID);
    graphics->iconCount = 0;

    if (chain->count <= 1) {
        PrintMessage(&graphics->rightWindow, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_no_evolution, graphics->rightX + graphics->rightW / 2, ROW_TOP + 40, 2, TEXT_DARK, heapID);
        return;
    }

    // Below the first stage, in columns of up to EVO_ROWS - 1.
    const int children = chain->count - 1;
    const int columns = (children + EVO_ROWS - 2) / (EVO_ROWS - 1);
    const int rowsPerColumn = (children + columns - 1) / columns;
    const int columnW = width / columns;
    enum EvoLayout layout = EVO_LAYOUT_LIST;

    if (chain->count > EVO_ROWS) {
        layout = columnW - EVO_TEXT_X - 4 >= EVO_MIN_TEXT ? EVO_LAYOUT_GRID : EVO_LAYOUT_COMPACT;
    }

    if (layout == EVO_LAYOUT_LIST) {
        for (int i = 0; i < chain->count; i++) {
            const int indent = chain->nodes[i].depth * 12;

            DrawEvoCell(graphics, data, &chain->nodes[i], left + indent, ROW_TOP + i * EVO_ROW_HEIGHT, width - indent, heapID);
        }

        return;
    }

    if (layout == EVO_LAYOUT_GRID) {
        DrawEvoCell(graphics, data, &chain->nodes[0], left, ROW_TOP, columnW, heapID);

        for (int i = 0; i < children; i++) {
            const int column = i / rowsPerColumn;
            const int row = 1 + i % rowsPerColumn;

            DrawEvoCell(graphics, data, &chain->nodes[1 + i], left + column * columnW, ROW_TOP + row * EVO_ROW_HEIGHT, columnW, heapID);
        }

        return;
    }

    // Compact: names in a column, then the methods.
    const int rowH = EVO_COMPACT_ROW_H * chain->count <= SCREEN_BOTTOM - ROW_TOP - 4 ? EVO_COMPACT_ROW_H : (SCREEN_BOTTOM - ROW_TOP - 4) / chain->count;
    int nameW = 0;

    for (int i = 0; i < chain->count; i++) {
        const PokedexEvoNode *node = &chain->nodes[i];
        String *name = Pokedex_HasSeenSpecies(data->sortData->pokedex, node->species) ? GetMessage(TEXT_BANK_SPECIES_NAME, node->species, heapID) : GetMessage(TEXT_BANK_POKEDEX, pl_msg_pokedex_wide_unknown, heapID);
        const int nameWidth = Font_CalcStringWidth(FONT_SYSTEM, name, 0);

        nameW = nameWidth > nameW ? nameWidth : nameW;
        String_Free(name);
    }

    nameW = nameW > width / 2 ? width / 2 : nameW;

    for (int i = 0; i < chain->count; i++) {
        const PokedexEvoNode *node = &chain->nodes[i];
        const int y = ROW_TOP + i * rowH;

        if (node->species == data->species) {
            PokedexCanvas_Panel(graphics->canvas, left, y, width, rowH - 1, PDW_CREAM, PDW_ORANGE);
        }

        DrawEvoName(graphics, data, node, left + 3, y, nameW, heapID);

        if (node->parent >= 0) {
            EvoMethod method;

            FormatEvolutionMethod(&method, node, heapID);
            DrawMethod(graphics, &method, left + 3 + nameW + 6, y, width - nameW - 12, TEXT_COLOR(PDW_GREEN, PDW_TEXT_SHADOW, 0));
            FreeMethod(&method);
        }
    }
}

// A footprint file is 4x2 tiles: the 16x16 print on the left, filler on the right. The
// print's lower half comes first (tiles 0 and 1, then 4 and 5). Any color is the print.
static void DrawFootprint(PokedexCanvas *canvas, int species, int x, int y, enum HeapID heapID)
{
    NNSG2dCharacterData *charData;
    void *file = Graphics_GetCharData(GetPokedexFootprintsNarcID(), GetSpeciesFootprintTiles(species), TRUE, &charData, heapID);
    const u8 *tiles = charData->pRawData;

    for (int row = 0; row < 2; row++) {
        for (int column = 0; column < 2; column++) {
            const u8 *tile = tiles + ((1 - row) * 4 + column) * 32;

            for (int dot = 0; dot < 64; dot++) {
                const u8 byte = tile[dot / 2];

                if ((dot & 1) ? byte >> 4 : byte & 0xF) {
                    PokedexCanvas_Dot(canvas, x + column * 8 + dot % 8, y + row * 8 + dot / 8, PDW_DARK);
                }
            }
        }
    }

    Heap_Free(file);
}

// Platinum breaks the dex entries for its 240-dot box; rebreaks one for a narrower panel.
static void WrapEntry(String *string, int maxWidth, enum HeapID heapID)
{
    if ((int)Font_CalcMaxLineWidth(FONT_SYSTEM, string, 0) <= maxWidth) {
        return;
    }

    String *line = String_Init(string->size + 1, heapID);
    int lineStart = 0;
    int lastSpace = -1;

    for (int i = 0; i < (int)string->size; i++) {
        if (string->data[i] == CHAR_CR) {
            string->data[i] = CHAR_SPACE;
        }
    }

    for (int i = 0; i < (int)string->size; i++) {
        String_CopyNumChars(line, &string->data[lineStart], i - lineStart + 1);

        if ((int)Font_CalcMaxLineWidth(FONT_SYSTEM, line, 0) > maxWidth && lastSpace > lineStart) {
            string->data[lastSpace] = CHAR_CR;
            lineStart = lastSpace + 1;
        }

        if (string->data[i] == CHAR_SPACE) {
            lastSpace = i;
        }
    }

    String_Free(line);
}

#define INFO_FOOTPRINT_BOX 26
#define INFO_LABEL_X       38
#define INFO_VALUE_X       (INFO_LABEL_X + 32)
#define INFO_ENTRY_TOP     (BODY_TOP + 36)

static void DrawInfoPage(PageGraphics *graphics, const PageData *data, enum HeapID heapID)
{
    Window *window = &graphics->rightWindow;
    // As in Platinum, the details wait for the Pokémon to be caught.
    const BOOL caught = PokedexSort_CurrentCaughtStatus(data->sortData) == CS_CAUGHT;
    const int species = caught ? data->species : SPECIES_NONE;
    String *string = PokedexText_Category(species, GAME_LANGUAGE, heapID);

    FitString(string, FONT_SYSTEM, graphics->rightW - 16);
    PrintAt(window, FONT_SYSTEM, string, graphics->rightX + 8, PANEL_TOP, 0, TEXT_LIGHT);
    PokedexText_Free(string);

    const int boxX = graphics->rightX + 6;
    const int boxY = BODY_TOP + 4;

    PokedexCanvas_Panel(graphics->canvas, boxX, boxY, INFO_FOOTPRINT_BOX, INFO_FOOTPRINT_BOX, PDW_CREAM, PDW_DARK);

    if (caught) {
        // Giratina's Origin Forme leaves no print, like Metapod.
        const int print = data->species == SPECIES_GIRATINA && data->form > 0 ? SPECIES_METAPOD : data->species;

        DrawFootprint(graphics->canvas, print, boxX + (INFO_FOOTPRINT_BOX - 16) / 2, boxY + (INFO_FOOTPRINT_BOX - 16) / 2, heapID);
    }

    const int labelX = graphics->rightX + INFO_LABEL_X;
    const int valueX = graphics->rightX + INFO_VALUE_X;

    PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_ht, labelX, BODY_TOP + 1, 0, TEXT_DARK, heapID);
    PrintMessage(window, FONT_SYSTEM, Height_Message_Bank_Index(), species, valueX, BODY_TOP + 1, 0, TEXT_DARK, heapID);
    PrintMessage(window, FONT_SYSTEM, TEXT_BANK_POKEDEX, pl_msg_pokedex_wt, labelX, BODY_TOP + 17, 0, TEXT_DARK, heapID);
    PrintMessage(window, FONT_SYSTEM, Weight_Message_Bank_Index(), species, valueX, BODY_TOP + 17, 0, TEXT_DARK, heapID);

    PokedexCanvas_Fill(graphics->canvas, graphics->rightX + 4, INFO_ENTRY_TOP - 2, graphics->rightW - 8, 1, PDW_GREY);

    // The entry, centered as a block in the rest of the panel.
    string = PokedexText_DexEntry(species, GAME_LANGUAGE, 0, heapID);
    WrapEntry(string, graphics->rightW - 12, heapID);

    const int entryW = Font_CalcMaxLineWidth(FONT_SYSTEM, string, 0);
    const int entryH = String_NumLines(string) * ROW_HEIGHT;
    const int entryRoom = SCREEN_BOTTOM - 2 - INFO_ENTRY_TOP;
    const int entryY = INFO_ENTRY_TOP + (entryH < entryRoom ? (entryRoom - entryH) / 2 : 0);

    PrintAt(window, FONT_SYSTEM, string, graphics->rightX + (graphics->rightW - entryW) / 2, entryY, 0, TEXT_DARK);
    PokedexText_Free(string);
}

// The sprites' art doesn't fill their 80x80 frame evenly, and Platinum places them for
// the ground rather than for a box. Puts the art's own center on (x, y).
static void CenterSpriteArt(PokemonSprite *sprite, int x, int y, enum HeapID heapID)
{
    const PokemonSpriteTemplate *template = PokemonSprite_GetTemplate(sprite);
    void *file = NARC_AllocAndReadWholeMemberByIndexPair(template->narcID, template->character, heapID);
    NNSG2dCharacterData *charData;
    int minX = SPRITE_FRAME_SIZE, minY = SPRITE_FRAME_SIZE, maxX = -1, maxY = -1;

    NNS_G2dGetUnpackedCharacterData(file, &charData);
    PokemonSprite_Decrypt(charData->pRawData, template->narcID);

    const u8 *pixels = charData->pRawData;

    // The file holds the two frames side by side; the Pokédex shows the first.
    for (int row = 0; row < SPRITE_FRAME_SIZE; row++) {
        for (int column = 0; column < SPRITE_FRAME_SIZE; column++) {
            const u8 byte = pixels[row * SPRITE_FRAME_SIZE + column / 2];

            if ((column & 1) ? byte >> 4 : byte & 0xF) {
                minX = column < minX ? column : minX;
                maxX = column > maxX ? column : maxX;
                minY = row < minY ? row : minY;
                maxY = row > maxY ? row : maxY;
            }
        }
    }

    Heap_Free(file);

    if (maxX >= 0) {
        x += SPRITE_FRAME_SIZE / 2 - (minX + maxX + 1) / 2;
        y += SPRITE_FRAME_SIZE / 2 - (minY + maxY + 1) / 2;
    }

    PokemonSprite_SetAttribute(sprite, MON_SPRITE_X_CENTER, x);
    PokemonSprite_SetAttribute(sprite, MON_SPRITE_Y_CENTER, y);
}

static void Redraw(PageGraphics *graphics, PokedexGraphicData *graphicData, const PageData *data, enum HeapID heapID)
{
    HideTypeIcons(graphics);
    DrawFrame(graphics, data);

    Window_FillTilemap(&graphics->leftWindow, 0);
    Window_FillTilemap(&graphics->rightWindow, 0);

    DrawLeftText(graphics, data, heapID);
    ShowSpeciesTypes(graphics, data);

    switch (data->kind) {
    case POKEDEX_WIDE_PAGE_STATS:
        DrawStatsPage(graphics, data, heapID);
        break;
    case POKEDEX_WIDE_PAGE_MOVES:
        DrawMovesPage(graphics, data, heapID);
        break;
    case POKEDEX_WIDE_PAGE_EVOS:
        DrawEvosPage(graphics, data, heapID);
        break;
    case POKEDEX_WIDE_PAGE_INFO:
        DrawInfoPage(graphics, data, heapID);
        break;
    }

    PokedexCanvas_Upload(graphics->canvas, graphicData->bgConfig, BG_LAYER_MAIN_3, 1, 1023, 0, 0);
    Window_CopyToVRAM(&graphics->leftWindow);
    Window_CopyToVRAM(&graphics->rightWindow);

    // The Pokémon sprite, centered in its box: the 3D layer moves to put native x 48 there.
    const int spriteX = graphics->leftX + graphics->leftW / 2;

    PokedexWide_Set3DOffset(spriteX - graphics->left - 48);
    PokedexMain_DisplayPokemonSprite(graphicData, data->sortData, data->species, 2, 48, PANEL_TOP + SPRITE_BOX_H / 2);
    CenterSpriteArt(PokemonGraphics_GetPokemonChar(graphicData), 48, PANEL_TOP + SPRITE_BOX_H / 2, heapID);
    PokemonSprite_SetAttribute(PokemonGraphics_GetPokemonChar(graphicData), MON_SPRITE_HIDE, FALSE);
    PokemonSprite_SetAttribute(PokemonGraphics_GetPokemonChar(graphicData), MON_SPRITE_ALPHA, 31);

    graphics->drawnSpecies = data->species;
    graphics->drawnScroll = data->scroll;
    graphics->drawnHeightBank = Height_Message_Bank_Index();
}

static void SetSpritesShown(PageGraphics *graphics, BOOL shown)
{
    graphics->spritesShown = shown;

    for (int i = 0; i < MAX_TYPE_SPRITES; i++) {
        if (!shown) {
            Sprite_SetDrawFlag(graphics->typeSprites[i], FALSE);
        }
    }
}

static void StartFade(PageGraphics *graphics, PokedexGraphicData *graphicData, BOOL fadeIn)
{
    // pokedex_graphics.c widens this to every layer and the sprites (PokedexWide_AdjustFade).
    const GXBlendPlaneMask planes = GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG3;

    if (fadeIn) {
        PokedexGraphics_InitBlendTransition(&graphicData->blendMain, 3, -16, 0, 0, 16, planes, planes, FALSE);
    } else {
        PokedexGraphics_InitBlendTransition(&graphicData->blendMain, 3, 0, -16, 16, 0, planes, 0, FALSE);
    }

    graphics->fade = fadeIn ? PAGE_FADE_IN : PAGE_FADE_OUT;
}

static BOOL PageGraphics_Init(void *graphicsData, PokedexGraphicsManager *graphicsMan, const void *pageData, const PokedexDataManager *dataMan)
{
    PokedexGraphicData *graphicData = *(PokedexGraphicData **)graphicsData;
    const PageData *data = pageData;
    PageGraphics *graphics = graphicsMan->pageGraphics;
    const enum HeapID heapID = graphicsMan->heapID;

    switch (graphicsMan->state) {
    case 0: {
        graphics = Heap_Alloc(heapID, sizeof(PageGraphics));
        memset(graphics, 0, sizeof(PageGraphics));
        graphicsMan->pageGraphics = graphics;

        // Opening an entry from the grid, this page is set up before the tab strip.
        if (!PokedexWide_IsActive()) {
            PokedexWide_Begin();
        }

        graphics->width = PokedexWide_Width();
        graphics->left = PokedexWide_Left();
        graphics->leftX = 8;

        int leftW = ((graphics->width - 24) * 34 / 100) & ~7;
        graphics->leftW = leftW < 96 ? 96 : leftW > 144 ? 144 : leftW;
        graphics->rightX = graphics->leftX + graphics->leftW + 8;
        graphics->rightW = graphics->width - 8 - graphics->rightX;

        BgConfig *bgConfig = graphicData->bgConfig;

        PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_1, TRUE);
        PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_3, TRUE);
        PokedexWide_LoadPalette();

        const int leftRows = 24 - NAME_BOX_TOP / 8;

        Window_Add(bgConfig, &graphics->leftWindow, BG_LAYER_MAIN_1, graphics->leftX / 8, NAME_BOX_TOP / 8, graphics->leftW / 8, leftRows, POKEDEX_WIDE_PLTT, 1);
        Window_Add(bgConfig, &graphics->rightWindow, BG_LAYER_MAIN_1, graphics->rightX / 8, PANEL_TOP / 8, graphics->rightW / 8, 24 - PANEL_TOP / 8, POKEDEX_WIDE_PLTT, 1 + (graphics->leftW / 8) * leftRows);

        graphics->canvas = PokedexCanvas_New(graphics->width, 192, heapID);
        LoadTypeIcons(graphics, graphicData, heapID);

        if (data->kind == POKEDEX_WIDE_PAGE_EVOS) {
            SetupIcons(graphics, graphicData, heapID);
        }

        if (graphicData->pokedexSpeciesLabel.textData) {
            PokedexGraphics_SetPokedexSpeciesLabelDraw(graphicData, FALSE);
        }

        // The screen is black (PokedexWide_FadeStep); the badges and icons fade in with the page.
        graphics->spritesShown = TRUE;
        Redraw(graphics, graphicData, data, heapID);
        StartFade(graphics, graphicData, TRUE);
        graphicsMan->state++;
        break;
    }
    case 1:
        EnterIconOams(graphics);

        if (PokedexGraphics_TakeBlendTransitionStep(&graphicData->blendMain)) {
            graphics->fade = PAGE_FADE_NONE;
            return TRUE;
        }
        break;
    }

    return FALSE;
}

static BOOL PageGraphics_Main(void *graphicsData, PokedexGraphicsManager *graphicsMan, const void *pageData, const PokedexDataManager *dataMan)
{
    PokedexGraphicData *graphicData = *(PokedexGraphicData **)graphicsData;
    const PageData *data = pageData;
    PageGraphics *graphics = graphicsMan->pageGraphics;

    // The bank changes with the units setting in the Tab menu.
    const BOOL unitsChanged = data->kind == POKEDEX_WIDE_PAGE_INFO && graphics->drawnHeightBank != Height_Message_Bank_Index();

    if (graphics->drawnSpecies != data->species || graphics->drawnScroll != data->scroll || unitsChanged) {
        Redraw(graphics, graphicData, data, graphicsMan->heapID);
    }

    EnterIconOams(graphics);
    return FALSE;
}

static BOOL PageGraphics_Exit(void *graphicsData, PokedexGraphicsManager *graphicsMan, const void *pageData, const PokedexDataManager *dataMan)
{
    PokedexGraphicData *graphicData = *(PokedexGraphicData **)graphicsData;
    const PageData *data = pageData;
    PageGraphics *graphics = graphicsMan->pageGraphics;

    switch (graphicsMan->state) {
    case 0:
        StartFade(graphics, graphicData, FALSE);
        EnterIconOams(graphics);
        graphicsMan->state++;
        break;
    case 1:
        EnterIconOams(graphics);

        if (PokedexGraphics_TakeBlendTransitionStep(&graphicData->blendMain)) {
            SetSpritesShown(graphics, FALSE);
            graphicsMan->state++;
        }
        break;
    case 2: {
        BgConfig *bgConfig = graphicData->bgConfig;

        FreeTypeIcons(graphics, graphicData);

        if (data->kind == POKEDEX_WIDE_PAGE_EVOS) {
            FreeIcons(graphics, graphicData);
        }

        Window_Remove(&graphics->leftWindow);
        Window_Remove(&graphics->rightWindow);
        PokedexCanvas_Free(graphics->canvas);

        PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_1, FALSE);
        PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_3, FALSE);
        Window_PutToTilemap(&graphicData->window);

        // Platinum's pages place the sprite in the native screen.
        PokedexWide_Set3DOffset(0);
        PokemonSprite_SetAttribute(PokemonGraphics_GetPokemonChar(graphicData), MON_SPRITE_HIDE, TRUE);

        Heap_Free(graphics);
        graphicsMan->pageGraphics = NULL;
        return TRUE;
    }
    }

    return FALSE;
}

static void InitPage(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID, int kind)
{
    PageData *data = Heap_Alloc(heapID, sizeof(PageData));
    PokedexGraphicData **graphics = Heap_Alloc(heapID, sizeof(PokedexGraphicData *));

    memset(data, 0, sizeof(PageData));
    data->sortData = PokedexMain_GetSortData(pokedexApp);
    data->infoState.sortData = data->sortData;
    data->kind = kind;
    *graphics = PokedexMain_GetGraphicData(pokedexApp);

    screenMan->pageData = data;
    screenMan->pageGraphics = graphics;
    screenMan->screenStates = NULL;
    screenMan->numStates = 0;
    screenMan->dataFunc[0] = PageData_Init;
    screenMan->dataFunc[1] = PageData_Main;
    screenMan->dataFunc[2] = PageData_Exit;
    screenMan->graphicsFunc[0] = PageGraphics_Init;
    screenMan->graphicsFunc[1] = PageGraphics_Main;
    screenMan->graphicsFunc[2] = PageGraphics_Exit;
}

void PokedexInfoPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID)
{
    InitPage(screenMan, pokedexApp, heapID, POKEDEX_WIDE_PAGE_INFO);
}

void PokedexStatsPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID)
{
    InitPage(screenMan, pokedexApp, heapID, POKEDEX_WIDE_PAGE_STATS);
}

void PokedexMovesPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID)
{
    InitPage(screenMan, pokedexApp, heapID, POKEDEX_WIDE_PAGE_MOVES);
}

void PokedexEvosPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID)
{
    InitPage(screenMan, pokedexApp, heapID, POKEDEX_WIDE_PAGE_EVOS);
}

void PokedexWidePage_Free(PokedexScreenManager *screenMan)
{
    Heap_Free(screenMan->pageData);
    Heap_Free(screenMan->pageGraphics);
}

void PokedexWidePage_InitTabState(enum HeapID heapID, UnkStruct_ov21_021D4660 *state, PokedexApp *pokedexApp, int requestBit, int page)
{
    UnkStruct_ov21_021D4558 *transition = Heap_Alloc(heapID, sizeof(UnkStruct_ov21_021D4558));

    memset(transition, 0, sizeof(UnkStruct_ov21_021D4558));

    // Swap the page slot (main and sub updaters A) for the page, leaving the sub screen empty.
    transition->unk_00 = ov21_021D12EC(pokedexApp);
    transition->unk_04 = ov21_021D1300(pokedexApp);
    transition->unk_08 = ov21_021D133C(pokedexApp);
    transition->unk_0C = ov21_021D1350(pokedexApp);
    transition->unk_10 = ov21_021D1410(pokedexApp, POKEDEX_WIDE_PAGE_FIRST_SCREEN + page);
    transition->unk_14 = NULL;

    state->unk_00 = requestBit;
    state->unk_04 = ov21_021D4558;
    state->unk_08.unk_08 = transition;
}
