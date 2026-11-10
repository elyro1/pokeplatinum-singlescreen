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
#define TITLE_WINDOW_LEFT  1
// The right panel's content, from CONTENT_COLUMN to the native right edge.
#define CONTENT_DOTS ((NATIVE_COLUMNS - CONTENT_COLUMN) * 8)

// The left panel's art is uniform in these columns, so it widens by repeating them: the
// first on its left side and the second on its right, which centers what is between.
#define LEFT_PANEL_STRETCH_FIRST  1
#define LEFT_PANEL_STRETCH_SECOND 11
// The move details have their labels on the left and the values in boxes from column 11
// on. Those panels widen between the two; the contest panel's art is centered.
#define BATTLE_PANEL_STRETCH_SECOND  10
#define CONTEST_PANEL_STRETCH_SECOND 14
#define BATTLE_PANEL_VALUES_COLUMN   11
// Windows that reach this far from the left side span the screen, like messages.
#define MOVE_INFO_PANEL_COLUMNS 17

#define FOCUSED_TAB_DOTS    24
#define UNFOCUSED_TAB_DOTS  16
// The arrows' positions from the row's edges. Sprite positions are 8 dots right of a tab's
// left edge.
#define TAB_ARROW_LEFT_X  -4
#define TAB_ARROW_RIGHT_X 4
// Room kept between the tabs' arrows and the panel's edges.
#define TAB_ROW_MARGIN 8

// The tab sprites' art: unfocused tabs are 16x16, focused ones 24x16 from two cells.
#define TAB_CHAR_TILES           80
#define TAB_ARTS                 8
#define UNFOCUSED_TAB_FIRST_TILE 0
#define UNFOCUSED_TAB_TILES      4
#define FOCUSED_TAB_FIRST_TILE   32
#define FOCUSED_TAB_TILES        6
#define TAB_CELL_PALETTE_2_FIRST 4
// The rows of a tab's face, from the shade row under its highlight, and how wide a frame
// is when it takes in face columns.
#define TAB_SHADE_ROW       2
#define TAB_FACE_LAST_ROW   13
#define TAB_WIDE_FRAME_DOTS 4
// Where the wide tabs go on BG layer 1: after the windows' tiles, and in palettes
// nothing else uses.
#define TAB_FIRST_ROW        2
#define TAB_ROWS             2
#define TAB_TILE_FIRST       960
#define TAB_TILES_MAX        64
#define TAB_BG_PALETTE_FIRST 11
#define TAB_BG_PALETTES      3

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
// How many tiles the left panel grows by, and how far its content moves to stay centered.
static int sLeftGrowTiles = 0;
static int sLeftShiftTiles = 0;
// How far the right panel's content moves, to the center of the rest of the view.
static int sContentShiftTiles = 0;
// How far something centered on the native screen moves to stay centered on the view.
static int sCenterShiftTiles = 0;
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
    sCenterShiftTiles = (sLeft + 4) / 8;
    // The left panel keeps its share of the width.
    sLeftGrowTiles = ((sWidth - NATIVE_WIDTH) * LEFT_PANEL_DOTS / NATIVE_WIDTH + 4) / 8;
    sLeftShiftTiles = sLeftGrowTiles / 2;

    const int rightPanelLeft = (CONTENT_COLUMN + sLeftGrowTiles) * 8;
    const int contentLeft = rightPanelLeft + (sWidth - rightPanelLeft - CONTENT_DOTS) / 2;

    sContentShiftTiles = (contentLeft - LEFT_PANEL_DOTS + 4) / 8;

    if (sContentShiftTiles < sLeftGrowTiles) {
        sContentShiftTiles = sLeftGrowTiles;
    }

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
    sLeftGrowTiles = 0;
    sLeftShiftTiles = 0;
    sContentShiftTiles = 0;
    sCenterShiftTiles = 0;
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

int SummaryWide_LeftShiftTiles(void)
{
    return sLeftShiftTiles;
}

