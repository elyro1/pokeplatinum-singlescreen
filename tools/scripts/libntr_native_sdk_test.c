// Real native SDK sources and register storage, exercised without SDL or a ROM.
#include <nitro.h>
#include <nitro/snd/common/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Native SDK check failed at line %d: %s\n", __LINE__, #condition); \
    exit(1); \
} } while (0)

// Mixer arrays have the production types; no audio device/callback is needed.
u32 s_SIM_sndcnt[16];
u8 *s_SIM_sndsad[16];
u16 s_SIM_sndtmr[16], s_SIM_sndpnt[16];
u32 s_SIM_sndlen[16];
u32 GXi_DmaId = GX_DMA_NOT_USE;
static int channelStarts[16];

// Starting a hardware channel also notifies the mixer; verify that boundary
// without starting the SDL audio callback.
void SIM_Audio_StartChannel(int channel) { CHECK(channel >= 0 && channel < 16); ++channelStarts[channel]; }

void SIM_handleAssertionFailure(const char *file, unsigned int line, const char *expr)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, expr);
    abort();
}
void SIM_handleAssertionFailureMsg(const char *file, unsigned int line, const char *fmt, ...)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, fmt);
    abort();
}
// Table initialization is deliberately tested through its CPU path.
void MI_DmaFill32(u32 dma, void *dest, u32 data, u32 size) { abort(); }
void MI_DmaFill32Async(u32 dma, void *dest, u32 data, u32 size, MIDmaCallback cb, void *arg) { abort(); }

static void TestFill(void)
{
    _Alignas(32) u8 bytes[128];
    const u32 patterns[] = {0, 0x200, 0x12345678, 0xffffffff};
    for (unsigned p = 0; p < sizeof(patterns) / sizeof(*patterns); ++p) {
        for (int route = 0; route < 3; ++route) {
            memset(bytes, 0xa5, sizeof(bytes));
            if (route == 0) MI_CpuFillFast(bytes + 32, patterns[p], 64);
            if (route == 1) MI_CpuFill32(bytes + 32, patterns[p], 64);
            if (route == 2) MIi_CpuClear32(patterns[p], bytes + 32, 64);
            for (unsigned i = 0; i < sizeof(bytes); ++i) {
                u8 expected = i >= 32 && i < 96 ? patterns[p] >> ((i - 32) % 4 * 8) : 0xa5;
                CHECK(bytes[i] == expected);
            }
        }
        for (u32 length = 0; length <= 37; ++length) {
            memset(bytes, 0xa5, sizeof(bytes));
            MIi_CpuClearFast(patterns[p], bytes + 3, length);
            for (unsigned i = 0; i < sizeof(bytes); ++i) {
                u8 expected = i >= 3 && i < 3 + length ? patterns[p] >> ((i - 3) % 4 * 8) : 0xa5;
                CHECK(bytes[i] == expected);
            }
        }
    }
}

static void TestArena(void)
{
    static _Alignas(64) u8 arena[4096];
    // SDK_FINALROM omits the initialization assertion; setters/getters use
    // production arena-info storage without requiring the game startup symbols.
    for (u32 align = 4; align <= 64; align *= 2) {
        OS_SetArenaLo(OS_ARENA_MAINEX, arena + 3);
        OS_SetArenaHi(OS_ARENA_MAINEX, arena + sizeof(arena) - 3);
        for (u32 size = 1; size <= 17; ++size) {
            u8 *oldLo = OS_GetArenaLo(OS_ARENA_MAINEX);
            u8 *oldHi = OS_GetArenaHi(OS_ARENA_MAINEX);
            u8 *lo = OS_AllocFromArenaLo(OS_ARENA_MAINEX, size, align);
            u8 *hi = OS_AllocFromArenaHi(OS_ARENA_MAINEX, size, align);
            CHECK(lo && hi && lo >= oldLo && lo + size <= hi);
            CHECK(hi + size <= oldHi);
            CHECK((uintptr_t)lo % align == 0 && (uintptr_t)hi % align == 0);
            CHECK((uintptr_t)OS_GetArenaLo(OS_ARENA_MAINEX) % align == 0);
            CHECK((uintptr_t)OS_GetArenaHi(OS_ARENA_MAINEX) % align == 0);
            memset(lo, 0x12, size);
            memset(hi, 0x34, size);
        }
        void *lo = OS_GetArenaLo(OS_ARENA_MAINEX), *hi = OS_GetArenaHi(OS_ARENA_MAINEX);
        CHECK(OS_AllocFromArenaLo(OS_ARENA_MAINEX, sizeof(arena), align) == NULL);
        CHECK(OS_AllocFromArenaHi(OS_ARENA_MAINEX, sizeof(arena), align) == NULL);
        CHECK(OS_GetArenaLo(OS_ARENA_MAINEX) == lo && OS_GetArenaHi(OS_ARENA_MAINEX) == hi);
    }
}

