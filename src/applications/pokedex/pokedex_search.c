#include "applications/pokedex/pokedex_search.h"

#include <nitro.h>
#include <string.h>

#include "applications/pokedex/infomain.h"
#include "applications/pokedex/ov21_021D4340.h"
#include "applications/pokedex/pokedex_app.h"
#include "applications/pokedex/pokedex_data_manager.h"
#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_graphics_manager.h"
#include "applications/pokedex/pokedex_main.h"
#include "applications/pokedex/pokedex_sort.h"
#include "applications/pokedex/pokedex_sort_data.h"
#include "applications/pokedex/pokedex_wide.h"
#include "applications/pokedex/struct_ov21_021D4660.h"
#include "applications/pokedex/struct_ov21_021D5B68.h"
#include "applications/pokedex/struct_ov21_021E68F4.h"

#include "constants/charcode.h"
#include "generated/pokemon_types.h"

#include "bg_window.h"
#include "brightness_controller.h"
#include "font.h"
#include "heap.h"
#include "narc.h"
#include "pokemon_sprite.h"
#include "sound_playback.h"
#include "sprite.h"
#include "sprite_resource.h"
#include "sprite_transfer.h"
#include "sprite_util.h"
#include "string_gf.h"
#include "text.h"
#include "system.h"

#include "res/graphics/pokedex/zukan.naix"
#include "res/text/bank/pokedex.h"

#define EXITSEARCH (1 << 1)

#ifndef SDK_BUILD_ARM
// In the new Pokédex the body shapes and the Poke Ball use the first palette; the
// type badges need OBJ palette slots too.
#define SEARCH_BUTTON_PALETTES (PokedexWide_IsEnabled() ? 1 : 12)
#else
#define SEARCH_BUTTON_PALETTES 12
#endif

enum ScreenState {
    SS_MENU,
    SS_SEARCH,
    SS_LOADING,
    SS_RETURN,
    SS_EXIT
};

typedef struct {
    int *exitFlag;
    PokedexSortData *unk_04;
    UnkStruct_ov21_021D5B68 *unk_08;
    enum FilterMethod filterMethod;
    enum SortOrder sortOrder;
    enum FilterName filterName;
    enum FilterType typeFilter1;
    enum FilterType typeFilter2;
    enum FilterForm filterForm;
    BOOL screenChange;
    BOOL applyFilter;
    BOOL noneFound;
    enum ScreenState screenState;
    int screenTimer;
#ifndef SDK_BUILD_ARM
    int selectedRow;
    int menuRevision;
#endif
} PokedexSearchSettings;

typedef struct {
    enum FilterMethod filterMethod;
    enum SortOrder sortOrder;
    enum FilterName filterName;
    enum FilterType typeFilter1;
    enum FilterType typeFilter2;
    enum FilterForm filterForm;
    BOOL noneFound;
    int noneFoundTimer;
#ifndef SDK_BUILD_ARM
    int menuRevision;
    // The widescreen menu (pokedex_wide.h), redrawn when drawnKey changes.
    PokedexCanvas *canvas;
    Window titleWindow;
    Window dexWindow;
    Window hintWindow;
    Window listWindow;
    Window infoWindow;
    SpriteResource *typeResources[4];
    Sprite *typeSprites[2];
    int width;
    int left;
    int listX;
    int listW;
    int rightX;
    int rightW;
    BOOL hasInfo;
    u32 drawnKey;
    int spin;
#endif
    Sprite *form;
    SpriteResource *searchGraphics[4];
    PokedexLoadingScreen loadingScreen;
    Sprite *pokeball;
    void *tileMap;
    NNSG2dScreenData *screenData;
} PokedexSearchDisplay;

static PokedexSearchSettings *InitSearchSettings(enum HeapID heapID, PokedexApp *param1);
static PokedexGraphicData **ov21_021D8724(enum HeapID heapID, PokedexApp *param1);
static UnkStruct_ov21_021D4660 *ov21_021D874C(enum HeapID heapID, PokedexApp *param1);
static void FreeSearchSettings(PokedexSearchSettings *searchSettings);
static void ov21_021D879C(PokedexGraphicData **param0);
static void ov21_021D87B0(UnkStruct_ov21_021D4660 *param0);
static int ov21_021D87C8(void);
static int ResetSearchSettings(PokedexDataManager *dataMan, void *data);
static int UpdateScreenState(PokedexDataManager *dataMan, void *data);
static int ov21_021D8928(PokedexDataManager *dataMan, void *data);
static int EnterPokedexSearch(void *graphics, PokedexGraphicsManager *graphicsMan, const void *data, const PokedexDataManager *dataMan);
static int UpdateDisplay(void *graphics, PokedexGraphicsManager *graphicsMan, const void *data, const PokedexDataManager *dataMan);
static int ExitPokedexSearch(void *graphics, PokedexGraphicsManager *graphicsMan, const void *data, const PokedexDataManager *dataMan);
static void LoadingScreenTransition(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID);
static void LoadingScreenAnimation(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID);
static void SearchingMessage(PokedexGraphicData **param0, enum HeapID heapID);
static void ReturnSearchDisplay(PokedexGraphicData **param0, PokedexSearchDisplay *searchDisplay, enum HeapID heapID);
static void DefaultDisplay(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, const int *param3, enum HeapID heapID);
static void ClearScreen(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1);
static void ov21_021D8B8C(PokedexGraphicData **param0, const PokedexSearchSettings *searchSettings, enum HeapID heapID);
static void GetSearchGraphics(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, enum HeapID heapID);
static void FreeSearchGraphics(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1);
static void InitCellActors(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID);
static void FreeCellActors(PokedexSearchDisplay *searchDisplay);
static void GetDisplayMap(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID, int param4);
static void FreeDisplayMap(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1);
static void UpdateSelectionDisplay(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID);
static void FilterMethodMap(PokedexGraphicData **param0, int filterMethod, enum HeapID heapID);
static void DescriptionMessage(PokedexGraphicData **param0, int filterMethod, enum HeapID heapID);
static void SortOrderMessage(PokedexGraphicData **param0, int sortOrder, enum HeapID heapID);
static void FilterNameMessage(PokedexGraphicData **param0, int filterName, enum HeapID heapID);
static void FilterType1Message(PokedexGraphicData **param0, int typeFilter1, enum HeapID heapID);
static void FilterType2Message(PokedexGraphicData **param0, int typeFilter2, enum HeapID heapID);
static void FilterTypeMessage(PokedexGraphicData **param0, int typeFilter, enum HeapID heapID, int y);
static void FilterFormSilhouette(PokedexSearchDisplay *searchDisplay, int filterForm);
static void EmptyDexMessage(PokedexGraphicData **param0, enum HeapID heapID);
static void ov21_021D8C00(PokedexGraphicData **param0);
#ifndef SDK_BUILD_ARM
static void HandleSearchMenuInput(PokedexSearchSettings *settings);
static void HandleClassicSearchMenuInput(PokedexSearchSettings *settings);
static void DrawClassicSearchMenu(PokedexSearchDisplay *display, PokedexGraphicData **graphics, const PokedexSearchSettings *settings, enum HeapID heapID);
static void SearchWide_Setup(PokedexSearchDisplay *display, PokedexGraphicData *graphicData, const PokedexSearchSettings *settings, enum HeapID heapID);
static void SearchWide_Teardown(PokedexSearchDisplay *display, PokedexGraphicData *graphicData);
static void SearchWide_Update(PokedexSearchDisplay *display, PokedexGraphicData *graphicData, const PokedexSearchSettings *settings, enum HeapID heapID);
static void SearchWide_StartFade(PokedexGraphicData *graphicData, BOOL fadeIn);
#endif

void PokedexSearch_TransitionFunctions(PokedexScreenManager *param0, PokedexApp *param1, enum HeapID heapID)
{
    PokedexSearchSettings *searchSettings = InitSearchSettings(heapID, param1);
    PokedexGraphicData **v1 = ov21_021D8724(heapID, param1);
    UnkStruct_ov21_021D4660 *v2 = ov21_021D874C(heapID, param1);

    param0->pageData = searchSettings;
    param0->pageGraphics = v1;
    param0->screenStates = v2;
    param0->numStates = ov21_021D87C8();

    param0->dataFunc[0] = ResetSearchSettings;
    param0->dataFunc[1] = UpdateScreenState;
    param0->dataFunc[2] = ov21_021D8928;
    param0->graphicsFunc[0] = EnterPokedexSearch;
    param0->graphicsFunc[1] = UpdateDisplay;
    param0->graphicsFunc[2] = ExitPokedexSearch;
}

void PokedexSearch_FreeData(PokedexScreenManager *param0)
{
    FreeSearchSettings(param0->pageData);
    ov21_021D879C(param0->pageGraphics);
    ov21_021D87B0(param0->screenStates);
}

void PokedexSearch_SetFilterMethod(PokedexScreenManager *param0, enum FilterMethod filterMethod)
{
    PokedexSearchSettings *searchSettings = param0->pageData;

    GF_ASSERT(filterMethod < MAX_FILTER_METHOD);
    searchSettings->filterMethod = filterMethod;
}

enum FilterMethod PokedexSearch_GetFilterMethod(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->filterMethod;
}

void PokedexSearch_SetSortOrder(PokedexScreenManager *param0, enum SortOrder sortOrder)
{
    PokedexSearchSettings *searchSettings = param0->pageData;

    GF_ASSERT(sortOrder < MAX_SORT_ORDER);
    searchSettings->sortOrder = sortOrder;
}

enum SortOrder PokedexSearch_GetSortOrder(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->sortOrder;
}

void PokedexSearch_SetFilterName(PokedexScreenManager *param0, enum FilterName filterName)
{
    PokedexSearchSettings *searchSettings = param0->pageData;

    GF_ASSERT(filterName < MAX_FILTER_NAME);
    searchSettings->filterName = filterName;
}

enum FilterName PokedexSearch_GetFilterName(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->filterName;
}

void PokedexSearch_SetFilterType(PokedexScreenManager *param0, enum FilterType filterType, int typeSlot)
{
    PokedexSearchSettings *searchSettings = param0->pageData;

    GF_ASSERT(filterType < MAX_FILTER_TYPE);

    if (typeSlot == 0) {
        searchSettings->typeFilter1 = filterType;
    } else {
        searchSettings->typeFilter2 = filterType;
    }
}

enum FilterType PokedexSearch_GetFilterType(const PokedexScreenManager *param0, int typeSlot)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;

    if (typeSlot == 0) {
        return searchSettings->typeFilter1;
    }

    return searchSettings->typeFilter2;
}