int SummaryWide_RightPanelColumn(void)
{
    return CONTENT_COLUMN + sLeftGrowTiles;
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

    if (template->tilemapTop == 0) {
        // The title bar: the page's title stays at the far left, the button prompt goes
        // to the far right.
        if (template->tilemapLeft == PROMPT_WINDOW_LEFT) {
            template->tilemapLeft += sPromptShiftTiles;
        } else if (template->tilemapLeft != TITLE_WINDOW_LEFT) {
            template->tilemapLeft += sContentShiftTiles;
        }
    } else if (template->tilemapLeft >= CONTENT_COLUMN) {
        template->tilemapLeft += sContentShiftTiles;
    } else if (template->tilemapLeft + template->width > MOVE_INFO_PANEL_COLUMNS) {
        // Wider windows on the left (ribbon text, messages) span the screen.
        template->tilemapLeft += sCenterShiftTiles;
    } else if (template->tilemapLeft >= BATTLE_PANEL_VALUES_COLUMN) {
        // Power and accuracy, in the move details' boxes
        template->tilemapLeft += sLeftGrowTiles;
    } else {
        // The left panel, and the move details that slide over it
        template->tilemapLeft += sLeftShiftTiles;
    }
}

// Writes a row of native tiles with two of its columns repeated: firstColumn firstCount
// more times, and secondColumn secondCount more times. Returns the columns written.
static int StretchRow(u16 *dest, const u16 *src, int srcColumns, int firstColumn, int firstCount, int secondColumn, int secondCount)
{
    int written = 0;

    for (int column = 0; column < srcColumns; column++) {
        int copies = 1;

        if (column == firstColumn) {
            copies += firstCount;
        }

        if (column == secondColumn) {
            copies += secondCount;
        }

        while (copies-- > 0) {
            dest[written++] = src[column];
        }
    }

    return written;
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
        const int borderColumn = BORDER_COLUMN + sLeftGrowTiles;
        int firstMoved = CONTENT_COLUMN;
        int borderTile = -1;
        u16 leftFill = src[CONTENT_COLUMN];
        // A box that runs off the native screen keeps going to the edge of the view.
        u16 rightFill = src[NATIVE_COLUMNS - 1] == src[NATIVE_COLUMNS - 2] ? src[NATIVE_COLUMNS - 1] : src[CONTENT_COLUMN];

        StretchRow(dest, src, BORDER_COLUMN, LEFT_PANEL_STRETCH_FIRST, sLeftShiftTiles, LEFT_PANEL_STRETCH_SECOND, sLeftGrowTiles - sLeftShiftTiles);
        dest[borderColumn] = src[BORDER_COLUMN];

        if (isConditionPage && row >= CONDITION_FIRST_ROW) {
            leftFill = conditionPlain[CONTENT_COLUMN];
            rightFill = conditionPlain[CONTENT_COLUMN];

            borderTile = sContentShiftTiles > sLeftGrowTiles ? FindConditionBorderTile(row) : -1;

            if (borderTile >= 0) {
                firstMoved = BORDER_COLUMN;
                dest[borderColumn] = conditionPlain[BORDER_COLUMN];
            }
        }

        for (int column = borderColumn + 1; column < firstMoved + sContentShiftTiles; column++) {
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
            } else if (column <= sCenterShiftTiles) {
                tile = src[1];
            } else if (column < NATIVE_COLUMNS - 1 + sCenterShiftTiles) {
                tile = src[column - sCenterShiftTiles];
            } else {
                // Column 30 is mirrored from the right edge's art
                tile = src[NATIVE_COLUMNS - 3];
            }

            map[(column >> 5) * BLOCK_TILES + mapRow * NATIVE_COLUMNS + (column & 0x1F)] = tile;
        }
    }
}

