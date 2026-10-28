#include "sprite.h"

#include <nitro.h>
#include <string.h>

#include "port/battle_timing.h"

#include "heap.h"
#include "system.h"

typedef void (*SpriteDrawFunc)(const SpriteList *, Sprite *);
typedef void (*SpriteAnimUpdateFunc)(Sprite *);

static void SpriteList_Reset(SpriteList *list);
static enum CellAnimType SpriteResourcesHeader_GetCellType(const SpriteResourcesHeader *resourceData);
static void Sprite_SetCellBank(const NNSG2dCellDataBank *cellBank, Sprite *sprite);
static void Sprite_SetCellAnimBank(const NNSG2dCellAnimBankData *cellAnimBank, Sprite *sprite);
static void Sprite_SetMultiCellBank(const NNSG2dMultiCellDataBank *multiCellBank, Sprite *sprite);
static void Sprite_SetMultiCellAnimBank(const NNSG2dMultiCellAnimBankData *multiCellAnimBank, Sprite *sprite);
static void Sprite_CreateCellAnim(Sprite *sprite, enum HeapID heapID);
static void Sprite_CreateVRamCellAnim(const SpriteResourcesHeader *resourceData, Sprite *sprite, enum HeapID heapID);
static void Sprite_CreateMultiCellAnim(Sprite *sprite, enum HeapID heapID);
static BOOL SpriteList_InitSprite(const SpriteList *list,
    const SpriteResourcesHeader *resourceData,
    Sprite *sprite,
    enum HeapID heapID);
static u32 GetPaletteIndexForProxy(const NNSG2dImagePaletteProxy *paletteProxy, u32 vramType);
static void SpriteList_DrawSprite(const SpriteList *list, Sprite *sprite);
static void SpriteList_DrawSprite_Stub(const SpriteList *list, Sprite *sprite);
static void Sprite_UpdateAnimInternal(Sprite *list);
static void Sprite_UpdateAnimInternal_Stub(Sprite *list);
static void SpriteList_Insert(SpriteList *list, Sprite *sprite);
static void SpriteList_Remove(Sprite *sprite);
static void SpriteList_InitSprites(SpriteList *list);
static Sprite *SpriteList_AllocSprite(SpriteList *list);
static BOOL SpriteList_FreeSprite(SpriteList *list, Sprite *sprite);

#ifdef SDK_PORT
// Render lists survive skipped animation callbacks; registration follows list lifetime.
static SpriteList *sBattleDrawLists;
#endif

SpriteList *SpriteList_New(const SpriteListParams *params)
{
    GF_ASSERT(params);
    GF_ASSERT(params->renderer);

    SpriteList *list = Heap_Alloc(params->heapID, sizeof(SpriteList));
    GF_ASSERT(list);

    SpriteList_Reset(list);

    list->sprites = Heap_Alloc(params->heapID, sizeof(Sprite) * params->maxElements);
    GF_ASSERT(list->sprites);
    list->capacity = params->maxElements;

    list->freeSprites = Heap_Alloc(params->heapID, sizeof(Sprite *) * params->maxElements);
    GF_ASSERT(list->freeSprites);

    SpriteList_InitSprites(list);
    Sprite_Reset(&list->sentinelData);

    list->sentinelData.prev = &list->sentinelData;
    list->sentinelData.next = &list->sentinelData;
    list->renderer = params->renderer;
    list->rawAnimData = ReadFileToHeap(params->heapID, "data/clact_default.NANR");

    NNS_G2dGetUnpackedAnimBank(list->rawAnimData, &list->defaultAnimBank);
    list->active = TRUE;
#ifdef SDK_PORT
    list->battleDrawFrame = 0;
    list->battleDrawDomain = BATTLE_TIMING_NONE;
    list->battleDrawNext = NULL;
    if (BattleTiming_IsActive() && params->heapID == HEAP_ID_BATTLE) {
        list->battleDrawNext = sBattleDrawLists;
        sBattleDrawLists = list;
    }
#endif

    return list;
}

BOOL SpriteList_Delete(SpriteList *list)
{
    if (list == NULL) {
        return FALSE;
    }

    if (list->sprites == NULL) {
        return TRUE;
    }

#ifdef SDK_PORT
    SpriteList **link = &sBattleDrawLists;
    while (*link != NULL && *link != list) {
        link = &(*link)->battleDrawNext;
    }
    if (*link == list) {
        *link = list->battleDrawNext;
    }
#endif

    SpriteList_DeleteAll(list);

    Heap_Free(list->rawAnimData);
    Heap_Free(list->freeSprites);
    Heap_Free(list->sprites);

    SpriteList_Reset(list);
    Heap_Free(list);

    list = NULL;

    return TRUE;
}