static void TestBlend(void)
{
    vu16 *controls[] = {&reg_G2_BLDCNT, &reg_G2S_DB_BLDCNT};
    vu16 *alphas[] = {&reg_G2_BLDALPHA, &reg_G2S_DB_BLDALPHA};
    vu16 *brightness[] = {&reg_G2_BLDY, &reg_G2S_DB_BLDY};
    for (int engine = 0; engine < 2; ++engine) {
        for (int raw = 0; raw < 2; ++raw) {
            for (int i = 0; i < 2; ++i) *controls[i] = *alphas[i] = *brightness[i] = 0x1357;
            u64 addr = raw ? (engine ? 0x04001050 : 0x04000050) : (u64)controls[engine];
            G2x_SetBlendAlpha_(addr, 1, 2, 7, 9);
            CHECK(*controls[engine] == 0x0241 && *alphas[engine] == 0x0907);
            CHECK(*brightness[engine] == 0x1357);
            G2x_SetBlendBrightness_(addr, 1, -12);
            CHECK(*controls[engine] == 0x00c1 && *brightness[engine] == 12);
            CHECK(*alphas[engine] == 0x0907);
            G2x_ChangeBlendBrightness_(addr, 6);
            CHECK(*controls[engine] == 0x0081 && *brightness[engine] == 6);
            G2x_SetBlendBrightnessExt_(addr, 1, 2, 4, 11, 5);
            CHECK(*controls[engine] == 0x0281 && *alphas[engine] == 0x0b04 && *brightness[engine] == 5);
            G2x_ChangeBlendBrightness_(addr, -9);
            CHECK(*controls[engine] == 0x02c1 && *brightness[engine] == 9);
            CHECK(*controls[1-engine] == 0x1357 && *alphas[1-engine] == 0x1357 && *brightness[1-engine] == 0x1357);
        }
    }
}

static void TestAffine(void)
{
    struct Registers { vu16 *pa, *pb, *pc, *pd; vu32 *x, *y; } regs[] = {
        {&reg_G2_BG2PA, &reg_G2_BG2PB, &reg_G2_BG2PC, &reg_G2_BG2PD, &reg_G2_BG2X, &reg_G2_BG2Y},
        {&reg_G2_BG3PA, &reg_G2_BG3PB, &reg_G2_BG3PC, &reg_G2_BG3PD, &reg_G2_BG3X, &reg_G2_BG3Y},
        {&reg_G2S_DB_BG2PA, &reg_G2S_DB_BG2PB, &reg_G2S_DB_BG2PC, &reg_G2S_DB_BG2PD, &reg_G2S_DB_BG2X, &reg_G2S_DB_BG2Y},
        {&reg_G2S_DB_BG3PA, &reg_G2S_DB_BG3PB, &reg_G2S_DB_BG3PC, &reg_G2S_DB_BG3PD, &reg_G2S_DB_BG3X, &reg_G2S_DB_BG3Y}
    };
    const u64 addresses[] = {0x04000020, 0x04000030, 0x04001020, 0x04001030};
    MtxFx22 matrix = {._00 = FX32_ONE, ._01 = FX32_ONE / 2, ._10 = -FX32_ONE / 4, ._11 = 2 * FX32_ONE};
    for (int target = 0; target < 4; ++target) {
        for (int raw = 0; raw < 2; ++raw) {
            for (int i = 0; i < 4; ++i) {
                *regs[i].pa = *regs[i].pb = *regs[i].pc = *regs[i].pd = 0x1357;
                *regs[i].x = *regs[i].y = 0x2468;
            }
            G2x_SetBGyAffine_(raw ? addresses[target] : (u64)regs[target].pa, &matrix, 3, 5, 9, 8);
            CHECK(*regs[target].pa == 256 && *regs[target].pb == 128);
            CHECK(*regs[target].pc == (u16)-64 && *regs[target].pd == 512);
            CHECK(*regs[target].x == 2688 && *regs[target].y == 2432);
            for (int i = 0; i < 4; ++i) if (i != target) {
                CHECK(*regs[i].pa == 0x1357 && *regs[i].pb == 0x1357 && *regs[i].pc == 0x1357 && *regs[i].pd == 0x1357);
                CHECK(*regs[i].x == 0x2468 && *regs[i].y == 0x2468);
            }
        }
    }
}

