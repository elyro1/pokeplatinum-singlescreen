#include "applications/pokedex/pokedex_grid.h"

#include <nitro.h>
#include <string.h>

#include "constants/narc.h"
#include "constants/species.h"

#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_main.h"
#include "applications/pokedex/pokedex_sort.h"
#include "applications/pokedex/pokedex_sort_data.h"
#include "applications/pokedex/pokedex_wide.h"
#include "applications/pokedex/species_caught_status.h"

#include "bg_window.h"
#include "char_transfer.h"
#include "font.h"
#include "heap.h"
#include "message.h"
#include "narc.h"
#include "pltt_transfer.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_sprite.h"
#include "render_oam.h"
#include "sound_playback.h"
#include "sprite_resource.h"
#include "sprite_transfer.h"
#include "string_gf.h"
#include "system.h"
#include "text.h"

#include "res/text/bank/pokedex.h"

#define CELL_WIDTH    40
#define CELL_HEIGHT   44
#define GRID_TOP      16
#define GRID_ROWS     4
#define MAX_COLUMNS   (POKEDEX_WIDE_MAX_WIDTH / CELL_WIDTH)
#define MAX_CELLS     (MAX_COLUMNS * GRID_ROWS)
#define ICON_BYTES    (32 * 32 / 2)
#define ICON_PALETTES 3
// A resource ID of the Pokédex's sprite collections that no other screen uses.
#define ICON_PALETTE_RESOURCE_ID 9200
// The selected icon flaps between its two frames.
#define FLAP_FRAMES 10

#define BG_TILES_START   1
#define BG_TILES_MAX     1023
#define TEXT_TILES_START 1

enum GridFade {
    GRID_FADE_NONE,
    GRID_FADE_IN,
    GRID_FADE_OUT,
};

struct PokedexGrid {
    PokedexGraphicData *graphicData;
    const PokedexSortData *sortData;
    enum HeapID heapID;
    int width;
    int left;
    int columns;
    int gridX;
    int firstRow;
    int selected;
    int numDisplayed;
    int isNational;
    int drawnFirstRow;
    int drawnSelected;
    int drawnNumDisplayed;
    BOOL dirty;
    BOOL pendingPage;
    BOOL pendingSelection;
    u16 slotSpecies[MAX_CELLS];
    u8 slotStatus[MAX_CELLS];
    PokedexCanvas *canvas;
    Window rowWindows[GRID_ROWS];
    Window titleWindow;
    Window countWindow;
    NARC *iconNarc;
    CharTransferAllocation iconVram;
    SpriteResource *iconPalette;
    u32 iconPaletteOffset;
    u8 *selectedFrames;
    int flapTimer;
    enum GridFade fade;
};

static int sColumns = 6;

static int GridColumnsForWidth(int width)
{
    int columns = (width - 8) / CELL_WIDTH;
    return columns > MAX_COLUMNS ? MAX_COLUMNS : columns;
}

static int CellX(const PokedexGrid *grid, int column)
{
    return grid->gridX + column * CELL_WIDTH;
}

static int CellY(int row)
{
    return GRID_TOP + row * CELL_HEIGHT;
}

// The tile row of a card row's number window, three tiles tall.
static int RowWindowTop(int row)
{
    return (CellY(row) - 1) / 8;
}

static const SpeciesCaughtStatus *DisplayStatus(const PokedexSortData *sortData, int displayIndex)
{
    if (!PokedexSort_IsValidDisplayIndex(sortData, displayIndex)) {
        return NULL;
    }

    return PokedexSort_StatusIndexToCaughtStatus(sortData, PokedexSort_DisplayIndexToStatusIndex(sortData, displayIndex));
}

static int DexNumber(const PokedexGrid *grid, int species)
{
    return grid->isNational ? species : Pokemon_SinnohDexNumber(species);
}

static void LoadIconFrames(PokedexGrid *grid, int species, u8 *dest, int bytes)
{
    void *file = NARC_AllocAndReadWholeMember(grid->iconNarc, PokeIconSpriteIndex(species, FALSE, 0), grid->heapID);
    NNSG2dCharacterData *charData;

    if (NNS_G2dGetUnpackedCharacterData(file, &charData)) {
        memcpy(dest, charData->pRawData, bytes);
    } else {
        memset(dest, 0, bytes);
    }

    Heap_Free(file);
}

