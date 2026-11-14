#include "applications/poketch/route_encounters/graphics.h"

#include <nitro.h>
#include <string.h>

#include "constants/species.h"

#include "applications/poketch/poketch_animation.h"
#include "applications/poketch/poketch_graphics.h"
#include "applications/poketch/poketch_task.h"

#include "bg_window.h"
#include "font.h"
#include "heap.h"
#include "message.h"
#include "pokemon_icon.h"
#include "string_gf.h"
#include "sys_task_manager.h"
#include "text.h"

#include "res/graphics/poketch/poketch.naix"
#include "res/text/bank/poketch_route_encounters.h"

// Same grid as the Pokemon History app.
#define ICON_COLUMNS 4
#define ICON_STEP_X  40
#define ICON_STEP_Y  48
#define ICON_FIRST_X 48
#define ICON_FIRST_Y 48

#define BG_LAYER           BG_LAYER_SUB_2
#define TITLE_BASE_TILE    1
#define MESSAGE_BASE_TILE  (TITLE_BASE_TILE + 24 * 2)
#define QUESTION_TILE      (MESSAGE_BASE_TILE + 24 * 2)
#define QUESTION_SIZE      4 // Tiles per side, the size of an icon.
#define TEXT_WINDOW_WIDTH  24
#define TEXT_WINDOW_HEIGHT 2

// Icons are 32x32 and tile-aligned, so a "?" can replace one on the BG.
#define ICON_TILE_X(column) ((ICON_FIRST_X - 16 + ICON_STEP_X * (column)) / 8)
#define ICON_TILE_Y(row)    ((ICON_FIRST_Y - 16 + ICON_STEP_Y * (row)) / 8)

// Caught marks are sprites in front of the icons. They reuse the 32x32 icon
// cell with their own tiles: transparent but for a ball in the bottom-right.
#define ICON_CHARS   16
#define BALL_CHAR    (ROUTE_ENCOUNTERS_MAX_MONS * ICON_CHARS)
#define BALL_PALETTE 4 // After the four icon palettes.

#define MON_ANIM_DATA(r, c)                                             \
    {                                                                   \
        .translation = { FX32_CONST(ICON_FIRST_X + ICON_STEP_X * c),    \
            FX32_CONST(ICON_FIRST_Y + ICON_STEP_Y * r) },               \
        .animIdx = 4,                                                   \
        .flip = NNS_G2D_RENDERERFLIP_NONE,                              \
        .oamPriority = 2,                                               \
        .priority = ROUTE_ENCOUNTERS_MAX_MONS - (ICON_COLUMNS * r) - c, \
        .hasAffineTransform = TRUE,                                     \
    }

#define BALL_ANIM_DATA(r, c)                                         \
    {                                                                \
        .translation = { FX32_CONST(ICON_FIRST_X + ICON_STEP_X * c), \
            FX32_CONST(ICON_FIRST_Y + ICON_STEP_Y * r) },            \
        .animIdx = 4,                                                \
        .flip = NNS_G2D_RENDERERFLIP_NONE,                           \
        .oamPriority = 2,                                            \
        .priority = 0,                                               \
        .hasAffineTransform = TRUE,                                  \
    }

static void EndTask(PoketchTaskManager *taskMan);
static void Task_Init(SysTask *task, void *taskMan);
static void Task_SetSprites(SysTask *task, void *taskMan);
static void Task_SetTilemap(SysTask *task, void *taskMan);
static void Task_LoadTiles(SysTask *task, void *taskMan);
static void Task_Free(SysTask *task, void *taskMan);
static void SetupSprites(RouteEncountersGraphics *graphics);
static void UnloadSprites(RouteEncountersGraphics *graphics);
static void SetSpriteAttributes(RouteEncountersGraphics *graphics, const RouteEncountersView *view);
static void SetTilemap(RouteEncountersGraphics *graphics, const RouteEncountersView *view);
static void LoadTiles(RouteEncountersGraphics *graphics, const RouteEncountersView *view);