static void TestTables(void)
{
    u32 fog[8];
    GXRgb edge[8], toon[32];
    for (int i = 0; i < 8; ++i) { fog[i] = 0x10203040 + i; edge[i] = GX_RGB(i, i+1, i+2); }
    for (int i = 0; i < 32; ++i) toon[i] = GX_RGB(i, 31-i, i);
    G3X_SetFogTable(fog);
    G3X_SetToonTable(toon);
    G3X_SetEdgeColorTable(edge);
    CHECK(memcmp((const void *)REG_FOG_TABLE_0_ADDR, fog, sizeof(fog)) == 0);
    CHECK(memcmp((const void *)REG_TOON_TABLE_0_L_ADDR, toon, sizeof(toon)) == 0);
    CHECK(memcmp((const void *)REG_EDGE_COLOR_0_L_ADDR, edge, sizeof(edge)) == 0);
    s_reg_G3X_EDGE_COLOR_0_L = 0x0123;
    s_reg_G3X_EDGE_COLOR_0_H = 0x4567;
    CHECK(s_reg_G3X_EDGE_COLOR_0 == 0x45670123);
    s_reg_G3X_FOG_TABLE_7_L = 0x2345;
    s_reg_G3X_FOG_TABLE_7_H = 0x6789;
    CHECK(s_reg_G3X_FOG_TABLE_7 == 0x67892345);
    s_reg_G3X_TOON_TABLE_15_L = 0x1234;
    s_reg_G3X_TOON_TABLE_15_H = 0x5678;
    CHECK(s_reg_G3X_TOON_TABLE_15 == 0x56781234);

    // Read back full production matrix arrays through the SDK entry points.
    for (int i = 0; i < 16; ++i) s_reg_G3X_CLIPMTX_RESULT[i] = 0x1000 + i;
    for (int i = 0; i < 9; ++i) s_reg_G3X_VECMTX_RESULT[i] = 0x2000 + i;
    s_reg_G3X_GXSTAT = 0;
    MtxFx44 clip;
    MtxFx33 vector;
    CHECK(G3X_GetClipMtx(&clip) == 0 && G3X_GetVectorMtx(&vector) == 0);
    for (int i = 0; i < 16; ++i) CHECK(clip.a[i] == 0x1000 + i);
    for (int i = 0; i < 9; ++i) CHECK(vector.a[i] == 0x2000 + i);
    G3X_InitTable();
    for (int i = 0; i < 4; ++i) CHECK(s_reg_G3X_edgeTable[i] == 0);
    for (int i = 0; i < 24; ++i) CHECK(s_reg_G3X_fogToonTables[i] == 0);
    CHECK(s_reg_G3X_CLIPMTX_RESULT[15] == 0x100f && s_reg_G3X_VECMTX_RESULT[8] == 0x2008);
}

static void TestSound(void)
{
    for (int channel = 0; channel < 16; ++channel) {
        for (int i = 0; i < 16; ++i) { s_SIM_sndcnt[i] = 0x12345678; s_SIM_sndtmr[i] = 0x1357; }
        SND_SetChannelTimer7(channel, 0x2000);
        CHECK(s_SIM_sndtmr[channel] == 0xe000);
        SND_SetChannelVolume7(channel, 85, (SNDChannelDataShift)2);
        CHECK((s_SIM_sndcnt[channel] & 0x37f) == 0x255);
        CHECK((s_SIM_sndcnt[channel] & 0xffff0000) == 0x12340000);
        SND_SetChannelPan7(channel, 23);
        CHECK(((s_SIM_sndcnt[channel] >> 16) & 127) == 23);
        SND_StartChannel7(channel);
        CHECK(SND_IsChannelActive7(channel));
        CHECK(channelStarts[channel] == 1);
        SND_StopChannel7(channel, 0);
        CHECK(!SND_IsChannelActive7(channel));
        for (int i = 0; i < 16; ++i) if (i != channel) {
            CHECK(s_SIM_sndcnt[i] == 0x12345678 && s_SIM_sndtmr[i] == 0x1357);
        }
    }
    for (int i = 0; i < 16; ++i) SND_SetChannelPan7(i, i + 32);
    SND_SetMasterPan7(80);
    for (int i = 0; i < 16; ++i) CHECK(((s_SIM_sndcnt[i] >> 16) & 127) == 80);
    SND_SetMasterPan7(-1);
    for (int i = 0; i < 16; ++i) CHECK(((s_SIM_sndcnt[i] >> 16) & 127) == i + 32);
    // Exercise the setup paths that formerly wrote into independent scalars.
    SND_SetupChannelPsg7(8, (SNDDuty)3, 70, (SNDChannelDataShift)1, 0x1800, 64);
    CHECK(s_SIM_sndtmr[8] == 0xe800 && (s_SIM_sndcnt[8] & 127) == 70);
    SND_SetupChannelNoise7(15, 60, (SNDChannelDataShift)0, 0x1000, 64);
    CHECK(s_SIM_sndtmr[15] == 0xf000 && (s_SIM_sndcnt[15] & 127) == 60);
}

int main(void)
{
    TestFill();
    TestArena();
    TestBlend();
    TestAffine();
    TestTables();
    TestSound();
    puts("PASS: word fills, arena alignment, main/sub blend and affine registers, graphics tables/matrix readback, and all sound channels");
}