// The icon tiles live in one OBJ VRAM block: a 32x32 frame per grid cell, then both
// frames of the selected icon.
static u32 IconVramOffset(const PokedexGrid *grid, int slot)
{
    return grid->iconVram.offset + slot * ICON_BYTES;
}

static void UploadIcons(PokedexGrid *grid)
{
    u8 *frames = Heap_Alloc(grid->heapID, ICON_BYTES * 2);

    for (int slot = 0; slot < grid->columns * GRID_ROWS; slot++) {
        if (grid->slotStatus[slot] == CS_UNENCOUNTERED) {
            continue;
        }

        LoadIconFrames(grid, grid->slotSpecies[slot], frames, ICON_BYTES);
        DC_FlushRange(frames, ICON_BYTES);
        GX_LoadOBJ(frames, IconVramOffset(grid, slot), ICON_BYTES);
    }

    Heap_Free(frames);
}

static void UploadSelectedIcon(PokedexGrid *grid)
{
    const int slot = grid->selected - grid->firstRow * grid->columns;

    if (slot < 0 || slot >= grid->columns * GRID_ROWS || grid->slotStatus[slot] == CS_UNENCOUNTERED) {
        return;
    }

    LoadIconFrames(grid, grid->slotSpecies[slot], grid->selectedFrames, ICON_BYTES * 2);
    DC_FlushRange(grid->selectedFrames, ICON_BYTES * 2);
    GX_LoadOBJ(grid->selectedFrames, IconVramOffset(grid, MAX_CELLS), ICON_BYTES * 2);
}

static void SetupIcons(PokedexGrid *grid)
{
    PokedexGraphicData *graphicData = grid->graphicData;

    grid->iconNarc = NARC_ctor(NARC_INDEX_POKETOOL__ICONGRA__PL_POKE_ICON, grid->heapID);

    BOOL allocated = CharTransfer_AllocRange(ICON_BYTES * (MAX_CELLS + 2), TRUE, NNS_G2D_VRAM_TYPE_2DMAIN, &grid->iconVram);
    GF_ASSERT(allocated);

    grid->iconPalette = SpriteResourceCollection_AddPaletteFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], grid->iconNarc, PokeIconPalettesFileIndex(), FALSE, ICON_PALETTE_RESOURCE_ID, NNS_G2D_VRAM_TYPE_2DMAIN, ICON_PALETTES, grid->heapID);
    SpriteTransfer_RequestPlttFreeSpace(grid->iconPalette);
    SpriteResource_ReleaseData(grid->iconPalette);
    grid->iconPaletteOffset = PlttTransfer_GetPlttOffset(SpriteTransfer_GetPaletteProxy(grid->iconPalette, NULL), NNS_G2D_VRAM_TYPE_2DMAIN);

    grid->selectedFrames = Heap_Alloc(grid->heapID, ICON_BYTES * 2);
}

static void FreeIcons(PokedexGrid *grid)
{
    SpriteTransfer_ResetPlttTransfer(grid->iconPalette);
    SpriteResourceCollection_Remove(grid->graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], grid->iconPalette);
    CharTransfer_ClearRange(&grid->iconVram);
    NARC_dtor(grid->iconNarc);
    Heap_Free(grid->selectedFrames);
}

static void EnterIconOams(PokedexGrid *grid)
{
    GXOamAttr oams[MAX_CELLS];
    int count = 0;
    // OBJ VRAM is mapped in 128-byte units.
    const u32 charBase = grid->iconVram.offset / 128;

    for (int slot = 0; slot < grid->columns * GRID_ROWS; slot++) {
        if (grid->slotStatus[slot] == CS_UNENCOUNTERED) {
            continue;
        }

        const int column = slot % grid->columns;
        const int row = slot / grid->columns;
        const int x = CellX(grid, column) + 4 - grid->left;
        const int y = CellY(row) + 11;
        const BOOL selected = grid->firstRow * grid->columns + slot == grid->selected;
        u32 charName = charBase + slot * (ICON_BYTES / 128);

        if (selected) {
            charName = charBase + (MAX_CELLS + ((grid->flapTimer / FLAP_FRAMES) & 1)) * (ICON_BYTES / 128);
        }

        const u32 palette = grid->iconPaletteOffset + PokeIconPaletteIndex(grid->slotSpecies[slot], 0, FALSE);

        // Square 32x32, 16 colors, priority 1: above the card backgrounds (BG3),
        // below the number text (BG1).
        oams[count].attr0 = y & 0xFF;
        oams[count].attr1 = (x & 0x1FF) | (2 << 14);
        oams[count].attr2 = (charName & 0x3FF) | (1 << 10) | ((palette & 0xF) << 12);
        oams[count]._3 = 0;
        count++;
    }

    if (count > 0) {
        NNS_G2dEntryOamManagerOam(RenderOam_GetScreenOam(DS_SCREEN_MAIN), oams, count);
    }
}

