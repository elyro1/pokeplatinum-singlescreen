#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/port/battle_timing.c"
#include "../../src/sys_task_manager.c"
#include "../../src/pokemon_sprite.c"
#include "../../src/particle_system.c"
#include "../../src/battle_anim/battle_anim_system.c"
#include "../../src/battle/battle_display.c"

static SIM_Config_prj_type sConfig;
System gSystem;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
void ErrorHandling_AssertFail(void) { assert(0); }
static int sParticleUpdates, sSpriteUpdates, sScriptUpdates, sScriptEnd;
static BattleAnimSystem sEncounterScript;
static int sScriptFrees;
BattleAnimSystem *BattleSystem_GetBattleAnimSystem(BattleSystem *system) { return &sEncounterScript; }
void Heap_Free(void *data) { free(data); sScriptFrees++; }
void SPLManager_Update(SPLManager *manager) { sParticleUpdates++; }
static SysTask sUploadTask;
static SPLResource sResource;
static SPLEmitter sEmitter;
static int sResourceLoads, sTextureUploads, sPaletteUploads;
void SPLManager_LoadResources(SPLManager *manager, const void *data) { manager->resources = &sResource; sResourceLoads++; }
BOOL SPLManager_UploadTextures(SPLManager *manager) { assert(manager->resources); sTextureUploads++; return TRUE; }
BOOL SPLManager_UploadPalettes(SPLManager *manager) { assert(manager->resources); sPaletteUploads++; return TRUE; }
BOOL SPLManager_UploadTexturesEx(SPLManager *manager, SPLTexVRAMAllocFunc alloc) { return SPLManager_UploadTextures(manager); }
BOOL SPLManager_UploadPalettesEx(SPLManager *manager, SPLPalVRAMAllocFunc alloc) { return SPLManager_UploadPalettes(manager); }
void NNS_GfdGetFrmTexVramState(NNSGfdFrmTexVramState *state) { assert(0); }
void NNS_GfdGetFrmPlttVramState(NNSGfdFrmPlttVramState *state) { assert(0); }
SysTask *SysTask_ExecuteAfterVBlank(SysTaskFunc callback, void *data, u32 priority)
{
    assert(!sUploadTask.callback);
    sUploadTask = (SysTask){ .callback = callback, .param = data };
    return &sUploadTask;
}
void SysTask_Done(SysTask *task)
{
    if (task == &sUploadTask) sUploadTask.callback = NULL;
    else SysTask_Delete(task);
}
SPLEmitter *SPLManager_CreateEmitterWithCallback(SPLManager *manager, int resource, SPLEmitterCallback callback)
{
    // SPL dereferences the resource immediately when initializing an emitter.
    assert(manager->resources && sTextureUploads && sPaletteUploads);
    if (callback) callback(&sEmitter);
    return &sEmitter;
}

typedef struct { int ticks, inputs, touches, nestedSteps; } Counter;
static void Count(SysTask *task, void *data)
{
    Counter *counter = data;
    counter->ticks++;
    counter->inputs += !!gSystem.pressedKeys;
    counter->touches += !!gSystem.touchPressed;
    counter->nestedSteps += BattleTiming_StepCount(task->timingDomain);
}
static void SpriteTick(PokemonSprite *sprite, PokemonSpriteTransforms *transforms) { sSpriteUpdates++; }
static void ScriptTick(BattleAnimSystem *system)
{
    sScriptUpdates++;
    if (sScriptUpdates == sScriptEnd) system->moveActive = FALSE;
}

static SysTask *Add(SysTaskManager *manager, BattleTimingDomain domain, SysTaskFunc func, void *data, int priority)
{
    BattleTimingDomain previous = BattleTiming_SetDomain(domain);
    SysTask *task = SysTaskManager_AddTask(manager, func, data, priority);
    BattleTiming_SetDomain(previous);
    assert(task);
    return task;
}

