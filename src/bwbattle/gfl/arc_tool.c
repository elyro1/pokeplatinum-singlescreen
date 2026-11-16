#include "types.h"
#include "assets/bw2_assets.h"
#include "shim/bw_shim.h"
#include "gfl/arc.h"
#include "gfl/heap.h"
#include "gfl/std.h"
#include "nitro/fs.h"

// A NARC starts with a header whose size is at offset 0xc. The blocks follow it: FATB, the start and end of each file
// in FIMG, then FNTB and FIMG. Each block starts with its magic and size
#define NARC_HEADER_SIZE_OFFSET 0xc
#define NARC_BLOCK_SIZE_OFFSET 4
#define NARC_FATB_ENTRIES_OFFSET 0xc
#define NARC_FIMG_DATA_OFFSET 8

typedef struct {
    u32 size;
    u16 fileCount;
} NarcFATBHeader;

typedef struct {
    u32 start;
    u32 end;
} NarcFATBEntry;

struct ArcTool {
    FSFile file;
    u32 fimgOffset;
    u16 fatbOffset;
    u16 fileCount;
};

typedef struct {
    // The path of each archive, or the start and end of each in memory
    const void *table;
    u32 count;
    BOOL inMemory;
} ArcSys;

static void GFL_ArcSysReadImpl(void *dest, u32 arcId, u32 fileId, u32 offset, u32 size);
static void *GFL_ArcSysReadHeapNewImpl(u32 arcId, u32 fileId, HeapID heapId, u32 offset, u32 size);
static void GFL_ArcSysInitArcHandle(FSFile *file, u32 arcId);
static u32 ArchiveMoveImageTop(FSFile *file, u32 fileId, u32 offset, u32 size);
static void GFL_ArcToolInit(ArcTool *handle);

static ArcSys sArcSys;

void GFL_ArcSysInit(const char **paths, u32 count) {
    sArcSys.table = paths;
    sArcSys.count = count;
    sArcSys.inMemory = FALSE;
}

static void GFL_ArcSysReadImpl(void *dest, u32 arcId, u32 fileId, u32 offset, u32 size) {
    FSFile file;

    GFL_ArcSysInitArcHandle(&file, arcId);
    size = ArchiveMoveImageTop(&file, fileId, offset, size);
    FS_ReadFile(&file, dest, size);
    FS_CloseFile(&file);
}

static void *GFL_ArcSysReadHeapNewImpl(u32 arcId, u32 fileId, HeapID heapId, u32 offset, u32 size) {
    FSFile file;
    u32 dataSize;
    void *data;

    GFL_ArcSysInitArcHandle(&file, arcId);
    dataSize = ArchiveMoveImageTop(&file, fileId, offset, size);
    data = GFL_HeapAllocate(heapId, dataSize, FALSE, "arc_tool.c", 187);
    FS_ReadFile(&file, data, dataSize);
    FS_CloseFile(&file);
    return data;
}

static void GFL_ArcSysInitArcHandle(FSFile *file, u32 arcId) {
    FS_InitFile(file);
    // bwbattle: archives always come from the files extracted from the BW2 ROM. BW2's other case, a table of archives
    // in memory, held their 32-bit addresses. An archive bwbattle doesn't extract is a bug: say which before the
    // port's file system stops the game for it (bw2_assets.c lists the archives)
    if (!BwAssets_ArcExists(arcId)) {
        BwShim_Log("BW2 archive %u isn't extracted", arcId);
    }
    FS_OpenFile(file, ((const char **)sArcSys.table)[arcId]);
}

