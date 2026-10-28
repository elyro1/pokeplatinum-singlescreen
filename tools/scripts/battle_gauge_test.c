#include <assert.h>
#include <stdio.h>
#include <string.h>

void *Test_GetOBJCharPtr(void);
#define G2_GetOBJCharPtr Test_GetOBJCharPtr
#include "../../src/bwbattle/view/btlv_gauge.c"
#include "../../src/bwbattle/system/hp_gauge.c"
#include "../../src/bwbattle/system/palanm.c"

static u8 sVram[4096];
static u8 sChars[4096];
static BOOL sVisible, sTickIdle = TRUE;
static int sPositionChanges;
static PaletteFade sPaletteFade;
static u16 sUnfaded[16 * 16], sFaded[16 * 16];

void *Test_GetOBJCharPtr(void) { return sVram; }
void MIi_CpuCopy16(const void *source, void *dest, u32 size) { memcpy(dest, source, size); }
void func_0204c124(ClActor *actor, BOOL visible) { assert(actor); sVisible = visible; }
void func_0204c140(ClActor *actor, const ClActorPos *pos, u16 surface) { assert(actor); sPositionChanges++; }
void func_0204c178(ClActor *actor, ClActorPos *pos, u16 surface) { assert(actor); pos->x = 100; pos->y = 20; }
void func_0204c40c(ClActor *actor, NNSG2dImageProxy *proxy) { memset(proxy, 0, sizeof(*proxy)); }
BOOL BtlvEffect_ShouldTickIdle(void) { return sTickIdle; }
PaletteFade *BtlvEffect_GetPaletteFade(void) { return &sPaletteFade; }

static void CheckDrain(int index, int maxHp, int updatesPerFrame)
{
    BtlvGauge gauge = { 0 };
    BtlvGaugeEntry *entry = &gauge.entries[index];
    int actor;
    gauge.chars = sChars;
    entry->hpBarActor = (ClActor *)&actor;
    entry->damageActor = (ClActor *)&actor;
    entry->mon.maxHp = maxHp;
    entry->mon.hp = maxHp;
    entry->mon.shown = entry->mon.visible = TRUE;
    sPositionChanges = 0;
    sTickIdle = TRUE;

    for (int hp = maxHp - 1; hp >= maxHp / 2; hp--) {
        BtlvGauge_SyncHp(&gauge, index, hp, TRUE);
        assert(entry->mon.hp == hp);
        assert(!entry->mon.hpMoving);
        assert(!entry->mon.blinking);
        assert(sVisible);
        for (int tick = 0; tick < updatesPerFrame; tick++) {
            sTickIdle = tick == updatesPerFrame - 1;
            BtlvGauge_UpdateDamageBlink(entry, FALSE, index);
        }
    }
    assert(sPositionChanges == 1); // One strip anchored at the HP before the hit.
    assert(entry->mon.hpDots == 48);
    BtlvGauge_SyncHp(&gauge, index, entry->mon.hp, FALSE);
    assert(entry->mon.blinking);
    assert(entry->mon.blinkCount == 24);
    int flashes = 0;
    BOOL wasVisible = FALSE;
    for (int frame = 0; frame <= 24; frame++) {
        for (int tick = 0; tick < updatesPerFrame; tick++) {
            sTickIdle = tick == updatesPerFrame - 1;
            u32 remaining = entry->mon.blinkCount;
            BtlvGauge_UpdateDamageBlink(entry, FALSE, index);
            if (!sTickIdle && remaining > 0) {
                assert(entry->mon.blinkCount == remaining);
            }
        }
        if (sVisible && !wasVisible) flashes++;
        wasVisible = sVisible;
    }
    assert(flashes == 3);
    assert(!entry->mon.blinking);
    assert(!sVisible);

    // A second hit gets its own strip and blink, while healing never starts one.
    sTickIdle = TRUE;
    BtlvGauge_SyncHp(&gauge, index, 1, FALSE);
    assert(sPositionChanges == 2);
    assert(entry->mon.blinking);
    BtlvGauge_SyncHp(&gauge, index, maxHp, FALSE);
    assert(entry->mon.hp == maxHp);
    assert(!entry->mon.blinking);
    assert(!sVisible);
    BtlvGauge_SyncHp(&gauge, index, -10, FALSE);
    assert(entry->mon.hp == 0);
    BtlvGauge_SyncHp(&gauge, index, maxHp + 10, FALSE);
    assert(entry->mon.hp == maxHp);
}

