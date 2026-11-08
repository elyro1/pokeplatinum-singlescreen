#include "applications/pokemon_summary_screen/wide.h"

#include <nitro.h>
#include <string.h>

#ifdef SDK_PORT
#include <simulator/sim.h>

#include "port/sim_config_prj.h"
#endif

#include "applications/pokemon_summary_screen/main.h"

#include "bg_window.h"
#include "graphics.h"
#include "heap.h"
#include "narc.h"
#include "render_view.h"
#include "sprite.h"
#include "sprite_system.h"

#define NATIVE_WIDTH      256
#define NATIVE_COLUMNS    32
#define NATIVE_ROWS       24
#define WIDE_COLUMNS      64
#define WIDE_ROWS         32
#define BLOCK_TILES       1024
#define PANEL_MAP_TILES   (BLOCK_TILES * 4)
#define WIDE_TILEMAP_SIZE 0x1000

// The first tile column of the right panel's content, after the panel's border.
#define CONTENT_COLUMN  14
#define BORDER_COLUMN   13
#define LEFT_PANEL_DOTS (CONTENT_COLUMN * 8)

#define PROMPT_WINDOW_LEFT 26

// A row of the condition page that has no labels or bar on it.
#define CONDITION_PLAIN_ROW 18
#define CONDITION_FIRST_ROW 4

// Copies of these tiles without the panel's border, in free slots after the tile sheet.
#define CONDITION_PATCH_FIRST_TILE 480
#define TILE_BYTES                 32
#define TILE_ROW_BYTES             4
// The border is the first two dots of each row. Index 0xD is the page's plain band color.
#define BORDER_DOTS_TO_BAND 0xDD
#define TILEMAP_TILE_MASK   0x3FF

#define MOVE_INFO_NATIVE_START 136
#define MOVE_INFO_WIDE_START   72
#define BG_WRAP_DOTS           512
#define MOVE_INFO_SLIDE_STEP   64
#define MOVE_INFO_NATIVE_HIDE  (MOVE_INFO_SLIDE_STEP * 2)

#define RIBBON_PANEL_FIRST_ROW 24
#define RIBBON_PANEL_ROWS      8
#define RIBBON_PANEL_X_NATIVE  256

static BOOL sActive = FALSE;
static int sWidth = NATIVE_WIDTH;
static int sLeft = 0;
static int sContentShiftTiles = 0;
static int sColumns = NATIVE_COLUMNS;
static int sPromptShiftTiles = 0;
static u16 sPanels[PANEL_MAP_TILES];

void SummaryWide_Begin(void)
{
#ifdef SDK_PORT
    sActive = FALSE;

    if (SIM_Config_prj_GetConfig()->newPokedex == FALSE) {
        return;
    }

    sWidth = SIM_SetWideUI(TRUE);

    if (sWidth <= NATIVE_WIDTH) {
        SIM_SetWideUI(FALSE);
        sWidth = NATIVE_WIDTH;
        return;
    }

    sActive = TRUE;
    sLeft = (sWidth - NATIVE_WIDTH) / 2;
    sContentShiftTiles = (sLeft + 4) / 8;
    sColumns = (sWidth + 7) / 8;
    sPromptShiftTiles = sWidth / 8 - NATIVE_COLUMNS;
    RenderView_SetHorizontalMargin(sLeft);
#endif
}

void SummaryWide_End(void)
{
#ifdef SDK_PORT
    if (sActive) {
        SIM_SetWideUI3DOffset(0);
        SIM_SetWideUI(FALSE);
        RenderView_SetHorizontalMargin(0);
    }
#endif

    sActive = FALSE;
    sWidth = NATIVE_WIDTH;
    sLeft = 0;
    sContentShiftTiles = 0;
    sColumns = NATIVE_COLUMNS;
    sPromptShiftTiles = 0;
}

BOOL SummaryWide_IsActive(void)
{
    return sActive;
}

int SummaryWide_Left(void)
{
    return sLeft;
}

int SummaryWide_ContentShiftTiles(void)
{
    return sContentShiftTiles;
}

int SummaryWide_FullRowTiles(void)
{
    return sActive ? WIDE_COLUMNS : NATIVE_COLUMNS;
}

void SummaryWide_WidenTemplate(BgTemplate *template, GXBGScrBase screenBase)
{
    if (sActive == FALSE) {
        return;
    }

    template->screenSize = BG_SCREEN_SIZE_512x256;
    template->bufferSize = WIDE_TILEMAP_SIZE;
    template->screenBase = screenBase;
}

void SummaryWide_PlaceWindow(WindowTemplate *template)
{
    if (sActive == FALSE) {
        return;
    }

    if (template->tilemapTop == 0 && template->tilemapLeft == PROMPT_WINDOW_LEFT) {
        // The button prompt, on the title bar
        template->tilemapLeft += sPromptShiftTiles;
    } else if (template->tilemapLeft >= CONTENT_COLUMN || template->tilemapLeft + template->width > CONTENT_COLUMN + 2) {
        // Move details reach 2 tiles past the left panel and stay with it. Wider windows
        // on the left (ribbon text, messages) span the screen, and center with the content.
        template->tilemapLeft += sContentShiftTiles;
    }
}

