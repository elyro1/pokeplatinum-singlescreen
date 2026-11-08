#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "bwbattle/bwbattle.h"
#include "port/bw_mon_sprites.h"

#include "system.h"

// BW2's animated sprites, for a PokemonSpriteManager with large frames. A sprite is a multi-cell animation whose cells
// are quads of a 256x128 image, which the sprite's bin file describes. This draws them as bwbattle/system/mcss.c does,
// through the sprite manager's transforms, so that Platinum's screens keep their moves, fades and scales.

#define ARC_POKEGRA BWBATTLE_ASSET_DIR "/a/0/0/4"

// A sprite's files after its first, front or back (bwbattle/system/pokegra.c)
#define POKEGRA_FILE_COUNT     20
#define POKEGRA_FILE_BACK      9
#define POKEGRA_FILE_ATLAS     2
#define POKEGRA_FILE_CELLS     4
#define POKEGRA_FILE_CELL_ANIMS 5
#define POKEGRA_FILE_MULTI_CELLS 6
#define POKEGRA_FILE_MULTI_CELL_ANIMS 7
#define POKEGRA_FILE_BIN       8

#define NCGR_PIXELS 0x30

// The most frames a late draw catches up on. A screen that hid the sprite for a while just carries on
#define MAX_TICKS_PER_DRAW 4

// Cells are drawn this far behind the one before, as MCSS draws them
#define CELL_DEPTH 0x400
#define MAX_POLYGON_ID 63
#define MAX_ALPHA 31

typedef struct {
    fx32 x;
    fx32 y;
    fx32 width;
    fx32 height;
    fx32 s;
    fx32 t;
} BinQuad;

typedef struct {
    BinQuad quad;
    BinQuad quad2; // Drawn before the first when its width is not 0
} BinCell;

typedef struct {
    u32 count;
    u16 unk4;
    u16 unk6;
    s16 unk8;
    s16 unkA;
    BinCell cells[];
} Bin;

struct BwMonAnim {
    void *cellFile;
    void *cellAnimFile;
    void *multiCellFile;
    void *multiCellAnimFile;
    // The port's NitroSystem registers its copies of the banks by where these pointers are kept
    NNSG2dCellDataBank *cellBank;
    NNSG2dAnimBankData *cellAnimBank;
    NNSG2dMultiCellDataBank *multiCellBank;
    NNSG2dAnimBankData *multiCellAnimBank;
    NNSG2dMultiCellAnimation animation;
    void *animationWork;
    Bin *bin;
    unsigned binSize;
    PokemonSpriteTemplate template;
    int originX;
    int originY;
    u32 lastTick;
    BOOL ticked;
};

static BOOL sAnimationEnded;

static void OnAnimationEnd(u32 param, fx32 frame)
{
    (void)param;
    (void)frame;
    sAnimationEnded = TRUE;
}

static void GetAnimSrt(const NNSG2dAnimController *controller, NNSG2dAnimDataSRT *srt)
{
    switch ((u16)controller->pAnimSequence->animType) {
    case NNS_G2D_ANIMELEMENT_INDEX_SRT:
        *srt = *(const NNSG2dAnimDataSRT *)controller->pCurrent->pContent;
        break;
    case NNS_G2D_ANIMELEMENT_INDEX_T: {
        const NNSG2dAnimDataT *data = controller->pCurrent->pContent;

        srt->index = data->index;
        srt->rotZ = 0;
        srt->sx = FX32_ONE;
        srt->sy = FX32_ONE;
        srt->px = data->px;
        srt->py = data->py;
        break;
    }
    default:
        srt->index = *(const u16 *)controller->pCurrent->pContent;
        srt->rotZ = 0;
        srt->sx = FX32_ONE;
        srt->sy = FX32_ONE;
        srt->px = 0;
        srt->py = 0;
        break;
    }
}

static const NNSG2dMultiCellData *CurrentMultiCell(const BwMonAnim *anim, NNSG2dAnimDataSRT *srt)
{
    GetAnimSrt(&anim->animation.animCtrl, srt);
    return srt->index < anim->multiCellBank->numMultiCellData ? &anim->multiCellBank->pMultiCellDataArray[srt->index] : NULL;
}

// A node's cell at this frame. FALSE if the frame names a cell that the bin doesn't have
static BOOL NodeCell(const BwMonAnim *anim, const NNSG2dMultiCellData *multiCell, int node, NNSG2dAnimDataSRT *srt, const BinCell **cell)
{
    NNSG2dMCCellAnimation *cellAnims = anim->animation.multiCellInstance.pCellAnimInstasnces;
    const int cellAnim = (multiCell->pHierDataArray[node].nodeAttr & 0xFF00) >> 8;

    GetAnimSrt(&cellAnims[cellAnim].cellAnim.animCtrl, srt);
    if (srt->index >= anim->bin->count) {
        return FALSE;
    }
    *cell = &anim->bin->cells[srt->index];
    return TRUE;
}

