#include "types.h"
#include "gfl/arc.h"
#include "gfl/clact.h"
#include "gfl/graphics.h"
#include "gfl/gx_layers.h"
#include "gfl/heap.h"
#include "gfl/std.h"
#include "gfl/vman.h"
#include "nitro/fx.h"
#include "nitro/gx.h"
#include "nitro/mi.h"
#include "nitro/os.h"
#include "nnsys/g2d.h"
#include "nnsys/gfd.h"

// The OAM managers, renderer, units of actors and resources of the cell actor system

// A resource that is not loaded
#define CLACT_RES_NONE 0xffffffff
// Positions that are not relative to a surface of the renderer
#define CLACT_SURFACE_NONE 0xffff

// How an actor animates: a cell animation, one whose characters are transferred to VRAM as it plays, or a multi-cell
// animation
enum {
    CLACT_ANIM_CELL,
    CLACT_ANIM_VRAM_TRANSFER,
    CLACT_ANIM_MULTICELL,
};

typedef struct {
    NNSG2dImageProxy proxy;
    void *data;
    // The characters a VRAM transfer cell animation sends from
    NNSG2dCharacterData *transferChar;
    VRAMAlloc allocMain;
    VRAMAlloc allocSub;
    // Whether data is someone else's, which freeing the resource leaves
    u32 extData : 1;
    u32 size : 30;
    u32 free : 1;
} ClActCharRes;

typedef struct {
    NNSG2dImagePaletteProxy proxy;
    u32 size : 31;
    u32 free : 1;
} ClActPlttRes;

typedef struct {
    void *cellFile;
    void *animFile;
    NNSG2dCellDataBank *cells;
    NNSG2dAnimBankData *anims;
    BOOL free;
} ClActCellAnimRes;

typedef struct {
    u16 charCount;
    u16 plttCount;
    u16 cellAnimCount;
    u16 unk6;
    // Where in OBJ VRAM the resources go, in 32-byte units
    u16 offsetMain;
    u16 offsetSub;
} ClActResManSetup;

typedef struct {
    VRAMMan *vmanMain;
    VRAMMan *vmanSub;
    ClActCharRes *chars;
    ClActPlttRes *pltts;
    ClActCellAnimRes *cellAnims;
    u16 charCount;
    u16 plttCount;
    u16 cellAnimCount;
    u16 unk1A;
    u16 offsetMain;
    u16 offsetSub;
} ClActResMan;

typedef struct {
    NNSG2dOamManagerInstance man;
    BOOL active;
} ClActOamMan;

struct ClActRenderer {
    NNSG2dRendererInstance renderer;
    NNSG2dRenderSurface *surfaces;
    u16 surfaceCount;
    u16 unk9E;
};

typedef struct {
    NNSG2dOamRegisterFunction oamRegister;
    NNSG2dAffineRegisterFunction affineRegister;
    NNSG2dRndCellCullingFunction culling;
} ClActSurfaceFuncs;

// A transfer of a VRAM transfer cell animation's characters, at the vertical blank
typedef struct {
    void *src;
    u32 dst;
    u32 size;
    u32 screen;
} ClActTransferTask;

typedef struct {
    NNSG2dCellTransferState *states;
    ClActTransferTask *tasks;
    u32 count;
    BOOL active;
} ClActTransfer;

struct ClActSys {
    ClActOamMan oamMans[2];
    ClActRenderer renderer;
    ClActTransfer transfer;
    // What the OAMs' palettes are offset or set to
    u16 palettes[2];
    u32 paletteMode;
    BOOL updating;
    BOOL oamReady;
    ClActUnit *units;
    ClActResMan res;
};

struct ClActUnit {
    ClActor *actors;
    // The actors in use, as a list sorted by priority
    ClActor *actorList;
    ClActRenderer *renderer;
    u16 count;
    u8 rendererType : 4;
    u8 active : 1;
    u8 unk5 : 1;
    u8 unk6 : 1;
    u8 priority;
    ClActUnit *next;
    ClActUnit *prev;
};

// The resources an actor is made with
typedef struct {
    u16 charIdx;
    u16 plttIdx;
    ClActCharRes *chars;
    ClActPlttRes *pltt;
    NNSG2dCellDataBank *cells;
    NNSG2dAnimBankData *anims;
    NNSG2dMultiCellDataBank *mcBank;
    NNSG2dAnimBankData *mcAnims;
    NNSG2dCharacterData *transferChar;
} ClActResSetup;

struct ClActAnim {
    u32 type;
    NNSG2dCellDataBank *cells;
    NNSG2dAnimBankData *anims;
    union {
        struct {
            NNSG2dCellAnimation cellAnim;
            u32 transferHandle;
        };
        struct {
            NNSG2dMultiCellAnimation mcAnim;
            NNSG2dMultiCellDataBank *mcBank;
            NNSG2dAnimBankData *mcAnims;
            void *mcWork;
        };
    };
};

struct ClActor {
    ClActor *next;
    ClActor *prev;
    ClActUnit *unit;
    ClActorPos pos;
    ClActorPos affineCenter;
    ClActorScale scale;
    NNSG2dImageProxy imgProxy;
    NNSG2dImagePaletteProxy pltProxy;
    fx32 unk54;
    u16 rotation;
    u16 sequence;
    u16 charIdx;
    u16 plttIdx;
    u32 priority : 8;
    u32 unk60_8 : 4;
    u32 palette : 4;
    u32 bgPriority : 2;
    u32 affineMode : 2;
    u32 flip : 2;
    u32 objMode : 2;
    u32 unk60_24 : 1;
    u32 unk60_25 : 1;
    u32 visible : 1;
    u32 playMode : 4;
    u32 unk60_31 : 1;
    u32 unk64_0 : 1;
    ClActAnim anim;
};

// What the renderers pass the culling callback of a surface for each OAM, which sets skip when it is out of view
typedef struct {
    u8 unk0[0x30];
    BOOL skip;
    u32 unk34;
    GXOamAttr oam;
} ClActDrawState;

ClActSys *g_ClActSys;
// bwbattle: whether resources go to OBJ VRAM and OBJ palette RAM, which inside Platinum's battle belong to Platinum's
// sprites (ClActSys_SetVramWritesEnabled)
static BOOL sBwVramWrites = TRUE;

void ClActSys_SetVramWritesEnabled(BOOL enabled) {
    sBwVramWrites = enabled;
}

void func_0204c5e4(ClActRenderer *renderer, const ClActSysSetup *setup, HeapID heapId);
static void func_0204c614(BOOL updating);
static BOOL func_0204c628(void);
static void func_0204c640(BOOL ready);
static BOOL func_0204c654(void);
static void func_0204c66c(void);
static void func_0204c690(void);
static void func_0204c6d8(ClActUnit *unit);
static void func_0204c724(ClActUnit *unit);
static void func_0204c758(ClActOamMan *mans, u8 fromMain, u8 numMain, u8 fromSub, u8 numSub);
static void func_0204c784(ClActOamMan *mans);
static void func_0204c798(ClActOamMan *mans);
static void func_0204c7b0(ClActOamMan *mans);
static void func_0204c7c8(ClActOamMan *man, u8 from, u8 num, u32 type);
static void func_0204c7e0(ClActOamMan *man);
static void func_0204c7f0(ClActOamMan *man);
static void func_0204c800(ClActRenderer *renderer, const ClActSurfaceSetup *setups, u16 count, HeapID heapId);
static void func_0204c884(ClActRenderer *renderer);
static void func_0204c890(ClActRenderer *renderer, BOOL cull);
static void func_0204c8d0(NNSG2dRenderSurface *surface, const ClActSurfaceSetup *setup);
static void func_0204c93c(NNSG2dRenderSurface *surface, u32 screen);
static BOOL func_0204c94c(ClActOamMan *man, const GXOamAttr *oam, u16 affineIdx, u16 palette, u32 mode);
static BOOL func_0204c9a4(const GXOamAttr *oam, u16 affineIdx, BOOL doubleAffine);
static BOOL func_0204c9c8(const GXOamAttr *oam, u16 affineIdx, BOOL doubleAffine);
static u16 func_0204c9f4(const MtxFx22 *mtx);
static u16 func_0204ca08(const MtxFx22 *mtx);
static BOOL func_0204ca1c(const NNSG2dCellData *cell, const MtxFx32 *mtx, const NNSG2dViewRect *view);
static BOOL func_0204cb14(const NNSG2dCellData *cell, const MtxFx32 *mtx, const NNSG2dViewRect *view);
static void func_0204cb8c(ClActDrawState *state, const NNSG2dViewRect *view, u32 a2, u32 a3, const MtxFx32 *mtx);
static BOOL func_0204cc54(fx32 posX, fx32 posY, fx32 left, fx32 right, fx32 top, fx32 bottom, const MtxFx32 *mtx,
                          const NNSG2dViewRect *view);
static void func_0204cda4(ClActTransfer *transfer, u8 count, HeapID heapId);
static void func_0204cdf8(ClActTransfer *transfer);
static void func_0204ce18(ClActTransfer *transfer, u32 count, HeapID heapId);
static void func_0204ce5c(ClActTransfer *transfer);
static void func_0204ce6c(ClActTransfer *transfer);
static ClActTransferTask *func_0204cea0(ClActTransfer *transfer);
static BOOL func_0204cec8(NNS_GFD_DST_TYPE type, u32 dstAddr, void *src, u32 size);
static void func_0204cf08(ClActTransferTask *task, u32 screen, u32 dst, void *src, u32 size);
static void func_0204cf14(ClActTransferTask *task);
static ClActor *func_0204cf38(ClActUnit *unit);
static void func_0204cf70(ClActUnit *unit, ClActor *actor);
static void func_0204cfd8(ClActUnit *unit, ClActor *actor);
static ClActor *func_0204d000(ClActor *head, u8 priority);
static ClActor *func_0204d020(ClActor *tail, u8 priority);
static void func_0204d040(ClActor *actor);
static void func_0204d094(ClActor *actor, const ClActorSetup *setup, u16 surface);
static void func_0204d0d8(ClActor *actor, const ClActResSetup *res);
static void func_0204d0fc(ClActor *actor, u32 surface, ClActorPos *offset);
static void func_0204d11c(ClActResSetup *res, u16 charIdx, u16 plttIdx, ClActCharRes *chars, ClActPlttRes *pltt,
                          NNSG2dCellDataBank *cells, NNSG2dAnimBankData *anims);
static ClActor *func_0204d138(ClActUnit *unit, const ClActorSetup *setup, const ClActResSetup *res, u16 surface,
                              HeapID heapId);
static ClActor *func_0204d17c(ClActUnit *unit, const ClActorSetupEx *setup, const ClActResSetup *res, u16 surface,
                              HeapID heapId);
