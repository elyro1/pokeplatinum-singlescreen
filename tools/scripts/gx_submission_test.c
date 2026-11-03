// Actual SDK submissions and NitroSystem caller; instrument the decoder boundary.
#include <nitro.h>
#include <nnsys/g3d/gecom.h>
#include <simulator/drawmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "GX submission check failed at line %d: %s\n", __LINE__, #condition); \
    exit(1); \
} } while (0)

u32 GXi_DmaId = 3;
static unsigned decoded, callbacks;
static const void *expectedSource;
static void *expectedArg;
static u32 expectedSize;
static BOOL reenter;

void SIM_HandleG3Command(draw_msg_t *msg)
{
    CHECK(msg->type == DRAW_CMD_G3_CMD_LIST && msg->size == expectedSize);
    CHECK(msg->data.ptr != expectedSource);
    CHECK(memcmp(msg->data.ptr, expectedSource, expectedSize) == 0);
    ++decoded;
    // The production decoder owns the copy; the caller still owns its input.
    free(msg->data.ptr);
}
void SIM_handleAssertionFailure(const char *file, unsigned int line, const char *expr) { abort(); }
void SIM_handleAssertionFailureMsg(const char *file, unsigned int line, const char *fmt, ...) { abort(); }

// These boundaries keep the old implementation linkable for regression checks.
// Native submission must not wait for hardware, install IRQs or program DMA.
void MIi_CheckDma0SourceAddress(u32 dma, u32 src, u32 size, u32 dir) {}
void MIi_CheckAnotherAutoDMA(u32 dma, u32 type) { CHECK(FALSE); }
void MIi_DmaSetParams(u32 dma, u32 src, u32 dest, u32 ctrl) { CHECK(FALSE); }
void MI_WaitDma(u32 dma) { CHECK(FALSE); }
void OSi_EnterDmaCallback(u32 dma, void (*cb)(void *), void *arg) { CHECK(FALSE); }
OSIntrMode OS_DisableInterrupts(void) { CHECK(FALSE); return 0; }
OSIntrMode OS_RestoreInterrupts(OSIntrMode state) { CHECK(FALSE); return 0; }
OSIrqMask OS_EnableIrqMask(OSIrqMask mask) { CHECK(FALSE); return 0; }
OSIrqMask OS_DisableIrqMask(OSIrqMask mask) { CHECK(FALSE); return 0; }
OSIrqMask OS_ResetRequestIrqMask(OSIrqMask mask) { CHECK(FALSE); return 0; }
void OS_SetIrqFunction(OSIrqMask mask, OSIrqFunction fn) { CHECK(FALSE); }
OSIrqFunction OS_GetIrqFunction(OSIrqMask mask) { CHECK(FALSE); return NULL; }

static void Completed(void *arg)
{
    CHECK(arg == expectedArg);
    CHECK(decoded == (expectedSize ? callbacks + 1 : 0));
    ++callbacks;
    if (reenter && callbacks == 1)
        MI_SendGXCommandAsyncFast(3, expectedSource, expectedSize, Completed, expectedArg);
}

static void RunSubmission(int mode, u32 size)
{
    u32 words[256], original[256];
    int marker = 42;
    for (unsigned i = 0; i < 256; ++i) words[i] = i ^ 0x12345678;
    memcpy(original, words, sizeof(words));
    expectedSource = words;
    expectedSize = size;
    expectedArg = mode >= 8 ? NULL : &marker;
    decoded = callbacks = 0;
    switch (mode) {
    case 0: MI_SendGXCommand(3, words, size); break;
    case 1: MI_SendGXCommandAsync(3, words, size, Completed, expectedArg); break;
    case 2: MI_SendGXCommandFast(3, words, size); break;
    case 3: MI_SendGXCommandAsyncFast(3, words, size, Completed, expectedArg); break;
    case 4:
    case 5:
        NNS_G3dGeUseFastDma(mode == 5);
        NNS_G3dGeSendDL(words, size);
        CHECK(!NNS_G3dGeIsSendDLBusy());
        break;
    case 6: MI_SendGXCommandAsync(3, words, size, NULL, NULL); break;
    case 7: MI_SendGXCommandAsyncFast(3, words, size, NULL, NULL); break;
    case 8: MI_SendGXCommandAsync(3, words, size, Completed, NULL); break;
    case 9: MI_SendGXCommandAsyncFast(3, words, size, Completed, NULL); break;
    }
    const unsigned completions = mode == 1 || mode == 3 || mode >= 8 ? (reenter ? 2 : 1) : 0;
    CHECK(callbacks == completions);
    CHECK(decoded == (size ? (reenter ? 2 : 1) : 0));
    CHECK(memcmp(words, original, sizeof(words)) == 0);
}

int main(void)
{
    const u32 sizes[] = {0, 4, 472, 476, 1024};
    for (int mode = 0; mode < 10; ++mode) {
        if (mode == 4 || mode == 5) {
            RunSubmission(mode, 256);
            RunSubmission(mode, 1024);
        } else {
            for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) RunSubmission(mode, sizes[i]);
        }
    }
    reenter = TRUE;
    RunSubmission(1, 1024);
    RunSubmission(3, 1024);
    puts("PASS: all GX variants, copied payload ownership, inline/reentrant callbacks, zero lengths and NitroSystem completion");
}