static void Rotate(float rotation, float *x, float *y)
{
    const float angle = rotation * 2.0f * 3.14159265f / 65536.0f;
    const float c = cosf(angle), s = sinf(angle);
    const float rotatedX = *x * c - *y * s;

    *y = *x * s + *y * c;
    *x = rotatedX;
}

static void ExtendBoundsByQuad(const u8 *atlas, const BinQuad *quad, const NNSG2dAnimDataSRT *multiCellSrt, const NNSG2dAnimDataSRT *cellSrt, const NNSG2dMultiCellHierarchyData *node, float bounds[4])
{
    const int s = quad->s >> FX32_SHIFT, t = quad->t >> FX32_SHIFT;
    const int width = quad->width >> FX32_SHIFT, height = quad->height >> FX32_SHIFT;

    for (int v = 0; v < height; v++) {
        for (int u = 0; u < width; u++) {
            const int texelX = s + u, texelY = t + v;

            if (texelX < 0 || texelX >= 256 || texelY < 0 || texelY >= 128) {
                continue;
            }

            const u8 byte = atlas[texelY * 128 + texelX / 2];

            if (((texelX & 1) ? byte >> 4 : byte & 0xF) == 0) {
                continue;
            }

            // The same order as BwMonAnim_Draw's matrices, from the texel out
            float x = quad->x / 256.0f + u, y = -quad->y / 256.0f + v;

            x *= cellSrt->sx / (float)FX32_ONE;
            y *= cellSrt->sy / (float)FX32_ONE;
            Rotate(cellSrt->rotZ, &x, &y);
            x += cellSrt->px + node->posX;
            y += cellSrt->py + node->posY;
            x *= multiCellSrt->sx / (float)FX32_ONE;
            y *= multiCellSrt->sy / (float)FX32_ONE;
            Rotate(multiCellSrt->rotZ, &x, &y);
            x += multiCellSrt->px;
            y += multiCellSrt->py;

            bounds[0] = x < bounds[0] ? x : bounds[0];
            bounds[1] = y < bounds[1] ? y : bounds[1];
            bounds[2] = x > bounds[2] ? x : bounds[2];
            bounds[3] = y > bounds[3] ? y : bounds[3];
        }
    }
}

// Lines the first frame up with the sprite's single image, which the screens' positions are worked out from: centered
// across, and standing on the same row
static void FindOrigin(BwMonAnim *anim, const u8 *atlas)
{
    NNSG2dAnimDataSRT multiCellSrt, cellSrt;
    const BinCell *cell;
    const NNSG2dMultiCellData *multiCell = CurrentMultiCell(anim, &multiCellSrt);
    float bounds[4] = { 1e9f, 1e9f, -1e9f, -1e9f };

    anim->originX = BW_MON_SPRITE_SIZE / 2;
    anim->originY = BW_MON_SPRITE_SIZE / 2;

    if (!multiCell) {
        return;
    }
    for (int node = 0; node < multiCell->numNodes; node++) {
        if (NodeCell(anim, multiCell, node, &cellSrt, &cell)) {
            ExtendBoundsByQuad(atlas, &cell->quad, &multiCellSrt, &cellSrt, &multiCell->pHierDataArray[node], bounds);
            if (cell->quad2.width != 0) {
                ExtendBoundsByQuad(atlas, &cell->quad2, &multiCellSrt, &cellSrt, &multiCell->pHierDataArray[node], bounds);
            }
        }
    }

    int minX, minY, maxX, maxY;

    if (bounds[2] >= bounds[0] && BwMonSprites_ArtBounds(&anim->template, &minX, &minY, &maxX, &maxY)) {
        anim->originX = (int)lroundf((minX + maxX + 1) / 2.0f - (bounds[0] + bounds[2] + 1.0f) / 2.0f);
        anim->originY = (int)lroundf(maxY - bounds[3]);
    }
}

void BwMonAnim_Free(BwMonAnim *anim)
{
    if (!anim) {
        return;
    }

    // The checks take the pointers strictly inside the range, and free one bank of a kind each
    u8 *start = (u8 *)anim - 1, *end = (u8 *)(anim + 1);

    WIN_CheckAndFreeCellBank(start, end);
    WIN_CheckAndFreeAnimBank(start, end);
    WIN_CheckAndFreeAnimBank(start, end);
    WIN_CheckAndFreeMultiCellBank(start, end);
    free(anim->cellFile);
    free(anim->cellAnimFile);
    free(anim->multiCellFile);
    free(anim->multiCellAnimFile);
    free(anim->animationWork);
    free(anim->bin);
    free(anim);
}