BOOL SpriteList_SetActive(SpriteList *list, u8 active)
{
    if (list == NULL) {
        return FALSE;
    }

    if (list->sprites == NULL) {
        return FALSE;
    }

    list->active = active;
#ifdef SDK_PORT
    if (!active) {
        for (Sprite *sprite = list->sentinelData.next; sprite != &list->sentinelData; sprite = sprite->next) {
            sprite->battleVisualHistory.valid = FALSE;
        }
    }
#endif

    return TRUE;
}

BOOL SpriteList_DeleteAll(SpriteList *list)
{
    if (list == NULL) {
        return FALSE;
    }

    if (list->sprites == NULL) {
        return TRUE;
    }

    Sprite *sprite = list->sentinelData.next;
    while (sprite != &list->sentinelData) {
        Sprite *next = sprite->next;
        Sprite_Delete(sprite);
        sprite = next;
    }

    return TRUE;
}

void SpriteList_Update(const SpriteList *list)
{
    static const SpriteDrawFunc sDrawFuncs[] = {
        SpriteList_DrawSprite_Stub,
        SpriteList_DrawSprite
    };
    static const SpriteAnimUpdateFunc sAnimUpdateFuncs[] = {
        Sprite_UpdateAnimInternal_Stub,
        Sprite_UpdateAnimInternal
    };

    GF_ASSERT(list);

    if (list->active == FALSE) {
        return;
    }

#ifdef SDK_PORT
    // Rendering is logically mutable even though the legacy API takes const.
    SpriteList *renderList = (SpriteList *)list;
    renderList->battleDrawFrame = BattleTiming_GetFrame();
    renderList->battleDrawDomain = BattleTiming_GetDomain();
#endif

    Sprite *sprite = list->sentinelData.next;

    while (sprite != &list->sentinelData) {
#ifdef SDK_PORT
        if (!sprite->draw || sprite->portHidden) {
            sprite->battleVisualHistory.valid = FALSE;
        }
        if (!BattleTiming_IsExtraAnimationStep()) {
            sDrawFuncs[sprite->draw && !sprite->portHidden](list, sprite);
        }
#else
        sDrawFuncs[sprite->draw](list, sprite);
#endif
#ifdef SDK_PORT
        for (int step = BattleTiming_StepCount(sprite->battleTimingDomain); step > 0; step--) {
            BattleTimingDomain previous = BattleTiming_SetDomain(sprite->battleTimingDomain);
            sAnimUpdateFuncs[sprite->animate](sprite);
            BattleTiming_SetDomain(previous);
        }
#else
        sAnimUpdateFuncs[sprite->animate](sprite);
#endif
        sprite = sprite->next;
    }
}

void SpriteList_ResetBattleDrawLists(void)
{
#ifdef SDK_PORT
    sBattleDrawLists = NULL;
#endif
}

void SpriteList_DrawSkippedBattleLists(void)
{
#ifdef SDK_PORT
    if (!BattleTiming_IsActive()) {
        return;
    }
    u32 frame = BattleTiming_GetFrame();
    for (SpriteList *list = sBattleDrawLists; list != NULL; list = list->battleDrawNext) {
        if (!list->active || list->battleDrawFrame != frame - 1
            || BattleTiming_ShouldTick(list->battleDrawDomain)) {
            continue;
        }
        // Replay only OAM/geometry submission. Never advance cell animations here.
        for (Sprite *sprite = list->sentinelData.next; sprite != &list->sentinelData; sprite = sprite->next) {
            if (sprite->draw && !sprite->portHidden) {
                SpriteList_DrawSprite(list, sprite);
            }
        }
        list->battleDrawFrame = frame;
    }
#endif
}

static void SpriteList_Reset(SpriteList *list)
{
    list->sprites = NULL;
    list->capacity = 0;
    list->freeSprites = NULL;
    list->freeSpriteHead = 0;
    list->renderer = NULL;

    Sprite_Reset(&list->sentinelData);
    list->active = FALSE;
}

void Sprite_Reset(Sprite *sprite)
{
    sprite->list = NULL;
    memset(sprite, 0, sizeof(Sprite));

    NNS_G2dInitImageProxy(&sprite->imageProxy);
    NNS_G2dInitImagePaletteProxy(&sprite->paletteProxy);

    sprite->explicitOamMode = GX_OAM_MODE_NORMAL;
}