BOOL PoketchRouteEncountersGraphics_New(RouteEncountersGraphics **dest, const RouteEncountersView *view, BgConfig *bgConfig)
{
    RouteEncountersGraphics *graphics = Heap_Alloc(HEAP_ID_POKETCH_APP, sizeof(RouteEncountersGraphics));

    if (graphics != NULL) {
        PoketchTask_InitActiveTaskList(graphics->activeTasks, ROUTE_ENCOUNTERS_TASK_SLOTS);
        graphics->view = view;
        graphics->bgConfig = PoketchGraphics_GetBgConfig();
        graphics->animMan = PoketchGraphics_GetAnimationManager();
        *dest = graphics;

        return TRUE;
    }

    return FALSE;
}

void PoketchRouteEncountersGraphics_Free(RouteEncountersGraphics *graphics)
{
    if (graphics != NULL) {
        Heap_Free(graphics);
    }
}

static const PoketchTask sRouteEncountersTasks[] = {
    { ROUTE_ENCOUNTERS_GRAPHICS_INIT, Task_Init, 0 },
    { ROUTE_ENCOUNTERS_GRAPHICS_SET_SPRITES, Task_SetSprites, 0 },
    { ROUTE_ENCOUNTERS_GRAPHICS_SET_TILEMAP, Task_SetTilemap, 0 },
    { ROUTE_ENCOUNTERS_GRAPHICS_LOAD_TILES, Task_LoadTiles, 0 },
    { ROUTE_ENCOUNTERS_GRAPHICS_FREE, Task_Free, 0 },
    { 0 }
};

void PoketchRouteEncountersGraphics_StartTask(RouteEncountersGraphics *graphics, enum RouteEncountersGraphicsTask taskID)
{
    PoketchTask_Start(sRouteEncountersTasks, taskID, graphics, graphics->view, graphics->activeTasks, 2, HEAP_ID_POKETCH_APP);
}

BOOL PoketchRouteEncountersGraphics_TaskIsNotActive(RouteEncountersGraphics *graphics, enum RouteEncountersGraphicsTask taskID)
{
    return PoketchTask_TaskIsNotActive(graphics->activeTasks, taskID);
}

BOOL PoketchRouteEncountersGraphics_NoActiveTasks(RouteEncountersGraphics *graphics)
{
    return PoketchTask_NoActiveTasks(graphics->activeTasks);
}

static void EndTask(PoketchTaskManager *taskMan)
{
    RouteEncountersGraphics *graphics = PoketchTask_GetTaskData(taskMan);
    PoketchTask_EndTask(graphics->activeTasks, taskMan);
}

// 1 is the darkest Poketch shade, 4 the screen background, 0 transparent.
static const char *const sBallArt[8] = {
    "00111100",
    "01111110",
    "11111111",
    "11144111",
    "14411441",
    "14444441",
    "01444410",
    "00111100",
};

// The species 0 icon is a pale bubble that vanishes in the Poketch's
// palette, so unseen species get this dark "?" instead.
static const char *const sQuestionArt[QUESTION_SIZE * 8] = {
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444441111114444444444444",
    "44444444444111111111144444444444",
    "44444444441111444411114444444444",
    "44444444411114444441111444444444",
    "44444444411144444444111444444444",
    "44444444411144444444111444444444",
    "44444444444444444444111444444444",
    "44444444444444444441111444444444",
    "44444444444444444411114444444444",
    "44444444444444444111144444444444",
    "44444444444444441111444444444444",
    "44444444444444411114444444444444",
    "44444444444444411144444444444444",
    "44444444444444411144444444444444",
    "44444444444444411144444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444411144444444444444",
    "44444444444444411144444444444444",
    "44444444444444411144444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
    "44444444444444444444444444444444",
};

// Converts pixel art, sizeInTiles tiles square, to 4bpp tiles in row-major tile order.
static void ArtToTiles(const char *const *art, int sizeInTiles, u32 *tiles)
{
    for (int tileY = 0; tileY < sizeInTiles; tileY++) {
        for (int tileX = 0; tileX < sizeInTiles; tileX++) {
            u32 *tile = tiles + (tileY * sizeInTiles + tileX) * 8;

            for (int y = 0; y < 8; y++) {
                tile[y] = 0;

                for (int x = 0; x < 8; x++) {
                    tile[y] |= (u32)(art[tileY * 8 + y][tileX * 8 + x] - '0') << (x * 4);
                }
            }
        }
    }
}