static void CheckPaletteFlash(int updatesPerFrame, BOOL moving)
{
    BtlvGauge gauge = { 0 };
    BtlvGaugeFlashTask flash = { .gauge = &gauge };
    BtlvGaugeEntry *entry = &gauge.entries[1];
    int actor;
    const u16 original = GX_RGB(4, 20, 10);
    gauge.chars = sChars;
    entry->hpBarActor = (ClActor *)&actor;
    entry->damageActor = (ClActor *)&actor;
    entry->mon.maxHp = entry->mon.hp = 100;
    entry->mon.shown = entry->mon.visible = TRUE;
    memset(&sPaletteFade, 0, sizeof(sPaletteFade));
    sPaletteFade.buffers[PALFADE_BUFFER_MAIN_OBJ].unfaded = sUnfaded;
    sPaletteFade.buffers[PALFADE_BUFFER_MAIN_OBJ].faded = sFaded;
    for (int i = 0; i < 16 * 16; i++) sUnfaded[i] = sFaded[i] = original;

    for (int hit = 0; hit < 2; hit++) {
        int peaks = 0;
        BOOL wasPeak = FALSE;
        for (int frame = 0; frame < 70; frame++) {
            // A move may still own the OBJ fade when the first HP sample arrives.
            sPaletteFade.activeMask = frame < 4 ? 1 << PALFADE_BUFFER_MAIN_OBJ : 0;
            if (frame == 0 || (moving && frame < 40)) {
                BtlvGauge_SyncHp(&gauge, 1, entry->mon.hp - 1, moving);
            } else if (moving && frame == 40) {
                BtlvGauge_SyncHp(&gauge, 1, entry->mon.hp, FALSE);
            }
            for (int tick = 0; tick < updatesPerFrame; tick++) {
                sTickIdle = tick == updatesPerFrame - 1;
                u16 before = sFaded[16];
                BtlvGauge_FlashTask(NULL, &flash);
                if (!sTickIdle) assert(sFaded[16] == before);
            }
            if (frame < 4) assert(sFaded[16] == original);
            u16 peak;
            BlendColors(&original, &peak, 1, 8, 0x10);
            BOOL atPeak = sFaded[16] == peak;
            if (atPeak && !wasPeak) peaks++;
            wasPeak = atPeak;
            assert(sFaded[0] == original); // The other side's palette stays intact.
        }
        assert(peaks == 3); // Even for a one-frame HP change, and never restarted per sample.
        assert(sFaded[16] == original);
    }
    BtlvGauge_SyncHp(&gauge, 1, 100, FALSE);
    for (int frame = 0; frame < 40; frame++) {
        sTickIdle = TRUE;
        BtlvGauge_FlashTask(NULL, &flash);
        assert(sFaded[16] == original); // Healing does not flash red.
    }
}

int main(void)
{
    for (int index = 0; index < 6; index++) {
        for (int updates = 1; updates <= 2; updates++) {
            CheckDrain(index, 30, updates);
            CheckDrain(index, 100, updates);
        }
    }
    for (int updates = 1; updates <= 2; updates++) {
        CheckPaletteFlash(updates, FALSE);
        CheckPaletteFlash(updates, TRUE);
    }
    puts("HP gauge feedback: 24 strip scenarios and 4 palette scenarios passed");
    return 0;
}