Sprite *SpriteList_AddAffine(const AffineSpriteListTemplate *template)
{
    Sprite *sprite = SpriteList_AllocSprite(template->list);
    if (sprite == NULL) {
        return NULL;
    }

    sprite->list = template->list;
#ifdef SDK_PORT
    sprite->battleTimingDomain = BattleTiming_GetDomain();
    if (sprite->battleTimingDomain == BATTLE_TIMING_NONE
        && template->heapID == HEAP_ID_BATTLE && BattleTiming_IsActive()) {
        sprite->battleTimingDomain = BATTLE_TIMING_UI;
    }
#endif
    sprite->activeAnimID = 0;
    sprite->position = template->position;
    sprite->affineScale = template->affineScale;
    sprite->affineZRotation = template->affineZRotation;
    sprite->vramType = template->vramType;
    sprite->priority = template->priority;
    sprite->affineOverwriteMode = NNS_G2D_RND_AFFINE_OVERWRITE_NONE;
    sprite->flip = NNS_G2D_RENDERERFLIP_NONE;
    sprite->explicitMosaic = FALSE;
    sprite->explicitOamMode = GX_OAM_MODE_NORMAL;
    sprite->overwriteFlags = NNS_G2D_RND_OVERWRITE_PLTTNO_OFFS | NNS_G2D_RND_OVERWRITE_PRIORITY;

    NNS_G2dSetRndCoreAffineOverwriteMode(
        &template->list->renderer->rendererCore,
        sprite->affineOverwriteMode);
    NNS_G2dSetRndCoreFlipMode(
        &template->list->renderer->rendererCore,
        sprite->flip & NNS_G2D_RENDERERFLIP_H,
        sprite->flip & NNS_G2D_RENDERERFLIP_V);

    sprite->draw = TRUE;
    sprite->animate = FALSE;
    sprite->animSpeed = (FX32_ONE * 2);
#ifdef SDK_PORT
    sprite->portHidden = FALSE;
#endif

    if (SpriteList_InitSprite(template->list, template->resourceData, sprite, template->heapID) == FALSE) {
        Sprite_Delete(sprite);
        return NULL;
    }

    sprite->explicitPaletteOffset = GetPaletteIndexForProxy(&sprite->paletteProxy, sprite->vramType);
    sprite->explicitPalette = sprite->explicitPaletteOffset;

    SpriteList_Insert(template->list, sprite);

    return sprite;
}

Sprite *SpriteList_Add(const SpriteListTemplate *template)
{
    AffineSpriteListTemplate affineTemplate;

    affineTemplate.list = template->list;
    affineTemplate.resourceData = template->resourceData;
    affineTemplate.position = template->position;
    affineTemplate.affineScale.x = FX32_ONE;
    affineTemplate.affineScale.y = FX32_ONE;
    affineTemplate.affineScale.z = FX32_ONE;
    affineTemplate.affineZRotation = 0;
    affineTemplate.priority = template->priority;
    affineTemplate.vramType = template->vramType;
    affineTemplate.heapID = template->heapID;

    return SpriteList_AddAffine(&affineTemplate);
}

void Sprite_Delete(Sprite *sprite)
{
    if (sprite->type == CELL_ANIM_TYPE_NONE) {
        return;
    }

    if (sprite->prev != NULL) {
        SpriteList_Remove(sprite);
    }

    if (sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        VRamCellAnimationData *vramCellAnim = (VRamCellAnimationData *)&sprite->animData;

        if (NNS_G2dGetImageLocation(&sprite->imageProxy, sprite->vramType) != NNS_G2D_VRAM_ADDR_NONE) {
            NNS_G2dFreeCellTransferStateHandle(vramCellAnim->transferHandle);
        }
    }

    if (sprite->type == CELL_ANIM_TYPE_MULTI_CELL) {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;

        if (multiCellAnim->nodes != NULL) {
            Heap_Free(multiCellAnim->nodes);
        }

        if (multiCellAnim->cellAnims != NULL) {
            Heap_Free(multiCellAnim->cellAnims);
        }
    }

    sprite->type = CELL_ANIM_TYPE_NONE;

    SpriteList_FreeSprite(sprite->list, sprite);
}

void Sprite_SetPosition(Sprite *sprite, const VecFx32 *position)
{
    sprite->position = *position;
}

void Sprite_SetAffineTranslation(Sprite *sprite, const VecFx32 *translation)
{
    sprite->affineTranslation = *translation;
}

void Sprite_SetAffineScale(Sprite *sprite, const VecFx32 *scale)
{
    sprite->affineScale = *scale;
}

void Sprite_SetAffineScaleEx(Sprite *sprite, const VecFx32 *scale, enum AffineOverwriteMode mode)
{
    Sprite_SetAffineScale(sprite, scale);
    Sprite_SetAffineOverwriteMode(sprite, mode);
}

void Sprite_SetAffineZRotation(Sprite *sprite, u16 angle)
{
    sprite->affineZRotation = angle;
}

void Sprite_SetAffineZRotationEx(Sprite *sprite, u16 angle, enum AffineOverwriteMode mode)
{
    Sprite_SetAffineZRotation(sprite, angle);
    Sprite_SetAffineOverwriteMode(sprite, mode);
}

void Sprite_SetDrawFlag(Sprite *sprite, BOOL draw)
{
    GF_ASSERT(sprite);
    GF_ASSERT(draw < 2); // This value is used as an index into an array of size 2 inside SpriteList_Update

    sprite->draw = draw;
#ifdef SDK_PORT
    if (!draw) {
        sprite->battleVisualHistory.valid = FALSE;
    }
#endif
}