static void LoadCustomTiles(BgConfig *bgConfig)
{
    static u32 questionTiles[QUESTION_SIZE * QUESTION_SIZE * 8];
    static u32 ballChars[ICON_CHARS * 8];

    ArtToTiles(sQuestionArt, QUESTION_SIZE, questionTiles);
    Bg_LoadTiles(bgConfig, BG_LAYER, questionTiles, sizeof(questionTiles), QUESTION_TILE);

    // Icons use 1D mapping, so the bottom-right tile of the cell is the last.
    memset(ballChars, 0, sizeof(ballChars));
    ArtToTiles(sBallArt, 1, ballChars + (ICON_CHARS - 1) * 8);
    DC_FlushRange(ballChars, sizeof(ballChars));
    GXS_LoadOBJ(ballChars, BALL_CHAR * 0x20, sizeof(ballChars));
}

static void Task_Init(SysTask *task, void *taskMan)
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

    RouteEncountersGraphics *graphics = PoketchTask_GetTaskData(taskMan);
    const RouteEncountersView *view = PoketchTask_GetConstTaskData(taskMan);

    Bg_InitFromTemplate(graphics->bgConfig, BG_LAYER, &bgTemplate, BG_TYPE_STATIC);
    Bg_FillTilesRange(graphics->bgConfig, BG_LAYER, 4, 1, 0);
    LoadCustomTiles(graphics->bgConfig);
    PoketchGraphics_LoadActivePalette(0, PLTT_OFFSET(BALL_PALETTE));

    // The screen is concealed while an app starts, so everything can change at once.
    SetupSprites(graphics);
    SetSpriteAttributes(graphics, view);
    SetTilemap(graphics, view);
    LoadTiles(graphics, view);

    GXSDispCnt dispCnt = GXS_GetDispCnt();
    GXS_SetVisiblePlane(dispCnt.visiblePlane | GX_PLANEMASK_BG2);

    EndTask(taskMan);
}

static void Task_SetSprites(SysTask *task, void *taskMan)
{
    SetSpriteAttributes(PoketchTask_GetTaskData(taskMan), PoketchTask_GetConstTaskData(taskMan));
    EndTask(taskMan);
}

static void Task_SetTilemap(SysTask *task, void *taskMan)
{
    SetTilemap(PoketchTask_GetTaskData(taskMan), PoketchTask_GetConstTaskData(taskMan));
    EndTask(taskMan);
}

static void Task_LoadTiles(SysTask *task, void *taskMan)
{
    LoadTiles(PoketchTask_GetTaskData(taskMan), PoketchTask_GetConstTaskData(taskMan));
    EndTask(taskMan);
}

static void Task_Free(SysTask *task, void *taskMan)
{
    RouteEncountersGraphics *graphics = PoketchTask_GetTaskData(taskMan);

    UnloadSprites(graphics);
    Bg_FreeTilemapBuffer(graphics->bgConfig, BG_LAYER);
    EndTask(taskMan);
}

