#include <nitro.h>

#include "gui_createitem_utils.h"

#include "message_util.h"
#include "savedata.h"
#include "bag.h"
#include "item.h"

#include "charcode_convert.h"

#define ITEM_NAME_SIZE 32


const char * GUI_CreateItem_GetItemName(int itemId) {
    String *itemStr = String_Init(ITEM_NAME_SIZE, HEAP_ID_SYSTEM);
    Item_LoadName(itemStr, itemId, HEAP_ID_SYSTEM);

    char * itemNameBuf = malloc(sizeof(char) * itemStr->size + 1);
    CharCode_ToAsciiString(itemStr->data, itemNameBuf, itemStr->size);
    itemNameBuf[itemStr->size] = 0;
    String_Free(itemStr);

    return itemNameBuf;
}

int GUI_CreateItem_GetPocket(int itemId) {
    return Item_LoadParam(itemId, ITEM_PARAM_FIELD_POCKET, HEAP_ID_SYSTEM);
}

int GUI_CreateItem_GenItems(int itemId, int quantity) {
    SaveData * mySaveData = SaveData_Ptr();
    return Bag_TryAddItem(SaveData_GetBag(mySaveData), itemId, quantity, HEAP_ID_SYSTEM);
}
