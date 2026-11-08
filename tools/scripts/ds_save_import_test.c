#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failRename, failSync, brokenLayout;

static int TestRename(const char *from, const char *to)
{
    return failRename ? -1 : rename(from, to);
}

static int TestSync(int fd)
{
    return failSync ? -1 : fsync(fd);
}

#define rename TestRename
#define fsync TestSync
#include "../../src/port/file_commit.c"
#include "../../src/port/ds_save_import.c"
#undef rename
#undef fsync

#define ENTRY_SIZE(i) \
    static int EntrySize##i(void) { return PcEntrySize(i) + (brokenLayout && i == 3 ? 4 : 0); }
#define ENTRIES(X)                                                                  \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12)             \
    X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24)         \
    X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37)
ENTRIES(ENTRY_SIZE)

#define ENTRY_ROW(i) { .dataID = i, .sizeFunc = EntrySize##i },
const SaveTableEntry gSaveTable[] = { ENTRIES(ENTRY_ROW) };
const int gSaveTableSize = NELEMS(gSaveTable);

static int RecordingSize(void) { return RECORDING_PC_SIZE; }

const SaveTableEntry gExtraSaveTable[] = {
    [EXTRA_SAVE_TABLE_ENTRY_MY_RECORDINGS] = { .sizeFunc = RecordingSize },
    [EXTRA_SAVE_TABLE_ENTRY_DL_RECORDINGS_0] = { .sizeFunc = RecordingSize },
    [EXTRA_SAVE_TABLE_ENTRY_DL_RECORDINGS_1] = { .sizeFunc = RecordingSize },
    [EXTRA_SAVE_TABLE_ENTRY_DL_RECORDINGS_2] = { .sizeFunc = RecordingSize },
};
const int gExtraSaveTableSize = NELEMS(gExtraSaveTable);

static u8 sDs[FLASH_SIZE + 64];

static void WriteBlock(u8 *block, int id, int bodySize, u32 counter, u32 blockCounter, u8 fill)
{
    for (int i = 0; i < bodySize; i++) {
        block[i] = (u8)(fill + i * 7 + (i >> 8));
    }
    u8 *footer = block + bodySize;
    memset(footer, 0, BLOCK_FOOTER_SIZE);
    Write32(footer, counter);
    Write32(footer + 4, blockCounter);
    Write32(footer + 8, bodySize + BLOCK_FOOTER_SIZE);
    Write32(footer + 12, SECTOR_SIGNATURE);
    footer[16] = (u8)id;
    Write16(footer + 18, Crc16(block, bodySize));
}

// Each half holds one complete DS save; the fill byte tells the halves apart.
static void BuildDs(u32 counterA, u32 counterB)
{
    int sizes[BLOCK_COUNT];
    BlockSizes(DsEntrySize, sizes);
    memset(sDs, 0xFF, sizeof(sDs));
    for (int half = 0; half < 2; half++) {
        u32 counter = half ? counterB : counterA;
        u8 fill = half ? 0x40 : 0x10;
        u8 *block = sDs + half * HALF_SIZE;
        WriteBlock(block, 0, sizes[0] - BLOCK_FOOTER_SIZE, counter, 5, fill);
        WriteBlock(block + sizes[0], 1, sizes[1] - BLOCK_FOOTER_SIZE, counter, 6, (u8)(fill + 3));
    }
    // A Hall of Fame byte pattern that the import must carry over untouched.
    memset(sDs + SAVE_PAGE_MAX * SAVE_SECTOR_SIZE, 0x5A, 64);

    u8 *record = sDs + sRecordingPages[0] * (size_t)SAVE_SECTOR_SIZE;
    for (int i = 0; i < RECORDING_DS_SIZE; i++) {
        record[i] = (u8)(i * 3);
    }
    u8 *footer = record + RECORDING_DS_SIZE;
    Write32(footer, SECTOR_SIGNATURE);
    Write32(footer + 4, 9);
    Write32(footer + 8, RECORDING_DS_SIZE);
    Write16(footer + 12, RECORDING_FIRST_ID);
    Write16(footer + 14, Crc16(record, RECORDING_DS_SIZE + EXTRA_FOOTER_SIZE - 2));
}