// The move details cover the left panel, so they widen with it.
static void DrawMovePanel(u16 *map, int block, int secondColumn)
{
    for (int row = 0; row < NATIVE_ROWS; row++) {
        const u16 *src = sPanels + block * BLOCK_TILES + row * NATIVE_COLUMNS;
        u16 wide[WIDE_COLUMNS];
        const int columns = StretchRow(wide, src, MOVE_INFO_PANEL_COLUMNS, 0, sLeftShiftTiles, secondColumn, sLeftGrowTiles - sLeftShiftTiles);

        for (int column = 0; column < columns; column++) {
            map[(block + (column >> 5)) * BLOCK_TILES + row * NATIVE_COLUMNS + (column & 0x1F)] = wide[column];
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
        DrawMovePanel(map, 0, BATTLE_PANEL_STRETCH_SECOND);
        break;
    case SUMMARY_WIDE_PANEL_CONTEST_MOVE:
        DrawMovePanel(map, 2, CONTEST_PANEL_STRETCH_SECOND);
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
    // View dots are native dots + sLeft.
    if (index == SUMMARY_SPRITE_BUTTON_TAP_CIRCLE) {
        // On the sub screen
        return 0;
    }

    if (index == SUMMARY_SPRITE_A_BUTTON) {
        // Placed from its window's column, which is in view coordinates
        return -sLeft;
    }

    if (index == SUMMARY_SPRITE_MOVE_CATEGORY_ICON) {
        // In the move details' boxes
        return sLeftGrowTiles * 8 - sLeft;
    }

    if (x >= LEFT_PANEL_DOTS) {
        return sContentShiftTiles * 8 - sLeft;
    }

    return sLeftShiftTiles * 8 - sLeft;
}

// The tabs are drawn wider than their sprites, on BG layer 1 with tiles made from the
// sprites' art: the border and bevel stay at the ends, and the face color fills out the
// middle around the icon. Each tab gets an equal share of the row, in whole tiles so that
// each can have its own palette, with the arrows just outside as on the native screen.
typedef struct TabRow {
    int count;
    int slotTiles;
    int firstColumn;
    u8 shown;
    u8 visible;
    // The art each sprite shows, plus TAB_ARTS when it is focused.
    u8 anims[SUMMARY_SPRITE_TAB_EXIT + 1];
    u8 palettes[SUMMARY_SPRITE_TAB_EXIT + 1];
    u8 bgPalettes[SUMMARY_SPRITE_TAB_EXIT + 1];
} TabRow;

static BgConfig *sBgConfig = NULL;

static u8 sTabChars[TAB_CHAR_TILES * TILE_BYTES];
static BOOL sTabCharsLoaded = FALSE;
static TabRow sTabRow;
static BOOL sTabRowBuilt = FALSE;
static BOOL sTabTilesPending = FALSE;
static u8 sTabTiles[TAB_TILES_MAX * TILE_BYTES];
static u16 sTabTilemap[2][WIDE_COLUMNS];

void SummaryWide_LoadTabTiles(NARC *narc, u32 narcMemberIdx, enum HeapID heapID)
{
    sTabCharsLoaded = FALSE;
    sTabRowBuilt = FALSE;
    sTabTilesPending = FALSE;

    if (sActive == FALSE) {
        return;
    }

    NNSG2dCharacterData *charData;
    void *buffer = Graphics_GetCharDataFromOpenNARC(narc, narcMemberIdx, FALSE, &charData, heapID);

    if (charData->szByte >= sizeof(sTabChars)) {
        memcpy(sTabChars, charData->pRawData, sizeof(sTabChars));
        sTabCharsLoaded = TRUE;
    }

    Heap_Free(buffer);
}

// The sprites' cells: a 16x16 tab, or a focused one of 16x16 and 8x16, in 1D tile order.
static int TabSpritePixel(int art, BOOL focused, int x, int y)
{
    int tile;

    if (focused == FALSE) {
        tile = UNFOCUSED_TAB_FIRST_TILE + art * UNFOCUSED_TAB_TILES + (y / 8) * 2 + x / 8;
    } else if (x < UNFOCUSED_TAB_DOTS) {
        tile = FOCUSED_TAB_FIRST_TILE + art * FOCUSED_TAB_TILES + (y / 8) * 2 + x / 8;
    } else {
        tile = FOCUSED_TAB_FIRST_TILE + art * FOCUSED_TAB_TILES + 4 + y / 8;
    }

    const u8 pair = sTabChars[tile * TILE_BYTES + (y % 8) * TILE_ROW_BYTES + (x % 8) / 2];

    return (x & 1) ? pair >> 4 : pair & 0xF;
}

// How a tab widens. Its icon often reaches the edges of its face, so dots are added only
// between the icon and the frame at each end: the columns that are one color all the way
// down the face. Where a frame takes in face columns (a focused tab's), the added dots are
// that face color; otherwise they are the face's most common color.
typedef struct TabStretch {
    int width;
    // The first column after the left frame, and the first of the right frame
    int leftAt;
    int rightAt;
    // A frame column to repeat, or -1 to use faceColor.
    int frameColumn;
    int faceColor;
} TabStretch;

static BOOL IsTabFrameColumn(int art, BOOL focused, int x)
{
    const int color = TabSpritePixel(art, focused, x, TAB_SHADE_ROW);

    for (int y = TAB_SHADE_ROW + 1; y <= TAB_FACE_LAST_ROW; y++) {
        if (TabSpritePixel(art, focused, x, y) != color) {
            return FALSE;
        }
    }

    return TRUE;
}

// Scans in from the border for the frame's run of plain columns, past any shading at the
// edge. Returns the innermost, or -1 if there is none.
static int FindTabFrameEnd(int art, BOOL focused, int from, int to, int step)
{
    int last = -1;

    for (int x = from; x != to; x += step) {
        if (IsTabFrameColumn(art, focused, x)) {
            last = x;
        } else if (last >= 0) {
            break;
        }
    }

    return last;
}

static void MeasureTabStretch(int art, BOOL focused, TabStretch *stretch)
{
    const int width = focused ? FOCUSED_TAB_DOTS : UNFOCUSED_TAB_DOTS;
    const int leftEnd = FindTabFrameEnd(art, focused, 1, width / 2, 1);
    const int rightEnd = FindTabFrameEnd(art, focused, width - 2, width / 2 - 1, -1);
    int counts[16] = { 0 };

    stretch->width = width;
    stretch->leftAt = leftEnd >= 0 ? leftEnd + 1 : 1;
    stretch->rightAt = rightEnd >= 0 ? rightEnd : width - 1;
    stretch->frameColumn = -1;

    if (stretch->leftAt >= TAB_WIDE_FRAME_DOTS) {
        stretch->frameColumn = stretch->leftAt - 1;
    } else if (width - stretch->rightAt >= TAB_WIDE_FRAME_DOTS) {
        stretch->frameColumn = stretch->rightAt;
    }

    for (int y = TAB_SHADE_ROW + 1; y <= TAB_FACE_LAST_ROW; y++) {
        for (int x = stretch->leftAt; x < stretch->rightAt; x++) {
            counts[TabSpritePixel(art, focused, x, y)]++;
        }
    }

    stretch->faceColor = 0;

    for (int i = 1; i < 16; i++) {
        if (counts[i] > counts[stretch->faceColor]) {
            stretch->faceColor = i;
        }
    }
}

// A dot of the tab at slot width.
static int WideTabPixel(int art, BOOL focused, const TabStretch *stretch, int slotDots, int x, int y)
{
    const int extra = slotDots - stretch->width;
    const int leftAdded = extra / 2;
    const int middle = stretch->rightAt - stretch->leftAt;
    BOOL addedOnLeft;

    if (x < stretch->leftAt) {
        return TabSpritePixel(art, focused, x, y);
    } else if (x < stretch->leftAt + leftAdded) {
        addedOnLeft = TRUE;
    } else if (x < stretch->leftAt + leftAdded + middle) {
        return TabSpritePixel(art, focused, x - leftAdded, y);
    } else if (x < stretch->leftAt + extra + middle) {
        addedOnLeft = FALSE;
    } else {
        return TabSpritePixel(art, focused, x - extra, y);
    }

    if (stretch->frameColumn >= 0) {
        return TabSpritePixel(art, focused, stretch->frameColumn, y);
    }

    if (y > TAB_SHADE_ROW && y <= TAB_FACE_LAST_ROW) {
        return stretch->faceColor;
    }

    // The border, highlight and shade rows above the face, continued
    return TabSpritePixel(art, focused, addedOnLeft ? stretch->leftAt : stretch->rightAt - 1, y);
}

static int TabSpritePalette(Sprite *sprite, int art)
{
    const int cellPalette = art < TAB_CELL_PALETTE_2_FIRST ? 1 : 2;

    if (sprite->overwriteFlags & NNS_G2D_RND_OVERWRITE_PLTTNO) {
        return sprite->explicitPalette;
    }

    if (sprite->overwriteFlags & NNS_G2D_RND_OVERWRITE_PLTTNO_OFFS) {
        return cellPalette + sprite->explicitPaletteOffset;
    }

    return cellPalette;
}

// The art in the sprite's current cell. The animations don't show the cells in page order,
// so this goes by the cell's tiles.
static int TabSpriteArt(Sprite *sprite)
{
    const NNSG2dCellData *cell = NNS_G2dGetCellAnimationCurrentCell(&((CellAnimationData *)sprite->animData)->anim);
    const int firstTile = cell->pOamAttrArray[0].attr2 & GX_OAM_ATTR2_NAME_MASK;

    if (cell->numOAMAttrs > 1) {
        return (firstTile - FOCUSED_TAB_FIRST_TILE) / FOCUSED_TAB_TILES + TAB_ARTS;
    }

    return (firstTile - UNFOCUSED_TAB_FIRST_TILE) / UNFOCUSED_TAB_TILES;
}

// Returns FALSE when the tabs don't have room for more than their native widths.
static BOOL LayOutTabRow(PokemonSummaryScreen *summaryScreen, TabRow *row)
{
    memset(row, 0, sizeof(*row));
    row->count = PokemonSummaryScreen_CountVisiblePages(summaryScreen);

    if (row->count == 0 || sTabCharsLoaded == FALSE) {
        return FALSE;
    }

    const int center = (CONTENT_COLUMN + sContentShiftTiles) * 8 + CONTENT_DOTS / 2;
    const int panelLeft = (CONTENT_COLUMN + sLeftGrowTiles) * 8;
    const int leftRoom = center - panelLeft - TAB_ROW_MARGIN;
    const int rightRoom = sWidth - center - TAB_ROW_MARGIN;
    const int halfRow = leftRoom < rightRoom ? leftRoom : rightRoom;

    row->slotTiles = halfRow * 2 / row->count / 8;

    while (row->slotTiles * row->count * TAB_ROWS > TAB_TILES_MAX) {
        row->slotTiles--;
    }

    if (row->slotTiles * 8 < FOCUSED_TAB_DOTS) {
        return FALSE;
    }

    row->firstColumn = (center - row->slotTiles * 8 * row->count / 2 + 4) / 8;
    for (int page = 0; page <= SUMMARY_SPRITE_TAB_EXIT; page++) {
        Sprite *sprite = summaryScreen->sprites[SUMMARY_SPRITE_TAB_INFO + page];

        if (PokemonSummaryScreen_PageIsVisble(summaryScreen, page)) {
            row->visible |= 1 << page;

            if (Sprite_GetDrawFlag(sprite)) {
                row->shown |= 1 << page;
            }
        }

        row->anims[page] = TabSpriteArt(sprite);
        row->palettes[page] = TabSpritePalette(sprite, row->anims[page] % TAB_ARTS);
    }

    return TRUE;
}

// The tabs' OBJ palettes are copied to free BG palettes, in order of first use.
static BOOL AssignTabBgPalettes(TabRow *row)
{
    u8 objPalettes[TAB_BG_PALETTES];
    int count = 0;

    for (int page = 0; page <= SUMMARY_SPRITE_TAB_EXIT; page++) {
        int slot = 0;

        while (slot < count && objPalettes[slot] != row->palettes[page]) {
            slot++;
        }

        if (slot == count) {
            if (count == TAB_BG_PALETTES) {
                return FALSE;
            }

            objPalettes[count++] = row->palettes[page];
        }

        row->bgPalettes[page] = TAB_BG_PALETTE_FIRST + slot;
    }

    return TRUE;
}

static void BuildTabRow(const TabRow *row)
{
    const int slotDots = row->slotTiles * 8;
    int slot = 0;

    memset(sTabTiles, 0, sizeof(sTabTiles));
    memset(sTabTilemap, 0, sizeof(sTabTilemap));

    for (int page = 0; page <= SUMMARY_SPRITE_TAB_EXIT; page++) {
        if ((row->shown & (1 << page)) == 0) {
            if (row->visible & (1 << page)) {
                slot++;
            }

            continue;
        }

        const int art = row->anims[page] % TAB_ARTS;
        const BOOL focused = row->anims[page] >= TAB_ARTS;
        TabStretch stretch;
        const int firstTile = slot * row->slotTiles * TAB_ROWS;

        MeasureTabStretch(art, focused, &stretch);

        for (int y = 0; y < TAB_ROWS * 8; y++) {
            for (int x = 0; x < slotDots; x++) {
                const int tile = firstTile + (y / 8) * row->slotTiles + x / 8;
                u8 *pair = &sTabTiles[tile * TILE_BYTES + (y % 8) * TILE_ROW_BYTES + (x % 8) / 2];
                const int color = WideTabPixel(art, focused, &stretch, slotDots, x, y);

                *pair |= (x & 1) ? color << 4 : color;
            }
        }

        for (int tileRow = 0; tileRow < TAB_ROWS; tileRow++) {
            for (int column = 0; column < row->slotTiles; column++) {
                const int tile = TAB_TILE_FIRST + firstTile + tileRow * row->slotTiles + column;

                sTabTilemap[tileRow][row->firstColumn + slot * row->slotTiles + column] = tile | (row->bgPalettes[page] << 12);
            }
        }

        slot++;
    }
}

static void LoadTabRowGfx(const TabRow *row)
{
    for (int i = 0; i < TAB_BG_PALETTES; i++) {
        for (int page = 0; page <= SUMMARY_SPRITE_TAB_EXIT; page++) {
            if (row->bgPalettes[page] == TAB_BG_PALETTE_FIRST + i) {
                const u16 *colors = (const u16 *)HW_OBJ_PLTT + row->palettes[page] * 16;

                GX_LoadBGPltt(colors, (TAB_BG_PALETTE_FIRST + i) * 32, 32);
                break;
            }
        }
    }

    Bg_LoadTiles(sBgConfig, BG_LAYER_MAIN_1, sTabTiles, sizeof(sTabTiles), TAB_TILE_FIRST);
}

// Writes the row's tilemap to BG layer 1 when the layer's buffer doesn't have it, as after
// the page's windows are cleared.
static void PutTabRowTilemap(BgConfig *bgConfig, BOOL show)
{
    u16 *map = Bg_GetTilemapBuffer(bgConfig, BG_LAYER_MAIN_1);
    const int firstColumn = SummaryWide_RightPanelColumn();
    BOOL changed = FALSE;

    for (int tileRow = 0; tileRow < TAB_ROWS; tileRow++) {
        for (int column = firstColumn; column < WIDE_COLUMNS; column++) {
            const u16 entry = show ? sTabTilemap[tileRow][column] : 0;
            u16 *dest = &map[(column >> 5) * BLOCK_TILES + (TAB_FIRST_ROW + tileRow) * NATIVE_COLUMNS + (column & 0x1F)];

            if (*dest != entry) {
                *dest = entry;
                changed = TRUE;
            }
        }
    }

    if (changed) {
        Bg_ScheduleTilemapTransfer(bgConfig, BG_LAYER_MAIN_1);
    }
}

static BOOL TabRowsEqual(const TabRow *a, const TabRow *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

// Places the tab sprites (or draws the wide tabs instead), and the arrows. Writes native dots.
static BOOL PlaceTabs(PokemonSummaryScreen *summaryScreen, const s16 *nativeX, s16 *drawX)
{
    TabRow row;

    // Tiles go to VRAM at once, but the tilemap and the page's other changes only at the
    // next VBlank, so new tiles are loaded a frame after their tilemap is written.
    if (sTabTilesPending) {
        LoadTabRowGfx(&sTabRow);
        sTabTilesPending = FALSE;
    }

    if (LayOutTabRow(summaryScreen, &row) == FALSE || AssignTabBgPalettes(&row) == FALSE) {
        // Keep the native row when there isn't room for wider tabs.
        PutTabRowTilemap(summaryScreen->bgConfig, FALSE);
        sTabRowBuilt = FALSE;

        for (int i = 0; i <= SUMMARY_SPRITE_TAB_EXIT; i++) {
            drawX[i] = nativeX[i] + sContentShiftTiles * 8 - sLeft;
        }

        drawX[SUMMARY_SPRITE_PAGE_ARROW_LEFT] = nativeX[SUMMARY_SPRITE_PAGE_ARROW_LEFT] + sContentShiftTiles * 8 - sLeft;
        drawX[SUMMARY_SPRITE_PAGE_ARROW_RIGHT] = nativeX[SUMMARY_SPRITE_PAGE_ARROW_RIGHT] + sContentShiftTiles * 8 - sLeft;
        return FALSE;
    }

    if (sTabRowBuilt == FALSE || TabRowsEqual(&row, &sTabRow) == FALSE) {
        sTabRow = row;
        sTabRowBuilt = TRUE;
        BuildTabRow(&sTabRow);
        sTabTilesPending = TRUE;
    }

    PutTabRowTilemap(summaryScreen->bgConfig, TRUE);

    const int rowLeft = row.firstColumn * 8;
    const int rowRight = rowLeft + row.slotTiles * 8 * row.count;

    drawX[SUMMARY_SPRITE_PAGE_ARROW_LEFT] = rowLeft + TAB_ARROW_LEFT_X - sLeft;
    drawX[SUMMARY_SPRITE_PAGE_ARROW_RIGHT] = rowRight + TAB_ARROW_RIGHT_X - sLeft;
    return TRUE;
}

void SummaryWide_DrawSprites(PokemonSummaryScreen *summaryScreen)
{
    if (sActive == FALSE) {
        SpriteSystem_DrawSprites(summaryScreen->spriteMan);
        return;
    }

    s16 nativeX[SUMMARY_SPRITE_MAX];
    s16 drawX[SUMMARY_SPRITE_MAX];
    BOOL tabDrawn[SUMMARY_SPRITE_TAB_EXIT + 1];

    sBgConfig = summaryScreen->bgConfig;

    for (int i = 0; i < SUMMARY_SPRITE_MAX; i++) {
        s16 y;

        Sprite_GetPositionXY(summaryScreen->sprites[i], &nativeX[i], &y);
        drawX[i] = nativeX[i] + SpriteShift(i, nativeX[i]);
    }

    const BOOL wideTabs = PlaceTabs(summaryScreen, nativeX, drawX);

    for (int i = 0; i < SUMMARY_SPRITE_MAX; i++) {
        s16 x, y;

        Sprite_GetPositionXY(summaryScreen->sprites[i], &x, &y);
        Sprite_SetPositionXY(summaryScreen->sprites[i], drawX[i], y);
    }

    // The wide tabs replace the tab sprites.
    for (int i = 0; i <= SUMMARY_SPRITE_TAB_EXIT; i++) {
        tabDrawn[i] = Sprite_GetDrawFlag(summaryScreen->sprites[i]);

        if (wideTabs) {
            Sprite_SetDrawFlag(summaryScreen->sprites[i], FALSE);
        }
    }

    SpriteSystem_DrawSprites(summaryScreen->spriteMan);

    for (int i = 0; i <= SUMMARY_SPRITE_TAB_EXIT; i++) {
        Sprite_SetDrawFlag(summaryScreen->sprites[i], tabDrawn[i]);
    }

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
        SIM_SetWideUI3DOffset(sLeftShiftTiles * 8 - sLeft);
    }
#endif
}