static void CheckClock(int pacing, BOOL enable60fps)
{
    void *memory = calloc(1, SysTaskManager_GetRequiredSize(8));
    SysTaskManager *manager = SysTaskManager_Init(8, memory);
    Counter animation = { 0 }, ui = { 0 }, logic = { 0 };
    sConfig.enable60fps = enable60fps;
    sConfig.battlePacing = pacing;
    BattleTiming_SetActive(TRUE);
    Add(manager, BATTLE_TIMING_ANIMATION, Count, &animation, 0);
    Add(manager, BATTLE_TIMING_UI, Count, &ui, 1);
    Add(manager, BATTLE_TIMING_NONE, Count, &logic, 2);
    PokemonSpriteManager sprites = { .heapID = HEAP_ID_BATTLE };
    PokemonSprite sprite = { .active = TRUE, .callback = SpriteTick };
    ParticleSystem particles = { .timingDomain = BATTLE_TIMING_ANIMATION };
    BattleAnimSystem script = { .isActive = TRUE, .moveActive = TRUE, .executeAnimScriptFunc = ScriptTick };
    sParticleUpdates = sSpriteUpdates = sScriptUpdates = 0;
    sScriptEnd = 1000;
    for (int frame = 0; frame < 60; frame++) {
        BattleTiming_BeginFrame();
        gSystem.pressedKeys = PAD_BUTTON_A;
        gSystem.touchPressed = TRUE;
        SysTaskManager_ExecuteTasks(manager);
        assert(gSystem.pressedKeys == PAD_BUTTON_A && gSystem.touchPressed);
        assert(!BattleTiming_IsExtraAnimationStep());
        PokemonSpriteManager_AdvanceSprite(&sprites, &sprite);
        ParticleSystem_Update(&particles);
        BattleAnimSystem_ExecuteScript(&script);
    }
    int expected = !enable60fps ? 60 : (pacing == BATTLE_PACING_SMOOTH ? 30 : (pacing == BATTLE_PACING_QUICK ? 60 : 90));
    assert(animation.ticks == expected && animation.nestedSteps == expected);
    assert(animation.inputs == (expected < 60 ? expected : 60));
    assert(animation.touches == animation.inputs);
    assert(ui.ticks == 60 && logic.ticks == 60);
    assert(ui.inputs == 60 && logic.inputs == 60);
    assert(sSpriteUpdates == expected && sParticleUpdates == expected);
    assert(sScriptUpdates == expected);
    free(memory);
}

static void UpdateEncounterAnimation(SysTask *task, void *data)
{
    assert(BattleTiming_GetDomain() == BATTLE_TIMING_NONE);
    assert(BattleDisplay_UpdateEncounterAnimation(data));
}

static void CheckEncounterClock(int pacing, BOOL enable60fps, BOOL liveChange)
{
    void *memory = calloc(1, SysTaskManager_GetRequiredSize(4));
    SysTaskManager *manager = SysTaskManager_Init(4, memory);
    MonEncounterData encounter = { .state = 4 };
    sEncounterScript = (BattleAnimSystem){ .isActive = TRUE, .moveActive = TRUE,
        .executeAnimScriptFunc = ScriptTick, .scriptData = malloc(1) };
    sConfig.enable60fps = enable60fps;
    sConfig.battlePacing = pacing;
    BattleTiming_SetActive(TRUE);
    Add(manager, BATTLE_TIMING_NONE, UpdateEncounterAnimation, &encounter, 0);
    sScriptUpdates = sScriptFrees = 0;
    sScriptEnd = 1000;
    int expected = 0;
    for (int frame = 0; frame < 60; frame++) {
        if (liveChange) sConfig.battlePacing = (frame / 10) % BATTLE_PACING_COUNT;
        BattleTiming_BeginFrame();
        expected += BattleTiming_StepCount(BATTLE_TIMING_ANIMATION);
        SysTaskManager_ExecuteTasks(manager);
        assert(sScriptUpdates == expected);
        assert(encounter.state == 4 && sScriptFrees == 0);
        assert(BattleTiming_GetDomain() == BATTLE_TIMING_NONE);
    }

    // Finish on a two-step Turbo frame: the script must stop and free only once.
    sConfig.enable60fps = TRUE;
    sConfig.battlePacing = BATTLE_PACING_TURBO;
    do { BattleTiming_BeginFrame(); } while (BattleTiming_StepCount(BATTLE_TIMING_ANIMATION) != 2);
    sScriptEnd = sScriptUpdates + 1;
    SysTaskManager_ExecuteTasks(manager);
    assert(sScriptUpdates == sScriptEnd && encounter.state == 0xFF && sScriptFrees == 1);
    assert(!BattleDisplay_UpdateEncounterAnimation(&encounter));
    assert(sScriptFrees == 1);
    free(memory);
}

static Counter sChild, sReplacement;
static void SpawnAndDelete(SysTask *task, void *data)
{
    SysTaskManager *manager = task->manager;
    assert(BattleTiming_IsExtraAnimationStep());
    Add(manager, BATTLE_TIMING_ANIMATION, Count, &sChild, 1);
    free(data);
    SysTask_Delete(task);
    // Reuse the just-deleted slot for a UI task. It must never run in the extra pass.
    assert(Add(manager, BATTLE_TIMING_UI, Count, &sReplacement, 0) == task);
}

static void CheckDeletionAndReuse(void)
{
    void *memory = calloc(1, SysTaskManager_GetRequiredSize(8));
    SysTaskManager *manager = SysTaskManager_Init(8, memory);
    sConfig.enable60fps = TRUE;
    sConfig.battlePacing = BATTLE_PACING_TURBO;
    BattleTiming_SetActive(TRUE);
    do { BattleTiming_BeginFrame(); } while (BattleTiming_StepCount(BATTLE_TIMING_ANIMATION) != 2);
    Add(manager, BATTLE_TIMING_ANIMATION, SpawnAndDelete, malloc(1), 0);
    BattleAnimSystem script = { .isActive = TRUE, .moveActive = TRUE, .executeAnimScriptFunc = ScriptTick };
    sScriptUpdates = 0;
    sScriptEnd = 1;
    BattleAnimSystem_ExecuteScript(&script);
    assert(sScriptUpdates == 1 && !script.moveActive); // End/cleanup executes once even on a two-step frame.
    SysTaskManager_ExecuteTasks(manager);
    assert(sChild.ticks == 1 && sReplacement.ticks == 0);
    BattleTiming_BeginFrame();
    SysTaskManager_ExecuteTasks(manager);
    assert(sChild.ticks == 2 && sReplacement.ticks == 1);
    free(memory);
}