static void func_0204d1cc(ClActAnim *anim, const ClActResSetup *res, HeapID heapId);
static void func_0204d1f4(ClActAnim *anim);
static u32 func_0204d214(const ClActResSetup *res);
static void func_0204d22c(ClActAnim *anim, const ClActResSetup *res, HeapID heapId);
static void func_0204d24c(ClActAnim *anim, const ClActResSetup *res, HeapID heapId);
static void func_0204d2a8(ClActAnim *anim, const ClActResSetup *res, HeapID heapId);
static void func_0204d30c(ClActAnim *anim);
static void func_0204d318(ClActAnim *anim);
static void func_0204d330(ClActAnim *anim);
static void func_0204d348(ClActAnim *anim, u16 sequence);
static void func_0204d35c(ClActAnim *anim, u16 sequence);
static void func_0204d374(ClActAnim *anim, u16 sequence);
static void setSeqToMultiCellAnim(ClActAnim *anim, u16 sequence);
static void func_0204d3a4(ClActAnim *anim, fx32 time);
static void func_0204d3d0(ClActAnim *anim, fx32 frames);
static void func_0204d3dc(ClActAnim *anim, fx32 frames);
static void func_0204d3e8(ClActAnim *anim, fx32 frames);
static NNSG2dAnimController *func_0204d3f4(ClActAnim *anim);
static NNSG2dAnimController *func_0204d408(ClActAnim *anim);
static NNSG2dAnimController *func_0204d40c(ClActAnim *anim);
static NNSG2dAnimController *func_0204d410(ClActAnim *anim);
static NNSG2dAnimController *func_0204d414(ClActAnim *anim);
static NNSG2dAnimController *func_0204d428(ClActAnim *anim);
static NNSG2dAnimController *func_0204d42c(ClActAnim *anim);
static NNSG2dAnimController *func_0204d430(ClActAnim *anim);
static void func_0204d434(ClActAnim *anim, u16 frame);
static void func_0204d448(ClActAnim *anim, u16 frame);
static void func_0204d454(ClActAnim *anim, u16 frame);
static void func_0204d460(ClActAnim *anim, u16 frame);
static void func_0204d480(ClActAnim *anim, fx32 speed);
static void func_0204d48c(ClActAnim *anim, fx32 speed);
static void func_0204d498(ClActAnim *anim, fx32 speed);
static void func_0204d4a4(ClActAnim *anim);
static void func_0204d4b8(ClActAnim *anim);
static void func_0204d4c4(ClActAnim *anim);
static void func_0204d4d0(ClActAnim *anim);
static const NNSG2dUserExAnimSequenceAttr *func_0204d4e4(ClActAnim *anim, u32 sequence);
static const NNSG2dUserExAnimSequenceAttr *func_0204d524(ClActAnim *anim, u32 sequence);
static const NNSG2dUserExAnimSequenceAttr *func_0204d564(ClActAnim *anim, u32 sequence);
static const NNSG2dUserExCellAttrBank *func_0204d5a4(ClActAnim *anim);
static const NNSG2dUserExCellAttrBank *func_0204d5c8(ClActAnim *anim);
static const NNSG2dUserExCellAttrBank *func_0204d5ec(ClActAnim *anim);
static u32 func_0204d610(ClActAnim *anim);
static u32 func_0204d624(ClActAnim *anim);
static u32 func_0204d62c(ClActAnim *anim);
static u32 func_0204d634(ClActAnim *anim);
static void func_0204d63c(ClActAnim *anim, const ClActorCallback *callback);
static void func_0204d678(ClActAnim *anim);
static void ClActVRAMManager_Init(ClActResMan *man, HeapID heapId, const BGSysVRAMConfig *vramConfig,
                                  const ClActResManSetup *setup);
static void func_0204d6f8(ClActResMan *man);
static BOOL func_0204d798(u32 vramType, u32 which);
static void *func_0204d7b0(ArcTool *arc, u32 fileId, BOOL compressed, HeapID heapId);
static ClActCharRes *func_0204d810(HeapID heapId, u32 count);
static void func_0204d868(ClActResMan *man, u32 idx, NNSG2dCharacterData *charData, NNSG2dCellDataBank *transferCells,
                          u32 vramType);
static void func_0204d984(ClActCharRes *chars);
static void func_0204d98c(NNSG2dCharacterData *charData, NNSG2dVRamType type);
static ClActPlttRes *func_0204d9b0(HeapID heapId, u32 count);
static void func_0204d9f4(ClActResMan *man, u32 idx, void *file, u32 vramType, u16 offset, u16 start, u16 count);
static void func_0204db2c(ClActResMan *man, u32 idx, void *file, u32 vramType, u16 offset);
static void func_0204dbec(ClActPlttRes *pltts);
static ClActCellAnimRes *func_0204dbf4(HeapID heapId, u32 count);
static BOOL func_0204dc2c(ClActResMan *man, u32 idx, void *cellFile, void *animFile);
static void func_0204dc60(ClActCellAnimRes *cellAnims);
static void func_0204dc68(ClActResMan *man, u32 idx, void *file, u32 vramType, NNSG2dCellDataBank *transferCells);

// MWCC sorts a file's data by size, in an order that depends on where each object is declared; these are declared in
// the order that lays them out as in the ROM
static const u16 sSurfaceTypes[2] = { NNS_G2D_SURFACETYPE_MAIN2D, NNS_G2D_SURFACETYPE_SUB2D };
// Unused
const u16 data_02093e44[2] = { NNS_G2D_VRAM_TYPE_2DMAIN, NNS_G2D_VRAM_TYPE_2DSUB };
static void (*const sTransferFuncs[2])(const void *src, u32 offset, u32 size) = {
    GX_LoadOBJ,
    GXS_LoadOBJ,
};
// Global, as only functions that are never compiled read them
const NNSG2dUserExAnimSequenceAttr *(*const gClActAnimGetSeqAttrFuncs[3])(ClActAnim *anim, u32 sequence) = {
    func_0204d4e4,
    func_0204d524,
    func_0204d564,
};
static NNSG2dAnimController *(*const sAnimGetCtrlFuncs[3])(ClActAnim *anim) = {
    func_0204d428,
    func_0204d42c,
    func_0204d430,
};
static void (*const sAnimFreeFuncs[3])(ClActAnim *anim) = {
    func_0204d30c,
    func_0204d318,
    func_0204d330,
};
static const NNSG2dRndCellCullingFunction sCullingFuncs[3] = {
    func_0204ca1c,
    func_0204cb14,
    NULL,
};
static NNSG2dAnimController *(*const sAnimGetCtrlFuncs2[3])(ClActAnim *anim) = {
    func_0204d408,
    func_0204d40c,
    func_0204d410,
};
const NNSG2dUserExCellAttrBank *(*const gClActAnimGetCellAttrFuncs[3])(ClActAnim *anim) = {
    func_0204d5a4,
    func_0204d5c8,
    func_0204d5ec,
};
static void (*const sAnimSetFrameFuncs[3])(ClActAnim *anim, u16 frame) = {
    func_0204d448,
    func_0204d454,
    func_0204d460,
};
static void (*const sAnimInitFuncs[3])(ClActAnim *anim, const ClActResSetup *res, HeapID heapId) = {
    func_0204d22c,
    func_0204d24c,
    func_0204d2a8,
};
static u32 (*const sAnimGetSequenceCountFuncs[3])(ClActAnim *anim) = {
    func_0204d624,
    func_0204d62c,
    func_0204d634,
};
static void (*const sAnimSetSequenceFuncs[3])(ClActAnim *anim, u16 sequence) = {
    func_0204d35c,
    func_0204d374,
    setSeqToMultiCellAnim,
};
static const ClActSurfaceFuncs sSurfaceFuncs[2] = {
    { func_0204c9a4, func_0204c9f4, NULL },
    { func_0204c9c8, func_0204ca08, NULL },
};
static const NNSG2dCharacterData sCharDataTemplate = { 0xffff, 0xffff, GX_TEXFMT_PLTT16, 0, 0, 0, NULL };
static void (*const sAnimTickFuncs[3])(ClActAnim *anim, fx32 frames) = {
    func_0204d3d0,
    func_0204d3dc,
    func_0204d3e8,
};
const ClActSysSetup data_02093f08 = { 0, 0, 0, 512, 4, 124, 4, 124, 0, 32, 32, 32, 32, 16, 16 };
static void (*sAnimSetSpeedFuncs[3])(ClActAnim *anim, fx32 speed) = {
    func_0204d480,
    func_0204d48c,
    func_0204d498,
};
static void (*sAnimRestartFuncs[3])(ClActAnim *anim) = {
    func_0204d4b8,
    func_0204d4c4,
    func_0204d4d0,
};
const ClActSysSetup data_02093f24 = { 0, 0, 0, 192, 4, 124, 4, 124, 0, 32, 32, 32, 32, 16, 16 };
static void (*renderers[4])(ClActUnit *unit) = {
    mainRenderer,
    func_01ff8318,
    func_01ff8610,
    func_01ff8900,
};
// The surfaces of the system's renderer, at the positions ClActSys_Create is given
static ClActSurfaceSetup sSurfaceSetups[2] = {
    { 0, 0, 256, 192, 0, 0 },
    { 0, 0, 256, 192, 1, 0 },
};

static inline void ClAct_SetVec(NNSG2dFVec2 *vec, s16 x, s16 y) {
    vec->x = x << FX32_SHIFT;
    vec->y = y << FX32_SHIFT;
}

static inline void ClAct_RunTransfers(ClActTransfer *transfer) {
    if (transfer->active) {
        func_0204ce6c(transfer);
    }
}

// The most characters a VRAM transfer cell animation of the bank sends at once
static inline u32 ClAct_GetTransferSize(const NNSG2dCellDataBank *bank) {
    BOOL has = TRUE;
    const NNSG2dVramTransferData *data = bank->pVramTransferData;

    if (data == NULL) {
        has = FALSE;
    }
    return has ? data->szByteMax : 0;
}

static inline u32 ClAct_FindFreeChar(const ClActResMan *man) {
    u32 i;

    for (i = 0; i < man->charCount; i++) {
        if (man->chars[i].free) {
            return i;
        }
    }
    return CLACT_RES_NONE;
}

static inline u32 ClAct_FindFreePltt(const ClActResMan *man) {
    u32 i;

    for (i = 0; i < man->plttCount; i++) {
        if (man->pltts[i].free) {
            return i;
        }
    }
    return CLACT_RES_NONE;
}

static inline u32 ClAct_FindFreeCellAnim(const ClActResMan *man) {
    u32 i;

    for (i = 0; i < man->cellAnimCount; i++) {
        if (man->cellAnims[i].free) {
            return i;
        }
    }
    return CLACT_RES_NONE;
}

void ClActSys_Create(const ClActSysSetup *setup, const BGSysVRAMConfig *vramConfig, HeapID heapId) {
    ClActResManSetup resSetup;
    int i = 0;

    g_ClActSys = GFL_HeapAllocate(heapId, sizeof(ClActSys), FALSE, "clact.c", 671);
    MI_CpuFill8(g_ClActSys, 0, sizeof(ClActSys));
    resSetup.charCount = setup->charCount;
    resSetup.plttCount = setup->plttCount;
    resSetup.cellAnimCount = setup->cellAnimCount;
    resSetup.unk6 = setup->unk16;
    resSetup.offsetMain = setup->charOffsetMain;
    resSetup.offsetSub = setup->charOffsetSub;
    ClActVRAMManager_Init(&g_ClActSys->res, heapId, vramConfig, &resSetup);
    func_0204c758(g_ClActSys->oamMans, setup->oamStartMain, setup->oamCountMain, setup->oamStartSub,
                  setup->oamCountSub);
    func_0204c5e4(&g_ClActSys->renderer, setup, heapId);
    func_0204cda4(&g_ClActSys->transfer, setup->transferCount, heapId);
    for (; i < 2; i++) {
        g_ClActSys->palettes[i] = 0;
    }
    func_0204c614(TRUE);
    func_0204c640(TRUE);
}

void func_0204b758(void) {
    func_0204c614(FALSE);
    func_0204c784(g_ClActSys->oamMans);
    func_0204c884(&g_ClActSys->renderer);
    func_0204cdf8(&g_ClActSys->transfer);
    func_0204d6f8(&g_ClActSys->res);
    GFL_HeapFree(g_ClActSys);
    g_ClActSys = NULL;
}

void func_0204b794(void) {
    if (g_ClActSys != NULL) {
        func_0204c614(FALSE);
        func_0204c66c();
        func_0204c690();
        if (g_ClActSys->transfer.active) {
            NNS_G2dUpdateCellTransferStateManager();
        }
        func_0204c614(TRUE);
    }
}

void func_0204b7c8(void) {
    // bwbattle: the OAM and the transfers to OBJ VRAM are Platinum's too (ClActSys_SetVramWritesEnabled)
    if (g_ClActSys != NULL && func_0204c628() && sBwVramWrites) {
        func_0204c798(g_ClActSys->oamMans);
        ClAct_RunTransfers(&g_ClActSys->transfer);
        func_0204c640(TRUE);
    }
}

void func_0204b7fc(void) {
    if (g_ClActSys != NULL) {
        func_0204c614(FALSE);
        func_0204c66c();
        func_0204c614(TRUE);
    }
}

u32 func_0204b81c(ArcTool *arc, u32 fileId, BOOL compressed, u32 vramType, HeapID heapId) {
    u32 idx = ClAct_FindFreeChar(&g_ClActSys->res);

    if (idx != CLACT_RES_NONE) {
        ClActCharRes *res = &g_ClActSys->res.chars[idx];
        void *data = func_0204d7b0(arc, fileId, compressed, HEAPID_TAIL(heapId));

        func_0204dc68(&g_ClActSys->res, idx, data, vramType, NULL);
        GFL_HeapFree(data);
        res->data = NULL;
        res->extData = FALSE;
        return idx;
    }
    return CLACT_RES_NONE;
}