void Sprite_SetAnimateFlag(Sprite *sprite, BOOL animate)
{
    GF_ASSERT(sprite);
    GF_ASSERT(animate < 2); // This value is used as an index into an array of size 2 inside SpriteList_Update

    sprite->animate = animate;
}

void Sprite_SetAnimSpeed(Sprite *sprite, fx32 speed)
{
    GF_ASSERT(sprite);
    sprite->animSpeed = speed;
}

void Sprite_SetAffineOverwriteMode(Sprite *sprite, enum AffineOverwriteMode mode)
{
    GF_ASSERT(sprite);
    sprite->affineOverwriteMode = mode;
}

void Sprite_SetFlipMode(Sprite *sprite, u32 mode)
{
    GF_ASSERT(sprite);
    sprite->flip = mode;
    sprite->affineOverwriteMode = NNS_G2D_RND_AFFINE_OVERWRITE_NONE;
}

const VecFx32 *Sprite_GetPosition(const Sprite *sprite)
{
    return &sprite->position;
}

const VecFx32 *Sprite_GetAffineScale(const Sprite *sprite)
{
    return &sprite->affineScale;
}

u16 Sprite_GetAffineZRotation(const Sprite *sprite)
{
    return sprite->affineZRotation;
}

BOOL Sprite_GetDrawFlag(const Sprite *sprite)
{
    return sprite->draw;
}

BOOL Sprite_GetAnimateFlag(const Sprite *sprite)
{
    return sprite->animate;
}

u32 Sprite_GetAnimCount(const Sprite *sprite)
{
    GF_ASSERT(sprite);

    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
        return cellAnim->animBank->numSequences;
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
        return multiCellAnim->animBank->numSequences;
    }
}

void Sprite_SetAnim(Sprite *sprite, u32 animID)
{
    GF_ASSERT(Sprite_GetAnimCount(sprite) > animID);
    sprite->activeAnimID = animID;

    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;

        const NNSG2dAnimSequence *animSequence = NNS_G2dGetAnimSequenceByIdx(cellAnim->animBank, animID);
        NNS_G2dSetCellAnimationSequence(&cellAnim->anim, animSequence);
        NNS_G2dStartAnimCtrl(&cellAnim->anim.animCtrl);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;

        const NNSG2dAnimSequence *animSequence = NNS_G2dGetAnimSequenceByIdx(multiCellAnim->animBank, animID);
        NNS_G2dSetAnimSequenceToMCAnimation(&multiCellAnim->anim, animSequence);
        NNS_G2dStartAnimCtrl(&multiCellAnim->anim.animCtrl);
    }
}

void Sprite_SetAnimNoRestart(Sprite *sprite, u32 animID)
{
    if (sprite->activeAnimID != animID) {
        Sprite_SetAnim(sprite, animID);
    }
}

void Sprite_RestartAnim(Sprite *sprite)
{
    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;

        NNS_G2dResetAnimCtrlState(&cellAnim->anim.animCtrl);
        NNS_G2dStartAnimCtrl(&cellAnim->anim.animCtrl);
        Sprite_SetAnimFrame(sprite, 0);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;

        NNS_G2dResetAnimCtrlState(&multiCellAnim->anim.animCtrl);
        NNS_G2dStartAnimCtrl(&multiCellAnim->anim.animCtrl);
        Sprite_SetAnimFrame(sprite, 0);
    }
}

u32 Sprite_GetActiveAnim(const Sprite *sprite)
{
    return sprite->activeAnimID;
}

void Sprite_UpdateAnim(Sprite *sprite, fx32 frames)
{
    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
        NNS_G2dTickCellAnimation(&cellAnim->anim, frames);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
        NNS_G2dTickMCAnimation(&multiCellAnim->anim, frames);
    }
}

void Sprite_SetAnimFrame(Sprite *sprite, u16 frame)
{
    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
        NNS_G2dSetCellAnimationCurrentFrame(&cellAnim->anim, frame);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
        NNS_G2dSetMCAnimationCurrentFrame(&multiCellAnim->anim, frame);
    }
}

u16 Sprite_GetAnimFrame(const Sprite *sprite)
{
    NNSG2dAnimController *controller;

    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
        controller = NNS_G2dGetCellAnimationAnimCtrl(&cellAnim->anim);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
        controller = NNS_G2dGetMCAnimAnimCtrl(&multiCellAnim->anim);
    }

    return NNS_G2dGetAnimCtrlCurrentFrame(controller);
}

void Sprite_SetExplicitPriority(Sprite *sprite, u8 priority)
{
    sprite->explicitPriority = priority;
}

u8 Sprite_GetExplicitPriority(const Sprite *sprite)
{
    return sprite->explicitPriority;
}

void Sprite_SetExplicitPalette(Sprite *sprite, u32 palette)
{
    GF_ASSERT(sprite);

    sprite->explicitPalette = palette;
    sprite->overwriteFlags |= NNS_G2D_RND_OVERWRITE_PLTTNO;
    sprite->overwriteFlags &= ~NNS_G2D_RND_OVERWRITE_PLTTNO_OFFS;
}