// Seeks to the data of a file of an archive, at offset into it, and returns its size, or size if it is not 0
static u32 ArchiveMoveImageTop(FSFile *file, u32 fileId, u32 offset, u32 size) {
    NarcFATBEntry entry;
    NarcFATBHeader fatb;
    u32 value = 0;
    u32 fimgOffset;
    u32 fntbOffset;
    u32 fatbOffset;

    FS_SeekFile(file, NARC_HEADER_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(file, &value, 2);
    fatbOffset = value;
    FS_SeekFile(file, fatbOffset + NARC_BLOCK_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(file, &fatb, 6);
    GFL_ASSERT_MSG(fatb.fileCount > fileId, "ArchiveMoveImageTop fileCnt=%d, datID=%d", fatb.fileCount, fileId);
    fntbOffset = fatbOffset + fatb.size;
    FS_SeekFile(file, fntbOffset + NARC_BLOCK_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(file, &value, 4);
    fimgOffset = fntbOffset + value;
    FS_SeekFile(file, fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry), FS_SEEK_SET);
    FS_ReadFile(file, &entry, sizeof(NarcFATBEntry));
    fimgOffset += NARC_FIMG_DATA_OFFSET;
    FS_SeekFile(file, offset + (fimgOffset + entry.start), FS_SEEK_SET);
    if (size == 0) {
        size = entry.end - entry.start;
    }
    return size;
}

void GFL_ArcSysRead(void *dest, u32 arcId, u32 fileId) {
    GFL_ArcSysReadImpl(dest, arcId, fileId, 0, 0);
}

void *GFL_ArcSysReadHeapNew(u32 arcId, u32 fileId, HeapID heapId) {
    return GFL_ArcSysReadHeapNewImpl(arcId, fileId, heapId, 0, 0);
}

void GFL_ArcSysReadRange(void *dest, u32 arcId, u32 fileId, u32 offset, u32 size) {
    GFL_ArcSysReadImpl(dest, arcId, fileId, offset, size);
}

void *GFL_ArcSysReadHeapNewRange(u32 arcId, u32 fileId, HeapID heapId, u32 offset, u32 size) {
    return GFL_ArcSysReadHeapNewImpl(arcId, fileId, heapId, offset, size);
}

void *GFL_ArcSysReadHeapNewDirect(const char *path, u32 fileId, HeapID heapId) {
    FSFile file;
    u32 size;
    void *data;

    FS_InitFile(&file);
    FS_OpenFile(&file, path);
    size = ArchiveMoveImageTop(&file, fileId, 0, 0);
    data = GFL_HeapAllocate(heapId, size, FALSE, "arc_tool.c", 386);
    FS_ReadFile(&file, data, size);
    FS_CloseFile(&file);
    return data;
}

u32 GFL_ArcSysGetDataMax(u32 arcId) {
    FSFile file;
    u32 value = 0;
    NarcFATBHeader fatb;

    GFL_ArcSysInitArcHandle(&file, arcId);
    FS_SeekFile(&file, NARC_HEADER_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(&file, &value, 2);
    FS_SeekFile(&file, value + NARC_BLOCK_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(&file, &fatb, 6);
    FS_CloseFile(&file);
    return fatb.fileCount;
}

u32 GFL_ArcSysGetDataLength(u32 arcId, u32 fileId) {
    FSFile file;
    u32 size;

    GFL_ArcSysInitArcHandle(&file, arcId);
    size = ArchiveMoveImageTop(&file, fileId, 0, 0);
    FS_CloseFile(&file);
    return size;
}

ArcTool *GFL_ArcSysCreateFileHandle(u32 arcId, HeapID heapId) {
    ArcTool *handle = GFL_HeapAllocate(heapId, sizeof(ArcTool), FALSE, "arc_tool.c", 521);

    GFL_ArcSysInitArcHandle(&handle->file, arcId);
    GFL_ArcToolInit(handle);
    return handle;
}

ArcTool *GFL_ArcSysCreateMemoryHandle(const void *data, u32 size, HeapID heapId) {
    ArcTool *handle = GFL_HeapAllocate(heapId, sizeof(ArcTool), FALSE, "arc_tool.c", 543);

    FS_InitFile(&handle->file);
    // The SDK takes the buffer as non-const but only reads it
    FS_CreateFileFromMemory(&handle->file, (void *)data, size);
    GFL_ArcToolInit(handle);
    return handle;
}

static void GFL_ArcToolInit(ArcTool *handle) {
    u32 fntbSize;
    NarcFATBHeader fatb;
    u32 fntbOffset;

    handle->fatbOffset = 0;
    FS_SeekFile(&handle->file, NARC_HEADER_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(&handle->file, &handle->fatbOffset, 2);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_BLOCK_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(&handle->file, &fatb, 6);
    handle->fileCount = fatb.fileCount;
    fntbOffset = handle->fatbOffset + fatb.size;
    FS_SeekFile(&handle->file, fntbOffset + NARC_BLOCK_SIZE_OFFSET, FS_SEEK_SET);
    FS_ReadFile(&handle->file, &fntbSize, 4);
    handle->fimgOffset = fntbOffset + fntbSize;
}

void GFL_ArcToolFree(ArcTool *handle) {
    FS_CloseFile(&handle->file);
    GFL_HeapFree(handle);
}

void *GFL_ArcToolReadHeapNew(ArcTool *handle, u32 fileId, HeapID heapId) {
    NarcFATBEntry entry;
    void *data;

    GFL_ASSERT_MSG(handle->fileCount > fileId, "DatCount=%d, DatID=%d", handle->fileCount, fileId);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry),
                FS_SEEK_SET);
    FS_ReadFile(&handle->file, &entry, sizeof(NarcFATBEntry));
    FS_SeekFile(&handle->file, handle->fimgOffset + NARC_FIMG_DATA_OFFSET + entry.start, FS_SEEK_SET);
    data = GFL_HeapAllocate(heapId, entry.end - entry.start, FALSE, "arc_tool.c", 619);
    if (data != NULL) {
        FS_ReadFile(&handle->file, data, entry.end - entry.start);
    }
    return data;
}

void GFL_ArcToolRead(ArcTool *handle, u32 fileId, void *dest) {
    NarcFATBEntry entry;

    GFL_ASSERT_MSG(handle->fileCount > fileId, "DatCount=%d, DatID=%d", handle->fileCount, fileId);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry),
                FS_SEEK_SET);
    FS_ReadFile(&handle->file, &entry, sizeof(NarcFATBEntry));
    FS_SeekFile(&handle->file, handle->fimgOffset + NARC_FIMG_DATA_OFFSET + entry.start, FS_SEEK_SET);
    FS_ReadFile(&handle->file, dest, entry.end - entry.start);
}

u32 GFL_ArcToolGetDataLength(ArcTool *handle, u32 fileId) {
    NarcFATBEntry entry;

    GFL_ASSERT_MSG(handle->fileCount > fileId, "DatCount=%d, DatID=%d", handle->fileCount, fileId);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry),
                FS_SEEK_SET);
    FS_ReadFile(&handle->file, &entry, sizeof(NarcFATBEntry));
    return entry.end - entry.start;
}

