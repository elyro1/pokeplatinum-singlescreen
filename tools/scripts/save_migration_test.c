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
#include "../../src/port/save_migration.c"
#undef rename
#undef fsync

#define ENTRY_SIZE(i) \
    static int EntrySize##i(void) { return DsEntrySize(i) + (brokenLayout && i == 3 ? 4 : 0); }
#define ENTRIES(X)                                                                  \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12)             \
    X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24)         \
    X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37)
ENTRIES(ENTRY_SIZE)

#define ENTRY_ROW(i) { .dataID = i, .sizeFunc = EntrySize##i },
const SaveTableEntry gSaveTable[] = { ENTRIES(ENTRY_ROW) };
const int gSaveTableSize = NELEMS(gSaveTable);

static int RecordingSize(void) { return RECORDING_DS_SIZE; }

const SaveTableEntry gExtraSaveTable[] = {
    [EXTRA_SAVE_TABLE_ENTRY_MY_RECORDINGS] = { .sizeFunc = RecordingSize },
    [EXTRA_SAVE_TABLE_ENTRY_DL_RECORDINGS_0] = { .sizeFunc = RecordingSize },
    [EXTRA_SAVE_TABLE_ENTRY_DL_RECORDINGS_1] = { .sizeFunc = RecordingSize },
    [EXTRA_SAVE_TABLE_ENTRY_DL_RECORDINGS_2] = { .sizeFunc = RecordingSize },
};
const int gExtraSaveTableSize = NELEMS(gExtraSaveTable);

static const char sDesmumeFooter[] = "|-DESMUME SAVE-|";
#define DESMUME_FOOTER_SIZE (sizeof(sDesmumeFooter) - 1)

static u8 sSave[FLASH_SIZE + 64];

static u8 Pattern(u8 fill, int i)
{
    return (u8)(fill + i * 7 + (i >> 8));
}