void Sprite_SetExplicitPaletteWithOffset(Sprite *sprite, u32 palette)
{
    Sprite_SetExplicitPalette(sprite, palette);
    sprite->explicitPalette += GetPaletteIndexForProxy(&sprite->paletteProxy, sprite->vramType);
}

u32 Sprite_GetExplicitPalette(const Sprite *sprite)
{
    return sprite->explicitPalette;
}

void Sprite_SetExplicitPaletteOffset(Sprite *sprite, u32 paletteOffset)
{
    GF_ASSERT(sprite);

    sprite->explicitPaletteOffset = paletteOffset;
    sprite->overwriteFlags |= NNS_G2D_RND_OVERWRITE_PLTTNO_OFFS;
    sprite->overwriteFlags &= ~NNS_G2D_RND_OVERWRITE_PLTTNO;
}

void Sprite_SetExplicitPaletteOffsetAutoAdjust(Sprite *sprite, u32 paletteOffset)
{
    Sprite_SetExplicitPaletteOffset(sprite, paletteOffset);
    sprite->explicitPaletteOffset += GetPaletteIndexForProxy(&sprite->paletteProxy, sprite->vramType);
}

u32 Sprite_GetExplicitPaletteOffset(const Sprite *sprite)
{
    GF_ASSERT(sprite);
    return sprite->explicitPaletteOffset;
}

void Sprite_SetPriority(Sprite *sprite, u32 priority)
{
    SpriteList *list = sprite->list; // Required to match
    sprite->priority = priority;

    SpriteList_Remove(sprite);
    SpriteList_Insert(list, sprite);
}

u32 Sprite_GetPriority(const Sprite *sprite)
{
    return sprite->priority;
}

void Sprite_SetImageProxy(Sprite *sprite, const NNSG2dImageProxy *imageProxy)
{
    sprite->imageProxy = *imageProxy;
}

NNSG2dImageProxy *Sprite_GetImageProxy(Sprite *sprite)
{
    return &sprite->imageProxy;
}

NNSG2dImagePaletteProxy *Sprite_GetPaletteProxy(Sprite *paletteProxy)
{
    return &paletteProxy->paletteProxy;
}

void Sprite_SetMosaicFlag(Sprite *sprite, BOOL mosaic)
{
    sprite->explicitMosaic = mosaic;

    if (mosaic == TRUE) {
        sprite->overwriteFlags |= NNS_G2D_RND_OVERWRITE_MOSAIC;
    } else {
        sprite->overwriteFlags ^= NNS_G2D_RND_OVERWRITE_MOSAIC;
    }
}

NNS_G2D_VRAM_TYPE Sprite_GetVRamType(const Sprite *sprite)
{
    return sprite->vramType;
}

BOOL Sprite_IsAnimated(Sprite *sprite)
{
    GF_ASSERT(sprite);

    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
        return NNS_G2dIsAnimCtrlActive(&cellAnim->anim.animCtrl);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
        return NNS_G2dIsAnimCtrlActive(&multiCellAnim->anim.animCtrl);
    }
}

void Sprite_SetExplicitOAMMode(Sprite *sprite, GXOamMode mode)
{
    GF_ASSERT(sprite);

    sprite->explicitOamMode = mode;

    if (mode == GX_OAM_MODE_NORMAL) {
        sprite->overwriteFlags ^= NNS_G2D_RND_OVERWRITE_OBJMODE;
    } else {
        sprite->overwriteFlags |= NNS_G2D_RND_OVERWRITE_OBJMODE;
    }
}

void Utility_Clear2DMainOAM(enum HeapID heapID)
{
    GXOamAttr *oam = Heap_Alloc(heapID, sizeof(GXOamAttr) * MAX_SPRITES);

    MI_CpuFill16(oam, 0x2C0, sizeof(GXOamAttr) * MAX_SPRITES);
    DC_FlushRange(oam, sizeof(GXOamAttr) * MAX_SPRITES);
    GX_LoadOAM(oam, 0, sizeof(GXOamAttr) * MAX_SPRITES);

    Heap_Free(oam);
}

void Utility_Clear2DSubOAM(enum HeapID heapID)
{
    GXOamAttr *oam = Heap_Alloc(heapID, sizeof(GXOamAttr) * MAX_SPRITES);

    MI_CpuFill16(oam, 0x2C0, sizeof(GXOamAttr) * MAX_SPRITES);
    // According to the NitroSDK docs there should be a call to DC_FlushRange here.
    GXS_LoadOAM(oam, 0, sizeof(GXOamAttr) * MAX_SPRITES);

    Heap_Free(oam);
}

