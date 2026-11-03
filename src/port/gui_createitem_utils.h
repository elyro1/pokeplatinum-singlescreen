#ifndef GUI_CREATEITEM_UTILS_H
#define GUI_CREATEITEM_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: You will need to free the pointer returned by this!
const char * GUI_CreateItem_GetItemName(int itemId);

int GUI_CreateItem_GetPocket(int itemId);
int GUI_CreateItem_GenItems(int itemId, int quantity);

#ifdef __cplusplus
}
#endif


#endif
