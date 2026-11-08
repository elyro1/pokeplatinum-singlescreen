#include "port/ds_save_import.h"

#include "constants/savedata/save_table.h"
#include "constants/savedata/savedata.h"
#include "port/file_commit.h"
#include "savedata/save_table.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define FLASH_SIZE            0x80000
#define HALF_SIZE             0x40000
#define NORMAL_REGION_SIZE    0x20000
#define DESMUME_MAX_EXTRA     4096
#define BLOCK_FOOTER_SIZE     20
#define EXTRA_FOOTER_SIZE     16
#define ENTRY_COUNT           38
#define BOXES_ENTRY           37
#define BLOCK_COUNT           2
#define RECORDING_DS_SIZE     7504
#define RECORDING_PC_SIZE     7512
#define RECORDING_COUNT       4
#define RECORDING_FIRST_ID    EXTRA_SAVE_TABLE_ENTRY_MY_RECORDINGS
#define BACKUP_NAME_ATTEMPTS  1000

_Static_assert(SAVE_TABLE_ENTRY_MAX == ENTRY_COUNT, "DS save import needs updating for the save table");

static const char sDesmumeFooter[] = "|-DESMUME SAVE-|";
#define DESMUME_FOOTER_SIZE (sizeof(sDesmumeFooter) - 1)

// sizeof each gSaveTable entry on DS, indexed by save table ID.
static const int sDsSizes[ENTRY_COUNT] = {
    92, 44, 1424, 1908, 940, 280, 160, 804, 480, 2176, 1932,
    5120, 2484, 32, 2100, 1120, 800, 264, 1120, 1568, 444, 368,
    1004, 5672, 14, 340, 248, 3016, 3744, 2240, 4088, 4904,
    1672, 40, 8, 64, 8, 74184
};

static const u8 sRecordingPages[RECORDING_COUNT] = { 36, 38, 40, 42 };

typedef struct {
    u32 counter;
    u32 blockCounter;
    const u8 *body;
} BlockCopy;

typedef struct {
    BlockCopy copies[2];
    int count;
} BlockCopies;

// The entries whose size differs are exactly those that ConvertEntry remaps.
static int PcEntrySize(int index)
{
    switch (index) {
    case 0:
        return 104;
    case 8:
        return 488;
    case BOXES_ENTRY:
        return 74192;
    default:
        return sDsSizes[index];
    }
}

static int DsEntrySize(int index)
{
    return sDsSizes[index];
}

static int PageSize(int size)
{
    return size + 4 - size % 4 + 4;
}

static void BlockSizes(int (*entrySize)(int), int sizes[BLOCK_COUNT])
{
    sizes[0] = BLOCK_FOOTER_SIZE;
    for (int i = 0; i < BOXES_ENTRY; i++) {
        sizes[0] += PageSize(entrySize(i));
    }
    sizes[1] = PageSize(entrySize(BOXES_ENTRY)) + BLOCK_FOOTER_SIZE;
}

static u16 Crc16(const u8 *data, size_t size)
{
    static u16 table[256];
    static int ready;
    if (!ready) {
        for (int i = 0; i < 256; i++) {
            u16 value = (u16)(i << 8);
            for (int bit = 0; bit < 8; bit++) {
                value = (value & 0x8000) ? (u16)((value << 1) ^ 0x1021) : (u16)(value << 1);
            }
            table[i] = value;
        }
        ready = 1;
    }
    u16 crc = 0xFFFF;
    for (size_t i = 0; i < size; i++) {
        crc = (u16)((crc << 8) ^ table[(crc >> 8) ^ data[i]]);
    }
    return crc;
}

