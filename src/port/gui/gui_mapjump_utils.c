#include <nitro.h>

#include "port/debug/debug_field.h"
#include "field_map_change.h"
#include "constants/map_object.h"

void GUI_MapJump_JumpToMap(int mapId, int x, int z) {
    FieldSystem * fsys = DEBUG_GetFieldSystem();
    FieldTask_StartMapChangeFly(fsys, mapId, -1, x, z, DIR_SOUTH);
}