u32 func_0204b8bc(u32 size, u32 vramType, HeapID heapId) {
    u32 idx = ClAct_FindFreeChar(&g_ClActSys->res);
    ClActCharRes *res;
    NNSG2dCharacterData charData;

    if (idx == CLACT_RES_NONE) {
        return CLACT_RES_NONE;
    }
    res = &g_ClActSys->res.chars[idx];
    charData = sCharDataTemplate;
    charData.mappingType = GX_GetOBJVRamModeChar();
    charData.szByte = size;
    charData.pRawData = GFL_HeapAllocate(HEAPID_TAIL(heapId), size, TRUE, "clact.c", 992);
    func_0204d868(&g_ClActSys->res, idx, &charData, NULL, vramType);
    res->free = FALSE;
    res->data = NULL;
    res->extData = FALSE;
    GFL_HeapFree(charData.pRawData);
    return idx;
}

void func_0204b98c(u32 chars) {
    if (!GFL_VRAMManagerAllocIsInvalid(&g_ClActSys->res.chars[chars].allocMain)) {
        GFL_VRAMManagerDeAlloc(g_ClActSys->res.vmanMain, &g_ClActSys->res.chars[chars].allocMain);
    }
    if (!GFL_VRAMManagerAllocIsInvalid(&g_ClActSys->res.chars[chars].allocSub)) {
        GFL_VRAMManagerDeAlloc(g_ClActSys->res.vmanSub, &g_ClActSys->res.chars[chars].allocSub);
    }
    if (g_ClActSys->res.chars[chars].data != NULL) {
        if (g_ClActSys->res.chars[chars].extData == FALSE) {
            GFL_HeapFree(g_ClActSys->res.chars[chars].data);
        } else {
            g_ClActSys->res.chars[chars].extData = FALSE;
        }
        g_ClActSys->res.chars[chars].data = NULL;
    }
    g_ClActSys->res.chars[chars].free = TRUE;
    g_ClActSys->res.chars[chars].size = 0;
}

void func_0204ba40(u32 chars, NNSG2dCharacterData *character) {
    if (NNS_G2dIsImageReadyToUse(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DMAIN)) {
        u32 addr = NNS_G2dGetImageLocation(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DMAIN);

        DC_FlushRange(character->pRawData, character->szByte);
        GX_LoadOBJ(character->pRawData, addr, character->szByte);
    }
    if (NNS_G2dIsImageReadyToUse(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DSUB)) {
        u32 addr = NNS_G2dGetImageLocation(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DSUB);

        DC_FlushRange(character->pRawData, character->szByte);
        GXS_LoadOBJ(character->pRawData, addr, character->szByte);
    }
}

void func_0204bab8(u32 chars, void *src, u32 size, u32 offset, u32 vramType) {
    if (vramType == CLACT_VRAM_MAIN || vramType == CLACT_VRAM_BOTH) {
        if (NNS_G2dIsImageReadyToUse(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DMAIN)) {
            u32 addr = NNS_G2dGetImageLocation(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DMAIN);

            DC_FlushRange(src, size);
            GX_LoadOBJ(src, addr + offset, size);
        }
    }
    if (vramType == CLACT_VRAM_SUB || vramType == CLACT_VRAM_BOTH) {
        if (NNS_G2dIsImageReadyToUse(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DSUB)) {
            u32 addr = NNS_G2dGetImageLocation(&g_ClActSys->res.chars[chars].proxy, NNS_G2D_VRAM_TYPE_2DSUB);

            DC_FlushRange(src, size);
            GXS_LoadOBJ(src, addr + offset, size);
        }
    }
}

void func_0204bb58(u32 chars, NNSG2dImageProxy *proxy) {
    *proxy = g_ClActSys->res.chars[chars].proxy;
}

u32 func_0204bb80(u32 chars, BOOL sub) {
    NNSG2dImageProxy proxy;
    NNSG2dVRamType type = NNS_G2D_VRAM_TYPE_2DMAIN;

    if (sub) {
        type = NNS_G2D_VRAM_TYPE_2DSUB;
    }
    func_0204bb58(chars, &proxy);
    return NNS_G2dGetImageLocation(&proxy, type);
}

u32 func_0204bba0(ArcTool *arc, u32 fileId, u32 vramType, u16 offset, HeapID heapId) {
    return func_0204bbb8(arc, fileId, vramType, offset, 0, 0, heapId);
}

u32 func_0204bbb8(ArcTool *arc, u32 fileId, u32 vramType, u16 offset, u16 start, u16 count, HeapID heapId) {
    u32 idx = ClAct_FindFreePltt(&g_ClActSys->res);

    if (idx != CLACT_RES_NONE) {
        void *data = GFL_ArcToolReadHeapNew(arc, fileId, HEAPID_TAIL(heapId));

        func_0204d9f4(&g_ClActSys->res, idx, data, vramType, offset, start, count);
        GFL_HeapFree(data);
        return idx;
    }
    return idx;
}

u32 func_0204bc48(ArcTool *arc, u32 fileId, u32 vramType, u16 offset, HeapID heapId) {
    u32 idx = ClAct_FindFreePltt(&g_ClActSys->res);

    if (idx != CLACT_RES_NONE) {
        void *data = GFL_ArcToolReadHeapNew(arc, fileId, HEAPID_TAIL(heapId));

        func_0204db2c(&g_ClActSys->res, idx, data, vramType, offset);
        GFL_HeapFree(data);
        return idx;
    }
    return idx;
}

void func_0204bcd0(u32 palette) {
    g_ClActSys->res.pltts[palette].free = TRUE;
    g_ClActSys->res.pltts[palette].size = 0;
}

void func_0204bd10(u32 palette, NNSG2dPaletteData *data, u32 count) {
    u32 size = count * 32;

    if (count == 0) {
        size = data->szByte;
    }
    if (NNS_G2dIsImagePaletteReadyToUse(&g_ClActSys->res.pltts[palette].proxy, NNS_G2D_VRAM_TYPE_2DMAIN)) {
        u32 addr = NNS_G2dGetImagePaletteLocation(&g_ClActSys->res.pltts[palette].proxy, NNS_G2D_VRAM_TYPE_2DMAIN);

        DC_FlushRange(data->pRawData, size);
        GX_LoadOBJPltt(data->pRawData, addr, size);
    }
    if (NNS_G2dIsImagePaletteReadyToUse(&g_ClActSys->res.pltts[palette].proxy, NNS_G2D_VRAM_TYPE_2DSUB)) {
        u32 addr = NNS_G2dGetImagePaletteLocation(&g_ClActSys->res.pltts[palette].proxy, NNS_G2D_VRAM_TYPE_2DSUB);

        DC_FlushRange(data->pRawData, size);
        GXS_LoadOBJPltt(data->pRawData, addr, size);
    }
}

void func_0204bd9c(u32 palette, NNSG2dImagePaletteProxy *proxy) {
    *proxy = g_ClActSys->res.pltts[palette].proxy;
}

u32 func_0204bdc0(u32 palette, BOOL sub) {
    NNSG2dImagePaletteProxy proxy;
    NNSG2dVRamType type = NNS_G2D_VRAM_TYPE_2DMAIN;

    if (sub) {
        type = NNS_G2D_VRAM_TYPE_2DSUB;
    }
    func_0204bd9c(palette, &proxy);
    // bwbattle: the location in the view's palettes, without the OBJ extension's offset, as palette fades index them
    if (!sub) {
        return NNS_G2dGetImagePaletteLocation(&proxy, type) - BW_OBJ_EXT_PLTT_OFFSET;
    }
    return NNS_G2dGetImagePaletteLocation(&proxy, type);
}

u32 func_0204bde0(ArcTool *arc, u32 cellFileId, u32 animFileId, HeapID heapId) {
    u32 idx = ClAct_FindFreeCellAnim(&g_ClActSys->res);
    ClActCellAnimRes *cellAnims = g_ClActSys->res.cellAnims;
    ClActCellAnimRes *res = &cellAnims[idx];

    // BUG: this does not check that a cell animation was free, and writes before the array when none is
#ifdef BUGFIX
    if (idx == CLACT_RES_NONE) {
        return CLACT_RES_NONE;
    }
#endif
    cellAnims[idx].cellFile = GFL_ArcToolReadHeapNew(arc, cellFileId, heapId);
    res->animFile = GFL_ArcToolReadHeapNew(arc, animFileId, heapId);
    func_0204dc2c(&g_ClActSys->res, idx, cellAnims[idx].cellFile, res->animFile);
    return idx;
}

void func_0204be64(u32 cellAnims) {
    ClActCellAnimRes *res = &g_ClActSys->res.cellAnims[cellAnims];

    if (res->cellFile != NULL) {
        GFL_HeapFree(res->cellFile);
        res->cellFile = NULL;
    }
    if (res->animFile != NULL) {
        GFL_HeapFree(res->animFile);
        res->animFile = NULL;
    }
    res->free = TRUE;
}

ClActRenderer *func_0204be9c(const ClActSurfaceSetup *setups, u16 count, HeapID heapId) {
    ClActRenderer *renderer = GFL_HeapAllocate(heapId, sizeof(ClActRenderer), TRUE, "clact.c", 1611);

    func_0204c800(renderer, setups, count, heapId);
    return renderer;
}

void func_0204becc(ClActRenderer *renderer) {
    func_0204c884(renderer);
    GFL_HeapFree(renderer);
}

void func_0204bedc(ClActRenderer *renderer, u32 surface, const ClActorPos *pos) {
    ClAct_SetVec(&renderer->surfaces[surface].viewRect.posTopLeft, pos->x, pos->y);
}

void func_0204befc(ClActRenderer *renderer, u32 surface, ClActorPos *pos) {
    const NNSG2dFVec2 *vec = &renderer->surfaces[surface].viewRect.posTopLeft;

    pos->x = vec->x >> FX32_SHIFT;
    pos->y = vec->y >> FX32_SHIFT;
}

void func_0204bf14(ClActRenderer *renderer, BOOL cull) {
    func_0204c890(renderer, cull);
}

ClActUnit *func_0204bf1c(u16 count, u8 priority, HeapID heapId) {
    ClActUnit *unit;
    int i = 0;

    unit = GFL_HeapAllocate(heapId, sizeof(ClActUnit), FALSE, "clact.c", 1795);
    MI_CpuFill8(unit, 0, sizeof(ClActUnit));
    unit->actors = GFL_HeapAllocate(heapId, count * sizeof(ClActor), FALSE, "clact.c", 1799);
    unit->count = count;
    unit->priority = priority;
    for (; i < count; i++) {
        func_0204d040(&unit->actors[i]);
    }
    func_0204c028(unit);
    func_0204bfd4(unit, TRUE);
    func_0204c6d8(unit);
    return unit;
}

void func_0204bf98(ClActUnit *unit) {
    int i;

    func_0204c724(unit);
    for (i = 0; i < unit->count; i++) {
        if (unit->actors[i].next != NULL) {
            func_0204c108(&unit->actors[i]);
        }
    }
    GFL_HeapFree(unit->actors);
    GFL_HeapFree(unit);
}

void func_0204bfd4(ClActUnit *unit, BOOL active) {
    unit->active = active;
}

BOOL func_0204bfe8(ClActUnit *unit) {
    return unit->active;
}

void func_0204bff0(ClActUnit *unit, BOOL a1) {
    unit->unk5 = a1;
}

void func_0204c004(ClActUnit *unit, BOOL a1) {
    unit->unk6 = a1;
}

void func_0204c018(ClActUnit *unit, ClActRenderer *renderer) {
    unit->renderer = renderer;
    unit->rendererType = 1;
}

void func_0204c028(ClActUnit *unit) {
    unit->renderer = &g_ClActSys->renderer;
    unit->rendererType = 0;
}

ClActor *func_0204c040(ClActUnit *unit, u32 chars, u32 palette, u32 cellAnims, const ClActorSetup *setup, u16 surface,
                       HeapID heapId) {
    ClActResSetup res;

    func_0204d11c(&res, chars, palette, &g_ClActSys->res.chars[chars], &g_ClActSys->res.pltts[palette],
                  g_ClActSys->res.cellAnims[cellAnims].cells, g_ClActSys->res.cellAnims[cellAnims].anims);
    return func_0204d138(unit, setup, &res, surface, heapId);
}

