#include "applications/bag/grid.h"

#include <nitro.h>
#include <nnsys.h>
#include <string.h>

#ifdef SDK_PORT
#include <simulator/sim.h>

#include "port/sim_config_prj.h"
#endif

#include "constants/graphics.h"
#include "constants/heap.h"
#include "constants/items.h"
#include "constants/narc.h"

#include "applications/bag/main.h"

#include "res/graphics/bag/bag_graphics.naix"

#include "bag.h"
#include "bag_context.h"
#include "bg_window.h"
#include "font.h"
#include "graphics.h"
#include "heap.h"
#include "item.h"
#include "move_table.h"
#include "narc.h"
#include "render_view.h"
#include "sprite.h"
#include "sprite_system.h"
#include "system.h"
#include "text.h"
#include "type_icon.h"

#define NATIVE_WIDTH      256
#define NATIVE_COLUMNS    32
#define MAX_VIEW_COLUMNS  56
#define WIDE_TILEMAP_SIZE 0x1000

// The grid's panel: everything right of the bag, above the description.
#define PANEL_COLUMN 13
#define PANEL_X      (PANEL_COLUMN * TILE_WIDTH_PIXELS)
#define PANEL_ROWS   18
#define PANEL_HEIGHT (PANEL_ROWS * TILE_HEIGHT_PIXELS)
// The description's panel, below it.
#define DESCRIPTION_ROW  18
#define DESCRIPTION_ROWS 6

// The cells start at the first tile column after the panel's edge.
#define GRID_COLUMN (PANEL_COLUMN + 1)
// Dots kept clear right of the cells, for the scroll bar.
#define SCROLL_BAR_SPACE 8
#define MAX_GRID_COLUMNS 10

// The grid view: square cells with the selected item's entry below them.
#define GRID_CELL_WIDTH  32
#define GRID_CELL_HEIGHT 31
#define GRID_ROWS        4
#define GRID_Y           1
#define GRID_ICON_Y      3

// The list view: each item's name beside its icon, in two columns where there is room.
#define LIST_CELL_HEIGHT    27
#define LIST_ROWS           5
#define LIST_Y              2
#define LIST_ICON_Y         1
#define LIST_NAME_X         32
#define LIST_NAME_Y         5
#define LIST_NAME_MARGIN    3
#define LIST_TWO_COLUMN_MIN 256

// The selected item's entry, an entry of the list: its window's size, where its bar
// is and where the window goes in it. Text starts two dots down in its line.
#define ENTRY_WIDTH  136
#define ENTRY_Y      125
#define ENTRY_HEIGHT 13
#define ENTRY_TEXT_X 5
#define ENTRY_TEXT_Y (ENTRY_Y - 1)

#define ICON_CELL_X 4

// An icon file is 32 dots square with the art in its top left 24.
#define ICON_FILE_SIZE 32
#define ICON_SIZE      24

// The move info starts a tile after the native screen's right edge, in tiles the
// list's window has no use for here: the grid's entry takes only its first two rows.
#define MOVE_INFO_COLUMN      (NATIVE_COLUMNS + 1)
#define MOVE_INFO_X           (MOVE_INFO_COLUMN * TILE_WIDTH_PIXELS)
#define MOVE_INFO_MAX_COLUMNS 16
#define MOVE_INFO_MIN_WIDTH   100
#define MOVE_INFO_PP_WIDTH    120
#define MOVE_INFO_BASE_TILE   40
#define MOVE_INFO_ICON_Y      152
#define MOVE_INFO_TYPE_X      (MOVE_INFO_X + 16)
#define MOVE_INFO_CATEGORY_X  (MOVE_INFO_X + 52)
// The resources of the bag's type and category icon sprites, and where the type icons'
// palettes start among the sprite palettes (sprites.c).
#define TYPE_ICON_RESOURCE     49409
#define CATEGORY_ICON_RESOURCE 49410
#define TYPE_ICON_PLTT_START   6

// BG1: the bag's tiles come first, the panel's follow.
#define PANEL_PLTT       5
#define PANEL_TILE_START 256
#define PANEL_TILE_MAX   (1024 - PANEL_TILE_START)

// BG2: the icons, and the text that goes with them, cover the panel over the cells.
#define TEXT_PLTT       3
#define ICON_TILE_START 1
#define ICON_COLORS     256
// Where BG2's extended palettes start in their VRAM bank.
#define ICON_PLTT_SLOT_OFFSET 0x4000

#define RGB8(r, g, b) GX_RGB((r) >> 3, (g) >> 3, (b) >> 3)

enum PanelColor {
    PANEL_CLEAR = 0,
    PANEL_GREEN,
    PANEL_GREEN_LIGHT,
    PANEL_GREEN_DARK,
    PANEL_WHITE,
    PANEL_CELL_SHADE,
    PANEL_CELL_EDGE,
    PANEL_FRAME,
    PANEL_GREY,
    PANEL_GREY_LIGHT,
    PANEL_RED,
    PANEL_RED_LIGHT,
    PANEL_ORANGE,
    PANEL_YELLOW,
    PANEL_BLUE_LIGHT,
    PANEL_BLUE_DARK,
};

// The bag's own colors (bag_ui_main.pal), so the panel continues its art.
static const u16 sPanelPalette[16] = {
    [PANEL_CLEAR] = RGB8(0, 0, 0),
    [PANEL_GREEN] = RGB8(0x5a, 0xcd, 0x52),
    [PANEL_GREEN_LIGHT] = RGB8(0x8b, 0xe6, 0x62),
    [PANEL_GREEN_DARK] = RGB8(0x41, 0xb4, 0x41),
    [PANEL_WHITE] = RGB8(0xff, 0xff, 0xff),
    [PANEL_CELL_SHADE] = RGB8(0xde, 0xee, 0xee),
    [PANEL_CELL_EDGE] = RGB8(0x7b, 0xa4, 0xbd),
    [PANEL_FRAME] = RGB8(0x73, 0x7b, 0x94),
    [PANEL_GREY] = RGB8(0x94, 0x94, 0x94),
    [PANEL_GREY_LIGHT] = RGB8(0xe6, 0xe6, 0xe6),
    [PANEL_RED] = RGB8(0xde, 0x29, 0x08),
    [PANEL_RED_LIGHT] = RGB8(0xff, 0xde, 0xd5),
    [PANEL_ORANGE] = RGB8(0xf6, 0x94, 0x29),
    [PANEL_YELLOW] = RGB8(0xff, 0xe6, 0x9c),
    [PANEL_BLUE_LIGHT] = RGB8(0xa4, 0xcd, 0xf6),
    [PANEL_BLUE_DARK] = RGB8(0x52, 0x62, 0x9c),
};