void PokedexSearch_SetFilterForm(PokedexScreenManager *param0, enum FilterForm filterForm)
{
    PokedexSearchSettings *searchSettings = param0->pageData;

    GF_ASSERT(filterForm < MAX_FILTER_FORM);
    searchSettings->filterForm = filterForm;
}

enum FilterForm PokedexSearch_GetFilterForm(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->filterForm;
}

void PokedexSearch_SetFilteredState(PokedexScreenManager *param0, BOOL applyFilter)
{
    PokedexSearchSettings *searchSettings = param0->pageData;

    searchSettings->screenChange = TRUE;
    searchSettings->applyFilter = applyFilter;
}

BOOL PokedexSearch_GetFilteredState(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->applyFilter;
}

int PokedexSearch_GetScreenState(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->screenState;
}

int PokedexSearch_GetScreenTimer(const PokedexScreenManager *param0)
{
    const PokedexSearchSettings *searchSettings = param0->pageData;
    return searchSettings->screenTimer;
}

static PokedexSearchSettings *InitSearchSettings(enum HeapID heapID, PokedexApp *param1)
{
    PokedexSearchSettings *searchSettings = Heap_Alloc(heapID, sizeof(PokedexSearchSettings));

    GF_ASSERT(searchSettings);
    memset(searchSettings, 0, sizeof(PokedexSearchSettings));

    searchSettings->exitFlag = ov21_021D138C(param1);
    searchSettings->unk_04 = PokedexMain_GetSortData(param1);
    PokedexScreenManager *v1 = ov21_021D1410(param1, 0);
    searchSettings->unk_08 = v1->pageData;

    return searchSettings;
}

static PokedexGraphicData **ov21_021D8724(enum HeapID heapID, PokedexApp *param1)
{
    PokedexGraphicData **v0 = Heap_Alloc(heapID, sizeof(PokedexGraphicData **));

    GF_ASSERT(v0);
    memset(v0, 0, sizeof(PokedexGraphicData **));

    *v0 = PokedexMain_GetGraphicData(param1);

    return v0;
}

static UnkStruct_ov21_021D4660 *ov21_021D874C(enum HeapID heapID, PokedexApp *param1)
{
    int v1 = ov21_021D87C8();

    UnkStruct_ov21_021D4660 *v0 = Heap_Alloc(heapID, sizeof(UnkStruct_ov21_021D4660) * v1);

    GF_ASSERT(v0);
    memset(v0, 0, sizeof(UnkStruct_ov21_021D4660) * v1);

    ov21_021D47F0(heapID, &v0[0], param1, (1 << 1));

    return v0;
}

static void FreeSearchSettings(PokedexSearchSettings *searchSettings)
{
    GF_ASSERT(searchSettings);
    Heap_Free(searchSettings);
}

static void ov21_021D879C(PokedexGraphicData **param0)
{
    GF_ASSERT(param0);
    Heap_Free(param0);
}

static void ov21_021D87B0(UnkStruct_ov21_021D4660 *param0)
{
    GF_ASSERT(param0);

    ov21_021D4660(&param0[0]);
    Heap_Free(param0);
}

static int ov21_021D87C8(void)
{
    return 1;
}

static int ResetSearchSettings(PokedexDataManager *dataMan, void *data)
{
    PokedexSearchSettings *searchSettings = data;
    int *v1 = Heap_Alloc(dataMan->heapID, sizeof(int));

    GF_ASSERT(v1);
    memset(v1, 0, sizeof(int));

    dataMan->pageData = v1;

    searchSettings->filterMethod = FM_ORDER;
    searchSettings->sortOrder = SO_NUMERICAL;
    searchSettings->filterName = FN_NONE;
    searchSettings->typeFilter1 = FT_NONE;
    searchSettings->typeFilter2 = FT_NONE;
    searchSettings->filterForm = FF_NONE;
    searchSettings->screenChange = FALSE;
    searchSettings->applyFilter = FALSE;
    searchSettings->screenState = SS_MENU;
#ifndef SDK_BUILD_ARM
    searchSettings->selectedRow = 0;
    searchSettings->menuRevision = 0;
#endif

    return 1;
}

static int UpdateScreenState(PokedexDataManager *dataMan, void *data)
{
    PokedexSearchSettings *searchSettings = data;

    if (dataMan->exit == 1) {
        return 1;
    }

    if (dataMan->unchanged == 1) {
        return 0;
    }

    switch (dataMan->state) {
    case 0:

        if (searchSettings->noneFound) {
            searchSettings->noneFound = FALSE;
        }

#ifndef SDK_BUILD_ARM
        if (PokedexWide_IsEnabled()) {
            HandleSearchMenuInput(searchSettings);
        } else {
            HandleClassicSearchMenuInput(searchSettings);
        }
#endif

        if (searchSettings->screenChange == TRUE) {
            if (searchSettings->applyFilter == FALSE) {
                *searchSettings->exitFlag |= EXITSEARCH;
                searchSettings->screenState = SS_EXIT;
            } else {
                searchSettings->screenState = SS_SEARCH;
                searchSettings->screenTimer = 4;
                dataMan->state++;
            }
        }
        break;
    case 1:
        searchSettings->screenTimer--;

        if (searchSettings->screenTimer < 0) {
            dataMan->state++;

            searchSettings->screenState = SS_LOADING;
            searchSettings->screenTimer = 32;

            Sound_PlayEffect(SEQ_SE_DP_Z_SEARCH_sseq);
        }
        break;
    case 2:
        searchSettings->screenTimer--;

        if (searchSettings->screenTimer < 0) {
            dataMan->state++;
        }
        break;
    case 3:
        BOOL dexExists = TRUE;
        dexExists = PokedexSort_Sort(searchSettings->unk_04, searchSettings->sortOrder, searchSettings->filterName, searchSettings->typeFilter1, searchSettings->typeFilter2, searchSettings->filterForm, PokedexSort_IsNationalDex(searchSettings->unk_04), dataMan->heapID, TRUE);

        if (dexExists == TRUE) {
            *searchSettings->exitFlag |= EXITSEARCH;
            PokedexSort_SetCurrentValues(searchSettings->unk_04, 0);
            searchSettings->unk_04->unk_1740 = 1;
            searchSettings->unk_08->unk_18 = 1;
        } else {
            searchSettings->applyFilter = FALSE;
            searchSettings->screenChange = FALSE;
            searchSettings->noneFound = TRUE;
            searchSettings->screenState = SS_RETURN;
            searchSettings->screenTimer = 4;

            dataMan->state++;
        }
        break;
    case 4:
        searchSettings->screenTimer--;

        if (searchSettings->screenTimer < 0) {
            searchSettings->screenState = SS_MENU;
            searchSettings->screenTimer = 0;
            dataMan->state = 0;
        }
        break;
    default:
        GF_ASSERT(FALSE);
        break;
    }

    return 0;
}

static int ov21_021D8928(PokedexDataManager *dataMan, void *data)
{
    int *v0 = dataMan->pageData;

    Heap_Free(v0);
    dataMan->pageData = NULL;

    return 1;
}

static int EnterPokedexSearch(void *graphics, PokedexGraphicsManager *graphicsMan, const void *data, const PokedexDataManager *dataMan)
{
    const PokedexSearchSettings *searchSettings = data;
    const int *v1 = dataMan->pageData;
    PokedexGraphicData **v2 = graphics;
    PokedexSearchDisplay *searchDisplay = graphicsMan->pageGraphics;

    switch (graphicsMan->state) {
    case 0:
        graphicsMan->pageGraphics = Heap_Alloc(graphicsMan->heapID, sizeof(PokedexSearchDisplay));
        memset(graphicsMan->pageGraphics, 0, sizeof(PokedexSearchDisplay));
        graphicsMan->state++;
        break;
    case 1:
        DefaultDisplay(searchDisplay, v2, searchSettings, v1, graphicsMan->heapID);
#ifndef SDK_BUILD_ARM
        // The grid left the screen black with master brightness (PokedexWide_FadeStep),
        // which a BLDCNT fade can't lift: fade in the same way the grid does.
        if (PokedexWide_IsEnabled()) {
            SearchWide_StartFade(*v2, TRUE);
            graphicsMan->state++;
            break;
        }
#endif
        BrightnessController_StartTransition(1, 0, -16, (GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ | GX_BLEND_PLANEMASK_BD), BRIGHTNESS_MAIN_SCREEN);
        graphicsMan->state++;
        break;
    case 2:
#ifndef SDK_BUILD_ARM
        if (PokedexWide_IsEnabled()) {
            if (PokedexGraphics_TakeBlendTransitionStep(&(*v2)->blendMain)) {
                graphicsMan->state++;
            }
            break;
        }
#endif
        if (BrightnessController_IsTransitionComplete(BRIGHTNESS_MAIN_SCREEN)) {
            graphicsMan->state++;
        }
        break;
    case 3:
        return 1;
    }

    return 0;
}

static int UpdateDisplay(void *graphics, PokedexGraphicsManager *graphicsMan, const void *data, const PokedexDataManager *dataMan)
{
    const PokedexSearchSettings *searchSettings = data;
    PokedexGraphicData **v2 = graphics;
    PokedexSearchDisplay *searchDisplay = graphicsMan->pageGraphics;

#ifndef SDK_BUILD_ARM
    if (PokedexWide_IsEnabled()) {
        SearchWide_Update(searchDisplay, *v2, searchSettings, graphicsMan->heapID);
        return 0;
    }
#endif

    if (searchSettings->screenState == SS_MENU) {
        UpdateSelectionDisplay(searchDisplay, v2, searchSettings, graphicsMan->heapID);
    } else if ((searchSettings->screenState == SS_SEARCH) || (searchSettings->screenState == SS_RETURN)) {
        LoadingScreenTransition(searchDisplay, v2, searchSettings, graphicsMan->heapID);
    } else if (searchSettings->screenState != SS_EXIT) {
        LoadingScreenAnimation(searchDisplay, v2, searchSettings, graphicsMan->heapID);
    }

    return 0;
}

