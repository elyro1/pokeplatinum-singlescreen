#ifndef POKEPLATINUM_POKETCH_ROUTE_ENCOUNTERS_DATA_H
#define POKEPLATINUM_POKETCH_ROUTE_ENCOUNTERS_DATA_H

#include <nitro/types.h>

#include "constants/rtc.h"

#include "overlay006/wild_encounters.h"

// Each method has at most 12 slots, so the deduplicated list always fits one page.
#define ROUTE_ENCOUNTERS_MAX_MONS MAX_GRASS_ENCOUNTERS

enum RouteEncounterMethod {
    ROUTE_ENCOUNTER_METHOD_WALK = 0,
    ROUTE_ENCOUNTER_METHOD_SURF,
    ROUTE_ENCOUNTER_METHOD_OLD_ROD,
    ROUTE_ENCOUNTER_METHOD_GOOD_ROD,
    ROUTE_ENCOUNTER_METHOD_SUPER_ROD,
    ROUTE_ENCOUNTER_METHOD_COUNT,
};

typedef struct RouteEncounterMon {
    u16 species;
    u8 seen;
    u8 caught;
} RouteEncounterMon;

typedef struct RouteEncounterList {
    u32 count;
    RouteEncounterMon mons[ROUTE_ENCOUNTERS_MAX_MONS];
} RouteEncounterList;

typedef BOOL (*RouteEncounterDexCheck)(void *context, u16 species);

// Bitmask of methods (1 << method) with a nonzero rate and at least one species.
u32 RouteEncounters_AvailableMethods(const WildEncounters *encounters);
// Next available method after current in direction step (+1/-1), wrapping; current if none.
enum RouteEncounterMethod RouteEncounters_StepMethod(u32 methods, enum RouteEncounterMethod current, int step);
// Unique species in slot order. Walking slots 2 and 3 follow timeOfDay like the encounter code.
void RouteEncounters_BuildList(const WildEncounters *encounters, enum RouteEncounterMethod method, enum TimeOfDay timeOfDay, RouteEncounterDexCheck hasSeen, RouteEncounterDexCheck hasCaught, void *context, RouteEncounterList *list);

#endif // POKEPLATINUM_POKETCH_ROUTE_ENCOUNTERS_DATA_H