// A pixel canvas, one byte per dot, sliced into tiles as it is put on a layer.
typedef struct Canvas {
    u8 *pixels;
    int width;
    int height;
} Canvas;

typedef struct BagGrid {
    BOOL active;
    int width;
    int left;
    int viewColumns;
    // The view x of the first cell, on a tile boundary, and the width of the cells.
    int gridX;
    int gridWidth;
    // The current view's cells.
    int columns;
    int rows;
    int cellWidth;
    int cellHeight;
    int gridY;
    int iconCellY;
    int numItems;
    int firstRow;
    BOOL iconsDirty;
    BOOL cursorInactive;
    BOOL hasMoveInfo;
    Window moveInfoWindow;
    // Each pocket's scroll position, kept while the bag is open. -1: not shown yet.
    s16 firstRows[POCKET_MAX];
    Canvas panel;
    Canvas icons;
    NARC *iconNarc;
    u16 iconPalette[ICON_COLORS];
    // The colors of the bag's text, which is drawn on the icon layer here.
    u16 textPalette[16];
    int iconColors;
} BagGrid;

static BagGrid sGrid;
// The view stays as it was left for as long as the game runs.
static BOOL sListView = FALSE;

#define DIGIT_WIDTH  5
#define DIGIT_HEIGHT 7

enum Glyph {
    GLYPH_H = 10,
    GLYPH_COUNT,
};