static u32 Read32(const u8 *data)
{
    return (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
}

static u16 Read16(const u8 *data)
{
    return (u16)(data[0] | data[1] << 8);
}

static void Write32(u8 *data, u32 value)
{
    data[0] = (u8)value;
    data[1] = (u8)(value >> 8);
    data[2] = (u8)(value >> 16);
    data[3] = (u8)(value >> 24);
}

static void Write16(u8 *data, u16 value)
{
    data[0] = (u8)value;
    data[1] = (u8)(value >> 8);
}

static void FindBlocks(const u8 *data, const int sizes[BLOCK_COUNT], BlockCopies found[BLOCK_COUNT])
{
    memset(found, 0, sizeof(BlockCopies) * BLOCK_COUNT);
    for (int half = 0; half < 2; half++) {
        const u8 *block = data + half * HALF_SIZE;
        for (int id = 0; id < BLOCK_COUNT; id++) {
            int bodySize = sizes[id] - BLOCK_FOOTER_SIZE;
            const u8 *footer = block + bodySize;
            if (Read32(footer + 8) == (u32)sizes[id] && Read32(footer + 12) == SECTOR_SIGNATURE
                && footer[16] == id && Read16(footer + 18) == Crc16(block, bodySize)) {
                BlockCopy *copy = &found[id].copies[found[id].count++];
                copy->counter = Read32(footer);
                copy->blockCounter = Read32(footer + 4);
                copy->body = block;
            }
            block += sizes[id];
        }
    }
}

// Matches SaveData_CompareCounters, which treats the UINT32_MAX -> 0 rollover specially.
static int CounterNewer(u32 a, u32 b)
{
    if (a == 0 && b == 0xFFFFFFFF) return 1;
    if (a == 0xFFFFFFFF && b == 0) return 0;
    return a > b;
}

static int SelectPair(const BlockCopies found[BLOCK_COUNT], BlockCopy best[BLOCK_COUNT])
{
    int have = 0;
    for (int n = 0; n < found[0].count; n++) {
        for (int b = 0; b < found[1].count; b++) {
            const BlockCopy *normal = &found[0].copies[n];
            const BlockCopy *boxes = &found[1].copies[b];
            if (normal->counter != boxes->counter) continue;
            if (!have
                || CounterNewer(normal->counter, best[0].counter)
                || (normal->counter == best[0].counter
                    && (CounterNewer(normal->blockCounter, best[0].blockCounter)
                        || (normal->blockCounter == best[0].blockCounter
                            && CounterNewer(boxes->blockCounter, best[1].blockCounter))))) {
                best[0] = *normal;
                best[1] = *boxes;
                have = 1;
            }
        }
    }
    return have;
}

static void ConvertEntry(int index, const u8 *source, u8 *target)
{
    switch (index) {
    case 0:
        // GameTime: align its two s64 timestamps to 8 and add tail padding.
        memcpy(target, source, 52);
        memcpy(target + 56, source + 52, 20);
        memcpy(target + 80, source + 72, 20);
        break;
    case 8:
        // Two 236-byte DaycareMon records become 240-byte records.
        memcpy(target, source, 236);
        memcpy(target + 240, source + 236, 236);
        memcpy(target + 480, source + 472, 8);
        break;
    case BOXES_ENTRY:
        // Align the BoxPokemon array; Pokemon bytes and encryption stay intact.
        memcpy(target, source, 4);
        memcpy(target + 8, source + 4, 74179);
        break;
    default:
        memcpy(target, source, sDsSizes[index]);
        break;
    }
}

static int ConvertBody(int blockID, const u8 *source, u8 *target)
{
    int first = blockID == 0 ? 0 : BOXES_ENTRY;
    int end = blockID == 0 ? BOXES_ENTRY : ENTRY_COUNT;
    int written = 0;
    for (int index = first; index < end; index++) {
        int pageSize = PageSize(PcEntrySize(index));
        u8 *page = target + written;
        memset(page, 0, pageSize);
        ConvertEntry(index, source, page);
        Write16(page + pageSize - 4, Crc16(page, pageSize - 4));
        written += pageSize;
        source += PageSize(sDsSizes[index]);
    }
    return written;
}

static void ConvertRecordings(const u8 *data, u8 *output)
{
    // Extra saves use fixed pages in each flash half and a 16-byte footer.
    for (int half = 0; half < 2; half++) {
        for (int i = 0; i < RECORDING_COUNT; i++) {
            size_t offset = (size_t)half * HALF_SIZE + sRecordingPages[i] * (size_t)SAVE_SECTOR_SIZE;
            const u8 *old = data + offset;
            const u8 *footer = old + RECORDING_DS_SIZE;
            int id = RECORDING_FIRST_ID + i;
            if (Read32(footer) != SECTOR_SIGNATURE || Read32(footer + 8) != RECORDING_DS_SIZE
                || Read16(footer + 12) != id
                || Read16(footer + 14) != Crc16(old, RECORDING_DS_SIZE + EXTRA_FOOTER_SIZE - 2)) {
                continue;
            }
            u8 *record = output + offset;
            memset(record, 0, RECORDING_PC_SIZE);
            memcpy(record, old, 132);
            memcpy(record + 136, old + 132, 100);
            memcpy(record + 240, old + 232, RECORDING_DS_SIZE - 232);
            // The native validator hashes 92 bytes of battleInfo, compared with
            // 88 on DS; the encrypted battleData itself has identical layout.
            Write16(record + 232, Crc16(record + 136, 92));
            u8 *newFooter = record + RECORDING_PC_SIZE;
            Write32(newFooter, SECTOR_SIGNATURE);
            Write32(newFooter + 4, Read32(footer + 4));
            Write32(newFooter + 8, RECORDING_PC_SIZE);
            Write16(newFooter + 12, (u16)id);
            Write16(newFooter + 14, Crc16(record, RECORDING_PC_SIZE + EXTRA_FOOTER_SIZE - 2));
        }
    }
}

static void Convert(const u8 *data, const BlockCopy pair[BLOCK_COUNT], u8 *output)
{
    memcpy(output, data, FLASH_SIZE); // Preserve Hall of Fame, frontier and extra saves.
    for (int half = 0; half < 2; half++) {
        // Rebuild both normal save copies from the most recent consistent pair.
        u8 *block = output + half * HALF_SIZE;
        memset(block, 0xFF, NORMAL_REGION_SIZE);
        for (int id = 0; id < BLOCK_COUNT; id++) {
            int bodySize = ConvertBody(id, pair[id].body, block);
            u8 *footer = block + bodySize;
            Write32(footer, pair[id].counter);
            Write32(footer + 4, pair[id].blockCounter);
            Write32(footer + 8, bodySize + BLOCK_FOOTER_SIZE);
            Write32(footer + 12, SECTOR_SIGNATURE);
            footer[16] = (u8)id;
            footer[17] = 0;
            Write16(footer + 18, Crc16(block, bodySize));
            block = footer + BLOCK_FOOTER_SIZE;
        }
    }
    ConvertRecordings(data, output);
}

// The conversion targets one exact layout, so a resized save entry must stop
// it rather than replace the player's file with a save the game then rejects.
static int LiveLayoutMatches(void)
{
    if (gSaveTableSize != ENTRY_COUNT) return 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (gSaveTable[i].sizeFunc() != PcEntrySize(i)) return 0;
    }
    for (int i = 0; i < RECORDING_COUNT; i++) {
        if (gExtraSaveTable[RECORDING_FIRST_ID + i].sizeFunc() != RECORDING_PC_SIZE) return 0;
    }
    return 1;
}