static void DrawBackground(PokedexGrid *grid, const PokedexSortData *sortData)
{
    PokedexCanvas *canvas = grid->canvas;

    PokedexCanvas_Stripes(canvas, 0, 0, canvas->width, canvas->height, PDW_OLIVE, PDW_OLIVE_LIGHT);

    // Header band with the title pill on the left.
    PokedexCanvas_Fill(canvas, 0, 0, canvas->width, 15, PDW_GOLD);
    PokedexCanvas_Fill(canvas, 0, 15, canvas->width, 1, PDW_GOLD_DARK);
    PokedexCanvas_Panel(canvas, 4, 1, 116, 13, PDW_RED, PDW_RED_DARK);
    PokedexCanvas_Fill(canvas, 6, 2, 112, 1, PDW_WHITE);

    for (int slot = 0; slot < grid->columns * GRID_ROWS; slot++) {
        const int x = CellX(grid, slot % grid->columns);
        const int y = CellY(slot / grid->columns);
        const int displayIndex = grid->firstRow * grid->columns + slot;

        if (displayIndex >= grid->numDisplayed) {
            continue;
        }

        const BOOL selected = displayIndex == grid->selected;
        const u8 border = selected ? PDW_RED_DARK : PDW_DARK;

        PokedexCanvas_Panel(canvas, x + 2, y + 2, 36, 41, PDW_CREAM_LIGHT, border);
        PokedexCanvas_Fill(canvas, x + 3, y + 3, 34, 10, selected ? PDW_ORANGE : PDW_GOLD);
        PokedexCanvas_Fill(canvas, x + 3, y + 13, 34, 1, selected ? PDW_RED_DARK : PDW_GOLD_DARK);

        if (selected) {
            // A two-dot frame around the selected card.
            PokedexCanvas_Fill(canvas, x + 1, y + 2, 1, 41, PDW_RED_DARK);
            PokedexCanvas_Fill(canvas, x + 38, y + 2, 1, 41, PDW_RED_DARK);
            PokedexCanvas_Fill(canvas, x + 2, y + 1, 36, 1, PDW_RED_DARK);
            PokedexCanvas_Fill(canvas, x + 2, y + 43, 36, 1, PDW_RED_DARK);
        }

        switch (grid->slotStatus[slot]) {
        case CS_UNENCOUNTERED:
            PokedexCanvas_Question(canvas, x + 15, y + 20, PDW_GREY, PDW_WHITE);
            break;
        case CS_CAUGHT:
            PokedexCanvas_Ball(canvas, x + 25, y + 30);
            break;
        default:
            break;
        }
    }

    // Scroll position along the right edge.
    const int totalRows = (grid->numDisplayed + grid->columns - 1) / grid->columns;

    if (totalRows > GRID_ROWS) {
        const int trackX = grid->width - 5;
        const int trackY = GRID_TOP + 3;
        const int trackHeight = 192 - GRID_TOP - 6;
        const int thumbHeight = trackHeight * GRID_ROWS / totalRows < 8 ? 8 : trackHeight * GRID_ROWS / totalRows;
        const int thumbY = trackY + (trackHeight - thumbHeight) * grid->firstRow / (totalRows - GRID_ROWS);

        PokedexCanvas_Fill(canvas, trackX, trackY, 3, trackHeight, PDW_DARK);
        PokedexCanvas_Fill(canvas, trackX, thumbY, 3, thumbHeight, PDW_RED);
    }

    PokedexCanvas_Upload(canvas, grid->graphicData->bgConfig, BG_LAYER_MAIN_3, BG_TILES_START, BG_TILES_MAX, 0, 0);
}