static const char *const sGlyphs[GLYPH_COUNT][DIGIT_HEIGHT] = {
    { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." },
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." },
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" },
    { ".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###." },
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." },
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." },
    { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." },
    { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." },
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." },
    { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." },
    [GLYPH_H] = { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" },
};

// The mark on the registered Key Item.
#define REGISTERED_MARK_SIZE 9

static const char *const sRegisteredMark[REGISTERED_MARK_SIZE] = {
    "..#####..",
    ".#######.",
    "##.###.##",
    "###.#.###",
    "####.####",
    "####.####",
    "####.####",
    ".#######.",
    "..#####..",
};

// Both views fill the same part of the panel.
static void ApplyView(void)
{
    if (sListView) {
        sGrid.columns = sGrid.gridWidth >= LIST_TWO_COLUMN_MIN ? 2 : 1;
        sGrid.rows = LIST_ROWS;
        sGrid.cellWidth = sGrid.gridWidth / sGrid.columns;
        sGrid.cellHeight = LIST_CELL_HEIGHT;
        sGrid.gridY = LIST_Y;
        sGrid.iconCellY = LIST_ICON_Y;
    } else {
        sGrid.columns = sGrid.gridWidth / GRID_CELL_WIDTH;
        sGrid.rows = GRID_ROWS;
        sGrid.cellWidth = GRID_CELL_WIDTH;
        sGrid.cellHeight = GRID_CELL_HEIGHT;
        sGrid.gridY = GRID_Y;
        sGrid.iconCellY = GRID_ICON_Y;
    }

    // The scroll positions count rows of the other view.
    for (int i = 0; i < POCKET_MAX; i++) {
        sGrid.firstRows[i] = -1;
    }

    sGrid.iconsDirty = TRUE;
}

void BagGrid_Begin(void)
{
    memset(&sGrid, 0, sizeof(sGrid));

#ifdef SDK_PORT
    if (SIM_Config_prj_GetConfig()->gridBag == FALSE
        || SIM_GetConfigPtr()->screenLayout != SIM_CONFIG_SCREEN_LAYOUT_TOP_ONLY) {
        return;
    }

    sGrid.active = TRUE;
    sGrid.width = SIM_SetWideUI(TRUE);
    sGrid.left = (sGrid.width - NATIVE_WIDTH) / 2;
    sGrid.viewColumns = (sGrid.width + TILE_WIDTH_PIXELS - 1) / TILE_WIDTH_PIXELS;

    if (sGrid.viewColumns > MAX_VIEW_COLUMNS) {
        sGrid.viewColumns = MAX_VIEW_COLUMNS;
    }

    const int room = sGrid.width - GRID_COLUMN * TILE_WIDTH_PIXELS - SCROLL_BAR_SPACE;
    int gridColumns = room / GRID_CELL_WIDTH;

    if (gridColumns > MAX_GRID_COLUMNS) {
        gridColumns = MAX_GRID_COLUMNS;
    }

    // What the cells leave over goes to both sides, in whole tiles.
    const int shiftTiles = (room - gridColumns * GRID_CELL_WIDTH) / 2 / TILE_WIDTH_PIXELS;

    sGrid.gridX = (GRID_COLUMN + shiftTiles) * TILE_WIDTH_PIXELS;
    sGrid.gridWidth = gridColumns * GRID_CELL_WIDTH;
    ApplyView();

    RenderView_SetHorizontalMargin(sGrid.left);
#endif
}

void BagGrid_End(void)
{
#ifdef SDK_PORT
    if (sGrid.active) {
        SIM_SetWideUI(FALSE);
        RenderView_SetHorizontalMargin(0);
    }
#endif

    sGrid.active = FALSE;
}

BOOL BagGrid_IsActive(void)
{
    return sGrid.active;
}

u8 BagGrid_PopupLayer(void)
{
    return sGrid.active ? BG_LAYER_MAIN_3 : BG_LAYER_MAIN_0;
}

u8 BagGrid_TextLayer(void)
{
    return sGrid.active ? BG_LAYER_MAIN_3 : BG_LAYER_MAIN_2;
}

GXVRamBGExtPltt BagGrid_ExtPaletteBank(void)
{
    return sGrid.active ? GX_VRAM_BGEXTPLTT_0123_E : GX_VRAM_BGEXTPLTT_NONE;
}

// The first 32K of VRAM holds BG1's tiles, the next 16K the screens, then come 32K of
// window tiles for BG3 and 48K of icon tiles for BG2. BG0 stays empty: a libntr from
// before its wide BG0 fix draws that layer at its native width, which stretches a
// 512-dot screen to twice its size.
void BagGrid_SetupLayers(BgConfig *bgConfig)
{
    static const struct {
        u8 screenSize;
        u8 colorMode;
        u8 screenBase;
        u8 charBase;
        u8 priority;
    } layers[4] = {
        { BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0x8000, GX_BG_CHARBASE_0x0c000, 3 },
        { BG_SCREEN_SIZE_512x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0x9000, GX_BG_CHARBASE_0x00000, 3 },
        { BG_SCREEN_SIZE_512x256, GX_BG_COLORMODE_256, GX_BG_SCRBASE_0xa000, GX_BG_CHARBASE_0x14000, 1 },
        { BG_SCREEN_SIZE_512x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xb000, GX_BG_CHARBASE_0x0c000, 0 },
    };

    for (int layer = BG_LAYER_MAIN_0; layer <= BG_LAYER_MAIN_3; layer++) {
        const BOOL wide = layers[layer].screenSize == BG_SCREEN_SIZE_512x256;
        BgTemplate template = {
            .x = 0,
            .y = 0,
            .bufferSize = wide ? WIDE_TILEMAP_SIZE : WIDE_TILEMAP_SIZE / 2,
            .baseTile = 0,
            .screenSize = layers[layer].screenSize,
            .colorMode = layers[layer].colorMode,
            .screenBase = layers[layer].screenBase,
            .charBase = layers[layer].charBase,
            .bgExtPltt = GX_BG_EXTPLTT_01,
            .priority = layers[layer].priority,
            .areaOver = 0,
            .mosaic = FALSE,
        };

        Bg_InitFromTemplate(bgConfig, layer, &template, BG_TYPE_STATIC);
        Bg_ClearTilemap(bgConfig, layer);
        Bg_SetOffset(bgConfig, layer, BG_OFFSET_UPDATE_SET_X, wide ? sGrid.left : 0);
    }

    Bg_ClearTilesRange(BG_LAYER_MAIN_2, TILE_SIZE_8BPP, 0, HEAP_ID_BAG);
    Bg_ClearTilesRange(BG_LAYER_MAIN_3, TILE_SIZE_4BPP, 0, HEAP_ID_BAG);
    Bg_ToggleLayer(BG_LAYER_MAIN_0, FALSE);
}

void BagGrid_LoadGraphics(BgConfig *bgConfig)
{
    u16 *tilemap = Bg_GetTilemapBuffer(bgConfig, BG_LAYER_MAIN_1);
    u16 column[DESCRIPTION_ROWS];

    // The description's panel runs on to the view's right edge.
    for (int row = 0; row < DESCRIPTION_ROWS; row++) {
        column[row] = tilemap[(DESCRIPTION_ROW + row) * NATIVE_COLUMNS + NATIVE_COLUMNS - 1];
    }

    for (int x = NATIVE_COLUMNS; x < sGrid.viewColumns; x++) {
        Bg_LoadToTilemapRect(bgConfig, BG_LAYER_MAIN_1, column, x, DESCRIPTION_ROW, 1, DESCRIPTION_ROWS);
    }

    // The list's frame makes way for the grid's panel.
    Bg_FillTilemapRect(bgConfig, BG_LAYER_MAIN_1, 0, PANEL_COLUMN, 0, sGrid.viewColumns - PANEL_COLUMN, PANEL_ROWS, 0);
    Bg_LoadPalette(BG_LAYER_MAIN_1, (void *)sPanelPalette, sizeof(sPanelPalette), PANEL_PLTT * sizeof(sPanelPalette));

    // The bag's art has a hole for the pocket's name, which showed the list's white
    // backing layer. Nothing is behind it here, so the backdrop takes that color.
    static const u16 backdrop = RGB8(0xff, 0xff, 0xff);
    Bg_LoadPalette(BG_LAYER_MAIN_1, (void *)&backdrop, sizeof(backdrop), 0);
    Bg_CopyTilemapBufferToVRAM(bgConfig, BG_LAYER_MAIN_1);
}

static void Canvas_Init(Canvas *canvas, int width, int height)
{
    canvas->width = width;
    canvas->height = height;
    canvas->pixels = Heap_Alloc(HEAP_ID_BAG, width * height);
    memset(canvas->pixels, 0, width * height);
}

static void Canvas_Fill(Canvas *canvas, int x, int y, int width, int height, u8 color)
{
    const int x0 = x < 0 ? 0 : x;
    const int y0 = y < 0 ? 0 : y;
    const int x1 = x + width > canvas->width ? canvas->width : x + width;
    const int y1 = y + height > canvas->height ? canvas->height : y + height;

    if (x1 <= x0) {
        return;
    }

    for (int row = y0; row < y1; row++) {
        memset(canvas->pixels + row * canvas->width + x0, color, x1 - x0);
    }
}

static void Canvas_Dot(Canvas *canvas, int x, int y, u8 color)
{
    if (x >= 0 && y >= 0 && x < canvas->width && y < canvas->height) {
        canvas->pixels[y * canvas->width + x] = color;
    }
}

// A filled box with a border and cut corners.
static void Canvas_Box(Canvas *canvas, int x, int y, int width, int height, u8 fill, u8 border, int borderWidth)
{
    Canvas_Fill(canvas, x + 1, y, width - 2, height, border);
    Canvas_Fill(canvas, x, y + 1, width, height - 2, border);
    Canvas_Fill(canvas, x + borderWidth, y + borderWidth, width - borderWidth * 2, height - borderWidth * 2, fill);
}

void BagGrid_Init(BagController *controller)
{
    Canvas_Init(&sGrid.panel, (sGrid.viewColumns - PANEL_COLUMN) * TILE_WIDTH_PIXELS, PANEL_HEIGHT);
    // The entry's text can reach past a narrow grid.
    const int textWidth = ENTRY_TEXT_X + ENTRY_WIDTH + TILE_WIDTH_PIXELS - 1;
    const int iconsWidth = sGrid.gridWidth > textWidth ? sGrid.gridWidth : textWidth;

    Canvas_Init(&sGrid.icons, iconsWidth & ~(TILE_WIDTH_PIXELS - 1), PANEL_HEIGHT);

    void *paletteFile = NARC_AllocAndReadWholeMember(controller->bagGraphicsNARC, bag_ui_main_NCLR, HEAP_ID_BAG);
    NNSG2dPaletteData *paletteData;

    if (NNS_G2dGetUnpackedPaletteData(paletteFile, &paletteData)) {
        memcpy(sGrid.textPalette, (const u16 *)paletteData->pRawData + TEXT_PLTT * 16, sizeof(sGrid.textPalette));
    }

    Heap_Free(paletteFile);

    int moveInfoColumns = sGrid.viewColumns - MOVE_INFO_COLUMN;

    if (moveInfoColumns > MOVE_INFO_MAX_COLUMNS) {
        moveInfoColumns = MOVE_INFO_MAX_COLUMNS;
    }

    if (sGrid.width - MOVE_INFO_X >= MOVE_INFO_MIN_WIDTH) {
        sGrid.hasMoveInfo = TRUE;
        Window_Add(controller->bgConfig, &sGrid.moveInfoWindow, BG_LAYER_MAIN_3, MOVE_INFO_COLUMN, DESCRIPTION_ROW, moveInfoColumns, DESCRIPTION_ROWS, 3, MOVE_INFO_BASE_TILE);
    }

    sGrid.iconNarc = NARC_ctor(NARC_INDEX_ITEMTOOL__ITEMDATA__ITEM_ICON, HEAP_ID_BAG);
    sGrid.iconsDirty = TRUE;
}

void BagGrid_Free(void)
{
    if (sGrid.active == FALSE) {
        return;
    }

    if (sGrid.hasMoveInfo) {
        Window_Remove(&sGrid.moveInfoWindow);
    }

    NARC_dtor(sGrid.iconNarc);
    Heap_Free(sGrid.icons.pixels);
    Heap_Free(sGrid.panel.pixels);
}

static BagApplicationPocket *CurrentPocket(BagController *controller)
{
    return &controller->bagCtx->accessiblePockets[controller->bagCtx->currPocketIdx];
}

int BagGrid_NumItems(void)
{
    return sGrid.numItems;
}

int BagGrid_Selected(BagController *controller)
{
    BagApplicationPocket *pocket = CurrentPocket(controller);
    return pocket->cursorScroll + pocket->cursorPos - 1;
}

// The list's cursor is a scroll position and a line, counted from an empty first
// entry. The grid keeps its slot in the same two values, as the code around the list
// reads them, with the line always 1.
static void SetSelected(BagController *controller, int selected)
{
    BagApplicationPocket *pocket = CurrentPocket(controller);

    pocket->cursorScroll = selected;
    pocket->cursorPos = 1;
}

void BagGrid_ToListCursor(int selected, int numItems, u16 *cursorScroll, u16 *cursorPos)
{
    // The list's entries: an empty one, the items, the one to close the bag, an empty one.
    const int lastScroll = numItems + 3 > BAG_UI_NUM_VISIBLE_ITEMS ? numItems + 3 - BAG_UI_NUM_VISIBLE_ITEMS : 0;
    int scroll = selected - 3;

    if (scroll > lastScroll) {
        scroll = lastScroll;
    }

    if (scroll < 0) {
        scroll = 0;
    }

    *cursorScroll = scroll;
    *cursorPos = selected + 1 - scroll;
}

static int TotalRows(void)
{
    return (sGrid.numItems + sGrid.columns - 1) / sGrid.columns;
}

static void SetFirstRow(BagController *controller, int firstRow)
{
    const int lastFirstRow = TotalRows() > sGrid.rows ? TotalRows() - sGrid.rows : 0;

    if (firstRow > lastFirstRow) {
        firstRow = lastFirstRow;
    }

    if (firstRow < 0) {
        firstRow = 0;
    }

    if (firstRow != sGrid.firstRow) {
        sGrid.iconsDirty = TRUE;
    }

    sGrid.firstRow = firstRow;
    sGrid.firstRows[controller->bagCtx->currPocketIdx] = firstRow;
}

// Scrolls as little as it takes to show the cursor's row.
static void FollowSelection(BagController *controller)
{
    const int row = BagGrid_Selected(controller) / sGrid.columns;
    int firstRow = sGrid.firstRow;

    if (row < firstRow) {
        firstRow = row;
    } else if (row >= firstRow + sGrid.rows) {
        firstRow = row - sGrid.rows + 1;
    }

    SetFirstRow(controller, firstRow);
}

void BagGrid_SetPocket(BagController *controller)
{
    BagApplicationPocket *pocket = CurrentPocket(controller);
    const int pocketSize = BagApplication_PocketSize(pocket->pocketType);
    int numItems = 0;

    while (numItems < pocketSize && pocket->items[numItems].item != ITEM_NONE && pocket->items[numItems].quantity != 0) {
        numItems++;
    }

    int selected = BagGrid_Selected(controller);

    if (selected > numItems - 1) {
        selected = numItems - 1;
    }

    if (selected < 0) {
        selected = 0;
    }

    sGrid.numItems = numItems;
    sGrid.iconsDirty = TRUE;
    SetSelected(controller, selected);

    // A pocket opens with the cursor's row second from the top, as far as that goes.
    const int savedFirstRow = sGrid.firstRows[controller->bagCtx->currPocketIdx];

    sGrid.firstRow = savedFirstRow >= 0 ? savedFirstRow : selected / sGrid.columns - 1;
    SetFirstRow(controller, sGrid.firstRow);
    FollowSelection(controller);
}

enum BagGridMove BagGrid_HandleMovement(BagController *controller)
{
    const int current = BagGrid_Selected(controller);
    const int column = current % sGrid.columns;
    const BOOL atLeftEdge = sGrid.numItems == 0 || column == 0;
    const BOOL atRightEdge = sGrid.numItems == 0 || column == sGrid.columns - 1 || current == sGrid.numItems - 1;
    int target = current;

    // Past the cells' edge is the next pocket, on a press of its own: holding the
    // key to cross the cells stops at the edge.
    if (JOY_NEW(PAD_KEY_LEFT) && atLeftEdge) {
        return BAG_GRID_MOVE_PREVIOUS_POCKET;
    }

    if (JOY_NEW(PAD_KEY_RIGHT) && atRightEdge) {
        return BAG_GRID_MOVE_NEXT_POCKET;
    }

    if (sGrid.numItems == 0) {
        return BAG_GRID_MOVE_NONE;
    }

    if (JOY_REPEAT(PAD_KEY_LEFT)) {
        target = atLeftEdge ? current : current - 1;
    } else if (JOY_REPEAT(PAD_KEY_RIGHT)) {
        target = atRightEdge ? current : current + 1;
    } else if (JOY_REPEAT(PAD_KEY_UP)) {
        target = current - sGrid.columns;

        if (target < 0) {
            target = current;
        }
    } else if (JOY_REPEAT(PAD_KEY_DOWN)) {
        target = current + sGrid.columns;

        // Down from the row above a short last row lands on the last item.
        if (target > sGrid.numItems - 1 && current / sGrid.columns == (sGrid.numItems - 1) / sGrid.columns) {
            target = current;
        }
    } else {
        return BAG_GRID_MOVE_NONE;
    }

    if (target > sGrid.numItems - 1) {
        target = sGrid.numItems - 1;
    }

    if (target == current) {
        return BAG_GRID_MOVE_NONE;
    }

    SetSelected(controller, target);
    FollowSelection(controller);

    return BAG_GRID_MOVE_CURSOR;
}

void BagGrid_ToggleView(BagController *controller)
{
    sListView = !sListView;
    ApplyView();
    BagGrid_SetPocket(controller);
}

void BagGrid_InvalidateIcons(void)
{
    sGrid.iconsDirty = TRUE;
}

static int CellY(int row)
{
    return sGrid.gridY + row * sGrid.cellHeight;
}

// Puts a 16-color canvas on BG1 at the panel's place, as tiles of its own. Tiles that
// look the same share one.
#define HASH_SIZE 2048

static void UploadPanel(BgConfig *bgConfig)
{
    Canvas *canvas = &sGrid.panel;
    const int tilesWide = canvas->width / TILE_WIDTH_PIXELS;
    const int tilesHigh = canvas->height / TILE_HEIGHT_PIXELS;

    u8 *tiles = Heap_Alloc(HEAP_ID_BAG, PANEL_TILE_MAX * TILE_SIZE_4BPP);
    u16 *tilemap = Heap_Alloc(HEAP_ID_BAG, tilesWide * tilesHigh * sizeof(u16));
    s16 *buckets = Heap_Alloc(HEAP_ID_BAG, HASH_SIZE * sizeof(s16));
    s16 *chain = Heap_Alloc(HEAP_ID_BAG, PANEL_TILE_MAX * sizeof(s16));
    u8 tile[TILE_SIZE_4BPP];
    int used = 0;

    for (int i = 0; i < HASH_SIZE; i++) {
        buckets[i] = -1;
    }

    for (int ty = 0; ty < tilesHigh; ty++) {
        for (int tx = 0; tx < tilesWide; tx++) {
            u32 hash = 2166136261u;

            for (int row = 0; row < TILE_HEIGHT_PIXELS; row++) {
                const u8 *src = canvas->pixels + (ty * TILE_HEIGHT_PIXELS + row) * canvas->width + tx * TILE_WIDTH_PIXELS;

                for (int col = 0; col < TILE_WIDTH_PIXELS; col += 2) {
                    const u8 packed = (src[col] & 0xF) | ((src[col + 1] & 0xF) << 4);

                    tile[row * 4 + col / 2] = packed;
                    hash = (hash ^ packed) * 16777619u;
                }
            }

            const int bucket = hash & (HASH_SIZE - 1);
            int found = -1;

            for (int i = buckets[bucket]; i >= 0; i = chain[i]) {
                if (memcmp(tiles + i * TILE_SIZE_4BPP, tile, TILE_SIZE_4BPP) == 0) {
                    found = i;
                    break;
                }
            }

            if (found < 0 && used < PANEL_TILE_MAX) {
                found = used++;
                memcpy(tiles + found * TILE_SIZE_4BPP, tile, TILE_SIZE_4BPP);
                chain[found] = buckets[bucket];
                buckets[bucket] = found;
            }

            GF_ASSERT(found >= 0);
            tilemap[ty * tilesWide + tx] = found >= 0 ? (PANEL_TILE_START + found) | (PANEL_PLTT << 12) : 0;
        }
    }

    // Tile indices change between redraws, so the tiles and their map go to VRAM together.
    Bg_LoadTiles(bgConfig, BG_LAYER_MAIN_1, tiles, used * TILE_SIZE_4BPP, PANEL_TILE_START);
    Bg_LoadToTilemapRect(bgConfig, BG_LAYER_MAIN_1, tilemap, PANEL_COLUMN, 0, tilesWide, tilesHigh);
    Bg_CopyTilemapBufferToVRAM(bgConfig, BG_LAYER_MAIN_1);

    Heap_Free(chain);
    Heap_Free(buckets);
    Heap_Free(tilemap);
    Heap_Free(tiles);
}

// Puts the 256-color icon canvas on BG2 over the grid. Each tile with something in
// it gets a tile of its own.
static void UploadIcons(BgConfig *bgConfig)
{
    Canvas *canvas = &sGrid.icons;
    const int tilesWide = canvas->width / TILE_WIDTH_PIXELS;
    const int tilesHigh = canvas->height / TILE_HEIGHT_PIXELS;

    u8 *tiles = Heap_Alloc(HEAP_ID_BAG, tilesWide * tilesHigh * TILE_SIZE_8BPP);
    u16 *tilemap = Heap_Alloc(HEAP_ID_BAG, tilesWide * tilesHigh * sizeof(u16));
    int used = 0;

    for (int ty = 0; ty < tilesHigh; ty++) {
        for (int tx = 0; tx < tilesWide; tx++) {
            u8 *tile = tiles + used * TILE_SIZE_8BPP;
            BOOL blank = TRUE;

            for (int row = 0; row < TILE_HEIGHT_PIXELS; row++) {
                const u8 *src = canvas->pixels + (ty * TILE_HEIGHT_PIXELS + row) * canvas->width + tx * TILE_WIDTH_PIXELS;

                memcpy(tile + row * TILE_WIDTH_PIXELS, src, TILE_WIDTH_PIXELS);

                for (int col = 0; col < TILE_WIDTH_PIXELS; col++) {
                    blank &= src[col] == 0;
                }
            }

            tilemap[ty * tilesWide + tx] = blank ? 0 : ICON_TILE_START + used++;
        }
    }

    if (used > 0) {
        Bg_LoadTiles(bgConfig, BG_LAYER_MAIN_2, tiles, used * TILE_SIZE_8BPP, ICON_TILE_START);
    }

    // The simulator reads every layer's extended palette from the start of the bank,
    // so the palette goes there as well as to BG2's own slot.
    GX_BeginLoadBGExtPltt();
    GX_LoadBGExtPltt(sGrid.iconPalette, ICON_PLTT_SLOT_OFFSET, sizeof(sGrid.iconPalette));
    GX_LoadBGExtPltt(sGrid.iconPalette, 0, sizeof(sGrid.iconPalette));
    GX_EndLoadBGExtPltt();

    Bg_LoadToTilemapRect(bgConfig, BG_LAYER_MAIN_2, tilemap, sGrid.gridX / TILE_WIDTH_PIXELS, 0, tilesWide, tilesHigh);
    Bg_CopyTilemapBufferToVRAM(bgConfig, BG_LAYER_MAIN_2);

    Heap_Free(tilemap);
    Heap_Free(tiles);
}

// The page's palette: each color the icons use, once. Should a page ever use more
// than fit, the rest take the closest color there is.
static u8 IconColor(u16 color)
{
    color &= 0x7FFF;

    for (int i = 1; i < sGrid.iconColors; i++) {
        if (sGrid.iconPalette[i] == color) {
            return i;
        }
    }

    if (sGrid.iconColors < ICON_COLORS) {
        sGrid.iconPalette[sGrid.iconColors] = color;
        return sGrid.iconColors++;
    }

    int closest = 1;
    int closestDistance = 0x7FFFFFFF;

    for (int i = 1; i < ICON_COLORS; i++) {
        const u16 other = sGrid.iconPalette[i];
        const int red = (color & 31) - (other & 31);
        const int green = ((color >> 5) & 31) - ((other >> 5) & 31);
        const int blue = ((color >> 10) & 31) - ((other >> 10) & 31);
        const int distance = red * red + green * green + blue * blue;

        if (distance < closestDistance) {
            closest = i;
            closestDistance = distance;
        }
    }

    return closest;
}

static void DrawIcon(u16 item, int x, int y)
{
    void *charFile = NARC_AllocAndReadWholeMember(sGrid.iconNarc, Item_FileID(item, ITEM_FILE_TYPE_ICON), HEAP_ID_BAG);
    void *paletteFile = NARC_AllocAndReadWholeMember(sGrid.iconNarc, Item_FileID(item, ITEM_FILE_TYPE_PALETTE), HEAP_ID_BAG);
    NNSG2dCharacterData *charData;
    NNSG2dPaletteData *paletteData;

    if (NNS_G2dGetUnpackedCharacterData(charFile, &charData) && NNS_G2dGetUnpackedPaletteData(paletteFile, &paletteData)) {
        const u8 *tiles = charData->pRawData;
        const u16 *colors = paletteData->pRawData;
        const int tilesPerRow = ICON_FILE_SIZE / TILE_WIDTH_PIXELS;
        u8 indices[16];
        BOOL mapped[16] = { FALSE };

        for (int row = 0; row < ICON_SIZE; row++) {
            for (int col = 0; col < ICON_SIZE; col++) {
                const u8 *tile = tiles + ((row / TILE_HEIGHT_PIXELS) * tilesPerRow + col / TILE_WIDTH_PIXELS) * TILE_SIZE_4BPP;
                const u8 packed = tile[(row % TILE_HEIGHT_PIXELS) * 4 + (col % TILE_WIDTH_PIXELS) / 2];
                const u8 index = (col & 1) ? packed >> 4 : packed & 0xF;

                if (index == 0) {
                    continue;
                }

                if (mapped[index] == FALSE) {
                    indices[index] = IconColor(colors[index]);
                    mapped[index] = TRUE;
                }

                Canvas_Dot(&sGrid.icons, x + col, y + row, indices[index]);
            }
        }
    }

    Heap_Free(paletteFile);
    Heap_Free(charFile);
}

// Dots drawn over an icon, with a dark outline to stand out from whatever is below.
static void DrawOutlined(const char *const *rows, int numRows, int x, int y, u8 color, u8 outline)
{
    Canvas *canvas = &sGrid.icons;

    for (int pass = 0; pass < 2; pass++) {
        for (int row = 0; row < numRows; row++) {
            for (int col = 0; rows[row][col] != '\0'; col++) {
                if (rows[row][col] != '#') {
                    continue;
                }

                if (pass == 1) {
                    Canvas_Dot(canvas, x + col, y + row, color);
                    continue;
                }

                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        Canvas_Dot(canvas, x + col + dx, y + row + dy, outline);
                    }
                }
            }
        }
    }
}

static int NumberWidth(int numGlyphs)
{
    return numGlyphs * (DIGIT_WIDTH + 1) - 1;
}

// Draws a number, with a glyph before it if prefix isn't negative, and returns its width.
static int DrawNumber(int value, int minDigits, int prefix, int x, int y, BOOL alignRight, u8 color, u8 outline)
{
    int glyphs[5];
    int count = 0;

    do {
        glyphs[count++] = value % 10;
        value /= 10;
    } while ((value > 0 || count < minDigits) && count < 4);

    if (prefix >= 0) {
        glyphs[count++] = prefix;
    }

    if (alignRight) {
        x -= NumberWidth(count);
    }

    for (int i = 0; i < count; i++) {
        DrawOutlined(sGlyphs[glyphs[count - 1 - i]], DIGIT_HEIGHT, x + i * (DIGIT_WIDTH + 1), y, color, outline);
    }

    return NumberWidth(count);
}

// Puts what a window holds on the icon layer. The bag's windows are on the layer
// above, with the menus that open over the cells, so the text among the cells is
// printed to a window that is never shown and copied from it.
static void DrawWindow(Window *window, int x, int y, int maxWidth)
{
    const u8 *pixels = window->pixels;
    const int height = window->height * TILE_HEIGHT_PIXELS;
    int width = window->width * TILE_WIDTH_PIXELS;
    u8 colors[16];
    BOOL mapped[16] = { FALSE };

    if (width > maxWidth) {
        width = maxWidth;
    }

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            const u8 *tile = pixels + ((row / TILE_HEIGHT_PIXELS) * window->width + col / TILE_WIDTH_PIXELS) * TILE_SIZE_4BPP;
            const u8 packed = tile[(row % TILE_HEIGHT_PIXELS) * 4 + (col % TILE_WIDTH_PIXELS) / 2];
            const u8 index = (col & 1) ? packed >> 4 : packed & 0xF;

            if (index == 0) {
                continue;
            }

            if (mapped[index] == FALSE) {
                colors[index] = IconColor(sGrid.textPalette[index]);
                mapped[index] = TRUE;
            }

            Canvas_Dot(&sGrid.icons, x + col, y + row, colors[index]);
        }
    }
}

static void DrawIcons(BagController *controller)
{
    BagApplicationPocket *pocket = CurrentPocket(controller);
    Window *window = &controller->windows[BAG_UI_WINDOW_ITEM_LIST];
    Canvas *canvas = &sGrid.icons;

    memset(canvas->pixels, 0, canvas->width * canvas->height);
    memset(sGrid.iconPalette, 0, sizeof(sGrid.iconPalette));
    sGrid.iconColors = 1;

    const u8 dark = IconColor(RGB8(0x10, 0x18, 0x20));
    const u8 white = IconColor(RGB8(0xff, 0xff, 0xff));
    const u8 yellow = IconColor(RGB8(0xff, 0xde, 0x52));
    const u8 blue = IconColor(RGB8(0x9c, 0xde, 0xff));

    for (int slot = 0; slot < sGrid.columns * sGrid.rows; slot++) {
        const int index = sGrid.firstRow * sGrid.columns + slot;

        if (index >= sGrid.numItems) {
            break;
        }

        const BagItem *bagItem = &pocket->items[index];
        // The canvas starts at the cells' left edge.
        const int cellX = (slot % sGrid.columns) * sGrid.cellWidth;
        const int cellY = CellY(slot / sGrid.columns);
        const int x = cellX + ICON_CELL_X;
        const int y = cellY + sGrid.iconCellY;
        const int numberY = y + ICON_SIZE - DIGIT_HEIGHT - 1;

        DrawIcon(bagItem->item, x, y);

        switch (pocket->pocketType) {
        case POCKET_KEY_ITEMS:
            if (Bag_GetRegisteredItem(controller->bag) == bagItem->item) {
                DrawOutlined(sRegisteredMark, REGISTERED_MARK_SIZE, x + ICON_SIZE - REGISTERED_MARK_SIZE, y + ICON_SIZE - REGISTERED_MARK_SIZE - 1, blue, dark);
            }
            break;
        case POCKET_TMHMS:
            // The machine's number; HMs can't be used up and show no count.
            if (bagItem->item >= ITEM_HM01) {
                DrawNumber(bagItem->item - ITEM_HM01 + 1, 1, GLYPH_H, x, numberY, FALSE, blue, dark);
            } else {
                DrawNumber(bagItem->item - ITEM_TM01 + 1, 2, -1, x, numberY, FALSE, yellow, dark);

                if (bagItem->quantity > 1) {
                    DrawNumber(bagItem->quantity, 1, -1, x + ICON_SIZE, numberY, TRUE, white, dark);
                }
            }
            break;
        default:
            DrawNumber(bagItem->quantity, 1, -1, x + ICON_SIZE, numberY, TRUE, white, dark);
            break;
        }

        if (sListView) {
            Window_FillTilemap(window, 0);
            Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, controller->itemNames[index], 0, 0, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 2, 0), NULL);
            DrawWindow(window, cellX + LIST_NAME_X, cellY + LIST_NAME_Y, sGrid.cellWidth - LIST_NAME_X - LIST_NAME_MARGIN);
        }
    }
}