static int ExitPokedexSearch(void *graphics, PokedexGraphicsManager *graphicsMan, const void *data, const PokedexDataManager *dataMan)
{
    const PokedexSearchSettings *searchSettings = data;
    PokedexGraphicData **v2 = graphics;
    PokedexSearchDisplay *searchDisplay = graphicsMan->pageGraphics;

    switch (graphicsMan->state) {
    case 0:
#ifndef SDK_BUILD_ARM
        // Both ways out fade to black; the grid fades back in from it.
        if (PokedexWide_IsEnabled()) {
            SearchWide_StartFade(*v2, FALSE);
            graphicsMan->state++;
            break;
        }
#endif
        if (searchSettings->applyFilter == FALSE) {
            BrightnessController_StartTransition(6, -16, 0, (GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ | GX_BLEND_PLANEMASK_BD), BRIGHTNESS_MAIN_SCREEN);
            graphicsMan->state++;
        } else {
            graphicsMan->state = 2;
        }
        break;
    case 1:
#ifndef SDK_BUILD_ARM
        if (PokedexWide_IsEnabled()) {
            SearchWide_Update(searchDisplay, *v2, searchSettings, graphicsMan->heapID);

            if (PokedexGraphics_TakeBlendTransitionStep(&(*v2)->blendMain)) {
                graphicsMan->state++;
            }
            break;
        }
#endif
        if (BrightnessController_IsTransitionComplete(BRIGHTNESS_MAIN_SCREEN)) {
            graphicsMan->state++;
        }
        break;
    case 2:
        ClearScreen(searchDisplay, v2);
        graphicsMan->state++;
        break;
    case 3:
        Heap_Free(graphicsMan->pageGraphics);
        graphicsMan->pageGraphics = NULL;
        graphicsMan->state++;
        break;
    case 4:
        return 1;
    }

    return 0;
}