u32 Sprite_GetUserAttrForAnimFrame(const Sprite *sprite, u32 animID, u32 frame)
{
    const NNSG2dAnimBankData *animBank;

    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
        animBank = cellAnim->animBank;
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
        animBank = multiCellAnim->animBank;
    }

    const NNSG2dUserExAnimAttrBank *animAttrBank = NNS_G2dGetUserExAnimAttrBank(animBank);

    // Guard clauses don't match here so the nested if's are required
    if (animAttrBank) {
        const NNSG2dUserExAnimSequenceAttr *animAttr = NNS_G2dGetUserExAnimSequenceAttr(animAttrBank, animID);

        if (animAttr) {
            const NNSG2dUserExAnimFrameAttr *frameAttr = NNS_G2dGetUserExAnimFrameAttr(animAttr, frame);

            if (frameAttr) {
                return NNS_G2dGetUserExAnimFrmAttrValue(frameAttr);
            }
        }
    }

    return 0;
}

u32 Sprite_GetUserAttrForCurrentAnimFrame(const Sprite *sprite)
{
    u32 animID = Sprite_GetActiveAnim(sprite);
    u32 frame = Sprite_GetAnimFrame(sprite);

    return Sprite_GetUserAttrForAnimFrame(sprite, animID, frame);
}

static BOOL SpriteList_InitSprite(const SpriteList *list,
    const SpriteResourcesHeader *resourceData,
    Sprite *sprite,
    enum HeapID heapID)
{
    sprite->type = SpriteResourcesHeader_GetCellType(resourceData);
    sprite->imageProxy = *resourceData->imageProxy;
    sprite->paletteProxy = *resourceData->paletteProxy;

    Sprite_SetCellBank(resourceData->cellBank, sprite);

    if (resourceData->cellAnimBank) {
        Sprite_SetCellAnimBank(resourceData->cellAnimBank, sprite);
    } else {
        Sprite_SetCellAnimBank(list->defaultAnimBank, sprite);
    }

    if (sprite->type == CELL_ANIM_TYPE_MULTI_CELL) {
        Sprite_SetMultiCellBank(resourceData->multiCellBank, sprite);
        Sprite_SetMultiCellAnimBank(resourceData->multiCellAnimBank, sprite);
        Sprite_CreateMultiCellAnim(sprite, heapID);
    } else if (sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        Sprite_CreateVRamCellAnim(resourceData, sprite, heapID);
    } else {
        Sprite_CreateCellAnim(sprite, heapID);
    }

    sprite->explicitPriority = resourceData->priority;

    return TRUE;
}

static enum CellAnimType SpriteResourcesHeader_GetCellType(const SpriteResourcesHeader *resourceData)
{
    if (resourceData->multiCellBank != NULL) {
        return CELL_ANIM_TYPE_MULTI_CELL;
    }

    if (resourceData->isVRamTransfer == TRUE) {
        return CELL_ANIM_TYPE_VRAM_CELL;
    }

    return CELL_ANIM_TYPE_CELL;
}

static void Sprite_SetCellBank(const NNSG2dCellDataBank *cellBank, Sprite *sprite)
{
    CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
    cellAnim->cellBank = cellBank;
}

static void Sprite_SetCellAnimBank(const NNSG2dCellAnimBankData *cellAnimBank, Sprite *sprite)
{
    CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
    cellAnim->animBank = cellAnimBank;
}

static void Sprite_SetMultiCellBank(const NNSG2dMultiCellDataBank *multiCellBank, Sprite *sprite)
{
    MultiCellAnimationData *multiCell = (MultiCellAnimationData *)&sprite->animData;
    multiCell->cellBank = multiCellBank;
}

static void Sprite_SetMultiCellAnimBank(const NNSG2dMultiCellAnimBankData *multiCellAnimBank, Sprite *sprite)
{
    MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
    multiCellAnim->animBank = multiCellAnimBank;
}

static void Sprite_CreateCellAnim(Sprite *sprite, enum HeapID heapID)
{
    CellAnimationData *cellAnim = (CellAnimationData *)&sprite->animData;
    NNS_G2dInitCellAnimation(
        &cellAnim->anim,
        NNS_G2dGetAnimSequenceByIdx(cellAnim->animBank, 0),
        cellAnim->cellBank);
}

static void Sprite_CreateVRamCellAnim(const SpriteResourcesHeader *resourceData, Sprite *sprite, enum HeapID heapID)
{
    VRamCellAnimationData *vramCellAnim = (VRamCellAnimationData *)&sprite->animData;
    vramCellAnim->transferHandle = NNS_G2dGetNewCellTransferStateHandle();
    const NNSG2dCharacterData *charData = resourceData->charData;

    NNS_G2dInitCellAnimationVramTransfered(
        &vramCellAnim->anim,
        NNS_G2dGetAnimSequenceByIdx(vramCellAnim->animBank, 0),
        vramCellAnim->cellBank,
        vramCellAnim->transferHandle,
        NNS_G2D_VRAM_ADDR_NONE,
        NNS_G2dGetImageLocation(&sprite->imageProxy, NNS_G2D_VRAM_TYPE_2DMAIN),
        NNS_G2dGetImageLocation(&sprite->imageProxy, NNS_G2D_VRAM_TYPE_2DSUB),
        charData->pRawData,
        NULL,
        charData->szByte);
}

