// The effects' moves and waves of the battle's background, BG 3. The name is the ROM's string, from
// GFL_HeapAllocate's asserts. The names are ours; swan has none for this file

#include "battle/btlv_bg.h"
#include "types.h"
#include "battle/btlv_bg3d.h"
#include "battle/btlv_effect.h"
#include "gfl/bg_sys.h"
#include "gfl/heap.h"
#include "gfl/tcb.h"
#include "nitro/fx.h"
#include "nitro/gx.h"
#include "system/sin_wave_table.h"

struct BtlvBg {
    TCBManager *tcbMgr; // 0x0
    u32 moving : 1;     // 0x4
    u32 waving : 1;
    HeapID heapId; // 0x8
};

// The offset to set at the next V-blank
typedef struct {
    s32 x;
    s32 y;
} BtlvBgScroll;

// A move of the offset, by the effect tools' moving value
typedef struct {
    BtlvBg *bg;           // 0x00
    VecFx32 pos;          // 0x04
    BtlvEffToolMove move; // 0x10
} BtlvBgMove;

// A wave of the lines' horizontal or vertical offset
typedef struct {
    BtlvBg *bg;     // 0x000
    TCB *vblankTcb; // 0x004
    TCB *hblankTcb; // 0x008
    s32 frames;     // 0x00c
    fx32 amplitude; // 0x010
    s32 step;       // 0x014  in degrees
    u32 fadeMode;   // 0x018
    s32 fadeFrames; // 0x01c
    s16 table[256]; // 0x020
    s16 shift;      // 0x220  the table's line at line 0
    u32 lines;      // 0x224  192 shifts the lines horizontally, 256 vertically
    u16 counter;    // 0x228
    fx32 fadeStep;  // 0x22c
} BtlvBgWave;

static void BtlvBg_AddMoveTask(BtlvBg *work, u32 mode, VecFx32 *start, const VecFx32 *end, s32 frames, s32 wait,
                               s32 count, TCBFunc func);
static void BtlvBg_MoveTask(TCB *tcb, void *data);
static void BtlvBg_MoveTaskEnd(TCB *tcb);
static void BtlvBg_SetOffsetVBlank(TCB *tcb, void *data);
static void BtlvBg_WaveTask(TCB *tcb, void *data);
static void BtlvBg_WaveTaskEnd(TCB *tcb);
static void BtlvBg_WaveVBlank(TCB *tcb, void *data);
#ifndef BWBATTLE
static void BtlvBg_WaveHBlank(TCB *tcb, void *data);
#endif

BtlvBg *BtlvBg_Create(TCBManager *tcbMgr, HeapID heapId) {
    BtlvBg *work = GFL_HeapAllocate(heapId, sizeof(BtlvBg), TRUE, "btlv_bg.c", 0x6f);
    work->tcbMgr = tcbMgr;
    work->heapId = heapId;
    return work;
}

void BtlvBg_Delete(BtlvBg *work) {
    GFL_HeapFree(work);
}

void BtlvBg_SetOffsetReq(BtlvBg *work, s32 x, s32 y) {
    BtlvBgScroll *scroll = GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvBgScroll), FALSE, "btlv_bg.c", 0x8e);
    scroll->x = x;
    scroll->y = y;
    BtlvEffect_AddTask(GFL_VBlankTCBAdd(BtlvBg_SetOffsetVBlank, scroll, 0), NULL, 0);
}

void BtlvBg_StartMove(BtlvBg *work, u32 pos, u32 mode, s32 x, s32 y, s32 frames, s32 wait, s32 count) {
    VecFx32 start;
    VecFx32 end;

    end.x = x << FX32_SHIFT;
    end.y = y << FX32_SHIFT;
    end.z = 0;
    start.x = GFL_BGSysGetBGOffsetX(3) << FX32_SHIFT;
    start.y = GFL_BGSysGetBGOffsetY(3) << FX32_SHIFT;
    start.z = 0;
    if (mode == 4 && (pos & 1)) {
        end.x *= -1;
    }
    if (mode == 1) {
        end.x += start.x;
        end.y += start.y;
        end.z += start.z;
    }
    BtlvBg_AddMoveTask(work, mode, &start, &end, frames, wait, count, BtlvBg_MoveTask);
    work->moving = TRUE;
}