static void DefaultDisplay(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, const int *param3, enum HeapID heapID)
{
#ifndef SDK_BUILD_ARM
    if (PokedexWide_IsEnabled()) {
        ov21_021D8C00(param1);
        GetSearchGraphics(searchDisplay, param1, heapID);
        InitCellActors(searchDisplay, param1, searchSettings, heapID);
        SearchWide_Setup(searchDisplay, *param1, searchSettings, heapID);
        return;
    }
#endif

    Bg_ClearTilemap((*param1)->bgConfig, 2);
    Bg_ClearTilemap((*param1)->bgConfig, 1);
    Window_FillTilemap(&(*param1)->window, 0);
    Window_SetPalette(&(*param1)->window, 7);

    ov21_021D8C00(param1);
    ov21_021D8B8C(param1, searchSettings, heapID);
    GetDisplayMap(searchDisplay, param1, searchSettings, heapID, PokedexSort_IsNationalDex(searchSettings->unk_04));
    GetSearchGraphics(searchDisplay, param1, heapID);
    InitCellActors(searchDisplay, param1, searchSettings, heapID);

#ifndef SDK_BUILD_ARM
    const u16 menuPalette[16] = {
        0, GX_RGB(6, 4, 6), GX_RGB(31, 31, 31), GX_RGB(17, 7, 10),
        GX_RGB(12, 4, 7), 0, GX_RGB(25, 12, 6), 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    GX_LoadBGPltt(menuPalette, 7 * 32, sizeof(menuPalette));
    Bg_ClearTilemap((*param1)->bgConfig, BG_LAYER_MAIN_3);
    VecFx32 formPosition = { 174 * FX32_ONE, 137 * FX32_ONE, 0 };
    VecFx32 formScale = { FX32_ONE / 2, FX32_ONE / 2, FX32_ONE };
    Sprite_SetPosition(searchDisplay->form, &formPosition);
    Sprite_SetExplicitPriority(searchDisplay->form, 0);
    Sprite_SetAffineScaleEx(searchDisplay->form, &formScale, AFFINE_OVERWRITE_MODE_NORMAL);
    DrawClassicSearchMenu(searchDisplay, param1, searchSettings, heapID);
#else
    DescriptionMessage(param1, searchSettings->filterMethod, heapID);
    FilterMethodMap(param1, searchSettings->filterMethod, heapID);
    SortOrderMessage(param1, searchSettings->sortOrder, heapID);
    FilterNameMessage(param1, searchSettings->filterName, heapID);
    FilterType1Message(param1, searchSettings->typeFilter1, heapID);
    FilterType2Message(param1, searchSettings->typeFilter2, heapID);
    FilterFormSilhouette(searchDisplay, searchSettings->filterForm);
#endif

    Bg_SetPriority(BG_LAYER_MAIN_1, 1);
    Bg_SetPriority(BG_LAYER_MAIN_2, 0);
}

static void ClearScreen(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1)
{
    FreeCellActors(searchDisplay);
    FreeSearchGraphics(searchDisplay, param1);

#ifndef SDK_BUILD_ARM
    if (PokedexWide_IsEnabled()) {
        SearchWide_Teardown(searchDisplay, *param1);
        return;
    }
#endif

    FreeDisplayMap(searchDisplay, param1);

    Bg_ClearTilemap((*param1)->bgConfig, 1);
    Window_FillTilemap(&(*param1)->window, 0);
    Window_SetPalette(&(*param1)->window, 0);
    Bg_SetPriority(BG_LAYER_MAIN_1, 0);
    Bg_SetPriority(BG_LAYER_MAIN_2, 1);
}

static void ov21_021D8B8C(PokedexGraphicData **param0, const PokedexSearchSettings *searchSettings, enum HeapID heapID)
{
    PokedexGraphics_LoadGraphicNarcCharacterData(*param0, scroll_main_background_NCGR_lz, (*param0)->bgConfig, 3, 0, 0, TRUE, heapID);
    PokedexGraphics_LoadGraphicNarcPaletteData(*param0, search_NCLR, 0, 0, 32, heapID);
}

static void GetDisplayMap(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID, int param4)
{
    int mapIndex;

    if (param4 == 1) {
        mapIndex = search_national_NSCR_lz;
    } else {
        mapIndex = search_sinnoh_NSCR_lz;
    }

    searchDisplay->tileMap = PokedexGraphics_GetGraphicNarcTilemapData(*param1, mapIndex, TRUE, &searchDisplay->screenData, heapID);
}

static void FreeDisplayMap(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1)
{
    Heap_Free(searchDisplay->tileMap);
    searchDisplay->tileMap = NULL;
    searchDisplay->screenData = NULL;
}

static void ov21_021D8C00(PokedexGraphicData **param0)
{
    PokemonSprite *v0 = PokemonGraphics_GetPokemonChar(*param0);

    PokemonSprite_SetAttribute(v0, MON_SPRITE_HIDE, 1);
    PokemonSprite_ClearFade(v0);
}

static void GetSearchGraphics(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, enum HeapID heapID)
{
    PokedexGraphicData *v0 = *param1;
    NARC *pokedexGraphics = PokedexGraphics_GetNARC(v0);

    searchDisplay->searchGraphics[SPRITE_RESOURCE_CHAR] = SpriteResourceCollection_AddTilesFrom(v0->spriteResourceCollection[SPRITE_RESOURCE_CHAR], pokedexGraphics, search_body_shapes_NCGR_lz, TRUE, search_body_shapes_NCGR_lz + 15000, NNS_G2D_VRAM_TYPE_2DMAIN, heapID);

    SpriteTransfer_RequestCharAtEnd(searchDisplay->searchGraphics[SPRITE_RESOURCE_CHAR]);
    SpriteResource_ReleaseData(searchDisplay->searchGraphics[SPRITE_RESOURCE_CHAR]);

    searchDisplay->searchGraphics[SPRITE_RESOURCE_PLTT] = SpriteResourceCollection_AddPaletteFrom(v0->spriteResourceCollection[SPRITE_RESOURCE_PLTT], pokedexGraphics, buttons_NCLR, FALSE, buttons_NCLR + 15000, NNS_G2D_VRAM_TYPE_2DMAIN, SEARCH_BUTTON_PALETTES, heapID);

    SpriteTransfer_RequestPlttFreeSpace(searchDisplay->searchGraphics[SPRITE_RESOURCE_PLTT]);
    SpriteResource_ReleaseData(searchDisplay->searchGraphics[SPRITE_RESOURCE_PLTT]);

    searchDisplay->searchGraphics[SPRITE_RESOURCE_CELL] = SpriteResourceCollection_AddFrom(v0->spriteResourceCollection[SPRITE_RESOURCE_CELL], pokedexGraphics, search_body_shapes_cell_NCER_lz, TRUE, search_body_shapes_cell_NCER_lz + 15000, SPRITE_RESOURCE_CELL, heapID);
    searchDisplay->searchGraphics[SPRITE_RESOURCE_ANIM] = SpriteResourceCollection_AddFrom(v0->spriteResourceCollection[SPRITE_RESOURCE_ANIM], pokedexGraphics, search_body_shapes_anim_NANR_lz, TRUE, search_body_shapes_anim_NANR_lz + 15000, SPRITE_RESOURCE_ANIM, heapID);
}

static void FreeSearchGraphics(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1)
{
    PokedexGraphicData *v0 = *param1;

    SpriteTransfer_ResetCharTransfer(searchDisplay->searchGraphics[SPRITE_RESOURCE_CHAR]);
    SpriteTransfer_ResetPlttTransfer(searchDisplay->searchGraphics[SPRITE_RESOURCE_PLTT]);

    SpriteResourceCollection_Remove(v0->spriteResourceCollection[SPRITE_RESOURCE_CHAR], searchDisplay->searchGraphics[SPRITE_RESOURCE_CHAR]);
    SpriteResourceCollection_Remove(v0->spriteResourceCollection[SPRITE_RESOURCE_PLTT], searchDisplay->searchGraphics[SPRITE_RESOURCE_PLTT]);
    SpriteResourceCollection_Remove(v0->spriteResourceCollection[SPRITE_RESOURCE_CELL], searchDisplay->searchGraphics[SPRITE_RESOURCE_CELL]);
    SpriteResourceCollection_Remove(v0->spriteResourceCollection[SPRITE_RESOURCE_ANIM], searchDisplay->searchGraphics[SPRITE_RESOURCE_ANIM]);
}

static void InitCellActors(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID)
{
    SpriteResourcesHeader v0;
    SpriteListTemplate v1;
    PokedexGraphicData *v2 = *param1;

    SpriteResourcesHeader_Init(&v0, search_body_shapes_NCGR_lz + 15000, buttons_NCLR + 15000, search_body_shapes_cell_NCER_lz + 15000, search_body_shapes_anim_NANR_lz + 15000, 0xffffffff, 0xffffffff, FALSE, 2, v2->spriteResourceCollection[SPRITE_RESOURCE_CHAR], v2->spriteResourceCollection[SPRITE_RESOURCE_PLTT], v2->spriteResourceCollection[SPRITE_RESOURCE_CELL], v2->spriteResourceCollection[SPRITE_RESOURCE_ANIM], NULL, NULL);

    v1.list = v2->spriteList;
    v1.resourceData = &v0;
    v1.priority = 31;
    v1.vramType = NNS_G2D_VRAM_TYPE_2DMAIN;
    v1.heapID = heapID;

    v1.position.x = 128 << FX32_SHIFT;
    v1.position.y = 164 << FX32_SHIFT;
    searchDisplay->form = SpriteList_Add(&v1);

    Sprite_SetAnim(searchDisplay->form, 3);

    v1.position.x = 128 << FX32_SHIFT;
    v1.position.y = 96 << FX32_SHIFT;

    searchDisplay->pokeball = SpriteList_Add(&v1);

    Sprite_SetAnim(searchDisplay->pokeball, 17);
    Sprite_SetDrawFlag(searchDisplay->pokeball, FALSE);
    Sprite_SetAffineOverwriteMode(searchDisplay->pokeball, AFFINE_OVERWRITE_MODE_NORMAL);
    Sprite_SetExplicitPriority(searchDisplay->pokeball, 0);
}

static void FreeCellActors(PokedexSearchDisplay *searchDisplay)
{
    Sprite_Delete(searchDisplay->form);
    Sprite_Delete(searchDisplay->pokeball);
}

static void DescriptionMessage(PokedexGraphicData **param0, int filterMethod, enum HeapID heapID)
{
    PokedexGraphicData *v0 = *param0;
    int entryID;

    Window_FillRectWithColor(&v0->window, 0, 24, 8, 208, 32);

    switch (filterMethod) {
    case FM_ORDER:
        entryID = pl_msg_pokedex_listing_description;
        break;
    case FM_NAME:
        entryID = pl_msg_pokedex_alphabetical_description;
        break;
    case FM_TYPE:
        entryID = pl_msg_pokedex_type_description;
        break;
    case FM_FORM:
        entryID = pl_msg_pokedex_body_description;
        break;
    default:
        break;
    }

    String *descriptionMessage = PokedexMain_GetMessage(entryID, heapID);

    u32 xOffset = 24 + (208 - Font_CalcMaxLineWidth(FONT_SYSTEM, descriptionMessage, 0)) / 2;
    u32 yOffset = 8 + (32 - String_NumLines(descriptionMessage) * 16) / 2;
    Text_AddPrinterWithParamsAndColor(&v0->window, FONT_SYSTEM, descriptionMessage, xOffset, yOffset, TEXT_SPEED_INSTANT, TEXT_COLOR(2, 1, 0), NULL);

    String_Free(descriptionMessage);
}

static void EmptyDexMessage(PokedexGraphicData **param0, enum HeapID heapID)
{
    PokedexGraphicData *v0 = *param0;

    Window_FillRectWithColor(&v0->window, 0, 24, 8, 208, 32);

    String *noneMessage = PokedexMain_GetMessage(pl_msg_pokedex_nonefound, heapID);

    {
        u32 xOffset = 24 + (208 - Font_CalcMaxLineWidth(FONT_SYSTEM, noneMessage, 0)) / 2;
        u32 yOffset = 8 + (32 - String_NumLines(noneMessage) * 16) / 2;
        Text_AddPrinterWithParamsAndColor(&v0->window, FONT_SYSTEM, noneMessage, xOffset, yOffset, TEXT_SPEED_INSTANT, TEXT_COLOR(2, 1, 0), NULL);
    }

    String_Free(noneMessage);
}

static void UpdateSelectionDisplay(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID)
{
#ifndef SDK_BUILD_ARM
    BOOL redraw = searchDisplay->menuRevision != searchSettings->menuRevision;
    if (searchSettings->noneFound && !searchDisplay->noneFound) {
        searchDisplay->noneFoundTimer = 64;
        redraw = TRUE;
    }
    searchDisplay->noneFound = searchSettings->noneFound;
    if (searchDisplay->noneFoundTimer > 0) {
        searchDisplay->noneFoundTimer--;
        if (searchDisplay->noneFoundTimer == 0) {
            redraw = TRUE;
        }
    }
    if (redraw) {
        DrawClassicSearchMenu(searchDisplay, param1, searchSettings, heapID);
    }
    return;
#endif
    if (searchSettings->noneFound != searchDisplay->noneFound) {
        searchDisplay->noneFound = searchSettings->noneFound;

        if (searchDisplay->noneFound == TRUE) {
            searchDisplay->noneFoundTimer = 64;
            EmptyDexMessage(param1, heapID);
        }
    }

    if (searchDisplay->noneFoundTimer > 0) {
        searchDisplay->noneFoundTimer--;
    } else if (searchDisplay->noneFoundTimer == 0) {
        DescriptionMessage(param1, searchSettings->filterMethod, heapID);
        searchDisplay->noneFoundTimer--;
    }

    if (searchDisplay->filterMethod != searchSettings->filterMethod) {
        FilterMethodMap(param1, searchSettings->filterMethod, heapID);
        DescriptionMessage(param1, searchSettings->filterMethod, heapID);
        searchDisplay->filterMethod = searchSettings->filterMethod;
    }

    if (searchDisplay->sortOrder != searchSettings->sortOrder) {
        SortOrderMessage(param1, searchSettings->sortOrder, heapID);
        searchDisplay->sortOrder = searchSettings->sortOrder;
    }

    if (searchDisplay->filterName != searchSettings->filterName) {
        FilterNameMessage(param1, searchSettings->filterName, heapID);
        searchDisplay->filterName = searchSettings->filterName;
    }

    if (searchDisplay->typeFilter1 != searchSettings->typeFilter1) {
        FilterType1Message(param1, searchSettings->typeFilter1, heapID);
        searchDisplay->typeFilter1 = searchSettings->typeFilter1;
    }

    if (searchDisplay->typeFilter2 != searchSettings->typeFilter2) {
        FilterType2Message(param1, searchSettings->typeFilter2, heapID);
        searchDisplay->typeFilter2 = searchSettings->typeFilter2;
    }

    if (searchDisplay->filterForm != searchSettings->filterForm) {
        FilterFormSilhouette(searchDisplay, searchSettings->filterForm);
        searchDisplay->filterForm = searchSettings->filterForm;
    }
}

static void FilterMethodMap(PokedexGraphicData **param0, int filterMethod, enum HeapID heapID)
{
    void *v0;
    NNSG2dScreenData *v1;
    int y;
    int mapIndex;

    v0 = PokedexGraphics_GetGraphicNarcTilemapData(*param0, search_filter_NSCR_lz, TRUE, &v1, heapID);

    Bg_LoadToTilemapRect((*param0)->bgConfig, 3, v1->rawData, 0, 0, v1->screenWidth / 8, v1->screenHeight / 8);
    Heap_Free(v0);

    switch (filterMethod) {
    case FM_ORDER:
        y = 6;
        mapIndex = search_filter_order_NSCR_lz;
        break;
    case FM_NAME:
        y = 9;
        mapIndex = search_filter_name_NSCR_lz;
        break;
    case FM_TYPE:
        y = 12;
        mapIndex = search_filter_type_NSCR_lz;
        break;
    case FM_FORM:
        y = 17;
        mapIndex = search_filter_form_NSCR_lz;
        break;
    default:
        break;
    }

    v0 = PokedexGraphics_GetGraphicNarcTilemapData(*param0, mapIndex, TRUE, &v1, heapID);

    Bg_LoadToTilemapRect((*param0)->bgConfig, 3, v1->rawData, 6, y, v1->screenWidth / 8, v1->screenHeight / 8);
    Heap_Free(v0);
    Bg_ScheduleTilemapTransfer((*param0)->bgConfig, 3);
}

static void SortOrderMessage(PokedexGraphicData **param0, int sortOrder, enum HeapID heapID)
{
    PokedexGraphicData *v0 = *param0;

    Window_FillRectWithColor(&v0->window, 0, 88, 52, 80, 16);

    int entryID = pl_msg_pokedex_numerical + sortOrder;
    String *sortMessage = PokedexMain_GetMessage(entryID, heapID);

    u32 xOffset = 88 + Font_CalcCenterAlignment(FONT_SYSTEM, sortMessage, 0, 80);
    Text_AddPrinterWithParamsAndColor(&v0->window, FONT_SYSTEM, sortMessage, xOffset, 52, TEXT_SPEED_INSTANT, TEXT_COLOR(4, 3, 0), NULL);

    String_Free(sortMessage);
}

static void FilterNameMessage(PokedexGraphicData **param0, int filterName, enum HeapID heapID)
{
    PokedexGraphicData *v0 = *param0;
    int entryID;

    Window_FillRectWithColor(&v0->window, 0, 88, 77, 80, 16);

    switch (filterName) {
    case FN_NONE:
        entryID = pl_msg_pokedex_nonealphabetical;
        break;
    case FN_ABC:
        entryID = pl_msg_pokedex_abc;
        break;
    case FN_DEF:
        entryID = pl_msg_pokedex_def;
        break;
    case FN_GHI:
        entryID = pl_msg_pokedex_ghi;
        break;
    case FN_JKL:
        entryID = pl_msg_pokedex_jkl;
        break;
    case FN_MNO:
        entryID = pl_msg_pokedex_mno;
        break;
    case FN_PQR:
        entryID = pl_msg_pokedex_pqr;
        break;
    case FN_STU:
        entryID = pl_msg_pokedex_stu;
        break;
    case FN_VWX:
        entryID = pl_msg_pokedex_vwx;
        break;
    case FN_YZ:
        entryID = pl_msg_pokedex_yz;
        break;
    }

    String *filterMessage = PokedexMain_GetMessage(entryID, heapID);

    {
        u32 xOffset = 88 + Font_CalcCenterAlignment(FONT_SYSTEM, filterMessage, 0, 80);
        Text_AddPrinterWithParamsAndColor(&v0->window, FONT_SYSTEM, filterMessage, xOffset, 77, TEXT_SPEED_INSTANT, TEXT_COLOR(4, 3, 0), NULL);
    }

    String_Free(filterMessage);
}

static void FilterTypeMessage(PokedexGraphicData **param0, int typeFilter, enum HeapID heapID, int y)
{
    PokedexGraphicData *v0 = *param0;
    int entryID;

    Window_FillRectWithColor(&v0->window, 0, 88, y, 80, 16);

    switch (typeFilter) {
    case FT_NONE:
        entryID = pl_msg_pokedex_dash;
        break;
    case FT_NORMAL:
        entryID = pl_msg_pokedex_normal;
        break;
    case FT_FIGHTING:
        entryID = pl_msg_pokedex_fight;
        break;
    case FT_FLYING:
        entryID = pl_msg_pokedex_flying;
        break;
    case FT_POISON:
        entryID = pl_msg_pokedex_poison;
        break;
    case FT_GROUND:
        entryID = pl_msg_pokedex_ground;
        break;
    case FT_ROCK:
        entryID = pl_msg_pokedex_rock;
        break;
    case FT_BUG:
        entryID = pl_msg_pokedex_bug;
        break;
    case FT_GHOST:
        entryID = pl_msg_pokedex_ghost;
        break;
    case FT_STEEL:
        entryID = pl_msg_pokedex_steel;
        break;
    case FT_FIRE:
        entryID = pl_msg_pokedex_fire;
        break;
    case FT_WATER:
        entryID = pl_msg_pokedex_water;
        break;
    case FT_GRASS:
        entryID = pl_msg_pokedex_grass;
        break;
    case FT_ELECTRIC:
        entryID = pl_msg_pokedex_electric;
        break;
    case FT_PSYCHIC:
        entryID = pl_msg_pokedex_psychic;
        break;
    case FT_ICE:
        entryID = pl_msg_pokedex_ice;
        break;
    case FT_DRAGON:
        entryID = pl_msg_pokedex_dragon;
        break;
    case FT_DARK:
        entryID = pl_msg_pokedex_dark;
        break;
    }

    String *filterMessage = PokedexMain_GetMessage(entryID, heapID);

    {
        u32 x = 88 + Font_CalcCenterAlignment(FONT_SYSTEM, filterMessage, 0, 80);
        Text_AddPrinterWithParamsAndColor(&v0->window, FONT_SYSTEM, filterMessage, x, y, TEXT_SPEED_INSTANT, TEXT_COLOR(4, 3, 0), NULL);
    }

    String_Free(filterMessage);
}

static void FilterType1Message(PokedexGraphicData **param0, int typeFilter1, enum HeapID heapID)
{
    FilterTypeMessage(param0, typeFilter1, heapID, 102);
}

static void FilterType2Message(PokedexGraphicData **param0, int typeFilter2, enum HeapID heapID)
{
    FilterTypeMessage(param0, typeFilter2, heapID, 120);
}

static void FilterFormSilhouette(PokedexSearchDisplay *searchDisplay, int filterForm)
{
    if (filterForm == FF_NONE) {
        Sprite_SetDrawFlag(searchDisplay->form, FALSE);
    } else {
        Sprite_SetDrawFlag(searchDisplay->form, TRUE);

        switch (filterForm) {
        case FF_QUADRUPED:
            Sprite_SetAnim(searchDisplay->form, 3);
            break;
        case FF_BIPEDALTAILLESS:
            Sprite_SetAnim(searchDisplay->form, 4);
            break;
        case FF_BIPEDALTAILED:
            Sprite_SetAnim(searchDisplay->form, 5);
            break;
        case FF_SERPENTINE:
            Sprite_SetAnim(searchDisplay->form, 6);
            break;
        case FF_MULTIWINGED:
            Sprite_SetAnim(searchDisplay->form, 7);
            break;
        case FF_WINGED:
            Sprite_SetAnim(searchDisplay->form, 8);
            break;
        case FF_INSECTOID:
            Sprite_SetAnim(searchDisplay->form, 9);
            break;
        case FF_HEADBASE:
            Sprite_SetAnim(searchDisplay->form, 10);
            break;
        case FF_HEADARMS:
            Sprite_SetAnim(searchDisplay->form, 11);
            break;
        case FF_HEADLEGS:
            Sprite_SetAnim(searchDisplay->form, 12);
            break;
        case FF_TENTACLES:
            Sprite_SetAnim(searchDisplay->form, 13);
            break;
        case FF_FINS:
            Sprite_SetAnim(searchDisplay->form, 14);
            break;
        case FF_HEAD:
            Sprite_SetAnim(searchDisplay->form, 15);
            break;
        case FF_MULTIBODY:
            Sprite_SetAnim(searchDisplay->form, 16);
            break;
        default:
            break;
        }
    }
}

static void LoadingScreenTransition(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID)
{
    if (searchSettings->screenTimer == 4) {
        if (searchSettings->screenState == SS_SEARCH) {
#ifndef SDK_BUILD_ARM
            Sprite_SetDrawFlag(searchDisplay->form, FALSE);
#endif
            PokedexMain_SetLoadingScreenParams(&searchDisplay->loadingScreen, (*param1)->bgConfig, 2, searchDisplay->screenData, 0, 12, 0, 20, 4);
        } else {
            ReturnSearchDisplay(param1, searchDisplay, heapID);

            PokedexMain_SetLoadingScreenParams(&searchDisplay->loadingScreen, (*param1)->bgConfig, 2, searchDisplay->screenData, 12, 0, 20, 0, 4);
        }
    }

    PokedexMain_LoadingScreenMove(&searchDisplay->loadingScreen);
}

static void LoadingScreenAnimation(PokedexSearchDisplay *searchDisplay, PokedexGraphicData **param1, const PokedexSearchSettings *searchSettings, enum HeapID heapID)
{
    if (searchSettings->screenTimer == 32) {
        Sprite_SetDrawFlag(searchDisplay->pokeball, TRUE);
        SearchingMessage(param1, heapID);
    } else if (searchSettings->screenTimer == 0) {
        Sprite_SetDrawFlag(searchDisplay->pokeball, FALSE);
        Window_FillTilemap(&(*param1)->window, 0);
        Window_ScheduleCopyToVRAM(&(*param1)->window);
    }

    int time = 4 - searchSettings->screenTimer;
    int angle = ((180 * 0xffff) / 360) * time;
    angle = angle / 4;

    Sprite_SetAffineZRotation(searchDisplay->pokeball, (u16)angle);
}

static void SearchingMessage(PokedexGraphicData **param0, enum HeapID heapID)
{
    PokedexGraphicData *v0 = *param0;

    Window_FillTilemap(&(*param0)->window, 0);

    String *searchingMessage = PokedexMain_GetMessage(pl_msg_pokedex_searching, heapID);

    u32 xOffset = (256 - Font_CalcMaxLineWidth(FONT_SYSTEM, searchingMessage, 0)) / 2;

    Text_AddPrinterWithParamsAndColor(&v0->window, FONT_SYSTEM, searchingMessage, xOffset, 128, TEXT_SPEED_INSTANT, TEXT_COLOR(2, 1, 0), NULL);

    String_Free(searchingMessage);
    Bg_SetPriority(BG_LAYER_MAIN_1, 0);
    Bg_SetPriority(BG_LAYER_MAIN_2, 1);
}

static void ReturnSearchDisplay(PokedexGraphicData **param0, PokedexSearchDisplay *searchDisplay, enum HeapID heapID)
{
    Bg_SetPriority(BG_LAYER_MAIN_1, 1);
    Bg_SetPriority(BG_LAYER_MAIN_2, 0);
    Window_FillTilemap(&(*param0)->window, 0);
#ifndef SDK_BUILD_ARM
    searchDisplay->menuRevision = -1;
    return;
#endif

    FilterMethodMap(param0, searchDisplay->filterMethod, heapID);
    DescriptionMessage(param0, searchDisplay->filterMethod, heapID);
    SortOrderMessage(param0, searchDisplay->sortOrder, heapID);
    FilterNameMessage(param0, searchDisplay->filterName, heapID);
    FilterType1Message(param0, searchDisplay->typeFilter1, heapID);
    FilterType2Message(param0, searchDisplay->typeFilter2, heapID);
    FilterFormSilhouette(searchDisplay, searchDisplay->filterForm);
}

#ifndef SDK_BUILD_ARM
// The classic PC search menu, for when the new Pokédex is off.
static void HandleClassicSearchMenuInput(PokedexSearchSettings *settings)
{
    if (settings->screenChange) {
        return;
    }
    if (gSystem.pressedKeys & PAD_BUTTON_B) {
        settings->screenChange = TRUE;
        settings->applyFilter = FALSE;
        Sound_PlayEffect(SEQ_SE_DP_DECIDE_sseq);
        return;
    }
    if (gSystem.pressedKeys & (PAD_KEY_UP | PAD_KEY_DOWN)) {
        int direction = (gSystem.pressedKeys & PAD_KEY_UP) ? -1 : 1;
        settings->selectedRow = (settings->selectedRow + direction + 6) % 6;
        settings->menuRevision++;
        Sound_PlayEffect(SEQ_SE_DP_DENSI06_sseq);
        return;
    }
    if (!(gSystem.pressedKeys & PAD_BUTTON_A)) {
        return;
    }
    switch (settings->selectedRow) {
    case 0:
        // Highest stats belongs to the new Pokédex's search: the classic screens have no stats to show for it.
        settings->sortOrder = (settings->sortOrder + 1) % SO_HIGHEST_STATS;
        break;
    case 1:
        settings->filterName = (settings->filterName + 1) % MAX_FILTER_NAME;
        break;
    case 2:
        settings->typeFilter1 = (settings->typeFilter1 + 1) % MAX_FILTER_TYPE;
        break;
    case 3:
        settings->typeFilter2 = (settings->typeFilter2 + 1) % MAX_FILTER_TYPE;
        break;
    case 4:
        settings->filterForm = (settings->filterForm + 1) % MAX_FILTER_FORM;
        break;
    case 5:
        settings->screenChange = TRUE;
        settings->applyFilter = TRUE;
        break;
    }
    settings->menuRevision++;
    Sound_PlayEffect(SEQ_SE_DP_DECIDE_sseq);
}

static void DrawClassicSearchMenu(PokedexSearchDisplay *display, PokedexGraphicData **graphics, const PokedexSearchSettings *settings, enum HeapID heapID)
{
    const int labelMessages[] = { pl_msg_pokedex_order, pl_msg_pokedex_name, pl_msg_pokedex_type, pl_msg_pokedex_type, pl_msg_pokedex_form, pl_msg_pokedex_ok };
    const int typeMessages[MAX_FILTER_TYPE] = {
        pl_msg_pokedex_none, pl_msg_pokedex_normal, pl_msg_pokedex_fight,
        pl_msg_pokedex_flying, pl_msg_pokedex_poison, pl_msg_pokedex_ground,
        pl_msg_pokedex_rock, pl_msg_pokedex_bug, pl_msg_pokedex_ghost,
        pl_msg_pokedex_steel, pl_msg_pokedex_fire, pl_msg_pokedex_water,
        pl_msg_pokedex_grass, pl_msg_pokedex_electric, pl_msg_pokedex_psychic,
        pl_msg_pokedex_ice, pl_msg_pokedex_dragon, pl_msg_pokedex_dark
    };
    const int valueMessages[] = {
        pl_msg_pokedex_numerical + settings->sortOrder,
        settings->filterName == FN_NONE ? pl_msg_pokedex_none : pl_msg_pokedex_abc + settings->filterName - 1,
        typeMessages[settings->typeFilter1], typeMessages[settings->typeFilter2],
        pl_msg_pokedex_none
    };
    Window *window = &(*graphics)->window;
    Window_FillTilemap(window, 4);

    String *heading = PokedexMain_GetMessage(display->noneFoundTimer > 0 ? pl_msg_pokedex_nonefound : pl_msg_pokedex_search, heapID);
    String *firstLine = String_Init(128, heapID);
    if (display->noneFoundTimer > 0) {
        String_Copy(firstLine, heading);
    } else {
        String_CopyLineNum(firstLine, heading, 0);
    }
    int headingX = (256 - (int)Font_CalcMaxLineWidth(FONT_SYSTEM, firstLine, 0)) / 2;
    Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, firstLine, headingX, 4, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(2, 1, 4), NULL);
    String_Free(firstLine);
    String_Free(heading);

    for (int row = 0; row < 6; row++) {
        int y = row == 5 ? 162 : 32 + row * 24;
        int background = row == settings->selectedRow ? 6 : 3;
        Window_FillRectWithColor(window, background, 12, y - 2, 232, 21);
        String *label = PokedexMain_GetMessage(labelMessages[row], heapID);
        if (row == 2 || row == 3) {
            // Give each independent type slot a visible number.
            String *numbered = String_Init(32, heapID);
            String_Copy(numbered, label);
            String_AppendChar(numbered, CHAR_SPACE);
            String_AppendChar(numbered, row == 2 ? CHAR_1 : CHAR_2);
            String_Free(label);
            label = numbered;
        }
        int x = row == 5 ? 12 + Font_CalcCenterAlignment(FONT_SYSTEM, label, 0, 232) : 24;
        Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, label, x, y, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(2, 1, background), NULL);
        String_Free(label);
        if (row < 5 && (row != 4 || settings->filterForm == FF_NONE)) {
            String *value = PokedexMain_GetMessage(valueMessages[row], heapID);
            Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, value, 112, y, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(2, 1, background), NULL);
            String_Free(value);
        }
    }
    FilterFormSilhouette(display, settings->filterForm);
    display->menuRevision = settings->menuRevision;
    Window_ScheduleCopyToVRAM(window);
}