static void Sprite_CreateMultiCellAnim(Sprite *sprite, enum HeapID heapID)
{
    MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&sprite->animData;
    const NNSG2dMultiCellAnimSequence *animSequence = NNS_G2dGetAnimSequenceByIdx(multiCellAnim->animBank, 0);
    u16 maxNodes = NNS_G2dGetMCBankNumNodesRequired(multiCellAnim->cellBank);
    multiCellAnim->nodes = Heap_Alloc(heapID, sizeof(NNSG2dNode) * maxNodes);
    multiCellAnim->cellAnims = Heap_Alloc(heapID, sizeof(NNSG2dCellAnimation) * maxNodes);

    NNS_G2dInitMCAnimation(
        &multiCellAnim->anim,
        multiCellAnim->nodes,
        multiCellAnim->cellAnims,
        maxNodes,
        multiCellAnim->individualAnimBank,
        multiCellAnim->individualCellBank,
        multiCellAnim->cellBank);

    NNS_G2dSetAnimSequenceToMCAnimation(
        &multiCellAnim->anim,
        animSequence);
}

static u32 GetPaletteIndexForProxy(const NNSG2dImagePaletteProxy *paletteProxy, u32 vramType)
{
    u32 paletteSize;

    if (paletteProxy->bExtendedPlt) {
        paletteSize = 32 * 16;
    } else {
        if (paletteProxy->fmt == GX_TEXFMT_PLTT256) {
            paletteSize = 0;
        } else {
            paletteSize = 32;
        }
    }

    return paletteSize != 0
        ? NNS_G2dGetImagePaletteLocation(paletteProxy, vramType) / paletteSize
        : 0;
}

void Sprite_SetBattleSmoothingEnabled(Sprite *sprite, BOOL enabled)
{
#ifdef SDK_PORT
    if (sprite->battleSmoothDisabled != !enabled) {
        sprite->battleVisualHistory.valid = FALSE;
    }
    sprite->battleSmoothDisabled = !enabled;
#endif
}

static void SpriteList_DrawSprite(const SpriteList *list, Sprite *sprite)
{
    const Sprite *assetSprite = sprite;
#ifdef SDK_PORT
    // Draw a copy: even G2D renderer callbacks cannot observe interpolated game state.
    Sprite rendered = *sprite;
    BattleVisualPose pose = {0}, renderPose;
    pose.position[0] = sprite->position.x;
    pose.position[1] = sprite->position.y;
    pose.position[2] = sprite->position.z;
    pose.offset[0] = sprite->affineTranslation.x;
    pose.offset[1] = sprite->affineTranslation.y;
    pose.offset[2] = sprite->affineTranslation.z;
    pose.scale[0] = sprite->affineScale.x;
    pose.scale[1] = sprite->affineScale.y;
    pose.scale[2] = sprite->affineScale.z;
    pose.rotation[2] = sprite->affineZRotation;
    u64 identity = sprite->activeAnimID | ((u64)sprite->type << 16)
        | ((u64)sprite->flip << 24) | ((u64)sprite->affineOverwriteMode << 32)
        | ((u64)sprite->vramType << 40);
    BattleSmoothing_Sample(&sprite->battleVisualHistory, &pose, identity, sprite->draw,
        sprite->battleTimingDomain == BATTLE_TIMING_ANIMATION && !sprite->battleSmoothDisabled, &renderPose);
    rendered.position = (VecFx32){renderPose.position[0], renderPose.position[1], renderPose.position[2]};
    rendered.affineTranslation = (VecFx32){renderPose.offset[0], renderPose.offset[1], renderPose.offset[2]};
    rendered.affineScale = (VecFx32){renderPose.scale[0], renderPose.scale[1], renderPose.scale[2]};
    rendered.affineZRotation = renderPose.rotation[2];
    sprite = &rendered;
#endif
    VecFx32 pos = sprite->position;

    NNS_G2dSetRendererImageProxy(list->renderer, &assetSprite->imageProxy, &assetSprite->paletteProxy);
    NNS_G2dBeginRendering(list->renderer);
    NNS_G2dPushMtx();

    NNS_G2dSetRndCoreAffineOverwriteMode(&list->renderer->rendererCore, sprite->affineOverwriteMode);

    if (sprite->affineOverwriteMode == NNS_G2D_RND_AFFINE_OVERWRITE_NONE) {
        NNS_G2dSetRndCoreFlipMode(
            &list->renderer->rendererCore,
            sprite->flip & NNS_G2D_RENDERERFLIP_H,
            sprite->flip & NNS_G2D_RENDERERFLIP_V);
    } else {
        NNS_G2dSetRndCoreFlipMode(&list->renderer->rendererCore, FALSE, FALSE);
    }

    NNS_G2dTranslate(pos.x, pos.y, pos.z);

    if (sprite->affineOverwriteMode != NNS_G2D_RND_AFFINE_OVERWRITE_NONE) {
        NNS_G2dTranslate(sprite->affineTranslation.x, sprite->affineTranslation.y, sprite->affineTranslation.z);
        NNS_G2dScale(sprite->affineScale.x, sprite->affineScale.y, sprite->affineScale.z);
        NNS_G2dRotZ(FX_SinIdx(sprite->affineZRotation), FX_CosIdx(sprite->affineZRotation));

        // affineTranslation only serves as a pivot point for rotation and scaling
        // so we undo this translation after applying these transformations.
        NNS_G2dTranslate(-sprite->affineTranslation.x, -sprite->affineTranslation.y, -sprite->affineTranslation.z);
    }

    // Set the overwrite parameters
    // We always want the actual overwrite flags to be equal to overwriteParam
    // so we set the flags that are not in overwriteParam to 0 with the second call
    NNS_G2dSetRendererOverwriteEnable(list->renderer, sprite->overwriteFlags);
    NNS_G2dSetRendererOverwriteDisable(list->renderer, ~sprite->overwriteFlags);

    NNS_G2dSetRendererOverwritePlttNo(list->renderer, sprite->explicitPalette);
    NNS_G2dSetRendererOverwritePlttNoOffset(list->renderer, sprite->explicitPaletteOffset);
    NNS_G2dSetRendererOverwriteMosaicFlag(list->renderer, sprite->explicitMosaic);
    NNS_G2dSetRendererOverwriteOBJMode(list->renderer, sprite->explicitOamMode);
    NNS_G2dSetRendererOverwritePriority(list->renderer, sprite->explicitPriority);

    if (sprite->type == CELL_ANIM_TYPE_CELL || sprite->type == CELL_ANIM_TYPE_VRAM_CELL) {
        CellAnimationData *cellAnim = (CellAnimationData *)&assetSprite->animData;
        NNS_G2dDrawCellAnimation(&cellAnim->anim);
    } else {
        MultiCellAnimationData *multiCellAnim = (MultiCellAnimationData *)&assetSprite->animData;
        NNS_G2dDrawMultiCellAnimation(&multiCellAnim->anim);
    }

    NNS_G2dPopMtx();
    NNS_G2dEndRendering();
}