static void PrintText(Window *window, const String *string, int x, int y, TextColor color)
{
    Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, string, x, y, TEXT_SPEED_NO_TRANSFER, color, NULL);
}

// The cards' numbers, 6x7 digits with two-dot strokes. FONT_SUBSCREEN's digits are
// too narrow for its outline: the 8 closed up and the shadow ran along the top left.
#define DIGIT_WIDTH  6
#define DIGIT_HEIGHT 7
#define DIGIT_GAP    1

static const char *sDigits[10][DIGIT_HEIGHT] = {
    { ".####.", "##..##", "##..##", "##..##", "##..##", "##..##", ".####." },
    { "..##..", ".###..", "..##..", "..##..", "..##..", "..##..", ".####." },
    { ".####.", "##..##", "....##", "...##.", "..##..", ".##...", "######" },
    { ".####.", "##..##", "....##", "..###.", "....##", "##..##", ".####." },
    { "...##.", "..###.", ".####.", "##.##.", "######", "...##.", "...##." },
    { "######", "##....", "#####.", "....##", "....##", "##..##", ".####." },
    { "..###.", ".##...", "##....", "#####.", "##..##", "##..##", ".####." },
    { "######", "....##", "...##.", "..##..", ".##...", ".##...", ".##..." },
    { ".####.", "##..##", "##..##", ".####.", "##..##", "##..##", ".####." },
    { ".####.", "##..##", "##..##", ".#####", "....##", "...##.", ".###.." },
};

static void DrawDigit(Window *window, int digit, int x, int y, u8 color)
{
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        const char *line = sDigits[digit][row];

        for (int col = 0; col < DIGIT_WIDTH;) {
            if (line[col] != '#') {
                col++;
                continue;
            }

            int end = col;

            while (end < DIGIT_WIDTH && line[end] == '#') {
                end++;
            }

            Window_FillRectWithColor(window, color, x + col, y + row, end - col, 1);
            col = end;
        }
    }
}

static void DrawNumbers(PokedexGrid *grid)
{
    const int textWidth = 3 * DIGIT_WIDTH + 2 * DIGIT_GAP;

    for (int row = 0; row < GRID_ROWS; row++) {
        Window *window = &grid->rowWindows[row];
        // The card's number band is dots 3-12; the digits and their shadow take 4-11.
        const int textY = CellY(row) + 4 - RowWindowTop(row) * 8;

        Window_FillTilemap(window, 0);

        for (int column = 0; column < grid->columns; column++) {
            const int slot = row * grid->columns + column;

            if (grid->firstRow * grid->columns + slot >= grid->numDisplayed) {
                continue;
            }

            // Blank (unseen) entries only appear in the numerical listing, where the
            // display position is the dex number.
            const int dexNumber = grid->slotSpecies[slot] != SPECIES_NONE
                ? DexNumber(grid, grid->slotSpecies[slot])
                : grid->firstRow * grid->columns + slot + 1;
            // Centered on the card, which is dots 2-37 of the cell.
            const int textX = column * CELL_WIDTH + 2 + (36 - textWidth) / 2;

            for (int pass = 0; pass < 2; pass++) {
                int value = dexNumber;

                for (int place = 2; place >= 0; place--) {
                    const int x = textX + place * (DIGIT_WIDTH + DIGIT_GAP) + (pass ? 0 : 1);

                    DrawDigit(window, value % 10, x, textY + (pass ? 0 : 1), pass ? PDW_TEXT : PDW_GOLD_DARK);
                    value /= 10;
                }
            }
        }

        Window_CopyToVRAM(window);
    }
}