void GFL_ArcToolReadRange(ArcTool *handle, u32 fileId, u32 offset, u32 size, void *dest) {
    u32 start;

    GFL_ASSERT_MSG(handle->fileCount > fileId, "DatCount=%d, DatID=%d", handle->fileCount, fileId);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry),
                FS_SEEK_SET);
    FS_ReadFile(&handle->file, &start, 4);
    FS_SeekFile(&handle->file, offset + (handle->fimgOffset + NARC_FIMG_DATA_OFFSET + start), FS_SEEK_SET);
    FS_ReadFile(&handle->file, dest, size);
}

void GFL_ArcToolCopyDataOfs(ArcTool *handle, u32 fileId, u32 *offset) {
    u32 start;

    GFL_ASSERT_MSG(handle->fileCount > fileId, "DatCount=%d, DatID=%d", handle->fileCount, fileId);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry),
                FS_SEEK_SET);
    FS_ReadFile(&handle->file, &start, 4);
    *offset = handle->fimgOffset + NARC_FIMG_DATA_OFFSET + start;
}

u32 GFL_ArcToolGetDataOfs(ArcTool *handle, u32 fileId) {
    u32 start;

    GFL_ASSERT_MSG(handle->fileCount > fileId, "DatCount=%d, DatID=%d", handle->fileCount, fileId);
    FS_SeekFile(&handle->file, handle->fatbOffset + NARC_FATB_ENTRIES_OFFSET + fileId * sizeof(NarcFATBEntry),
                FS_SEEK_SET);
    FS_ReadFile(&handle->file, &start, 4);
    return handle->fimgOffset + NARC_FIMG_DATA_OFFSET + start;
}

void GFL_ArcToolReadRaw(ArcTool *handle, u32 size, void *dest) {
    FS_ReadFile(&handle->file, dest, size);
}

void GFL_ArcToolSeek(ArcTool *handle, u32 offset) {
    FS_SeekFile(&handle->file, offset, FS_SEEK_SET);
}

u32 GFL_ArcToolGetDataMax(ArcTool *handle) {
    return handle->fileCount;
}
