#ifndef POKEPLATINUM_APPLICATIONS_BAG_GRID_H
#define POKEPLATINUM_APPLICATIONS_BAG_GRID_H

#include <nitro.h>

#include "applications/bag/defs.h"

#include "bg_window.h"

// The PC bag for the single-screen layout: the bag, the pocket selector and the
// description keep their places, and the item list becomes one of two views, which
// X and Y change between:
//   - a grid of item icons, with the selected item's list entry (name, count, TM
//     number) below it;
//   - a list of icons with each item's name beside it, in two columns where the view
//     is wide enough.
// The touch screen's dial and pocket buttons are out of sight in this layout, so the
// D-pad moves through the cells, and L and R, or Left and Right past the cells' edge,
// change pockets.
//
// The main screen's layers are 512 dots wide with tile column 0 at the left edge
// of the view, which is up to 448 dots wide; whatever the view adds to the native
// 256 dots goes to the grid. Sprites keep native coordinates and move to the
// view's left edge as they are drawn.
//
//   BG1  the bag's own art and the grid's panel
//   BG2  the item icons, their counts and the text among the cells, 256 colors with
//        an extended palette
//   BG3  every window: the pocket name, the description, message boxes and menus
//
// The icons are on a 256-color layer because each one has a palette of its own:
// a page of them shares one palette, built from the colors the page uses.

enum BagGridMove {
    BAG_GRID_MOVE_NONE = 0,
    BAG_GRID_MOVE_CURSOR,
    BAG_GRID_MOVE_PREVIOUS_POCKET,
    BAG_GRID_MOVE_NEXT_POCKET,
};

// Before the VRAM banks are set, and after the layers are freed.
void BagGrid_Begin(void);
void BagGrid_End(void);
BOOL BagGrid_IsActive(void);

// The layer for the description, message boxes and menus, and the one for the list
// and the pocket name.
u8 BagGrid_PopupLayer(void);
u8 BagGrid_TextLayer(void);
GXVRamBGExtPltt BagGrid_ExtPaletteBank(void);
// The main screen's layers, in place of the bag's own.
void BagGrid_SetupLayers(BgConfig *bgConfig);
// After the bag's art is on BG1: makes room for the grid and widens the description's panel.
void BagGrid_LoadGraphics(BgConfig *bgConfig);
void BagGrid_Init(BagController *controller);
void BagGrid_Free(void);

// After the current pocket's items are read or change: counts them and brings the
// cursor back in range.
void BagGrid_SetPocket(BagController *controller);
int BagGrid_NumItems(void);
// The pocket slot the cursor is on.
int BagGrid_Selected(BagController *controller);
// Moves the cursor with the D-pad. Left from the first column and Right from the last
// ask for the pocket before or after instead.
enum BagGridMove BagGrid_HandleMovement(BagController *controller);
// Changes between the grid of icons and the list with each item's name.
void BagGrid_ToggleView(BagController *controller);
// The cursor as the list keeps it, for the saved cursor.
void BagGrid_ToListCursor(int selected, int numItems, u16 *cursorScroll, u16 *cursorPos);

// The icons are read again only after BagGrid_SetPocket, a scroll or this.
void BagGrid_InvalidateIcons(void);
void BagGrid_Draw(BagController *controller);
// The cursor turns grey while a menu or a message has the input, as the list's does.
void BagGrid_SetCursorActive(BagController *controller, BOOL active);
void BagGrid_DrawSprites(BagController *controller);

// A view with room right of the description shows a TM's move there while the cursor
// is on it: its type, category, power and accuracy. Without the room they take the
// description's place once the TM is selected, as in the list.
BOOL BagGrid_HasMoveInfo(void);
Window *BagGrid_MoveInfoWindow(void);
// Whether the PP fit after the icons on the first line.
BOOL BagGrid_MoveInfoHasPP(void);
// Shows the type and category icons of a TM's move in the move info. ITEM_NONE hides them.
void BagGrid_ShowMoveIcons(BagController *controller, u16 item);

#endif // POKEPLATINUM_APPLICATIONS_BAG_GRID_H
