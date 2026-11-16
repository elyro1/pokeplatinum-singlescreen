#ifndef POKEBW2_GFL_KEY_H
#define POKEBW2_GFL_KEY_H

#include "types.h"
#include "gfl/heap.h"
#include "struct_decls.h"

// The keys (key.c); the same values as the SDK's pad.h, which may come first

#ifndef PAD_BUTTON_A
#define PAD_BUTTON_A 0x1
#define PAD_BUTTON_B 0x2
#define PAD_BUTTON_SELECT 0x4
#define PAD_BUTTON_START 0x8
#define PAD_KEY_RIGHT 0x10
#define PAD_KEY_LEFT 0x20
#define PAD_KEY_UP 0x40
#define PAD_KEY_DOWN 0x80
#define PAD_BUTTON_R 0x100
#define PAD_BUTTON_L 0x200
#define PAD_BUTTON_X 0x400
#define PAD_BUTTON_Y 0x800
#define PAD_PLUS_KEY_MASK 0xf0
#endif

KeypadManager *GFL_HIDCreateKeypadManager(HeapID heapId);
void GCTX_HIDUpdateKeypad(void);
u32 GCTX_HIDGetHeldKeys(void);
u32 GCTX_HIDGetPressedKeys(void);
// The pressed keys, and the held keys again after a delay, repeating
u32 GCTX_HIDGetTypedKeys(void);
void GFL_HIDClearKeypadState(SystemUI *ui);
// Sets the frames before a held key repeats, and between repeats, and gets them
void setKeypressFramecounts(u32 repeatWait, u32 repeatStart);
void GCTX_HIDGetRepeat(u32 *repeatWait, u32 *repeatStart);
// Sets a table of key remappings, which make keys report as others
void setKeyBlkStarted(const void *remaps);
// Set the keys of a frame
void func_0203df90(SystemUI *ui, u32 pressed, u32 pressed30);
void func_0203dfa0(SystemUI *ui, u32 held, u32 held30);
void func_0203dfb0(SystemUI *ui, u32 typed, u32 typed30);

#endif // POKEBW2_GFL_KEY_H