static void Put(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size && fclose(file) == 0);
}

static int Matches(const char *path, const void *data, size_t size)
{
    size_t actual = 0;
    u8 *contents = ReadWhole(path, &actual);
    int same = contents && actual == size && memcmp(contents, data, size) == 0;
    free(contents);
    return same;
}

static int Exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void Clean(void)
{
    remove("save1.bin");
    remove("save1.bak");
    remove("save1.1.bak");
    remove("save1.bin.import-tmp");
    failRename = failSync = brokenLayout = 0;
}

static void AssertConverted(u8 fill)
{
    int pcSizes[BLOCK_COUNT], dsSizes[BLOCK_COUNT];
    BlockSizes(PcEntrySize, pcSizes);
    BlockSizes(DsEntrySize, dsSizes);
    size_t size = 0;
    u8 *pc = ReadWhole("save1.bin", &size);
    assert(pc && size == FLASH_SIZE);

    BlockCopies found[BLOCK_COUNT];
    FindBlocks(pc, pcSizes, found);
    assert(found[0].count == 2 && found[1].count == 2);
    assert(memcmp(pc, pc + HALF_SIZE, pcSizes[0] + pcSizes[1]) == 0);

    // Build the expected source bytes from the same fill the block was made with.
    u8 *ds = malloc(dsSizes[0]);
    u8 *boxesDs = malloc(dsSizes[1]);
    assert(ds && boxesDs);
    for (int i = 0; i < dsSizes[0] - BLOCK_FOOTER_SIZE; i++) {
        ds[i] = (u8)(fill + i * 7 + (i >> 8));
    }
    for (int i = 0; i < dsSizes[1] - BLOCK_FOOTER_SIZE; i++) {
        boxesDs[i] = (u8)(fill + 3 + i * 7 + (i >> 8));
    }

    // GameTime gains alignment gaps at 52 and 76, and its entry grows.
    assert(memcmp(pc, ds, 52) == 0);
    assert(pc[52] == 0 && pc[55] == 0);
    assert(memcmp(pc + 56, ds + 52, 20) == 0);
    assert(memcmp(pc + 80, ds + 72, 20) == 0);
    assert(Read16(pc + PageSize(104) - 4) == Crc16(pc, PageSize(104) - 4));

    int pcOffset = 0, dsOffset = 0;
    for (int i = 0; i < 8; i++) {
        pcOffset += PageSize(PcEntrySize(i));
        dsOffset += PageSize(sDsSizes[i]);
    }
    assert(memcmp(pc + pcOffset, ds + dsOffset, 236) == 0);
    assert(memcmp(pc + pcOffset + 240, ds + dsOffset + 236, 236) == 0);
    assert(memcmp(pc + pcOffset + 480, ds + dsOffset + 472, 8) == 0);

    const u8 *boxes = pc + pcSizes[0];
    assert(memcmp(boxes, boxesDs, 4) == 0);
    assert(memcmp(boxes + 8, boxesDs + 4, 74179) == 0);

    assert(pc[SAVE_PAGE_MAX * SAVE_SECTOR_SIZE] == 0x5A);

    const u8 *record = pc + sRecordingPages[0] * (size_t)SAVE_SECTOR_SIZE;
    assert(Read32(record + RECORDING_PC_SIZE + 8) == RECORDING_PC_SIZE);
    assert(Read16(record + RECORDING_PC_SIZE + 14) == Crc16(record, RECORDING_PC_SIZE + EXTRA_FOOTER_SIZE - 2));
    assert(Read16(record + 232) == Crc16(record + 136, 92));
    free(ds);
    free(boxesDs);
    free(pc);
}

static void TestConvertsAndKeepsOriginal(void)
{
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE);
    PC_ImportDsSaves();
    assert(Matches("save1.bak", sDs, FLASH_SIZE));
    assert(!Exists("save1.bin.import-tmp"));
    AssertConverted(0x10);

    // A second launch sees a PC save, so it changes nothing and adds no backup.
    size_t size = 0;
    u8 *first = ReadWhole("save1.bin", &size);
    PC_ImportDsSaves();
    assert(Matches("save1.bin", first, size));
    assert(!Exists("save1.1.bak"));
    free(first);

    // Restoring the backup by renaming it imports it again.
    assert(rename("save1.bak", "save1.bin") == 0);
    PC_ImportDsSaves();
    assert(Matches("save1.bak", sDs, FLASH_SIZE));
    AssertConverted(0x10);
}

