#include "bwbattle.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef SDK_BUILD_WIN64
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

#include "types.h"
#include "assets/bw2_assets.h"
#include "gfl/arc.h"

// The BW2 archives bwbattle reads, by archive ID (docs/bwbattle.md). BW2 opens archive N at the ROM path
// a/<hundreds>/<tens>/<ones>
static const u16 sNeededArcs[] = {
    4,   // Pokémon sprites (MCSS)
    6,   // Particles
    11,  // Battle graphics
    16,  // Personal data: the form sprites of each species
    21,  // Move data: the targets the move effects are drawn for
    60,  // Field lights: the outdoor arenas' light by time of day
    61,  // Battle lights: by weather
    65,  // Move effect scripts
    66,  // The other effect scripts
    71,  // Trainer sprites, front
    72,  // Trainer sprites, back
    82,  // The menus' shared graphics: status and type icons
    94,  // The effects' backgrounds
    151, // Stage and field tables
    231, // Effect graphics
};

// More than BW2 has; the paths of IDs BW2 doesn't have are never opened
#define BW2_ARC_COUNT 400
#define BW2_ARC_PATH_SIZE sizeof(BWBATTLE_ASSET_DIR "/a/0/0/0")
#define BW2_FILE_PATH_SIZE 64

// BW2's sound archive in the ROM's root
#define BW2_SOUND_ARCHIVE_ROM_NAME "swan_sound_data.sdat"

static char sArcPathData[BW2_ARC_COUNT][BW2_ARC_PATH_SIZE];
static const char *sArcPaths[BW2_ARC_COUNT];

// The ROM header: the game code, and the file name table and file allocation table
#define ROM_GAME_CODE 0x0c
#define ROM_FNT_OFFSET 0x40
#define ROM_FNT_SIZE 0x44
#define ROM_FAT_OFFSET 0x48
#define ROM_FAT_SIZE 0x4c
#define ROM_HEADER_SIZE 0x50

// The file name table's directory IDs start here; the root's is the first
#define FNT_DIR_ID_BASE 0xf000

typedef struct {
    FILE *file;
    u8 *fnt;
    u32 fntSize;
    u8 *fat;
    u32 fatSize;
} Bw2Rom;

static void ArcPath(u32 arcId, char *path, size_t size) {
    snprintf(path, size, BWBATTLE_ASSET_DIR "/a/%u/%u/%u", arcId / 100, arcId / 10 % 10, arcId % 10);
}

void BwAssets_InitArcSys(void) {
    for (u32 i = 0; i < BW2_ARC_COUNT; i++) {
        ArcPath(i, sArcPathData[i], sizeof(sArcPathData[i]));
        sArcPaths[i] = sArcPathData[i];
    }
    GFL_ArcSysInit(sArcPaths, BW2_ARC_COUNT);
}

const char *GFL_ArcSysGetResourcePath(u32 arcId) {
    if (arcId >= BW2_ARC_COUNT) {
        return NULL;
    }
    if (sArcPaths[arcId] == NULL) {
        ArcPath(arcId, sArcPathData[arcId], sizeof(sArcPathData[arcId]));
        sArcPaths[arcId] = sArcPathData[arcId];
    }
    return sArcPaths[arcId];
}