// The PC search menu: the grid's header over a list of the search settings, and on
// wide views a panel describing the selected setting with the controls. Laid out in
// view coordinates like the other widescreen Pokédex screens (pokedex_wide.h).

#define SW_HEADER_H     16
#define SW_PANEL_TOP    20
#define SW_PANEL_BOTTOM 190
#define SW_ROW_COUNT    6 // order, name, type 1, type 2, form, start
#define SW_ROW_SEARCH   5
#define SW_ROW_FORM     4
#define SW_ROW_TOP      26
#define SW_ROW_STEP     24
#define SW_ROW_H        21
#define SW_FORM_ROW_H   37
#define SW_SEARCH_Y     165
#define SW_LABEL_W      64
#define SW_HINT_TOP     126
#define SW_HINT_STEP    15
#define SW_INFO_MIN_W   368

#define SW_TYPE_ICON_RESOURCE_ID 9400
#define SW_TYPE_ICON_PALETTES    4

#define SW_TEXT_DARK   TEXT_COLOR(PDW_TEXT, PDW_TEXT_SHADOW, 0)
#define SW_TEXT_LIGHT  TEXT_COLOR(PDW_WHITE, PDW_RED_DARK, 0)
#define SW_TEXT_HEADER TEXT_COLOR(PDW_TEXT, PDW_CREAM, 0)
#define SW_TEXT_KEY    TEXT_COLOR(PDW_WHITE, PDW_DARK, 0)