static void WriteBlock(u8 *block, int id, int bodySize, u32 counter, u32 blockCounter, u8 fill)
{
    for (int i = 0; i < bodySize; i++) {
        block[i] = Pattern(fill, i);
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

static void WriteRecording(u8 *record, int recordSize)
{
    for (int i = 0; i < recordSize; i++) {
        record[i] = (u8)(i * 3);
    }
    u8 *footer = record + recordSize;
    Write32(footer, SECTOR_SIGNATURE);
    Write32(footer + 4, 9);
    Write32(footer + 8, recordSize);
    Write16(footer + 12, RECORDING_FIRST_ID);
    Write16(footer + 14, Crc16(record, recordSize + EXTRA_FOOTER_SIZE - 2));
}

// Each half holds one complete save in the given layout; the fill byte tells
// the halves apart.
static void Build(int (*entrySize)(int), int recordSize, u32 counterA, u32 counterB)
{
    int sizes[BLOCK_COUNT];
    BlockSizes(entrySize, sizes);
    memset(sSave, 0xFF, sizeof(sSave));
    for (int half = 0; half < 2; half++) {
        u32 counter = half ? counterB : counterA;
        u8 fill = half ? 0x40 : 0x10;
        u8 *block = sSave + half * HALF_SIZE;
        WriteBlock(block, 0, sizes[0] - BLOCK_FOOTER_SIZE, counter, 5, fill);
        WriteBlock(block + sizes[0], 1, sizes[1] - BLOCK_FOOTER_SIZE, counter, 6, (u8)(fill + 3));
    }
    // A Hall of Fame byte pattern that the migration must carry over untouched.
    memset(sSave + SAVE_PAGE_MAX * SAVE_SECTOR_SIZE, 0x5A, 64);
    WriteRecording(sSave + sRecordingPages[0] * (size_t)SAVE_SECTOR_SIZE, recordSize);
}

static void BuildOld(u32 counterA, u32 counterB)
{
    Build(OldEntrySize, RECORDING_OLD_SIZE, counterA, counterB);
}

static void BuildDs(u32 counterA, u32 counterB)
{
    Build(DsEntrySize, RECORDING_DS_SIZE, counterA, counterB);
}

// The old backend only wrote as far as its last used address; here that is the
// first half's recording, so the second half was never written.
static size_t OldShortLength(void)
{
    return sRecordingPages[0] * (size_t)SAVE_SECTOR_SIZE + RECORDING_OLD_SIZE + EXTRA_FOOTER_SIZE;
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

static void AssertMigrated(u8 fill)
{
    int oldSizes[BLOCK_COUNT], dsSizes[BLOCK_COUNT];
    BlockSizes(OldEntrySize, oldSizes);
    BlockSizes(DsEntrySize, dsSizes);
    size_t size = 0;
    u8 *ds = ReadWhole("save1.bin", &size);
    assert(ds && size == FLASH_SIZE);

    BlockCopies found[BLOCK_COUNT];
    FindBlocks(ds, dsSizes, found);
    assert(found[0].count == 2 && found[1].count == 2);
    assert(memcmp(ds, ds + HALF_SIZE, dsSizes[0] + dsSizes[1]) == 0);
    FindBlocks(ds, oldSizes, found);
    assert(found[0].count == 0 && found[1].count == 0);

    // Rebuild the source bytes from the same fill the old blocks were made with.
    u8 *old = malloc(oldSizes[0]);
    u8 *boxesOld = malloc(oldSizes[1]);
    assert(old && boxesOld);
    for (int i = 0; i < oldSizes[0] - BLOCK_FOOTER_SIZE; i++) {
        old[i] = Pattern(fill, i);
    }
    for (int i = 0; i < oldSizes[1] - BLOCK_FOOTER_SIZE; i++) {
        boxesOld[i] = Pattern((u8)(fill + 3), i);
    }

    // SystemData loses the alignment gaps at 52 and 76 and its tail padding.
    assert(memcmp(ds, old, 52) == 0);
    assert(memcmp(ds + 52, old + 56, 20) == 0);
    assert(memcmp(ds + 72, old + 80, 20) == 0);
    assert(Read16(ds + PageSize(92) - 4) == Crc16(ds, PageSize(92) - 4));

    // Unchanged entries are copied as they are.
    int dsOffset = PageSize(92), oldOffset = PageSize(104);
    assert(memcmp(ds + dsOffset, old + oldOffset, sDsSizes[1]) == 0);

    for (int i = 1; i < 8; i++) {
        dsOffset += PageSize(sDsSizes[i]);
        oldOffset += PageSize(OldEntrySize(i));
    }
    assert(memcmp(ds + dsOffset, old + oldOffset, 236) == 0);
    assert(memcmp(ds + dsOffset + 236, old + oldOffset + 240, 236) == 0);
    assert(memcmp(ds + dsOffset + 472, old + oldOffset + 480, 8) == 0);

    const u8 *boxes = ds + dsSizes[0];
    assert(memcmp(boxes, boxesOld, 4) == 0);
    assert(memcmp(boxes + 4, boxesOld + 8, 74179) == 0);

    assert(ds[SAVE_PAGE_MAX * SAVE_SECTOR_SIZE] == 0x5A);

    const u8 *record = ds + sRecordingPages[0] * (size_t)SAVE_SECTOR_SIZE;
    for (int i = 0; i < 132; i++) {
        assert(record[i] == (u8)(i * 3));
    }
    for (int i = 0; i < 96; i++) {
        assert(record[132 + i] == (u8)((136 + i) * 3));
    }
    for (int i = 232; i < RECORDING_DS_SIZE; i++) {
        assert(record[i] == (u8)((i + 8) * 3));
    }
    assert(Read16(record + 228) == Crc16(record + 132, 88));
    assert(Read32(record + RECORDING_DS_SIZE + 8) == RECORDING_DS_SIZE);
    assert(Read16(record + RECORDING_DS_SIZE + 14) == Crc16(record, RECORDING_DS_SIZE + EXTRA_FOOTER_SIZE - 2));
    free(old);
    free(boxesOld);
    free(ds);
}

static void TestMigratesAndKeepsOriginal(void)
{
    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    PC_MigrateSaves();
    assert(Matches("save1.bak", sSave, FLASH_SIZE));
    assert(!Exists("save1.bin.import-tmp"));
    AssertMigrated(0x10);

    // A second launch sees a DS-layout save, so it changes nothing and adds no backup.
    size_t size = 0;
    u8 *first = ReadWhole("save1.bin", &size);
    PC_MigrateSaves();
    assert(Matches("save1.bin", first, size));
    assert(!Exists("save1.1.bak"));
    free(first);

    // Restoring the backup by renaming it migrates it again.
    assert(rename("save1.bak", "save1.bin") == 0);
    PC_MigrateSaves();
    assert(Matches("save1.bak", sSave, FLASH_SIZE));
    AssertMigrated(0x10);
}

static void TestMigratesShortSave(void)
{
    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, OldShortLength());
    PC_MigrateSaves();
    assert(Matches("save1.bak", sSave, OldShortLength()));
    AssertMigrated(0x10);
}

static void TestUsesNewestCopy(void)
{
    Clean();
    BuildOld(10, 11);
    Put("save1.bin", sSave, FLASH_SIZE);
    PC_MigrateSaves();
    AssertMigrated(0x40);

    // The counter rolled over, so 0 is newer than UINT32_MAX.
    Clean();
    BuildOld(0xFFFFFFFF, 0);
    Put("save1.bin", sSave, FLASH_SIZE);
    PC_MigrateSaves();
    AssertMigrated(0x40);
}

static void TestDsSavesAreUsedAsTheyAre(void)
{
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE) && !Exists("save1.bak"));

    // An emulator's trailer is kept; the game never reads or writes past the flash.
    Clean();
    BuildDs(10, 9);
    memcpy(sSave + FLASH_SIZE + 64 - DESMUME_FOOTER_SIZE, sDesmumeFooter, DESMUME_FOOTER_SIZE);
    Put("save1.bin", sSave, FLASH_SIZE + 64);
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE + 64) && !Exists("save1.bak"));
}

