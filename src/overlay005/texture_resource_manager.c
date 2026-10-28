#include "overlay005/texture_resource_manager.h"

#include <nitro.h>
#include <string.h>

#include "billboard_vram_transfer.h"
#include "heap.h"
#include "narc.h"
#include "particle_system.h"
#include "vram_transfer.h"

typedef struct {
    char name[16];
    u8 animationData[18][2];
} TextureAnimationData;

typedef struct {
    u8 *textureData;
    int textureSize;
    NNSG3dResTex *textureResource;
    void *narcData;
    TextureAnimationData *animationData;
    u16 currentFrame;
    u16 frameCounter;
} TextureSlot;

typedef struct {
    u32 textureCount;
    TextureAnimationData animationData[0];
} TextureDataHeader;

typedef struct FieldTextureManager_t {
    void *dataBuffer;
    TextureDataHeader *dataHeader;
    TextureSlot textureSlots[MAX_TEXTURE_KEYS];
    enum HeapID heapID;
} FieldTextureManager;

static int TextureResourceManager_LoadSingleTexture(FieldTextureManager *textureManager, NNSG3dResTex *textureResource, int textureIndex, NARC *narc);

static FieldTextureManager *TextureResourceManager_Alloc(enum HeapID heapID)
{
    FieldTextureManager *textureManager = Heap_Alloc(heapID, sizeof(FieldTextureManager));

    GF_ASSERT(textureManager);
    textureManager->heapID = heapID;

    for (int slotIndex = 0; slotIndex < MAX_TEXTURE_KEYS; slotIndex++) {
        textureManager->textureSlots[slotIndex].currentFrame = 0;
        textureManager->textureSlots[slotIndex].frameCounter = 0;
        textureManager->textureSlots[slotIndex].textureData = NULL;
        textureManager->textureSlots[slotIndex].textureResource = NULL;
        textureManager->textureSlots[slotIndex].animationData = NULL;
        textureManager->textureSlots[slotIndex].narcData = NULL;
    }

    return textureManager;
}

FieldTextureManager *TextureResourceManager_Create(void)
{
    return TextureResourceManager_Alloc(HEAP_ID_FIELD1);
}

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
FieldTextureManager *TextureResourceManager_CreateOnHeap(enum HeapID heapID)
{
    return TextureResourceManager_Alloc(heapID);
}
#endif

int TextureResourceManager_LoadTexture(FieldTextureManager *textureManager, NNSG3dResTex *textureResource)
{
    int loadedCount = 0;
    NARC *narc = NARC_ctor(NARC_INDEX_DATA__FLDTANIME, textureManager->heapID);

    textureManager->dataBuffer = NARC_AllocAndReadWholeMember(narc, 0, textureManager->heapID);
    textureManager->dataHeader = (TextureDataHeader *)textureManager->dataBuffer;

    for (int textureIndex = 0; textureIndex < textureManager->dataHeader->textureCount; textureIndex++) {
        if (TextureResourceManager_LoadSingleTexture(textureManager, textureResource, textureIndex, narc) >= 0) {
            loadedCount++;
        }
    }

    NARC_dtor(narc);

    return loadedCount;
}

static int TextureResourceManager_LoadSingleTexture(FieldTextureManager *textureManager, NNSG3dResTex *textureResource, int textureIndex, NARC *narc)
{
    int slotIndex;
    char debugString[40];

    if (textureManager == NULL) {
        return -1;
    }

    for (slotIndex = 0; slotIndex < MAX_TEXTURE_KEYS; slotIndex++) {
        if (textureManager->textureSlots[slotIndex].narcData == NULL) {
            break;
        }
    }

    if (slotIndex == MAX_TEXTURE_KEYS || textureResource == NULL) {
        return -1;
    }

    textureManager->textureSlots[slotIndex].textureData = GetTextureDataBufferFromResourceName(textureResource, textureManager->dataHeader->animationData[textureIndex].name);

    if (textureManager->textureSlots[slotIndex].textureData == NULL) {
        return -1;
    }

    textureManager->textureSlots[slotIndex].textureSize = CalcTextureDataSizeFromResourceName(textureResource, textureManager->dataHeader->animationData[textureIndex].name);
    sprintf(debugString, "data/fld_anime%d.bin", textureIndex);
    textureManager->textureSlots[slotIndex].animationData = &(textureManager->dataHeader->animationData[textureIndex]);
    textureManager->textureSlots[slotIndex].narcData = NARC_AllocAndReadWholeMember(narc, textureIndex + 1, textureManager->heapID);
    textureManager->textureSlots[slotIndex].textureResource = NNS_G3dGetTex((NNSG3dResFileHeader *)textureManager->textureSlots[slotIndex].narcData);

    return slotIndex;
}

void TextureResourceManager_Free(FieldTextureManager *textureManager)
{
    if (textureManager == NULL) {
        return;
    }

    for (int slotIndex = 0; slotIndex < MAX_TEXTURE_KEYS; slotIndex++) {
        TextureSlot *textureSlot = &textureManager->textureSlots[slotIndex];

        if (textureSlot->narcData == NULL || textureSlot->animationData == NULL) {
            continue;
        }

        if (textureSlot->animationData->animationData[textureSlot->currentFrame][1] <= textureSlot->frameCounter) {
            textureSlot->frameCounter = 0;
            textureSlot->currentFrame++;

            if (textureSlot->animationData->animationData[textureSlot->currentFrame][0] == 0xff) {
                textureSlot->currentFrame = 0;
            }

            VramTransfer_Request(NNS_GFD_DST_3D_TEX_VRAM, (u32)textureSlot->textureData, GetTextureDataVRAMBuffer(textureSlot->textureResource, textureSlot->animationData->animationData[textureSlot->currentFrame][0]), textureSlot->textureSize);
        } else {
            textureSlot->frameCounter++;
        }
    }
}

void TextureResourceManager_FreeSlot(FieldTextureManager *textureManager, int slotIndex)
{
    if (textureManager == NULL) {
        return;
    }

    if (textureManager->textureSlots[slotIndex].narcData) {
        Heap_Free(textureManager->textureSlots[slotIndex].narcData);
    }

    if (textureManager->textureSlots[slotIndex].animationData != NULL) {
        textureManager->textureSlots[slotIndex].animationData = NULL;
    }

    textureManager->textureSlots[slotIndex].currentFrame = 0;
    textureManager->textureSlots[slotIndex].frameCounter = 0;
}

void TextureResourceManager_FreeAllSlots(FieldTextureManager *textureManager)
{
    if (textureManager == NULL) {
        return;
    }

    for (int slotIndex = 0; slotIndex < MAX_TEXTURE_KEYS; slotIndex++) {
        TextureResourceManager_FreeSlot(textureManager, slotIndex);
    }

    Heap_Free(textureManager->dataBuffer);
}

void TextureResourceManager_Destroy(FieldTextureManager *textureManager)
{
    if (textureManager != NULL) {
        Heap_Free(textureManager);
        textureManager = NULL;
    }
}