static void SetupSprites(RouteEncountersGraphics *graphics)
{
    static const PoketchAnimation_AnimationData animData[] = {
        MON_ANIM_DATA(0, 0),
        MON_ANIM_DATA(0, 1),
        MON_ANIM_DATA(0, 2),
        MON_ANIM_DATA(0, 3),
        MON_ANIM_DATA(1, 0),
        MON_ANIM_DATA(1, 1),
        MON_ANIM_DATA(1, 2),
        MON_ANIM_DATA(1, 3),
        MON_ANIM_DATA(2, 0),
        MON_ANIM_DATA(2, 1),
        MON_ANIM_DATA(2, 2),
        MON_ANIM_DATA(2, 3),
    };

    PoketchTask_LoadPokemonIconLuminancePalette(0);
    PoketchAnimation_LoadSpriteFromNARC(&graphics->spriteData, NARC_INDEX_GRAPHIC__POKETCH, poke_icon_cell_NCER_lz, poke_icon_anim_NANR_lz, HEAP_ID_POKETCH_APP);

    static const PoketchAnimation_AnimationData ballAnimData[] = {
        BALL_ANIM_DATA(0, 0),
        BALL_ANIM_DATA(0, 1),
        BALL_ANIM_DATA(0, 2),
        BALL_ANIM_DATA(0, 3),
        BALL_ANIM_DATA(1, 0),
        BALL_ANIM_DATA(1, 1),
        BALL_ANIM_DATA(1, 2),
        BALL_ANIM_DATA(1, 3),
        BALL_ANIM_DATA(2, 0),
        BALL_ANIM_DATA(2, 1),
        BALL_ANIM_DATA(2, 2),
        BALL_ANIM_DATA(2, 3),
    };

    for (int i = 0; i < ROUTE_ENCOUNTERS_MAX_MONS; i++) {
        graphics->sprites[i] = PoketchAnimation_SetupNewAnimatedSprite(graphics->animMan, &animData[i], &graphics->spriteData);
        PoketchAnimation_SetSpriteCharNo(graphics->sprites[i], i * ICON_CHARS);
        PoketchAnimation_HideSprite(graphics->sprites[i], TRUE);

        graphics->balls[i] = PoketchAnimation_SetupNewAnimatedSprite(graphics->animMan, &ballAnimData[i], &graphics->spriteData);
        PoketchAnimation_SetSpriteCharNo(graphics->balls[i], BALL_CHAR);
        PoketchAnimation_SetCParam(graphics->balls[i], BALL_PALETTE);
        PoketchAnimation_HideSprite(graphics->balls[i], TRUE);
    }
}

static void UnloadSprites(RouteEncountersGraphics *graphics)
{
    for (int i = 0; i < ROUTE_ENCOUNTERS_MAX_MONS; i++) {
        if (graphics->sprites[i] != NULL) {
            PoketchAnimation_RemoveAnimatedSprite(graphics->animMan, graphics->sprites[i]);
            graphics->sprites[i] = NULL;
        }

        if (graphics->balls[i] != NULL) {
            PoketchAnimation_RemoveAnimatedSprite(graphics->animMan, graphics->balls[i]);
            graphics->balls[i] = NULL;
        }
    }

    PoketchAnimation_FreeSpriteData(&graphics->spriteData);
}

static int MethodTitle(const RouteEncountersView *view)
{
    switch (view->method) {
    case ROUTE_ENCOUNTER_METHOD_WALK:
        return view->isCave ? PoketchRouteEncounters_Text_Cave : PoketchRouteEncounters_Text_Grass;
    case ROUTE_ENCOUNTER_METHOD_SURF:
        return PoketchRouteEncounters_Text_Surf;
    case ROUTE_ENCOUNTER_METHOD_OLD_ROD:
        return PoketchRouteEncounters_Text_OldRod;
    case ROUTE_ENCOUNTER_METHOD_GOOD_ROD:
        return PoketchRouteEncounters_Text_GoodRod;
    case ROUTE_ENCOUNTER_METHOD_SUPER_ROD:
        return PoketchRouteEncounters_Text_SuperRod;
    default:
        return -1;
    }
}

static void GetPage(const RouteEncountersView *view, int *page, int *pages)
{
    *page = 0;
    *pages = 0;

    for (int method = 0; method < ROUTE_ENCOUNTER_METHOD_COUNT; method++) {
        if (view->methods & (1 << method)) {
            (*pages)++;

            if (method <= view->method) {
                *page = *pages;
            }
        }
    }
}

static void AddTextWindow(BgConfig *bgConfig, Window *window, const RouteEncountersView *view)
{
    // The title row while there are encounters, otherwise a centered message.
    if (view->methods != 0) {
        Window_Add(bgConfig, window, BG_LAYER, 2, 2, TEXT_WINDOW_WIDTH, TEXT_WINDOW_HEIGHT, 0, TITLE_BASE_TILE);
    } else {
        Window_Add(bgConfig, window, BG_LAYER, 2, 11, TEXT_WINDOW_WIDTH, TEXT_WINDOW_HEIGHT, 0, MESSAGE_BASE_TILE);
    }
}