static void DrawHeader(PokedexGrid *grid, const PokedexSortData *sortData)
{
    MessageLoader *loader = MessageLoader_Init(MSG_LOADER_PRELOAD_ENTIRE_BANK, NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_POKEDEX, grid->heapID);
    String *string;

    Window_FillTilemap(&grid->titleWindow, 0);
    string = MessageLoader_GetNewString(loader, grid->isNational ? pl_msg_pokedex_wide_national : pl_msg_pokedex_wide_sinnoh);
    PrintText(&grid->titleWindow, string, (112 - Font_CalcStringWidth(FONT_SYSTEM, string, 0)) / 2, 0, TEXT_COLOR(PDW_WHITE, PDW_RED_DARK, 0));
    String_Free(string);
    Window_CopyToVRAM(&grid->titleWindow);

    Window_FillTilemap(&grid->countWindow, 0);

    const int countWidth = grid->countWindow.width * 8;
    String *value = String_Init(8, grid->heapID);
    int x = countWidth - 4;

    String_FormatInt(value, PokedexSort_NumCaught(sortData), 3, PADDING_MODE_ZEROES, CHARSET_MODE_EN);
    x -= Font_CalcStringWidth(FONT_SYSTEM, value, 0);
    PrintText(&grid->countWindow, value, x, 0, TEXT_COLOR(PDW_TEXT, PDW_CREAM, 0));

    string = MessageLoader_GetNewString(loader, pl_msg_pokedex_obtained);
    x -= Font_CalcStringWidth(FONT_SYSTEM, string, 0) + 4;
    PrintText(&grid->countWindow, string, x, 0, TEXT_COLOR(PDW_TEXT, PDW_CREAM, 0));
    String_Free(string);

    String_FormatInt(value, PokedexSort_NumEncountered(sortData), 3, PADDING_MODE_ZEROES, CHARSET_MODE_EN);
    x -= Font_CalcStringWidth(FONT_SYSTEM, value, 0) + 12;
    PrintText(&grid->countWindow, value, x, 0, TEXT_COLOR(PDW_TEXT, PDW_CREAM, 0));

    string = MessageLoader_GetNewString(loader, pl_msg_pokedex_seen);
    x -= Font_CalcStringWidth(FONT_SYSTEM, string, 0) + 4;
    PrintText(&grid->countWindow, string, x, 0, TEXT_COLOR(PDW_TEXT, PDW_CREAM, 0));
    String_Free(string);

    String_Free(value);
    Window_CopyToVRAM(&grid->countWindow);
    MessageLoader_Free(loader);
}

static void SetupWindows(PokedexGrid *grid)
{
    BgConfig *bgConfig = grid->graphicData->bgConfig;
    const int tileWidth = PokedexWide_TileWidth();
    u16 baseTile = TEXT_TILES_START;

    // The header text is on BG2: the first row's numbers need BG1's tile row 1.
    Window_Add(bgConfig, &grid->titleWindow, BG_LAYER_MAIN_2, 1, 0, 14, 2, POKEDEX_WIDE_PLTT, TEXT_TILES_START);

    const int countLeft = 16;
    Window_Add(bgConfig, &grid->countWindow, BG_LAYER_MAIN_2, countLeft, 0, tileWidth - countLeft, 2, POKEDEX_WIDE_PLTT, TEXT_TILES_START + 14 * 2);

    for (int row = 0; row < GRID_ROWS; row++) {
        const int width = grid->columns * CELL_WIDTH / 8;

        Window_Add(bgConfig, &grid->rowWindows[row], BG_LAYER_MAIN_1, grid->gridX / 8, RowWindowTop(row), width, 3, POKEDEX_WIDE_PLTT, baseTile);
        baseTile += width * 3;
    }
}

static void FreeWindows(PokedexGrid *grid)
{
    Window_Remove(&grid->titleWindow);
    Window_Remove(&grid->countWindow);

    for (int row = 0; row < GRID_ROWS; row++) {
        Window_Remove(&grid->rowWindows[row]);
    }
}

static void ReadSlots(PokedexGrid *grid, const PokedexSortData *sortData)
{
    for (int slot = 0; slot < grid->columns * GRID_ROWS; slot++) {
        const SpeciesCaughtStatus *status = DisplayStatus(sortData, grid->firstRow * grid->columns + slot);

        grid->slotSpecies[slot] = status ? status->species : SPECIES_NONE;
        grid->slotStatus[slot] = status ? status->caughtStatus : CS_UNENCOUNTERED;
    }
}

// A frame is shown before its VBlank applies the OAM entered for it, so it pairs what
// is written to VRAM right away with the previous frame's OAM. These writes wait a
// frame for the OAM entered with the change: new icon tiles under the old entries
// showed the moved cursor's icon on the card it left, in that card's palette.
static void FlushPendingDraws(PokedexGrid *grid)
{
    if (grid->pendingPage) {
        UploadIcons(grid);
        DrawNumbers(grid);
        DrawHeader(grid, grid->sortData);
    }

    if (grid->pendingSelection) {
        DrawBackground(grid, grid->sortData);
        UploadSelectedIcon(grid);
    }

    grid->pendingPage = FALSE;
    grid->pendingSelection = FALSE;
}