static void TestUsesNewestCopy(void)
{
    Clean();
    BuildDs(10, 11);
    Put("save1.bin", sDs, FLASH_SIZE);
    PC_ImportDsSaves();
    AssertConverted(0x40);

    // The counter rolled over, so 0 is newer than UINT32_MAX.
    Clean();
    BuildDs(0xFFFFFFFF, 0);
    Put("save1.bin", sDs, FLASH_SIZE);
    PC_ImportDsSaves();
    AssertConverted(0x40);
}

static void TestDesmumeFooter(void)
{
    Clean();
    BuildDs(10, 9);
    memcpy(sDs + FLASH_SIZE + 64 - DESMUME_FOOTER_SIZE, sDesmumeFooter, DESMUME_FOOTER_SIZE);
    Put("save1.bin", sDs, FLASH_SIZE + 64);
    PC_ImportDsSaves();
    assert(Matches("save1.bak", sDs, FLASH_SIZE + 64));
    AssertConverted(0x10);
}

static void TestLeavesOtherFilesAlone(void)
{
    static u8 other[FLASH_SIZE];

    Clean();
    Put("save1.bin", "short pc save", 13);
    PC_ImportDsSaves();
    assert(Matches("save1.bin", "short pc save", 13) && !Exists("save1.bak"));

    Clean();
    memset(other, 0x33, sizeof(other));
    Put("save1.bin", other, sizeof(other));
    PC_ImportDsSaves();
    assert(Matches("save1.bin", other, sizeof(other)) && !Exists("save1.bak"));

    // A slightly oversized file without an emulator footer is not a known format.
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE + 64);
    PC_ImportDsSaves();
    assert(Matches("save1.bin", sDs, FLASH_SIZE + 64) && !Exists("save1.bak"));

    // One damaged block with no matching partner cannot be imported.
    Clean();
    BuildDs(10, 9);
    sDs[0] ^= 1;
    sDs[HALF_SIZE] ^= 1;
    Put("save1.bin", sDs, FLASH_SIZE);
    PC_ImportDsSaves();
    assert(Matches("save1.bin", sDs, FLASH_SIZE) && !Exists("save1.bak"));
}

static void TestBackupsAreNeverReplaced(void)
{
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE);
    Put("save1.bak", "an older original", 17);
    PC_ImportDsSaves();
    assert(Matches("save1.bak", "an older original", 17));
    assert(Matches("save1.1.bak", sDs, FLASH_SIZE));
    AssertConverted(0x10);

    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE);
    Put("save1.bak", sDs, FLASH_SIZE);
    PC_ImportDsSaves();
    assert(!Exists("save1.1.bak"));
    AssertConverted(0x10);
}

static void TestFailuresKeepTheSlot(void)
{
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE);
    failRename = 1;
    PC_ImportDsSaves();
    assert(Matches("save1.bin", sDs, FLASH_SIZE));
    assert(!Exists("save1.bin.import-tmp"));

    // The next launch finishes the job with the backup already in place.
    failRename = 0;
    PC_ImportDsSaves();
    assert(Matches("save1.bak", sDs, FLASH_SIZE) && !Exists("save1.1.bak"));
    AssertConverted(0x10);

    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE);
    failSync = 1;
    PC_ImportDsSaves();
    assert(Matches("save1.bin", sDs, FLASH_SIZE));
    assert(!Exists("save1.bak") && !Exists("save1.bin.import-tmp"));

    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sDs, FLASH_SIZE);
    brokenLayout = 1;
    PC_ImportDsSaves();
    assert(Matches("save1.bin", sDs, FLASH_SIZE) && !Exists("save1.bak"));
}

int main(void)
{
    TestConvertsAndKeepsOriginal();
    TestUsesNewestCopy();
    TestDesmumeFooter();
    TestLeavesOtherFilesAlone();
    TestBackupsAreNeverReplaced();
    TestFailuresKeepTheSlot();
    puts("DS save import tests passed");
    return 0;
}
