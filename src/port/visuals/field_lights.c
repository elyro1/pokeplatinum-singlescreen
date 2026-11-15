#include "port/visuals/field_lights.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POKEPLATINUM_GENERATED_LOOKUP
#define POKEPLATINUM_GENERATED_LOOKUP_IMPL
#include "generated/map_headers.h"

// The built-in copy of res/pc_port/field_lights.txt.
#include "field_lights.inc"

#define LIGHTS_FILE "field_lights.txt"
#define ANY -1
// Scopes a material to the land mesh, which is drawn with prop -1.
#define LAND -2
#define MATERIAL_LIMIT 256
#define LAMP_LIMIT 128
#define PROP_LIGHT_LIMIT 32
#define DAY_EMISSION_LIMIT 16

typedef struct {
    char name[16];
    int map, prop;
    FieldNightMaterialInfo info;
} Material;

typedef struct {
    Material materials[MATERIAL_LIMIT];
    FieldLamp lamps[LAMP_LIMIT];
    FieldPropLight propLights[PROP_LIGHT_LIMIT];
    int dayEmission[DAY_EMISSION_LIMIT];
    int materialCount, lampCount, propLightCount, dayEmissionCount;
} Table;

static Table sTable;
static BOOL sLoaded;
static char sStatus[192];

static const struct {
    const char *name;
    enum FieldNightMaterial role;
} sRoles[] = {
    { "shadow", FIELD_NIGHT_MATERIAL_SHADOW },
    { "painted_shadow", FIELD_NIGHT_MATERIAL_PAINTED_SHADOW },
    { "tree", FIELD_NIGHT_MATERIAL_TREE },
    { "puddle_ground", FIELD_NIGHT_MATERIAL_PUDDLE_GROUND },
    { "window", FIELD_NIGHT_MATERIAL_WINDOW },
    { "source", FIELD_NIGHT_MATERIAL_SOURCE },
    { "lamp", FIELD_NIGHT_MATERIAL_LAMP },
    { "light_bar", FIELD_NIGHT_MATERIAL_LIGHT_BAR },
    { "neon", FIELD_NIGHT_MATERIAL_NEON },
    { "keyed_window", FIELD_NIGHT_MATERIAL_KEYED_WINDOW },
    { "beacon", FIELD_NIGHT_MATERIAL_BEACON },
    { "screen", FIELD_NIGHT_MATERIAL_SCREEN },
    { "emblem", FIELD_NIGHT_MATERIAL_EMBLEM },
    { "lava", FIELD_NIGHT_MATERIAL_LAVA },
    { "water", FIELD_NIGHT_MATERIAL_WATER },
};

static const struct {
    const char *name;
    enum FieldLightKind kind;
} sLampKinds[] = {
    { "street", FIELD_LIGHT_STREET },
    { "downlight", FIELD_LIGHT_DOWNLIGHT },
}, sPropKinds[] = {
    { "window", FIELD_LIGHT_WINDOW },
    { "center", FIELD_LIGHT_CENTER },
    { "beacon", FIELD_LIGHT_BEACON },
};

// A map header name without its MAP_HEADER_ prefix; -1 if unknown.
static int FindMap(const char *name)
{
    char full[96];
    snprintf(full, sizeof(full), "MAP_HEADER_%s", name);
    for (long i = 0; i < lengthof__MapHeaderID; i++)
        if (strcmp(lookup__MapHeaderID[i].def, full) == 0)
            return (int)lookup__MapHeaderID[i].value;
    return -1;
}

static BOOL ParseNumber(const char *text, float *value)
{
    char *end;
    *value = strtof(text, &end);
    return end != text && *end == 0;
}
static BOOL ParseInt(const char *text, int *value)
{
    char *end;
    long number = strtol(text, &end, 10);
    *value = (int)number;
    return end != text && *end == 0;
}

// Splits a line into words in place; returns the count.
static int Words(char *line, char **words, int limit)
{
    int count = 0;
    for (char *c = line; *c && count < limit;) {
        while (*c == ' ' || *c == '\t' || *c == '\r')
            *c++ = 0;
        if (!*c)
            break;
        words[count++] = c;
        while (*c && *c != ' ' && *c != '\t' && *c != '\r')
            c++;
    }
    return count;
}