static void SpriteList_DrawSprite_Stub(const SpriteList *list, Sprite *sprite)
{
    return;
}

static void Sprite_UpdateAnimInternal(Sprite *sprite)
{
    Sprite_UpdateAnim(sprite, sprite->animSpeed);
}

static void Sprite_UpdateAnimInternal_Stub(Sprite *sprite)
{
    return;
}

static void SpriteList_Insert(SpriteList *list, Sprite *sprite)
{
    // If the list is empty, insert the sprite as the only sprite
    if (list->sentinelData.next == &list->sentinelData) {
        list->sentinelData.next = sprite;
        list->sentinelData.prev = sprite;
        sprite->prev = &list->sentinelData;
        sprite->next = &list->sentinelData;
        return;
    }

    // Check if the sprite should be inserted at the end of the list (lowest priority)
    if (list->sentinelData.prev->priority <= sprite->priority) {
        sprite->prev = list->sentinelData.prev;
        list->sentinelData.prev->next = sprite;
        sprite->next = &list->sentinelData;
        list->sentinelData.prev = sprite;
        return;
    }

    // Insert the sprite in the correct position according to its priority
    Sprite *cur = list->sentinelData.next;
    while (cur != &list->sentinelData) {
        if (cur->priority > sprite->priority) {
            cur->prev->next = sprite;
            sprite->prev = cur->prev;
            cur->prev = sprite;
            sprite->next = cur;
            break;
        }

        cur = cur->next;
    }
}

static void SpriteList_Remove(Sprite *sprite)
{
    sprite->prev->next = sprite->next;
    sprite->next->prev = sprite->prev;
}

static void SpriteList_InitSprites(SpriteList *list)
{
    for (int i = 0; i < list->capacity; i++) {
        Sprite_Reset(&list->sprites[i]);
        list->freeSprites[i] = list->sprites + i;
    }

    list->freeSpriteHead = 0;
}

static Sprite *SpriteList_AllocSprite(SpriteList *list)
{
    if (list->freeSpriteHead >= list->capacity) {
        return NULL;
    }

    Sprite *sprite = list->freeSprites[list->freeSpriteHead];
    list->freeSpriteHead++;

    return sprite;
}

static BOOL SpriteList_FreeSprite(SpriteList *list, Sprite *sprite)
{
    if (list->freeSpriteHead <= 0) {
        return FALSE;
    }

    Sprite_Reset(sprite);

    list->freeSpriteHead--;
    list->freeSprites[list->freeSpriteHead] = sprite;

    return TRUE;
}
