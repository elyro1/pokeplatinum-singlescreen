#include "overlay005/area_data.h"

#include <nitro.h>
#include <string.h>

#include "constants/graphics.h"
#include "constants/heap.h"

#include "overlay005/map_prop_animation.h"
#include "overlay005/map_prop_material_shape.h"

#include "easy3d.h"
#include "heap.h"
#include "narc.h"

static const GXRgb sIndoorsEdgeMarkings[8] = {
    COLOR_BLACK,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY
};

static const GXRgb sOutdoorsEdgeMarkings[8] = {
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY,
    COLOR_DARK_GRAY
};

static void AreaData_StripTextureData(void *resourceFile, NNSG3dResTex *texture)
{
    u8 *textureData = (u8 *)texture + texture->texInfo.ofsTex;
    u32 strippedTextureDataSize = (u32)(textureData - (u8 *)resourceFile);

    Heap_Realloc(resourceFile, strippedTextureDataSize);
}

static enum HeapID AreaDataManager_Heap(const AreaDataManager *area)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    return area->heapID;
#else
    return HEAP_ID_FIELD1;
#endif
}

static AreaDataManager *AreaDataManager_AllocOnHeap(int areaDataArchiveID, MapPropAnimationManager *mapPropAnimMan, enum HeapID heapID)
{
    AreaDataManager *areaDataManager = Heap_Alloc(heapID, sizeof(AreaDataManager));

    MI_CpuClear8(areaDataManager, sizeof(AreaDataManager));
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    areaDataManager->areaDataArchiveID = areaDataArchiveID;
    areaDataManager->heapID = heapID;
#endif

    areaDataManager->loadData = Heap_AllocAtEnd(heapID, sizeof(AreaDataManagerLoadData));
    areaDataManager->loadData->areaDataArchiveID = areaDataArchiveID;
    areaDataManager->loadData->mapPropAnimMan = mapPropAnimMan;
    areaDataManager->loadData->dummy0C = 0;

    return areaDataManager;
}

AreaDataManager *AreaDataManager_Alloc(int areaDataArchiveID, MapPropAnimationManager *mapPropAnimMan)
{
    return AreaDataManager_AllocOnHeap(areaDataArchiveID, mapPropAnimMan, HEAP_ID_FIELD1);
}

static BOOL AreaDataManager_LoadTextures(AreaDataManager *areaDataManager)
{
    AreaDataManagerLoadData *loadData = areaDataManager->loadData;
    enum HeapID heapID = AreaDataManager_Heap(areaDataManager);

    NARC_ReadWholeMemberByIndexPair(&areaDataManager->areaData, NARC_INDEX_FIELDDATA__AREADATA__AREA_DATA, loadData->areaDataArchiveID);

    areaDataManager->mapPropModelIDs = NARC_AllocAndReadWholeMemberByIndexPair(NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREA_BUILD, areaDataManager->areaData.mapPropArchivesID, heapID);
    loadData->mapPropModelIDsCount = areaDataManager->mapPropModelIDs[0];

    GF_ASSERT(loadData->mapPropModelIDsCount < MAX_MAP_PROP_MODEL_FILES);

    areaDataManager->mapTexture = NULL;
    areaDataManager->mapTextureFile = NARC_AllocAndReadWholeMemberByIndexPair(NARC_INDEX_FIELDDATA__AREADATA__AREA_MAP_TEX__MAP_TEX_SET, areaDataManager->areaData.mapTextureArchiveID, heapID);
    areaDataManager->mapPropTextureFile = NARC_AllocAndReadWholeMemberByIndexPair(NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREABM_TEXSET, areaDataManager->areaData.mapPropArchivesID, heapID);
    areaDataManager->mapTexture = NNS_G3dGetTex((NNSG3dResFileHeader *)areaDataManager->mapTextureFile);

    if (loadData->mapPropModelIDsCount != 0) {
        areaDataManager->mapPropTexture = NNS_G3dGetTex((NNSG3dResFileHeader *)areaDataManager->mapPropTextureFile);
    } else {
        areaDataManager->mapPropTexture = NULL;
    }

    BOOL textureUploaded = Easy3D_UploadTextureToVRAM(areaDataManager->mapTexture);
    if (!textureUploaded) {
        return FALSE;
    }

    AreaData_StripTextureData(areaDataManager->mapTextureFile, areaDataManager->mapTexture);

    if (areaDataManager->mapPropTexture != NULL) {
        BOOL textureUploaded = Easy3D_UploadTextureToVRAM(areaDataManager->mapPropTexture);
        if (!textureUploaded) {
            return FALSE;
        }

        AreaData_StripTextureData(areaDataManager->mapPropTextureFile, areaDataManager->mapPropTexture);
    }

    for (int i = 0; i < MAX_MAP_PROP_MODEL_FILES; i++) {
        areaDataManager->mapPropModelFiles[i] = NULL;
    }

    return TRUE;
}

