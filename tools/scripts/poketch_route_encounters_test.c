#include <assert.h>
#include <string.h>

#include "constants/species.h"

#include "applications/poketch/route_encounters/data.h"

static BOOL SeenSet(void *context, u16 species)
{
    const u16 *seen = context;

    for (; *seen != SPECIES_NONE; seen++) {
        if (*seen == species) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL CaughtSet(void *context, u16 species)
{
    return species == SPECIES_STARLY;
}

static void FillGrass(WildEncounters *data)
{
    static const int grass[MAX_GRASS_ENCOUNTERS] = {
        SPECIES_STARLY, SPECIES_BIDOOF, SPECIES_STARLY, SPECIES_BIDOOF, SPECIES_STARLY, SPECIES_BIDOOF, SPECIES_STARLY, SPECIES_BIDOOF, SPECIES_STARLY, SPECIES_BIDOOF, SPECIES_KRICKETOT, SPECIES_KRICKETOT
    };

    data->grassEncounters.encounterRate = 30;

    for (int i = 0; i < MAX_GRASS_ENCOUNTERS; i++) {
        data->grassEncounters.encounters[i].species = grass[i];
        data->grassEncounters.encounters[i].level = 3;
    }

    data->dayEncounters[0] = SPECIES_STARLY;
    data->dayEncounters[1] = SPECIES_BIDOOF;
    data->nightEncounters[0] = SPECIES_HOOTHOOT;
    data->nightEncounters[1] = SPECIES_KRICKETOT;
}

static void TestNoEncounters(void)
{
    WildEncounters data;
    memset(&data, 0, sizeof(data));
    assert(RouteEncounters_AvailableMethods(&data) == 0);

    // A rate without species, or species without a rate, is not a method.
    data.surfEncounters.encounterRate = 10;
    data.oldRodEncounters.encounters[0].species = SPECIES_MAGIKARP;
    assert(RouteEncounters_AvailableMethods(&data) == 0);
}

static void TestDedupeAndTime(void)
{
    WildEncounters data;
    memset(&data, 0, sizeof(data));
    FillGrass(&data);

    u16 seen[] = { SPECIES_BIDOOF, SPECIES_HOOTHOOT, SPECIES_NONE };
    RouteEncounterList list;

    RouteEncounters_BuildList(&data, ROUTE_ENCOUNTER_METHOD_WALK, TIMEOFDAY_MORNING, SeenSet, CaughtSet, seen, &list);
    assert(list.count == 3);
    assert(list.mons[0].species == SPECIES_STARLY && list.mons[0].caught && list.mons[0].seen);
    assert(list.mons[1].species == SPECIES_BIDOOF && !list.mons[1].caught && list.mons[1].seen);
    assert(list.mons[2].species == SPECIES_KRICKETOT && !list.mons[2].seen);

    // Night replaces slots 2 and 3, adding Hoothoot ahead of Kricketot.
    RouteEncounters_BuildList(&data, ROUTE_ENCOUNTER_METHOD_WALK, TIMEOFDAY_LATE_NIGHT, SeenSet, CaughtSet, seen, &list);
    assert(list.count == 4);
    assert(list.mons[2].species == SPECIES_HOOTHOOT && list.mons[2].seen);
    assert(list.mons[3].species == SPECIES_KRICKETOT);
}

static void TestMethods(void)
{
    WildEncounters data;
    memset(&data, 0, sizeof(data));
    FillGrass(&data);

    data.surfEncounters.encounterRate = 10;
    data.surfEncounters.encounters[0].species = SPECIES_TENTACOOL;
    data.surfEncounters.encounters[1].species = SPECIES_TENTACOOL;
    data.superRodEncounters.encounterRate = 40;
    data.superRodEncounters.encounters[4].species = SPECIES_GYARADOS;

    u32 methods = RouteEncounters_AvailableMethods(&data);
    assert(methods == ((1 << ROUTE_ENCOUNTER_METHOD_WALK) | (1 << ROUTE_ENCOUNTER_METHOD_SURF) | (1 << ROUTE_ENCOUNTER_METHOD_SUPER_ROD)));

    assert(RouteEncounters_StepMethod(methods, ROUTE_ENCOUNTER_METHOD_WALK, 1) == ROUTE_ENCOUNTER_METHOD_SURF);
    assert(RouteEncounters_StepMethod(methods, ROUTE_ENCOUNTER_METHOD_SURF, 1) == ROUTE_ENCOUNTER_METHOD_SUPER_ROD);
    assert(RouteEncounters_StepMethod(methods, ROUTE_ENCOUNTER_METHOD_SUPER_ROD, 1) == ROUTE_ENCOUNTER_METHOD_WALK);
    assert(RouteEncounters_StepMethod(methods, ROUTE_ENCOUNTER_METHOD_WALK, -1) == ROUTE_ENCOUNTER_METHOD_SUPER_ROD);
    assert(RouteEncounters_StepMethod(1 << ROUTE_ENCOUNTER_METHOD_SURF, ROUTE_ENCOUNTER_METHOD_SURF, 1) == ROUTE_ENCOUNTER_METHOD_SURF);

    u16 seen[] = { SPECIES_NONE };
    RouteEncounterList list;

    RouteEncounters_BuildList(&data, ROUTE_ENCOUNTER_METHOD_SURF, TIMEOFDAY_DAY, SeenSet, CaughtSet, seen, &list);
    assert(list.count == 1 && list.mons[0].species == SPECIES_TENTACOOL && !list.mons[0].seen);

    RouteEncounters_BuildList(&data, ROUTE_ENCOUNTER_METHOD_OLD_ROD, TIMEOFDAY_DAY, SeenSet, CaughtSet, seen, &list);
    assert(list.count == 0);
}

int main(void)
{
    TestNoEncounters();
    TestDedupeAndTime();
    TestMethods();
    return 0;
}