BwMonAnim *BwMonAnim_Load(const PokemonSpriteTemplate *spriteTemplate, u8 *atlas)
{
    // Front or back, and male or female, are the place of the sprite's single image in its files
    const u32 offset = spriteTemplate->character % POKEGRA_FILE_COUNT;
    const u32 direction = offset >= POKEGRA_FILE_BACK ? POKEGRA_FILE_BACK : 0;
    const u32 female = offset - direction;
    const u32 base = spriteTemplate->character - offset + direction;

    BwMonAnim *anim = calloc(1, sizeof(BwMonAnim));

    if (!anim) {
        return NULL;
    }
    anim->template = *spriteTemplate;
    anim->cellFile = BwBattle_ReadMember(ARC_POKEGRA, base + POKEGRA_FILE_CELLS, NULL);
    anim->cellAnimFile = BwBattle_ReadMember(ARC_POKEGRA, base + POKEGRA_FILE_CELL_ANIMS, NULL);
    anim->multiCellFile = BwBattle_ReadMember(ARC_POKEGRA, base + POKEGRA_FILE_MULTI_CELLS, NULL);
    anim->multiCellAnimFile = BwBattle_ReadMember(ARC_POKEGRA, base + POKEGRA_FILE_MULTI_CELL_ANIMS, NULL);
    // Raw: its first byte is its number of cells, which can look like a compressed file's
    anim->bin = BwBattle_ReadMemberRaw(ARC_POKEGRA, base + POKEGRA_FILE_BIN, &anim->binSize);

    unsigned atlasSize = 0;
    u8 *atlasFile = BwBattle_ReadMember(ARC_POKEGRA, base + POKEGRA_FILE_ATLAS + female, &atlasSize);

    BOOL valid = anim->cellFile && anim->cellAnimFile && anim->multiCellFile && anim->multiCellAnimFile && anim->bin
        && atlasFile && atlasSize >= NCGR_PIXELS + BW_MON_ANIM_ATLAS_SIZE && !memcmp(atlasFile, "RGCN", 4)
        && anim->binSize >= sizeof(Bin) && anim->bin->count <= (anim->binSize - sizeof(Bin)) / sizeof(BinCell)
        && NNS_G2dGetUnpackedCellBank(anim->cellFile, &anim->cellBank)
        && NNS_G2dGetUnpackedAnimBank(anim->cellAnimFile, &anim->cellAnimBank)
        && NNS_G2dGetUnpackedMultiCellBank(anim->multiCellFile, &anim->multiCellBank)
        && NNS_G2dGetUnpackedMCAnimBank(anim->multiCellAnimFile, &anim->multiCellAnimBank)
        && anim->multiCellAnimBank->numSequences > 0;

    if (valid) {
        anim->animationWork = malloc(NNS_G2dGetMCWorkAreaSize(anim->multiCellBank, NNS_G2D_MCTYPE_SHARE_CELLANIM));
        valid = anim->animationWork != NULL;
    }
    if (!valid) {
        free(atlasFile);
        BwMonAnim_Free(anim);
        return NULL;
    }

    u8 *pixels = atlasFile + NCGR_PIXELS;

    if (spriteTemplate->spindaSpots) {
        BwBattle_DrawMcssSpindaSpots(pixels, spriteTemplate->personality);
    }
    memcpy(atlas, pixels, BW_MON_ANIM_ATLAS_SIZE);

    NNS_G2dInitMCAnimationInstance(&anim->animation, anim->animationWork, anim->cellAnimBank, anim->cellBank, anim->multiCellBank, NNS_G2D_MCTYPE_SHARE_CELLANIM);
    NNS_G2dSetAnimSequenceToMCAnimation(&anim->animation, NNS_G2dGetAnimSequenceByIdx(anim->multiCellAnimBank, 0));
    NNS_G2dSetAnimCtrlCallBackFunctor(&anim->animation.animCtrl, NNS_G2D_ANMCALLBACKTYPE_LAST_FRM, 0, OnAnimationEnd);
    NNS_G2dTickMCAnimation(&anim->animation, 0);
    FindOrigin(anim, pixels);
    free(atlasFile);
    return anim;
}

BOOL BwMonAnim_IsFor(const BwMonAnim *anim, const PokemonSpriteTemplate *spriteTemplate)
{
    return anim->template.character == spriteTemplate->character
        && anim->template.spindaSpots == spriteTemplate->spindaSpots
        && anim->template.personality == spriteTemplate->personality;
}