static void PrintPage(Window *window, MessageLoader *loader, int page, int pages)
{
    String *text = String_Init(8, HEAP_ID_POKETCH_APP);
    String *number = String_Init(4, HEAP_ID_POKETCH_APP);
    String *separator = MessageLoader_GetNewString(loader, PoketchRouteEncounters_Text_PageSeparator);

    String_FormatInt(text, page, 1, PADDING_MODE_NONE, CHARSET_MODE_EN);
    String_Concat(text, separator);
    String_FormatInt(number, pages, 1, PADDING_MODE_NONE, CHARSET_MODE_EN);
    String_Concat(text, number);
    Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, text, TEXT_WINDOW_WIDTH * 8 - 4 - Font_CalcStringWidth(FONT_SYSTEM, text, 0), 0, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 8, 4), NULL);

    String_Free(separator);
    String_Free(number);
    String_Free(text);
}

// Sprite attributes reach the screen two frames after they change, the BG
// tilemap one frame after, and tiles at once. A refresh calls these on three
// consecutive frames so the whole new view shows on the same frame.
static void SetSpriteAttributes(RouteEncountersGraphics *graphics, const RouteEncountersView *view)
{
    for (u32 i = 0; i < ROUTE_ENCOUNTERS_MAX_MONS; i++) {
        if (i < view->list.count) {
            // Unseen species keep a slot in the icon tiles but draw a "?" on the BG.
            BOOL seen = view->list.mons[i].seen;
            u32 species = seen ? view->list.mons[i].species : SPECIES_NONE;

            graphics->iconIndices[i] = PokeIconSpriteIndex(species, FALSE, 0);
            PoketchAnimation_SetCParam(graphics->sprites[i], PokeIconPaletteIndex(species, 0, FALSE));
            PoketchAnimation_HideSprite(graphics->sprites[i], !seen);
            PoketchAnimation_HideSprite(graphics->balls[i], !view->list.mons[i].caught);
        } else {
            PoketchAnimation_HideSprite(graphics->sprites[i], TRUE);
            PoketchAnimation_HideSprite(graphics->balls[i], TRUE);
        }
    }
}

static void SetTilemap(RouteEncountersGraphics *graphics, const RouteEncountersView *view)
{
    BgConfig *bgConfig = graphics->bgConfig;
    Window window;

    Bg_FillTilemapRect(bgConfig, BG_LAYER, 0, 0, 0, POKETCH_WIDTH_TILES, POKETCH_HEIGHT_TILES, 0);

    AddTextWindow(bgConfig, &window, view);
    Window_PutToTilemap(&window);
    Window_Remove(&window);

    for (u32 i = 0; i < view->list.count; i++) {
        u32 column = i % ICON_COLUMNS;
        u32 row = i / ICON_COLUMNS;

        if (!view->list.mons[i].seen) {
            for (int y = 0; y < QUESTION_SIZE; y++) {
                for (int x = 0; x < QUESTION_SIZE; x++) {
                    Bg_FillTilemapRect(bgConfig, BG_LAYER, QUESTION_TILE + y * QUESTION_SIZE + x, ICON_TILE_X(column) + x, ICON_TILE_Y(row) + y, 1, 1, 0);
                }
            }
        }
    }

    Bg_CopyTilemapBufferToVRAM(bgConfig, BG_LAYER);
}

static void LoadTiles(RouteEncountersGraphics *graphics, const RouteEncountersView *view)
{
    MessageLoader *loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_POKETCH_ROUTE_ENCOUNTERS, HEAP_ID_POKETCH_APP);
    int messageID = view->methods != 0 ? MethodTitle(view) : PoketchRouteEncounters_Text_NoWildPokemon;
    Window window;

    AddTextWindow(graphics->bgConfig, &window, view);
    Window_FillTilemap(&window, 4);

    String *string = MessageLoader_GetNewString(loader, messageID);
    Text_AddPrinterWithParamsAndColor(&window, FONT_SYSTEM, string, (TEXT_WINDOW_WIDTH * 8 - Font_CalcStringWidth(FONT_SYSTEM, string, 0)) / 2, 0, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 8, 4), NULL);
    String_Free(string);

    // The page count only shows when there are methods to cycle through.
    int page, pages;
    GetPage(view, &page, &pages);

    if (pages > 1) {
        PrintPage(&window, loader, page, pages);
    }

    Window_LoadTiles(&window);
    Window_Remove(&window);
    MessageLoader_Free(loader);

    PoketchTask_LoadPokemonIcons(0, graphics->iconIndices, view->list.count, FALSE);
}