ClActor *func_0204c0a4(ClActUnit *unit, u32 chars, u32 palette, u32 cellAnims, const ClActorSetupEx *setup, u16 surface,
                       HeapID heapId) {
    ClActResSetup res;

    func_0204d11c(&res, chars, palette, &g_ClActSys->res.chars[chars], &g_ClActSys->res.pltts[palette],
                  g_ClActSys->res.cellAnims[cellAnims].cells, g_ClActSys->res.cellAnims[cellAnims].anims);
    return func_0204d17c(unit, setup, &res, surface, heapId);
}

void func_0204c108(ClActor *actor) {
    func_0204cfd8(actor->unit, actor);
    func_0204d1f4(&actor->anim);
    func_0204d040(actor);
}

void func_0204c124(ClActor *actor, BOOL visible) {
    actor->visible = visible;
}

BOOL func_0204c138(ClActor *actor) {
    return actor->visible;
}

void func_0204c140(ClActor *actor, const ClActorPos *pos, u16 surface) {
    ClActorPos offset;
    ClActorPos actorPos;

    func_0204d0fc(actor, surface, &offset);
    actorPos.x = pos->x + offset.x;
    actorPos.y = pos->y + offset.y;
    func_0204c210(actor, &actorPos);
}

void func_0204c178(ClActor *actor, ClActorPos *pos, u16 surface) {
    ClActorPos offset;

    func_0204d0fc(actor, surface, &offset);
    func_0204c21c(actor, pos);
    pos->x -= offset.x;
    pos->y -= offset.y;
}

void func_0204c1a8(ClActor *actor, s16 value, u16 surface, u32 axis) {
    ClActorPos offset;

    func_0204d0fc(actor, surface, &offset);
    if (axis == 0) {
        value += offset.x;
    } else {
        value += offset.y;
    }
    func_0204c228(actor, value, axis);
}

s16 func_0204c1dc(ClActor *actor, u16 surface, u32 axis) {
    ClActorPos offset;
    s16 value;

    func_0204d0fc(actor, surface, &offset);
    value = func_0204c234(actor, axis);
    if (axis == 0) {
        return value - offset.x;
    }
    return value - offset.y;
}

void func_0204c210(ClActor *actor, const ClActorPos *pos) {
    actor->pos = *pos;
}

void func_0204c21c(const ClActor *actor, ClActorPos *pos) {
    *pos = actor->pos;
}

void func_0204c228(ClActor *actor, s16 value, u32 axis) {
    if (axis == 0) {
        actor->pos.x = value;
    } else {
        actor->pos.y = value;
    }
}

s16 func_0204c234(ClActor *actor, u32 axis) {
    if (axis == 0) {
        return actor->pos.x;
    }
    return actor->pos.y;
}

void func_0204c244(ClActor *actor, u32 affineMode) {
    actor->affineMode = affineMode;
}

void func_0204c258(ClActor *actor, const ClActorPos *center) {
    actor->affineCenter = *center;
}

void func_0204c264(ClActor *actor, s16 value, u32 axis) {
    if (axis == 0) {
        actor->affineCenter.x = value;
    } else {
        actor->affineCenter.y = value;
    }
}

void func_0204c270(ClActor *actor, const ClActorScale *scale) {
    actor->scale = *scale;
}

void func_0204c27c(ClActor *actor, ClActorScale *scale) {
    *scale = actor->scale;
}

void func_0204c288(ClActor *actor, fx32 value, u32 axis) {
    if (axis == 0) {
        actor->scale.x = value;
    } else {
        actor->scale.y = value;
    }
}

fx32 func_0204c294(ClActor *actor, u32 axis) {
    if (axis == 0) {
        return actor->scale.x;
    }
    return actor->scale.y;
}

void func_0204c2a0(ClActor *actor, u16 rotation) {
    actor->rotation = rotation;
}

u16 func_0204c2a8(ClActor *actor) {
    return actor->rotation;
}

void func_0204c2b0(ClActor *actor, u32 axis, BOOL flip) {
    u32 bit = 1;

    if (axis) {
        bit = 2;
    }
    if (flip == FALSE) {
        actor->flip &= ~bit;
    } else {
        actor->flip |= bit;
    }
}

BOOL func_0204c2f0(ClActor *actor, u32 axis) {
    BOOL flip = FALSE;

    if (axis == 0) {
        if (actor->flip & 1) {
            flip = TRUE;
        }
    } else if (actor->flip & 2) {
        flip = TRUE;
    }
    return flip;
}

void func_0204c318(ClActor *actor, u32 mode) {
    actor->objMode = mode;
    if (mode == GX_OAM_MODE_NORMAL) {
        if (actor->unk60_8 & 8) {
            actor->unk60_8 ^= 8;
        }
    } else {
        actor->unk60_8 |= 8;
    }
}

u32 func_0204c370(ClActor *actor) {
    return actor->objMode;
}

void func_0204c378(ClActor *actor, u32 palette, u32 a2) {
    actor->palette = palette;
    actor->unk60_31 = a2;
}

u8 func_0204c39c(ClActor *actor) {
    return actor->palette;
}

void func_0204c3a8(ClActor *actor, const NNSG2dImagePaletteProxy *proxy) {
    NNS_G2dIsImagePaletteReadyToUse(proxy, NNS_G2D_VRAM_TYPE_2DMAIN);
    NNS_G2dIsImagePaletteReadyToUse(proxy, NNS_G2D_VRAM_TYPE_2DSUB);
    actor->pltProxy = *proxy;
}

void func_0204c3d0(ClActor *actor, NNSG2dImagePaletteProxy *proxy) {
    *proxy = actor->pltProxy;
}

void func_0204c3e4(ClActor *actor, const NNSG2dImageProxy *proxy) {
    NNS_G2dIsImageReadyToUse(proxy, NNS_G2D_VRAM_TYPE_2DMAIN);
    NNS_G2dIsImageReadyToUse(proxy, NNS_G2D_VRAM_TYPE_2DSUB);
    actor->imgProxy = *proxy;
}

void func_0204c40c(ClActor *actor, NNSG2dImageProxy *proxy) {
    *proxy = actor->imgProxy;
}

u16 func_0204c428(ClActor *actor) {
    return actor->charIdx;
}

u16 func_0204c430(ClActor *actor) {
    return actor->plttIdx;
}

void func_0204c438(ClActor *actor, u8 priority) {
    actor->priority = priority;
    func_0204cfd8(actor->unit, actor);
    func_0204cf70(actor->unit, actor);
}

u8 func_0204c45c(ClActor *actor) {
    return actor->priority;
}

void func_0204c468(ClActor *actor, u32 bgPriority) {
    actor->bgPriority = bgPriority;
}

u8 func_0204c47c(ClActor *actor) {
    return actor->bgPriority;
}

void func_0204c488(ClActor *actor, u16 sequence) {
    actor->sequence = sequence;
    func_0204d348(&actor->anim, sequence);
    func_0204c540(actor);
}

u16 func_0204c4a0(ClActor *actor) {
    return actor->sequence;
}

u16 func_0204c4a8(ClActor *actor) {
    return func_0204d610(&actor->anim);
}

void func_0204c4b8(ClActor *actor, u16 sequence) {
    if (sequence != actor->sequence) {
        func_0204c488(actor, sequence);
        func_0204c540(actor);
    }
}

void func_0204c4d4(ClActor *actor, fx32 time) {
    func_0204d3a4(&actor->anim, time);
}

void func_0204c4e0(ClActor *actor, fx32 speed) {
    func_0204d46c(&actor->anim, speed);
    func_0204d3bc(&actor->anim, FX32_ONE);
}

fx32 func_0204c4f8(ClActor *actor) {
    return func_0204d414(&actor->anim)->currentTime;
}

void func_0204c504(ClActor *actor, u16 frame) {
    func_0204d434(&actor->anim, frame);
}

u16 func_0204c510(ClActor *actor) {
    return NNS_G2dGetAnimCtrlCurrentFrame(func_0204d414(&actor->anim));
}

void func_0204c520(ClActor *actor, BOOL a1) {
    actor->unk60_25 = a1;
}

BOOL func_0204c534(ClActor *actor) {
    return actor->unk60_25;
}

void func_0204c53c(ClActor *actor, fx32 a1) {
    actor->unk54 = a1;
}

void func_0204c540(ClActor *actor) {
    func_0204d3f4(&actor->anim)->bActive = TRUE;
}

void func_0204c550(ClActor *actor) {
    func_0204d3f4(&actor->anim)->bActive = FALSE;
}

BOOL func_0204c560(ClActor *actor) {
    return func_0204d414(&actor->anim)->bActive;
}

void func_0204c56c(ClActor *actor) {
    func_0204d3f4(&actor->anim)->bActive = TRUE;
    func_0204d4a4(&actor->anim);
}

void func_0204c584(ClActor *actor, u32 playMode) {
    NNSG2dAnimController *animCtrl;

    actor->playMode = playMode;
    animCtrl = func_0204d3f4(&actor->anim);
    if (playMode == 0) {
        animCtrl->bReverse = FALSE;
        animCtrl->overriddenPlayMode = 0;
    } else {
        animCtrl->overriddenPlayMode = playMode;
    }
}

void func_0204c5b0(ClActor *actor, const ClActorCallback *callback) {
    func_0204d63c(&actor->anim, callback);
}

void func_0204c5bc(ClActor *actor) {
    func_0204d678(&actor->anim);
}

void func_0204c5c8(ClActor *actor, BOOL a1) {
    if (a1) {
        actor->unk64_0 = 0;
    } else {
        actor->unk64_0 = 1;
    }
}

void func_0204c5e4(ClActRenderer *renderer, const ClActSysSetup *setup, HeapID heapId) {
    sSurfaceSetups[0].x = setup->mainX;
    sSurfaceSetups[0].y = setup->mainY;
    sSurfaceSetups[1].x = setup->subX;
    sSurfaceSetups[1].y = setup->subY;
    func_0204c800(renderer, sSurfaceSetups, 2, heapId);
}

static void func_0204c614(BOOL updating) {
    if (g_ClActSys != NULL) {
        g_ClActSys->updating = updating;
    }
}

static BOOL func_0204c628(void) {
    if (g_ClActSys != NULL) {
        return g_ClActSys->updating;
    }
    return FALSE;
}

static void func_0204c640(BOOL ready) {
    if (g_ClActSys != NULL) {
        g_ClActSys->oamReady = ready;
    }
}

static BOOL func_0204c654(void) {
    if (g_ClActSys != NULL) {
        return g_ClActSys->oamReady;
    }
    return FALSE;
}

static void func_0204c66c(void) {
    if (g_ClActSys != NULL && func_0204c654()) {
        func_0204c7b0(g_ClActSys->oamMans);
        func_0204c640(FALSE);
    }
}

// bwbattle: the renderers, which run from ITCM in BW2 (pokebw2 has no C for them; this is from the Thumb in the ROM's
// ITCM, 0x01ff8020 to 0x01ff8bf0). mainRenderer and func_01ff8318 are the same: each actor sets the renderer up for
// itself. func_01ff8610 and func_01ff8900 are the same: the first actor sets it up for all of them. The positions
// were floats rounded to fx32, which the integer positions make exact

// The renderer's state from an actor: its images, affine and flip modes, what overwrites its OAMs, and the palettes
// the OAM callbacks put in (func_0204c9a4)
static u16 ClAct_SetupRenderer(ClActRenderer *renderer, ClActor *actor) {
    u16 flags;

    NNS_G2dSetRendererImageProxy(&renderer->renderer, &actor->imgProxy, &actor->pltProxy);
    NNS_G2dSetRendererAffineOverwriteMode(&renderer->renderer, actor->affineMode);
    if (actor->affineMode == 0) {
        NNS_G2dSetRndCoreFlipMode(&renderer->renderer.rendererCore, func_0204c2f0(actor, 1), func_0204c2f0(actor, 0));
        flags = actor->unk64_0 ? (renderer->unk9E | NNS_G2D_RDR_OPZHINT_NOT_SR) : renderer->unk9E;
    } else {
        flags = renderer->unk9E;
    }
    NNS_G2dSetRendererOverwriteEnable(&renderer->renderer, actor->unk60_8);
    NNS_G2dSetRendererOverwriteDisable(&renderer->renderer, ~actor->unk60_8);
    NNS_G2dSetRendererOverwriteMosaicFlag(&renderer->renderer, actor->unk60_24);
    NNS_G2dSetRendererOverwriteOBJMode(&renderer->renderer, actor->objMode);
    NNS_G2dSetRendererOverwritePriority(&renderer->renderer, actor->bgPriority);
    g_ClActSys->palettes[0] = (NNS_G2dGetImagePaletteLocation(&actor->pltProxy, NNS_G2D_VRAM_TYPE_2DMAIN) >> 5) +
                              actor->palette;
    g_ClActSys->palettes[1] = (NNS_G2dGetImagePaletteLocation(&actor->pltProxy, NNS_G2D_VRAM_TYPE_2DSUB) >> 5) +
                              actor->palette;
    g_ClActSys->paletteMode = actor->unk60_31;
    return flags;
}

