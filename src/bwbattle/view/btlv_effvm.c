// The battle effect VM, which runs the move and effect scripts: their commands (swan's MOVE_SCRCMD) move the camera
// and the Pokémon, add cell actors and particles, change the background and play the sounds of each move's
// animation. The name is the ROM's string, from GFL_HeapAllocate's asserts. BtlvEffect_QueueCommands and MOVE_SCRCMD
// are swan's names; the others are ours

#include "battle/btlv_effvm.h"
#include "types.h"
#include "battle/btl_pokeparam.h"
#include "battle/btlv.h"
#include "battle/btlv_bg.h"
#include "battle/btlv_bg3d.h"
#include "battle/btlv_camera.h"
#include "battle/btlv_clact.h"
#include "battle/btlv_effect.h"
#include "battle/btlv_gauge.h"
#include "battle/btlv_mcss.h"
#include "gfl/arc.h"
#include "gfl/arc_util.h"
#include "gfl/bg_sys.h"
#include "gfl/g3d.h"
#include "gfl/graphics.h"
#include "gfl/heap.h"
#include "gfl/net.h"
#include "gfl/particle.h"
#include "gfl/sound.h"
#include "gfl/std.h"
#include "gfl/tcb.h"
#include "nitro/fx.h"
#include "nitro/gx.h"
#include "nnsys/g3d.h"
#include "pml/item.h"
#include "system/palanm.h"
#include "system/vm.h"

struct BtlvEffvm {
    u32 flipSides : 1;         // bit 0   swaps the sides of the positions; nothing in the file sets it
    u32 cameraReset : 1;       // bit 1   the camera returns to its default with the effect's end
    u32 seDelayed : 1;         // bit 2   a delayed sound effect waits (BtlvEffvm_PlaySEAt)
    u32 seSliding : 1;         // bit 3   a sound player's parameter moves (BtlvEffvm_SlideSE)
    u32 bgMoved : 1;           // bit 4   BG 3 was loaded; the end restores its priority and offset
    u32 alphaBlend : 1;        // bit 5   the blend was changed; the end restores it
    u32 isEffect : 1;          // bit 6   the script is an effect's, from archive 66, rather than a move's, from 65
    u32 paused : 1;            // bit 7   until BtlvEffvm_Resume
    u32 windowTask : 1;        // bit 8   BtlvEffect_QueueCommands' window task runs
    u32 bgmLowered : 1;        // bit 9
    u32 bgmLowerRequested : 1; // bit 10
    u32 cryDelayed : 1;        // bit 11
    u32 sePlayed : 1;          // bit 12  the end stops the sounds
    u32 mcssScaled : 1;        // bit 13
    u32 unk0_14 : 1;           // bit 14  never used
    u32 cameraLocked : 1;      // bit 15  the camera commands do nothing
    u32 particleLoading : 1;   // bit 16  a particle resource uploads in the V-blank
    u32 substituteRestore : 1; // bit 17
    u32 unk0_18 : 1;           // bit 18  set for effect 0x27e, cleared for 0x291; while set the end keeps the Pokémon
    u32 paletteAnim : 1;       // bit 19
    u32 unk0_20 : 12;
    s32 value;                     // 0x004  the scripts' register, variable 17
    TCBManager *tcbManager;        // 0x008
    ParticleSystem *particles[16]; // 0x00c
    s32 particleIds[16];           // 0x04c  each particle system's file, -1 for none
    u32 emitterCounts[16];         // 0x08c  each particle resource's count of emitters
    s32 clactIndexes[4];           // 0x0cc  -1 for none
    int attacker;                  // 0x0dc  a position, 0xff for none
    int defender;                  // 0x0e0
    u32 callAttackers[16];         // 0x0e4  the call stack, by callDepth
    u32 callDefenders[16];         // 0x124
    void *callScripts[16];         // 0x164
    s32 callValues[16];            // 0x1a4
    s32 waitTimer;                 // 0x1e4
    void *script;                  // 0x1e8
    void *savedScript;             // 0x1ec  the move's script while the substitute's runs first
    u32 savedOffset;               // 0x1f0
    void *allocs[16];              // 0x1f4  the emitters' works, freed with the effect's end
    int allocCount;                // 0x234
    HeapID heapId;                 // 0x238
    BOOL ret;                      // 0x23c  what the commands return, TRUE to make VM_Run return
    u32 waitKind;                  // 0x240  what BtlvEffvm_WaitNative waits for
    u32 unk244;                    // 0x244  1: BtlvEffvm_ResetMcss resets the Pokémon with 6f10, else 6ecc
    BtlvEffvmParam param;          // 0x248
    u16 move;                      // 0x258  a move, or an effect from BTLV_EFFECT_FIRST
    u32 ballSource;                // 0x25c  whose ball the ball particles show: 8 for param's item, 9 the attacker's
    u32 callDepth;                 // 0x260
    int bgX;                       // 0x264  BG 3's offset before the effect
    int bgY;                       // 0x268
    s32 voices[6];                 // 0x26c  PokeVoice handles, -1 for none
    BOOL camSaved;                 // 0x284
    VecFx32 camPos;                // 0x288
    VecFx32 camTarget;             // 0x294
    u32 unk2A0;                    // 0x2a0  variable 0x37
    u32 lastMove;                  // 0x2a4  effects 0x261 and 0x262 don't repeat for the same attacker
    u32 lastAttacker;              // 0x2a8
};

#define BTLV_EFFECT_FIRST 0x231

// The emitter parameters the particle commands pass BtlvEffvm_InitEmitter, 0x5c bytes
typedef struct {
    VM *vm;             // 0x00
    u32 from;           // 0x04  where the emitter starts: a position code, 8 for fromPos
    u32 to;             // 0x08  where it aims: a position code, 8 for toPos
    VecFx32 offset;     // 0x0c  added to both, turned toward the side
    u32 unk18;          // 0x18  written only
    fx32 motionHeight;  // 0x1c  of the arc of motion types other than 1 and 4
    u32 motionType;     // 0x20  0 for none, else the emitter moves from to to (BtlvEffvm_MoveEmitterArc)
    fx32 motionFrames;  // 0x24
    VecFx32 fromPos;    // 0x28
    VecFx32 toPos;      // 0x34
    u32 offsetMode;     // 0x40  1, 2: offset both positions, 3: the start only
    fx32 radiusScale;   // 0x44  scales the emitter's radius and length, 0 to keep them
    fx32 lifeTimeScale; // 0x48
    fx32 scaleScale;    // 0x4c
    fx32 cameraScale;   // 0x50  scales the camera position and the amplifiers
    fx32 turns;         // 0x54  of motion types 5 and 6
    BOOL flip;          // 0x58  mirrors the spin axis
} BtlvEffvmEmitParam;

// BtlvEffvm_InitEmitter's motion of an emitter from its start to its target, moved by BtlvEffvm_MoveEmitterArc, 0x60
// bytes
typedef struct {
    MtxFx43 mtx;    // 0x00  turns the motion toward the target, from the start
    fx32 radius;    // 0x30  half the distance
    fx32 height;    // 0x34
    s32 angle;      // 0x38
    s32 angle2;     // 0x3c
    s32 angleStep;  // 0x40
    s32 angle2Step; // 0x44
    int frames;     // 0x48
    u32 type;       // 0x4c
    int wait;       // 0x50  type 3 waits longer each step
    fx32 waitAccum; // 0x54
    fx32 waitStep;  // 0x58
    u32 offsetMode; // 0x5c
} BtlvEffvmEmitterMotionWork;

// BtlvEffvm_PlaySEAt's delayed sound effect, 0x24 bytes
typedef struct {
    BtlvEffvm *wk; // 0x00
    u32 se;        // 0x04
    u32 player;    // 0x08
    int pan;       // 0x0c
    int wait;      // 0x10
    int arg4;      // 0x14
    int arg5;      // 0x18
    int vol;       // 0x1c
    int pitch;     // 0x20
} BtlvEffvmSeWork;

// BtlvEffvm_SlideSE's move of a sound player's parameter, 0x38 bytes
typedef struct {
    BtlvEffvm *wk;     // 0x00
    u32 player;        // 0x04
    u32 type;          // 0x08  0: once, 1: back and forth
    u32 param;         // 0x0c  0, 1 or 2: which parameter
    int start;         // 0x10
    int end;           // 0x14
    fx32 value;        // 0x18
    fx32 step;         // 0x1c
    int delay;         // 0x20
    int frames;        // 0x24
    int framesReset;   // 0x28
    int stepWait;      // 0x2c
    int stepWaitReset; // 0x30
    int count;         // 0x34
} BtlvEffvmSeMoveWork;

// BtlvEffvm_CmdParticleLoad's work for loading a particle resource in the V-blank, 0xc bytes
typedef struct {
    BtlvEffvm *wk;            // 0x0
    ParticleSystem *particle; // 0x4
    void *resource;           // 0x8
} BtlvEffvmParticleVBlankWork;

// BtlvEffvm_AddCircleEmitter's emitter that circles a point, moved by BtlvEffvm_MoveCircleEmitter, 0x40 bytes (line
// 7544)
typedef struct {
    u32 type;        // 0x00  0, 1: around the attacker, 2, 3: the defender, 4, 5: the origin; bit 0: clockwise
    VecFx32 center;  // 0x04
    fx32 radiusX;    // 0x10
    fx32 radiusZ;    // 0x14
    u32 angle;       // 0x18
    u32 angleStep;   // 0x1c
    int frames;      // 0x20
    int framesReset; // 0x24
    int wait;        // 0x28
    int waitReset;   // 0x2c
    u32 unk30;       // 0x30
    int delay;       // 0x34
    int delayReset;  // 0x38
    BOOL ortho;      // 0x3c
} BtlvEffvmEmitterCircleWork;

// BtlvEffvm_CmdBgPalAnim's palette animation of BG 3 from archive 231, stepped by BtlvEffvm_PalAnimTask; 0x18c bytes,
// allocated at line 3435
typedef struct {
    BtlvEffvm *wk;      // 0x000
    u8 palettes[128];   // 0x004  the palette of each frame, from the file's list ended by 0xff
    u16 durations[128]; // 0x084  frames each one shows, from the list at 0x80 ended by 0xff98
    u8 count;           // 0x184
    u8 fileId;          // 0x185  the NCLR's file
    u8 timer;           // 0x186
    u8 state;           // 0x187
    u8 index;           // 0x188
    u8 loops;           // 0x189
} BtlvEffvmPalAnimWork;

// BtlvEffect_QueueCommands' window work, stepped by BtlvEffvm_WindowTask; 0x28 bytes, allocated at line 3659
typedef struct {
    BtlvEffvm *wk;  // 0x00
    u32 state;      // 0x04
    u32 dir;        // 0x08
    int winH;       // 0x0c
    int winV;       // 0x10
    int winInOut;   // 0x14
    u32 count;      // 0x18
    u32 wait;       // 0x1c
    u32 waitReload; // 0x20
    u32 keepWindow; // 0x24
} BtlvEffvmWindowWork;

// BtlvEffvm_CmdPlayCry's delayed cry, played by BtlvEffvm_CryDelayTask; 0x20 bytes, allocated at line 4551
typedef struct {
    BtlvEffvm *wk; // 0x00
    int pos;       // 0x04
    int arg2;      // 0x08  the five arguments of BtlvMcss_PlayCry
    int arg3;      // 0x0c
    int arg4;      // 0x10
    int arg5;      // 0x14
    int arg6;      // 0x18
    int delay;     // 0x1c
} BtlvEffvmCryWork;

// The work of BtlvEffvm_CryWaitTask, which waits for the cries; 4 bytes, allocated at lines 4540 and 8044
typedef struct {
    BtlvEffvm *wk; // 0x0
} BtlvEffvmCryWaitWork;

// BtlvEffvm_CmdWaitSide's work, stepped by BtlvEffvm_SideResetTask; 0xc bytes, allocated at line 4883
typedef struct {
    BtlvEffvm *wk; // 0x0
    u32 side;      // 0x4
    u32 doneMask;  // 0x8
} BtlvEffvmSideWork;

// BtlvEffvm_StartBgmFade's fade of the BGM's volume, stepped by BtlvEffvm_BgmFadeTask; 0x10 bytes, allocated at line
// 7799
typedef struct {
    BtlvEffvm *wk; // 0x0
    fx32 volume;   // 0x4
    fx32 target;   // 0x8
    fx32 step;     // 0xc
} BtlvEffvmBgmFadeWork;