static const u16 sSearchRowLabels[SW_ROW_COUNT] = {
    pl_msg_pokedex_order,
    pl_msg_pokedex_name,
    pl_msg_pokedex_wide_search_type1,
    pl_msg_pokedex_wide_search_type2,
    pl_msg_pokedex_form,
    pl_msg_pokedex_wide_search_start,
};

static const u16 sSearchRowInfo[SW_ROW_COUNT] = {
    pl_msg_pokedex_wide_search_info_order,
    pl_msg_pokedex_wide_search_info_name,
    pl_msg_pokedex_wide_search_info_type,
    pl_msg_pokedex_wide_search_info_type,
    pl_msg_pokedex_wide_search_info_form,
    pl_msg_pokedex_wide_search_info_start,
};

// enum FilterType to the Pokémon type its badge shows.
static const u8 sFilterTypes[MAX_FILTER_TYPE] = {
    [FT_NONE] = TYPE_NORMAL,
    [FT_NORMAL] = TYPE_NORMAL,
    [FT_FIGHTING] = TYPE_FIGHTING,
    [FT_FLYING] = TYPE_FLYING,
    [FT_POISON] = TYPE_POISON,
    [FT_GROUND] = TYPE_GROUND,
    [FT_ROCK] = TYPE_ROCK,
    [FT_BUG] = TYPE_BUG,
    [FT_GHOST] = TYPE_GHOST,
    [FT_STEEL] = TYPE_STEEL,
    [FT_FIRE] = TYPE_FIRE,
    [FT_WATER] = TYPE_WATER,
    [FT_GRASS] = TYPE_GRASS,
    [FT_ELECTRIC] = TYPE_ELECTRIC,
    [FT_PSYCHIC] = TYPE_PSYCHIC,
    [FT_ICE] = TYPE_ICE,
    [FT_DRAGON] = TYPE_DRAGON,
    [FT_DARK] = TYPE_DARK,
};

static int CycleValue(int value, int direction, int count)
{
    return (value + direction + count) % count;
}

// Steps the setting on a row; returns FALSE for the start row.
static BOOL ChangeSetting(PokedexSearchSettings *settings, int row, int direction)
{
    switch (row) {
    case 0:
        settings->sortOrder = CycleValue(settings->sortOrder, direction, MAX_SORT_ORDER);
        break;
    case 1:
        settings->filterName = CycleValue(settings->filterName, direction, MAX_FILTER_NAME);
        break;
    case 2:
        settings->typeFilter1 = CycleValue(settings->typeFilter1, direction, MAX_FILTER_TYPE);
        break;
    case 3:
        settings->typeFilter2 = CycleValue(settings->typeFilter2, direction, MAX_FILTER_TYPE);
        break;
    case SW_ROW_FORM:
        settings->filterForm = CycleValue(settings->filterForm, direction, MAX_FILTER_FORM);
        break;
    default:
        return FALSE;
    }

    return TRUE;
}

static void StartSearch(PokedexSearchSettings *settings)
{
    settings->screenChange = TRUE;
    settings->applyFilter = TRUE;
    Sound_PlayEffect(SEQ_SE_DP_DECIDE_sseq);
}

// Up/Down pick a row, Left/Right (or A) change its setting, A on the last row or
// Start searches, B goes back to the grid.
static void HandleSearchMenuInput(PokedexSearchSettings *settings)
{
    if (settings->screenChange) {
        return;
    }

    if (gSystem.pressedKeys & PAD_BUTTON_B) {
        settings->screenChange = TRUE;
        settings->applyFilter = FALSE;
        Sound_PlayEffect(SEQ_SE_DP_DECIDE_sseq);
        return;
    }

    if (gSystem.pressedKeys & PAD_BUTTON_START) {
        StartSearch(settings);
        return;
    }

    if (JOY_REPEAT(PAD_KEY_UP) || JOY_REPEAT(PAD_KEY_DOWN)) {
        const int direction = JOY_REPEAT(PAD_KEY_UP) ? -1 : 1;

        settings->selectedRow = CycleValue(settings->selectedRow, direction, SW_ROW_COUNT);
        Sound_PlayEffect(SEQ_SE_DP_DENSI06_sseq);
        return;
    }

    if (JOY_REPEAT(PAD_KEY_LEFT) || JOY_REPEAT(PAD_KEY_RIGHT)) {
        if (ChangeSetting(settings, settings->selectedRow, JOY_REPEAT(PAD_KEY_LEFT) ? -1 : 1)) {
            Sound_PlayEffect(SEQ_SE_DP_DENSI06_sseq);
        }

        return;
    }

    if (gSystem.pressedKeys & PAD_BUTTON_A) {
        if (ChangeSetting(settings, settings->selectedRow, 1)) {
            Sound_PlayEffect(SEQ_SE_DP_DECIDE_sseq);
        } else {
            StartSearch(settings);
        }
    }
}

static int RowY(int row)
{
    return row == SW_ROW_SEARCH ? SW_SEARCH_Y : SW_ROW_TOP + row * SW_ROW_STEP;
}

static int RowHeight(int row)
{
    return row == SW_ROW_FORM ? SW_FORM_ROW_H : SW_ROW_H;
}

static int RowX(const PokedexSearchDisplay *display)
{
    return display->listX + 5;
}

static int RowW(const PokedexSearchDisplay *display)
{
    return display->listW - 10;
}

// The center of a row's value area, right of its label.
static int ValueCenterX(const PokedexSearchDisplay *display)
{
    const int valueX = RowX(display) + 1 + SW_LABEL_W + 1;

    return valueX + (RowX(display) + RowW(display) - 1 - valueX) / 2;
}

// Prints at view coordinates within a window; align: 0 left, 1 right, 2 center.
static void PrintAt(Window *window, const String *string, int x, int y, int align, TextColor color)
{
    const int width = Font_CalcStringWidth(FONT_SYSTEM, string, 0);

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
        Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, string, x, y, TEXT_SPEED_NO_TRANSFER, color, NULL);
    }
}

static void PrintMessage(Window *window, int entryID, int x, int y, int align, TextColor color, enum HeapID heapID)
{
    String *string = PokedexMain_GetMessage(entryID, heapID);

    PrintAt(window, string, x, y, align, color);
    String_Free(string);
}

// A 4x7 arrowhead pointing left (direction < 0) or right, its tip at x.
static void DrawArrow(PokedexCanvas *canvas, int x, int centerY, int direction, u8 color)
{
    for (int i = 0; i < 4; i++) {
        PokedexCanvas_Fill(canvas, x - (direction < 0 ? -i : i), centerY - i, 1, i * 2 + 1, color);
    }
}

