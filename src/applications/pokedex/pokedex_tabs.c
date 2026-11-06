#include "applications/pokedex/pokedex_tabs.h"

#include <nitro.h>

#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_sort.h"
#include "applications/pokedex/species_caught_status.h"
#include "applications/pokedex/pokedex_wide.h"

#include "bg_window.h"

#include "res/text/bank/pokedex.h"

static const u8 sTabOrder[POKEDEX_TAB_SHOWN] = {
    POKEDEX_TAB_INFO,
    POKEDEX_TAB_STATS,
    POKEDEX_TAB_MOVES,
    POKEDEX_TAB_EVOS,
    POKEDEX_TAB_AREA,
    POKEDEX_TAB_SIZE,
    POKEDEX_TAB_FORMS,
};

int PokedexTabs_TabAt(int position)
{
    return sTabOrder[(position % POKEDEX_TAB_SHOWN + POKEDEX_TAB_SHOWN) % POKEDEX_TAB_SHOWN];
}

int PokedexTabs_Position(int tab)
{
    for (int position = 0; position < POKEDEX_TAB_SHOWN; position++) {
        if (sTabOrder[position] == tab) {
            return position;
        }
    }

    return -1;
}

BOOL PokedexTabs_IsAvailable(const PokedexSortData *sortData, int tab)
{
    switch (tab) {
    case POKEDEX_TAB_CRY:
        return FALSE;
    case POKEDEX_TAB_SIZE:
        return PokedexSort_CurrentCaughtStatus(sortData) == CS_CAUGHT;
    case POKEDEX_TAB_FORMS:
        return PokedexSort_CanDetectForms(sortData);
    default:
        return TRUE;
    }
}

BOOL PokedexTabs_IsWidePage(int tab)
{
    return tab == POKEDEX_TAB_INFO || tab >= POKEDEX_TAB_STATS;
}

u32 PokedexTabs_LabelMessage(int tab)
{
    switch (tab) {
    case POKEDEX_TAB_STATS:
        return pl_msg_pokedex_wide_tab_stats;
    case POKEDEX_TAB_MOVES:
        return pl_msg_pokedex_wide_tab_moves;
    case POKEDEX_TAB_EVOS:
        return pl_msg_pokedex_wide_tab_evos;
    default:
        return pl_msg_pokedex_info + tab;
    }
}

static int TabWidth(void)
{
    return (PokedexWide_Width() - 8) / POKEDEX_TAB_SHOWN;
}

static int TabX(int position)
{
    return (PokedexWide_Width() - TabWidth() * POKEDEX_TAB_SHOWN) / 2 + position * TabWidth();
}

int PokedexTabs_LabelX(int tab, int labelWidth)
{
    if (PokedexTabs_Position(tab) < 0) {
        return 0;
    }

    return TabX(PokedexTabs_Position(tab)) + (TabWidth() - labelWidth) / 2 - PokedexWide_Left();
}

// The strip shows the selected tab at once; the side panels change with the page, while
// the screen is black between the pages' fades.
static struct {
    PokedexGraphicData *graphicData;
    enum HeapID heapID;
    int selectedTab;
    int sidesTab;
} sTabs;

static void DrawStrip(PokedexGraphicData *graphicData, int selectedTab, int sidesTab, enum HeapID heapID);

static void DrawPendingSides(void)
{
    if (sTabs.graphicData && sTabs.sidesTab != sTabs.selectedTab) {
        sTabs.sidesTab = sTabs.selectedTab;
        DrawStrip(sTabs.graphicData, sTabs.selectedTab, sTabs.sidesTab, sTabs.heapID);
    }
}

void PokedexTabs_Setup(PokedexGraphicData *graphicData, enum HeapID heapID)
{
    PokedexWide_Begin();
    PokedexWide_ConfigureLayer(graphicData->bgConfig, BG_LAYER_MAIN_2, TRUE);
    PokedexWide_LoadPalette();

    sTabs.graphicData = graphicData;
    sTabs.heapID = heapID;
    sTabs.selectedTab = -1;
    sTabs.sidesTab = -1;
}

