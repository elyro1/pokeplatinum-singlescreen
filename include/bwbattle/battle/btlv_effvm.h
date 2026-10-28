#ifndef POKEBW2_BATTLE_BTLV_EFFVM_H
#define POKEBW2_BATTLE_BTLV_EFFVM_H

// Overlay 168's btlv_effvm.c (named by its string), the battle effect VM: it runs the move effect scripts, whose
// commands (swan's MOVE_SCRCMD) drive the camera, the Pokémon, the cell actors, the particles, the screen effects and
// the sounds of each move's animation. BtlvEffect_QueueCommands is swan's name; the others are ours

#include "types.h"
#include "gfl/heap.h"
#include "struct_decls.h"
#include "system/vm.h"

// What btlv_effect.c tells BtlvEffvm_Start of the effect, 0x10 bytes; BtlvEffvmParam_Clear clears it. The scripts
// read the fields as their variables 10 to 16
typedef struct {
    u8 unk0;    // 0x0  a byte of the move's parameter 27
    u8 variant; // 0x1  which of the script's variants runs, the turn of a two-turn move
    u8 unk2;    // 0x2
    u8 unk3;    // 0x3
    u32 unk4;   // 0x4
    u32 unk8;   // 0x8
    u16 itemNo; // 0xc  the ball the ball effects show
} BtlvEffvmParam;

VM *BtlvEffvm_Create(TCBManager *tcbManager, HeapID heapId);
BOOL BtlvEffvm_Main(VM *vm);
void BtlvEffvm_Delete(VM *vm);
void BtlvEffvm_Start(VM *vm, u32 attacker, u32 defender, u16 move, BtlvEffvmParam *param);
#ifdef BWBATTLE
// Start the wild shinies' sparkle subroutines before the current send-out script.
// Their particles share its lifetime and run during the throw, without a separate wait.
void BtlvEffvm_PrefixWildShinies(VM *vm);
// The shortened wild opening may reach the menu while its send-out cry finishes.
void BtlvEffvm_OverlapIntroCry(VM *vm);
#endif
void BtlvEffvm_Stop(VM *vm);
void BtlvEffvm_Resume(VM *vm);
s32 BtlvEffvm_GetScriptKind(VM *vm);
void BtlvEffvmParam_Clear(BtlvEffvmParam *param);
void BtlvEffvm_PlaySEAt(VM *vm, u32 se, u32 player, u32 pan, int arg4, int arg5, int vol, int pitch, int wait);
void BtlvEffvm_SlideSE(VM *vm, u32 player, u32 type, u32 param, int start, int end, int delay, int frames, int stepWait,
                       int count);
void BtlvEffvm_ReleaseVoices(VM *vm);
void BtlvEffvm_ClearLastEffect(VM *vm);
// A script command, global in the original: starts the task that opens or closes window 0 step by step
BOOL BtlvEffect_QueueCommands(VM *vm, void *env);

#endif // POKEBW2_BATTLE_BTLV_EFFVM_H
