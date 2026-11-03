#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "gfl/heap.h"
#include "gfl/std.h"
#include "nitro/mi.h"
#include "nnsys/fnd.h"
#include <nnsys/g2d/load/g2d_NAN_load.h>
#include <nnsys/g2d/load/g2d_NCE_load.h>
#include <nnsys/g2d/load/g2d_NCG_load.h>
#include <nnsys/g2d/load/g2d_NMC_load.h>

// BW2's heaps (gfl/heap.h) on the host's allocator. The view only allocates and frees; heap IDs, the tail flag
// (HEAPID_TAIL) and DTCM all come from the same place on PC. A header before each block keeps its size for
// GFL_HeapResize and checks GFL_HeapFree's pointer.

#define BW_HEAP_MAGIC 0x42574850 // BWHP
#define BW_HEAP_ALIGN 32

typedef struct {
    void *block; // What malloc returned
    u32 magic;
    u32 size;
    HeapID heapId;
} BwHeapHeader;

static BwHeapHeader *HeaderOf(void *ptr) {
    return (BwHeapHeader *)((u8 *)ptr - sizeof(BwHeapHeader));
}

// Blocks are aligned like the DS heap's, for the textures and buffers the SDK copies from
static void *BwHeap_Alloc(HeapID heapId, u32 size, u32 align) {
    u8 *block;
    uintptr_t ptr;
    BwHeapHeader *header;

    if (align < BW_HEAP_ALIGN) {
        align = BW_HEAP_ALIGN;
    }
    block = malloc(sizeof(BwHeapHeader) + align + size);
    if (block == NULL) {
        return NULL;
    }
    ptr = ((uintptr_t)block + sizeof(BwHeapHeader) + align - 1) / align * align;
    header = HeaderOf((void *)ptr);
    header->block = block;
    header->magic = BW_HEAP_MAGIC;
    header->size = size;
    header->heapId = heapId & (HEAPID_TAIL_BIT - 1);
    return (void *)ptr;
}

// The port's NitroSystem copies the banks of a 2D file into native structs (64-bit pointers), registered by where
// the pointer to them is kept, and frees them when a heap block holding that pointer is freed (expheap.c does this
// for Platinum's heaps). Each check frees one bank, and a block can hold several of a kind (MCSS keeps a cell
// animation bank and a multi-cell animation bank)
#define BW_HEAP_BANKS_PER_KIND 4

static void BwHeap_FreeUnpackedBanks(void *start, void *end) {
    for (int i = 0; i < BW_HEAP_BANKS_PER_KIND; i++) {
        WIN_CheckAndFreeAnimBank(start, end);
        WIN_CheckAndFreeCellBank(start, end);
        WIN_CheckAndFreeCharData(start, end);
        WIN_CheckAndFreeMultiCellBank(start, end);
    }
}

void *GFL_HeapAllocate(HeapID heapId, u32 size, BOOL clear, const char *file, u16 line) {
    void *ptr = BwHeap_Alloc(heapId, size, BW_HEAP_ALIGN);

    (void)file;
    (void)line;
    if (ptr != NULL && clear) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void GFL_HeapFree(void *ptr) {
    BwHeapHeader *header;

    if (ptr == NULL) {
        return;
    }
    header = HeaderOf(ptr);
    GFL_ASSERT(header->magic == BW_HEAP_MAGIC);
    // The checks take the pointers strictly inside the range (expheap.c gives them the block's header first), so it
    // starts before the block: a bank kept at the block's start is freed with it too
    BwHeap_FreeUnpackedBanks((u8 *)ptr - 1, (u8 *)ptr + header->size);
    header->magic = 0;
    free(header->block);
}

// The DS heap shrinks the block in place. A host block can't shrink in place, so it keeps its size: callers only
// shrink, and only to give the memory back
void GFL_HeapResize(void *ptr, u32 size) {
    (void)ptr;
    (void)size;
}

void *GFL_HeapDTCMAllocate(u32 size) {
    return BwHeap_Alloc(HEAPID_SYSTEM, size, BW_HEAP_ALIGN);
}

void _freeBlkFromDTCM(void *ptr) {
    GFL_HeapFree(ptr);
}

static void *BwHeap_AllocatorAlloc(NNSFndAllocator *allocator, u32 size) {
    return BwHeap_Alloc((HeapID)(uintptr_t)allocator->pHeap, size, allocator->heapParam1);
}

static void BwHeap_AllocatorFree(NNSFndAllocator *allocator, void *ptr) {
    (void)allocator;
    GFL_HeapFree(ptr);
}

static const NNSFndAllocatorFunc sAllocatorFunc = { BwHeap_AllocatorAlloc, BwHeap_AllocatorFree };

void GFL_HeapCreateAllocator(NNSFndAllocator *allocator, HeapID heapId, int alignment) {
    allocator->pFunc = &sAllocatorFunc;
    allocator->pHeap = (void *)(uintptr_t)heapId;
    allocator->heapParam1 = alignment < 0 ? -alignment : alignment;
    allocator->heapParam2 = 0;
}