void PokedexTabs_Draw(PokedexGraphicData *graphicData, int selectedTab, enum HeapID heapID)
{
    // The first draw is made while the screen is black.
    if (sTabs.sidesTab < 0 || PokedexTabs_IsWidePage(sTabs.sidesTab) == PokedexTabs_IsWidePage(selectedTab)) {
        sTabs.sidesTab = selectedTab;
    }

    sTabs.selectedTab = selectedTab;
    DrawStrip(graphicData, selectedTab, sTabs.sidesTab, heapID);

    if (sTabs.sidesTab != selectedTab) {
        PokedexWide_RunAtBlack(DrawPendingSides);
    }
}

static void DrawStrip(PokedexGraphicData *graphicData, int selectedTab, int sidesTab, enum HeapID heapID)
{
    const int width = PokedexWide_Width();
    const int left = PokedexWide_Left();
    PokedexCanvas *canvas = PokedexCanvas_New(width, 192, heapID);

    PokedexCanvas_Fill(canvas, 0, 0, width, POKEDEX_TAB_STRIP_HEIGHT - 2, PDW_OLIVE);
    PokedexCanvas_Fill(canvas, 0, POKEDEX_TAB_STRIP_HEIGHT - 2, width, 1, PDW_GOLD);
    PokedexCanvas_Fill(canvas, 0, POKEDEX_TAB_STRIP_HEIGHT - 1, width, 1, PDW_GOLD_DARK);

    for (int position = 0; position < POKEDEX_TAB_SHOWN; position++) {
        const int x = TabX(position);

        if (PokedexTabs_TabAt(position) == selectedTab) {
            PokedexCanvas_Panel(canvas, x + 1, 1, TabWidth() - 2, POKEDEX_TAB_STRIP_HEIGHT - 3, PDW_RED, PDW_RED_DARK);
            PokedexCanvas_Fill(canvas, x + 3, 2, TabWidth() - 6, 1, PDW_WHITE);
        } else if (position > 0 && PokedexTabs_TabAt(position - 1) != selectedTab) {
            PokedexCanvas_Fill(canvas, x, 3, 1, POKEDEX_TAB_STRIP_HEIGHT - 7, PDW_OLIVE_LIGHT);
        }
    }

    // Platinum's own pages stay 256 dots wide; frame them with the striped sides.
    if (!PokedexTabs_IsWidePage(sidesTab) && left > 0) {
        PokedexCanvas_Stripes(canvas, 0, POKEDEX_TAB_STRIP_HEIGHT, left, 192 - POKEDEX_TAB_STRIP_HEIGHT, PDW_OLIVE, PDW_OLIVE_LIGHT);
        PokedexCanvas_Stripes(canvas, left + 256, POKEDEX_TAB_STRIP_HEIGHT, width - left - 256, 192 - POKEDEX_TAB_STRIP_HEIGHT, PDW_OLIVE, PDW_OLIVE_LIGHT);
        PokedexCanvas_Fill(canvas, left - 2, POKEDEX_TAB_STRIP_HEIGHT, 2, 192 - POKEDEX_TAB_STRIP_HEIGHT, PDW_DARK);
        PokedexCanvas_Fill(canvas, left + 256, POKEDEX_TAB_STRIP_HEIGHT, 2, 192 - POKEDEX_TAB_STRIP_HEIGHT, PDW_DARK);
    }

    Bg_ClearTilemap(graphicData->bgConfig, BG_LAYER_MAIN_2);
    PokedexCanvas_Upload(canvas, graphicData->bgConfig, BG_LAYER_MAIN_2, 1, 1023, 0, 0);
    PokedexCanvas_Free(canvas);
}

void PokedexTabs_Teardown(PokedexGraphicData *graphicData)
{
    sTabs.graphicData = NULL;

    PokedexWide_ConfigureLayer(graphicData->bgConfig, BG_LAYER_MAIN_2, FALSE);
    PokedexWide_End();
}