static void CheckLatchedInputAndLiveChanges(void)
{
    void *memory = calloc(1, SysTaskManager_GetRequiredSize(4));
    SysTaskManager *manager = SysTaskManager_Init(4, memory);
    Counter animation = { 0 };
    sConfig.enable60fps = TRUE;
    sConfig.battlePacing = BATTLE_PACING_SMOOTH;
    BattleTiming_SetActive(TRUE);
    BattleTiming_BeginFrame();
    assert(!BattleTiming_ShouldTick(BATTLE_TIMING_ANIMATION));
    Add(manager, BATTLE_TIMING_ANIMATION, Count, &animation, 0);
    gSystem.pressedKeys = PAD_BUTTON_A;
    gSystem.touchPressed = TRUE;
    SysTaskManager_ExecuteTasks(manager);
    assert(animation.ticks == 0);
    gSystem.pressedKeys = 0;
    gSystem.touchPressed = FALSE;
    BattleTiming_BeginFrame();
    SysTaskManager_ExecuteTasks(manager);
    assert(animation.inputs == 1 && animation.touches == 1);
    int expected = animation.ticks;
    for (int frame = 0; frame < 120; frame++) {
        sConfig.battlePacing = (frame / 10) % BATTLE_PACING_COUNT;
        BattleTiming_BeginFrame();
        expected += BattleTiming_StepCount(BATTLE_TIMING_ANIMATION);
        SysTaskManager_ExecuteTasks(manager);
        assert(animation.ticks == expected);
    }
    assert(animation.inputs == 1 && animation.touches == 1);
    free(memory);
}

typedef struct { ParticleSystem particles; int state; } ResourceTask;
static void CreateResourceAndEmitter(SysTask *task, void *data)
{
    ResourceTask *owner = data;
    if (owner->state++ == 0) {
        ParticleSystem_SetResource(&owner->particles, &sResource, VRAM_AUTO_RELEASE_NONE, FALSE);
        if (task->timingDomain != BATTLE_TIMING_ANIMATION) assert(sUploadTask.callback);
    } else {
        assert(ParticleSystem_CreateEmitterWithCallback(&owner->particles, 0, NULL, NULL) == &sEmitter);
        SysTask_Done(task);
    }
}

static void CheckParticleResourceOrdering(int pacing, BOOL enable60fps, BattleTimingDomain domain)
{
    void *memory = calloc(1, SysTaskManager_GetRequiredSize(4));
    SysTaskManager *manager = SysTaskManager_Init(4, memory);
    SPLManager spl = { 0 };
    ResourceTask owner = { .particles = { .manager = &spl } };
    sConfig.enable60fps = enable60fps;
    sConfig.battlePacing = pacing;
    BattleTiming_SetActive(TRUE);
    // Start the owner on a two-step Turbo frame, before post-VBlank uploads run.
    BattleTiming_BeginFrame();
    sResourceLoads = sTextureUploads = sPaletteUploads = 0;
    sUploadTask = (SysTask){ 0 };
    Add(manager, domain, CreateResourceAndEmitter, &owner, 0);
    for (int frame = 0; frame < 10 && owner.state < 2; frame++) {
        BattleTiming_BeginFrame();
        SysTaskManager_ExecuteTasks(manager);
        if (sUploadTask.callback) sUploadTask.callback(&sUploadTask, sUploadTask.param);
    }
    assert(owner.state == 2 && manager->stackPointer == 0);
    assert(sResourceLoads == 1 && sTextureUploads == 1 && sPaletteUploads == 1);
    assert(!sUploadTask.callback);
    free(memory);
}

int main(void)
{
    for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
        CheckClock(pacing, TRUE);
        CheckClock(pacing, FALSE);
        CheckEncounterClock(pacing, TRUE, FALSE);
        CheckEncounterClock(pacing, FALSE, FALSE);
        CheckParticleResourceOrdering(pacing, TRUE, BATTLE_TIMING_ANIMATION);
        CheckParticleResourceOrdering(pacing, FALSE, BATTLE_TIMING_ANIMATION);
        CheckParticleResourceOrdering(pacing, TRUE, BATTLE_TIMING_UI);
    }
    CheckDeletionAndReuse();
    CheckLatchedInputAndLiveChanges();
    CheckEncounterClock(BATTLE_PACING_SMOOTH, TRUE, TRUE);
    puts("Animation clocks: 30/60/90 updates including unscaled encounter scripts, particle resource ordering, native sprites/particles, nested owners, input edges, task deletion/reuse and live pacing passed.");
}
