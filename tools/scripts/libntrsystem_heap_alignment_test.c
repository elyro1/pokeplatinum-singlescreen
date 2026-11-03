#include <nitro.h>
#include <nnsys/fnd/allocator.h>
#include <nnsys/fnd/expheap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the real heap/list/allocator code without loading graphics resources. */
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            fprintf(stderr, "heap check line %d: %s\n", __LINE__, #c); \
            exit(1);                                                   \
        }                                                              \
    } while (0)
static unsigned frees;
static unsigned cellFrees, charFrees, multiCellFrees;
/* Other allocator-table entries remain linked by sanitizer metadata. */
void *NNS_FndAllocFromFrmHeapEx(NNSFndHeapHandle h, u32 size, int alignment)
{
    abort();
}
void *NNS_FndAllocFromUnitHeap(NNSFndHeapHandle h)
{
    abort();
}
void NNS_FndFreeToUnitHeap(NNSFndHeapHandle h, void *p)
{
    abort();
}
void *OS_AllocFromHeap(OSArenaId id, OSHeapHandle h, u32 size)
{
    abort();
}
void OS_FreeToHeap(OSArenaId id, OSHeapHandle h, void *p)
{
    abort();
}
void WIN_CheckAndFreeAnimBank(void *a, void *b)
{
    ++frees;
}
void WIN_CheckAndFreeCellBank(void *a, void *b)
{
    ++cellFrees;
}
void WIN_CheckAndFreeCharData(void *a, void *b)
{
    ++charFrees;
}
void WIN_CheckAndFreeMultiCellBank(void *a, void *b)
{
    ++multiCellFrees;
}
void SIM_handleAssertionFailure(const char *f, unsigned int n, const char *e)
{
    abort();
}
void SIM_handleAssertionFailureMsg(const char *f, unsigned int n, const char *fmt, ...)
{
    abort();
}
void OS_Terminate(void)
{
    fprintf(stderr, "native debug assertion failed\n");
    abort();
}

