#include "applications/poketch/poketch_app_picker.h"

#include <nitro.h>
#include <string.h>

#include "applications/poketch/poketch_graphics.h"

#include "bg_window.h"
#include "font.h"
#include "heap.h"
#include "message.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "text.h"

#include "res/sound/pl_sound_data.naix"
#include "res/text/bank/poketch_app_picker.h"

#define BG_LAYER      BG_LAYER_SUB_2
#define WINDOW_X      2
#define WINDOW_Y      2
#define WINDOW_WIDTH  24
#define WINDOW_HEIGHT 20
#define BASE_TILE     1
#define COLUMN_WIDTH  (WINDOW_WIDTH * 8 / 2)
// FONT_SYSTEM capitals only ink rows 3-12 of the 16px cell, and glyph pixels in
// color 0 are skipped, so rows can overlap: 13 rows (every app) fit unscrolled.
#define ROW_HEIGHT    12
#define GLYPH_TOP     3

#define COLOR_DARK   1
#define COLOR_SHADOW 8
#define COLOR_LIGHT  4

struct PoketchAppPicker {
    BgConfig *bgConfig;
    MessageLoader *loader;
    Window window;
    u8 apps[POKETCH_APPID_PORT_MAX];
    u8 count;
    u8 rows;
    u8 cursor;
};

static void PoketchAppPicker_Draw(PoketchAppPicker *picker)
{
    Window *window = &picker->window;
    u32 top = (WINDOW_HEIGHT * 8 - picker->rows * ROW_HEIGHT) / 2;

    Window_FillTilemap(window, COLOR_LIGHT);

    for (u32 i = 0; i < picker->count; i++) {
        u32 x = (i / picker->rows) * COLUMN_WIDTH;
        u32 y = top + (i % picker->rows) * ROW_HEIGHT;
        BOOL selected = i == picker->cursor;

        if (selected) {
            Window_FillRectWithColor(window, COLOR_DARK, x + 2, y, COLUMN_WIDTH - 4, ROW_HEIGHT);
        }

        String *name = MessageLoader_GetNewString(picker->loader, PoketchAppPicker_Text_DigitalWatch + picker->apps[i]);
        TextColor color = selected ? TEXT_COLOR(COLOR_LIGHT, COLOR_SHADOW, 0) : TEXT_COLOR(COLOR_DARK, COLOR_SHADOW, 0);
        Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, name, x + 6, y + 1 - GLYPH_TOP, TEXT_SPEED_NO_TRANSFER, color, NULL);
        String_Free(name);
    }

    Window_LoadTiles(window);
}

PoketchAppPicker *PoketchAppPicker_New(BgConfig *bgConfig, const Poketch *poketch, enum HeapID heapID)
{
    static const BgTemplate bgTemplate = {
        .x = 0,
        .y = 0,
        .bufferSize = 0x800,
        .baseTile = 0,
        .screenSize = BG_SCREEN_SIZE_256x256,
        .colorMode = GX_BG_COLORMODE_16,
        .screenBase = GX_BG_SCRBASE_0x7000,
        .charBase = GX_BG_CHARBASE_0x00000,
        .bgExtPltt = GX_BG_EXTPLTT_01,
        .priority = 2,
        .areaOver = 0,
        .mosaic = FALSE,
    };

    PoketchAppPicker *picker = Heap_Alloc(heapID, sizeof(PoketchAppPicker));
    enum PoketchAppID current = Poketch_CurrentAppID(poketch);

    memset(picker, 0, sizeof(*picker));
    picker->bgConfig = bgConfig;

    // App ID order, the same order L and R cycle through.
    for (int appID = 0; appID < POKETCH_APPID_PORT_MAX; appID++) {
        if (Poketch_IsAppVisible(poketch, appID)) {
            if (appID == current) {
                picker->cursor = picker->count;
            }
            picker->apps[picker->count++] = appID;
        }
    }
    picker->rows = (picker->count + 1) / 2;

    Bg_InitFromTemplate(bgConfig, BG_LAYER, &bgTemplate, BG_TYPE_STATIC);
    Bg_FillTilesRange(bgConfig, BG_LAYER, COLOR_LIGHT, 1, 0);
    Bg_ClearTilemap(bgConfig, BG_LAYER);
    PoketchGraphics_LoadActivePalette(0, 0);

    picker->loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_POKETCH_APP_PICKER, heapID);
    Window_Add(bgConfig, &picker->window, BG_LAYER, WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT, 0, BASE_TILE);
    Window_PutToTilemap(&picker->window);
    PoketchAppPicker_Draw(picker);
    Bg_CopyTilemapBufferToVRAM(bgConfig, BG_LAYER);

    GXSDispCnt dispCnt = GXS_GetDispCnt();
    GXS_SetVisiblePlane(dispCnt.visiblePlane | GX_PLANEMASK_BG2);

    return picker;
}

void PoketchAppPicker_Free(PoketchAppPicker *picker)
{
    // Not every app uses BG2, so hide the layer for the next one.
    GXSDispCnt dispCnt = GXS_GetDispCnt();
    GXS_SetVisiblePlane(dispCnt.visiblePlane & ~GX_PLANEMASK_BG2);

    Window_Remove(&picker->window);
    MessageLoader_Free(picker->loader);
    Bg_FreeTilemapBuffer(picker->bgConfig, BG_LAYER);
    Heap_Free(picker);
}

static u32 PoketchAppPicker_ColumnCount(const PoketchAppPicker *picker, u32 column)
{
    return column == 0 ? picker->rows : picker->count - picker->rows;
}

static u32 PoketchAppPicker_Move(const PoketchAppPicker *picker, u32 keys)
{
    u32 column = picker->cursor / picker->rows;
    u32 row = picker->cursor % picker->rows;
    u32 columnCount = PoketchAppPicker_ColumnCount(picker, column);

    if (keys & PAD_KEY_UP) {
        row = row == 0 ? columnCount - 1 : row - 1;
    } else if (keys & PAD_KEY_DOWN) {
        row = row + 1 >= columnCount ? 0 : row + 1;
    } else if (keys & (PAD_KEY_LEFT | PAD_KEY_RIGHT)) {
        u32 other = column ^ 1;
        u32 otherCount = PoketchAppPicker_ColumnCount(picker, other);

        if (otherCount == 0) {
            return picker->cursor;
        }

        column = other;
        row = row >= otherCount ? otherCount - 1 : row;
    }

    return column * picker->rows + row;
}

int PoketchAppPicker_HandleInput(PoketchAppPicker *picker, u32 pressedKeys, u32 repeatKeys, u32 cancelKeys)
{
    if (cancelKeys) {
        Sound_PlayEffect(SEQ_SE_DP_POKETCH_003_sseq);
        return POKETCH_APP_PICKER_CANCEL;
    }

    if (pressedKeys & PAD_BUTTON_A) {
        Sound_PlayEffect(SEQ_SE_DP_POKETCH_003_sseq);
        return picker->apps[picker->cursor];
    }

    u32 cursor = PoketchAppPicker_Move(picker, repeatKeys);

    if (cursor != picker->cursor) {
        picker->cursor = cursor;
        Sound_PlayEffect(SEQ_SE_DP_POKETCH_003_sseq);
        PoketchAppPicker_Draw(picker);
    }

    return POKETCH_APP_PICKER_BROWSING;
}