static void ClAct_DrawActor(ClActRenderer *renderer, ClActor *actor, u16 flags) {
    NNS_G2dBeginRenderingEx(&renderer->renderer, flags);
    NNS_G2dTranslate(actor->pos.x << FX32_SHIFT, actor->pos.y << FX32_SHIFT, 0);
    if (actor->affineMode != 0) {
        NNS_G2dTranslate(actor->affineCenter.x << FX32_SHIFT, actor->affineCenter.y << FX32_SHIFT, 0);
        NNS_G2dScale(actor->scale.x, actor->scale.y, FX32_ONE);
        NNS_G2dRotZ(FX_SinIdx(actor->rotation), FX_CosIdx(actor->rotation));
        NNS_G2dTranslate(-(actor->affineCenter.x << FX32_SHIFT), -(actor->affineCenter.y << FX32_SHIFT), 0);
    }
    switch (actor->anim.type) {
    case 0:
        if (actor->unk64_0 == TRUE) {
            NNS_G2dDrawCell(actor->anim.cellAnim.pCurrentCell);
        } else {
            NNS_G2dDrawCellAnimation(&actor->anim.cellAnim);
        }
        break;
    case 1:
        NNS_G2dDrawCellAnimation(&actor->anim.cellAnim);
        break;
    case 2:
        NNS_G2dDrawMultiCellAnimation(&actor->anim.mcAnim);
        break;
    }
    NNS_G2dEndRendering();
}

static void ClAct_TickActor(ClActor *actor) {
    if (actor->unk60_25 == TRUE) {
        func_0204d46c(&actor->anim, actor->unk54);
        func_0204d3bc(&actor->anim, FX32_ONE);
    }
}

void mainRenderer(ClActUnit *unit) {
    ClActor *actor = unit->actorList;

    do {
        if (actor->visible) {
            ClAct_DrawActor(unit->renderer, actor, ClAct_SetupRenderer(unit->renderer, actor));
        }
        ClAct_TickActor(actor);
        actor = actor->next;
    } while (actor != unit->actorList);
}

void func_01ff8318(ClActUnit *unit) {
    mainRenderer(unit);
}

void func_01ff8610(ClActUnit *unit) {
    ClActor *actor = unit->actorList;
    u16 flags = ClAct_SetupRenderer(unit->renderer, actor);

    do {
        if (actor->visible) {
            ClAct_DrawActor(unit->renderer, actor, flags);
        }
        ClAct_TickActor(actor);
        actor = actor->next;
    } while (actor != unit->actorList);
}

void func_01ff8900(ClActUnit *unit) {
    func_01ff8610(unit);
}

static void func_0204c690(void) {
    ClActUnit *unit = g_ClActSys->units;

    if (unit != NULL) {
        do {
            if (unit->active && unit->actorList != NULL) {
                renderers[unit->unk5 + unit->unk6 * 2](unit);
            }
            unit = unit->next;
        } while (unit != g_ClActSys->units);
    }
}

static void func_0204c6d8(ClActUnit *unit) {
    ClActUnit *cur;

    if (g_ClActSys->units == NULL) {
        g_ClActSys->units = unit;
        g_ClActSys->units->next = unit;
        g_ClActSys->units->prev = unit;
        return;
    }
    cur = g_ClActSys->units;
    do {
        if (cur->priority > unit->priority) {
            break;
        }
        cur = cur->next;
    } while (cur != g_ClActSys->units);
    cur = cur->prev;
    unit->prev = cur;
    unit->next = cur->next;
    cur->next->prev = unit;
    cur->next = unit;
}

static void func_0204c724(ClActUnit *unit) {
    if (g_ClActSys->units == unit) {
        if (unit->next == unit) {
            g_ClActSys->units = NULL;
        } else {
            g_ClActSys->units = unit->next;
        }
    }
    unit->next->prev = unit->prev;
    unit->prev->next = unit->next;
    unit->next = NULL;
    unit->prev = NULL;
}

static void func_0204c758(ClActOamMan *mans, u8 fromMain, u8 numMain, u8 fromSub, u8 numSub) {
    NNS_G2dInitOamManagerModule();
    func_0204c7c8(&mans[0], fromMain, numMain, NNS_G2D_OAMTYPE_MAIN);
    func_0204c7c8(&mans[1], fromSub, numSub, NNS_G2D_OAMTYPE_SUB);
}

static void func_0204c784(ClActOamMan *mans) {
    func_0204c7b0(mans);
    func_0204c798(mans);
    NNS_G2dInitOamManagerModule();
}

static void func_0204c798(ClActOamMan *mans) {
    int i;

    for (i = 0; i < 2; i++) {
        func_0204c7e0(&mans[i]);
    }
}

static void func_0204c7b0(ClActOamMan *mans) {
    int i;

    for (i = 0; i < 2; i++) {
        func_0204c7f0(&mans[i]);
    }
}

static void func_0204c7c8(ClActOamMan *man, u8 from, u8 num, u32 type) {
    if (num == 0) {
        man->active = FALSE;
    } else {
        NNS_G2dGetNewOamManagerInstanceAsFastTransferMode(&man->man, from, num, type);
        man->active = TRUE;
    }
}

static void func_0204c7e0(ClActOamMan *man) {
    if (man->active) {
        NNS_G2dApplyOamManagerToHW(&man->man);
    }
}

static void func_0204c7f0(ClActOamMan *man) {
    if (man->active) {
        NNS_G2dResetOamManagerBuffer(&man->man);
    }
}

static void func_0204c800(ClActRenderer *renderer, const ClActSurfaceSetup *setups, u16 count, HeapID heapId) {
    u32 i;

    NNS_G2dInitRenderer(&renderer->renderer);
    renderer->renderer.spriteZoffsetStep = 1;
    renderer->surfaces = GFL_HeapAllocate(heapId, count * sizeof(NNSG2dRenderSurface), FALSE, "clact.c", 4124);
    renderer->surfaceCount = count;
    for (i = 0; i < count; i++) {
        func_0204c8d0(&renderer->surfaces[i], &setups[i]);
        NNS_G2dAddRendererTargetSurface(&renderer->renderer, &renderer->surfaces[i]);
    }
    if (renderer->surfaceCount == 1) {
        renderer->unk9E = 2;
    }
}

static void func_0204c884(ClActRenderer *renderer) {
    GFL_HeapFree(renderer->surfaces);
}

static void func_0204c890(ClActRenderer *renderer, BOOL cull) {
    int i;
    void *callback;

    if (cull) {
        callback = func_0204cb8c;
    } else {
        callback = NULL;
        renderer->renderer.rendererCore.bDrawEnable = TRUE;
    }
    for (i = 0; i < renderer->surfaceCount; i++) {
        renderer->surfaces[i].pBeforeDrawOamBackFunc = callback;
    }
}

static void func_0204c8d0(NNSG2dRenderSurface *surface, const ClActSurfaceSetup *setup) {
    ClActSurfaceFuncs funcs;

    NNS_G2dInitRenderSurface(surface);
    ClAct_SetVec(&surface->viewRect.posTopLeft, setup->x, setup->y);
    ClAct_SetVec(&surface->viewRect.sizeView, setup->w, setup->h);
    func_0204c93c(surface, setup->screen);
    funcs = sSurfaceFuncs[setup->screen];
    funcs.culling = sCullingFuncs[setup->culling];
    surface->pFuncOamRegister = funcs.oamRegister;
    surface->pFuncOamAffineRegister = funcs.affineRegister;
    surface->pFuncVisibilityCulling = funcs.culling;
}

static void func_0204c93c(NNSG2dRenderSurface *surface, u32 screen) {
    surface->type = sSurfaceTypes[screen];
}

static BOOL func_0204c94c(ClActOamMan *man, const GXOamAttr *oam, u16 affineIdx, u16 palette, u32 mode) {
    GXOamAttr attr;

    MI_CpuCopy8(oam, &attr, sizeof(GXOamAttr));
    if (mode == 0) {
        attr.cParam += palette;
    } else if (mode == 1) {
        attr.cParam = palette;
    }
    return NNS_G2dEntryOamManagerOamWithAffineIdx(&man->man, &attr, affineIdx);
}

static BOOL func_0204c9a4(const GXOamAttr *oam, u16 affineIdx, BOOL doubleAffine) {
    return func_0204c94c(&g_ClActSys->oamMans[0], oam, affineIdx, g_ClActSys->palettes[0], g_ClActSys->paletteMode);
}

static BOOL func_0204c9c8(const GXOamAttr *oam, u16 affineIdx, BOOL doubleAffine) {
    return func_0204c94c(&g_ClActSys->oamMans[1], oam, affineIdx, g_ClActSys->palettes[1], g_ClActSys->paletteMode);
}

static u16 func_0204c9f4(const MtxFx22 *mtx) {
    return NNS_G2dEntryOamManagerAffine(&g_ClActSys->oamMans[0].man, mtx);
}

static u16 func_0204ca08(const MtxFx22 *mtx) {
    return NNS_G2dEntryOamManagerAffine(&g_ClActSys->oamMans[1].man, mtx);
}

// Whether a cell's bounding rect, or circle, is in view, its corners transformed by the matrix
static BOOL func_0204ca1c(const NNSG2dCellData *cell, const MtxFx32 *mtx, const NNSG2dViewRect *view) {
    fx32 tmp;
    const NNSG2dCellBoundingRectS16 *rect = &((const NNSG2dCellDataWithBR *)cell)->boundingRect;
    u32 r = NNSi_G2dGetCellBoundingSphereR(cell);
    fx32 posX = mtx->_20 - view->posTopLeft.x;
    fx32 posY = mtx->_21 - view->posTopLeft.y;
    fx32 maxY;
    fx32 minX;
    fx32 maxX;
    fx32 minY;
    fx32 top;
    fx32 bottom;
    fx32 left;
    fx32 right;

    if (((cell->cellAttr >> NNSi_G2D_CELLATTR_BOUNDINGRECT_SHIFT) & 1) == 1) {
        minY = rect->minY << FX32_SHIFT;
        maxY = rect->maxY << FX32_SHIFT;
        minX = rect->minX << FX32_SHIFT;
        maxX = rect->maxX << FX32_SHIFT;
    } else {
        minY = -r << FX32_SHIFT;
        maxY = r << FX32_SHIFT;
        minX = minY;
        maxX = maxY;
    }
    top = posY + (FX_MulFunc(minY, mtx->_01) + FX_MulFunc(minY, mtx->_11));
    bottom = posY + (FX_MulFunc(maxY, mtx->_01) + FX_MulFunc(maxY, mtx->_11));
    left = posX + (FX_MulFunc(minX, mtx->_00) + FX_MulFunc(minX, mtx->_10));
    right = posX + (FX_MulFunc(maxX, mtx->_00) + FX_MulFunc(maxX, mtx->_10));
    if (bottom < top) {
        tmp = bottom;
        bottom = top;
        top = tmp;
    }
    if (right < left) {
        tmp = right;
        right = left;
        left = tmp;
    }
    if (bottom > 0 && top < view->sizeView.y && right > 0 && left < view->sizeView.x) {
        return TRUE;
    }
    return FALSE;
}

