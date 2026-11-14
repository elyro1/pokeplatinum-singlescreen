#include "applications/poketch/route_encounters/data.h"

#include <nitro.h>
#include <string.h>

#include "constants/species.h"

static const WaterEncounters *GetWaterEncounters(const WildEncounters *encounters, enum RouteEncounterMethod method)
{
    switch (method) {
    case ROUTE_ENCOUNTER_METHOD_SURF:
        return &encounters->surfEncounters;
    case ROUTE_ENCOUNTER_METHOD_OLD_ROD:
        return &encounters->oldRodEncounters;
    case ROUTE_ENCOUNTER_METHOD_GOOD_ROD:
        return &encounters->goodRodEncounters;
    case ROUTE_ENCOUNTER_METHOD_SUPER_ROD:
        return &encounters->superRodEncounters;
    default:
        return NULL;
    }
}

// Mirrors WildEncounters_ReplaceTimedEncounters with an explicit time of day.
static void GetWalkingSpecies(const WildEncounters *encounters, enum TimeOfDay timeOfDay, int *species)
{
    for (int i = 0; i < MAX_GRASS_ENCOUNTERS; i++) {
        species[i] = encounters->grassEncounters.encounters[i].species;
    }

    if (timeOfDay == TIMEOFDAY_DAY || timeOfDay == TIMEOFDAY_TWILIGHT) {
        species[2] = encounters->dayEncounters[0];
        species[3] = encounters->dayEncounters[1];
    } else if (timeOfDay == TIMEOFDAY_NIGHT || timeOfDay == TIMEOFDAY_LATE_NIGHT) {
        species[2] = encounters->nightEncounters[0];
        species[3] = encounters->nightEncounters[1];
    }
}

static BOOL HasSpecies(const int *species, int count)
{
    for (int i = 0; i < count; i++) {
        if (species[i] > SPECIES_NONE && species[i] <= NATIONAL_DEX_COUNT) {
            return TRUE;
        }
    }

    return FALSE;
}

static int GetMethodSpecies(const WildEncounters *encounters, enum RouteEncounterMethod method, enum TimeOfDay timeOfDay, int *species)
{
    if (method == ROUTE_ENCOUNTER_METHOD_WALK) {
        if (encounters->grassEncounters.encounterRate == 0) {
            return 0;
        }

        GetWalkingSpecies(encounters, timeOfDay, species);
        return MAX_GRASS_ENCOUNTERS;
    }

    const WaterEncounters *water = GetWaterEncounters(encounters, method);

    if (water == NULL || water->encounterRate == 0) {
        return 0;
    }

    for (int i = 0; i < MAX_WATER_ENCOUNTERS; i++) {
        species[i] = water->encounters[i].species;
    }

    return MAX_WATER_ENCOUNTERS;
}

u32 RouteEncounters_AvailableMethods(const WildEncounters *encounters)
{
    u32 methods = 0;
    int species[MAX_GRASS_ENCOUNTERS];

    for (int method = 0; method < ROUTE_ENCOUNTER_METHOD_COUNT; method++) {
        // Day and night only swap slots, so the morning table decides availability.
        int count = GetMethodSpecies(encounters, method, TIMEOFDAY_MORNING, species);

        if (HasSpecies(species, count)) {
            methods |= 1 << method;
        }
    }

    return methods;
}

enum RouteEncounterMethod RouteEncounters_StepMethod(u32 methods, enum RouteEncounterMethod current, int step)
{
    int method = current;

    for (int i = 0; i < ROUTE_ENCOUNTER_METHOD_COUNT; i++) {
        method = (method + step + ROUTE_ENCOUNTER_METHOD_COUNT) % ROUTE_ENCOUNTER_METHOD_COUNT;

        if (methods & (1 << method)) {
            return method;
        }
    }

    return current;
}

void RouteEncounters_BuildList(const WildEncounters *encounters, enum RouteEncounterMethod method, enum TimeOfDay timeOfDay, RouteEncounterDexCheck hasSeen, RouteEncounterDexCheck hasCaught, void *context, RouteEncounterList *list)
{
    int species[MAX_GRASS_ENCOUNTERS];
    int count = GetMethodSpecies(encounters, method, timeOfDay, species);

    memset(list, 0, sizeof(*list));

    for (int i = 0; i < count; i++) {
        if (species[i] <= SPECIES_NONE || species[i] > NATIONAL_DEX_COUNT) {
            continue;
        }

        u32 j;
        for (j = 0; j < list->count; j++) {
            if (list->mons[j].species == species[i]) {
                break;
            }
        }

        if (j < list->count) {
            continue;
        }

        RouteEncounterMon *mon = &list->mons[list->count++];
        mon->species = species[i];
        mon->caught = hasCaught(context, species[i]) ? TRUE : FALSE;
        // Owning a species implies having seen it, even if the seen flag is missing.
        mon->seen = mon->caught || hasSeen(context, species[i]);
    }
}