static bool FileExists(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

BOOL BwAssets_ArcExists(u32 arcId) {
    char path[BW2_ARC_PATH_SIZE];

    ArcPath(arcId, path, sizeof(path));
    return FileExists(path);
}

bool BwBattle_AssetsReady(void) {
    char path[BW2_ARC_PATH_SIZE];

    for (size_t i = 0; i < NELEMS(sNeededArcs); i++) {
        ArcPath(sNeededArcs[i], path, sizeof(path));
        if (!FileExists(path)) {
            return false;
        }
    }
    return true;
}

static u32 Read32(const u8 *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static u16 Read16(const u8 *p) {
    return p[0] | (p[1] << 8);
}

static bool ReadAt(FILE *file, u32 offset, void *dest, u32 size) {
    return fseek(file, offset, SEEK_SET) == 0 && fread(dest, 1, size, file) == size;
}

static void *ReadNew(FILE *file, u32 offset, u32 size) {
    void *data = malloc(size);

    if (data != NULL && !ReadAt(file, offset, data, size)) {
        free(data);
        return NULL;
    }
    return data;
}

static void Bw2Rom_Close(Bw2Rom *rom) {
    free(rom->fnt);
    free(rom->fat);
    if (rom->file != NULL) {
        fclose(rom->file);
    }
    memset(rom, 0, sizeof(*rom));
}

static BwBattleExtractResult Bw2Rom_Open(Bw2Rom *rom, const char *path) {
    u8 header[ROM_HEADER_SIZE];

    memset(rom, 0, sizeof(*rom));
    rom->file = fopen(path, "rb");
    if (rom->file == NULL) {
        return BWBATTLE_EXTRACT_CANT_OPEN;
    }
    if (!ReadAt(rom->file, 0, header, sizeof(header))) {
        Bw2Rom_Close(rom);
        return BWBATTLE_EXTRACT_NOT_BW2;
    }
    // IREO is Black 2 and IRDO White 2, of the USA
    if (memcmp(&header[ROM_GAME_CODE], "IREO", 4) != 0 && memcmp(&header[ROM_GAME_CODE], "IRDO", 4) != 0) {
        Bw2Rom_Close(rom);
        return BWBATTLE_EXTRACT_NOT_BW2;
    }
    rom->fntSize = Read32(&header[ROM_FNT_SIZE]);
    rom->fatSize = Read32(&header[ROM_FAT_SIZE]);
    rom->fnt = ReadNew(rom->file, Read32(&header[ROM_FNT_OFFSET]), rom->fntSize);
    rom->fat = ReadNew(rom->file, Read32(&header[ROM_FAT_OFFSET]), rom->fatSize);
    if (rom->fnt == NULL || rom->fat == NULL) {
        Bw2Rom_Close(rom);
        return BWBATTLE_EXTRACT_BAD_ROM;
    }
    return BWBATTLE_EXTRACT_OK;
}

// Looks up an entry of a directory by name. Returns its file ID, or its directory ID for a directory, or -1
static int Bw2Rom_FindEntry(const Bw2Rom *rom, u32 dirId, const char *name, bool wantDir) {
    u32 dirOffset = (dirId - FNT_DIR_ID_BASE) * 8;
    u32 pos;
    u32 fileId;
    size_t nameLen = strlen(name);

    if (dirOffset + 8 > rom->fntSize) {
        return -1;
    }
    pos = Read32(&rom->fnt[dirOffset]);
    fileId = Read16(&rom->fnt[dirOffset + 4]);
    while (pos < rom->fntSize) {
        u8 len = rom->fnt[pos++];
        bool isDir = (len & 0x80) != 0;

        if (len == 0 || len == 0x80) {
            break;
        }
        len &= 0x7f;
        if (pos + len + (isDir ? 2 : 0) > rom->fntSize) {
            break;
        }
        if (isDir == wantDir && len == nameLen && memcmp(&rom->fnt[pos], name, len) == 0) {
            return isDir ? Read16(&rom->fnt[pos + len]) : (int)fileId;
        }
        pos += len;
        if (isDir) {
            pos += 2;
        } else {
            fileId++;
        }
    }
    return -1;
}

// The file ID of an archive, from its path a/<hundreds>/<tens>/<ones>
static int Bw2Rom_FindArc(const Bw2Rom *rom, u32 arcId) {
    char digits[3][2] = { { '0' + arcId / 100, 0 }, { '0' + arcId / 10 % 10, 0 }, { '0' + arcId % 10, 0 } };
    int dir = Bw2Rom_FindEntry(rom, FNT_DIR_ID_BASE, "a", true);

    for (int i = 0; i < 2 && dir >= 0; i++) {
        dir = Bw2Rom_FindEntry(rom, dir, digits[i], true);
    }
    return dir < 0 ? -1 : Bw2Rom_FindEntry(rom, dir, digits[2], false);
}

// Creates each directory of a file's path
static bool MakeParentDirs(const char *path) {
    char dir[BW2_FILE_PATH_SIZE];

    for (size_t i = 0; path[i] != '\0' && i < sizeof(dir); i++) {
        if (path[i] == '/') {
            memcpy(dir, path, i);
            dir[i] = '\0';
            if (MKDIR(dir) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }
    return true;
}

// Copies a ROM file to path in pieces, under a temporary name first, so an interrupted extraction never leaves a short
// file
#define BW2_COPY_CHUNK 0x100000

static BwBattleExtractResult Bw2Rom_ExtractFile(const Bw2Rom *rom, int fileId, const char *path) {
    char tmpPath[BW2_FILE_PATH_SIZE + 4];
    u32 start, end;
    void *chunk;
    FILE *out;
    bool ok;

    if (fileId < 0 || (u32)fileId * 8 + 8 > rom->fatSize) {
        return BWBATTLE_EXTRACT_BAD_ROM;
    }
    start = Read32(&rom->fat[fileId * 8]);
    end = Read32(&rom->fat[fileId * 8 + 4]);
    if (end < start) {
        return BWBATTLE_EXTRACT_BAD_ROM;
    }
    chunk = malloc(BW2_COPY_CHUNK);
    if (chunk == NULL) {
        return BWBATTLE_EXTRACT_CANT_WRITE;
    }

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    ok = MakeParentDirs(path);
    if (ok) {
        out = fopen(tmpPath, "wb");
        ok = out != NULL;
        for (u32 pos = start; ok && pos < end; pos += BW2_COPY_CHUNK) {
            u32 size = end - pos < BW2_COPY_CHUNK ? end - pos : BW2_COPY_CHUNK;

            if (!ReadAt(rom->file, pos, chunk, size)) {
                free(chunk);
                fclose(out);
                remove(tmpPath);
                return BWBATTLE_EXTRACT_BAD_ROM;
            }
            ok = fwrite(chunk, 1, size, out) == size;
        }
        if (out != NULL) {
            ok = fclose(out) == 0 && ok;
        }
        remove(path);
        ok = ok && rename(tmpPath, path) == 0;
        if (!ok) {
            remove(tmpPath);
        }
    }
    free(chunk);
    return ok ? BWBATTLE_EXTRACT_OK : BWBATTLE_EXTRACT_CANT_WRITE;
}

BwBattleExtractResult BwBattle_ExtractAssets(const char *romPath) {
    Bw2Rom rom;
    BwBattleExtractResult result = Bw2Rom_Open(&rom, romPath);
    char path[BW2_ARC_PATH_SIZE];

    for (size_t i = 0; i < NELEMS(sNeededArcs) && result == BWBATTLE_EXTRACT_OK; i++) {
        ArcPath(sNeededArcs[i], path, sizeof(path));
        if (!FileExists(path)) {
            result = Bw2Rom_ExtractFile(&rom, Bw2Rom_FindArc(&rom, sNeededArcs[i]), path);
        }
    }
    // Optional to the battle view, but extracted by the same action, and older extractions are upgraded: the overworld
    // models' records (47) and textures (48, also Iris's), the VS mugshots (267), and the walking Pokémon's object
    // codes (208), which the followers and overworld encounters use (port/follower_assets.cpp)
    static const u16 optionalArcs[] = { 47, 48, 208, 267 };
    for (size_t i = 0; i < NELEMS(optionalArcs) && result == BWBATTLE_EXTRACT_OK; i++) {
        if (!BwAssets_ArcExists(optionalArcs[i])) {
            ArcPath(optionalArcs[i], path, sizeof(path));
            result = Bw2Rom_ExtractFile(&rom, Bw2Rom_FindArc(&rom, optionalArcs[i]), path);
        }
    }
    // The sound archive (the effects' sounds), which the view does without when it is missing
    if (result == BWBATTLE_EXTRACT_OK && !FileExists(BWBATTLE_SOUND_ARCHIVE)) {
        result = Bw2Rom_ExtractFile(&rom, Bw2Rom_FindEntry(&rom, FNT_DIR_ID_BASE, BW2_SOUND_ARCHIVE_ROM_NAME, false),
                                    BWBATTLE_SOUND_ARCHIVE);
    }
    Bw2Rom_Close(&rom);
    return result;
}

const char *BwBattle_ExtractResultMessage(BwBattleExtractResult result) {
    switch (result) {
    case BWBATTLE_EXTRACT_OK:
        return "BW2 graphics extracted.";
    case BWBATTLE_EXTRACT_CANT_OPEN:
        return "The ROM file couldn't be opened.";
    case BWBATTLE_EXTRACT_NOT_BW2:
        return "That isn't a US Pokemon Black 2 or White 2 ROM.";
    case BWBATTLE_EXTRACT_BAD_ROM:
        return "The ROM is missing BW2 battle data; it may be damaged or modified.";
    case BWBATTLE_EXTRACT_CANT_WRITE:
        return "The BW2 battle graphics couldn't be written to the game folder.";
    }
    return "";
}