// The same, untransformed
static BOOL func_0204cb14(const NNSG2dCellData *cell, const MtxFx32 *mtx, const NNSG2dViewRect *view) {
    const NNSG2dCellBoundingRectS16 *rect = &((const NNSG2dCellDataWithBR *)cell)->boundingRect;
    u32 r = NNSi_G2dGetCellBoundingSphereR(cell);
    fx32 posX = mtx->_20 - view->posTopLeft.x;
    fx32 posY = mtx->_21 - view->posTopLeft.y;
    fx32 minY;
    fx32 maxY;
    fx32 minX;
    fx32 maxX;

    if (((cell->cellAttr >> NNSi_G2D_CELLATTR_BOUNDINGRECT_SHIFT) & 1) == 1) {
        minY = rect->minY << FX32_SHIFT;
        maxY = rect->maxY << FX32_SHIFT;
        minX = rect->minX << FX32_SHIFT;
        maxX = rect->maxX << FX32_SHIFT;
    } else {
        minY = -r << FX32_SHIFT;
        maxY = r << FX32_SHIFT;
        minX = minY;
        maxX = maxY;
    }
    minY += posY;
    maxY += posY;
    minX += posX;
    maxX += posX;
    if (maxY > 0 && minY < view->sizeView.y && maxX > 0 && minX < view->sizeView.x) {
        return TRUE;
    }
    return FALSE;
}

// An OAM's x or y, sign extended from its 9 or 8 bits: one past the right or bottom of the screen wraps around to the
// left or top
static inline s16 ClAct_WrapOamX(s16 x) {
    if (x > 0xff) {
        x |= 0xff00;
    }
    return x;
}

static inline s16 ClAct_WrapOamY(s16 y) {
    if (y > 0x7f) {
        y |= 0xff00;
    }
    return y;
}

// Called by the renderers before each OAM: skips one that is out of view
static void func_0204cb8c(ClActDrawState *state, const NNSG2dViewRect *view, u32 a2, u32 a3, const MtxFx32 *mtx) {
    GXOamAttr *oam = &state->oam;
    GXOamShape shape = oam->attr01 & GX_OAM_ATTR01_SHAPE_MASK;
    fx32 left = ClAct_WrapOamX(oam->x) << FX32_SHIFT;
    fx32 right = (ClAct_WrapOamX(oam->x) + NNS_G2dGetOamSizeX(&shape)) << FX32_SHIFT;
    fx32 top = ClAct_WrapOamY(oam->y) << FX32_SHIFT;
    fx32 bottom = (ClAct_WrapOamY(oam->y) + NNS_G2dGetOamSizeY(&shape)) << FX32_SHIFT;

    if (func_0204cc54(mtx->_20 - view->posTopLeft.x, mtx->_21 - view->posTopLeft.y, left, right, top, bottom, mtx,
                      view)) {
        state->skip = TRUE;
    } else {
        state->skip = FALSE;
    }
}

static inline void ClAct_UpdateMinMax(fx32 value, fx32 *min, fx32 *max) {
    if (value < *min) {
        *min = value;
    } else if (value > *max) {
        *max = value;
    }
}

// Whether a rect is in view, its corners transformed by the matrix
static BOOL func_0204cc54(fx32 posX, fx32 posY, fx32 left, fx32 right, fx32 top, fx32 bottom, const MtxFx32 *mtx,
                          const NNSG2dViewRect *view) {
    fx32 minX;
    fx32 maxX;
    fx32 minY;
    fx32 maxY;

    minX = FX_MulFunc(left, mtx->_00) + FX_MulFunc(top, mtx->_10);
    maxX = minX;
    ClAct_UpdateMinMax(FX_MulFunc(left, mtx->_00) + FX_MulFunc(bottom, mtx->_10), &minX, &maxX);
    ClAct_UpdateMinMax(FX_MulFunc(right, mtx->_00) + FX_MulFunc(top, mtx->_10), &minX, &maxX);
    ClAct_UpdateMinMax(FX_MulFunc(right, mtx->_00) + FX_MulFunc(bottom, mtx->_10), &minX, &maxX);
    minY = FX_MulFunc(left, mtx->_01) + FX_MulFunc(top, mtx->_11);
    maxY = minY;
    ClAct_UpdateMinMax(FX_MulFunc(left, mtx->_01) + FX_MulFunc(bottom, mtx->_11), &minY, &maxY);
    ClAct_UpdateMinMax(FX_MulFunc(right, mtx->_01) + FX_MulFunc(top, mtx->_11), &minY, &maxY);
    ClAct_UpdateMinMax(FX_MulFunc(right, mtx->_01) + FX_MulFunc(bottom, mtx->_11), &minY, &maxY);
    minX += posX;
    maxX += posX;
    minY += posY;
    maxY += posY;
    if (maxY > 0 && minY < view->sizeView.y && maxX > 0 && minX < view->sizeView.x) {
        return TRUE;
    }
    return FALSE;
}

static void func_0204cda4(ClActTransfer *transfer, u8 count, HeapID heapId) {
    if (count == 0) {
        transfer->active = FALSE;
        return;
    }
    transfer->count = count;
    transfer->states = GFL_HeapAllocate(heapId, count * sizeof(NNSG2dCellTransferState), FALSE, "clact.c", 4976);
    NNS_G2dInitCellTransferStateManager(transfer->states, count, func_0204cec8);
    func_0204ce18(transfer, count, heapId);
    transfer->active = TRUE;
}

static void func_0204cdf8(ClActTransfer *transfer) {
    if (transfer->active) {
        GFL_HeapFree(transfer->states);
        transfer->states = NULL;
        func_0204ce5c(transfer);
        transfer->active = FALSE;
    }
}

static void func_0204ce18(ClActTransfer *transfer, u32 count, HeapID heapId) {
    u32 i = 0;

    transfer->tasks = GFL_HeapAllocate(heapId, count * sizeof(ClActTransferTask), FALSE, "clact.c", 5044);
    for (; i < transfer->count; i++) {
        MI_CpuFill8(&transfer->tasks[i], 0, sizeof(ClActTransferTask));
    }
}

static void func_0204ce5c(ClActTransfer *transfer) {
    GFL_HeapFree(transfer->tasks);
    transfer->tasks = NULL;
}

static void func_0204ce6c(ClActTransfer *transfer) {
    u32 i;

    for (i = 0; i < transfer->count; i++) {
        if (transfer->tasks[i].src != NULL) {
            func_0204cf14(&transfer->tasks[i]);
            MI_CpuFill8(&transfer->tasks[i], 0, sizeof(ClActTransferTask));
        }
    }
}

static ClActTransferTask *func_0204cea0(ClActTransfer *transfer) {
    u32 i;

    for (i = 0; i < transfer->count; i++) {
        if (transfer->tasks[i].src == NULL) {
            return &transfer->tasks[i];
        }
    }
    return NULL;
}

static BOOL func_0204cec8(NNS_GFD_DST_TYPE type, u32 dstAddr, void *src, u32 size) {
    ClActTransferTask *task = func_0204cea0(&g_ClActSys->transfer);
    u32 screen;

    if (type == NNS_GFD_DST_2D_OBJ_CHAR_MAIN) {
        screen = 0;
    } else if (type == NNS_GFD_DST_2D_OBJ_CHAR_SUB) {
        screen = 1;
    }
    if (size != 0) {
        func_0204cf08(task, screen, dstAddr, src, size);
    }
    return TRUE;
}

static void func_0204cf08(ClActTransferTask *task, u32 screen, u32 dst, void *src, u32 size) {
    task->screen = screen;
    task->dst = dst;
    task->src = src;
    task->size = size;
}

static void func_0204cf14(ClActTransferTask *task) {
    DC_FlushRange(task->src, task->size);
    sTransferFuncs[task->screen](task->src, task->dst, task->size);
}

static ClActor *func_0204cf38(ClActUnit *unit) {
    int i;

    for (i = 0; i < unit->count; i++) {
        if (unit->actors[i].next == NULL) {
            return &unit->actors[i];
        }
    }
    GFL_ASSERT(0);
    return NULL;
}

static void func_0204cf70(ClActUnit *unit, ClActor *actor) {
    ClActor *pos;
    u8 priority;
    u8 headPriority;

    if (unit->actorList == NULL) {
        unit->actorList = actor;
        actor->next = actor;
        unit->actorList->prev = actor;
        return;
    }
    priority = func_0204c45c(actor);
    headPriority = func_0204c45c(unit->actorList);
    if (priority < headPriority) {
        pos = unit->actorList->prev;
        unit->actorList = actor;
    } else if ((u8)((headPriority + func_0204c45c(unit->actorList->prev)) / 2) >= priority) {
        pos = func_0204d000(unit->actorList, priority);
    } else {
        pos = func_0204d020(unit->actorList->prev, priority);
    }
    actor->prev = pos;
    actor->next = pos->next;
    pos->next->prev = actor;
    pos->next = actor;
}

static void func_0204cfd8(ClActUnit *unit, ClActor *actor) {
    if (unit->actorList == actor) {
        if (actor->next == actor) {
            unit->actorList = NULL;
        } else {
            unit->actorList = actor->next;
        }
    }
    actor->next->prev = actor->prev;
    actor->prev->next = actor->next;
    actor->next = NULL;
    actor->prev = NULL;
}

static ClActor *func_0204d000(ClActor *head, u8 priority) {
    ClActor *cur = head;

    do {
        if (func_0204c45c(cur) > priority) {
            return cur->prev;
        }
        cur = cur->next;
    } while (cur != head);
    return head->prev;
}

static ClActor *func_0204d020(ClActor *tail, u8 priority) {
    ClActor *cur = tail;

    do {
        if (func_0204c45c(cur) <= priority) {
            return cur;
        }
        cur = cur->prev;
    } while (cur != tail);
    return tail->next;
}

static void func_0204d040(ClActor *actor) {
    MI_CpuFill8(actor, 0, sizeof(ClActor));
    actor->unk54 = FX32_ONE;
    NNS_G2dInitImageProxy(&actor->imgProxy);
    NNS_G2dInitImagePaletteProxy(&actor->pltProxy);
    actor->scale.x = FX32_ONE;
    actor->unk60_8 |= 1;
    actor->scale.y = FX32_ONE;
    actor->unk60_31 = 0;
    actor->affineMode = 0;
}

static void func_0204d094(ClActor *actor, const ClActorSetup *setup, u16 surface) {
    func_0204c1a8(actor, setup->x, surface, 0);
    func_0204c1a8(actor, setup->y, surface, 1);
    actor->priority = setup->priority;
    func_0204c468(actor, setup->bgPriority);
    func_0204c488(actor, setup->sequence);
    func_0204c540(actor);
}

static void func_0204d0d8(ClActor *actor, const ClActResSetup *res) {
    func_0204c3e4(actor, &res->chars->proxy);
    func_0204c3a8(actor, &res->pltt->proxy);
    actor->charIdx = res->charIdx;
    actor->plttIdx = res->plttIdx;
}

static void func_0204d0fc(ClActor *actor, u32 surface, ClActorPos *offset) {
    if (surface != CLACT_SURFACE_NONE) {
        func_0204befc(actor->unit->renderer, surface, offset);
    } else {
        offset->x = 0;
        offset->y = 0;
    }
}

static void func_0204d11c(ClActResSetup *res, u16 charIdx, u16 plttIdx, ClActCharRes *chars, ClActPlttRes *pltt,
                          NNSG2dCellDataBank *cells, NNSG2dAnimBankData *anims) {
    res->charIdx = charIdx;
    res->plttIdx = plttIdx;
    res->pltt = pltt;
    res->chars = chars;
    res->cells = cells;
    res->anims = anims;
    res->mcBank = NULL;
    res->mcAnims = NULL;
    res->transferChar = NULL;
}

static ClActor *func_0204d138(ClActUnit *unit, const ClActorSetup *setup, const ClActResSetup *res, u16 surface,
                              HeapID heapId) {
    ClActor *actor = func_0204cf38(unit);

    actor->unit = unit;
    func_0204d1cc(&actor->anim, res, heapId);
    func_0204d094(actor, setup, surface);
    func_0204d0d8(actor, res);
    func_0204cf70(unit, actor);
    func_0204c124(actor, TRUE);
    return actor;
}

