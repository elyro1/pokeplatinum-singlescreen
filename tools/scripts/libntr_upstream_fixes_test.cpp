// Exercise the real WVR callbacks and matrix commands without a game or GL window.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <nitro.h>
#include <nitro/wvr/common/wvr_common.h>
#include <nitro/wvr/ARM9/wvr.h>
#include <simulator/g3_handler.h>

// Register storage is supplied by the production sim_vars.c implementation.
extern G3SIM_MatrixStack_t s_projectionStack;
extern G3SIM_MatrixStack_t s_positionStack;
extern G3SIM_MatrixStack_t s_vectorStack;
extern G3SIM_MatrixStack_t s_textureStack;

static int pxiInitCalls, callbackCalls, arm7Calls;
static void *expectedArg;

extern "C" void PXI_Init(void) { ++pxiInitCalls; }
extern "C" BOOL PXI_IsCallbackReady(int, PXIProc) { ++arm7Calls; return FALSE; }
extern "C" int PXI_SendWordByFifo(int, uPtr, BOOL) { ++arm7Calls; return -1; }
// Keep the original hardware fallback linkable so this test also detects a
// regression to that path. Native startup/shutdown must never call these.
extern "C" void PXI_SetFifoRecvCallback(int, PXIFifoCallback) { std::abort(); }
extern "C" OSIntrMode OS_DisableInterrupts(void) { std::abort(); }
extern "C" OSIntrMode OS_RestoreInterrupts(OSIntrMode) { std::abort(); }
extern "C" void OSi_UnlockVram(u16, u16) { std::abort(); }

static void Completed(void *arg, WVRResult result)
{
    assert(arg == expectedArg);
    assert(result == WVR_RESULT_SUCCESS);
    ++callbackCalls;
}

static void TestWireless()
{
    int context = 42;
    // Repeat with and without arguments and callbacks to expose stale state.
    for (void *arg : { static_cast<void *>(nullptr), static_cast<void *>(&context) }) {
        expectedArg = arg;
        for (int repeat = 0; repeat < 3; ++repeat) {
            int before = callbackCalls;
            assert(WVR_StartUpAsync(GX_VRAM_ARM7_128_D, Completed, arg) == WVR_RESULT_OPERATING);
            assert(callbackCalls == before + 1);
            assert(WVR_TerminateAsync(Completed, arg) == WVR_RESULT_OPERATING);
            assert(callbackCalls == before + 2);
            assert(WVR_TerminateAsync(nullptr, arg) == WVR_RESULT_OPERATING);
            assert(WVR_StartUpAsync(GX_VRAM_ARM7_128_D, nullptr, arg) == WVR_RESULT_OPERATING);
            assert(callbackCalls == before + 2);
        }
    }
    assert(pxiInitCalls == 24);
    assert(arm7Calls == 0);
}

static MtxFx44 Fixture(int seed)
{
    MtxFx44 matrix = {};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            matrix.m[row][col] = (seed + row * 4 + col) * FX32_ONE;
    return matrix;
}

static void Load(GXMtxMode mode, MtxFx44 matrix)
{
    G3SIM_MtxMode(mode);
    G3SIM_MtxLoad44(&matrix);
}

static void Expect(const G3SIM_Matrix_t &actual, const MtxFx44 &expected)
{
    assert(std::memcmp(actual.nums, expected.m, sizeof(expected)) == 0);
}

static MtxFx44 RotatedAndScaled(MtxFx44 original)
{
    // A quarter-turn around Z followed by a Z scale of two: row 0 = -old
    // row 1, row 1 = old row 0, row 2 doubles, and translation stays intact.
    MtxFx44 result = original;
    for (int col = 0; col < 4; ++col) {
        result.m[0][col] = -original.m[1][col];
        result.m[1][col] = original.m[0][col];
        result.m[2][col] = 2 * original.m[2][col];
    }
    return result;
}

static void TestMatrices()
{
    for (GXMtxMode mode : { GX_MTXMODE_PROJECTION, GX_MTXMODE_POSITION,
                           GX_MTXMODE_POSITION_VECTOR, GX_MTXMODE_TEXTURE }) {
        MtxFx44 projection = Fixture(1), position = Fixture(21);
        MtxFx44 vector = Fixture(41), texture = Fixture(61);
        Load(GX_MTXMODE_PROJECTION, projection);
        Load(GX_MTXMODE_POSITION_VECTOR, vector);
        // A position-only load makes position and vector deliberately differ.
        Load(GX_MTXMODE_POSITION, position);
        Load(GX_MTXMODE_TEXTURE, texture);

        MtxFx33 transform = {};
        transform._01 = -FX32_ONE;
        transform._10 = FX32_ONE;
        transform._22 = 2 * FX32_ONE;
        G3SIM_MtxMode(mode);
        G3SIM_MtxMult33(&transform);
        if (mode == GX_MTXMODE_PROJECTION) projection = RotatedAndScaled(projection);
        if (mode == GX_MTXMODE_POSITION || mode == GX_MTXMODE_POSITION_VECTOR)
            position = RotatedAndScaled(position);
        if (mode == GX_MTXMODE_POSITION_VECTOR) vector = RotatedAndScaled(vector);
        if (mode == GX_MTXMODE_TEXTURE) texture = RotatedAndScaled(texture);

        G3SIM_MtxMode(GX_MTXMODE_PROJECTION);
        G3SIM_MtxStore(0);
        G3SIM_MtxMode(GX_MTXMODE_POSITION_VECTOR);
        G3SIM_MtxStore(0);
        G3SIM_MtxMode(GX_MTXMODE_TEXTURE);
        G3SIM_MtxStore(0);
        Expect(s_projectionStack.mtxs[0], projection);
        Expect(s_positionStack.mtxs[0], position);
        Expect(s_vectorStack.mtxs[0], vector);
        Expect(s_textureStack.mtxs[0], texture);

        // Non-texture commands must update clip/vector readback, while texture
        // commands leave it alone. The last load before the multiply was texture.
        MtxFx44 clip = {};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                for (int k = 0; k < 4; ++k)
                    clip.m[row][col] += FX_Mul(position.m[row][k], projection.m[k][col]);
        assert(std::memcmp((const void *)&s_reg_G3X_CLIPMTX_RESULT_0, &clip, sizeof(clip)) == 0);
        MtxFx33 vectorResult = {};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                vectorResult.m[row][col] = vector.m[row][col];
        assert(std::memcmp((const void *)&s_reg_G3X_VECMTX_RESULT_0, &vectorResult, sizeof(vectorResult)) == 0);
    }
}

int main()
{
    TestWireless();
    TestMatrices();
    puts("PASS: native WVR callback arguments/completion, all matrix modes, fourth column, separate position/vector state and register readback");
}