// BW2's sprites step once a V-blank, whichever rate the game is running its screens at
void BwMonAnim_Tick(BwMonAnim *anim)
{
    const u32 now = gSystem.vblankCounter;
    u32 ticks = anim->ticked ? now - anim->lastTick : 0;

    anim->ticked = TRUE;
    anim->lastTick = now;
    ticks = ticks > MAX_TICKS_PER_DRAW ? MAX_TICKS_PER_DRAW : ticks;

    for (u32 i = 0; i < ticks; i++) {
        sAnimationEnded = FALSE;
        NNS_G2dTickMCAnimation(&anim->animation, FX32_ONE);
        if (sAnimationEnded) {
            NNS_G2dRestartMCAnimation(&anim->animation);
            NNS_G2dTickMCAnimation(&anim->animation, 0);
        }
    }
}

static void DrawQuad(const BinQuad *quad, const NNSG2dAnimDataSRT *cellSrt, const NNSG2dMultiCellHierarchyData *node, int atlasY, fx32 z)
{
    const fx32 s = quad->s, t = quad->t + (atlasY << FX32_SHIFT);

    G3_PushMtx();
    G3_Translate((cellSrt->px + node->posX) << FX32_SHIFT, (cellSrt->py + node->posY) << FX32_SHIFT, z);
    G3_RotZ(FX_SinIdx(cellSrt->rotZ), FX_CosIdx(cellSrt->rotZ));
    G3_Scale(cellSrt->sx, cellSrt->sy, FX32_ONE);
    // The file's quads have 8 fractional bits, and y going up
    G3_Translate(quad->x * (FX32_ONE / 256), -quad->y * (FX32_ONE / 256), 0);
    G3_Scale(quad->width, quad->height, FX32_ONE);

    G3_Begin(GX_BEGIN_QUADS);
    G3_TexCoord(s, t);
    G3_Vtx(0, 0, 0);
    G3_TexCoord(s, t + quad->height);
    G3_Vtx(0, FX16_ONE, 0);
    G3_TexCoord(s + quad->width, t + quad->height);
    G3_Vtx(FX16_ONE, FX16_ONE, 0);
    G3_TexCoord(s + quad->width, t);
    G3_Vtx(FX16_ONE, 0, 0);
    G3_End();

    G3_PopMtx(1);
}

void BwMonAnim_Draw(const BwMonAnim *anim, const BwMonAnimDraw *draw)
{
    NNSG2dAnimDataSRT multiCellSrt, cellSrt;
    const BinCell *cell;
    const NNSG2dMultiCellData *multiCell = CurrentMultiCell(anim, &multiCellSrt);

    if (!multiCell) {
        return;
    }

    const u16 rotation = multiCellSrt.rotZ;
    // Translucent cells go from the back, so that they blend. MCSS gives them a polygon ID each for the same reason
    const BOOL translucent = draw->alpha != MAX_ALPHA;
    fx32 z = 0;

    G3_PushMtx();
    G3_Translate(draw->x << FX32_SHIFT, draw->y << FX32_SHIFT, draw->z << FX32_SHIFT);
    G3_Scale((draw->flipH ? -draw->scaleX : draw->scaleX) << (FX32_SHIFT - 8), (draw->flipV ? -draw->scaleY : draw->scaleY) << (FX32_SHIFT - 8), FX32_ONE);
    G3_Translate((anim->originX - BW_MON_SPRITE_SIZE / 2 + multiCellSrt.px) << FX32_SHIFT, (anim->originY - BW_MON_SPRITE_SIZE / 2 + multiCellSrt.py) << FX32_SHIFT, 0);
    G3_RotZ(FX_SinIdx(rotation), FX_CosIdx(rotation));
    G3_Scale(multiCellSrt.sx, multiCellSrt.sy, FX32_ONE);

    for (int i = 0; i < multiCell->numNodes; i++) {
        const int node = translucent ? multiCell->numNodes - 1 - i : i;

        if (!NodeCell(anim, multiCell, node, &cellSrt, &cell)) {
            continue;
        }

        const int polygonID = translucent ? (node > MAX_POLYGON_ID ? MAX_POLYGON_ID : node) : draw->polygonID;

        G3_PolygonAttr(GX_LIGHTMASK_NONE, GX_POLYGONMODE_MODULATE, GX_CULL_NONE, polygonID, draw->alpha, 0);

        if (cell->quad2.width != 0 && !translucent) {
            DrawQuad(&cell->quad2, &cellSrt, &multiCell->pHierDataArray[node], draw->atlasY, z);
            z -= CELL_DEPTH;
        }
        DrawQuad(&cell->quad, &cellSrt, &multiCell->pHierDataArray[node], draw->atlasY, z);
        z += translucent ? CELL_DEPTH : -CELL_DEPTH;
        if (cell->quad2.width != 0 && translucent) {
            DrawQuad(&cell->quad2, &cellSrt, &multiCell->pHierDataArray[node], draw->atlasY, z);
            z += CELL_DEPTH;
        }
    }

    G3_PopMtx(1);
}
