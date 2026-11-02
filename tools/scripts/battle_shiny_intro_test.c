#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/bwbattle/view/btlv_effvm.c"
#include "../../src/bwbattle/system/vm.c"

static unsigned sExists, sShiny;
static int sLoads, sFrees, sSparkles, sSendOut;
static int sSparkleOrder[4];
static BOOL sCryPlaying;
static int sCryQueries;

BOOL PokeVoice_IsPlaying(u32 handle)
{
    assert(handle == 7);
    sCryQueries++;
    return sCryPlaying;
}

static void CheckIntroCryOverlap(void)
{
    BtlvEffvm wk = { .move = 0x234, .voices = { 7, -1, -1, -1, -1, -1 } };
    VM vm = { .env = &wk };
    sCryPlaying = TRUE;
    sCryQueries = 0;
    assert(!BtlvEffvm_WaitCries(&wk)); // Ordinary openings retain their sound wait.
    assert(sCryQueries == 1);
    BtlvEffvm_OverlapSendOutCry(&vm);
    assert(BtlvEffvm_WaitCries(&wk));
    assert(wk.voices[0] == 7 && sCryQueries == 1); // Audio stays alive after the menu is ready.
    wk.cryDelayed = TRUE;
    assert(!BtlvEffvm_WaitCries(&wk)); // A scheduled cry must start before the effect can end.
    wk.cryDelayed = FALSE;
    wk.move = 0x238;
    BtlvEffvm_OverlapSendOutCry(&vm);
    assert(!BtlvEffvm_WaitCries(&wk)); // Trainer throws are unaffected.
    sCryPlaying = FALSE;
    assert(BtlvEffvm_WaitCries(&wk) && wk.voices[0] == -1);
}

static void CheckAcceleratedSendOutCryOverlap(void)
{
    const u16 effects[] = { 0x234, 0x239, 0x26d, 0x231, 0x238, 0x23b };
    for (unsigned i = 0; i < NELEMS(effects); i++) {
        BtlvEffvm wk = { .move = effects[i], .voices = { 7, -1, -1, -1, -1, -1 } };
        VM vm = { .env = &wk };
        sCryPlaying = TRUE;
        assert(!BtlvEffvm_WaitCries(&wk));
        BtlvEffvm_OverlapSendOutCry(&vm);
        // Player, trainer and replacement send-outs reach the shadow restore
        // after landing while the cry continues. Other effects keep waiting.
        assert(BtlvEffvm_WaitCries(&wk) == (i < 3));
        assert(wk.voices[0] == 7);
        wk.cryDelayed = TRUE;
        assert(!BtlvEffvm_WaitCries(&wk));
        wk.cryDelayed = FALSE;
        sCryPlaying = FALSE;
        assert(BtlvEffvm_WaitCries(&wk));
    }
}

BtlvMcss *BtlvEffect_GetMcss(void) { return NULL; }
BOOL BtlvMcss_Exists(BtlvMcss *mcss, int pos) { return (sExists & (1u << pos)) != 0; }
u32 BtlvMcss_GetFlags(BtlvMcss *mcss, int pos)
{
    assert(sExists & (1u << pos));
    return (sShiny & (1u << pos)) ? BTLV_MCSS_FLAG_RARE : 0;
}
void *GFL_ArcSysReadHeapNew(u32 arc, u32 file, HeapID heap)
{
    assert(arc == 66 && file == 0x260 - 0x231);
    // Stand in for the ROM's particle/sound commands, followed by its real Return.
    const u32 script[] = { 1, 8, 0x00010000 };
    void *copy = malloc(sizeof(script));
    assert(copy);
    memcpy(copy, script, sizeof(script));
    sLoads++;
    return copy;
}
void GFL_HeapFree(void *data) { assert(data); free(data); sFrees++; }

static BOOL MarkSparkle(VM *vm, void *env)
{
    BtlvEffvm *wk = env;
    assert(wk->attacker == wk->defender && (wk->attacker & 1));
    assert(wk->value == 0);
    sSparkleOrder[sSparkles++] = wk->attacker;
    // Subroutine-local values must not leak into the player's send-out.
    wk->value = 999;
    return FALSE;
}

static BOOL MarkSendOut(VM *vm, void *env)
{
    BtlvEffvm *wk = env;
    assert(wk->attacker == 0xFF && wk->defender == 0xFF && wk->value == 123);
    assert(wk->callDepth == 0 && vm->stackPos == 0);
    sSendOut++;
    VM_Halt(vm);
    return TRUE;
}

static void CheckShinies(unsigned exists, unsigned shiny)
{
    const VMCommand commands[] = { MarkSparkle, BtlvEffvm_CmdReturn, MarkSendOut };
    const u16 sendOut[] = { 2 };
    uintptr_t stack[16];
    BtlvEffvm wk = { .attacker = 0xFF, .defender = 0xFF, .value = 123, .script = (void *)sendOut };
    VM vm = { .param = { .stackSize = 16, .commands = commands, .commandCount = 3 },
        .env = &wk, .stack = stack, .pc = (const u8 *)sendOut, .state = VM_STATE_RUNNING };
    sExists = exists;
    sShiny = shiny;
    sLoads = sFrees = sSparkles = sSendOut = 0;

    BtlvEffvm_PrefixWildShinies(&vm);
    // The added subroutines introduce no timer, camera animation or particle wait.
    // Their emitters can continue while the original player send-out runs.
    VM_Run(&vm);
    assert(sSendOut == 1 && wk.script == (void *)sendOut);
    int expected = 0;
    for (int pos = 1; pos < 8; pos += 2) {
        if ((exists & shiny) & (1u << pos)) {
            assert(sSparkleOrder[expected++] == pos);
        }
    }
    assert(sSparkles == expected && sLoads == expected && sFrees == expected);
}

int main(void)
{
    CheckIntroCryOverlap();
    CheckAcceleratedSendOutCryOverlap();
    for (unsigned exists = 0; exists < 256; exists++) {
        for (unsigned shiny = 0; shiny < 256; shiny++) {
            CheckShinies(exists, shiny);
        }
    }
    puts("Send-out cry waits: Quick/Turbo player/trainer/replacement landings overlap audio; other effects and delayed cries retain waits. Fast wild intro and 65,536 shiny masks retain sparkle order and cleanup.");
}