// The selected item's entry, as the bag has printed it to the list's window.
static void DrawEntry(BagController *controller)
{
    Canvas_Fill(&sGrid.icons, 0, ENTRY_TEXT_Y, sGrid.icons.width, TEXT_LINES(1), 0);
    DrawWindow(&controller->windows[BAG_UI_WINDOW_ITEM_LIST], ENTRY_TEXT_X, ENTRY_TEXT_Y, ENTRY_WIDTH);
}

static void DrawPanel(BagController *controller)
{
    Canvas *canvas = &sGrid.panel;
    const int selected = BagGrid_Selected(controller);
    // The canvas starts at the panel's left edge.
    const int gridX = sGrid.gridX - PANEL_X;

    Canvas_Fill(canvas, 0, 0, canvas->width, canvas->height, PANEL_GREEN);
    Canvas_Fill(canvas, 0, 0, canvas->width, 1, PANEL_GREY);
    Canvas_Fill(canvas, 0, 1, canvas->width, 1, PANEL_GREEN_LIGHT);

    // The lines over the description, as the bag's art draws them left of the panel.
    Canvas_Fill(canvas, 0, PANEL_HEIGHT - 5, canvas->width, 1, PANEL_GREY);
    Canvas_Fill(canvas, 0, PANEL_HEIGHT - 4, canvas->width, 1, PANEL_BLUE_LIGHT);
    Canvas_Fill(canvas, 0, PANEL_HEIGHT - 3, canvas->width, 2, PANEL_BLUE_DARK);
    Canvas_Fill(canvas, 0, PANEL_HEIGHT - 1, canvas->width, 1, PANEL_BLUE_LIGHT);

    if (sListView == FALSE) {
        // The entry's bar is as wide as the cells, or as the entry where that is wider.
        const int entryWidth = ENTRY_TEXT_X + ENTRY_WIDTH + 1;

        Canvas_Box(canvas, gridX + 1, ENTRY_Y, (sGrid.gridWidth > entryWidth ? sGrid.gridWidth : entryWidth) - 2, ENTRY_HEIGHT, PANEL_WHITE, PANEL_WHITE, 1);
    }

    for (int slot = 0; slot < sGrid.columns * sGrid.rows; slot++) {
        const int index = sGrid.firstRow * sGrid.columns + slot;
        const int x = gridX + (slot % sGrid.columns) * sGrid.cellWidth + 1;
        const int y = CellY(slot / sGrid.columns) + 1;
        const int width = sGrid.cellWidth - 2;
        const int height = sGrid.cellHeight - 2;

        if (index >= sGrid.numItems) {
            Canvas_Box(canvas, x, y, width, height, PANEL_GREEN_DARK, PANEL_GREEN_DARK, 1);
        } else if (controller->isMovingItem == TRUE && controller->movedItemID == index) {
            Canvas_Box(canvas, x, y, width, height, PANEL_YELLOW, PANEL_ORANGE, 2);
        } else if (index == selected && sGrid.cursorInactive) {
            Canvas_Box(canvas, x, y, width, height, PANEL_GREY_LIGHT, PANEL_FRAME, 2);
        } else if (index == selected) {
            Canvas_Box(canvas, x, y, width, height, PANEL_RED_LIGHT, PANEL_RED, 2);
        } else {
            Canvas_Box(canvas, x, y, width, height, PANEL_WHITE, PANEL_CELL_EDGE, 1);
            Canvas_Fill(canvas, x + 1, y + height - 3, width - 2, 2, PANEL_CELL_SHADE);
        }

        // While an item is being moved the cursor is a frame, so the item under it still shows.
        if (controller->isMovingItem == TRUE && index == selected && controller->movedItemID != index) {
            Canvas_Box(canvas, x, y, width, height, PANEL_WHITE, PANEL_RED, 2);
        }
    }

    if (TotalRows() > sGrid.rows) {
        const int trackX = gridX + sGrid.gridWidth + 2;
        const int trackY = sGrid.gridY + 2;
        const int trackHeight = sGrid.rows * sGrid.cellHeight - 4;
        int thumbHeight = trackHeight * sGrid.rows / TotalRows();

        if (thumbHeight < 8) {
            thumbHeight = 8;
        }

        const int thumbY = trackY + (trackHeight - thumbHeight) * sGrid.firstRow / (TotalRows() - sGrid.rows);

        Canvas_Fill(canvas, trackX, trackY, 4, trackHeight, PANEL_GREEN_DARK);
        Canvas_Box(canvas, trackX, thumbY, 4, thumbHeight, PANEL_WHITE, PANEL_WHITE, 1);
    }

    UploadPanel(controller->bgConfig);
}