static NNSiFndExpHeapMBlockHead *Header(void *p)
{
    return (NNSiFndExpHeapMBlockHead *)((u8 *)p - sizeof(NNSiFndExpHeapMBlockHead));
}
static void Validate(NNSFndHeapHandle heap)
{
    CHECK((uintptr_t)heap % _Alignof(NNSiFndHeapHead) == 0);
    NNSiFndExpHeapHead *e = (void *)((u8 *)heap + sizeof(*heap));
    CHECK((uintptr_t)e % _Alignof(NNSiFndExpHeapHead) == 0);
    NNSiFndExpMBlockList *lists[] = { &e->mbFreeList, &e->mbUsedList };
    for (int i = 0; i < 2; i++) {
        NNSiFndExpHeapMBlockHead *previous = NULL;
        unsigned count = 0;
        for (NNSiFndExpHeapMBlockHead *p = lists[i]->head; p; p = p->pMBHeadNext) {
            CHECK(++count < 10000);
            CHECK((uintptr_t)p % _Alignof(NNSiFndExpHeapMBlockHead) == 0);
            CHECK(p->pMBHeadPrev == previous);
            CHECK((u8 *)p >= (u8 *)heap->heapStart
                && (u8 *)(p + 1) + p->blockSize <= (u8 *)heap->heapEnd);
            previous = p;
        }
        CHECK(previous == lists[i]->tail);
    }
    CHECK(NNS_FndCheckExpHeap(heap, 0));
}
static _Alignas(64) u8 memory[65536];
static unsigned bufferOffset;
static NNSFndHeapHandle NewHeap(unsigned offset)
{
    bufferOffset = offset;
    memset(memory, 0xa5, sizeof(memory));
    NNSFndHeapHandle h = NNS_FndCreateExpHeapEx(
        memory + 64 + offset, sizeof(memory) - 128 - offset, NNS_FND_HEAP_OPT_0_CLEAR);
    CHECK(h);
    return h;
}
static void Guards(void)
{
    for (unsigned i = 0; i < 64 + bufferOffset; i++) {
        CHECK(memory[i] == 0xa5);
    }
    for (unsigned i = sizeof(memory) - 64; i < sizeof(memory); i++) {
        CHECK(memory[i] == 0xa5);
    }
}
static void Sweep(void)
{
    const int aligns[] = { 4, -4, 8, -8, 16, -16, 32, -32 };
    for (unsigned offset = 0; offset < 16; offset++) {
        for (unsigned a = 0; a < 8; a++) {
            for (int mode = 0; mode < 2; mode++) {
                NNSFndHeapHandle h = NewHeap(offset);
                Validate(h);
                NNS_FndSetAllocModeForExpHeap(h, mode);
                u32 initial = NNS_FndGetTotalFreeSizeForExpHeap(h);
                void *blocks[64];
                for (u32 size = 0; size < 64; size++) {
                    void *p = blocks[size] = NNS_FndAllocFromExpHeapEx(h, size, aligns[a]);
                    CHECK(p && (uintptr_t)p % (unsigned)abs(aligns[a]) == 0
                        && (uintptr_t)p % sizeof(void *) == 0);
                    CHECK(NNS_FndGetAllocDirForMBlockExpHeap(p) == (aligns[a] < 0));
                    u32 capacity = Header(p)->blockSize;
                    CHECK(capacity >= (size ? size : 1));
                    for (u32 i = 0; i < capacity; i++) {
                        CHECK(((u8 *)p)[i] == 0);
                    }
                    memset(p, (int)size, size);
                    Validate(h);
                }
                for (int i = 0; i < 64; i++) {
                    int slot = i % 2 == 0 ? i / 2 : 63 - i / 2;
                    for (int j = 0; j < slot; j++) {
                        CHECK(((u8 *)blocks[slot])[j] == slot);
                    }
                    NNS_FndFreeToExpHeap(h, blocks[slot]);
                    Validate(h);
                }
                CHECK(NNS_FndGetTotalFreeSizeForExpHeap(h) == initial);
                u32 maximum = NNS_FndGetAllocatableSizeForExpHeapEx(h, aligns[a]);
                void *p = NNS_FndAllocFromExpHeapEx(h, maximum, aligns[a]);
                CHECK(p);
                Validate(h);
                CHECK(NNS_FndAllocFromExpHeapEx(h, 1, aligns[a]) == NULL);
                NNS_FndFreeToExpHeap(h, p);
                Validate(h);
                CHECK(NNS_FndGetTotalFreeSizeForExpHeap(h) == initial);
                NNS_FndDestroyExpHeap(h);
                Guards();
            }
        }
    }
}
static void Resize(void)
{
    for (u32 size = 1; size <= 63; size++) {
        NNSFndHeapHandle h = NewHeap(0);
        u32 initial = NNS_FndGetTotalFreeSizeForExpHeap(h);
        u8 *p = NNS_FndAllocFromExpHeapEx(h, 512, 4);
        CHECK(p);
        memset(p, 0x42, 512);
        CHECK(NNS_FndResizeForMBlockExpHeap(h, p, size) >= size);
        Validate(h);
        for (u32 i = 0; i < size; i++) {
            CHECK(p[i] == 0x42);
        }
        CHECK(NNS_FndResizeForMBlockExpHeap(h, p, 512) >= 512);
        Validate(h);
        for (u32 i = 0; i < size; i++) {
            CHECK(p[i] == 0x42);
        }
        u32 before = Header(p)->blockSize;
        CHECK(NNS_FndResizeForMBlockExpHeap(h, p, sizeof(memory) * 2) == 0);
        CHECK(Header(p)->blockSize == before);
        Validate(h);
        NNS_FndFreeToExpHeap(h, p);
        CHECK(NNS_FndGetTotalFreeSizeForExpHeap(h) == initial);
        NNS_FndDestroyExpHeap(h);
        Guards();
    }
}
static void Adapter(void)
{
    const int alignments[] = { 4, -4, 8, -8, 16, -16, 32, -32 };
    for (unsigned a = 0; a < 8; a++) {
        NNSFndHeapHandle h = NewHeap(1);
        NNSFndAllocator allocator;
        NNS_FndInitAllocatorForExpHeap(&allocator, h, alignments[a]);
        u32 initial = NNS_FndGetTotalFreeSizeForExpHeap(h);
        for (u32 size = 0; size < 64; size++) {
            void *p = NNS_FndAllocFromAllocator(&allocator, size);
            CHECK(p && (uintptr_t)p % (unsigned)abs(alignments[a]) == 0
                && (uintptr_t)p % sizeof(void *) == 0);
            memset(p, 0x5a, size);
            Validate(h);
            NNS_FndFreeToAllocator(&allocator, p);
            Validate(h);
            CHECK(NNS_FndGetTotalFreeSizeForExpHeap(h) == initial);
        }
        NNS_FndDestroyExpHeap(h);
        Guards();
    }
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "create")) {
        NNSFndHeapHandle h = NewHeap(1);
        Validate(h);
        NNS_FndDestroyExpHeap(h);
        Guards();
        puts("creation aligned");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "split")) {
        NNSFndHeapHandle h = NewHeap(0);
        void *p = NNS_FndAllocFromExpHeapEx(h, 1, 4);
        CHECK(p);
        Validate(h);
        NNS_FndFreeToExpHeap(h, p);
        NNS_FndDestroyExpHeap(h);
        Guards();
        puts("split aligned");
        return 0;
    }
    Sweep();
    Resize();
    Adapter();
    CHECK(frees == 17215);
    CHECK(cellFrees == frees && charFrees == frees && multiCellFrees == frees);
    printf("PASS: 256 heap/alignment/mode sweeps; 16384 small allocations; 63 shrink/grow cases; "
           "512 allocator-adapter calls; %u cleanup calls\n",
        frees);
}