static BOOL BtlvEffvm_CmdCameraMovePreset(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCameraMove(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCameraRotate(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCameraShake(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssCameraMode(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCameraSave(VM *vm, void *env);
static BOOL BtlvEffvm_CmdParticleLoad(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterAdd(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterAddAt(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterAddOrtho(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterAddAll(VM *vm, void *env);
static BOOL BtlvEffvm_CmdParticleDelete(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterMove(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterMoveFrom(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterMoveOrtho(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEmitterMoveFromOrtho(VM *vm, void *env);
static BOOL BtlvEffvm_CmdOrbitEmitters(VM *vm, void *env);
static BOOL BtlvEffvm_CmdOrbitEmittersAlt(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssMoveXY(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssCircle(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssShake(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssScale(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssRotate(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssAlpha(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssMosaic(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssBlink(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssSetVanish(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssPalFade(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssSetMode(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssSetValue(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssScaleTo(VM *vm, void *env);
static BOOL BtlvEffvm_CmdGaugeHide(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCallDisplay(VM *vm, void *env);
static BOOL BtlvEffvm_CmdTrainerMove(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssPlayAnim(VM *vm, void *env);
static BOOL BtlvEffvm_CmdDeleteMcss(VM *vm, void *env);
static BOOL BtlvEffvm_CmdLoadBg(VM *vm, void *env);
static BOOL BtlvEffvm_CmdBgMove(VM *vm, void *env);
static BOOL BtlvEffvm_CmdBgWave(VM *vm, void *env);
static BOOL BtlvEffvm_CmdBgPalAnim(VM *vm, void *env);
static BOOL BtlvEffvm_CmdBgSetPriority(VM *vm, void *env);
static BOOL BtlvEffvm_CmdSetAlphaBlend(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCallDfc70(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCallDfd74(VM *vm, void *env);
static BOOL BtlvEffvm_CmdClactAdd(VM *vm, void *env);
static BOOL BtlvEffvm_CmdClactMove(VM *vm, void *env);
static BOOL BtlvEffvm_CmdClactScale(VM *vm, void *env);
static BOOL BtlvEffvm_CmdClactSetAnim(VM *vm, void *env);
static BOOL BtlvEffvm_CmdClactPalFade(VM *vm, void *env);
static BOOL BtlvEffvm_CmdClactDelete(VM *vm, void *env);
static BOOL BtlvEffvm_CmdGauge(VM *vm, void *env);
static BOOL BtlvEffvm_CmdPlaySE(VM *vm, void *env);
static BOOL BtlvEffvm_CmdStopSE(VM *vm, void *env);
static BOOL BtlvEffvm_CmdPanSE(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMoveSE(VM *vm, void *env);
static BOOL BtlvEffvm_CmdWait(VM *vm, void *env);
static BOOL BtlvEffvm_CmdWaitFrames(VM *vm, void *env);
static BOOL BtlvEffvm_CmdSetYield(VM *vm, void *env);
static BOOL BtlvEffvm_CmdIfVarConst(VM *vm, void *env);
static BOOL BtlvEffvm_CmdIfVarVar(VM *vm, void *env);
static void BtlvEffvm_JumpIf(VM *vm, int value, int target, int cond, int offset);
static BOOL BtlvEffvm_CmdIfPokeExists(VM *vm, void *env);
static BOOL BtlvEffvm_CmdSetValue(VM *vm, void *env);
static BOOL BtlvEffvm_CmdLoadVar(VM *vm, void *env);
static BOOL BtlvEffvm_CmdSetVar(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssEffect(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssSwap(VM *vm, void *env);
static BOOL BtlvEffvm_CmdPlayCry(VM *vm, void *env);
static BOOL BtlvEffvm_CmdSetBallSource(VM *vm, void *env);
static BOOL BtlvEffvm_CmdBallAdd(VM *vm, void *env);
static BOOL BtlvEffvm_CmdCall(VM *vm, void *env);
static BOOL BtlvEffvm_CmdReturn(VM *vm, void *env);
static BOOL BtlvEffvm_CmdJump(VM *vm, void *env);
static BOOL BtlvEffvm_CmdWaitExternal(VM *vm, void *env);
static BOOL BtlvEffvm_CmdChangeScript(VM *vm, void *env);
static BOOL BtlvEffvm_CmdWaitSide(VM *vm, void *env);
static BOOL BtlvEffvm_CmdMcssReset(VM *vm, void *env);
static BOOL BtlvEffvm_CmdEnd(VM *vm, void *env);
static BOOL BtlvEffvm_WaitNative(VM *vm, void *env);
static BOOL BtlvEffvm_WaitTimerNative(VM *vm, void *env);
static BOOL BtlvEffvm_WaitResumeNative(VM *vm, void *env);
static BOOL BtlvEffvm_WaitParticleLoadNative(VM *vm, void *env);
static int BtlvEffvm_GetTargetPositions(BtlvEffvm *wk, int code, int *positions);
static int BtlvEffvm_GetDefenderPositions(BtlvEffvm *wk, int code, int *positions);
static int BtlvEffvm_GetTargetPosition(VM *vm, int code);
static int BtlvEffvm_CheckPosition(BtlvEffvm *wk, int pos);
static int BtlvEffvm_SwapSide(VM *vm, int pos);
static int BtlvEffvm_GetDefenderForEmitter(VM *vm, u32 *pos);
static BOOL BtlvEffvm_FindOrAddParticleId(BtlvEffvm *wk, u32 id, int *index);
static void BtlvEffvm_SetEmitterCount(BtlvEffvm *wk, int index, void *resource);
static int BtlvEffvm_FindParticleId(BtlvEffvm *wk, u32 id);
static u32 BtlvEffvm_GetEmitterCount(BtlvEffvm *wk, int index);
static void BtlvEffvm_InitEmitter(SPLEmitter *emitter);
static void BtlvEffvm_MoveEmitterArc(SPLEmitter *emitter, enum SPLUpdateCallbackType type);
static void BtlvEffvm_InitCircleEmitter(SPLEmitter *emitter);
static void BtlvEffvm_MoveCircleEmitter(SPLEmitter *emitter, enum SPLUpdateCallbackType type);
static void BtlvEffvm_DeleteParticle(ParticleSystem *particle);
static void BtlvEffvm_ResetMcss(BtlvEffvm *wk);
static void BtlvEffvm_PlaySENow(u32 se, u32 player, int pan, int arg3, int arg4, int vol, int pitch);
static u32 BtlvEffvm_GetVariable(BtlvEffvm *wk, int id);
static void BtlvEffvm_AddEmitters(BtlvEffvm *wk, BtlvEffvmEmitParam *param, int index, int resourceId);
static BOOL BtlvEffvm_AddCircleEmitter(VM *vm, BtlvEffvm *wk, BOOL ortho);
static void BtlvEffvm_ProjectToScreen(VecFx32 *pos, const VecFx32 *offset);
static void BtlvEffvm_MulVec44(const VecFx32 *src, const MtxFx44 *mtx, VecFx32 *dest, fx32 *w);
static u32 BtlvEffvm_GetBallParticleId(BtlvEffvm *wk, u32 id);
static void BtlvEffvm_StartBgmFade(BtlvEffvm *wk, fx32 start, fx32 end, int frames);
static int BtlvEffvm_GetFreeVoiceSlot(BtlvEffvm *wk);
static void BtlvEffvm_ResetMcssAll(BtlvEffvm *wk);
static void BtlvEffvm_ClearMcssFlagAll(BtlvEffvm *wk);
static void BtlvEffvm_SeWaitTask(TCB *tcb, void *data);
static void BtlvEffvm_SeWaitTaskRemove(TCB *tcb);
static void BtlvEffvm_SeMoveTask(TCB *tcb, void *data);
static void BtlvEffvm_SeMoveTaskRemove(TCB *tcb);
static void BtlvEffvm_CryDelayTask(TCB *tcb, void *data);
static void BtlvEffvm_CryDelayTaskRemove(TCB *tcb);
static void BtlvEffvm_BgmFadeTask(TCB *tcb, void *data);
static void BtlvEffvm_WindowTask(TCB *tcb, void *data);
static void BtlvEffvm_WindowTaskRemove(TCB *tcb);
static void BtlvEffvm_CryWaitTask(TCB *tcb, void *data);
static void BtlvEffvm_SideResetTask(TCB *tcb, void *data);
static void BtlvEffvm_ParticleUploadTask(TCB *tcb, void *data);
static void BtlvEffvm_PalAnimTask(TCB *tcb, void *data);
static void BtlvEffvm_PalAnimTaskRemove(TCB *tcb);
static void BtlvEffvm_ScaleAllMcss(fx32 scaleX, fx32 scaleY, int frames, int wait, int count);
static BOOL BtlvEffvm_IsGaugeKeptEffect(u32 move);

// The camera's positions and targets (.data), the particles' camera vectors, the effects that keep the gauges, the
// script commands and the VM setup. The declaration order lays them out as in the ROM
static const VecFx32 data_ov168_021f2f98 = { 0, FX32_ONE, 0 };
static const VecFx32 data_ov168_021f2fec = { 0, 0, -FX32_ONE };
static const VecFx32 data_ov168_021f2fd4 = { 0, 0, -FX32_ONE };
static const VecFx32 data_ov168_021f2fbc = { 0, 0, -FX32_ONE };
static const VMCommand MOVE_SCRCMD[] = {
    BtlvEffvm_CmdCameraMovePreset,
    BtlvEffvm_CmdCameraMove,
    BtlvEffvm_CmdCameraRotate,
    BtlvEffvm_CmdCameraShake,
    BtlvEffvm_CmdMcssCameraMode,
    BtlvEffvm_CmdCameraSave,
    BtlvEffvm_CmdParticleLoad,
    BtlvEffvm_CmdEmitterAdd,
    BtlvEffvm_CmdEmitterAddAt,
    BtlvEffvm_CmdEmitterAddOrtho,
    BtlvEffvm_CmdEmitterAddAll,
    BtlvEffvm_CmdParticleDelete,
    BtlvEffvm_CmdEmitterMove,
    BtlvEffvm_CmdEmitterMoveFrom,
    BtlvEffvm_CmdEmitterMoveOrtho,
    BtlvEffvm_CmdEmitterMoveFromOrtho,
    BtlvEffvm_CmdOrbitEmitters,
    BtlvEffvm_CmdOrbitEmittersAlt,
    BtlvEffvm_CmdMcssMoveXY,
    BtlvEffvm_CmdMcssCircle,
    BtlvEffvm_CmdMcssShake,
    BtlvEffvm_CmdMcssScale,
    BtlvEffvm_CmdMcssRotate,
    BtlvEffvm_CmdMcssAlpha,
    BtlvEffvm_CmdMcssMosaic,
    BtlvEffvm_CmdMcssBlink,
    BtlvEffvm_CmdMcssSetVanish,
    BtlvEffvm_CmdMcssPalFade,
    BtlvEffvm_CmdMcssSetMode,
    BtlvEffvm_CmdMcssSetValue,
    BtlvEffvm_CmdMcssScaleTo,
    BtlvEffvm_CmdGaugeHide,
    BtlvEffvm_CmdCallDisplay,
    BtlvEffvm_CmdTrainerMove,
    BtlvEffvm_CmdMcssPlayAnim,
    BtlvEffvm_CmdDeleteMcss,
    BtlvEffvm_CmdLoadBg,
    BtlvEffvm_CmdBgMove,
    BtlvEffvm_CmdBgWave,
    BtlvEffvm_CmdBgPalAnim,
    BtlvEffvm_CmdBgSetPriority,
    BtlvEffvm_CmdSetAlphaBlend,
    BtlvEffvm_CmdCallDfc70,
    BtlvEffvm_CmdCallDfd74,
    BtlvEffect_QueueCommands,
    BtlvEffvm_CmdClactAdd,
    BtlvEffvm_CmdClactMove,
    BtlvEffvm_CmdClactScale,
    BtlvEffvm_CmdClactSetAnim,
    BtlvEffvm_CmdClactPalFade,
    BtlvEffvm_CmdClactDelete,
    BtlvEffvm_CmdGauge,
    BtlvEffvm_CmdPlaySE,
    BtlvEffvm_CmdStopSE,
    BtlvEffvm_CmdPanSE,
    BtlvEffvm_CmdMoveSE,
    BtlvEffvm_CmdWait,
    BtlvEffvm_CmdWaitFrames,
    BtlvEffvm_CmdSetYield,
    BtlvEffvm_CmdIfVarConst,
    BtlvEffvm_CmdIfVarVar,
    BtlvEffvm_CmdIfPokeExists,
    BtlvEffvm_CmdSetValue,
    BtlvEffvm_CmdLoadVar,
    BtlvEffvm_CmdSetVar,
    BtlvEffvm_CmdMcssEffect,
    BtlvEffvm_CmdMcssSwap,
    BtlvEffvm_CmdPlayCry,
    BtlvEffvm_CmdSetBallSource,
    BtlvEffvm_CmdBallAdd,
    BtlvEffvm_CmdCall,
    BtlvEffvm_CmdReturn,
    BtlvEffvm_CmdJump,
    BtlvEffvm_CmdWaitExternal,
    BtlvEffvm_CmdChangeScript,
    BtlvEffvm_CmdWaitSide,
    BtlvEffvm_CmdMcssReset,
    BtlvEffvm_CmdEnd,
};
static const VecFx32 data_ov168_021f2fe0 = { 0, FX32_ONE, 0 };
static const VecFx32 data_ov168_021f2fa4 = { 0, 0, -FX32_ONE };
static const VecFx32 data_ov168_021f2fc8 = { 0, FX32_ONE, 0 };
static VecFx32 data_ov168_021f3fac = { FX32_CONST(8.7), FX32_CONST(7.7), FX32_CONST(23.8) };
static const VecFx32 data_ov168_021f2fb0 = { 0, 0, -FX32_ONE };
static VecFx32 data_ov168_021f3fb8 = { FX32_CONST(9.7), FX32_CONST(6.7), FX32_CONST(17.3) };
static const u32 data_ov168_021f3028[] = {
    0x235, 0x236, 0x24a, 0x24b, 0x24c, 0x24d, 0x24e, 0x24f, 0x250, 0x251, 0x252
};
static const VMInitParam data_ov168_021f3010 = { 16, 8, MOVE_SCRCMD, NELEMS(MOVE_SCRCMD), NULL, 0, 0 };
static const VecFx32 data_ov168_021f3004 = { 0, FX32_ONE, 0 };
static const VecFx32 data_ov168_021f2ff8 = { 0, FX32_ONE, 0 };
static VecFx32 data_ov168_021f3fe8 = { FX32_CONST(3), FX32_CONST(2.6), 0 };
static VecFx32 data_ov168_021f3fdc = { FX32_CONST(2), FX32_CONST(3.6), FX32_CONST(6.5) };
static VecFx32 data_ov168_021f3fc4 = { FX32_CONST(6.7), FX32_CONST(7.7), FX32_CONST(30.8) };
static VecFx32 data_ov168_021f3ff4[2] = { { 0x5ca6, FX32_CONST(5.95), 0x13cc3 }, { 0x6994, FX32_CONST(6.95), 0x6e79 } };
static VecFx32 data_ov168_021f3fd0 = { 0, FX32_CONST(3.6), FX32_CONST(13.5) };
static VecFx32 data_ov168_021f400c[2] = { { -0xe8d, FX32_CONST(1.85), 0x27f6 }, { -0x19f, FX32_CONST(2.85), -0xa654 } };
static s32 data_ov168_021f4044[8] = { 0, 0, 17, 16, 0, 0, 15, 14 };
static s32 data_ov168_021f4064[8] = { 0, 0, 5, 4, 0, 0, 3, 2 };
static VecFx32 data_ov168_021f4084[4] = { { FX32_CONST(2.7), FX32_CONST(5.2), FX32_CONST(17.3) },
                                          { 0x8994, FX32_CONST(6.95), 0x6e79 },
                                          { FX32_CONST(6.7), FX32_CONST(5.2), FX32_CONST(17.3) },
                                          { 0x4994, FX32_CONST(6.95), 0x6e79 } };
static s32 data_ov168_021f4024[8] = { 0, 0, 17, 16, 0, 0, 15, 14 };
static VecFx32 data_ov168_021f40b4[4] = { { FX32_CONST(-4), FX32_CONST(1.1), 0 },
                                          { 0x219f, FX32_CONST(2.85), -0xa654 },
                                          { 0, FX32_CONST(1.1), 0 },
                                          { -0x219f, FX32_CONST(2.85), -0xa654 } };
static VecFx32 data_ov168_021f412c[6] = { { FX32_CONST(-5), FX32_CONST(2.1), FX32_CONST(3) },
                                          { 0x519f, FX32_CONST(2.85), -0xa654 },
                                          { -0xe8d, FX32_CONST(1.85), 0x27f6 },
                                          { -0x19f, FX32_CONST(2.85), -0xa654 },
                                          { FX32_CONST(5), FX32_CONST(2.1), FX32_CONST(3) },
                                          { -0x419f, FX32_CONST(2.85), -0xa654 } };
static VecFx32 data_ov168_021f40e4[6] = { { FX32_CONST(1.7), FX32_CONST(6.2), FX32_CONST(20.3) },
                                          { 0xb994, FX32_CONST(6.95), 0x6e79 },
                                          { 0x5ca6, FX32_CONST(5.95), 0x13cc3 },
                                          { 0x6994, FX32_CONST(6.95), 0x6e79 },
                                          { FX32_CONST(6.7), FX32_CONST(6.2), FX32_CONST(20.3) },
                                          { 0x2994, FX32_CONST(6.95), 0x6e79 } };

VM *BtlvEffvm_Create(TCBManager *tcbManager, HeapID heapId) {
    BtlvEffvm *wk = GFL_HeapAllocate(heapId, sizeof(BtlvEffvm), TRUE, "btlv_effvm.c", 718);
    VM *vm;
    int i;

    wk->heapId = heapId;
    wk->tcbManager = tcbManager;
    wk->ret = TRUE;
    wk->unk244 = 1;
    for (i = 0; i < 16; i++) {
        wk->particleIds[i] = -1;
    }
    for (i = 0; i < 4; i++) {
        wk->clactIndexes[i] = -1;
    }
    for (i = 0; i < 6; i++) {
        wk->voices[i] = -1;
    }
    vm = VM_Create(heapId, &data_ov168_021f3010);
    VM_ChangeEnv(vm, wk);
    return vm;
}

BOOL BtlvEffvm_Main(VM *vm) {
    BOOL running = VM_Run(vm);
    BtlvEffvm *wk = VM_GetEnv(vm);

    if (wk->paused) {
        return FALSE;
    }
    if (!running && wk->script != NULL) {
        BtlvEffect_SetGaugeDrawEnableBySide(1, 2);
        if (!wk->isEffect) {
            BtlvEffvm_StartBgmFade(wk, 0x6b000, 0x7f000, 20);
        }
        GFL_HeapFree(wk->script);
        wk->script = NULL;
    }
    return running;
}

void BtlvEffvm_Delete(VM *vm) {
    BtlvEffvm *wk = VM_GetEnv(vm);

    BtlvEffvm_Stop(vm);
    GFL_HeapFree(wk);
    VM_Free(vm);
}

void BtlvEffvm_Start(VM *vm, u32 attacker, u32 defender, u16 move, BtlvEffvmParam *param) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    u32 offset;
    BOOL visible;
    void *script;

    wk->script = NULL;
    BtlvEffect_EndTaskGroup(1);
    if (param != NULL) {
        wk->param = *param;
    } else {
        MIi_CpuClear16(0, &wk->param, sizeof(BtlvEffvmParam));
    }
    wk->attacker = attacker;
    wk->defender = defender;
    wk->unk244 = 1;
    wk->move = move;
    if (wk->move >= BTLV_EFFECT_FIRST && (wk->move == 0x261 || wk->move == 0x262) && wk->lastMove == wk->move &&
        wk->lastAttacker == attacker) {
        return;
    }
    wk->lastMove = wk->move;
    wk->lastAttacker = attacker;
    if (BtlvEffvm_IsGaugeKeptEffect(wk->move) == FALSE) {
        BtlvEffect_SetPokemonCheck(0);
    }
    if (wk->move < BTLV_EFFECT_FIRST) {
        wk->script = GFL_ArcSysReadHeapNew(65, move, HEAPID_TAIL(wk->heapId));
        if (wk->move != 144 && wk->move != 119 && wk->move != 267) {
            BtlvEffect_SetGaugeDrawEnableBySide(0, 2);
        }
        GFL_BGSysSetBGEnabled(1, FALSE);
        GFL_BGSysSetBGEnabled(2, FALSE);
        GFL_BGSysSetBGEnabled(3, FALSE);
        wk->isEffect = FALSE;
        BtlvEffvm_StartBgmFade(wk, 0x7f000, 0x6b000, 20);
    } else {
        wk->script = GFL_ArcSysReadHeapNew(66, move - BTLV_EFFECT_FIRST, HEAPID_TAIL(wk->heapId));
        wk->isEffect = TRUE;
        if (move >= 0x256 && !(reg_G2_BLDALPHA & 0x1f)) {
            GFL_BGSysSetBGEnabled(1, FALSE);
        }
    }
    offset = 4;
    if (param != NULL) {
        if (*(u8 *)wk->script <= param->variant) {
            param->variant = 0;
        }
        offset += param->variant * 0x38;
    }
    visible = FALSE;
    if (BtlvMcss_Exists(BtlvEffect_GetMcss(), wk->attacker)) {
        if (((wk->move == 248 || wk->move == 353) && param->variant == 1) || wk->move == 144 || wk->move == 119 ||
            wk->move == 267) {
        } else {
            visible = BtlvMcss_GetFlags(BtlvEffect_GetMcss(), wk->attacker) & 1;
        }
    }
    if (wk->move == 0x27e) {
        wk->unk0_18 = TRUE;
    }
    if (wk->move == 0x291) {
        wk->unk0_18 = FALSE;
    }
    if (wk->move == 0x27e && visible) {
        wk->savedScript = wk->script;
        wk->savedOffset = offset;
        wk->script = GFL_ArcSysReadHeapNew(66, 51, HEAPID_TAIL(wk->heapId));
        wk->isEffect = TRUE;
        wk->substituteRestore = TRUE;
        offset = 4;
    } else if (!wk->isEffect && wk->move != 144 && !wk->substituteRestore && visible) {
        wk->savedScript = wk->script;
        wk->savedOffset = offset;
        wk->script = GFL_ArcSysReadHeapNew(66, 51, HEAPID_TAIL(wk->heapId));
        wk->isEffect = TRUE;
        offset = 4;
    }
    script = wk->script;
    wk->value = 0;
    wk->sePlayed = FALSE;
    wk->ballSource = 8;
    VM_LoadScript(vm, (u8 *)wk->script + *(u32 *)((u8 *)script + offset));
}

void BtlvEffvm_Stop(VM *vm) {
    BtlvEffvm *wk = VM_GetEnv(vm);

    if (wk->script != NULL) {
        BtlvEffvm_CmdEnd(vm, VM_GetEnv(vm));
        GFL_HeapFree(wk->script);
        wk->script = NULL;
    }
}

void BtlvEffvm_Resume(VM *vm) {
    BtlvEffvm *wk = VM_GetEnv(vm);

    wk->paused = FALSE;
}

s32 BtlvEffvm_GetScriptKind(VM *vm) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    s32 ret = -1;

    if (wk->script != NULL) {
        ret = wk->isEffect;
    }
    return ret;
}

void BtlvEffvmParam_Clear(BtlvEffvmParam *param) {
    param->unk0 = 0;
    param->variant = 0;
    param->unk2 = 0;
    param->unk3 = 0;
    param->unk4 = 0;
    param->unk8 = 0;
    param->itemNo = 0;
}

void BtlvEffvm_PlaySEAt(VM *vm, u32 se, u32 player, u32 pan, int arg4, int arg5, int vol, int pitch, int wait) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    BtlvEffvmSeWork *work;

    if (BtlvEffect_GetFlag26() == TRUE) {
        return;
    }
    wk->sePlayed = TRUE;
    if (pan == 2) {
        pan = 0;
    } else if (pan <= 1) {
        pan = (pan & 1) ? 127 : -128;
    } else {
        pan = (BtlvEffvm_GetTargetPosition(vm, pan) & 1) ? 127 : -128;
    }
    if (vol > 255) {
        vol = 255;
    }
    if (pitch > 255) {
        pitch = 255;
    }
    if (wait == 0) {
        BtlvEffvm_PlaySENow(se, player, pan, arg4, arg5, vol, pitch);
        return;
    }
    work = GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmSeWork), FALSE, "btlv_effvm.c", 1168);
    work->wk = wk;
    work->se = se;
    work->player = player;
    work->pan = pan;
    work->wait = wait;
    work->arg4 = arg4;
    work->arg5 = arg5;
    work->vol = vol;
    work->pitch = pitch;
    wk->seDelayed = TRUE;
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_SeWaitTask, work, 0), BtlvEffvm_SeWaitTaskRemove, 1);
}

void BtlvEffvm_SlideSE(VM *vm, u32 player, u32 type, u32 param, int start, int end, int delay, int frames, int stepWait,
                       int count) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    BtlvEffvmSeMoveWork *work =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmSeMoveWork), FALSE, "btlv_effvm.c", 1204);

    work->wk = wk;
    work->player = player;
    work->type = type;
    work->param = param;
    work->start = start;
    work->end = end;
    work->delay = delay;
    work->frames = frames;
    work->framesReset = frames;
    work->stepWait = 0;
    work->stepWaitReset = stepWait;
    work->count = count * 2;
    if (type == 1 && work->count == 0) {
        work->count = 2;
    }
    work->value = FX32_CONST(work->start);
    work->step = FX_Div(FX32_CONST(work->end - work->start), FX32_CONST(work->frames));
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_SeMoveTask, work, 0), BtlvEffvm_SeMoveTaskRemove, 1);
    wk->seSliding = TRUE;
}

void BtlvEffvm_ReleaseVoices(VM *vm) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    int i;

    for (i = 0; i < 6; i++) {
        if (wk->voices[i] != -1) {
            PokeVoice_Release(wk->voices[i]);
        }
    }
}

void BtlvEffvm_ClearLastEffect(VM *vm) {
    BtlvEffvm *wk = VM_GetEnv(vm);

    wk->lastMove = 0;
    wk->lastAttacker = 0;
}

static BOOL BtlvEffvm_CmdCameraMovePreset(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    VecFx32 pos;
    VecFx32 target;
    int moveType = VM_Read32(vm);
    int posType = VM_Read32(vm);
    int frames = VM_Read32(vm);
    int wait = VM_Read32(vm);
    int brakeFrames = VM_Read32(vm);
    int rule;
    int battler;

    if (wk->cameraLocked) {
        return wk->ret;
    }
    if (posType == 13 && wk->camSaved == FALSE) {
        posType = 8;
    }
    if (posType != 8 && posType != 15 && posType != 16 && posType != 14 && posType != 17) {
        BtlvEffvm_ResetMcss(wk);
    } else {
        wk->cameraReset = TRUE;
    }
    switch (posType) {
    case 15:
    case 16:
        rule = BtlvEffect_GetBattleStyle();
        battler = posType == 15 ? wk->attacker : wk->defender;
        switch (rule) {
        case 0:
            return wk->ret;
        case 1:
        case 2:
            switch (battler) {
            case 2:
                posType = 2;
                break;
            case 3:
                posType = 8;
                if (rule != 1) {
                    posType = 18;
                }
                break;
            case 4:
                posType = 4;
                if (rule != 1) {
                    posType = 8;
                }
                break;
            case 5:
                posType = 8;
                break;
            case 6:
                posType = 8;
                if (rule != 1) {
                    posType = 6;
                }
                break;
            case 0:
            case 1:
            case 7:
            default:
                return wk->ret;
            }
            break;
        case 3:
            if (battler == 4 || battler == 6) {
                posType = 19;
            }
            break;
        }
        break;
    case 14:
        rule = BtlvEffect_GetBattleStyle();
        if (rule == 0 || rule == 3) {
            return wk->ret;
        }
        break;
    case 20:
        rule = BtlvEffect_GetBattleStyle();
        if (rule == 0 || rule == 3) {
            BtlvMcss_SetFlatAll(BtlvEffect_GetMcss());
            return wk->ret;
        } else {
            posType = 14;
        }
        break;
    case 21:
        rule = BtlvEffect_GetBattleStyle();
        if (rule == 0 || rule == 3) {
            posType = wk->attacker;
        } else if (wk->attacker & 1) {
            posType = 1;
        } else {
            posType = 14;
        }
        break;
    case 17:
        posType = 8;
        break;
    case 9:
    case 10:
        rule = BtlvEffect_GetBattleStyle();
        posType = wk->attacker;
        if (rule == 3 && (posType == 4 || posType == 6)) {
            posType = 19;
        }
        break;
    case 11:
    case 12:
        switch (wk->param.unk0) {
        case 0:
        case 1:
        case 2:
        case 3:
            posType = wk->defender;
            if (posType == 0xff) {
                posType = (wk->attacker & 1) ^ 1;
            }
            break;
        case 5:
        case 7:
        case 9:
        case 13:
            posType = wk->defender;
            if (posType != 0xff) {
                break;
            }
            // fallthrough
        case 11:
            posType = (wk->attacker & 1) ^ 1;
            break;
        case 6:
        case 12:
            posType = wk->attacker & 1;
            break;
        case 4:
        case 8:
        case 10:
            rule = BtlvEffect_GetBattleStyle();
            if (rule == 0 || rule == 3) {
                posType = (wk->attacker & 1) ^ 1;
            } else {
                posType = 14;
            }
            break;
        }
        break;
    }
    switch (posType) {
    case 0:
    case 1:
        pos.x = data_ov168_021f3ff4[posType].x;
        pos.y = data_ov168_021f3ff4[posType].y;
        pos.z = data_ov168_021f3ff4[posType].z;
        target.x = data_ov168_021f400c[posType].x;
        target.y = data_ov168_021f400c[posType].y;
        target.z = data_ov168_021f400c[posType].z;
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        rule = BtlvEffect_GetBattleStyle();
        if (rule == 3) {
            pos.x = data_ov168_021f3ff4[posType - 2].x;
            pos.y = data_ov168_021f3ff4[posType - 2].y;
            pos.z = data_ov168_021f3ff4[posType - 2].z;
            target.x = data_ov168_021f400c[posType - 2].x;
            target.y = data_ov168_021f400c[posType - 2].y;
            target.z = data_ov168_021f400c[posType - 2].z;
            break;
        }
        if (rule == 1) {
            pos.x = data_ov168_021f4084[posType - 2].x;
            pos.y = data_ov168_021f4084[posType - 2].y;
            pos.z = data_ov168_021f4084[posType - 2].z;
            target.x = data_ov168_021f40b4[posType - 2].x;
            target.y = data_ov168_021f40b4[posType - 2].y;
            target.z = data_ov168_021f40b4[posType - 2].z;
            break;
        }
        pos.x = data_ov168_021f40e4[posType - 2].x;
        pos.y = data_ov168_021f40e4[posType - 2].y;
        pos.z = data_ov168_021f40e4[posType - 2].z;
        target.x = data_ov168_021f412c[posType - 2].x;
        target.y = data_ov168_021f412c[posType - 2].y;
        target.z = data_ov168_021f412c[posType - 2].z;
        break;
    case 6:
    case 7:
        pos.x = data_ov168_021f40e4[posType - 2].x;
        pos.y = data_ov168_021f40e4[posType - 2].y;
        pos.z = data_ov168_021f40e4[posType - 2].z;
        target.x = data_ov168_021f412c[posType - 2].x;
        target.y = data_ov168_021f412c[posType - 2].y;
        target.z = data_ov168_021f412c[posType - 2].z;
        break;
    case 13:
        pos.x = wk->camPos.x;
        pos.y = wk->camPos.y;
        pos.z = wk->camPos.z;
        target.x = wk->camTarget.x;
        target.y = wk->camTarget.y;
        target.z = wk->camTarget.z;
        break;
    case 14:
        pos.x = data_ov168_021f3fac.x;
        pos.y = data_ov168_021f3fac.y;
        pos.z = data_ov168_021f3fac.z;
        target.x = data_ov168_021f3fdc.x;
        target.y = data_ov168_021f3fdc.y;
        target.z = data_ov168_021f3fdc.z;
        if (wk->cameraReset) {
            BtlvEffvm_ScaleAllMcss(0xa66, 0xb9a, frames, wait, 0);
            wk->mcssScaled = TRUE;
        }
        break;
    case 19:
        pos.x = data_ov168_021f3fc4.x;
        pos.y = data_ov168_021f3fc4.y;
        pos.z = data_ov168_021f3fc4.z;
        target.x = data_ov168_021f3fd0.x;
        target.y = data_ov168_021f3fd0.y;
        target.z = data_ov168_021f3fd0.z;
        if (wk->cameraReset) {
            BtlvEffvm_ScaleAllMcss(0xa66, 0xb9a, frames, wait, 0);
            wk->mcssScaled = TRUE;
        }
        break;
    case 18:
        pos.x = data_ov168_021f3fb8.x;
        pos.y = data_ov168_021f3fb8.y;
        pos.z = data_ov168_021f3fb8.z;
        target.x = data_ov168_021f3fe8.x;
        target.y = data_ov168_021f3fe8.y;
        target.z = data_ov168_021f3fe8.z;
        break;
    default:
        BtlvCamera_GetDefaultPosTarget(&pos, &target);
        if (wk->mcssScaled) {
            BtlvEffvm_ScaleAllMcss(FX32_ONE, FX32_ONE, frames, wait, 0);
            wk->mcssScaled = FALSE;
        }
        break;
    }
    switch (moveType) {
    case 0:
        BtlvCamera_SetPosTarget(BtlvEffect_GetCamera(), &pos, &target);
        break;
    case 1:
        BtlvCamera_MoveTo(BtlvEffect_GetCamera(), &pos, &target, frames, wait, brakeFrames);
        break;
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCameraMove(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int moveType = VM_Read32(vm);
    VecFx32 pos;
    VecFx32 target;
    VecFx32 curPos;
    VecFx32 curTarget;
    int frames;
    int wait;
    int brakeFrames;

    pos.x = VM_Read32(vm);
    pos.y = VM_Read32(vm);
    pos.z = VM_Read32(vm);
    target.x = VM_Read32(vm);
    target.y = VM_Read32(vm);
    target.z = VM_Read32(vm);
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    brakeFrames = VM_Read32(vm);
    BtlvEffvm_ResetMcss(wk);
    switch (moveType) {
    case 0:
        BtlvCamera_SetPosTarget(BtlvEffect_GetCamera(), &pos, &target);
        break;
    case 1:
        BtlvCamera_MoveTo(BtlvEffect_GetCamera(), &pos, &target, frames, wait, brakeFrames);
        break;
    case 2:
        BtlvCamera_GetPosTarget(BtlvEffect_GetCamera(), &curPos, &curTarget);
        VEC_Add(&curPos, &pos, &pos);
        VEC_Add(&curTarget, &target, &target);
        BtlvCamera_MoveTo(BtlvEffect_GetCamera(), &pos, &target, frames, wait, brakeFrames);
        break;
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCameraRotate(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int moveType = VM_Read32(vm);
    s32 pitch = VM_Read32(vm);
    s32 yaw = VM_Read32(vm);
    int frames = VM_Read32(vm);
    int wait = VM_Read32(vm);
    int brakeFrames = VM_Read32(vm);
    VecFx32 pos;
    VecFx32 target;

    BtlvEffvm_ResetMcss(wk);
    switch (moveType) {
    case 0:
        BtlvCamera_Rotate(BtlvEffect_GetCamera(), pitch, yaw);
        break;
    case 1:
        BtlvCamera_CalcRotatedPos(BtlvEffect_GetCamera(), pitch, yaw, &pos, &target);
        BtlvCamera_MoveTo(BtlvEffect_GetCamera(), &pos, &target, frames, wait, brakeFrames);
        break;
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCameraShake(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int dir = VM_Read32(vm);
    fx32 amplitude = VM_Read32(vm);
    int unused = VM_Read32(vm);
    int frames = VM_Read32(vm);
    int wait = VM_Read32(vm);
    int count = VM_Read32(vm);

    BtlvCamera_Shake(BtlvEffect_GetCamera(), dir, amplitude, unused, frames, wait, count);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssCameraMode(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int type = VM_Read32(vm);
    int count;
    int i;
    int positions[8];

    if (VM_Read32(vm) == 0) {
        wk->unk244 = type;
        BtlvEffvm_ResetMcss(wk);
    } else {
        count = BtlvEffvm_GetTargetPositions(wk, 14, positions);
        if (count != 0) {
            for (i = 0; i < count; i++) {
                if (type == 1) {
                    BtlvMcss_ClearFlat(BtlvEffect_GetMcss(), positions[i]);
                } else {
                    BtlvMcss_SetFlat(BtlvEffect_GetMcss(), positions[i]);
                }
            }
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCameraSave(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->camSaved = TRUE;
    BtlvCamera_GetPosTarget(BtlvEffect_GetCamera(), &wk->camPos, &wk->camTarget);
    return wk->ret;
}

// The work of a particle system the effects load. bwbattle: twice BW2's, for SPL's 64-bit structs (24 emitters and
// 200 particles alone take 0x5288 bytes)
#ifdef BWBATTLE
#define BTLV_EFFVM_PARTICLE_WORK_SIZE (0x4800 * 2)
#else
#define BTLV_EFFVM_PARTICLE_WORK_SIZE 0x4800
#endif

static BOOL BtlvEffvm_CmdParticleLoad(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 fileId = BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm));
    int index;
    void *work;
    void *resource;
    BtlvEffvmParticleVBlankWork *vblank;

    if (BtlvEffvm_FindOrAddParticleId(wk, fileId, &index) == TRUE) {
        work = GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), BTLV_EFFVM_PARTICLE_WORK_SIZE, FALSE, "btlv_effvm.c", 1912);
        resource = func_0204fdf8(6, fileId, HEAPID_TAIL(wk->heapId));
        if (work != NULL && resource != NULL) {
            wk->particles[index] =
                func_0204f980(work, BTLV_EFFVM_PARTICLE_WORK_SIZE, FALSE, 5, 6, 54, HEAPID_TAIL(wk->heapId));
            if (wk->particles[index] != NULL) {
                func_0204fef8(wk->particles[index], resource);
                vblank = GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmParticleVBlankWork), FALSE,
                                          "btlv_effvm.c", 1922);
                vblank->wk = wk;
                vblank->particle = wk->particles[index];
                vblank->resource = resource;
                wk->particleLoading = TRUE;
                BtlvEffect_AddTask(GFL_VBlankTCBAdd(BtlvEffvm_ParticleUploadTask, vblank, 0), NULL, 1);
                VM_SetNativeCallback(vm, BtlvEffvm_WaitParticleLoadNative);
                BtlvEffvm_SetEmitterCount(wk, index, resource);
            }
        }
        if (wk->particles[index] == NULL) {
            if (work != NULL) {
                GFL_HeapFree(work);
            }
            if (resource != NULL) {
                GFL_HeapFree(resource);
            }
            wk->particleIds[index] = -1;
        }
    }
    return TRUE;
}

static BOOL BtlvEffvm_CmdEmitterAdd(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 1965);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->from = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->unk18 = VM_Read32(vm);
    VM_Read32(vm);
    param->radiusScale = VM_Read32(vm);
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = VM_Read32(vm);
    param->cameraScale = VM_Read32(vm);
    if (param->to == 8) {
        param->to = param->from;
    }
    if (index != 16) {
        BtlvEffvm_AddEmitters(wk, param, index, resourceId);
    }
    GFL_HeapFree(param);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterAddAt(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2016);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->from = 8;
    param->to = 8;
    param->fromPos.x = VM_Read32(vm);
    param->fromPos.y = VM_Read32(vm);
    param->fromPos.z = VM_Read32(vm);
    param->toPos.x = VM_Read32(vm);
    param->toPos.y = VM_Read32(vm);
    param->toPos.z = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->unk18 = VM_Read32(vm);
    VM_Read32(vm);
    param->radiusScale = VM_Read32(vm);
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = VM_Read32(vm);
    param->cameraScale = VM_Read32(vm);
    if (index != 16) {
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, param) == (SPLEmitter *)-1) {
            GFL_HeapFree(param);
        }
    } else {
        GFL_HeapFree(param);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterAddOrtho(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2073);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->from = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = VM_Read32(vm);
    param->offset.y = VM_Read32(vm);
    param->offset.z = VM_Read32(vm);
    param->radiusScale = VM_Read32(vm);
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = VM_Read32(vm);
    param->cameraScale = VM_Read32(vm);
    param->offsetMode = 1;
    if (param->from == 13) {
        param->from = 9;
        param->offsetMode = 2;
    }
    if (param->to == 8) {
        param->to = param->from;
    }
    if (index != 16) {
        G3DCameraProjection projection;
        VecFx32 pos = { 0, 0, 0 };
        VecFx32 up = data_ov168_021f3004;
        VecFx32 target = data_ov168_021f2fb0;

        projection.type = G3DCAM_PROJECTION_ORTHO;
        projection.param1 = FX32_CONST(3);
        projection.param2 = FX32_CONST(-3);
        projection.param3 = FX32_CONST(-4);
        projection.param4 = FX32_CONST(4);
        projection.near = FX32_ONE;
        projection.far = FX32_CONST(512);
        projection.ndcRangeOverride = FX32_ONE;

        if (func_02050194(wk->particles[index]) == NULL) {
            func_020500cc(wk->particles[index], &projection, 0x2000, &pos, &up, &target, HEAPID_TAIL(wk->heapId));
        }
        BtlvEffvm_AddEmitters(wk, param, index, resourceId);
    }
    GFL_HeapFree(param);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterAddAll(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2146);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int count = BtlvEffvm_GetEmitterCount(wk, index);
    BOOL ortho;
    int i;

    param->vm = vm;
    param->from = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->unk18 = VM_Read32(vm);
    ortho = VM_Read32(vm);
    param->radiusScale = VM_Read32(vm);
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = VM_Read32(vm);
    param->cameraScale = VM_Read32(vm);
    if (index != 16) {
        if (ortho) {
            G3DCameraProjection projection;
            VecFx32 pos = { 0, 0, 0 };
            VecFx32 up = data_ov168_021f2f98;
            VecFx32 target = data_ov168_021f2fec;

            projection.type = G3DCAM_PROJECTION_ORTHO;
            projection.param1 = FX32_CONST(3);
            projection.param2 = FX32_CONST(-3);
            projection.param3 = FX32_CONST(-4);
            projection.param4 = FX32_CONST(4);
            projection.near = FX32_ONE;
            projection.far = FX32_CONST(512);
            projection.ndcRangeOverride = FX32_ONE;

            if (func_02050194(wk->particles[index]) == NULL) {
                func_020500cc(wk->particles[index], &projection, 0x2000, &pos, &up, &target, HEAPID_TAIL(wk->heapId));
            }
            param->offsetMode = 2;
        }
        if (param->to == 8) {
            param->to = param->from;
        }
        for (i = 0; i < count; i++) {
            BtlvEffvm_AddEmitters(wk, param, index, i);
        }
    }
    GFL_HeapFree(param);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdParticleDelete(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));

    if (index != 16) {
        BtlvEffvm_DeleteParticle(wk->particles[index]);
        wk->particles[index] = NULL;
        wk->particleIds[index] = -1;
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterMove(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2251);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->motionType = VM_Read32(vm);
    param->from = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->motionFrames = VM_Read32(vm);
    param->motionHeight = VM_Read32(vm);
    param->radiusScale = FX32_ONE;
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = FX32_ONE;
    param->cameraScale = VM_Read32(vm);
    param->turns = VM_Read32(vm);
    if (param->turns == 0) {
        param->turns = 1;
    }
    if (index != 16) {
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, param) == (SPLEmitter *)-1) {
            GFL_HeapFree(param);
        }
    } else {
        GFL_HeapFree(param);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterMoveFrom(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2309);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->motionType = VM_Read32(vm);
    param->from = 8;
    param->fromPos.x = VM_Read32(vm);
    param->fromPos.y = VM_Read32(vm);
    param->fromPos.z = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->motionFrames = VM_Read32(vm);
    param->motionHeight = VM_Read32(vm);
    param->radiusScale = FX32_ONE;
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = FX32_ONE;
    param->cameraScale = VM_Read32(vm);
    param->turns = VM_Read32(vm);
    if (param->turns == 0) {
        param->turns = 1;
    }
    if (index != 16) {
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, param) == (SPLEmitter *)-1) {
            GFL_HeapFree(param);
        }
    } else {
        GFL_HeapFree(param);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterMoveOrtho(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2371);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->motionType = VM_Read32(vm);
    param->from = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->motionFrames = VM_Read32(vm);
    param->motionHeight = VM_Read32(vm);
    param->radiusScale = FX32_CONST(0.75);
    param->lifeTimeScale = VM_Read32(vm);
    param->scaleScale = FX32_CONST(0.75);
    param->cameraScale = VM_Read32(vm);
    param->turns = VM_Read32(vm);
    param->offsetMode = 3;
    if (param->turns == 0) {
        param->turns = 1;
    }
    if (index != 16) {
        G3DCameraProjection projection;
        VecFx32 pos = { 0, 0, 0 };
        VecFx32 up = data_ov168_021f2fe0;
        VecFx32 target = data_ov168_021f2fa4;

        projection.type = G3DCAM_PROJECTION_ORTHO;
        projection.param1 = FX32_CONST(3);
        projection.param2 = FX32_CONST(-3);
        projection.param3 = FX32_CONST(-4);
        projection.param4 = FX32_CONST(4);
        projection.near = FX32_ONE;
        projection.far = FX32_CONST(512);
        projection.ndcRangeOverride = FX32_ONE;

        if (func_02050194(wk->particles[index]) == NULL) {
            func_020500cc(wk->particles[index], &projection, 0x2000, &pos, &up, &target, HEAPID_TAIL(wk->heapId));
        }
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, param) == (SPLEmitter *)-1) {
            GFL_HeapFree(param);
        }
    } else {
        GFL_HeapFree(param);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEmitterMoveFromOrtho(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmEmitParam *param =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 2448);
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);

    param->vm = vm;
    param->motionType = VM_Read32(vm);
    param->from = 8;
    param->fromPos.x = VM_Read32(vm);
    param->fromPos.y = VM_Read32(vm);
    param->fromPos.z = VM_Read32(vm);
    param->to = VM_Read32(vm);
    param->offset.x = 0;
    param->offset.y = VM_Read32(vm);
    param->offset.z = 0;
    param->motionFrames = VM_Read32(vm);
    param->motionHeight = VM_Read32(vm);
    param->radiusScale = FX32_ONE;
    param->lifeTimeScale = VM_Read32(vm);
    param->cameraScale = VM_Read32(vm);
    param->scaleScale = VM_Read32(vm);
    param->offsetMode = 3;
    if (index != 16) {
        G3DCameraProjection projection;
        VecFx32 pos = { 0, 0, 0 };
        VecFx32 up = data_ov168_021f2fc8;
        VecFx32 target = data_ov168_021f2fbc;

        projection.type = G3DCAM_PROJECTION_ORTHO;
        projection.param1 = FX32_CONST(3);
        projection.param2 = FX32_CONST(-3);
        projection.param3 = FX32_CONST(-4);
        projection.param4 = FX32_CONST(4);
        projection.near = FX32_ONE;
        projection.far = FX32_CONST(512);
        projection.ndcRangeOverride = FX32_ONE;

        if (func_02050194(wk->particles[index]) == NULL) {
            func_020500cc(wk->particles[index], &projection, 0x2000, &pos, &up, &target, HEAPID_TAIL(wk->heapId));
        }
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, param) == (SPLEmitter *)-1) {
            GFL_HeapFree(param);
        }
    } else {
        GFL_HeapFree(param);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdOrbitEmitters(VM *vm, void *env) {
    return BtlvEffvm_AddCircleEmitter(vm, env, 0);
}

static BOOL BtlvEffvm_CmdOrbitEmittersAlt(VM *vm, void *env) {
    return BtlvEffvm_AddCircleEmitter(vm, env, 1);
}

static BOOL BtlvEffvm_CmdMcssMoveXY(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    VecFx32 dest;
    s32 frames;
    s32 wait;
    s32 times;
    int i;

    dest.x = VM_Read32(vm);
    dest.y = VM_Read32(vm);
    dest.z = 0;
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    times = VM_Read32(vm);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_MovePosition(BtlvEffect_GetMcss(), positions[i], type, &dest, frames, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssCircle(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvMcssCircle circle;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int i;

    circle.mode = VM_Read32(vm);
    circle.start = VM_Read32(vm);
    circle.radius1 = VM_Read32(vm);
    circle.radius2 = VM_Read32(vm);
    circle.frames = (s32)VM_Read32(vm) >> FX32_SHIFT;
    circle.wait = (s32)VM_Read32(vm) >> FX32_SHIFT;
    circle.count = (s32)VM_Read32(vm) >> FX32_SHIFT;
    circle.turnWait = VM_Read32(vm);
    if (count != 0 && circle.count != 0) {
        for (i = 0; i < count; i++) {
            circle.pos = positions[i];
            BtlvMcss_MoveCircle(BtlvEffect_GetMcss(), &circle);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssShake(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvMcssShake shake;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    s32 axis = VM_Read32(vm);
    fx32 startAngle = VM_Read32(vm);
    fx32 endAngle = VM_Read32(vm);
    fx32 amplitude = VM_Read32(vm);
    s32 frames = VM_Read32(vm);
    int i;

    shake.frames = frames;
    shake.axis = axis;
    shake.angle = startAngle;
    shake.amplitude = amplitude;
    shake.speed = FX_Div(endAngle - startAngle, frames << FX32_SHIFT);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            shake.pos = positions[i];
            BtlvMcss_Shake(BtlvEffect_GetMcss(), &shake);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssScale(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    VecFx32 dest;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    s32 frames;
    s32 wait;
    s32 times;
    int i;

    dest.x = VM_Read32(vm);
    dest.y = VM_Read32(vm);
    dest.z = FX32_ONE;
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    times = VM_Read32(vm);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_MoveVec518(BtlvEffect_GetMcss(), positions[i], type, &dest, frames, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssRotate(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    VecFx32 dest;
    s32 frames;
    s32 wait;
    s32 times;
    int i;

    dest.x = 0;
    dest.y = 0;
    dest.z = VM_Read32(vm);
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    times = VM_Read32(vm);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_MoveVec51c(BtlvEffect_GetMcss(), positions[i], type, &dest, frames, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssAlpha(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    int alpha = VM_Read32(vm);
    s32 frames = VM_Read32(vm);
    s32 wait = VM_Read32(vm);
    s32 times = VM_Read32(vm);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_MoveAlpha(BtlvEffect_GetMcss(), positions[i], type, alpha, frames, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssMosaic(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    int level = VM_Read32(vm);
    s32 frames = VM_Read32(vm);
    s32 wait = VM_Read32(vm);
    s32 times = VM_Read32(vm);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_MoveLevel(BtlvEffect_GetMcss(), positions[i], type, level, frames, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssBlink(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    s32 wait = VM_Read32(vm);
    s32 times = VM_Read32(vm);
    int i;

    if (count != 0) {
        if (times == 0) {
            times = 1;
        }
        for (i = 0; i < count; i++) {
            BtlvMcss_Blink(BtlvEffect_GetMcss(), positions[i], type, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssSetVanish(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int mode = VM_Read32(vm);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_SetAnimPause(BtlvEffect_GetMcss(), positions[i], mode);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssPalFade(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int startEvy = VM_Read32(vm);
    int endEvy = VM_Read32(vm);
    int wait = VM_Read32(vm);
    u32 color = VM_Read32(vm);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_StartPaletteFade(BtlvEffect_GetMcss(), positions[i], startEvy, endEvy, wait, color);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssSetMode(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int mode = VM_Read32(vm);
    int i;

    if (mode == 5) {
        BtlvMcss_RestoreVanish(BtlvEffect_GetMcss());
    } else if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_SetVanish(BtlvEffect_GetMcss(), positions[i], mode);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssSetValue(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int value = VM_Read32(vm);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_SetShadowVanish(BtlvEffect_GetMcss(), positions[i], value);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssScaleTo(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    VecFx32 dest;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int type = VM_Read32(vm);
    s32 frames;
    s32 wait;
    s32 times;
    int i;

    dest.x = VM_Read32(vm);
    dest.y = VM_Read32(vm);
    dest.z = FX32_ONE;
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    times = VM_Read32(vm);
    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_MoveVec524(BtlvEffect_GetMcss(), positions[i], type, &dest, frames, wait, times);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdGaugeHide(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvEffect_DelPokemon(positions[i]);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCallDisplay(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 arg0 = VM_Read32(vm);
    u32 pos = BtlvEffvm_GetTargetPosition(vm, VM_Read32(vm));
    u32 arg2 = VM_Read32(vm);
    u32 arg3 = VM_Read32(vm);
    u32 arg4 = VM_Read32(vm);

    if (arg0 == -1) {
        arg0 = wk->value;
    }
    BtlvEffect_SetTrainer(arg0, pos, arg2, arg3, arg4);
    return wk->ret;
}

#ifdef BWBATTLE
#define BTLV_EFFECT_TRAINER_SLIDE_IN 0x270
// 0x270's moves after its first, which sets the place: -22528 then +8192 across, mirrored on the opponent's side
#define BTLV_SLIDE_IN_TRAVEL 14336
// Move the opponent's dialogue sprite to the right of its Pokémon.
#define BTLV_DIALOGUE_TRAINER_OFFSET 0x2000
#endif

static BOOL BtlvEffvm_CmdTrainerMove(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int pos = BtlvEffect_GetTrainerIndex(VM_Read32(vm));
    int type = VM_Read32(vm);
    VecFx32 dest;
    s32 frames;
    s32 wait;
    s32 times;

    dest.x = VM_Read32(vm);
    dest.y = VM_Read32(vm);
    dest.z = VM_Read32(vm);
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    times = VM_Read32(vm);
#ifdef BWBATTLE
    // Start 14336 away from the destination to account for the script's remaining moves. When an opposing
    // Pokémon is present, leave room for it; after the battle, keep the trainer's usual platform position.
    if (wk->move == BTLV_EFFECT_TRAINER_SLIDE_IN && type == 0) {
        BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &dest, pos);
        if ((pos & 1) && (BtlvMcss_Exists(BtlvEffect_GetMcss(), 1)
                || BtlvMcss_Exists(BtlvEffect_GetMcss(), 3)
                || BtlvMcss_Exists(BtlvEffect_GetMcss(), 5))) {
            dest.x += BTLV_DIALOGUE_TRAINER_OFFSET;
        }
        dest.x += (pos & 1) ? -BTLV_SLIDE_IN_TRAVEL : BTLV_SLIDE_IN_TRAVEL;
    }
#endif
    if (BtlvMcss_SetAnimation(BtlvEffect_GetMcss(), pos, 1) == TRUE) {
        BtlvMcss_WatchAnimationEnd(BtlvEffect_GetMcss(), pos);
    }
    BtlvMcss_MovePosition(BtlvEffect_GetMcss(), pos, type, &dest, frames, wait, times);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssPlayAnim(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int pos = VM_Read32(vm);
    int anim = VM_Read32(vm);

    if (BtlvMcss_SetAnimation(BtlvEffect_GetMcss(), pos, anim) == TRUE) {
        BtlvMcss_SetAnimSpeed(BtlvEffect_GetMcss(), pos, FX32_ONE);
        BtlvMcss_WatchAnimationEnd(BtlvEffect_GetMcss(), pos);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdDeleteMcss(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    BtlvEffect_DelTrainer(VM_Read32(vm));
    return wk->ret;
}

static BOOL BtlvEffvm_CmdLoadBg(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 fileId = VM_Read32(vm);

    GFL_BGSysSetBGPriority(3, 1);
    GFL_BGSysLoadNCGRStatic(94, fileId + 1, 3, 0, 0, FALSE, HEAPID_TAIL(wk->heapId));
    loadBGScrToVramByNarcNoReserveNegAlign(94, fileId, 3, 0, 0, FALSE, HEAPID_TAIL(wk->heapId));
    PaletteFade_LoadNCLREx(BtlvEffect_GetPaletteFade(), 94, fileId + 2, HEAPID_TAIL(wk->heapId), 0, 0, 0x80, 0);
    wk->bgX = GFL_BGSysGetBGOffsetX(3);
    wk->bgY = GFL_BGSysGetBGOffsetY(3);
    BtlvBg_SetOffsetReq(BtlvEffect_GetBg(), 0, 0);
    wk->bgMoved = TRUE;
    return wk->ret;
}

static BOOL BtlvEffvm_CmdBgMove(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 mode = VM_Read32(vm);
    s32 x = VM_Read32(vm);
    s32 y = VM_Read32(vm);
    s32 frames = VM_Read32(vm);
    s32 wait = VM_Read32(vm);
    s32 count = VM_Read32(vm);
    u32 pos = BtlvEffvm_GetTargetPosition(vm, 14);

    BtlvBg_StartMove(BtlvEffect_GetBg(), pos, mode, x, y, frames, wait, count);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdBgWave(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 mode = VM_Read32(vm);
    fx32 amplitude = VM_Read32(vm);
    s32 step = VM_Read32(vm);
    s32 frames = VM_Read32(vm);
    u32 fadeMode = VM_Read32(vm);
    s32 fadeFrames = VM_Read32(vm);

    BtlvBg_StartWave(BtlvEffect_GetBg(), mode, amplitude, step, frames, fadeMode, fadeFrames);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdBgPalAnim(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmPalAnimWork *work =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmPalAnimWork), TRUE, "btlv_effvm.c", 3435);
    u32 fileId = VM_Read32(vm);
    u32 loops = VM_Read32(vm);
    u8 *data;
    const u8 *palettes;
    u16 *durations;
    u16 i;

    work->wk = wk;
    work->loops = loops;
    data = GFL_ArcSysReadHeapNewLZ(231, fileId, FALSE, HEAPID_TAIL(wk->heapId));
    work->fileId = fileId + 1;
    palettes = data;
    for (i = 0; *palettes != 0xff; i++) {
        work->palettes[i] = *palettes;
        palettes++;
    }
    work->count = i;
    i = 0;
    durations = (u16 *)(data + 0x80);
    while (*durations != 0xff98) {
        work->durations[i] = *durations;
        durations++;
        i++;
    }
    GFL_HeapFree(data);
    wk->paletteAnim = TRUE;
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_PalAnimTask, work, 0), BtlvEffvm_PalAnimTaskRemove,
                       1);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdBgSetPriority(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    GFL_BGSysSetBGPriority(3, (u8)VM_Read32(vm));
    wk->bgMoved = TRUE;
    return wk->ret;
}

static BOOL BtlvEffvm_CmdSetAlphaBlend(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int alpha;

    VM_Read32(vm);
    VM_Read32(vm);
    alpha = VM_Read32(vm);
    VM_Read32(vm);
    VM_Read32(vm);
    VM_Read32(vm);
#ifdef BWBATTLE
    // bwbattle: BG 3 is the 3D scene's background quad (btlv_bg3d.c), which blends by its polygon alpha; BG 1 is
    // Platinum's textbox
    BtlvBg3d_SetBlend(alpha == 31 ? 0 : alpha);
#else
    if (alpha == 31) {
        G2x_SetBlendAlpha_(0x4000050, 2, 0x3d, 31, 7);
    } else {
        G2x_SetBlendAlpha_(0x4000050, 10, 0x35, 31, alpha);
    }
#endif
    wk->alphaBlend = TRUE;
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCallDfc70(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 arg0 = VM_Read32(vm);
    u32 arg1 = VM_Read32(vm);
    u32 arg2 = VM_Read32(vm);
    u32 arg3 = VM_Read32(vm);
    u32 arg4 = VM_Read32(vm);

    BtlvEffect_StartPaletteFade(arg0, arg1, arg2, arg3, arg4);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCallDfd74(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 arg0 = VM_Read32(vm);
    u32 arg1 = VM_Read32(vm);

    BtlvEffect_SetVanish(arg0, arg1);
    return wk->ret;
}

BOOL BtlvEffect_QueueCommands(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmWindowWork *work =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmWindowWork), TRUE, "btlv_effvm.c", 3659);

    work->wk = wk;
    work->dir = VM_Read32(vm);
    work->winH = VM_Read32(vm);
    work->winV = VM_Read32(vm);
    work->winInOut = VM_Read32(vm);
    work->count = VM_Read32(vm);
    work->waitReload = VM_Read32(vm);
    work->keepWindow = VM_Read32(vm);
    GFL_BGSysSetBGEnabled(1, FALSE);
    GFL_BGSysSetBGEnabled(2, FALSE);
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_WindowTask, work, 0), BtlvEffvm_WindowTaskRemove, 1);
    wk->windowTask = TRUE;
    return wk->ret;
}

static BOOL BtlvEffvm_CmdClactAdd(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);
    u32 fileId = VM_Read32(vm);
    int pos = BtlvEffvm_GetTargetPosition(vm, VM_Read32(vm));
    fx32 offsetX = VM_Read32(vm);
    fx32 offsetY = VM_Read32(vm);
    fx32 scaleX = VM_Read32(vm);
    fx32 scaleY = VM_Read32(vm);
    VecFx32 world;
    int x;
    int y;

    BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &world, pos);
    world.x += offsetX;
    world.y += offsetY;
    NNS_G3DProject(&world, &x, &y);
    wk->clactIndexes[index] = BtlvClact_AddActor(BtlvEffect_GetClact(), 94, fileId, x, y, scaleX, scaleY);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdClactMove(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);
    int type = VM_Read32(vm);
    ClActorPos pos;
    int frames;
    int wait;
    int count;

    pos.x = VM_Read32(vm);
    pos.y = VM_Read32(vm);
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    count = VM_Read32(vm);
    BtlvClact_StartMove(BtlvEffect_GetClact(), wk->clactIndexes[index], type, &pos, frames, wait, count);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdClactScale(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);
    int type = VM_Read32(vm);
    VecFx32 scale;
    int frames;
    int wait;
    int count;

    scale.x = VM_Read32(vm);
    scale.y = VM_Read32(vm);
    scale.z = FX32_ONE;
    frames = VM_Read32(vm);
    wait = VM_Read32(vm);
    count = VM_Read32(vm);
    BtlvClact_StartScale(BtlvEffect_GetClact(), wk->clactIndexes[index], type, &scale, frames, wait, count);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdClactSetAnim(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);
    int sequence = VM_Read32(vm);

    BtlvClact_SetAnimSeq(BtlvEffect_GetClact(), wk->clactIndexes[index], sequence);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdClactPalFade(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);
    int start = VM_Read32(vm);
    int end = VM_Read32(vm);
    int delay = VM_Read32(vm);
    int color = VM_Read32(vm);

    BtlvClact_StartPalFade(BtlvEffect_GetClact(), wk->clactIndexes[index], start, end, delay, color);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdClactDelete(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);

    BtlvClact_DeleteActor(BtlvEffect_GetClact(), wk->clactIndexes[index]);
    wk->clactIndexes[index] = -1;
    return wk->ret;
}

// NONMATCHING: the original keeps &positions in r6 from the prologue on and recomputes &offset at each use; this
// keeps &offset in r6 and recomputes &positions, 2 bytes longer
static BOOL BtlvEffvm_CmdGauge(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 mode = VM_Read32(vm);
    int target = VM_Read32(vm);
    int targets[8];
    int positions[8];
    int count;
    int i;

    if (mode <= 1) {
        switch (target) {
        case 3:
            BtlvEffect_SetGaugeDrawEnable(mode, wk->attacker);
            break;
        case 4:
            count = BtlvEffvm_GetTargetPositions(wk, 16, targets);
            if (count != 0) {
                for (i = 0; i < count; i++) {
                    // BUG: every call passes the entry past the last target, which was never written
#ifdef BUGFIX
                    BtlvEffect_SetGaugeDrawEnable(mode, targets[i]);
#else
                    BtlvEffect_SetGaugeDrawEnable(mode, targets[count]);
#endif
                }
            }
            break;
        default:
            BtlvEffect_SetGaugeDrawEnableBySide(mode, target);
            break;
        }
    } else {
        count = 0;
        switch (target) {
        case 0:
            for (i = 0; i < 8; i += 2) {
                if (BtlvEffect_CheckExist(i)) {
                    positions[count++] = i;
                }
            }
            break;
        case 1:
            for (i = 1; i < 8; i += 2) {
                if (BtlvEffect_CheckExist(i)) {
                    positions[count++] = i;
                }
            }
            break;
        case 2:
            for (i = 0; i < 8; i++) {
                if (BtlvEffect_CheckExist(i)) {
                    positions[count++] = i;
                }
            }
            break;
        case 3:
            count = 1;
            positions[0] = wk->attacker;
            break;
        case 4:
            count = BtlvEffvm_GetTargetPositions(wk, 16, positions);
            break;
        }
        if (count != 0) {
            for (i = 0; i < count; i++) {
                ClActorPos offset = { 0, 0 };

                if (mode == 2) {
                    offset.x = 128;
                } else {
                    offset.x = -16;
                }
                if (positions[i] & 1) {
                    offset.x *= -1;
                }
                BtlvGauge_SetPos(BtlvEffect_GetGauge(), positions[i], &offset);
            }
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdPlaySE(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 se = VM_Read32(vm);
    u32 player = VM_Read32(vm);
    int pan = VM_Read32(vm);
    int wait = VM_Read32(vm);
    int arg4 = VM_Read32(vm);
    int arg5 = VM_Read32(vm);
    int vol = VM_Read32(vm);
    int pitch = VM_Read32(vm);

    VM_Read32(vm);
    BtlvEffvm_PlaySEAt(vm, se, player, pan, arg4, arg5, vol, pitch, wait);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdStopSE(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    s32 player = VM_Read32(vm);

    if (player == 5) {
        GFL_SndStop();
    } else {
        GFL_SndPlayerStop(player);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdPanSE(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    BtlvEffvmSeMoveWork *work =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmSeMoveWork), FALSE, "btlv_effvm.c", 4058);
    int from;
    int to;

    work->wk = wk;
    work->player = VM_Read32(vm);
    work->type = VM_Read32(vm);
    work->param = 2;
    from = BtlvEffvm_GetTargetPosition(vm, VM_Read32(vm));
    to = BtlvEffvm_GetTargetPosition(vm, VM_Read32(vm));
    work->start = (from & 1) ? 127 : -128;
    work->end = (to & 1) ? 127 : -128;
    work->delay = VM_Read32(vm);
    work->framesReset = work->frames = VM_Read32(vm);
    work->stepWait = 0;
    work->stepWaitReset = VM_Read32(vm);
    work->count = VM_Read32(vm) * 2;
    if (work->type == 1 && work->count == 0) {
        work->count = 2;
    }
    work->value = FX32_CONST(work->start);
    work->step = FX_Div(FX32_CONST(work->end - work->start), FX32_CONST(work->frames));
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_SeMoveTask, work, 0), BtlvEffvm_SeMoveTaskRemove, 1);
    wk->seSliding = TRUE;
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMoveSE(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 player = VM_Read32(vm);
    u32 type = VM_Read32(vm);
    u32 param = VM_Read32(vm);
    int start = VM_Read32(vm);
    int end = VM_Read32(vm);
    int delay = VM_Read32(vm);
    int frames = VM_Read32(vm);
    int stepWait = VM_Read32(vm);
    int count = VM_Read32(vm);

    BtlvEffvm_SlideSE(vm, player, type, param, start, end, delay, frames, stepWait, count);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdWait(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->waitKind = VM_Read32(vm);
    VM_SetNativeCallback(vm, BtlvEffvm_WaitNative);
    wk->ret = TRUE;
    return TRUE;
}

static BOOL BtlvEffvm_CmdWaitFrames(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->waitTimer = VM_Read32(vm);
    VM_SetNativeCallback(vm, BtlvEffvm_WaitTimerNative);
    wk->ret = TRUE;
    return TRUE;
}

static BOOL BtlvEffvm_CmdSetYield(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->ret = VM_Read32(vm);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdIfVarConst(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int value = BtlvEffvm_GetVariable(wk, VM_Read32(vm));
    int cond = VM_Read32(vm);
    int target = VM_Read32(vm);
    int offset = VM_Read32(vm);

    BtlvEffvm_JumpIf(vm, value, target, cond, offset);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdIfVarVar(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int value = BtlvEffvm_GetVariable(wk, VM_Read32(vm));
    int cond = VM_Read32(vm);
    int target = BtlvEffvm_GetVariable(wk, VM_Read32(vm));
    int offset = VM_Read32(vm);

    BtlvEffvm_JumpIf(vm, value, target, cond, offset);
    return wk->ret;
}

static void BtlvEffvm_JumpIf(VM *vm, int value, int target, int cond, int offset) {
    BOOL jump = FALSE;

    switch (cond) {
    case 0:
        if (value == target) {
            jump = TRUE;
        }
        break;
    case 1:
        if (value != target) {
            jump = TRUE;
        }
        break;
    case 2:
        if (value < target) {
            jump = TRUE;
        }
        break;
    case 3:
        if (value > target) {
            jump = TRUE;
        }
        break;
    case 4:
        if (value <= target) {
            jump = TRUE;
        }
        break;
    case 5:
        if (value >= target) {
            jump = TRUE;
        }
        break;
    }
    if (jump == TRUE) {
        VM_Jump(vm, vm->pc + offset);
    }
}

static BOOL BtlvEffvm_CmdIfPokeExists(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int pos = VM_Read32(vm);
    int value = VM_Read32(vm);
    int offset = VM_Read32(vm);

    if (value == BtlvEffect_CheckExist(pos)) {
        VM_Jump(vm, vm->pc + offset);
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdSetValue(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->value = VM_Read32(vm);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdLoadVar(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->value = BtlvEffvm_GetVariable(wk, VM_Read32(vm));
    return wk->ret;
}

static BOOL BtlvEffvm_CmdSetVar(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int id = VM_Read32(vm);
    int value = VM_Read32(vm);

    switch (id) {
    case 9:
        wk->param.unk0 = value;
        break;
    case 10:
        wk->param.variant = value;
        break;
    case 11:
        wk->param.unk2 = value;
        break;
    case 12:
        wk->param.unk3 = value;
        break;
    case 13:
        wk->param.unk4 = value;
        break;
    case 14:
        wk->param.unk8 = value;
        break;
    case 15:
        wk->param.itemNo = value;
        break;
    case 53:
        wk->mcssScaled = value;
        break;
    case 54:
        wk->camSaved = value;
        break;
    case 55:
        wk->unk2A0 = value;
        break;
    case 56:
        wk->cameraLocked = value;
        break;
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssEffect(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int type = VM_Read32(vm);
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int arg = VM_Read32(vm);
    int i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            BtlvMcss_SetSubstitute(BtlvEffect_GetMcss(), positions[i], type, arg);
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssSwap(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    BtlvMcss_Transform(BtlvEffect_GetMcss(), wk->defender, wk->attacker);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdPlayCry(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int positions[8];
    int count = BtlvEffvm_GetTargetPositions(wk, VM_Read32(vm), positions);
    int arg2 = VM_Read32(vm);
    int arg3 = VM_Read32(vm);
    int arg4 = VM_Read32(vm);
    int arg5 = VM_Read32(vm);
    int arg6 = VM_Read32(vm);
    int delay = VM_Read32(vm);
    int i;

    if (count != 0) {
        if (delay == 0) {
            for (i = 0; i < count; i++) {
                int slot = BtlvEffvm_GetFreeVoiceSlot(wk);

                wk->voices[slot] = BtlvMcss_PlayCry(BtlvEffect_GetMcss(), positions[i], arg2, arg3, arg4, arg5, arg6);
            }
            BtlvEffvm_StartBgmFade(wk, 0x7f000, 0x6b000, 10);
            if (!wk->bgmLowerRequested) {
                BtlvEffvmCryWaitWork *work = GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmCryWaitWork),
                                                              FALSE, "btlv_effvm.c", 4540);

                work->wk = wk;
                BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_CryWaitTask, work, 0), NULL, 0);
            }
        } else {
            for (i = 0; i < count; i++) {
                BtlvEffvmCryWork *work =
                    GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmCryWork), FALSE, "btlv_effvm.c", 4551);

                work->wk = wk;
                work->pos = positions[i];
                work->arg2 = arg2;
                work->arg3 = arg3;
                work->arg4 = arg4;
                work->arg5 = arg5;
                work->arg6 = arg6;
                work->delay = delay;
                wk->cryDelayed = TRUE;
                BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_CryDelayTask, work, 0),
                                   BtlvEffvm_CryDelayTaskRemove, 1);
            }
        }
    }
    return wk->ret;
}

static BOOL BtlvEffvm_CmdSetBallSource(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->ballSource = VM_Read32(vm);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdBallAdd(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int index = VM_Read32(vm);
    int pos = BtlvEffvm_GetTargetPosition(vm, VM_Read32(vm));
    fx32 offsetX = VM_Read32(vm);
    fx32 offsetY = VM_Read32(vm);
    fx32 scaleX = VM_Read32(vm);
    fx32 scaleY = VM_Read32(vm);
    VecFx32 world;
    int x;
    int y;
    int ball;

    BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &world, pos);
    world.x += offsetX;
    world.y += offsetY;
    NNS_G3DProject(&world, &x, &y);
    ball = PML_ItemGetMonsBallID(wk->param.itemNo);
    if (ball == 0 || ball > 25) {
        ball = 4;
    }
    if (ball == 25) {
        ball = 17;
    }
    wk->clactIndexes[index] =
        BtlvClact_AddActorEx(BtlvEffect_GetClact(), 94, ball * 2 + 231, ball * 2 + 232, 231, 232, x, y, scaleX, scaleY);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdCall(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 effect = VM_Read32(vm);
    u32 attacker = VM_Read32(vm);
    u32 defender = VM_Read32(vm);

    wk->callAttackers[wk->callDepth] = wk->attacker;
    wk->callDefenders[wk->callDepth] = wk->defender;
    wk->callScripts[wk->callDepth] = wk->script;
    wk->callValues[wk->callDepth] = wk->value;
    if (attacker != 14) {
        wk->attacker = attacker;
    }
    if (defender != 16) {
        wk->defender = defender;
    }
    wk->value = 0;
    wk->script = GFL_ArcSysReadHeapNew(66, effect - BTLV_EFFECT_FIRST, HEAPID_TAIL(wk->heapId));
    VM_Call(vm, (u8 *)wk->script + ((u32 *)wk->script)[1]);
    wk->callDepth++;
    return wk->ret;
}

static BOOL BtlvEffvm_CmdReturn(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->callDepth--;
    GFL_HeapFree(wk->script);
    wk->attacker = wk->callAttackers[wk->callDepth];
    wk->defender = wk->callDefenders[wk->callDepth];
    wk->script = wk->callScripts[wk->callDepth];
    wk->value = wk->callValues[wk->callDepth];
    VM_Return(vm);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdJump(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int offset = VM_Read32(vm);

    VM_Jump(vm, vm->pc + offset);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdWaitExternal(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->paused = TRUE;
    VM_SetNativeCallback(vm, BtlvEffvm_WaitResumeNative);
    wk->ret = TRUE;
    return TRUE;
}

static BOOL BtlvEffvm_CmdChangeScript(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int effect = VM_Read32(vm);
    void *script;

    GFL_HeapFree(wk->script);
    if (effect < BTLV_EFFECT_FIRST) {
        wk->script = GFL_ArcSysReadHeapNew(65, effect, HEAPID_TAIL(wk->heapId));
    } else {
        wk->script = GFL_ArcSysReadHeapNew(66, effect - BTLV_EFFECT_FIRST, HEAPID_TAIL(wk->heapId));
    }
    script = wk->script;
    wk->value = 0;
    VM_Jump(vm, (u8 *)script + ((u32 *)script)[1]);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdWaitSide(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    u32 side = VM_Read32(vm);
    BtlvEffvmSideWork *work =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmSideWork), FALSE, "btlv_effvm.c", 4883);

    work->wk = wk;
    work->side = side;
    work->doneMask = 0;
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_SideResetTask, work, 0), NULL, 1);
    return wk->ret;
}

static BOOL BtlvEffvm_CmdMcssReset(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    BtlvEffect_SetPokemonCheck(VM_Read32(vm));
    return wk->ret;
}

static BOOL BtlvEffvm_CmdEnd(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int i;
    BOOL visible;

    for (i = 0; i < 16; i++) {
        if (wk->particles[i] != NULL) {
            BtlvEffvm_DeleteParticle(wk->particles[i]);
            wk->particles[i] = NULL;
        }
        wk->particleIds[i] = -1;
    }
    for (i = 0; i < 4; i++) {
        if (wk->clactIndexes[i] != -1) {
            BtlvClact_DeleteActor(BtlvEffect_GetClact(), wk->clactIndexes[i]);
            wk->clactIndexes[i] = -1;
        }
    }
    for (i = 0; i < wk->allocCount; i++) {
        GFL_HeapFree(wk->allocs[i]);
    }
    wk->allocCount = 0;
    BtlvEffect_EndTaskGroup(1);
    BtlvEffvm_ResetMcssAll(wk);
    BtlvEffvm_ClearMcssFlagAll(wk);
    if (!wk->unk0_18) {
        BtlvEffect_SetPokemonCheck(1);
    }
    VM_Halt(vm);
    wk->ret = TRUE;
    if (wk->bgMoved) {
        BtlvScu *scu = BtlvEffect_GetScu();

        if (scu != NULL) {
            func_ov167_021d0ff8(scu);
        }
        GFL_BGSysSetBGPriority(3, 0);
        wk->bgMoved = FALSE;
        GFL_BGSysMoveBG(3, 0, wk->bgX);
        GFL_BGSysMoveBG(3, 3, wk->bgY);
    }
    if (wk->alphaBlend) {
#ifdef BWBATTLE
        BtlvBg3d_SetBlend(0);
#else
        G2x_SetBlendAlpha_(0x4000050, 2, 0x3d, 0x1f, 7);
#endif
        wk->alphaBlend = FALSE;
    }
    if (wk->sePlayed) {
        GFL_SndStop();
    }
    visible = FALSE;
    if (BtlvMcss_Exists(BtlvEffect_GetMcss(), wk->attacker)) {
        if (((wk->move == 248 || wk->move == 353) && wk->param.variant == 1) || wk->move == 144 || wk->move == 119 ||
            wk->move == 267) {
        } else {
            visible = BtlvMcss_GetFlags(BtlvEffect_GetMcss(), wk->attacker) & 1;
        }
    }
    if (wk->savedScript != NULL) {
        u32 offset;
        void *script;

        GFL_HeapFree(wk->script);
        script = wk->savedScript;
        wk->script = script;
        wk->savedScript = NULL;
        wk->isEffect = FALSE;
        offset = wk->savedOffset;
        GFL_BGSysSetBGEnabled(1, FALSE);
        GFL_BGSysSetBGEnabled(2, FALSE);
        GFL_BGSysSetBGEnabled(3, FALSE);
        wk->value = 0;
        VM_LoadScript(vm, (u8 *)wk->script + *(u32 *)((u8 *)script + offset));
        return TRUE;
    }
    if (wk->substituteRestore == TRUE && wk->move == 0x291) {
        GFL_HeapFree(wk->script);
        wk->move = 52;
        wk->script = GFL_ArcSysReadHeapNew(66, wk->move, HEAPID_TAIL(wk->heapId));
        wk->isEffect = TRUE;
        wk->value = 0;
        wk->substituteRestore = FALSE;
        VM_LoadScript(vm, (u8 *)wk->script + ((u32 *)wk->script)[1]);
        return TRUE;
    }
    if ((wk->move == 0x286 || !wk->isEffect) && wk->move != 144 && wk->move != 226 && !wk->substituteRestore &&
        visible) {
        if (wk->move == 0x1f6) {
            BtlvMcss_SetVanish(BtlvEffect_GetMcss(), wk->attacker, 0);
        }
        GFL_HeapFree(wk->script);
        wk->move = 52;
        wk->script = GFL_ArcSysReadHeapNew(66, wk->move, HEAPID_TAIL(wk->heapId));
        wk->isEffect = TRUE;
        wk->value = 0;
        wk->substituteRestore = FALSE;
        VM_LoadScript(vm, (u8 *)wk->script + ((u32 *)wk->script)[1]);
        return TRUE;
    }
#ifdef BWBATTLE
    // bwbattle: BW2 reloads its UI layer onto BG 3 (func_ov167_021d0ff8); here BG 3 is only the effects' background
    BtlvBg3d_Unload();
#endif
    if (wk->move != 0x27e) {
        GFL_BGSysSetBGEnabled(1, TRUE);
        GFL_BGSysSetBGEnabled(2, TRUE);
        GFL_BGSysSetBGEnabled(3, TRUE);
    }
    return TRUE;
}

static BOOL BtlvEffvm_WaitNative(VM *vm, void *env) {
    BtlvEffvm *wk = env;
    int i;
    BtlvMcssPos pos;

    if (wk->waitKind <= 1) {
        if (BtlvCamera_IsMoving(BtlvEffect_GetCamera()) == TRUE) {
            return FALSE;
        }
        if (wk->cameraReset) {
            wk->cameraReset = FALSE;
            wk->unk244 = 1;
            BtlvMcss_SetFlatAll(BtlvEffect_GetMcss());
        }
    }
    if (wk->waitKind == 0 || wk->waitKind == 3) {
        for (pos = 0; pos < 14; pos++) {
            if (BtlvMcss_IsBusy(BtlvEffect_GetMcss(), pos) == TRUE) {
                return FALSE;
            }
        }
    }
    if (wk->waitKind == 4) {
        for (pos = 8; pos < 14; pos++) {
            if (BtlvMcss_Exists(BtlvEffect_GetMcss(), pos) &&
                BtlvMcss_IsAnimationSet(BtlvEffect_GetMcss(), pos) == TRUE) {
                return FALSE;
            }
        }
    }
    if (wk->waitKind == 0 || wk->waitKind == 5) {
        if (BtlvBg_IsMoving(BtlvEffect_GetBg()) == TRUE) {
            return FALSE;
        }
    }
    if (wk->waitKind == 0 || wk->waitKind == 2) {
        for (i = 0; i < 16; i++) {
            if (wk->particles[i] != NULL && func_020500a8(wk->particles[i])) {
                return FALSE;
            }
        }
    }
    if (wk->waitKind == 0 || wk->waitKind == 6 || wk->waitKind == 8) {
        if (BtlvEffect_IsPaletteFading(0)) {
            return FALSE;
        }
    }
    if (wk->waitKind == 0 || wk->waitKind == 7 || wk->waitKind == 8) {
        if (BtlvEffect_IsPaletteFading(1)) {
            return FALSE;
        }
    }
    if (wk->waitKind == 0 || wk->waitKind == 9) {
        if (BtlvEffect_IsPaletteFading(3)) {
            return FALSE;
        }
    }
    if (wk->waitKind == 10) {
        if (GFL_SndPlayerIsActiveAny() || wk->seDelayed || wk->seSliding) {
            return FALSE;
        }
    }
    if (wk->waitKind == 11) {
        if (GFL_SndPlayerIsActive(1) || wk->seDelayed || wk->seSliding) {
            return FALSE;
        }
    }
    if (wk->waitKind == 12) {
        if (GFL_SndPlayerIsActive(2) || wk->seDelayed || wk->seSliding) {
            return FALSE;
        }
    }
    if (wk->waitKind == 13) {
        if (GFL_SndPlayerIsActive(4) || wk->seDelayed || wk->seSliding) {
            return FALSE;
        }
    }
    if (wk->waitKind == 14) {
        if (GFL_SndPlayerIsActive(3) || wk->seDelayed || wk->seSliding) {
            return FALSE;
        }
    }
    if (wk->waitKind == 15) {
        if (GFL_SndPlayerIsActive(0) || wk->seDelayed || wk->seSliding) {
            return FALSE;
        }
    }
    if (wk->waitKind == 16) {
        if (wk->cryDelayed) {
            return FALSE;
        }
        for (i = 0; i < 6; i++) {
            if (wk->voices[i] != -1) {
                if (PokeVoice_IsPlaying(wk->voices[i])) {
                    return FALSE;
                }
                wk->voices[i] = -1;
            }
        }
    }
    if ((wk->waitKind == 0 || wk->waitKind == 17) && wk->windowTask) {
        return FALSE;
    }
    if ((wk->waitKind == 0 || wk->waitKind == 18) && wk->paletteAnim) {
        return FALSE;
    }
    if (wk->waitKind == 0 || wk->waitKind == 19) {
        if (BtlvBg_IsWaving(BtlvEffect_GetBg()) == TRUE) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL BtlvEffvm_WaitTimerNative(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    wk->waitTimer--;
    return wk->waitTimer <= 0;
}

static BOOL BtlvEffvm_WaitResumeNative(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    if (wk->paused) {
        return FALSE;
    }
    return TRUE;
}

static BOOL BtlvEffvm_WaitParticleLoadNative(VM *vm, void *env) {
    BtlvEffvm *wk = env;

    if (wk->particleLoading) {
        return FALSE;
    }
    return TRUE;
}

static int BtlvEffvm_GetTargetPositions(BtlvEffvm *wk, int code, int *positions) {
    int count = 1;
    int rule;
    int i;
    BtlvMcssPos pos;
    int start;
    int end;

    switch (code) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
        positions[0] = code;
        break;
    case 14:
        positions[0] = wk->attacker;
        if (positions[0] == 0xff) {
            if (BtlvEffect_CheckExist(0) == TRUE) {
                positions[0] = 0;
            } else if (BtlvEffect_CheckExist(2) == TRUE) {
                positions[0] = 2;
            } else if (BtlvEffect_CheckExist(4) == TRUE) {
                positions[0] = 4;
                // BUG: tests position 4 again where position 6 is meant
#ifdef BUGFIX
            } else if (BtlvEffect_CheckExist(6) == TRUE) {
#else
            } else if (BtlvEffect_CheckExist(4) == TRUE) {
#endif
                positions[0] = 6;
            }
        }
        break;
    case 15:
        if (wk->attacker > 1) {
            positions[0] = wk->attacker ^ 2;
        } else {
            positions[0] = 0xff;
        }
        break;
    case 16:
        positions[0] = wk->defender;
        if (positions[0] != 0xff) {
            break;
        }
        rule = BtlvEffect_GetBattleStyle();
        count = 0;
        if (rule == 0 || rule == 3) {
            if (wk->param.unk0 == 8 || wk->param.unk0 == 10) {
                count = 1;
                positions[0] = wk->attacker;
                if (BtlvEffect_CheckExist(wk->attacker ^ 1)) {
                    positions[1] = wk->attacker ^ 1;
                    count++;
                }
            } else {
                count = 0;
                if (BtlvEffect_CheckExist(wk->attacker ^ 1)) {
                    positions[0] = wk->attacker ^ 1;
                    count++;
                }
            }
            break;
        }
        switch (wk->param.unk0) {
        case 0:
        case 1:
        case 2:
        case 3:
            break;
        case 4:
            if (rule == 2 && wk->attacker != 4 && wk->attacker != 5) {
                int targets[4][3] = {
                    { 4, 5, 7 },
                    { 4, 5, 6 },
                    { 3, 4, 5 },
                    { 2, 4, 5 },
                };
                int row = (wk->attacker & 1) + (wk->attacker > 3 ? 2 : 0);

                for (i = 0; i < 3; i++) {
                    if (BtlvEffect_CheckExist(targets[row][i])) {
                        positions[count] = targets[row][i];
                        count++;
                    }
                }
            } else {
                for (pos = 2; pos <= 7; pos++) {
                    if (wk->attacker != pos && BtlvEffect_CheckExist(pos)) {
                        positions[count] = pos;
                        count++;
                    }
                }
            }
            break;
        case 5:
        case 7:
        case 9:
        case 13:
            start = (wk->attacker & 1) ? 2 : 3;
            end = rule == 2 ? 7 : 5;
            if (rule == 2) {
                if (wk->attacker <= 3) {
                    start += 2;
                }
                if (wk->attacker >= 6) {
                    end -= 2;
                }
            }
            for (pos = start; pos <= end; pos += 2) {
                if (BtlvEffect_CheckExist(pos)) {
                    positions[count] = pos;
                    count++;
                }
            }
            break;
        case 11:
            for (pos = (wk->attacker & 1) ? 2 : 3; pos <= 7; pos += 2) {
                if (BtlvEffect_CheckExist(pos)) {
                    positions[count] = pos;
                    count++;
                }
            }
            break;
        case 6:
        case 12:
            for (pos = (wk->attacker & 1) ? 3 : 2; pos <= 7; pos += 2) {
                if (BtlvEffect_CheckExist(pos)) {
                    positions[count] = pos;
                    count++;
                }
            }
            break;
        case 8:
        case 10:
            for (pos = 2; pos <= 7; pos++) {
                if (BtlvEffect_CheckExist(pos)) {
                    positions[count] = pos;
                    count++;
                }
            }
            break;
        }
        break;
    case 17:
        if (wk->defender > 1) {
            positions[0] = wk->defender ^ 2;
        } else {
            positions[0] = 0xff;
        }
        break;
    case 18:
        count = 0;
        for (pos = 0; pos <= 7; pos++) {
            if (BtlvEffect_CheckExist(pos)) {
                positions[count] = pos;
                count++;
            }
        }
        break;
    case 19:
    case 20:
        start = 0;
        if (code != 19) {
            start = 1;
        }
        count = 0;
        for (pos = start; pos <= 7; pos += 2) {
            if (BtlvEffect_CheckExist(pos)) {
                positions[count] = pos;
                count++;
            }
        }
        break;
    }
    if (positions[0] != 0xff && positions[0] < 8) {
        if (BtlvEffect_CheckExist(positions[0]) == TRUE) {
            positions[0] = BtlvEffvm_CheckPosition(wk, positions[0]);
        } else {
            positions[0] = 0xff;
            count = 0;
        }
    }
    return count;
}

static int BtlvEffvm_GetDefenderPositions(BtlvEffvm *wk, int code, int *positions) {
    int count = 1;
    int rule;
    int i;
    BtlvMcssPos pos;

    positions[0] = wk->defender;
    if (positions[0] == 0xff) {
        rule = BtlvEffect_GetBattleStyle();
        count = 0;
        if (rule == 0 || rule == 3) {
            if (wk->param.unk0 == 8 || wk->param.unk0 == 10) {
                count = 1;
                positions[0] = wk->attacker;
                if (BtlvEffect_CheckExist(wk->attacker ^ 1)) {
                    positions[1] = wk->attacker ^ 1;
                    count++;
                }
            } else {
                count = 0;
                if (BtlvEffect_CheckExist(wk->attacker ^ 1)) {
                    positions[0] = wk->attacker ^ 1;
                    count++;
                }
            }
        } else {
            switch (wk->param.unk0) {
            case 0:
            case 1:
            case 2:
            case 3:
                break;
            case 4:
                if (rule == 2 && wk->attacker != 4 && wk->attacker != 5) {
                    int targets[4][3] = {
                        { 4, 5, 7 },
                        { 4, 5, 6 },
                        { 3, 4, 5 },
                        { 2, 4, 5 },
                    };
                    int row = (wk->attacker & 1) + (wk->attacker > 3 ? 2 : 0);

                    for (i = 0; i < 3; i++) {
                        if (BtlvEffect_CheckExist(targets[row][i]) &&
                            !BtlvMcss_GetVanish(BtlvEffect_GetMcss(), targets[row][i])) {
                            positions[count] = targets[row][i];
                            count++;
                        }
                    }
                } else {
                    for (pos = 2; pos <= 7; pos++) {
                        if (wk->attacker != pos && BtlvEffect_CheckExist(pos) &&
                            !BtlvMcss_GetVanish(BtlvEffect_GetMcss(), pos)) {
                            positions[count] = pos;
                            count++;
                        }
                    }
                }
                break;
            case 5:
            case 7:
            case 9:
            case 13:
                if (rule == 1) {
                    positions[0] = (wk->attacker & 1) ? 0 : 1;
                    count = 1;
                } else if (wk->attacker != 4 && wk->attacker != 5) {
                    positions[0] = data_ov168_021f4024[wk->attacker];
                    count = 1;
                } else {
                    count = 1;
                    positions[0] = wk->attacker ^ 1;
                }
                break;
            case 11:
                if (rule == 1) {
                    positions[0] = (wk->attacker & 1) ? 0 : 1;
                    count = 1;
                } else if (wk->attacker != 4 && wk->attacker != 5) {
                    positions[0] = data_ov168_021f4044[wk->attacker];
                    count = 1;
                } else {
                    count = 1;
                    positions[0] = wk->attacker ^ 1;
                }
                break;
            case 6:
            case 12:
                for (pos = (wk->attacker & 1) ? 3 : 2; pos <= 7; pos += 2) {
                    if (BtlvEffect_CheckExist(pos) && !BtlvMcss_GetVanish(BtlvEffect_GetMcss(), pos)) {
                        positions[count] = pos;
                        count++;
                    }
                }
                break;
            case 8:
            case 10:
                for (pos = 2; pos <= 7; pos++) {
                    if (BtlvEffect_CheckExist(pos) && !BtlvMcss_GetVanish(BtlvEffect_GetMcss(), pos)) {
                        positions[count] = pos;
                        count++;
                    }
                }
                break;
            }
        }
    }
    return count;
}

static int BtlvEffvm_GetTargetPosition(VM *vm, int code) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    int pos;

    switch (code) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
        pos = code;
        break;
    case 14:
        pos = wk->attacker;
        if (pos == 0xff) {
            if (BtlvEffect_CheckExist(2) == TRUE) {
                return 2;
            }
            if (BtlvEffect_CheckExist(4) == TRUE) {
                return 4;
            }
            if (BtlvEffect_CheckExist(6) == TRUE) {
                return 6;
            }
            return 0;
        }
        break;
    case 15:
        if (wk->attacker > 1) {
            pos = wk->attacker ^ 2;
        } else {
            pos = 0xff;
        }
        break;
    case 16:
        pos = wk->defender;
        if (pos == 0xff) {
            if (wk->attacker & 1) {
                if (BtlvEffect_CheckExist(2) == TRUE) {
                    return 2;
                }
                if (BtlvEffect_CheckExist(4) == TRUE) {
                    return 4;
                }
                if (BtlvEffect_CheckExist(6) == TRUE) {
                    return 6;
                }
                return 0;
            }
            if (BtlvEffect_CheckExist(3) == TRUE) {
                return 3;
            }
            if (BtlvEffect_CheckExist(5) == TRUE) {
                return 5;
            }
            if (BtlvEffect_CheckExist(7) == TRUE) {
                return 7;
            }
            return 1;
        }
        break;
    case 17:
        if (wk->defender > 1) {
            pos = wk->defender ^ 2;
        } else {
            pos = 0xff;
        }
        break;
    }
    return BtlvEffvm_SwapSide(vm, pos);
}

static int BtlvEffvm_CheckPosition(BtlvEffvm *wk, int pos) {
    if (BtlvEffect_CheckExist(pos) == TRUE) {
        if (wk->flipSides) {
            pos ^= 1;
        }
    } else {
        pos = 0xff;
    }
    return pos;
}

static int BtlvEffvm_SwapSide(VM *vm, int pos) {
    BtlvEffvm *wk = VM_GetEnv(vm);

    if (wk->flipSides) {
        pos ^= 1;
    }
    return pos;
}

static int BtlvEffvm_GetDefenderForEmitter(VM *vm, u32 *pos) {
    BtlvEffvm *wk = VM_GetEnv(vm);
    int ret = BtlvEffect_GetBattleStyle();

    *pos = wk->defender;
    if (*pos == 0xff) {
        if (ret == 0 || ret == 3 || wk->isEffect == TRUE) {
            *pos = wk->attacker ^ 1;
        } else {
            switch (wk->param.unk0) {
            case 4:
            case 5:
            case 8:
            case 10:
                if (ret == 1) {
                    ret = 0;
                    *pos = (wk->attacker & 1) ? ret : 1;
                } else if (wk->attacker != 4 && wk->attacker != 5) {
                    ret = 1;
                    *pos = data_ov168_021f4064[wk->attacker];
                } else {
                    *pos = wk->attacker ^ 1;
                }
                break;
            case 7:
            case 9:
            case 13:
                *pos = wk->attacker ^ 1;
                break;
            case 11:
                ret = 0;
                *pos = (wk->attacker & 1) ? ret : 1;
                break;
            case 6:
            case 12:
                ret = 0;
                *pos = (wk->attacker & 1) ? 1 : ret;
                break;
            }
        }
    }
    return ret;
}

static BOOL BtlvEffvm_FindOrAddParticleId(BtlvEffvm *wk, u32 id, int *index) {
    int i;
    BOOL isNew = TRUE;

    for (i = 0; i < 16; i++) {
        if (id == wk->particleIds[i]) {
            isNew = FALSE;
            break;
        }
    }
    if (i == 16) {
        for (i = 0; i < 16; i++) {
            if (wk->particleIds[i] == -1) {
                wk->particleIds[i] = id;
                break;
            }
        }
    }
    *index = i;
    return isNew;
}

static void BtlvEffvm_SetEmitterCount(BtlvEffvm *wk, int index, void *resource) {
    wk->emitterCounts[index] = ((u16 *)resource)[4];
}

static int BtlvEffvm_FindParticleId(BtlvEffvm *wk, u32 id) {
    int i;

    for (i = 0; i < 16; i++) {
        if (id == wk->particleIds[i]) {
            break;
        }
    }
    return i;
}

static u32 BtlvEffvm_GetEmitterCount(BtlvEffvm *wk, int index) {
    return wk->emitterCounts[index];
}

// NONMATCHING: the swap of motion type 4 is scheduled differently (from.z spilled, stores interleaved)
static void BtlvEffvm_InitEmitter(SPLEmitter *emitter) {
    BtlvEffvmEmitParam *param = func_02050188();
    BOOL flip = param->flip;
    VecFx32 from;
    VecFx32 to;
    VecFx32 start;
    VecFx32 axis;
    VecFx32 dir;
    VecFx32 base;
    MtxFx43 mtx;
    VecFx32 vec;
    VecFx32 flat;
    VecFx32 rotAxis;
    VecFx32 cameraPos;
    u16 spinAxis;
    VecFx16 emitterAxis;
    u16 angle;
    u32 rule;

    switch (param->from) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        param->from = BtlvEffvm_SwapSide(param->vm, param->from);
        break;
    case 9:
    case 11:
        param->from = BtlvEffvm_GetTargetPosition(param->vm, param->from + 5);
        break;
    case 8:
        from.x = param->fromPos.x;
        from.y = param->fromPos.y;
        from.z = param->fromPos.z;
        break;
    case 14:
    case 15:
    case 16:
    case 17:
        BtlvMcss_GetDefaultPosForStyle(BtlvEffect_GetMcss(), &from, param->from - 12, 1);
        param->from = -1;
        break;
    case 18:
    case 19:
    case 20:
    case 21:
        BtlvMcss_GetDefaultPosForStyle(BtlvEffect_GetMcss(), &from, param->from - 16, 1);
        switch (param->from) {
        case 18:
            from.z += 0x1a00;
            break;
        case 19:
            from.z += 0x2600;
            break;
        case 20:
            from.z += 0x600;
            break;
        case 21:
            from.z += 0x3900;
            break;
        }
        param->from = -1;
        break;
    }
    if (param->from != -1 && param->from != 8) {
        BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &from, param->from);
    }
    switch (param->to) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        param->to = BtlvEffvm_SwapSide(param->vm, param->to);
        break;
    case 9:
        param->to = BtlvEffvm_GetTargetPosition(param->vm, param->to + 5);
        break;
    case 10:
        flip = TRUE;
        param->to = BtlvEffvm_GetTargetPosition(param->vm, param->to + 4);
        break;
    case 12:
        flip = TRUE;
    case 11:
        rule = BtlvEffvm_GetDefenderForEmitter(param->vm, &param->to);
        BtlvMcss_GetDefaultPosForStyle(BtlvEffect_GetMcss(), &to, param->to, rule);
        param->to = -1;
        break;
    case 8:
        to.x = param->toPos.x;
        to.y = param->toPos.y;
        to.z = param->toPos.z;
        break;
    case 14:
    case 15:
    case 16:
    case 17:
        BtlvMcss_GetDefaultPosForStyle(BtlvEffect_GetMcss(), &to, param->to - 12, 1);
        param->to = -1;
        break;
    case 18:
    case 19:
    case 20:
    case 21:
        BtlvMcss_GetDefaultPosForStyle(BtlvEffect_GetMcss(), &to, param->to - 16, 1);
        // The start's code, not the target's, picks the height, and the start's is no longer 18-21 by now
        switch (param->from) {
        case 18:
            to.z += 0x1a00;
            break;
        case 19:
            to.z += 0x2600;
            break;
        case 20:
            to.z += 0x600;
            break;
        case 21:
            to.z += 0x3900;
            break;
        }
        param->to = -1;
        break;
    }
    if (param->to != -1 && param->to != 8) {
        BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &to, param->to);
    }
    BtlvEffect_GetBattleStyle();
    from.z += 0x500;
    to.z += 0x500;
    if (param->offsetMode == 1 || param->offsetMode == 2) {
        BtlvEffvm_ProjectToScreen(&from, &param->offset);
        BtlvEffvm_ProjectToScreen(&to, &param->offset);
    } else if (param->offsetMode == 3) {
        start.x = from.x;
        start.y = from.y;
        start.z = from.z;
        from.y += param->offset.y;
        BtlvEffvm_ProjectToScreen(&start, &param->offset);
    } else {
        from.y += param->offset.y;
    }
    to.y += param->offset.y;
    if (param->motionType != 0) {
        BtlvEffvm *wk = VM_GetEnv(param->vm);
        BtlvEffvmEmitterMotionWork *work;

        wk->allocs[wk->allocCount] =
            GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitterMotionWork), FALSE, "btlv_effvm.c", 6693);
        work = wk->allocs[wk->allocCount];
        func_0205035c(emitter, work);
        func_02050354(emitter, BtlvEffvm_MoveEmitterArc);
        wk->allocCount++;
        work->angle = 0;
        work->wait = 0;
        work->waitAccum = 0;
        work->waitStep = 0;
        work->type = param->motionType;
        work->frames = param->motionFrames >> FX32_SHIFT;
        work->offsetMode = param->offsetMode;
        if (param->motionType == 3) {
            work->angleStep = FX_Div(0x6000 * FX32_ONE, param->motionFrames);
        } else if (param->motionType == 5 || param->motionType == 6) {
            work->angleStep = FX_Div(0x8000 * FX32_ONE, param->motionFrames);
            work->angle2Step = FX_Div(0x10000 * FX32_ONE, param->motionFrames);
            work->angle2Step = FX_Mul(work->angle2Step, param->turns);
        } else {
            work->angleStep = FX_Div(0x8000 * FX32_ONE, param->motionFrames);
        }
        if (work->angleStep == 0) {
            work->angleStep = 1;
        }
        work->radius = VEC_Distance(&from, &to) / 2;
        if (param->motionType == 1 || param->motionType == 4) {
            work->height = 0;
        } else {
            work->height = param->motionHeight;
        }
        if (param->motionType == 4) {
            fx32 x = from.x;
            fx32 y = from.y;
            fx32 z = from.z;

            from.x = to.x;
            from.y = to.y;
            from.z = to.z;
            to.x += x;
            to.y += y;
            to.z += z;
        }
        dir.x = to.x - from.x;
        dir.y = to.y - from.y;
        dir.z = to.z - from.z;
        VEC_Normalize(&dir, &dir);
        base.x = FX32_ONE;
        base.y = 0;
        base.z = 0;
        angle = FX_AcosIdx(VEC_DotProduct(&base, &dir));
        VEC_CrossProduct(&base, &dir, &axis);
        VEC_Normalize(&axis, &axis);
        MTX_RotAxis43(&work->mtx, &axis, FX_SinIdx(angle), FX_CosIdx(angle));
        work->mtx._30 = from.x;
        work->mtx._31 = from.y;
        work->mtx._32 = from.z;
    }
    if (param->from != 8 && param->to != 8 && param->from != param->to) {
        spinAxis = 2;
        to.x -= from.x;
        to.y -= from.y;
        to.z -= from.z;
        func_02050310(emitter, &to);
        func_0205033c(emitter, &to);
        flat.x = to.x;
        flat.y = 0;
        flat.z = to.z;
        func_020501b8(emitter, &emitterAxis);
        vec.x = emitterAxis.x;
        vec.y = 0;
        vec.z = emitterAxis.z;
        VEC_Normalize(&vec, &vec);
        VEC_Normalize(&flat, &flat);
        angle = FX_AcosIdx(VEC_DotProduct(&vec, &flat));
        VEC_CrossProduct(&vec, &flat, &rotAxis);
        VEC_Normalize(&rotAxis, &rotAxis);
        MTX_RotAxis43(&mtx, &rotAxis, FX_SinIdx(angle), FX_CosIdx(angle));
        vec.x = emitterAxis.x;
        vec.y = emitterAxis.y;
        vec.z = emitterAxis.z;
        MTX_MultVec43(&vec, &mtx, &vec);
        VEC_Normalize(&vec, &vec);
        if (flip == TRUE) {
            vec.x *= -1;
            vec.z *= -1;
        }
        emitterAxis.x = vec.x;
        emitterAxis.y = vec.y;
        emitterAxis.z = vec.z;
        if (!((angle >= 0x2000 && angle <= 0x6000) || (angle >= 0xa000 && angle <= 0xe000))) {
            spinAxis = 0;
        }
        func_02050328(emitter, &spinAxis);
        func_02050230(emitter, &emitterAxis);
    }
    if ((param->offsetMode != 0 && (param->from & 1)) || param->offsetMode == 0 || param->offsetMode == 2 ||
        param->offsetMode == 3) {
        fx32 radius = func_020501d0(emitter);
        fx32 lifeTime = func_02050200(emitter) << FX32_SHIFT;
        fx16 scale = func_020501f8(emitter);
        fx32 length = func_020501d8(emitter);
        fx16 amplifier;

        if (param->radiusScale != 0) {
            func_020501d4(emitter, FX_Mul(radius, param->radiusScale));
            func_020501dc(emitter, FX_Mul(length, param->radiusScale));
        }
        if (param->lifeTimeScale != 0) {
            func_0205024c(emitter, FX_Mul(lifeTime, param->lifeTimeScale) >> FX32_SHIFT);
        }
        if (param->scaleScale != 0) {
            func_02050248(emitter, (fx16)FX_Mul(scale, param->scaleScale));
        }
        if (param->cameraScale != 0) {
            // func_02050198 and func_020501a8 take the emitter here, though gfl/particle.h types them for a system
            func_02050198((ParticleSystem *)emitter, &cameraPos);
            cameraPos.x = FX_MUL(cameraPos.x, param->cameraScale);
            cameraPos.y = FX_MUL(cameraPos.y, param->cameraScale);
            cameraPos.z = FX_MUL(cameraPos.z, param->cameraScale);
            func_020501a8((ParticleSystem *)emitter, &cameraPos);
            amplifier = func_020501e0(emitter);
            func_020501e8(emitter, (fx16)FX_MUL(amplifier, param->cameraScale));
            amplifier = func_020501ec(emitter);
            func_020501f4(emitter, (fx16)FX_MUL(amplifier, param->cameraScale));
        }
    }
    if (param->offsetMode == 3) {
        func_02050208(emitter, &start);
    } else {
        func_02050208(emitter, &from);
    }
    GFL_HeapFree(param);
}

static void BtlvEffvm_MoveEmitterArc(SPLEmitter *emitter, enum SPLUpdateCallbackType type) {
    BtlvEffvmEmitterMotionWork *work = func_02050364(emitter);
    VecFx32 pos;
    u16 angle;
    u16 angle2;

    if (work->frames != 0 && type != 0) {
        if (work->type == 3) {
            if (work->wait != 0) {
                work->wait--;
                return;
            }
            work->waitStep += 12;
            work->waitAccum += work->waitStep;
            work->wait = work->waitAccum >> FX32_SHIFT;
        }
        work->frames--;
        work->angle += work->angleStep;
        work->angle2 += work->angle2Step;
        angle = (work->angle >> FX32_SHIFT) + 0xc000;
        angle2 = (work->angle2 >> FX32_SHIFT) + 0xc000;
        pos.x = FX_SinIdx(angle);
        pos.x = FX_Mul(pos.x, work->radius);
        pos.x += work->radius;
        if (work->type == 6) {
            pos.z = FX_CosIdx(angle2);
            pos.z = FX_Mul(pos.z, work->height);
            pos.y = 0;
        } else if (work->type == 5) {
            pos.y = FX_CosIdx(angle2);
            pos.y = FX_Mul(pos.y, work->height);
            pos.z = 0;
        } else {
            pos.y = FX_CosIdx(angle);
            pos.y = FX_Mul(pos.y, work->height);
            pos.z = 0;
        }
        MTX_MultVec43(&pos, &work->mtx, &pos);
        if (work->offsetMode != 0) {
            BtlvEffvm_ProjectToScreen(&pos, NULL);
        }
        func_02050208(emitter, &pos);
    }
}

static void BtlvEffvm_InitCircleEmitter(SPLEmitter *emitter) {
    BtlvEffvmEmitterCircleWork *work = func_02050188();

    if (work->ortho && work->center.z < 0) {
        func_02050248(emitter, FX32_HALF);
    }
    func_0205035c(emitter, work);
    func_02050354(emitter, BtlvEffvm_MoveCircleEmitter);
}

static void BtlvEffvm_MoveCircleEmitter(SPLEmitter *emitter, enum SPLUpdateCallbackType type) {
    BtlvEffvmEmitterCircleWork *work = func_02050364(emitter);
    VecFx32 pos;

    if (type != 0) {
        if (work->delay == 0) {
            if (work->wait == 0) {
                work->wait = work->waitReset;
                if (work->type & 1) {
                    work->angle = work->angle - work->angleStep;
                } else {
                    work->angle = work->angle + work->angleStep;
                }
                work->angle = (u16)work->angle;
                pos.x = FX_SinIdx(work->angle);
                pos.x = FX_Mul(pos.x, work->radiusX);
                pos.x += work->center.x;
                pos.y = work->center.y;
                pos.z = FX_CosIdx(work->angle);
                pos.z = FX_Mul(pos.z, work->radiusZ);
                pos.z += work->center.z;
                if (work->ortho) {
                    BtlvEffvm_ProjectToScreen(&pos, NULL);
                }
                func_02050208(emitter, &pos);
                if (work->frames != 0) {
                    work->frames--;
                } else {
                    work->frames = work->framesReset;
                    work->delay = work->delayReset;
                }
            } else {
                work->wait--;
            }
        } else {
            work->delay--;
        }
    }
}

static void BtlvEffvm_DeleteParticle(ParticleSystem *particle) {
    void *work = func_020500c8(particle);

    func_0204fa84(particle);
    GFL_HeapFree(work);
}

static void BtlvEffvm_ResetMcss(BtlvEffvm *wk) {
    if (wk->unk244 == 1) {
        BtlvMcss_ClearFlatAll(BtlvEffect_GetMcss());
    } else {
        BtlvMcss_SetFlatAll(BtlvEffect_GetMcss());
    }
}

static void BtlvEffvm_PlaySENow(u32 se, u32 player, int pan, int arg3, int arg4, int vol, int pitch) {
    if (player == 5) {
        GFL_SndSEPlay(se);
        player = GFL_SndSeqGetPlayerIndex(se);
    } else {
        GFL_SEPlayKeepVol(se, player);
    }
    NNS_SndPlayerSetVolume(func_020061a8(player), arg4);
    GFL_SndPlayerSetParams(player, -1, -1, pan);
    GFL_SndPlayerSetParams(player, -1, arg3, -1);
    if (vol != 0) {
        NNS_SndPlayerSetTrackModDepth(func_020061a8(player), 0xffff, vol);
        NNS_SndPlayerSetTrackModSpeed(func_020061a8(player), 0xffff, pitch);
    }
}

// A script variable; an unknown id returns whatever r0 holds, as the original does
static u32 BtlvEffvm_GetVariable(BtlvEffvm *wk, int id) {
    switch (id) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return BtlvMcss_GetWeight(BtlvEffect_GetMcss(), id);
    case 8:
        return BtlvMcss_GetWeight(BtlvEffect_GetMcss(), wk->attacker);
    case 9:
        return wk->param.unk0;
    case 10:
        return wk->param.variant;
    case 11:
        return wk->param.unk2;
    case 12:
        return wk->param.unk3;
    case 13:
        return wk->param.unk4;
    case 14:
        return wk->param.unk8;
    case 15:
        return wk->param.itemNo;
    case 16:
        return wk->value;
    case 17:
        return wk->attacker;
    case 18:
        return BtlvMcss_GetVanish(BtlvEffect_GetMcss(), wk->attacker);
    case 19:
        return wk->attacker & 1;
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        if (BtlvMcss_GetFlags(BtlvEffect_GetMcss(), id - 20) & 2) {
            return TRUE;
        }
        return FALSE;
    case 28:
        if (BtlvMcss_GetFlags(BtlvEffect_GetMcss(), wk->attacker) & 2) {
            return TRUE;
        }
        return FALSE;
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
        return BtlvMcss_IsNoBounce(BtlvEffect_GetMcss(), id - 29);
    case 37:
        return BtlvMcss_IsNoBounce(BtlvEffect_GetMcss(), wk->attacker);
    case 38:
        return BtlvEffect_IsMulti();
    case 39:
        return BtlvEffect_GetBattleStyle();
    case 40:
    case 41:
    case 42:
    case 43:
        return BtlvEffect_GetTrainerType(id - 40);
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
        return BtlvMcss_GetUnk(BtlvEffect_GetMcss(), id - 44);
    case 52:
        return BtlvMcss_GetUnk(BtlvEffect_GetMcss(), wk->attacker);
    case 53:
        return wk->mcssScaled;
    case 54:
        return wk->camSaved;
    case 55:
        return wk->unk2A0;
    case 56:
        return wk->cameraLocked;
    case 57:
        return wk->defender;
    case 58:
    case 59:
    case 60:
    case 61:
    case 62:
    case 63:
    case 64:
    case 65:
        if (BtlvMcss_GetFlags(BtlvEffect_GetMcss(), id - 58) & 8) {
            return TRUE;
        }
        return FALSE;
    case 66:
        if (BtlvMcss_GetFlags(BtlvEffect_GetMcss(), wk->attacker) & 8) {
            return TRUE;
        }
        return FALSE;
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
        if (BtlvMcss_GetFlags(BtlvEffect_GetMcss(), id - 67) & 16) {
            return TRUE;
        }
        return FALSE;
    case 75:
        if (BtlvMcss_GetFlags(BtlvEffect_GetMcss(), wk->attacker) & 16) {
            return TRUE;
        }
        return FALSE;
    }
}

static void BtlvEffvm_AddEmitters(BtlvEffvm *wk, BtlvEffvmEmitParam *param, int index, int resourceId) {
    int i;
    BtlvEffvmEmitParam *emit;

    if (param->to == param->from && (param->to == 11 || param->to == 12)) {
        int positions[8];
        int count;

        if (param->to == 12) {
            param->flip = TRUE;
        }
        count = BtlvEffvm_GetDefenderPositions(wk, 16, positions);
        if (count != 0) {
            for (i = 0; i < count; i++) {
                emit =
                    GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 7435);
                *emit = *param;
                emit->from = positions[i];
                emit->to = positions[i];
                if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, emit) == (SPLEmitter *)-1) {
                    GFL_HeapFree(emit);
                }
            }
        }
    } else if (param->to == param->from && wk->param.unk0 == 12 && !wk->isEffect && wk->move != 366 &&
               (param->to == 9 || param->to == 10)) {
        int targets[8];
        int count;

        if (param->to == 10) {
            param->flip = TRUE;
        }
        switch (BtlvEffect_GetBattleStyle()) {
        case 0:
        case 3:
        default:
            targets[0] = wk->attacker;
            count = 1;
            break;
        case 1:
            targets[0] = (wk->attacker & 1) ? 1 : 0;
            count = 1;
            break;
        case 2:
            targets[0] = (wk->attacker & 1) ? 19 : 18;
            targets[1] = (wk->attacker & 1) ? 21 : 20;
            count = 2;
            break;
        }
        if (count != 0) {
            for (i = 0; i < count; i++) {
                emit =
                    GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 7490);
                *emit = *param;
                emit->from = targets[i];
                emit->to = targets[i];
                if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, emit) == (SPLEmitter *)-1) {
                    GFL_HeapFree(emit);
                }
            }
        }
    } else {
        emit = GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitParam), TRUE, "btlv_effvm.c", 7509);
        *emit = *param;
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitEmitter, emit) == (SPLEmitter *)-1) {
            GFL_HeapFree(emit);
        }
    }
}

static BOOL BtlvEffvm_AddCircleEmitter(VM *vm, BtlvEffvm *wk, BOOL ortho) {
    int index = BtlvEffvm_FindParticleId(wk, BtlvEffvm_GetBallParticleId(wk, VM_Read32(vm)));
    int resourceId = VM_Read32(vm);
    int pos = 0;
    BtlvEffvmEmitterCircleWork *work;
    fx32 offsetY;

    wk->allocs[wk->allocCount] =
        GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmEmitterCircleWork), FALSE, "btlv_effvm.c", 7544);
    work = wk->allocs[wk->allocCount];
    wk->allocCount++;
    work->type = VM_Read32(vm);
    work->radiusX = VM_Read32(vm);
    work->radiusZ = VM_Read32(vm);
    offsetY = VM_Read32(vm);
    work->angle = 0;
    work->frames = VM_Read32(vm);
    work->framesReset = work->frames;
    work->wait = 0;
    work->waitReset = VM_Read32(vm);
    work->unk30 = VM_Read32(vm);
    work->delay = 0;
    work->delayReset = VM_Read32(vm);
    work->angleStep = 0x10000 / work->frames;
    work->ortho = ortho;
    if (index != 16) {
        if (ortho == TRUE && func_02050194(wk->particles[index]) == NULL) {
            G3DCameraProjection projection;
            VecFx32 camPos = { 0, 0, 0 };
            VecFx32 up = data_ov168_021f2ff8;
            VecFx32 target = data_ov168_021f2fd4;

            projection.type = G3DCAM_PROJECTION_ORTHO;
            projection.param1 = FX32_CONST(3);
            projection.param2 = FX32_CONST(-3);
            projection.param3 = FX32_CONST(-4);
            projection.param4 = FX32_CONST(4);
            projection.near = FX32_ONE;
            projection.far = FX32_CONST(512);
            projection.ndcRangeOverride = FX32_ONE;
            func_020500cc(wk->particles[index], &projection, 0x2000, &camPos, &up, &target, HEAPID_TAIL(wk->heapId));
        }
        switch (work->type) {
        case 0:
        case 1:
            pos = BtlvEffvm_GetTargetPosition(vm, 14);
            BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &work->center, pos);
            break;
        case 2:
        case 3:
            pos = BtlvEffvm_GetTargetPosition(vm, 16);
            BtlvMcss_GetDefaultPos(BtlvEffect_GetMcss(), &work->center, pos);
            break;
        case 4:
        case 5:
            work->center.x = 0;
            work->center.y = 0;
            work->center.z = 0;
            break;
        }
        if (pos & 1) {
            work->angle = 0x8000;
        }
        work->center.y += offsetY;
        if (func_0205007c(wk->particles[index], resourceId, BtlvEffvm_InitCircleEmitter, work) == (SPLEmitter *)-1) {
            GFL_HeapFree(work);
        }
    }
    return wk->ret;
}

static void BtlvEffvm_ProjectToScreen(VecFx32 *pos, const VecFx32 *offset) {
    MtxFx44 mtx;
    fx32 w;

    MTX_Copy43To44_(&NNS_G3dGlb.cameraMtx, &mtx);
    MTX_Concat44(&mtx, &NNS_G3dGlb.projMtx, &mtx);
    if (offset != NULL) {
        pos->x += offset->x;
        pos->y += offset->y;
        pos->z += offset->z;
    }
    BtlvEffvm_MulVec44(pos, &mtx, pos, &w);
#ifdef BWBATTLE
    // bwbattle: the particles' orthographic range is as wide as the view (BwG3d_NarrowProjection), 4 of it 256 dots
    pos->x = FX_Mul(FX_Div(pos->x, w), FX32_CONST(4) * (fx32)GFL_G3DSysGetViewWidth() / 256);
#else
    pos->x = FX_Mul(FX_Div(pos->x, w), FX32_CONST(4));
#endif
    pos->y = FX_Mul(FX_Div(pos->y, w), FX32_CONST(3));
    pos->z = -pos->z;
}

static void BtlvEffvm_MulVec44(const VecFx32 *src, const MtxFx44 *mtx, VecFx32 *dest, fx32 *w) {
    fx32 x = src->x;
    fx32 y = src->y;
    fx32 z = src->z;
    fx64 sum;

    sum = ((fx64)x * mtx->m[0][0] + (fx64)y * mtx->m[1][0] + (fx64)z * mtx->m[2][0]);
    dest->x = (fx32)(sum >> FX32_SHIFT) + mtx->m[3][0];
    sum = ((fx64)x * mtx->m[0][1] + (fx64)y * mtx->m[1][1] + (fx64)z * mtx->m[2][1]);
    dest->y = (fx32)(sum >> FX32_SHIFT) + mtx->m[3][1];
    sum = ((fx64)x * mtx->m[0][2] + (fx64)y * mtx->m[1][2] + (fx64)z * mtx->m[2][2]);
    dest->z = (fx32)(sum >> FX32_SHIFT) + mtx->m[3][2];
    sum = ((fx64)x * mtx->m[0][3] + (fx64)y * mtx->m[1][3] + (fx64)z * mtx->m[2][3]);
    *w = (fx32)(sum >> FX32_SHIFT) + mtx->m[3][3];
}

static u32 BtlvEffvm_GetBallParticleId(BtlvEffvm *wk, u32 id) {
    u32 ball;
    u32 source = wk->ballSource;

    if (source == 9) {
        ball = BtlvMcss_GetBall(BtlvEffect_GetMcss(), wk->attacker);
    } else if (source != 8) {
        ball = BtlvMcss_GetBall(BtlvEffect_GetMcss(), source);
    } else {
        ball = PML_ItemGetMonsBallID(wk->param.itemNo);
    }
    if (ball == 0 || ball > 25) {
        ball = 4;
    }
    ball--;
    switch (id) {
    case 3:
        id += ball;
        break;
    case 29:
        if (ball + 1 == 25) {
            ball = 16;
        } else if (ball + 1 >= 16) {
            ball = 3;
        }
        id += ball;
        break;
    case 46:
    case 47:
    case 48:
    case 49:
        id += ball * 4;
        break;
    }
    return id;
}

static void BtlvEffvm_StartBgmFade(BtlvEffvm *wk, fx32 start, fx32 end, int frames) {
    BtlvEffvmBgmFadeWork *work;

    if (start > end) {
        if (wk->bgmLowered) {
            wk->bgmLowerRequested = TRUE;
            return;
        }
        wk->bgmLowered = TRUE;
    } else {
        wk->bgmLowered = FALSE;
    }
    work = GFL_HeapAllocate(HEAPID_TAIL(wk->heapId), sizeof(BtlvEffvmBgmFadeWork), FALSE, "btlv_effvm.c", 7799);
    work->volume = start;
    work->target = end;
    work->wk = wk;
    BtlvEffTool_CalcStep(work->volume, work->target, &work->step, FX32_CONST(frames));
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(wk->tcbManager, BtlvEffvm_BgmFadeTask, work, 0), NULL, 1);
}

static int BtlvEffvm_GetFreeVoiceSlot(BtlvEffvm *wk) {
    int i;

    for (i = 0; i < 6; i++) {
        if (wk->voices[i] == -1) {
            break;
        }
    }
    if (i == 6) {
        i = 0;
    }
    return i;
}

static void BtlvEffvm_ResetMcssAll(BtlvEffvm *wk) {
    BtlvMcssPos i;

    for (i = BTLV_MCSS_POS_FIRST; i < BTLV_MCSS_POS_TRAINER; i++) {
        if (BtlvMcss_Exists(BtlvEffect_GetMcss(), i) == TRUE) {
            BtlvMcss_ResetPosition(BtlvEffect_GetMcss(), i);
        }
    }
}

static void BtlvEffvm_ClearMcssFlagAll(BtlvEffvm *wk) {
    BtlvMcssPos i;

    for (i = BTLV_MCSS_POS_FIRST; i < BTLV_MCSS_POS_TRAINER; i++) {
        if (BtlvMcss_Exists(BtlvEffect_GetMcss(), i) == TRUE) {
            BtlvMcss_SetShadowVanish(BtlvEffect_GetMcss(), i, 0);
        }
    }
}

static void BtlvEffvm_SeWaitTask(TCB *tcb, void *data) {
    BtlvEffvmSeWork *work = data;

    if (--work->wait == 0) {
        BtlvEffvm_PlaySENow(work->se, work->player, work->pan, work->arg4, work->arg5, work->vol, work->pitch);
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvEffvm_SeWaitTaskRemove(TCB *tcb) {
    BtlvEffvmSeWork *work = GFL_TCBGetData(tcb);

    work->wk->seDelayed = FALSE;
}

static void BtlvEffvm_SeMoveTask(TCB *tcb, void *data) {
    BtlvEffvmSeMoveWork *work = data;
    BOOL done = FALSE;
    int value;

    if (work->delay != 0) {
        work->delay--;
        return;
    }
    if (work->stepWait == 0) {
        work->stepWait = work->stepWaitReset;
        work->value += work->step;
        value = work->value >> FX32_SHIFT;
        if (work->count & 1) {
            if (work->step < 0) {
                if (value < work->start) {
                    value = work->start;
                }
            } else if (value > work->start) {
                value = work->start;
            }
        } else {
            if (work->step < 0) {
                if (value < work->end) {
                    value = work->end;
                }
            } else if (value > work->end) {
                value = work->end;
            }
        }
        work->value = FX32_CONST(value);
        switch (work->param) {
        case 0:
            GFL_SndPlayerSetParams(work->player, -1, value, -1);
            break;
        case 1:
            NNS_SndPlayerSetVolume(func_020061a8(work->player), value);
            break;
        case 2:
            GFL_SndPlayerSetParams(work->player, -1, -1, value);
            break;
        }
        if (work->frames == 0) {
            work->frames = work->framesReset;
            switch (work->type) {
            case 0:
                done = TRUE;
                break;
            case 1:
                if (--work->count != 0) {
                    work->step *= -1;
                } else {
                    done = TRUE;
                }
                break;
            }
        } else {
            work->frames--;
        }
    } else {
        work->stepWait--;
    }
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvEffvm_SeMoveTaskRemove(TCB *tcb) {
    BtlvEffvmSeMoveWork *work = GFL_TCBGetData(tcb);

    work->wk->seSliding = FALSE;
}

static void BtlvEffvm_CryDelayTask(TCB *tcb, void *data) {
    BtlvEffvmCryWork *work = data;
    int slot;
    BtlvEffvmCryWaitWork *wait;

    if (--work->delay == 0) {
        slot = BtlvEffvm_GetFreeVoiceSlot(work->wk);
        work->wk->voices[slot] = BtlvMcss_PlayCry(BtlvEffect_GetMcss(), work->pos, work->arg2, work->arg3, work->arg4,
                                                  work->arg5, work->arg6);
        BtlvEffvm_StartBgmFade(work->wk, 0x7f000, 0x6b000, 10);
        if (!work->wk->bgmLowerRequested) {
            wait = GFL_HeapAllocate(HEAPID_TAIL(work->wk->heapId), sizeof(BtlvEffvmCryWaitWork), FALSE, "btlv_effvm.c",
                                    8044);
            wait->wk = work->wk;
            BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->wk->tcbManager, BtlvEffvm_CryWaitTask, wait, 0), NULL, 0);
        }
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvEffvm_CryDelayTaskRemove(TCB *tcb) {
    BtlvEffvmCryWork *work = GFL_TCBGetData(tcb);

    work->wk->cryDelayed = FALSE;
}

static void BtlvEffvm_BgmFadeTask(TCB *tcb, void *data) {
    BtlvEffvmBgmFadeWork *work = data;
    BOOL done = TRUE;

    BtlvEffTool_Step(&work->volume, &work->step, &work->target, &done);
    func_02011bdc(0xffff, work->volume >> FX32_SHIFT);
    if (done) {
        BtlvEffect_EndTask(tcb);
    }
}

// NONMATCHING: the original masks the vertical bounds with 0xffff (an `ands` against a mask built as 0xff << 8 | 0xff,
// shared by both stores) where MWCC drops the mask under the 16-bit store, and loads winV before winH, 12 bytes
static void BtlvEffvm_WindowTask(TCB *tcb, void *data) {
    BtlvEffvmWindowWork *work = data;

    switch (work->state) {
    case 0:
        G2_SetWnd0InsidePlane((u8)work->winInOut, FALSE);
        G2_SetWnd1InsidePlane(work->winInOut & 0xff, FALSE);
        G2_SetWndOutsidePlane((work->winInOut & 0xff00) >> 8, FALSE);
        GX_SetVisibleWnd(GX_WNDMASK_W0 | GX_WNDMASK_W1);
        reg_G2_WIN1H = 0;
        reg_G2_WIN1V = 0;
        work->state++;
    case 1:
        if (work->wait == 0) {
            work->wait = work->waitReload;
            switch (work->dir) {
            case 0:
                work->winV -= 0x100;
                work->winV++;
                break;
            case 1:
                work->winV += 0x100;
                work->winV--;
                break;
            case 2:
                work->winH -= 0x100;
                work->winH++;
                break;
            case 3:
                work->winH += 0x100;
                work->winH--;
                break;
            }
            reg_G2_WIN0H = work->winH;
            reg_G2_WIN0V = work->winV & 0xffff;
            if ((u8)work->winH == 0xff) {
                reg_G2_WIN1H = 0x100;
                reg_G2_WIN1V = work->winV & 0xffff;
            } else {
                reg_G2_WIN1H = 0;
                reg_G2_WIN1V = 0;
            }
            if (--work->count == 0) {
                BtlvEffect_EndTask(tcb);
            }
        } else {
            work->wait--;
        }
        break;
    }
}

static void BtlvEffvm_WindowTaskRemove(TCB *tcb) {
    BtlvEffvmWindowWork *work = GFL_TCBGetData(tcb);

    if (!work->keepWindow) {
        GX_SetVisibleWnd(GX_WNDMASK_NONE);
    }
    work->wk->windowTask = FALSE;
}

static void BtlvEffvm_CryWaitTask(TCB *tcb, void *data) {
    BtlvEffvmCryWaitWork *work = data;
    int i;

    for (i = 0; i < 6; i++) {
        if (work->wk->voices[i] != -1) {
            if (PokeVoice_IsPlaying(work->wk->voices[i])) {
                return;
            }
            work->wk->voices[i] = -1;
        }
    }
    BtlvEffvm_StartBgmFade(work->wk, 0x6b000, 0x7f000, 10);
    work->wk->bgmLowerRequested = FALSE;
    BtlvEffect_EndTask(tcb);
}

static void BtlvEffvm_SideResetTask(TCB *tcb, void *data) {
    BtlvEffvmSideWork *work = data;
    int pos = work->side != 0 ? 1 : 0;
    u32 allMask = work->side != 0 ? 0xaa : 0x55;

    for (; pos <= 7; pos += 2) {
        if (BtlvEffect_CheckExist(pos)) {
            if (BtlvMcss_IsBusy(BtlvEffect_GetMcss(), pos)) {
                continue;
            }
            if (BtlvEffect_PosBit(pos) & work->doneMask) {
                continue;
            }
            BtlvMcss_SetAnimPause(BtlvEffect_GetMcss(), pos, 0);
            BtlvMcss_SetShadowVanish(BtlvEffect_GetMcss(), pos, 0);
            work->doneMask |= BtlvEffect_PosBit(pos);
        } else {
            work->doneMask |= BtlvEffect_PosBit(pos);
        }
    }
    if (allMask == work->doneMask) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvEffvm_ParticleUploadTask(TCB *tcb, void *data) {
    BtlvEffvmParticleVBlankWork *work = data;
#ifdef BWBATTLE
    // bwbattle: the port has no scanlines (see MCSS_LoadTask); its V-blank tasks run while nothing is drawn
    BOOL inWindow = TRUE;
#else
    u16 vcount = reg_GX_VCOUNT;
    BOOL inWindow = vcount >= 192 && vcount <= 200;
#endif

    if (inWindow) {
        func_0204ff04(work->particle);
        work->wk->particleLoading = FALSE;
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvEffvm_PalAnimTask(TCB *tcb, void *data) {
    BtlvEffvmPalAnimWork *work = data;
    NNSG2dPaletteData *palette;
    void *file;
    u16 offset;

    switch (work->state) {
    case 0:
        work->timer = 0xff;
        work->state++;
    case 1:
        if (work->timer < work->durations[work->index] - 2) {
            work->timer++;
            return;
        }
        offset = (work->palettes[work->index] - 8) * 16;
        file = GFL_G2DIOReadNCLR(231, work->fileId, &palette, HEAPID_TAIL(work->wk->heapId));
        MIi_CpuCopy16((u16 *)palette->pRawData + offset,
                      PaletteFade_GetUnfadedBuffer(BtlvEffect_GetPaletteFade(), 0) + 0x80, 0x20);
        PaletteFade_ApplyPalette(BtlvEffect_GetPaletteFade(), 8, 0);
        GFL_HeapFree(file);
        work->timer = 0;
        if (work->index >= work->count - 1) {
            work->index = 0;
            if (work->loops-- <= 1) {
                BtlvEffect_EndTask(tcb);
            }
        } else {
            work->index++;
        }
        break;
    }
}

static void BtlvEffvm_PalAnimTaskRemove(TCB *tcb) {
    BtlvEffvmPalAnimWork *work = GFL_TCBGetData(tcb);

    work->wk->paletteAnim = FALSE;
    // BUG: BtlvEffect_EndTask, which calls this end function, frees the task's data again after it
#ifndef BUGFIX
    GFL_HeapFree(work);
#endif
}

static void BtlvEffvm_ScaleAllMcss(fx32 scaleX, fx32 scaleY, int frames, int wait, int count) {
    BtlvMcssPos i;
    fx32 scale;
    VecFx32 dest;

    BtlvMcss_SetSideScale(BtlvEffect_GetMcss(), scaleX, scaleY);
    for (i = BTLV_MCSS_POS_FIRST; i < BTLV_MCSS_POS_TRAINER; i++) {
        if (BtlvMcss_Exists(BtlvEffect_GetMcss(), i)) {
            scale = BtlvMcss_GetDefaultScale(BtlvEffect_GetMcss(), i, 1);
            if (i & 1) {
                scale = FX_Mul(scale, scaleY);
            } else {
                scale = FX_Mul(scale, scaleX);
            }
            dest.x = scale;
            dest.y = scale;
            dest.z = FX32_ONE;
            BtlvMcss_MoveScale(BtlvEffect_GetMcss(), i, 1, &dest, frames, wait, count);
        }
    }
}

static BOOL BtlvEffvm_IsGaugeKeptEffect(u32 move) {
    int i;

    for (i = 0; i < NELEMS(data_ov168_021f3028); i++) {
        if (move == data_ov168_021f3028[i]) {
            return TRUE;
        }
    }
    return FALSE;
}