void BagGrid_Draw(BagController *controller)
{
    if (sGrid.iconsDirty) {
        DrawIcons(controller);
        sGrid.iconsDirty = FALSE;
    }

    if (sListView == FALSE) {
        DrawEntry(controller);
    }

    UploadIcons(controller->bgConfig);
    DrawPanel(controller);
}

void BagGrid_SetCursorActive(BagController *controller, BOOL active)
{
    if (sGrid.active == FALSE || sGrid.cursorInactive == !active) {
        return;
    }

    sGrid.cursorInactive = !active;
    DrawPanel(controller);
}

BOOL BagGrid_HasMoveInfo(void)
{
    return sGrid.hasMoveInfo;
}

Window *BagGrid_MoveInfoWindow(void)
{
    return &sGrid.moveInfoWindow;
}

BOOL BagGrid_MoveInfoHasPP(void)
{
    return sGrid.width - MOVE_INFO_X >= MOVE_INFO_PP_WIDTH;
}

void BagGrid_ShowMoveIcons(BagController *controller, u16 item)
{
    ManagedSprite *typeIcon = controller->sprites[BAG_SPRITE_MOVE_TYPE];
    ManagedSprite *categoryIcon = controller->sprites[BAG_SPRITE_MOVE_CATEGORY];

    ManagedSprite_SetDrawFlag(typeIcon, item != ITEM_NONE);
    ManagedSprite_SetDrawFlag(categoryIcon, item != ITEM_NONE);

    if (item == ITEM_NONE) {
        return;
    }

    const u16 move = Item_MoveForTMHM(item);
    const u16 type = MoveTable_LoadParam(move, MOVEATTRIBUTE_TYPE);
    const u16 category = MoveTable_LoadParam(move, MOVEATTRIBUTE_CLASS);

    // The tiles reach VRAM in VBlank, along with the palettes in the OAM.
    SpriteSystem_ReplaceCharResObj(controller->spriteSystem, controller->spriteMan, TypeIcon_GetNARC(), TypeIcon_GetChar(type), TRUE, TYPE_ICON_RESOURCE);
    SpriteSystem_ReplaceCharResObj(controller->spriteSystem, controller->spriteMan, CategoryIcon_GetNARC(), CategoryIcon_GetChar(category), TRUE, CATEGORY_ICON_RESOURCE);
    ManagedSprite_SetExplicitPalette(typeIcon, TypeIcon_GetPltt(type) + TYPE_ICON_PLTT_START);
    ManagedSprite_SetExplicitPalette(categoryIcon, CategoryIcon_GetPltt(category) + TYPE_ICON_PLTT_START);
    ManagedSprite_SetPositionXY(typeIcon, MOVE_INFO_TYPE_X, MOVE_INFO_ICON_Y);
    ManagedSprite_SetPositionXY(categoryIcon, MOVE_INFO_CATEGORY_X, MOVE_INFO_ICON_Y);
}