static void Refresh(PokedexGrid *grid, const PokedexSortData *sortData)
{
    const BOOL scrolled = grid->drawnFirstRow != grid->firstRow || grid->drawnNumDisplayed != grid->numDisplayed || grid->dirty;

    if (scrolled) {
        ReadSlots(grid, sortData);
        grid->pendingPage = TRUE;
    }

    if (scrolled || grid->drawnSelected != grid->selected) {
        grid->pendingSelection = TRUE;
        grid->flapTimer = 0;
    }

    grid->drawnFirstRow = grid->firstRow;
    grid->drawnSelected = grid->selected;
    grid->drawnNumDisplayed = grid->numDisplayed;
    grid->dirty = FALSE;
}

static void FollowSelection(PokedexGrid *grid, const PokedexSortData *sortData)
{
    grid->numDisplayed = PokedexSort_NumDisplayed(sortData);
    grid->selected = PokedexSort_CurrentDisplayIndex(sortData);
    grid->isNational = PokedexSort_IsNationalDex(sortData);

    const int row = grid->selected / grid->columns;
    const int totalRows = (grid->numDisplayed + grid->columns - 1) / grid->columns;
    const int lastFirstRow = totalRows > GRID_ROWS ? totalRows - GRID_ROWS : 0;

    if (row < grid->firstRow) {
        grid->firstRow = row;
    } else if (row >= grid->firstRow + GRID_ROWS) {
        grid->firstRow = row - GRID_ROWS + 1;
    }

    if (grid->firstRow > lastFirstRow) {
        grid->firstRow = lastFirstRow;
    }
}

PokedexGrid *PokedexGrid_New(PokedexGraphicData *graphicData, const PokedexSortData *sortData, enum HeapID heapID)
{
    PokedexGrid *grid = Heap_Alloc(heapID, sizeof(PokedexGrid));

    memset(grid, 0, sizeof(PokedexGrid));
    grid->graphicData = graphicData;
    grid->sortData = sortData;
    grid->heapID = heapID;
    grid->width = PokedexWide_Begin();
    grid->left = PokedexWide_Left();
    grid->columns = GridColumnsForWidth(grid->width);
    grid->gridX = ((grid->width - grid->columns * CELL_WIDTH) / 2) & ~7;
    grid->drawnFirstRow = -1;
    grid->drawnSelected = -1;
    grid->dirty = TRUE;
    sColumns = grid->columns;

    BgConfig *bgConfig = graphicData->bgConfig;

    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_1, TRUE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_2, TRUE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_3, TRUE);
    PokedexWide_LoadPalette();

    PokemonSprite *monSprite = PokemonGraphics_GetPokemonChar(graphicData);

    if (monSprite) {
        PokemonSprite_SetAttribute(monSprite, MON_SPRITE_HIDE, TRUE);
    }

    // The info screens leave the species name tag up.
    if (graphicData->pokedexSpeciesLabel.textData) {
        PokedexGraphics_SetPokedexSpeciesLabelDraw(graphicData, FALSE);
    }

    grid->canvas = PokedexCanvas_New(grid->width, 192, heapID);
    SetupWindows(grid);
    SetupIcons(grid);

    FollowSelection(grid, sortData);
    grid->firstRow = grid->selected / grid->columns - 1;

    if (grid->firstRow < 0) {
        grid->firstRow = 0;
    }

    FollowSelection(grid, sortData);
    Refresh(grid, sortData);
    FlushPendingDraws(grid);

    return grid;
}

void PokedexGrid_Update(PokedexGrid *grid, const PokedexSortData *sortData)
{
    FlushPendingDraws(grid);

    if (grid->numDisplayed != PokedexSort_NumDisplayed(sortData) || grid->isNational != PokedexSort_IsNationalDex(sortData)) {
        grid->dirty = TRUE;
    }

    FollowSelection(grid, sortData);
    Refresh(grid, sortData);

    grid->flapTimer++;
    EnterIconOams(grid);

    if (grid->fade != GRID_FADE_NONE && PokedexGraphics_TakeBlendTransitionStep(&grid->graphicData->blendMain)) {
        grid->fade = GRID_FADE_NONE;
    }
}