// The labels and sheen bar on the condition page are part of the page's art, some of
// them starting in the border column, so its rows can't be extended from their ends.
static const struct {
    u8 row;
    u16 tile;
} sConditionBorderTiles[] = {
    { 8, 0x010 },
    { 9, 0x030 },
    { 19, 0x01C },
    { 20, 0x03C },
    { 21, 0x05C },
    { 22, 0x07C },
};

void SummaryWide_LoadConditionTiles(BgConfig *bgConfig, NARC *narc, u32 narcMemberIdx, enum HeapID heapID)
{
    if (sActive == FALSE) {
        return;
    }

    NNSG2dCharacterData *charData;
    void *buffer = Graphics_GetCharDataFromOpenNARC(narc, narcMemberIdx, FALSE, &charData, heapID);

    for (u32 i = 0; i < NELEMS(sConditionBorderTiles); i++) {
        u8 tile[TILE_BYTES];

        memcpy(tile, (const u8 *)charData->pRawData + sConditionBorderTiles[i].tile * TILE_BYTES, TILE_BYTES);

        for (int row = 0; row < TILE_BYTES; row += TILE_ROW_BYTES) {
            tile[row] = BORDER_DOTS_TO_BAND;
        }

        Bg_LoadTiles(bgConfig, BG_LAYER_MAIN_3, tile, TILE_BYTES, CONDITION_PATCH_FIRST_TILE + i);
    }

    Heap_Free(buffer);
}

// Returns the index in sConditionBorderTiles, or -1 for a row that doesn't start in the border.
static int FindConditionBorderTile(int row)
{
    for (u32 i = 0; i < NELEMS(sConditionBorderTiles); i++) {
        if (sConditionBorderTiles[i].row == row) {
            return i;
        }
    }

    return -1;
}

void SummaryWide_LoadPageTilemap(BgConfig *bgConfig, const u16 *nativeTilemap, BOOL isConditionPage)
{
    u16 wide[WIDE_COLUMNS * WIDE_ROWS];
    const u16 *conditionPlain = nativeTilemap + CONDITION_PLAIN_ROW * NATIVE_COLUMNS;

    memset(wide, 0, sizeof(wide));

    for (int row = 0; row < NATIVE_ROWS; row++) {
        const u16 *src = nativeTilemap + row * NATIVE_COLUMNS;
        u16 *dest = wide + row * WIDE_COLUMNS;
        int firstMoved = CONTENT_COLUMN;
        int borderTile = -1;
        u16 leftFill = src[CONTENT_COLUMN];
        // A box that runs off the native screen keeps going to the edge of the view.
        u16 rightFill = src[NATIVE_COLUMNS - 1] == src[NATIVE_COLUMNS - 2] ? src[NATIVE_COLUMNS - 1] : src[CONTENT_COLUMN];

        memcpy(dest, src, sizeof(u16) * CONTENT_COLUMN);

        if (isConditionPage && row >= CONDITION_FIRST_ROW) {
            leftFill = conditionPlain[CONTENT_COLUMN];
            rightFill = conditionPlain[CONTENT_COLUMN];

            borderTile = sContentShiftTiles > 0 ? FindConditionBorderTile(row) : -1;

            if (borderTile >= 0) {
                firstMoved = BORDER_COLUMN;
                dest[BORDER_COLUMN] = conditionPlain[BORDER_COLUMN];
            }
        }

        for (int column = CONTENT_COLUMN; column < firstMoved + sContentShiftTiles; column++) {
            dest[column] = leftFill;
        }

        memcpy(dest + firstMoved + sContentShiftTiles, src + firstMoved, sizeof(u16) * (NATIVE_COLUMNS - firstMoved));

        if (borderTile >= 0) {
            u16 *moved = &dest[BORDER_COLUMN + sContentShiftTiles];

            *moved = (*moved & ~TILEMAP_TILE_MASK) | (CONDITION_PATCH_FIRST_TILE + borderTile);
        }

        for (int column = NATIVE_COLUMNS + sContentShiftTiles; column < WIDE_COLUMNS; column++) {
            dest[column] = rightFill;
        }
    }

    Bg_LoadToTilemapRect(bgConfig, BG_LAYER_MAIN_3, wide, 0, 0, WIDE_COLUMNS, WIDE_ROWS);
    Bg_ScheduleTilemapTransfer(bgConfig, BG_LAYER_MAIN_3);
}

void SummaryWide_SavePanels(BgConfig *bgConfig)
{
    if (sActive == FALSE) {
        return;
    }

    memcpy(sPanels, Bg_GetTilemapBuffer(bgConfig, BG_LAYER_MAIN_2), sizeof(sPanels));
    SummaryWide_ShowPanel(bgConfig, SUMMARY_WIDE_PANEL_NONE);
}

