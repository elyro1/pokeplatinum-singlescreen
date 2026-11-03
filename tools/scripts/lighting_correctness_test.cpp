#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nitro.h>
#include <simulator/drawmsg.h>
#include <simulator/g3_draw.h>
#include <simulator/g3_handler.h>

// Submission is irrelevant to the CPU lighting test; commands and vector math
// are the real renderer implementation, compiled with unused sections removed.
extern "C" void G3SIM_FlushArray()
{ }
extern "C" void G3SIM_AddVtx(G3SIM_Vertex_t *)
{ }
extern "C" void G3SIM_CollectEmitterPolygon(const G3SIM_FxVtx_t *, int)
{ }
extern "C" void SIM_handleAssertionFailure(const char *, int, const char *)
{
    std::abort();
}
extern "C" void SIM_u16ToRGB(u16 c, u8 *r, u8 *g, u8 *b)
{
    *r = ((c & 31) * 527 + 23) >> 6;
    *g = (((c >> 5) & 31) * 527 + 23) >> 6;
    *b = (((c >> 10) & 31) * 527 + 23) >> 6;
}
extern "C" u32 SIM_DecodeTextureAddress(u32 *)
{
    std::abort();
}
u8 s_SIM_useWBuffer = 0;
// Register storage is supplied by the production sim_vars.c implementation.

static u32 Vector(int x, int y, int z)
{
    return (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20);
}
static void WhiteLight(u32 vector)
{
    G3SIM_LightColor(GX_RGB(31, 31, 31));
    G3SIM_LightVector(vector);
    s_curPolygonAttr.lightFlag = 1;
}
static void Expect(int r, int g, int b)
{
    if (abs((int)s_g3CurColor[0] - r) > 1 || abs((int)s_g3CurColor[1] - g) > 1 || abs((int)s_g3CurColor[2] - b) > 1) {
        fprintf(stderr, "Expected %d %d %d, got %d %d %d\n", r, g, b, s_g3CurColor[0], s_g3CurColor[1], s_g3CurColor[2]);
    }
    assert(abs((int)s_g3CurColor[0] - r) <= 1);
    assert(abs((int)s_g3CurColor[1] - g) <= 1);
    assert(abs((int)s_g3CurColor[2] - b) <= 1);
}
static void DisplayListCompleted(void *arg)
{
    // Async completion must observe the decoded state, not just a queued copy.
    Expect(255, 0, 0);
    ++*static_cast<int *>(arg);
}

int main()
{
    G3SIM_SetLightingCorrection(TRUE);
    G3SIM_MtxMode(GX_MTXMODE_POSITION_VECTOR);
    G3SIM_Identity();
    G3SIM_DiffAmb(GX_RGB(31, 31, 31));
    G3SIM_SpecEmi(0);
    WhiteLight(Vector(-512, 0, 0));
    // Every possible signed 10-bit normal component, including values > 255.
    for (int bits = 0; bits < 1024; bits++) {
        int signedValue = bits >= 512 ? bits - 1024 : bits;
        int expected = signedValue > 0 ? (int)(signedValue * 255.0 / 512 + 0.5) : 0;
        G3SIM_Normal(Vector(bits, 0, 0));
        Expect(expected, expected, expected);
    }
    WhiteLight(Vector(0, -512, 0));
    G3SIM_Normal(Vector(0, 511, 0));
    Expect(255, 255, 255);
    G3SIM_Normal(Vector(0, -512, 0));
    Expect(0, 0, 0);

    // Scaled models (the map terrain) keep full light on faces turned
    // towards it, with a soft edge instead of an all-or-nothing step.
    WhiteLight(Vector(-512, 0, 0));
    fx32 scale[3] = { 32 * FX32_ONE, 32 * FX32_ONE, 32 * FX32_ONE };
    G3SIM_MtxScale(scale);
    G3SIM_Normal(Vector(300, 0, 0));
    Expect(255, 255, 255);
    G3SIM_Normal(Vector(0, 511, 0));
    Expect(128, 128, 128);
    G3SIM_Normal(Vector(-300, 0, 0));
    Expect(0, 0, 0);
    G3SIM_Identity();
    WhiteLight(Vector(0, -512, 0));

    // A model rotation must not rotate a previously submitted light again.
    MtxFx43 rotate = { };
    rotate._01 =FX32_ONE;
    rotate._10 = -FX32_ONE;
    rotate._22 =FX32_ONE;
    G3SIM_MtxLoad43(&rotate);
    G3SIM_Normal(Vector(511, 0, 0));
    Expect(255, 255, 255);
    G3SIM_Identity();
    G3SIM_DiffAmb(0);
    G3SIM_SpecEmi(GX_RGB(31, 31, 31));
    WhiteLight(Vector(0, 0, -512));
    G3SIM_Normal(Vector(0, 0, 511));
    Expect(254, 254, 254);
    G3SIM_Normal(Vector(0, 0, -512));
    Expect(0, 0, 0);

    // Bit15 selects a shininess table; it must not change current vertex color.
    memset(s_g3CurColor, 42, 3);
    G3SIM_SpecEmi(GX_RGB(31, 31, 31) | 0x8000);
    Expect(42, 42, 42);
    u32 table[32] = { };
    G3_Shininess(table);
    G3SIM_Normal(Vector(0, 0, 511));
    Expect(0, 0, 0);
    for (int i = 0; i < 32; i++) {
        table[i] = 0xffffffff;
    }
    G3_Shininess(table);
    G3SIM_Normal(Vector(0, 0, 511));
    Expect(254, 254, 254);

    // Packed command decoding consumes all 32 table words, then the next color.
    struct Commands {
        u32 ops;
        u32 table[32];
        u32 color;
    };
    Commands *commands = (Commands *)malloc(sizeof(Commands));
    commands->ops = G3OP_SHININESS | (G3OP_COLOR << 8);
    memset(commands->table, 0, sizeof(commands->table));
    commands->color = GX_RGB(31, 0, 0);
    draw_msg_t msg = { };
    msg.type = DRAW_CMD_G3_CMD_LIST;
    msg.data.ptr = commands;
    msg.size = sizeof(Commands);
    SIM_HandleG3Command(&msg);
    Expect(255, 0, 0);
    G3SIM_Normal(Vector(0, 0, 511));
    Expect(0, 0, 0);
    s_curPolygonAttr.lightFlag = 0;
    G3SIM_SpecEmi(GX_RGB(0, 31, 0) << 16);
    G3SIM_Normal(0);
    Expect(0, 255, 0);
    // All native submission variants must reach the real packed-command decoder.
    u32 displayList[] = { G3OP_COLOR, GX_RGB(31, 0, 0) };
    for (int variant = 0; variant < 4; ++variant) {
        memset(s_g3CurColor, 0, sizeof(s_g3CurColor));
        int completed = 0;
        if (variant == 0) MI_SendGXCommand(3, displayList, sizeof(displayList));
        if (variant == 1) MI_SendGXCommandAsync(3, displayList, sizeof(displayList), DisplayListCompleted, &completed);
        if (variant == 2) MI_SendGXCommandFast(3, displayList, sizeof(displayList));
        if (variant == 3) MI_SendGXCommandAsyncFast(3, displayList, sizeof(displayList), DisplayListCompleted, &completed);
        Expect(255, 0, 0);
        assert(completed == (variant == 1 || variant == 3));
        assert(displayList[0] == G3OP_COLOR && displayList[1] == GX_RGB(31, 0, 0));
    }
    puts("PASS: signed vectors, submission-time transforms, scaled models, directional specular and direct/packed shininess tables");
    puts("PASS: every GX submission variant reaches the real decoder before completion");
}