static void LoadTypeBadges(PokedexSearchDisplay *display, PokedexGraphicData *graphicData, enum HeapID heapID)
{
    NARC *narc = PokedexGraphics_GetNARC(graphicData);

    display->typeResources[SPRITE_RESOURCE_CHAR] = SpriteResourceCollection_AddTilesFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_CHAR], narc, type_icons_NCGR_lz, TRUE, SW_TYPE_ICON_RESOURCE_ID, NNS_G2D_VRAM_TYPE_2DMAIN, heapID);
    SpriteTransfer_RequestCharAtEnd(display->typeResources[SPRITE_RESOURCE_CHAR]);
    SpriteResource_ReleaseData(display->typeResources[SPRITE_RESOURCE_CHAR]);

    display->typeResources[SPRITE_RESOURCE_PLTT] = SpriteResourceCollection_AddPaletteFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], narc, type_icons_NCLR, FALSE, SW_TYPE_ICON_RESOURCE_ID, NNS_G2D_VRAM_TYPE_2DMAIN, SW_TYPE_ICON_PALETTES, heapID);
    SpriteTransfer_RequestPlttFreeSpace(display->typeResources[SPRITE_RESOURCE_PLTT]);
    SpriteResource_ReleaseData(display->typeResources[SPRITE_RESOURCE_PLTT]);

    display->typeResources[SPRITE_RESOURCE_CELL] = SpriteResourceCollection_AddFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_CELL], narc, type_icons_cell_NCER_lz, TRUE, SW_TYPE_ICON_RESOURCE_ID, SPRITE_RESOURCE_CELL, heapID);
    display->typeResources[SPRITE_RESOURCE_ANIM] = SpriteResourceCollection_AddFrom(graphicData->spriteResourceCollection[SPRITE_RESOURCE_ANIM], narc, type_icons_anim_NANR_lz, TRUE, SW_TYPE_ICON_RESOURCE_ID, SPRITE_RESOURCE_ANIM, heapID);

    SpriteResourcesHeader header;
    SpriteListTemplate template;

    SpriteResourcesHeader_Init(&header, SW_TYPE_ICON_RESOURCE_ID, SW_TYPE_ICON_RESOURCE_ID, SW_TYPE_ICON_RESOURCE_ID, SW_TYPE_ICON_RESOURCE_ID, 0xffffffff, 0xffffffff, FALSE, 0, graphicData->spriteResourceCollection[SPRITE_RESOURCE_CHAR], graphicData->spriteResourceCollection[SPRITE_RESOURCE_PLTT], graphicData->spriteResourceCollection[SPRITE_RESOURCE_CELL], graphicData->spriteResourceCollection[SPRITE_RESOURCE_ANIM], NULL, NULL);

    template.list = graphicData->spriteList;
    template.resourceData = &header;
    template.priority = 32;
    template.vramType = NNS_G2D_VRAM_TYPE_2DMAIN;
    template.heapID = heapID;
    template.position.x = 0;
    template.position.y = 0;
    template.position.z = 0;

    for (int i = 0; i < 2; i++) {
        display->typeSprites[i] = SpriteList_Add(&template);
        Sprite_SetDrawFlag(display->typeSprites[i], FALSE);
    }
}

static void FreeTypeBadges(PokedexSearchDisplay *display, PokedexGraphicData *graphicData)
{
    for (int i = 0; i < 2; i++) {
        Sprite_Delete(display->typeSprites[i]);
    }

    SpriteTransfer_ResetCharTransfer(display->typeResources[SPRITE_RESOURCE_CHAR]);
    SpriteTransfer_ResetPlttTransfer(display->typeResources[SPRITE_RESOURCE_PLTT]);

    for (int i = 0; i < 4; i++) {
        SpriteResourceCollection_Remove(graphicData->spriteResourceCollection[i], display->typeResources[i]);
    }
}

// Places a sprite by its center in view coordinates.
static void PlaceSprite(const PokedexSearchDisplay *display, Sprite *sprite, int x, int y)
{
    VecFx32 position = { (x - display->left) * FX32_ONE, y * FX32_ONE, 0 };

    Sprite_SetPosition(sprite, &position);
}

static BOOL IsSearching(const PokedexSearchSettings *settings)
{
    return settings->screenState == SS_SEARCH || settings->screenState == SS_LOADING;
}

static u32 DisplayKey(const PokedexSearchDisplay *display, const PokedexSearchSettings *settings)
{
    return settings->sortOrder
        | (settings->filterName << 3)
        | (settings->typeFilter1 << 7)
        | (settings->typeFilter2 << 12)
        | (settings->filterForm << 17)
        | (settings->selectedRow << 21)
        | ((display->noneFoundTimer > 0) << 24)
        | (IsSearching(settings) << 25);
}

static void DrawHeader(PokedexSearchDisplay *display, const PokedexSearchSettings *settings, enum HeapID heapID)
{
    PokedexCanvas *canvas = display->canvas;

    // The grid's header band and title pill.
    PokedexCanvas_Fill(canvas, 0, 0, canvas->width, SW_HEADER_H - 1, PDW_GOLD);
    PokedexCanvas_Fill(canvas, 0, SW_HEADER_H - 1, canvas->width, 1, PDW_GOLD_DARK);
    PokedexCanvas_Panel(canvas, 4, 1, 116, 13, PDW_RED, PDW_RED_DARK);
    PokedexCanvas_Fill(canvas, 6, 2, 112, 1, PDW_WHITE);

    Window_FillTilemap(&display->titleWindow, 0);
    String *title = PokedexMain_GetMessage(pl_msg_pokedex_wide_title_search, heapID);
    Text_AddPrinterWithParamsAndColor(&display->titleWindow, FONT_SYSTEM, title, (112 - Font_CalcStringWidth(FONT_SYSTEM, title, 0)) / 2, 0, TEXT_SPEED_NO_TRANSFER, SW_TEXT_LIGHT, NULL);
    String_Free(title);

    Window_FillTilemap(&display->dexWindow, 0);
    String *dexName = PokedexMain_GetMessage(PokedexSort_IsNationalDex(settings->unk_04) ? pl_msg_pokedex_wide_national : pl_msg_pokedex_wide_sinnoh, heapID);
    Text_AddPrinterWithParamsAndColor(&display->dexWindow, FONT_SYSTEM, dexName, display->dexWindow.width * 8 - 4 - Font_CalcStringWidth(FONT_SYSTEM, dexName, 0), 0, TEXT_SPEED_NO_TRANSFER, SW_TEXT_HEADER, NULL);
    String_Free(dexName);
}

static void DrawRow(PokedexSearchDisplay *display, const PokedexSearchSettings *settings, int row, enum HeapID heapID)
{
    PokedexCanvas *canvas = display->canvas;
    Window *window = &display->listWindow;
    const BOOL selected = row == settings->selectedRow;
    const int x = RowX(display);
    const int y = RowY(row);
    const int w = RowW(display);
    const int h = RowHeight(row);
    const int textY = y + (h - 16) / 2 + 1;
    const int centerX = ValueCenterX(display);
    const u8 border = selected ? PDW_RED_DARK : PDW_GOLD_DARK;

    if (selected) {
        // The grid's frame around the selected card.
        PokedexCanvas_Fill(canvas, x - 1, y + 1, 1, h - 2, PDW_RED_DARK);
        PokedexCanvas_Fill(canvas, x + w, y + 1, 1, h - 2, PDW_RED_DARK);
        PokedexCanvas_Fill(canvas, x + 1, y - 1, w - 2, 1, PDW_RED_DARK);
        PokedexCanvas_Fill(canvas, x + 1, y + h, w - 2, 1, PDW_RED_DARK);
    }

    if (row == SW_ROW_SEARCH) {
        PokedexCanvas_Panel(canvas, x, y, w, h, selected ? PDW_ORANGE : PDW_RED, PDW_RED_DARK);
        PokedexCanvas_Fill(canvas, x + 2, y + 1, w - 4, 1, PDW_WHITE);

        int entryID = pl_msg_pokedex_wide_search_start;

        if (IsSearching(settings)) {
            entryID = pl_msg_pokedex_searching;
        } else if (display->noneFoundTimer > 0) {
            entryID = pl_msg_pokedex_nonefound;
        }

        PrintMessage(window, entryID, x + w / 2, textY, 2, SW_TEXT_LIGHT, heapID);
        return;
    }

    PokedexCanvas_Panel(canvas, x, y, w, h, PDW_CREAM_LIGHT, border);
    PokedexCanvas_Fill(canvas, x + 1, y + 1, SW_LABEL_W, h - 2, selected ? PDW_ORANGE : PDW_GOLD);
    PokedexCanvas_Fill(canvas, x + 1 + SW_LABEL_W, y + 1, 1, h - 2, border);
    PrintMessage(window, sSearchRowLabels[row], x + 6, textY, 0, SW_TEXT_HEADER, heapID);

    if (selected) {
        const int arrowY = y + h / 2;

        DrawArrow(canvas, x + SW_LABEL_W + 6, arrowY, -1, PDW_RED_DARK);
        DrawArrow(canvas, x + w - 6, arrowY, 1, PDW_RED_DARK);
    }

    int valueID = -1;

    switch (row) {
    case 0:
        valueID = pl_msg_pokedex_numerical + settings->sortOrder;
        break;
    case 1:
        valueID = settings->filterName == FN_NONE ? pl_msg_pokedex_none : pl_msg_pokedex_abc + settings->filterName - 1;
        break;
    case 2:
    case 3: {
        const int filterType = row == 2 ? settings->typeFilter1 : settings->typeFilter2;
        Sprite *badge = display->typeSprites[row - 2];

        if (filterType == FT_NONE) {
            valueID = pl_msg_pokedex_none;
            Sprite_SetDrawFlag(badge, FALSE);
        } else {
            PlaceSprite(display, badge, centerX, y + h / 2);
            Sprite_SetAnim(badge, PokedexGraphics_GetAnimIDfromType(sFilterTypes[filterType]));
            Sprite_SetDrawFlag(badge, TRUE);
        }
        break;
    }
    case SW_ROW_FORM:
        if (settings->filterForm == FF_NONE) {
            valueID = pl_msg_pokedex_none;
        }

        PlaceSprite(display, display->form, centerX, y + h / 2);
        FilterFormSilhouette(display, settings->filterForm);
        break;
    }

    if (valueID >= 0) {
        PrintMessage(window, valueID, centerX, textY, 2, SW_TEXT_DARK, heapID);
    }
}