void PokedexGrid_StartFade(PokedexGrid *grid, BOOL fadeIn)
{
    FlushPendingDraws(grid);

    const GXBlendPlaneMask planes = GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ | GX_BLEND_PLANEMASK_BD;

    if (fadeIn) {
        PokedexGraphics_InitBlendTransition(&grid->graphicData->blendMain, 4, -16, 0, 0, 16, planes, planes, FALSE);
        grid->fade = GRID_FADE_IN;
    } else {
        PokedexGraphics_InitBlendTransition(&grid->graphicData->blendMain, 4, 0, -16, 16, 0, planes, 0, FALSE);
        grid->fade = GRID_FADE_OUT;
    }

    // The screens start fades in their exit step, where nothing else enters the icons:
    // without these they were gone for a frame as the fade began.
    EnterIconOams(grid);
}

BOOL PokedexGrid_FadeStep(PokedexGrid *grid)
{
    if (grid->fade == GRID_FADE_NONE) {
        return TRUE;
    }

    FlushPendingDraws(grid);
    EnterIconOams(grid);

    if (PokedexGraphics_TakeBlendTransitionStep(&grid->graphicData->blendMain)) {
        grid->fade = GRID_FADE_NONE;
        return TRUE;
    }

    return FALSE;
}

void PokedexGrid_Free(PokedexGrid *grid, PokedexSortData *sortData)
{
    BgConfig *bgConfig = grid->graphicData->bgConfig;

    // The info screens move the Pokémon sprite the list kept for the selection.
    const SpeciesCaughtStatus *status = DisplayStatus(sortData, PokedexSort_CurrentDisplayIndex(sortData));

    if (status) {
        PokedexMain_DisplayPokemonSprite(grid->graphicData, sortData, status->species, 2, 48, 72);
        PokemonSprite_SetAttribute(PokemonGraphics_GetPokemonChar(grid->graphicData), MON_SPRITE_HIDE, TRUE);
    }

    FreeIcons(grid);
    FreeWindows(grid);
    PokedexCanvas_Free(grid->canvas);

    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_1, FALSE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_2, FALSE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_3, FALSE);
    Window_PutToTilemap(&grid->graphicData->window);
    PokedexWide_End();
    Heap_Free(grid);
}

void PokedexGrid_PlayCry(const PokedexSortData *sortData)
{
    const SpeciesCaughtStatus *status = DisplayStatus(sortData, PokedexSort_CurrentDisplayIndex(sortData));

    if (status == NULL || status->caughtStatus == CS_UNENCOUNTERED) {
        return;
    }

    // As the info screen plays it, in the form the entry shows.
    Sound_StopPokemonCries(0);
    Sound_PlayPokemonCryEx(POKECRY_POKEDEX, status->species, 0x1ff, 0x1ff, 0x1ff, PokedexSort_DefaultForm(sortData, status->species));
}

BOOL PokedexGrid_HandleInput(PokedexSortData *sortData)
{
    const int count = PokedexSort_NumDisplayed(sortData);
    const int current = PokedexSort_CurrentDisplayIndex(sortData);
    int target = current;

    if (JOY_REPEAT(PAD_KEY_LEFT)) {
        target = current - 1;
    } else if (JOY_REPEAT(PAD_KEY_RIGHT)) {
        target = current + 1;
    } else if (JOY_REPEAT(PAD_KEY_UP)) {
        target = current - sColumns;
    } else if (JOY_REPEAT(PAD_KEY_DOWN)) {
        target = current + sColumns;
    } else if (JOY_REPEAT(PAD_BUTTON_L)) {
        target = current - sColumns * GRID_ROWS;
    } else if (JOY_REPEAT(PAD_BUTTON_R)) {
        target = current + sColumns * GRID_ROWS;
    } else {
        return FALSE;
    }

    if (target < 0) {
        target = 0;
    }

    if (target > count - 1) {
        // Down from the last full row lands on the final entry.
        target = count - 1;
    }

    if (target == current) {
        return FALSE;
    }

    PokedexSort_SetCurrentValues(sortData, target);
    Sound_PlayEffect(SEQ_SE_DP_DENSI06_sseq);

    return TRUE;
}