static void AreaDataManager_LoadProp(AreaDataManager *areaDataManager, NARC *narc, u16 mapPropModelID)
{
    AreaDataManagerLoadData *loadData = areaDataManager->loadData;
    int mapPropModelAnimeListNARCFileCount = MapPropAnimationManager_GetAnimeListNARCFileCount(loadData->mapPropAnimMan);

    GF_ASSERT(mapPropModelID < MAX_MAP_PROP_MODEL_FILES);
    GF_ASSERT(areaDataManager->mapPropModelFiles[mapPropModelID] == NULL);
    areaDataManager->mapPropModelFiles[mapPropModelID] = NARC_AllocAndReadWholeMember(narc, mapPropModelID, AreaDataManager_Heap(areaDataManager));

    if (mapPropModelID < mapPropModelAnimeListNARCFileCount) {
        NNSG3dResMdl *mapPropModel = NNS_G3dGetMdlByIdx(NNS_G3dGetMdlSet(areaDataManager->mapPropModelFiles[mapPropModelID]), 0);
        MapPropAnimationManager_LoadPropAnimations(mapPropModelID, mapPropModel, areaDataManager->mapPropTexture, loadData->mapPropAnimMan);
    }

    BOOL textureBound = Easy3D_BindTextureToResource(areaDataManager->mapPropModelFiles[mapPropModelID], areaDataManager->mapPropTexture);
    GF_ASSERT(textureBound);
}

static BOOL AreaDataManager_LoadDummyProp(AreaDataManager *areaDataManager, NARC *narc)
{
    // Make sure the dummy box ("dmybox00") model is always loaded
    if (areaDataManager->mapPropModelFiles[0] == NULL) {
        areaDataManager->mapPropModelFiles[0] = NARC_AllocAndReadWholeMember(narc, 0, AreaDataManager_Heap(areaDataManager));
        NNSG3dResTex *texture = NNS_G3dGetTex(areaDataManager->mapPropModelFiles[0]);

        if (texture != NULL) {
            BOOL res = Easy3D_UploadTextureToVRAM(texture);
            if (!res) {
                return FALSE;
            }

            res = Easy3D_BindTextureToResource(areaDataManager->mapPropModelFiles[0], texture);
            GF_ASSERT(res);
        }
    }
    return TRUE;
}

static void AreaDataManager_FinishLoading(AreaDataManager *areaDataManager)
{
    char mapPropMaterialShapeFilePath[256];

    if (!areaDataManager->mapPropMatShp) {
        areaDataManager->mapPropMatShp = MapPropMaterialShape_Alloc();
        sprintf(mapPropMaterialShapeFilePath, "fielddata/build_model/build_model_matshp.dat");
        MapPropMaterialShape_Load(mapPropMaterialShapeFilePath, areaDataManager->mapPropMatShp);
    }

    Heap_Free(areaDataManager->loadData);
    areaDataManager->loadData = NULL;
}

void AreaDataManager_Load(AreaDataManager *areaDataManager)
{
    BOOL loaded = AreaDataManager_LoadTextures(areaDataManager);
    GF_ASSERT(loaded);
    if (!loaded) {
        return;
    }
    NARC *narc = NARC_ctor(NARC_INDEX_FIELDDATA__BUILD_MODEL__BUILD_MODEL, HEAP_ID_FIELD1);
    for (int i = 0; i < areaDataManager->loadData->mapPropModelIDsCount; i++) {
        AreaDataManager_LoadProp(areaDataManager, narc, areaDataManager->mapPropModelIDs[i + 1]);
    }
    loaded = AreaDataManager_LoadDummyProp(areaDataManager, narc);
    GF_ASSERT(loaded);
    NARC_dtor(narc);
    G3X_EdgeMarking(TRUE);
    G3X_SetEdgeColorTable(AreaDataManager_IsOutdoorsLighting(areaDataManager) ? sOutdoorsEdgeMarkings : sIndoorsEdgeMarkings);
    AreaDataManager_FinishLoading(areaDataManager);
}

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
AreaDataManager *AreaDataManager_AllocPreview(int areaDataArchiveID, MapPropAnimationManager *animations, const AreaDataManager *owner)
{
    AreaDataManager *area = AreaDataManager_AllocOnHeap(areaDataArchiveID, animations, HEAP_ID_FIELD_SCENERY);
    // This table is common to every area and outlives the owner's scenery cache.
    area->mapPropMatShp = owner->mapPropMatShp;
    return area;
}

AreaPreviewLoadResult AreaDataManager_LoadPreviewStep(AreaDataManager *areaDataManager)
{
    AreaDataManagerLoadData *loadData = areaDataManager->loadData;
    if (!loadData) {
        return AREA_PREVIEW_READY;
    }
    if (loadData->dummy0C == 0) {
        if (!AreaDataManager_LoadTextures(areaDataManager)) {
            return AREA_PREVIEW_LOAD_FAILED;
        }
        loadData->dummy0C = 1;
        return AREA_PREVIEW_LOADING;
    }
    int index = loadData->dummy0C - 1;
    NARC *narc = NARC_ctor(NARC_INDEX_FIELDDATA__BUILD_MODEL__BUILD_MODEL, AreaDataManager_Heap(areaDataManager));
    if (index < loadData->mapPropModelIDsCount) {
        AreaDataManager_LoadProp(areaDataManager, narc, areaDataManager->mapPropModelIDs[index + 1]);
        loadData->dummy0C++;
        NARC_dtor(narc);
        return AREA_PREVIEW_LOADING;
    }
    BOOL loaded = AreaDataManager_LoadDummyProp(areaDataManager, narc);
    NARC_dtor(narc);
    if (!loaded) {
        return AREA_PREVIEW_LOAD_FAILED;
    }
    AreaDataManager_FinishLoading(areaDataManager);
    return AREA_PREVIEW_READY;
}