static ClActor *func_0204d17c(ClActUnit *unit, const ClActorSetupEx *setup, const ClActResSetup *res, u16 surface,
                              HeapID heapId) {
    ClActor *actor = func_0204d138(unit, &setup->base, res, surface, heapId);

    func_0204c264(actor, setup->affineCenter.x, 0);
    // BUG: the y of the affine center is set to its x
#ifdef BUGFIX
    func_0204c264(actor, setup->affineCenter.y, 1);
#else
    func_0204c264(actor, setup->affineCenter.x, 1);
#endif
    func_0204c288(actor, setup->scaleX, 0);
    func_0204c288(actor, setup->scaleY, 1);
    func_0204c2a0(actor, setup->rotation);
    func_0204c244(actor, setup->affineMode);
    return actor;
}

static void func_0204d1cc(ClActAnim *anim, const ClActResSetup *res, HeapID heapId) {
    anim->type = func_0204d214(res);
    sAnimInitFuncs[anim->type](anim, res, heapId);
}

static void func_0204d1f4(ClActAnim *anim) {
    sAnimFreeFuncs[anim->type](anim);
    MI_CpuFill8(anim, 0, sizeof(ClActAnim));
}

static u32 func_0204d214(const ClActResSetup *res) {
    if (res->transferChar != NULL) {
        return CLACT_ANIM_VRAM_TRANSFER;
    }
    if (res->mcBank != NULL) {
        return CLACT_ANIM_MULTICELL;
    }
    return CLACT_ANIM_CELL;
}

static void func_0204d22c(ClActAnim *anim, const ClActResSetup *res, HeapID heapId) {
    anim->cells = res->cells;
    anim->anims = res->anims;
    NNS_G2dInitCellAnimation(&anim->cellAnim, NNS_G2dGetAnimSequenceByIdx(anim->anims, 0), anim->cells);
}

static void func_0204d24c(ClActAnim *anim, const ClActResSetup *res, HeapID heapId) {
    NNSG2dCharacterData *transferChar;
    const NNSG2dAnimSequence *seq;

    anim->cells = res->cells;
    anim->anims = res->anims;
    anim->transferHandle = NNS_G2dGetNewCellTransferStateHandle();
    transferChar = res->transferChar;
    seq = NNS_G2dGetAnimSequenceByIdx(anim->anims, 0);
    NNS_G2dInitCellAnimationVramTransfered(&anim->cellAnim, seq, anim->cells, anim->transferHandle,
                                           NNS_G2D_VRAM_ADDR_NONE,
                                           NNS_G2dGetImageLocation(&res->chars->proxy, NNS_G2D_VRAM_TYPE_2DMAIN),
                                           NNS_G2dGetImageLocation(&res->chars->proxy, NNS_G2D_VRAM_TYPE_2DSUB),
                                           transferChar->pRawData, NULL, transferChar->szByte);
}

static void func_0204d2a8(ClActAnim *anim, const ClActResSetup *res, HeapID heapId) {
    anim->cells = res->cells;
    anim->anims = res->anims;
    anim->mcBank = res->mcBank;
    anim->mcAnims = res->mcAnims;
    anim->mcWork = GFL_HeapAllocate(heapId, NNS_G2dGetMCWorkAreaSize(anim->mcBank, NNS_G2D_MCTYPE_SHARE_CELLANIM),
                                    FALSE, "clact.c", 6175);
    NNS_G2dInitMCAnimationInstance(&anim->mcAnim, anim->mcWork, anim->anims, anim->cells, anim->mcBank,
                                   NNS_G2D_MCTYPE_SHARE_CELLANIM);
    NNS_G2dSetAnimSequenceToMCAnimation(&anim->mcAnim, NNS_G2dGetAnimSequenceByIdx(anim->mcAnims, 0));
}

static void func_0204d30c(ClActAnim *anim) {
    MI_CpuFill8(anim, 0, sizeof(ClActAnim));
}

static void func_0204d318(ClActAnim *anim) {
    NNS_G2dFreeCellTransferStateHandle(anim->transferHandle);
    MI_CpuFill8(anim, 0, sizeof(ClActAnim));
}

static void func_0204d330(ClActAnim *anim) {
    GFL_HeapFree(anim->mcWork);
    MI_CpuFill8(anim, 0, sizeof(ClActAnim));
}

static void func_0204d348(ClActAnim *anim, u16 sequence) {
    sAnimSetSequenceFuncs[anim->type](anim, sequence);
}

static void func_0204d35c(ClActAnim *anim, u16 sequence) {
    NNS_G2dSetCellAnimationSequence(&anim->cellAnim, NNS_G2dGetAnimSequenceByIdx(anim->anims, sequence));
}

static void func_0204d374(ClActAnim *anim, u16 sequence) {
    NNS_G2dSetCellAnimationSequence(&anim->cellAnim, NNS_G2dGetAnimSequenceByIdx(anim->anims, sequence));
}

static void setSeqToMultiCellAnim(ClActAnim *anim, u16 sequence) {
    NNS_G2dSetAnimSequenceToMCAnimation(&anim->mcAnim, NNS_G2dGetAnimSequenceByIdx(anim->mcAnims, sequence));
}

static void func_0204d3a4(ClActAnim *anim, fx32 time) {
    func_0204d3f4(anim)->currentTime = time;
    func_0204d3bc(anim, 0);
}

void func_0204d3bc(ClActAnim *anim, fx32 frames) {
    sAnimTickFuncs[anim->type](anim, frames);
}

static void func_0204d3d0(ClActAnim *anim, fx32 frames) {
    NNS_G2dTickCellAnimation(&anim->cellAnim, frames);
}

static void func_0204d3dc(ClActAnim *anim, fx32 frames) {
    NNS_G2dTickCellAnimation(&anim->cellAnim, frames);
}

static void func_0204d3e8(ClActAnim *anim, fx32 frames) {
    NNS_G2dTickMCAnimation(&anim->mcAnim, frames);
}

static NNSG2dAnimController *func_0204d3f4(ClActAnim *anim) {
    return sAnimGetCtrlFuncs2[anim->type](anim);
}

static NNSG2dAnimController *func_0204d408(ClActAnim *anim) {
    return &anim->cellAnim.animCtrl;
}

static NNSG2dAnimController *func_0204d40c(ClActAnim *anim) {
    return &anim->cellAnim.animCtrl;
}

static NNSG2dAnimController *func_0204d410(ClActAnim *anim) {
    return &anim->mcAnim.animCtrl;
}

static NNSG2dAnimController *func_0204d414(ClActAnim *anim) {
    return sAnimGetCtrlFuncs[anim->type](anim);
}

static NNSG2dAnimController *func_0204d428(ClActAnim *anim) {
    return &anim->cellAnim.animCtrl;
}

static NNSG2dAnimController *func_0204d42c(ClActAnim *anim) {
    return &anim->cellAnim.animCtrl;
}

static NNSG2dAnimController *func_0204d430(ClActAnim *anim) {
    return &anim->mcAnim.animCtrl;
}

static void func_0204d434(ClActAnim *anim, u16 frame) {
    sAnimSetFrameFuncs[anim->type](anim, frame);
}

static void func_0204d448(ClActAnim *anim, u16 frame) {
    NNS_G2dSetCellAnimationCurrentFrame(&anim->cellAnim, frame);
}

static void func_0204d454(ClActAnim *anim, u16 frame) {
    NNS_G2dSetCellAnimationCurrentFrame(&anim->cellAnim, frame);
}

static void func_0204d460(ClActAnim *anim, u16 frame) {
    NNS_G2dSetMCAnimationCurrentFrame(&anim->mcAnim, frame);
}

void func_0204d46c(ClActAnim *anim, fx32 speed) {
    sAnimSetSpeedFuncs[anim->type](anim, speed);
}

static void func_0204d480(ClActAnim *anim, fx32 speed) {
    NNS_G2dSetCellAnimationSpeed(&anim->cellAnim, speed);
}

static void func_0204d48c(ClActAnim *anim, fx32 speed) {
    NNS_G2dSetCellAnimationSpeed(&anim->cellAnim, speed);
}

static void func_0204d498(ClActAnim *anim, fx32 speed) {
    NNS_G2dSetMCAnimationSpeed(&anim->mcAnim, speed);
}

static void func_0204d4a4(ClActAnim *anim) {
    sAnimRestartFuncs[anim->type](anim);
}

static void func_0204d4b8(ClActAnim *anim) {
    NNS_G2dRestartCellAnimation(&anim->cellAnim);
}

static void func_0204d4c4(ClActAnim *anim) {
    NNS_G2dRestartCellAnimation(&anim->cellAnim);
}

static void func_0204d4d0(ClActAnim *anim) {
    func_0204d4b8(anim);
    NNS_G2dRestartMCCellAnimations(&anim->mcAnim.multiCellInstance);
}

static const NNSG2dUserExAnimSequenceAttr *func_0204d4e4(ClActAnim *anim, u32 sequence) {
    const NNSG2dUserExAnimAttrBank *bank = NNS_G2dGetUserExAnimAttrBank(anim->anims);

    if (bank != NULL) {
        return NNS_G2dGetUserExAnimSequenceAttr(bank, sequence);
    }
    return NULL;
}

static const NNSG2dUserExAnimSequenceAttr *func_0204d524(ClActAnim *anim, u32 sequence) {
    const NNSG2dUserExAnimAttrBank *bank = NNS_G2dGetUserExAnimAttrBank(anim->anims);

    if (bank != NULL) {
        return NNS_G2dGetUserExAnimSequenceAttr(bank, sequence);
    }
    return NULL;
}

static const NNSG2dUserExAnimSequenceAttr *func_0204d564(ClActAnim *anim, u32 sequence) {
    const NNSG2dUserExAnimAttrBank *bank = NNS_G2dGetUserExAnimAttrBank(anim->mcAnims);

    if (bank != NULL) {
        return NNS_G2dGetUserExAnimSequenceAttr(bank, sequence);
    }
    return NULL;
}

static const NNSG2dUserExCellAttrBank *func_0204d5a4(ClActAnim *anim) {
    return NNS_G2dGetUserExCellAttrBankFromCellBank(anim->cells);
}

static const NNSG2dUserExCellAttrBank *func_0204d5c8(ClActAnim *anim) {
    return NNS_G2dGetUserExCellAttrBankFromCellBank(anim->cells);
}

static const NNSG2dUserExCellAttrBank *func_0204d5ec(ClActAnim *anim) {
    return NNS_G2dGetUserExCellAttrBankFromMCBank(anim->mcBank);
}

static u32 func_0204d610(ClActAnim *anim) {
    return sAnimGetSequenceCountFuncs[anim->type](anim);
}

static u32 func_0204d624(ClActAnim *anim) {
    return anim->anims->numSequences;
}

static u32 func_0204d62c(ClActAnim *anim) {
    return anim->anims->numSequences;
}

static u32 func_0204d634(ClActAnim *anim) {
    return anim->mcAnims->numSequences;
}

static void func_0204d63c(ClActAnim *anim, const ClActorCallback *callback) {
    NNSG2dAnimController *animCtrl = func_0204d3f4(anim);

    switch (callback->type) {
    case CLACT_CALLBACK_LAST_FRAME:
        NNS_G2dSetAnimCtrlCallBackFunctor(animCtrl, NNS_G2D_ANMCALLBACKTYPE_LAST_FRM, callback->param, callback->func);
        break;
    case CLACT_CALLBACK_EVERY_FRAME:
        NNS_G2dSetAnimCtrlCallBackFunctor(animCtrl, NNS_G2D_ANMCALLBACKTYPE_EVER_FRM, callback->param, callback->func);
        break;
    case CLACT_CALLBACK_FRAME:
        NNS_G2dSetAnimCtrlCallBackFunctorAtAnimFrame(animCtrl, callback->param, callback->func, callback->frame);
        break;
    }
}

static void func_0204d678(ClActAnim *anim) {
    NNS_G2dSetAnimCtrlCallBackFunctor(func_0204d3f4(anim), NNS_G2D_ANMCALLBACKTYPE_NONE, 0, NULL);
}