static void DrawRibbonPanel(u16 *map)
{
    for (int row = 0; row < RIBBON_PANEL_ROWS; row++) {
        const int mapRow = RIBBON_PANEL_FIRST_ROW + row;
        const u16 *src = sPanels + BLOCK_TILES + mapRow * NATIVE_COLUMNS;

        for (int column = 0; column < sColumns; column++) {
            u16 tile;

            if (column == 0) {
                tile = src[0];
            } else if (column == sColumns - 1) {
                tile = src[NATIVE_COLUMNS - 1];
            } else if (column <= sContentShiftTiles) {
                tile = src[1];
            } else if (column < NATIVE_COLUMNS - 1 + sContentShiftTiles) {
                tile = src[column - sContentShiftTiles];
            } else {
                // Column 30 is mirrored from the right edge's art
                tile = src[NATIVE_COLUMNS - 3];
            }

            map[(column >> 5) * BLOCK_TILES + mapRow * NATIVE_COLUMNS + (column & 0x1F)] = tile;
        }
    }
}

void SummaryWide_ShowPanel(BgConfig *bgConfig, enum SummaryWidePanel panel)
{
    if (sActive == FALSE) {
        return;
    }

    u16 *map = Bg_GetTilemapBuffer(bgConfig, BG_LAYER_MAIN_2);
    memset(map, 0, sizeof(sPanels));

    switch (panel) {
    case SUMMARY_WIDE_PANEL_BATTLE_MOVE:
        memcpy(map, sPanels, sizeof(u16) * NATIVE_ROWS * NATIVE_COLUMNS);
        break;
    case SUMMARY_WIDE_PANEL_CONTEST_MOVE:
        memcpy(map + BLOCK_TILES * 2, sPanels + BLOCK_TILES * 2, sizeof(u16) * NATIVE_ROWS * NATIVE_COLUMNS);
        break;
    case SUMMARY_WIDE_PANEL_RIBBON:
        DrawRibbonPanel(map);
        break;
    default:
        break;
    }

    Bg_ScheduleTilemapTransfer(bgConfig, BG_LAYER_MAIN_2);
}

int SummaryWide_PanelX(int nativeOffset)
{
    return nativeOffset + sLeft;
}

int SummaryWide_RibbonPanelX(void)
{
    return sActive ? sLeft : RIBBON_PANEL_X_NATIVE;
}

int SummaryWide_MoveInfoStartOffset(void)
{
    if (sActive == FALSE) {
        return MOVE_INFO_NATIVE_START;
    }

    // Past this, the panel's left edge would wrap to the right edge of the view.
    const int furthest = BG_WRAP_DOTS - sWidth;

    return furthest < MOVE_INFO_WIDE_START ? furthest : MOVE_INFO_WIDE_START;
}

int SummaryWide_MoveInfoHideOffset(void)
{
    return sActive ? MOVE_INFO_SLIDE_STEP : MOVE_INFO_NATIVE_HIDE;
}

static int SpriteShift(int index, s16 x)
{
    if (index == SUMMARY_SPRITE_BUTTON_TAP_CIRCLE) {
        // On the sub screen
        return 0;
    }

    if (index == SUMMARY_SPRITE_A_BUTTON) {
        // Placed from its window's column, which is in view coordinates
        return -sLeft;
    }

    // The left arrow reaches into the left panel.
    const BOOL isContent = index <= SUMMARY_SPRITE_TAB_EXIT
        || index == SUMMARY_SPRITE_PAGE_ARROW_LEFT
        || index == SUMMARY_SPRITE_PAGE_ARROW_RIGHT;

    if (isContent || x >= LEFT_PANEL_DOTS) {
        return sContentShiftTiles * 8 - sLeft;
    }

    return -sLeft;
}

void SummaryWide_DrawSprites(PokemonSummaryScreen *summaryScreen)
{
    if (sActive == FALSE) {
        SpriteSystem_DrawSprites(summaryScreen->spriteMan);
        return;
    }

    s16 nativeX[SUMMARY_SPRITE_MAX];

    for (int i = 0; i < SUMMARY_SPRITE_MAX; i++) {
        s16 y;

        Sprite_GetPositionXY(summaryScreen->sprites[i], &nativeX[i], &y);
        Sprite_SetPositionXY(summaryScreen->sprites[i], nativeX[i] + SpriteShift(i, nativeX[i]), y);
    }

    SpriteSystem_DrawSprites(summaryScreen->spriteMan);

    for (int i = 0; i < SUMMARY_SPRITE_MAX; i++) {
        s16 x, y;

        Sprite_GetPositionXY(summaryScreen->sprites[i], &x, &y);
        Sprite_SetPositionXY(summaryScreen->sprites[i], nativeX[i], y);
    }
}

void SummaryWide_Set3DForContent(void)
{
#ifdef SDK_PORT
    if (sActive) {
        SIM_SetWideUI3DOffset(sContentShiftTiles * 8 - sLeft);
    }
#endif
}

void SummaryWide_Set3DForMon(void)
{
#ifdef SDK_PORT
    if (sActive) {
        SIM_SetWideUI3DOffset(-sLeft);
    }
#endif
}
