#ifndef POKEPLATINUM_RENDER_VIEW_H
#define POKEPLATINUM_RENDER_VIEW_H

#include <nnsys.h>

void InitRenderer(NNSG2dRendererInstance *renderer, fx32 spriteZOffset);
void InitRenderSurface(NNSG2dRenderSurface *surface,
    NNSG2dViewRect *viewRect,
    NNSG2dOamRegisterFunction oamRegisterFunc,
    NNSG2dAffineRegisterFunction oamAffineRegisterFunc,
    NNSG2dRndCellCullingFunction visibilityCullingFunc,
    NNSG2dSurfaceType surfaceType,
    NNSG2dRendererInstance *renderer);
void SetRenderSurfaceViewRect(NNSG2dRenderSurface *surface, NNSG2dViewRect *viewRect);
BOOL IsObjectInView(const NNSG2dCellData *cellData, const MtxFx32 *objectCoords, const NNSG2dViewRect *viewRect);
#ifdef SDK_PORT
// Keeps sprites up to this many dots beyond the left and right view edges,
// for apps that draw them across a wide UI view (SIM_SetWideUI).
void RenderView_SetHorizontalMargin(int dots);
#endif

#endif // POKEPLATINUM_RENDER_VIEW_H