static void ClActVRAMManager_Init(ClActResMan *man, HeapID heapId, const BGSysVRAMConfig *vramConfig,
                                  const ClActResManSetup *setup) {
    man->vmanMain = GFL_VRAMManagerCreate(heapId, VMAN_TYPE_OBJ, vramConfig->objMain, setup->offsetMain << 5,
                                          vramConfig->objMappingMain);
    man->vmanSub = GFL_VRAMManagerCreate(heapId, VMAN_TYPE_OBJ, vramConfig->objSub, setup->offsetSub << 5,
                                         vramConfig->objMappingSub);
    man->chars = func_0204d810(heapId, setup->charCount);
    man->pltts = func_0204d9b0(heapId, setup->plttCount);
    man->cellAnims = func_0204dbf4(heapId, setup->cellAnimCount);
    man->charCount = setup->charCount;
    man->plttCount = setup->plttCount;
    man->cellAnimCount = setup->cellAnimCount;
    man->unk1A = setup->unk6;
    man->offsetMain = setup->offsetMain;
    man->offsetSub = setup->offsetSub;
}

static void func_0204d6f8(ClActResMan *man) {
    u32 i;

    for (i = 0; i < man->charCount; i++) {
        if (!man->chars[i].free) {
            func_0204b98c(i);
        }
    }
    for (i = 0; i < man->cellAnimCount; i++) {
        if (man->cellAnims[i].free == FALSE) {
            func_0204be64(i);
        }
    }
    for (i = 0; i < man->plttCount; i++) {
        if (!man->pltts[i].free) {
            func_0204bcd0(i);
        }
    }
    func_0204d984(man->chars);
    func_0204dbec(man->pltts);
    func_0204dc60(man->cellAnims);
    GFL_VRAMManagerFree(man->vmanSub);
    GFL_VRAMManagerFree(man->vmanMain);
    man->vmanMain = NULL;
    man->vmanSub = NULL;
    man->chars = NULL;
    man->pltts = NULL;
    man->cellAnims = NULL;
}

static BOOL func_0204d798(u32 vramType, u32 which) {
    if (vramType == CLACT_VRAM_BOTH || which == CLACT_VRAM_BOTH) {
        return TRUE;
    }
    if (vramType == which) {
        return TRUE;
    }
    return FALSE;
}

static void *func_0204d7b0(ArcTool *arc, u32 fileId, BOOL compressed, HeapID heapId) {
    void *data;

    if (compressed) {
        void *file = GFL_ArcToolReadHeapNew(arc, fileId, HEAPID_TAIL(heapId));

        data = GFL_HeapAllocate(heapId, *(u32 *)file >> 8, FALSE, "clact.c", 7335);
        MI_UncompressLZ8(file, data);
        GFL_HeapFree(file);
    } else {
        data = GFL_ArcToolReadHeapNew(arc, fileId, HEAPID_TAIL(heapId));
    }
    return data;
}

static ClActCharRes *func_0204d810(HeapID heapId, u32 count) {
    ClActCharRes *chars = GFL_HeapAllocate(heapId, count * sizeof(ClActCharRes), FALSE, "clact.c", 7360);
    u32 i = 0;

    for (; i < count; i++) {
        chars[i].free = TRUE;
        GFL_VRAMManagerAllocInit(&chars[i].allocMain);
        GFL_VRAMManagerAllocInit(&chars[i].allocSub);
    }
    return chars;
}

static void func_0204d868(ClActResMan *man, u32 idx, NNSG2dCharacterData *charData, NNSG2dCellDataBank *transferCells,
                          u32 vramType) {
    ClActCharRes *res = &man->chars[idx];
    u32 size;
    BOOL vramTransfer;

    if (transferCells == NULL) {
        size = charData->szByte;
        vramTransfer = FALSE;
    } else {
        size = ClAct_GetTransferSize(transferCells);
        vramTransfer = TRUE;
    }
    NNS_G2dInitImageProxy(&res->proxy);
    if (func_0204d798(vramType, CLACT_VRAM_MAIN) && GFL_VRAMManagerAlloc(man->vmanMain, size, &res->allocMain)) {
        // bwbattle: the main engine's characters go to the OBJ extension (BW_OBJ_EXT_CHAR_OFFSET)
        u32 addr = GFL_VRAMManagerGetAllocAddress(man->vmanMain, &res->allocMain) + BW_OBJ_EXT_CHAR_OFFSET;

        func_0204d98c(charData, NNS_G2D_VRAM_TYPE_2DMAIN);
        if (vramTransfer) {
            NNS_G2dLoadImageVramTransfer(charData, addr, NNS_G2D_VRAM_TYPE_2DMAIN, &res->proxy);
        } else if (!sBwVramWrites) {
            // bwbattle: see ClActSys_SetVramWritesEnabled
        } else if (charData->mappingType != GX_OBJVRAMMODE_CHAR_2D) {
            NNS_G2dLoadImage1DMapping(charData, addr, NNS_G2D_VRAM_TYPE_2DMAIN, &res->proxy);
        } else {
            NNS_G2dLoadImage2DMapping(charData, addr, NNS_G2D_VRAM_TYPE_2DMAIN, &res->proxy);
        }
    }
    if (func_0204d798(vramType, CLACT_VRAM_SUB) && GFL_VRAMManagerAlloc(man->vmanSub, size, &res->allocSub)) {
        u32 addr = GFL_VRAMManagerGetAllocAddress(man->vmanSub, &res->allocSub);

        func_0204d98c(charData, NNS_G2D_VRAM_TYPE_2DSUB);
        if (vramTransfer) {
            NNS_G2dLoadImageVramTransfer(charData, addr, NNS_G2D_VRAM_TYPE_2DSUB, &res->proxy);
        } else if (!sBwVramWrites) {
            // bwbattle: see ClActSys_SetVramWritesEnabled
        } else if (charData->mappingType != GX_OBJVRAMMODE_CHAR_2D) {
            NNS_G2dLoadImage1DMapping(charData, addr, NNS_G2D_VRAM_TYPE_2DSUB, &res->proxy);
        } else {
            NNS_G2dLoadImage2DMapping(charData, addr, NNS_G2D_VRAM_TYPE_2DSUB, &res->proxy);
        }
    }
    if (!vramTransfer) {
        charData = NULL;
    }
    res->transferChar = charData;
    res->size = size;
}

static void func_0204d984(ClActCharRes *chars) {
    GFL_HeapFree(chars);
}

// Makes the characters' mapping the engine's, which loading them follows
static void func_0204d98c(NNSG2dCharacterData *charData, NNSG2dVRamType type) {
    u32 mapping = type == NNS_G2D_VRAM_TYPE_2DMAIN ? GX_GetOBJVRamModeChar() : GXS_GetOBJVRamModeChar();

    if (charData->mappingType != mapping) {
        charData->mappingType = mapping;
    }
}

static ClActPlttRes *func_0204d9b0(HeapID heapId, u32 count) {
    ClActPlttRes *pltts = GFL_HeapAllocate(heapId, count * sizeof(ClActPlttRes), FALSE, "clact.c", 7514);
    u32 i = 0;

    for (; i < count; i++) {
        pltts[i].free = TRUE;
    }
    return pltts;
}

static void func_0204d9f4(ClActResMan *man, u32 idx, void *file, u32 vramType, u16 offset, u16 start, u16 count) {
    ClActPlttRes *res = &man->pltts[idx];
    u32 limit = 0x200;
    BOOL extended;
    int maxPalettes;
    NNSG2dPaletteData *pltt;
    NNSG2dPaletteCompressInfo *cmpInfo;

    if (offset < limit) {
        extended = FALSE;
        maxPalettes = 16;
    } else {
        offset -= 0x200;
        extended = TRUE;
        maxPalettes = 256;
        limit = 0x2000;
    }
    NNS_G2dGetUnpackedPaletteCompressInfo(file, &cmpInfo);
    if (NNS_G2dGetUnpackedPaletteData(file, &pltt)) {
        u32 banks;

        NNS_G2dInitImagePaletteProxy(&res->proxy);
        if (start != 0 && start * 32 < pltt->szByte) {
            pltt->pRawData = (u8 *)pltt->pRawData + start * 32;
            pltt->szByte -= start * 32;
        }
        if (count != 0 && start + count <= maxPalettes && count * 32 < pltt->szByte) {
            pltt->szByte = count * 32;
        }
        if (offset != 0 && pltt->szByte + offset > limit) {
            pltt->szByte = limit - offset;
        }
        if (func_0204d798(vramType, CLACT_VRAM_MAIN)) {
            banks = GX_GetBankForOBJExtPltt();
            if (extended) {
                GX_SetBankForLCDC(banks);
            }
            if (sBwVramWrites) {
                // bwbattle: into the OBJ extension's palettes
                NNS_G2dLoadPalette(pltt, offset + BW_OBJ_EXT_PLTT_OFFSET, NNS_G2D_VRAM_TYPE_2DMAIN, &res->proxy);
            }
            if (extended) {
                GX_SetBankForOBJExtPltt(banks);
            }
        }
        if (func_0204d798(vramType, CLACT_VRAM_SUB)) {
            banks = GX_GetBankForSubOBJExtPltt();
            if (extended) {
                GX_SetBankForLCDC(banks);
            }
            if (sBwVramWrites) {
                NNS_G2dLoadPalette(pltt, offset, NNS_G2D_VRAM_TYPE_2DSUB, &res->proxy);
            }
            if (extended) {
                GX_SetBankForSubOBJExtPltt(banks);
            }
        }
        man->pltts[idx].free = FALSE;
        man->pltts[idx].size = pltt->szByte;
    }
}

static void func_0204db2c(ClActResMan *man, u32 idx, void *file, u32 vramType, u16 offset) {
    ClActPlttRes *res = &man->pltts[idx];
    BOOL extended;
    NNSG2dPaletteData *pltt;
    NNSG2dPaletteCompressInfo *cmpInfo;

    if (offset < 0x200) {
        extended = FALSE;
    } else {
        extended = TRUE;
        offset -= 0x200;
    }
    NNS_G2dGetUnpackedPaletteCompressInfo(file, &cmpInfo);
    if (NNS_G2dGetUnpackedPaletteData(file, &pltt)) {
        NNS_G2dInitImagePaletteProxy(&res->proxy);
        if (func_0204d798(vramType, CLACT_VRAM_MAIN)) {
            u32 banks = GX_GetBankForOBJExtPltt();

            if (extended) {
                GX_SetBankForLCDC(banks);
            }
            if (sBwVramWrites) {
                NNS_G2dLoadPaletteEx(pltt, cmpInfo, offset + BW_OBJ_EXT_PLTT_OFFSET, NNS_G2D_VRAM_TYPE_2DMAIN,
                                     &res->proxy);
            }
            if (extended) {
                GX_SetBankForOBJExtPltt(banks);
            }
        }
        if (func_0204d798(vramType, CLACT_VRAM_SUB)) {
            u32 banks = GX_GetBankForSubOBJExtPltt();

            if (extended) {
                GX_SetBankForLCDC(banks);
            }
            if (sBwVramWrites) {
                NNS_G2dLoadPaletteEx(pltt, cmpInfo, offset, NNS_G2D_VRAM_TYPE_2DSUB, &res->proxy);
            }
            if (extended) {
                GX_SetBankForSubOBJExtPltt(banks);
            }
        }
        man->pltts[idx].free = FALSE;
    }
}

static void func_0204dbec(ClActPlttRes *pltts) {
    GFL_HeapFree(pltts);
}

static ClActCellAnimRes *func_0204dbf4(HeapID heapId, u32 count) {
    ClActCellAnimRes *cellAnims = GFL_HeapAllocate(heapId, count * sizeof(ClActCellAnimRes), FALSE, "clact.c", 7752);
    u32 i = 0;

    for (; i < count; i++) {
        cellAnims[i].free = TRUE;
    }
    return cellAnims;
}

static BOOL func_0204dc2c(ClActResMan *man, u32 idx, void *cellFile, void *animFile) {
    ClActCellAnimRes *res = &man->cellAnims[idx];

    if (NNS_G2dGetUnpackedCellBank(cellFile, &res->cells) && NNS_G2dGetUnpackedAnimBank(animFile, &res->anims)) {
        res->free = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void func_0204dc60(ClActCellAnimRes *cellAnims) {
    GFL_HeapFree(cellAnims);
}

static void func_0204dc68(ClActResMan *man, u32 idx, void *file, u32 vramType, NNSG2dCellDataBank *transferCells) {
    NNSG2dCharacterData *charData;

    if (NNS_G2dGetUnpackedCharacterData(file, &charData)) {
        func_0204d868(man, idx, charData, transferCells, vramType);
        man->chars[idx].free = FALSE;
    }
}