static const char *ParseMaterial(Table *table, char **words, int count)
{
    if (count < 3)
        return "material needs a name and a role";
    if (table->materialCount >= MATERIAL_LIMIT)
        return "too many materials";
    Material *m = &table->materials[table->materialCount];
    memset(m, 0, sizeof(*m));
    if (strlen(words[1]) >= sizeof(m->name))
        return "material names have at most 15 characters";
    strcpy(m->name, words[1]);
    m->map = m->prop = ANY;
    m->info.role = FIELD_NIGHT_MATERIAL_NONE;
    for (unsigned i = 0; i < sizeof(sRoles) / sizeof(sRoles[0]); i++)
        if (strcmp(words[2], sRoles[i].name) == 0)
            m->info.role = sRoles[i].role;
    if (m->info.role == FIELD_NIGHT_MATERIAL_NONE)
        return "unknown material role";
    m->info.bloom = 0;
    m->info.intensity = 1;
    for (int i = 3; i < count; i++) {
        char *value = strchr(words[i], '=');
        if (!value)
            return "options are written key=value";
        *value++ = 0;
        if (strcmp(words[i], "bloom") == 0) {
            if (!ParseNumber(value, &m->info.bloom) || m->info.bloom < 0 || m->info.bloom > FIELD_BLOOM_PEAK)
                return "bloom is a number from 0 to 3";
        } else if (strcmp(words[i], "intensity") == 0) {
            if (!ParseNumber(value, &m->info.intensity) || m->info.intensity < 0)
                return "intensity is a number from 0";
        } else if (strcmp(words[i], "map") == 0) {
            if ((m->map = FindMap(value)) < 0)
                return "unknown map";
        } else if (strcmp(words[i], "prop") == 0) {
            if (strcmp(value, "land") == 0)
                m->prop = LAND;
            else if (!ParseInt(value, &m->prop) || m->prop < 0)
                return "prop is a prop model number or land";
        } else {
            return "unknown material option";
        }
    }
    table->materialCount++;
    return NULL;
}

static const char *ParseLamp(Table *table, char **words, int count)
{
    if (count < 6)
        return "lamp needs a kind, a map and x y z";
    if (table->lampCount >= LAMP_LIMIT)
        return "too many lamps";
    FieldLamp *lamp = &table->lamps[table->lampCount];
    memset(lamp, 0, sizeof(*lamp));
    lamp->kind = -1;
    for (unsigned i = 0; i < sizeof(sLampKinds) / sizeof(sLampKinds[0]); i++)
        if (strcmp(words[1], sLampKinds[i].name) == 0)
            lamp->kind = sLampKinds[i].kind;
    if ((int)lamp->kind < 0)
        return "lamp kinds are street and downlight";
    if ((lamp->map = FindMap(words[2])) < 0)
        return "unknown map";
    int position[3];
    for (int i = 0; i < 3; i++)
        if (!ParseInt(words[3 + i], &position[i]))
            return "lamp positions are whole numbers";
    lamp->x = position[0];
    lamp->y = position[1];
    lamp->z = position[2];
    for (int i = 6; i < count; i++) {
        char *text = strchr(words[i], '=');
        int value;
        if (!text)
            return "options are written key=value";
        *text++ = 0;
        if (!ParseInt(text, &value))
            return "lamp options are whole numbers";
        if (strcmp(words[i], "aim") == 0)
            lamp->aim = value;
        else if (strcmp(words[i], "width") == 0)
            lamp->width = value;
        else if (strcmp(words[i], "deck") == 0)
            lamp->deck = value;
        else
            return "unknown lamp option";
    }
    table->lampCount++;
    return NULL;
}

static const char *ParsePropLight(Table *table, char **words, int count)
{
    if (count != 6)
        return "prop_light needs a prop, a kind and x y z";
    if (table->propLightCount >= PROP_LIGHT_LIMIT)
        return "too many prop lights";
    FieldPropLight *light = &table->propLights[table->propLightCount];
    if (!ParseInt(words[1], &light->prop) || light->prop < 0)
        return "prop is a prop model number";
    light->kind = -1;
    for (unsigned i = 0; i < sizeof(sPropKinds) / sizeof(sPropKinds[0]); i++)
        if (strcmp(words[2], sPropKinds[i].name) == 0)
            light->kind = sPropKinds[i].kind;
    if ((int)light->kind < 0)
        return "prop light kinds are window, center and beacon";
    int position[3];
    for (int i = 0; i < 3; i++)
        if (!ParseInt(words[3 + i], &position[i]))
            return "prop light offsets are whole numbers";
    light->x = position[0];
    light->y = position[1];
    light->z = position[2];
    table->propLightCount++;
    return NULL;
}

