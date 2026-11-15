#ifndef POKEPLATINUM_FIELD_LIGHTS_H
#define POKEPLATINUM_FIELD_LIGHTS_H

#include <nitro/types.h>

#include "port/visuals/field_visuals.h"

// The field's light table: night materials and the light sources placed in
// the world. It is read from field_lights.txt in the game's folder (see that
// file for its format), with a built-in copy as the fallback.

enum FieldLightKind {
    FIELD_LIGHT_WINDOW = 0, // Lit window; a pool in front of it.
    FIELD_LIGHT_STREET = 1, // Street lamp; a pool around it.
    FIELD_LIGHT_CENTER = 2, // Pokémon Center logo; lights the entrance.
    FIELD_LIGHT_BEACON = 4, // Lighthouse lantern and its turning beams.
    FIELD_LIGHT_DOWNLIGHT = 5, // Cycling Road light shining onto the road.
};

typedef struct FieldLamp {
    int map;
    enum FieldLightKind kind;
    short x, y, z;
    // Downlights: x offset of the lit patch from the head, half width of its
    // pool, and the patch's height at the head's z (0: no patch).
    short aim, width, deck;
} FieldLamp;

typedef struct FieldPropLight {
    int prop;
    enum FieldLightKind kind;
    short x, y, z; // Offset from the prop's origin.
} FieldPropLight;

#ifdef __cplusplus
extern "C" {
#endif

// Rereads field_lights.txt. Returns FALSE and keeps the current table when
// the file has errors; FieldLights_Status then describes them.
BOOL FieldLights_Reload(void);
// Where the table came from, or the error that stopped a reload.
const char *FieldLights_Status(void);

// map is the current map header; prop is the prop model being drawn, or -1
// for the land mesh. Returns NULL for an untagged material.
const FieldNightMaterialInfo *FieldLights_Material(const char name[16], int map, int prop);
int FieldLights_Lamps(const FieldLamp **lamps);
int FieldLights_PropLights(const FieldPropLight **lights);
BOOL FieldLights_HasDayEmission(int map);

// Parses a light table into the current one. Returns FALSE and leaves the
// current table alone on errors, describing the first in error (size bytes).
BOOL FieldLights_Parse(const char *text, char *error, int size);

#ifdef __cplusplus
}
#endif

#endif