// A key cap: dark with a white letter, or white arrowheads (arrows != 0).
static int DrawKeyCap(PokedexSearchDisplay *display, int x, int y, int keyID, int arrow, enum HeapID heapID)
{
    int width = 13;
    String *key = NULL;

    if (keyID >= 0) {
        key = PokedexMain_GetMessage(keyID, heapID);
        width = Font_CalcStringWidth(FONT_SYSTEM, key, 0) + 6;
        width = width < 13 ? 13 : width;
    }

    PokedexCanvas_Panel(display->canvas, x, y, width, 13, PDW_DARK, PDW_DARK);

    if (key) {
        PrintAt(&display->hintWindow, key, x + width / 2 + 1, y - 2, 2, SW_TEXT_KEY);
        String_Free(key);
    } else {
        DrawArrow(display->canvas, arrow < 0 ? x + 4 : x + 8, y + 6, arrow, PDW_WHITE);
    }

    return width;
}

static void DrawInfoPanel(PokedexSearchDisplay *display, const PokedexSearchSettings *settings, enum HeapID heapID)
{
    PokedexCanvas *canvas = display->canvas;
    const int x = display->rightX;
    const int w = display->rightW;

    // The entry pages' panel: a red title strip over the body.
    PokedexCanvas_Panel(canvas, x, SW_PANEL_TOP, w, SW_HEADER_H + 1, PDW_RED, PDW_RED_DARK);
    PokedexCanvas_Fill(canvas, x + 2, SW_PANEL_TOP + 1, w - 4, 1, PDW_WHITE);
    PokedexCanvas_Panel(canvas, x, SW_PANEL_TOP + SW_HEADER_H, w, SW_PANEL_BOTTOM - SW_PANEL_TOP - SW_HEADER_H, PDW_CREAM_LIGHT, PDW_DARK);
    PokedexCanvas_Fill(canvas, x + 6, SW_HINT_TOP - 5, w - 12, 1, PDW_GREY);

    PrintMessage(&display->infoWindow, sSearchRowLabels[settings->selectedRow], x + 8, SW_PANEL_TOP, 0, SW_TEXT_LIGHT, heapID);
    PrintMessage(&display->infoWindow, sSearchRowInfo[settings->selectedRow], x + 8, SW_PANEL_TOP + 20, 0, SW_TEXT_DARK, heapID);

    static const struct {
        s16 keyID;
        s8 arrow;
        u16 labelID;
    } hints[] = {
        { -1, -1, pl_msg_pokedex_wide_hint_change },
        { pl_msg_pokedex_wide_key_a, 0, pl_msg_pokedex_wide_hint_select },
        { pl_msg_pokedex_wide_key_start, 0, pl_msg_pokedex_wide_hint_search },
        { pl_msg_pokedex_wide_key_b, 0, pl_msg_pokedex_wide_hint_back },
    };

    const int labelX = x + 50;

    for (int i = 0; i < (int)NELEMS(hints); i++) {
        const int y = SW_HINT_TOP + i * SW_HINT_STEP;

        if (hints[i].keyID < 0) {
            DrawKeyCap(display, x + 8, y, -1, -1, heapID);
            DrawKeyCap(display, x + 23, y, -1, 1, heapID);
        } else {
            DrawKeyCap(display, x + 8, y, hints[i].keyID, 0, heapID);
        }

        PrintMessage(&display->hintWindow, hints[i].labelID, labelX, y - 2, 0, SW_TEXT_DARK, heapID);
    }
}

static void Redraw(PokedexSearchDisplay *display, PokedexGraphicData *graphicData, const PokedexSearchSettings *settings, enum HeapID heapID)
{
    PokedexCanvas *canvas = display->canvas;

    PokedexCanvas_Stripes(canvas, 0, 0, canvas->width, canvas->height, PDW_OLIVE, PDW_OLIVE_LIGHT);
    DrawHeader(display, settings, heapID);

    Window_FillTilemap(&display->listWindow, 0);
    PokedexCanvas_Panel(canvas, display->listX, SW_PANEL_TOP, display->listW, SW_PANEL_BOTTOM - SW_PANEL_TOP, PDW_CREAM, PDW_DARK);

    for (int row = 0; row < SW_ROW_COUNT; row++) {
        DrawRow(display, settings, row, heapID);
    }

    if (display->hasInfo) {
        Window_FillTilemap(&display->infoWindow, 0);
        Window_FillTilemap(&display->hintWindow, 0);
        DrawInfoPanel(display, settings, heapID);
        Window_CopyToVRAM(&display->infoWindow);
        Window_CopyToVRAM(&display->hintWindow);
    }

    PokedexCanvas_Upload(canvas, graphicData->bgConfig, BG_LAYER_MAIN_3, 1, 1023, 0, 0);
    Window_CopyToVRAM(&display->titleWindow);
    Window_CopyToVRAM(&display->dexWindow);
    Window_CopyToVRAM(&display->listWindow);

    display->drawnKey = DisplayKey(display, settings);
}

static void SearchWide_Setup(PokedexSearchDisplay *display, PokedexGraphicData *graphicData, const PokedexSearchSettings *settings, enum HeapID heapID)
{
    BgConfig *bgConfig = graphicData->bgConfig;

    display->width = PokedexWide_Begin();
    display->left = PokedexWide_Left();
    display->hasInfo = display->width >= SW_INFO_MIN_W;
    display->listX = 8;

    if (display->hasInfo) {
        int listW = ((display->width - 24) * 58 / 100) & ~7;

        display->listW = listW < 224 ? 224 : listW > 256 ? 256 : listW;
        display->rightX = display->listX + display->listW + 8;
        display->rightW = display->width - 8 - display->rightX;
    } else {
        display->listW = display->width - 16;
    }

    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_1, TRUE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_2, TRUE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_3, TRUE);
    PokedexWide_LoadPalette();

    if (graphicData->pokedexSpeciesLabel.textData) {
        PokedexGraphics_SetPokedexSpeciesLabelDraw(graphicData, FALSE);
    }

    // BG2: the header and the controls. BG1: the settings and the description.
    const int tileWidth = PokedexWide_TileWidth();
    const int dexLeft = 16;
    const int listRows = 24 - SW_ROW_TOP / 8;
    const int infoRows = SW_HINT_TOP / 8 - 2 - SW_PANEL_TOP / 8;
    u16 bg2Tile = 1;
    u16 bg1Tile = 1;

    Window_Add(bgConfig, &display->titleWindow, BG_LAYER_MAIN_2, 1, 0, 14, 2, POKEDEX_WIDE_PLTT, bg2Tile);
    bg2Tile += 14 * 2;
    Window_Add(bgConfig, &display->dexWindow, BG_LAYER_MAIN_2, dexLeft, 0, tileWidth - dexLeft, 2, POKEDEX_WIDE_PLTT, bg2Tile);
    bg2Tile += (tileWidth - dexLeft) * 2;

    Window_Add(bgConfig, &display->listWindow, BG_LAYER_MAIN_1, display->listX / 8, SW_ROW_TOP / 8, display->listW / 8, listRows, POKEDEX_WIDE_PLTT, bg1Tile);
    bg1Tile += (display->listW / 8) * listRows;

    if (display->hasInfo) {
        const int hintTop = SW_HINT_TOP / 8 - 1;

        Window_Add(bgConfig, &display->infoWindow, BG_LAYER_MAIN_1, display->rightX / 8, SW_PANEL_TOP / 8, display->rightW / 8, infoRows, POKEDEX_WIDE_PLTT, bg1Tile);
        Window_Add(bgConfig, &display->hintWindow, BG_LAYER_MAIN_2, display->rightX / 8, hintTop, display->rightW / 8, 24 - hintTop, POKEDEX_WIDE_PLTT, bg2Tile);
    }

    display->canvas = PokedexCanvas_New(display->width, 192, heapID);
    LoadTypeBadges(display, graphicData, heapID);

    // The search's Poké Ball spins on the start button, a quarter of its size.
    VecFx32 ballScale = { FX32_ONE / 4, FX32_ONE / 4, FX32_ONE };

    Sprite_SetAffineScaleEx(display->pokeball, &ballScale, AFFINE_OVERWRITE_MODE_NORMAL);
    PlaceSprite(display, display->pokeball, RowX(display) + 16, SW_SEARCH_Y + SW_ROW_H / 2);
    Sprite_SetExplicitPriority(display->form, 0);

    display->noneFound = settings->noneFound;
    display->noneFoundTimer = 0;
    Redraw(display, graphicData, settings, heapID);
}

static void SearchWide_Teardown(PokedexSearchDisplay *display, PokedexGraphicData *graphicData)
{
    BgConfig *bgConfig = graphicData->bgConfig;

    FreeTypeBadges(display, graphicData);

    Window_Remove(&display->titleWindow);
    Window_Remove(&display->dexWindow);
    Window_Remove(&display->listWindow);

    if (display->hasInfo) {
        Window_Remove(&display->infoWindow);
        Window_Remove(&display->hintWindow);
    }

    PokedexCanvas_Free(display->canvas);

    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_1, FALSE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_2, FALSE);
    PokedexWide_ConfigureLayer(bgConfig, BG_LAYER_MAIN_3, FALSE);
    Window_FillTilemap(&graphicData->window, 0);
    Window_PutToTilemap(&graphicData->window);
    // The screen stays black into the grid's fade-in.
    PokedexWide_End();
}

static void SearchWide_Update(PokedexSearchDisplay *display, PokedexGraphicData *graphicData, const PokedexSearchSettings *settings, enum HeapID heapID)
{
    if (settings->noneFound && !display->noneFound) {
        display->noneFoundTimer = 64;
    }

    display->noneFound = settings->noneFound;

    if (display->noneFoundTimer > 0) {
        display->noneFoundTimer--;
    }

    const BOOL searching = IsSearching(settings);

    Sprite_SetDrawFlag(display->pokeball, searching);

    if (searching) {
        display->spin++;
        Sprite_SetAffineZRotation(display->pokeball, (u16)(display->spin * 0x800));
    } else {
        display->spin = 0;
    }

    if (display->drawnKey != DisplayKey(display, settings)) {
        Redraw(display, graphicData, settings, heapID);
    }
}

static void SearchWide_StartFade(PokedexGraphicData *graphicData, BOOL fadeIn)
{
    // With BG2 in the planes, the whole screen fades (PokedexWide_AdjustFade).
    const GXBlendPlaneMask planes = GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ | GX_BLEND_PLANEMASK_BD;

    if (fadeIn) {
        PokedexGraphics_InitBlendTransition(&graphicData->blendMain, 4, -16, 0, 0, 16, planes, planes, FALSE);
    } else {
        PokedexGraphics_InitBlendTransition(&graphicData->blendMain, 4, 0, -16, 16, 0, planes, 0, FALSE);
    }
}
#endif