BOOL FieldLights_Parse(const char *text, char *error, int size)
{
    static Table table;
    memset(&table, 0, sizeof(table));
    int lineNumber = 0;
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        char buffer[512];
        lineNumber++;
        if (length >= sizeof(buffer)) {
            snprintf(error, size, "line %d: too long", lineNumber);
            return FALSE;
        }
        memcpy(buffer, line, length);
        buffer[length] = 0;
        line += length + (end ? 1 : 0);
        char *comment = strchr(buffer, '#');
        if (comment)
            *comment = 0;
        char *words[16];
        int count = Words(buffer, words, 16);
        if (count == 0)
            continue;
        const char *problem = NULL;
        if (strcmp(words[0], "material") == 0) {
            problem = ParseMaterial(&table, words, count);
        } else if (strcmp(words[0], "lamp") == 0) {
            problem = ParseLamp(&table, words, count);
        } else if (strcmp(words[0], "prop_light") == 0) {
            problem = ParsePropLight(&table, words, count);
        } else if (strcmp(words[0], "day_emission") == 0) {
            int map = count == 2 ? FindMap(words[1]) : -1;
            if (count != 2 || map < 0)
                problem = "day_emission needs one known map";
            else if (table.dayEmissionCount >= DAY_EMISSION_LIMIT)
                problem = "too many day_emission maps";
            else
                table.dayEmission[table.dayEmissionCount++] = map;
        } else {
            problem = "unknown line; expected material, lamp, prop_light or day_emission";
        }
        if (problem) {
            snprintf(error, size, "line %d: %s", lineNumber, problem);
            return FALSE;
        }
    }
    sTable = table;
    return TRUE;
}

static char *ReadFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = size >= 0 ? malloc(size + 1) : NULL;
    if (text && fread(text, 1, size, file) != (size_t)size) {
        free(text);
        text = NULL;
    }
    if (text)
        text[size] = 0;
    fclose(file);
    return text;
}

BOOL FieldLights_Reload(void)
{
    char error[160];
    char *text = ReadFile(LIGHTS_FILE);
    BOOL ok;
    if (text) {
        ok = FieldLights_Parse(text, error, sizeof(error));
        free(text);
        if (ok)
            snprintf(sStatus, sizeof(sStatus), "%d materials, %d lamps from " LIGHTS_FILE, sTable.materialCount, sTable.lampCount + sTable.propLightCount);
        else
            snprintf(sStatus, sizeof(sStatus), LIGHTS_FILE " %s; kept the previous table", error);
    } else {
        ok = FieldLights_Parse(sEmbeddedFieldLights, error, sizeof(error));
        snprintf(sStatus, sizeof(sStatus), ok ? "built-in table (no " LIGHTS_FILE ")" : "built-in table %s", error);
    }
    // The first load always ends with a table, the built-in one if need be.
    if (!sLoaded && !ok && text && FieldLights_Parse(sEmbeddedFieldLights, error, sizeof(error))) {
        char *kept = strstr(sStatus, "kept the previous table");
        if (kept)
            snprintf(kept, sizeof(sStatus) - (kept - sStatus), "using the built-in table");
    }
    sLoaded = TRUE;
    return ok;
}

const char *FieldLights_Status(void)
{
    if (!sLoaded)
        FieldLights_Reload();
    return sStatus;
}

const FieldNightMaterialInfo *FieldLights_Material(const char name[16], int map, int prop)
{
    if (!sLoaded)
        FieldLights_Reload();
    for (int i = 0; i < sTable.materialCount; i++) {
        const Material *m = &sTable.materials[i];
        size_t length = strlen(m->name);
        if ((m->map == ANY || m->map == map)
            && (m->prop == ANY || m->prop == prop || (m->prop == LAND && prop == -1))
            && strncmp(name, m->name, length) == 0 && (name[length] == 0 || name[length] == '_'))
            return &m->info;
    }
    return NULL;
}

int FieldLights_Lamps(const FieldLamp **lamps)
{
    if (!sLoaded)
        FieldLights_Reload();
    *lamps = sTable.lamps;
    return sTable.lampCount;
}

int FieldLights_PropLights(const FieldPropLight **lights)
{
    if (!sLoaded)
        FieldLights_Reload();
    *lights = sTable.propLights;
    return sTable.propLightCount;
}

BOOL FieldLights_HasDayEmission(int map)
{
    if (!sLoaded)
        FieldLights_Reload();
    for (int i = 0; i < sTable.dayEmissionCount; i++)
        if (sTable.dayEmission[i] == map)
            return TRUE;
    return FALSE;
}
