#include "ribbon_save_data.h"

#include <nitro.h>
#include <string.h>

#include "savedata.h"

int Ribbons_SaveSize(void)
{
    return RIBBONS_SAVE_SIZE;
}

#ifdef SDK_PORT
// The port keeps the DS save layout byte for byte; see port/assets/ds_layout.h.
_Static_assert(RIBBONS_SAVE_SIZE == 14, "Ribbons must keep its DS save size");
#endif

void Ribbons_Init(u8 *ribbons)
{
    memset(ribbons, 0, RIBBONS_SAVE_SIZE);
}

u8 *SaveData_GetRibbons(SaveData *saveData)
{
    return SaveData_SaveTable(saveData, SAVE_TABLE_ENTRY_RIBBONS);
}