BOOL BtlvBg_IsMoving(BtlvBg *work) {
    if (work->moving) {
        return TRUE;
    }
    return FALSE;
}

void BtlvBg_StartWave(BtlvBg *work, u32 mode, fx32 amplitude, s32 step, s32 frames, u32 fadeMode, s32 fadeFrames) {
    BtlvBgWave *wave = GFL_HeapAllocate(HEAPID_TAIL(work->heapId), sizeof(BtlvBgWave), TRUE, "btlv_bg.c", 0xe6);
    wave->bg = work;
    wave->frames = frames;
    wave->amplitude = amplitude;
    wave->step = step;
    wave->fadeMode = fadeMode;
    wave->fadeFrames = fadeFrames;
    wave->fadeStep = FX_Div(amplitude, FX32_CONST(fadeFrames));
    switch (mode) {
    case 0:
        wave->lines = 192;
        break;
    case 1:
        wave->lines = 256;
        break;
    }
    wave->shift = 0;
    SinWaveTable_Make(wave->table, wave->lines, 0xffff * step / 360, amplitude);
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, BtlvBg_WaveTask, wave, 0), BtlvBg_WaveTaskEnd, 0);
#ifdef BWBATTLE
    // bwbattle: BG 3 is drawn in the 3D scene (btlv_bg3d.c), which shifts its lines itself
    BtlvBg3d_SetWave(wave->table, wave->lines, &wave->shift);
#else
    wave->hblankTcb = GFL_HBlankTCBAdd(BtlvBg_WaveHBlank, wave, 0);
#endif
    wave->vblankTcb = GFL_VBlankTCBAdd(BtlvBg_WaveVBlank, wave, 10);
    work->waving = TRUE;
}

BOOL BtlvBg_IsWaving(BtlvBg *work) {
    if (work->waving) {
        return TRUE;
    }
    return FALSE;
}

static void BtlvBg_AddMoveTask(BtlvBg *work, u32 mode, VecFx32 *start, const VecFx32 *end, s32 frames, s32 wait,
                               s32 count, TCBFunc func) {
    BtlvBgMove *move = GFL_HeapAllocate(work->heapId, sizeof(BtlvBgMove), FALSE, "btlv_bg.c", 0x138);
    move->bg = work;
    move->pos.x = start->x;
    move->pos.y = start->y;
    move->pos.z = start->z;
    move->move.type = mode;
    move->move.stepTime = frames;
    move->move.stepTimeReset = frames;
    move->move.wait = 0;
    move->move.waitReset = wait;
    move->move.count = count * 2;
    move->move.start.x = start->x;
    move->move.start.y = start->y;
    move->move.start.z = start->z;
    move->move.end.x = end->x;
    move->move.end.y = end->y;
    move->move.end.z = end->z;
    switch (mode) {
    case 0:
        break;
    case 1:
        BtlvEffTool_CalcStepVec(&move->move.start, end, &move->move.step, FX32_CONST(frames));
        break;
    case 3:
        move->move.stepTimeReset *= 2;
        move->move.count *= 2;
    case 2:
        move->move.step.x = FX_Div(end->x, FX32_CONST(frames));
        move->move.step.y = FX_Div(end->y, FX32_CONST(frames));
        move->move.step.z = FX_Div(end->z, FX32_CONST(frames));
        break;
    }
    BtlvEffect_AddTask(GFL_TCBMgrAddTask(work->tcbMgr, func, move, 0), BtlvBg_MoveTaskEnd, 0);
}