static void TestPadsShortDsLayoutSave(void)
{
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sSave, HALF_SIZE / 2);
    // What reads already returned: the written bytes, then erased flash.
    memset(sSave + HALF_SIZE / 2, 0xFF, FLASH_SIZE - HALF_SIZE / 2);
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE) && !Exists("save1.bak"));
    assert(!Exists("save1.bin.import-tmp"));
}

static void TestLeavesOtherFilesAlone(void)
{
    static u8 other[FLASH_SIZE];

    Clean();
    Put("save1.bin", "short unknown", 13);
    PC_MigrateSaves();
    assert(Matches("save1.bin", "short unknown", 13) && !Exists("save1.bak"));

    Clean();
    memset(other, 0x33, sizeof(other));
    Put("save1.bin", other, sizeof(other));
    PC_MigrateSaves();
    assert(Matches("save1.bin", other, sizeof(other)) && !Exists("save1.bak"));

    // One damaged block with no matching partner cannot be migrated.
    Clean();
    BuildOld(10, 9);
    sSave[0] ^= 1;
    sSave[HALF_SIZE] ^= 1;
    Put("save1.bin", sSave, FLASH_SIZE);
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE) && !Exists("save1.bak"));

    // Anything bigger than a flash dump with an emulator trailer is not a save.
    static u8 big[FLASH_SIZE + DESMUME_MAX_EXTRA + 1];
    Clean();
    BuildOld(10, 9);
    memcpy(big, sSave, FLASH_SIZE);
    Put("save1.bin", big, sizeof(big));
    PC_MigrateSaves();
    assert(Matches("save1.bin", big, sizeof(big)) && !Exists("save1.bak"));
}

static void TestBackupsAreNeverReplaced(void)
{
    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    Put("save1.bak", "an older original", 17);
    PC_MigrateSaves();
    assert(Matches("save1.bak", "an older original", 17));
    assert(Matches("save1.1.bak", sSave, FLASH_SIZE));
    AssertMigrated(0x10);

    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    Put("save1.bak", sSave, FLASH_SIZE);
    PC_MigrateSaves();
    assert(!Exists("save1.1.bak"));
    AssertMigrated(0x10);
}

static void TestFailuresKeepTheSlot(void)
{
    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    failRename = 1;
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE));
    assert(!Exists("save1.bin.import-tmp"));

    // The next launch finishes the job with the backup already in place.
    failRename = 0;
    PC_MigrateSaves();
    assert(Matches("save1.bak", sSave, FLASH_SIZE) && !Exists("save1.1.bak"));
    AssertMigrated(0x10);

    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    failSync = 1;
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE));
    assert(!Exists("save1.bak") && !Exists("save1.bin.import-tmp"));

    Clean();
    BuildOld(10, 9);
    Put("save1.bin", sSave, FLASH_SIZE);
    brokenLayout = 1;
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, FLASH_SIZE) && !Exists("save1.bak"));

    // A failed pad leaves the short file as it was.
    Clean();
    BuildDs(10, 9);
    Put("save1.bin", sSave, HALF_SIZE / 2);
    failRename = 1;
    PC_MigrateSaves();
    assert(Matches("save1.bin", sSave, HALF_SIZE / 2) && !Exists("save1.bin.import-tmp"));
}

int main(void)
{
    TestMigratesAndKeepsOriginal();
    TestMigratesShortSave();
    TestUsesNewestCopy();
    TestDsSavesAreUsedAsTheyAre();
    TestPadsShortDsLayoutSave();
    TestLeavesOtherFilesAlone();
    TestBackupsAreNeverReplaced();
    TestFailuresKeepTheSlot();
    Clean();
    puts("Save migration tests passed");
    return 0;
}