// The sprites are placed for the native screen, whose left edge the view's stands in
// for. The list's highlight and the bar that marks where a moved item goes have no
// place in the grid, which draws its own cursor.
void BagGrid_DrawSprites(BagController *controller)
{
    static const u8 hidden[] = { BAG_SPRITE_ITEM_HIGHLIGHT, BAG_SPRITE_MOVING_ITEM_POS_BAR };
    VecFx32 positions[NUM_BAG_UI_SPRITES];
    BOOL wasDrawn[NELEMS(hidden)];

    if (sGrid.active == FALSE) {
        SpriteSystem_DrawSprites(controller->spriteMan);
        return;
    }

    for (int i = 0; i < NUM_BAG_UI_SPRITES; i++) {
        positions[i] = *Sprite_GetPosition(controller->sprites[i]->sprite);

        // The touch screen's sprite stays where it is.
        if (i != BAG_SPRITE_PRESSED_BUTTON_SHOCKWAVE) {
            VecFx32 shifted = positions[i];

            shifted.x -= sGrid.left * FX32_ONE;
            Sprite_SetPosition(controller->sprites[i]->sprite, &shifted);
        }
    }

    for (int i = 0; i < NELEMS(hidden); i++) {
        wasDrawn[i] = ManagedSprite_GetDrawFlag(controller->sprites[hidden[i]]);
        ManagedSprite_SetDrawFlag(controller->sprites[hidden[i]], FALSE);
    }

    SpriteSystem_DrawSprites(controller->spriteMan);

    for (int i = 0; i < NELEMS(hidden); i++) {
        ManagedSprite_SetDrawFlag(controller->sprites[hidden[i]], wasDrawn[i]);
    }

    for (int i = 0; i < NUM_BAG_UI_SPRITES; i++) {
        Sprite_SetPosition(controller->sprites[i]->sprite, &positions[i]);
    }
}