static void BtlvBg_MoveTask(TCB *tcb, void *data) {
    BtlvBgMove *move = data;
    BtlvBg *work = move->bg;
    BOOL done;

    if (move->move.type == 4) {
        move->pos.x += move->move.end.x;
        move->pos.y += move->move.end.y;
        if (move->move.stepTime == 0) {
            done = TRUE;
        } else {
            move->move.stepTime--;
            done = FALSE;
        }
    } else {
        done = BtlvEffTool_Move(&move->move, &move->pos);
    }
    BtlvBg_SetOffsetReq(work, move->pos.x >> FX32_SHIFT, move->pos.y >> FX32_SHIFT);
    if (done == TRUE) {
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvBg_MoveTaskEnd(TCB *tcb) {
    BtlvBgMove *move = GFL_TCBGetData(tcb);
    move->bg->moving = FALSE;
}

static void BtlvBg_SetOffsetVBlank(TCB *tcb, void *data) {
    BtlvBgScroll *scroll = GFL_TCBGetData(tcb);
    GFL_BGSysMoveBG(3, BG_MOVE_SET_X, scroll->x);
    GFL_BGSysMoveBG(3, BG_MOVE_SET_Y, scroll->y);
    BtlvEffect_EndTask(tcb);
}

static void BtlvBg_WaveTask(TCB *tcb, void *data) {
    BtlvBgWave *wave = data;
    fx32 amplitude;

    switch (wave->fadeMode) {
    case 0:
        break;
    case 1:
    case 3:
        if (wave->counter < wave->fadeFrames) {
            amplitude = FX_MUL(wave->fadeStep, FX32_CONST(wave->counter));
            SinWaveTable_Make(wave->table, wave->lines, 0xffff * wave->step / 360, amplitude);
        } else if (wave->fadeMode == 3) {
            wave->fadeMode = 2;
        }
        break;
    case 2:
        if (wave->counter > wave->frames - wave->fadeFrames) {
            amplitude = FX_MUL(wave->fadeStep, FX32_CONST(wave->frames - wave->counter));
            SinWaveTable_Make(wave->table, wave->lines, 0xffff * wave->step / 360, amplitude);
        }
        break;
    }
    wave->counter++;
    if (wave->counter >= wave->frames) {
#ifdef BWBATTLE
        BtlvBg3d_SetWave(NULL, 0, NULL);
#else
        GFL_TCBRemove(wave->hblankTcb);
#endif
        GFL_TCBRemove(wave->vblankTcb);
        BtlvEffect_EndTask(tcb);
    }
}

static void BtlvBg_WaveTaskEnd(TCB *tcb) {
    BtlvBgWave *wave = GFL_TCBGetData(tcb);
    wave->bg->waving = FALSE;
#ifdef BWBATTLE
    // bwbattle: also when the effect ends the task early, before the table is freed
    BtlvBg3d_SetWave(NULL, 0, NULL);
#endif
    // BUG: BtlvEffect_EndTask, which calls this end function, frees the task's data again after it
#ifndef BUGFIX
    GFL_HeapFree(wave);
#endif
}

static void BtlvBg_WaveVBlank(TCB *tcb, void *data) {
    BtlvBgWave *wave = data;

    wave->shift++;
    if (wave->shift < 0) {
        wave->shift += wave->lines;
    } else {
        wave->shift %= wave->lines;
    }
}

#ifndef BWBATTLE
static void BtlvBg_WaveHBlank(TCB *tcb, void *data) {
    BtlvBgWave *wave = data;
    int x = GFL_BGSysGetBGOffsetX(3);
    int y = GFL_BGSysGetBGOffsetY(3);
    u32 line;

    line = reg_GX_VCOUNT + (wave->shift + 1);
    line %= wave->lines;
    if (GX_IsHBlank()) {
        switch (wave->lines) {
        case 256:
            G2_SetBG3Offset(x, y + wave->table[line]);
            break;
        case 192:
            G2_SetBG3Offset(x + wave->table[line], y);
            break;
        }
    }
}
#endif