static void AreaDataManager_ReleasePreviewTexture(NNSG3dResTex *texture)
{
    if (!texture) {
        return;
    }
    NNSG3dTexKey normal, compressed;
    NNS_G3dTexReleaseTexKey(texture, &normal, &compressed);
    NNSG3dPlttKey palette = NNS_G3dPlttReleasePlttKey(texture);
    if (normal) {
        NNS_GfdFreeTexVram(normal);
    }
    if (compressed) {
        NNS_GfdFreeTexVram(compressed);
    }
    if (palette) {
        NNS_GfdFreePlttVram(palette);
    }
}

void AreaDataManager_FreePreview(AreaDataManager **areaDataManager)
{
    AreaDataManager *area = *areaDataManager;
    AreaDataManager_ReleasePreviewTexture(area->mapTexture);
    AreaDataManager_ReleasePreviewTexture(area->mapPropTexture);
    // The fallback box has its own embedded texture.
    if (area->mapPropModelFiles[0]) {
        AreaDataManager_ReleasePreviewTexture(NNS_G3dGetTex(area->mapPropModelFiles[0]));
    }
    if (area->loadData) {
        Heap_Free(area->loadData);
        area->loadData = NULL;
    }
    area->mapPropMatShp = NULL; // Borrowed from the gameplay area.
    AreaDataManager_Free(areaDataManager);
}
#endif

NNSG3dResFileHeader **AreaDataManager_GetMapPropModelFile(const int mapPropModelID, AreaDataManager *const areaDataManager)
{
    GF_ASSERT(areaDataManager != NULL);

    if (areaDataManager->mapPropModelFiles[mapPropModelID] == NULL) {
        // Return the dummy box model if the requested one is not loaded
        GF_ASSERT(areaDataManager->mapPropModelFiles[0] != NULL);
        return &areaDataManager->mapPropModelFiles[0];
    }

    return &areaDataManager->mapPropModelFiles[mapPropModelID];
}

void AreaDataManager_Free(AreaDataManager **areaDataManager)
{
    if ((*areaDataManager)->mapPropMatShp != NULL) {
        MapPropMaterialShape_Free((*areaDataManager)->mapPropMatShp);
    }

    for (int i = 0; i < MAX_MAP_PROP_MODEL_FILES; i++) {
        if ((*areaDataManager)->mapPropModelFiles[i] != NULL) {
            Heap_Free((*areaDataManager)->mapPropModelFiles[i]);
        }
    }

    if ((*areaDataManager)->mapPropModelIDs) {
        Heap_Free((*areaDataManager)->mapPropModelIDs);
    }
    if ((*areaDataManager)->mapTextureFile) {
        Heap_Free((*areaDataManager)->mapTextureFile);
    }

    (*areaDataManager)->mapTextureFile = NULL;
    if ((*areaDataManager)->mapPropTextureFile) {
        Heap_Free((*areaDataManager)->mapPropTextureFile);
    }

    (*areaDataManager)->mapPropTextureFile = NULL;
    Heap_Free(*areaDataManager);

    (*areaDataManager) = NULL;
}

NNSG3dResTex *AreaDataManager_GetMapTexture(const AreaDataManager *areaDataManager)
{
    GF_ASSERT(areaDataManager != NULL);
    return areaDataManager->mapTexture;
}

NNSG3dResTex *AreaDataManager_GetMapPropTexture(const AreaDataManager *areaDataManager)
{
    GF_ASSERT(areaDataManager != NULL);
    return areaDataManager->mapPropTexture;
}

const MapPropMaterialShape *AreaDataManager_GetMapPropMaterialShape(const AreaDataManager *areaDataManager)
{
    return areaDataManager->mapPropMatShp;
}

BOOL AreaDataManager_IsOutdoorsLighting(const AreaDataManager *areaDataManager)
{
    if ((areaDataManager->areaData.areaLightArchiveID == 0) || (areaDataManager->areaData.areaLightArchiveID == 3)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

u8 AreaDataManager_GetAreaLightArchiveID(const AreaDataManager *areaDataManager)
{
    return areaDataManager->areaData.areaLightArchiveID;
}

int AreaDataManager_GetMapPropModelID(const AreaDataManager *areaDataManager, const int index)
{
    return areaDataManager->mapPropModelIDs[index];
}

BOOL AreaDataManager_HasMapPropModelFile(const AreaDataManager *areaDataManager, const int mapPropModelID)
{
    return areaDataManager->mapPropModelFiles[mapPropModelID] != NULL;
}