static u8 *ReadWhole(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    u8 *data = NULL;
    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0) length = ftell(file);
    if (length >= 0 && fseek(file, 0, SEEK_SET) == 0) {
        data = malloc(length ? (size_t)length : 1);
        if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    if (data) *size = (size_t)length;
    return data;
}

static int WriteAndClose(FILE *file, const u8 *data, size_t size)
{
    int ok = fwrite(data, 1, size, file) == size;
    if (ok) ok = PC_FileSync(file);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

// Stores the original beside the slot. Never replaces an existing backup: one
// with the same bytes is reused, which also completes an interrupted import.
static int StoreBackup(int slot, const u8 *data, size_t size)
{
    char path[32];
    for (int attempt = 0; attempt < BACKUP_NAME_ATTEMPTS; attempt++) {
        if (attempt == 0) {
            snprintf(path, sizeof(path), "save%d.bak", slot);
        } else {
            snprintf(path, sizeof(path), "save%d.%d.bak", slot, attempt);
        }
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
        if (fd >= 0) {
            FILE *file = fdopen(fd, "wb");
            if (!file) {
                close(fd);
                remove(path);
                return 0;
            }
            if (!WriteAndClose(file, data, size)) {
                remove(path);
                return 0;
            }
            fprintf(stderr, "[save-import] Kept the original DS save as %s\n", path);
            return 1;
        }
        if (errno != EEXIST) return 0;
        size_t existingSize = 0;
        u8 *existing = ReadWhole(path, &existingSize);
        if (!existing) return 0;
        int same = existingSize == size && memcmp(existing, data, size) == 0;
        free(existing);
        if (same) return 1;
    }
    return 0;
}

static int PublishConverted(const char *path, const u8 *data)
{
    char temporary[32];
    snprintf(temporary, sizeof(temporary), "%s.import-tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) return 0;
    int ok = WriteAndClose(file, data, FLASH_SIZE);
    if (ok) {
        size_t size = 0;
        u8 *written = ReadWhole(temporary, &size);
        ok = written && size == FLASH_SIZE && memcmp(written, data, FLASH_SIZE) == 0;
        free(written);
    }
    if (ok) ok = PC_FileReplace(temporary, path);
    if (!ok) remove(temporary);
    return ok;
}

static int IsDsSaveSize(const u8 *data, size_t size)
{
    if (size == FLASH_SIZE) return 1;
    return size > FLASH_SIZE && size <= FLASH_SIZE + DESMUME_MAX_EXTRA
        && memcmp(data + size - DESMUME_FOOTER_SIZE, sDesmumeFooter, DESMUME_FOOTER_SIZE) == 0;
}

static void ImportSlot(int slot)
{
    char path[16];
    snprintf(path, sizeof(path), "save%d.bin", slot);

    // The PC backend writes only as far as its last used address, so most PC
    // saves are shorter than any DS save and end here without being read.
    FILE *probe = fopen(path, "rb");
    if (!probe) return;
    long length = -1;
    if (fseek(probe, 0, SEEK_END) == 0) length = ftell(probe);
    fclose(probe);
    if (length < FLASH_SIZE || length > FLASH_SIZE + DESMUME_MAX_EXTRA) return;

    size_t size = 0;
    u8 *data = ReadWhole(path, &size);
    u8 *output = malloc(FLASH_SIZE);
    if (!data || !output) goto cleanup;
    if (!IsDsSaveSize(data, size)) goto cleanup;

    int dsSizes[BLOCK_COUNT], pcSizes[BLOCK_COUNT];
    BlockSizes(DsEntrySize, dsSizes);
    BlockSizes(PcEntrySize, pcSizes);
    BlockCopies found[BLOCK_COUNT];

    // Footers store their block size and the layouts differ, so this cannot
    // mistake one format for the other.
    FindBlocks(data, pcSizes, found);
    if (found[0].count && found[1].count) goto cleanup;

    BlockCopy pair[BLOCK_COUNT];
    FindBlocks(data, dsSizes, found);
    if (!SelectPair(found, pair)) {
        fprintf(stderr, "[save-import] %s holds no convertible DS save data; left unchanged\n", path);
        goto cleanup;
    }
    if (!LiveLayoutMatches()) {
        fprintf(stderr, "[save-import] This build's save layout is not supported by the DS import; %s left unchanged\n", path);
        goto cleanup;
    }

    Convert(data, pair, output);
    BlockCopy verified[BLOCK_COUNT];
    FindBlocks(output, pcSizes, found);
    if (!SelectPair(found, verified)) {
        fprintf(stderr, "[save-import] Conversion of %s did not validate; left unchanged\n", path);
        goto cleanup;
    }

    if (!StoreBackup(slot, data, size)) {
        fprintf(stderr, "[save-import] Could not back up %s; left unchanged\n", path);
        goto cleanup;
    }
    if (!PublishConverted(path, output)) {
        fprintf(stderr, "[save-import] Could not write the converted %s; left unchanged\n", path);
        goto cleanup;
    }
    fprintf(stderr, "[save-import] Converted DS save %s to the PC layout\n", path);

cleanup:
    free(data);
    free(output);
}

void PC_ImportDsSaves(void)
{
    for (int slot = 1; slot <= 3; slot++) {
        ImportSlot(slot);
    }
}
