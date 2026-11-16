#include "types.h"
#include "gfl/arc.h"
#include "gfl/arc_util.h"
#include "gfl/bmp.h"
#include "gfl/graphics.h"
#include "gfl/heap.h"
#include "gfl/std.h"
#include "nitro/gx.h"
#include "nitro/os.h"

// The byte of a pixel of a bitmap of 16 or 256 color tiles, whose rows are tilesX tiles wide
#define PIXEL_ADDR_16(pixels, x, y, tilesX)                                                                            \
    ((u8 *)((pixels) + (((x) >> 1) & 3) + (((x) << 2) & 0x3fe0) + ((((y) << 2) & 0x3fe0) * (tilesX)) +                 \
            (u32)(((y) << 2) & 0x1c)))
#define PIXEL_ADDR_256(pixels, x, y, tilesX)                                                                           \
    ((u8 *)((pixels) + ((x) & 7) + (((x) << 3) & 0x7fc0) + ((((y) << 3) & 0x7fc0) * (tilesX)) +                        \
            (u32)(((y) << 3) & 0x38)))
// A width in pixels as tiles
#define PIXELS_TO_TILES(x) (((x) + ((x) & 7)) >> 3)

static void GFL_BitmapCopyArea_IDX4(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX, int destY,
                                    u32 width, u32 height, u16 colorKey);
static void GFL_BitmapCopyArea_IDX8(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX, int destY,
                                    u32 width, u32 height, u16 colorKey);
static void GFL_BitmapCopyAreaRebased_IDX8(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX,
                                           int destY, u32 width, u32 height, u16 colorKey, u8 paletteOffset);
static void GFL_BitmapCopyArea_IDX4_VRAM(const GFLBitmap *src, GFLBitmap *dest, u16 srcX, u16 srcY, s16 destX,
                                         s16 destY, u16 width, u16 height, u16 colorKey);
static void GFL_BitmapCopyArea_IDX8_VRAM(const GFLBitmap *src, GFLBitmap *dest, u16 srcX, u16 srcY, s16 destX,
                                         s16 destY, u16 width, u16 height, u16 colorKey);
static void GFL_BitmapFillArea_IDX4(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color);
static void GFL_BitmapFillArea_IDX8(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color);
static void GFL_BitmapFillArea_IDX4_VRAM(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color);
static void GFL_BitmapFillArea_IDX8_VRAM(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color);

GFLBitmap *GFL_BitmapCreate(u32 tileWidth, u32 tileHeight, u32 tileSize, HeapID heapId) {
    GFLBitmap *bitmap = GFL_HeapAllocate(heapId, sizeof(GFLBitmap), FALSE, "bmp.c", 97);

    bitmap->width = tileWidth * 8;
    bitmap->height = tileHeight * 8;
    bitmap->tileSize = tileSize;
    bitmap->pixels = GFL_HeapAllocate(heapId, tileWidth * tileHeight * tileSize, TRUE, "bmp.c", 103);
    bitmap->storage = GFL_BITMAP_OWNED;
    return bitmap;
}

GFLBitmap *GFL_BitmapWrap(void *pixels, u32 tileWidth, u32 tileHeight, u32 tileSize, HeapID heapId) {
    GFLBitmap *bitmap = GFL_HeapAllocate(heapId, sizeof(GFLBitmap), FALSE, "bmp.c", 123);

    bitmap->width = tileWidth * 8;
    bitmap->height = tileHeight * 8;
    bitmap->pixels = pixels;
    bitmap->tileSize = tileSize;
    bitmap->storage = GFL_BITMAP_WRAPPED;
    return bitmap;
}

GFLBitmap *GFL_BitmapWrapVRAM(void *pixels, u32 tileWidth, u32 tileHeight, u32 tileSize, HeapID heapId) {
    GFLBitmap *bitmap = GFL_HeapAllocate(heapId, sizeof(GFLBitmap), FALSE, "bmp.c", 148);

    bitmap->width = tileWidth * 8;
    bitmap->height = tileHeight * 8;
    bitmap->pixels = pixels;
    bitmap->tileSize = tileSize;
    bitmap->storage = GFL_BITMAP_WRAPPED_VRAM;
    return bitmap;
}

void GFL_BitmapFree(GFLBitmap *bitmap) {
    if (bitmap->storage == GFL_BITMAP_OWNED) {
        GFL_HeapFree(bitmap->pixels);
    }
    GFL_HeapFree(bitmap);
}

u8 *GFL_BitmapGetPixelData(GFLBitmap *bitmap) {
    return bitmap->pixels;
}

u16 GFL_BitmapGetWidth(GFLBitmap *bitmap) {
    return bitmap->width;
}

u16 GFL_BitmapGetHeight(GFLBitmap *bitmap) {
    return bitmap->height;
}

u32 GFL_BitmapCalcPixelDataSize(const GFLBitmap *bitmap) {
    return bitmap->width / 8 * bitmap->height / 8 * bitmap->tileSize;
}

void GFL_BitmapCopy(const GFLBitmap *src, GFLBitmap *dest) {
    u32 size = GFL_BitmapCalcPixelDataSize(src);
    u32 destSize = GFL_BitmapCalcPixelDataSize(dest);

    if (destSize < size) {
        size = destSize;
    }
    MIi_CpuCopy32(src->pixels, dest->pixels, size);
}

GFLBitmap *GFL_G2DIOLoadBitmap(u32 arcId, u32 fileId, BOOL compressed, HeapID heapId) {
    NNSG2dCharacterData *chars;
    GFLBitmap *bitmap = GFL_HeapAllocate(heapId, sizeof(GFLBitmap), FALSE, "bmp.c", 266);
    void *file = GFL_ArcSysReadHeapNewLZ(arcId, fileId, compressed, HEAPID_TAIL(heapId));

    if (!NNS_G2dGetUnpackedBGCharacterData(file, &chars)) {
        OS_Terminate();
        return NULL;
    }
    GFL_BitmapConvFromCHAR(bitmap, chars, heapId);
    GFL_HeapFree(file);
    return bitmap;
}

void GFL_BitmapConvFromCHAR(GFLBitmap *bitmap, NNSG2dCharacterData *chars, HeapID heapId) {
    bitmap->width = chars->W * 8;
    bitmap->height = chars->H * 8;
    bitmap->pixels = GFL_HeapAllocate(heapId, chars->szByte, FALSE, "bmp.c", 321);
    bitmap->storage = GFL_BITMAP_OWNED;
    switch (chars->pixelFmt) {
    case GX_TEXFMT_PLTT16:
        bitmap->tileSize = 0x20;
        break;
    case GX_TEXFMT_PLTT256:
        bitmap->tileSize = 0x40;
        break;
    default:
        break;
    }
    MIi_CpuCopy32(chars->pRawData, bitmap->pixels, chars->szByte);
}

u8 GFL_BitmapGetBytesPerTile(GFLBitmap *bitmap) {
    return bitmap->tileSize;
}

void GFL_BitmapCopyArea(GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX, int destY, u32 width,
                        u32 height, u16 colorKey) {
    if (src->tileSize != dest->tileSize) {
        return;
    }
    if (dest->storage != GFL_BITMAP_WRAPPED_VRAM) {
        if (src->tileSize == 0x20) {
            GFL_BitmapCopyArea_IDX4(src, dest, srcX, srcY, destX, destY, width, height, colorKey);
        } else {
            GFL_BitmapCopyArea_IDX8(src, dest, srcX, srcY, destX, destY, width, height, colorKey);
        }
    } else {
        if (src->tileSize == 0x20) {
            GFL_BitmapCopyArea_IDX4_VRAM(src, dest, srcX, srcY, destX, destY, width, height, colorKey);
        } else {
            GFL_BitmapCopyArea_IDX8_VRAM(src, dest, srcX, srcY, destX, destY, width, height, colorKey);
        }
    }
}

void GFL_BitmapCopyAreaRebased(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX, int destY,
                               u32 width, u32 height, u16 colorKey, u16 paletteOffset) {
    GFL_BitmapCopyAreaRebased_IDX8(src, dest, srcX, srcY, destX, destY, width, height, colorKey, paletteOffset);
}

void GFL_BitmapFillArea(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color) {
    if (bitmap->storage != GFL_BITMAP_WRAPPED_VRAM) {
        if (bitmap->tileSize == 0x20) {
            GFL_BitmapFillArea_IDX4(bitmap, x, y, width, height, color);
        } else {
            GFL_BitmapFillArea_IDX8(bitmap, x, y, width, height, color);
        }
    } else {
        if (bitmap->tileSize == 0x20) {
            GFL_BitmapFillArea_IDX4_VRAM(bitmap, x, y, width, height, color);
        } else {
            GFL_BitmapFillArea_IDX8_VRAM(bitmap, x, y, width, height, color);
        }
    }
}

void GFL_BitmapFill(GFLBitmap *bitmap, u8 color) {
    if (bitmap->tileSize == 0x20) {
        color = ((color & 0xf) << 4) | (color & 0xf);
    }
    MI_CpuFill8(bitmap->pixels, color, bitmap->width / 8 * (bitmap->height / 8) * bitmap->tileSize);
}

// Clips a copy to the source and the destination, and returns whether any of it is left
static inline BOOL ClipCopyArea(const GFLBitmap *src, const GFLBitmap *dest, u32 *srcX, u32 *srcY, int *destX,
                                int *destY, u32 *width, u32 *height) {
    u32 h = *height;
    u32 w = *width;

    if (*width > src->width) {
        w = src->width;
    }
    if (*destX >= dest->width || *destX <= -(int)w) {
        return FALSE;
    }
    if (*height > src->height) {
        h = src->height;
    }
    if (*destY >= dest->height || *destY <= -(int)h) {
        return FALSE;
    }
    if (*destX < 0) {
        w += *destX;
        *srcX -= *destX;
        *destX = 0;
    } else if (*destX + w > dest->width) {
        w -= *destX + w - dest->width;
    }
    if (*destY < 0) {
        h += *destY;
        *srcY -= *destY;
        *destY = 0;
    } else if (*destY + h > dest->height) {
        h -= *destY + h - dest->height;
    }
    *width = w;
    *height = h;
    return TRUE;
}

static void GFL_BitmapCopyArea_IDX4(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX, int destY,
                                    u32 width, u32 height, u16 colorKey) {
    u32 srcTilesX;
    u32 destTilesX;
    u32 i;
    u32 j;
    int dx;
    int sx;
    u8 *srcPixel;
    u8 *destPixel;
    int color;
    int shift;

    if (!ClipCopyArea(src, dest, &srcX, &srcY, &destX, &destY, &width, &height)) {
        return;
    }
    srcTilesX = PIXELS_TO_TILES(src->width);
    destTilesX = PIXELS_TO_TILES(dest->width);
    if (colorKey == GFL_BITMAP_NO_COLOR_KEY) {
        for (i = 0; i < height; i++, srcY++, destY++) {
            for (j = 0, dx = destX, sx = srcX; j < width; j++, sx++, dx++) {
                srcPixel = PIXEL_ADDR_16(src->pixels, sx, srcY, srcTilesX);
                destPixel = PIXEL_ADDR_16(dest->pixels, dx, destY, destTilesX);
                color = (*srcPixel >> ((sx & 1) * 4)) & 0xf;
                shift = (dx & 1) * 4;
                *destPixel = (color << shift) | (*destPixel & (0xf0 >> shift));
            }
        }
    } else {
        for (i = 0; i < height; i++, srcY++, destY++) {
            for (j = 0, dx = destX, sx = srcX; j < width; j++, sx++, dx++) {
                srcPixel = PIXEL_ADDR_16(src->pixels, sx, srcY, srcTilesX);
                destPixel = PIXEL_ADDR_16(dest->pixels, dx, destY, destTilesX);
                color = (*srcPixel >> ((sx & 1) * 4)) & 0xf;
                if (color != colorKey) {
                    shift = (dx & 1) * 4;
                    *destPixel = (color << shift) | (*destPixel & (0xf0 >> shift));
                }
            }
        }
    }
}

static void GFL_BitmapCopyArea_IDX8(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX, int destY,
                                    u32 width, u32 height, u16 colorKey) {
    u32 srcTilesX;
    u32 destTilesX;
    u32 i;
    u32 j;
    int dx;
    int sx;
    u8 *srcPixel;
    u8 *destPixel;

    if (!ClipCopyArea(src, dest, &srcX, &srcY, &destX, &destY, &width, &height)) {
        return;
    }
    srcTilesX = PIXELS_TO_TILES(src->width);
    destTilesX = PIXELS_TO_TILES(dest->width);
    if (colorKey == GFL_BITMAP_NO_COLOR_KEY) {
        for (i = 0; i < height; i++, srcY++, destY++) {
            for (j = 0, dx = destX, sx = srcX; j < width; j++, sx++, dx++) {
                srcPixel = PIXEL_ADDR_256(src->pixels, sx, srcY, srcTilesX);
                destPixel = PIXEL_ADDR_256(dest->pixels, dx, destY, destTilesX);
                *destPixel = *srcPixel;
            }
        }
    } else {
        for (i = 0; i < height; i++, srcY++, destY++) {
            for (j = 0, dx = destX, sx = srcX; j < width; j++, sx++, dx++) {
                srcPixel = PIXEL_ADDR_256(src->pixels, sx, srcY, srcTilesX);
                destPixel = PIXEL_ADDR_256(dest->pixels, dx, destY, destTilesX);
                if (*srcPixel != colorKey) {
                    *destPixel = *srcPixel;
                }
            }
        }
    }
}

static void GFL_BitmapCopyAreaRebased_IDX8(const GFLBitmap *src, GFLBitmap *dest, u32 srcX, u32 srcY, int destX,
                                           int destY, u32 width, u32 height, u16 colorKey, u8 paletteOffset) {
    u32 srcTilesX;
    u32 destTilesX;
    u32 i;
    u32 j;
    int dx;
    int sx;
    u8 *srcPixel;
    u8 *destPixel;
    int color;

    if (!ClipCopyArea(src, dest, &srcX, &srcY, &destX, &destY, &width, &height)) {
        return;
    }
    srcTilesX = PIXELS_TO_TILES(src->width);
    destTilesX = PIXELS_TO_TILES(dest->width);
    if (colorKey == GFL_BITMAP_NO_COLOR_KEY) {
        for (i = 0; i < height; i++, srcY++, destY++) {
            for (j = 0, dx = destX, sx = srcX; j < width; j++, sx++, dx++) {
                srcPixel = PIXEL_ADDR_16(src->pixels, sx, srcY, srcTilesX);
                destPixel = PIXEL_ADDR_256(dest->pixels, dx, destY, destTilesX);
                color = (*srcPixel >> ((sx & 1) * 4)) & 0xf;
                if (color != 0) {
                    color += paletteOffset;
                }
                *destPixel = color;
            }
        }
    } else {
        // Any color key leaves color 0 transparent
        for (i = 0; i < height; i++, srcY++, destY++) {
            for (j = 0, dx = destX, sx = srcX; j < width; j++, sx++, dx++) {
                srcPixel = PIXEL_ADDR_16(src->pixels, sx, srcY, srcTilesX);
                destPixel = PIXEL_ADDR_256(dest->pixels, dx, destY, destTilesX);
                color = (*srcPixel >> ((sx & 1) * 4)) & 0xf;
                if (color != 0) {
                    color += paletteOffset;
                    *destPixel = color;
                }
            }
        }
    }
}

// VRAM only takes 16-bit writes, so a pixel's byte is written with its neighbor
static void GFL_BitmapCopyArea_IDX4_VRAM(const GFLBitmap *src, GFLBitmap *dest, u16 srcX, u16 srcY, s16 destX,
                                         s16 destY, u16 width, u16 height, u16 colorKey) {
    int sx;
    int sy;
    int dx;
    int dy;
    int xEnd;
    int yEnd;
    int srcTilesX;
    int destTilesX;
    u8 *srcPixel;
    u16 *destPixels;
    BOOL odd;
    u16 high;
    u16 low;
    int color;
    int shift;

    if (dest->width - destX < width) {
        xEnd = srcX + (dest->width - destX);
    } else {
        xEnd = width + srcX;
    }
    if (dest->height - destY < height) {
        yEnd = srcY + (dest->height - destY);
    } else {
        yEnd = height + srcY;
    }
    srcTilesX = PIXELS_TO_TILES(src->width);
    destTilesX = PIXELS_TO_TILES(dest->width);
    if (colorKey == GFL_BITMAP_NO_COLOR_KEY) {
        for (sy = srcY, dy = destY; sy < yEnd; sy++, dy++) {
            for (sx = srcX, dx = destX; sx < xEnd; sx++, dx++) {
                if (dx >= 0 && dy >= 0) {
                    srcPixel = PIXEL_ADDR_16(src->pixels, sx, sy, srcTilesX);
                    destPixels = (u16 *)PIXEL_ADDR_16(dest->pixels, dx, dy, destTilesX);
                    if ((uintptr_t)destPixels & 1) {
                        destPixels = (u16 *)((u8 *)destPixels - 1);
                        odd = TRUE;
                    } else {
                        odd = FALSE;
                    }
                    high = *destPixels >> 8;
                    low = *destPixels & 0xff;
                    color = (*srcPixel >> ((sx & 1) * 4)) & 0xf;
                    shift = (dx & 1) * 4;
                    if (!odd) {
                        *destPixels = ((color << shift) | (low & (0xf0 >> shift))) | (high << 8);
                    } else {
                        *destPixels = (((color << shift) | (high & (0xf0 >> shift))) << 8) | low;
                    }
                }
            }
        }
    } else {
        for (sy = srcY, dy = destY; sy < yEnd; sy++, dy++) {
            for (sx = srcX, dx = destX; sx < xEnd; sx++, dx++) {
                if (dx >= 0 && dy >= 0) {
                    srcPixel = PIXEL_ADDR_16(src->pixels, sx, sy, srcTilesX);
                    destPixels = (u16 *)PIXEL_ADDR_16(dest->pixels, dx, dy, destTilesX);
                    if ((uintptr_t)destPixels & 1) {
                        destPixels = (u16 *)((u8 *)destPixels - 1);
                        odd = TRUE;
                    } else {
                        odd = FALSE;
                    }
                    high = *destPixels >> 8;
                    low = *destPixels & 0xff;
                    color = (*srcPixel >> ((sx & 1) * 4)) & 0xf;
                    if (color != colorKey) {
                        shift = (dx & 1) * 4;
                        if (!odd) {
                            *destPixels = ((color << shift) | (low & (0xf0 >> shift))) | (high << 8);
                        } else {
                            *destPixels = (((color << shift) | (high & (0xf0 >> shift))) << 8) | low;
                        }
                    }
                }
            }
        }
    }
}

static void GFL_BitmapCopyArea_IDX8_VRAM(const GFLBitmap *src, GFLBitmap *dest, u16 srcX, u16 srcY, s16 destX,
                                         s16 destY, u16 width, u16 height, u16 colorKey) {
    int sx;
    int sy;
    int dx;
    int dy;
    int xEnd;
    int yEnd;
    int srcTilesX;
    int destTilesX;
    u8 *srcPixel;
    u8 *destPixel;
    u16 pixels;

    if (dest->width - destX < width) {
        xEnd = srcX + (dest->width - destX);
    } else {
        xEnd = width + srcX;
    }
    if (dest->height - destY < height) {
        yEnd = srcY + (dest->height - destY);
    } else {
        yEnd = height + srcY;
    }
    srcTilesX = PIXELS_TO_TILES(src->width);
    destTilesX = PIXELS_TO_TILES(dest->width);
    if (colorKey == GFL_BITMAP_NO_COLOR_KEY) {
        for (sy = srcY, dy = destY; sy < yEnd; sy++, dy++) {
            for (sx = srcX, dx = destX; sx < xEnd; sx++, dx++) {
                if (dx >= 0 && dy >= 0) {
                    srcPixel = PIXEL_ADDR_256(src->pixels, sx, sy, srcTilesX);
                    destPixel = PIXEL_ADDR_256(dest->pixels, dx, dy, destTilesX);
                    if ((uintptr_t)destPixel & 1) {
                        destPixel--;
                        pixels = (srcPixel[0] << 8) | srcPixel[-1];
                    } else {
                        pixels = srcPixel[0] | (srcPixel[1] << 8);
                    }
                    *(u16 *)destPixel = pixels;
                }
            }
        }
    } else {
        for (sy = srcY, dy = destY; sy < yEnd; sy++, dy++) {
            for (sx = srcX, dx = destX; sx < xEnd; sx++, dx++) {
                if (dx >= 0 && dy >= 0) {
                    srcPixel = PIXEL_ADDR_256(src->pixels, sx, sy, srcTilesX);
                    destPixel = PIXEL_ADDR_256(dest->pixels, dx, dy, destTilesX);
                    if ((uintptr_t)destPixel & 1) {
                        destPixel--;
                        pixels = (srcPixel[0] << 8) | srcPixel[-1];
                    } else {
                        pixels = srcPixel[0] | (srcPixel[1] << 8);
                    }
                    if (srcPixel[0] != colorKey) {
                        *(u16 *)destPixel = pixels;
                    }
                }
            }
        }
    }
}

static void GFL_BitmapFillArea_IDX4(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color) {
    int xEnd = x + width;
    int yEnd;
    int tilesX;
    int i, j;
    u8 *pixel;

    if (xEnd > bitmap->width) {
        xEnd = bitmap->width;
    }
    yEnd = y + height;
    if (yEnd > bitmap->height) {
        yEnd = bitmap->height;
    }
    tilesX = PIXELS_TO_TILES(bitmap->width);
    for (j = y; j < yEnd; j++) {
        for (i = x; i < xEnd; i++) {
            if (i >= 0 && j >= 0) {
                pixel = PIXEL_ADDR_16(bitmap->pixels, i, j, tilesX);
                if (i & 1) {
                    *pixel &= 0xf;
                    *pixel |= color << 4;
                } else {
                    *pixel &= 0xf0;
                    *pixel |= color;
                }
            }
        }
    }
}

static void GFL_BitmapFillArea_IDX8(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color) {
    int xEnd = x + width;
    int yEnd;
    int tilesX;
    int i, j;

    if (xEnd > bitmap->width) {
        xEnd = bitmap->width;
    }
    yEnd = y + height;
    if (yEnd > bitmap->height) {
        yEnd = bitmap->height;
    }
    tilesX = PIXELS_TO_TILES(bitmap->width);
    for (j = y; j < yEnd; j++) {
        for (i = x; i < xEnd; i++) {
            if (i >= 0 && j >= 0) {
                *PIXEL_ADDR_256(bitmap->pixels, i, j, tilesX) = color;
            }
        }
    }
}

static void GFL_BitmapFillArea_IDX4_VRAM(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color) {
    int xEnd = x + width;
    int yEnd;
    int tilesX;
    int i, j;
    u16 *pixels;
    BOOL odd;
    u16 high;
    u16 low;
    int shift;

    if (xEnd > bitmap->width) {
        xEnd = bitmap->width;
    }
    yEnd = y + height;
    if (yEnd > bitmap->height) {
        yEnd = bitmap->height;
    }
    tilesX = PIXELS_TO_TILES(bitmap->width);
    for (j = y; j < yEnd; j++) {
        for (i = x; i < xEnd; i++) {
            if (i >= 0 && j >= 0) {
                pixels = (u16 *)PIXEL_ADDR_16(bitmap->pixels, i, j, tilesX);
                if ((uintptr_t)pixels & 1) {
                    pixels = (u16 *)((u8 *)pixels - 1);
                    odd = TRUE;
                } else {
                    odd = FALSE;
                }
                high = *pixels >> 8;
                low = *pixels & 0xff;
                shift = (i & 1) * 4;
                if (!odd) {
                    *pixels = ((low & (0xf0 >> shift)) | (color << shift)) | (high << 8);
                } else {
                    *pixels = ((((0xf0 >> shift) & high) | (color << shift)) << 8) | low;
                }
            }
        }
    }
}

// The same as GFL_BitmapFillArea_IDX8, as a write of a byte to VRAM is lost
static void GFL_BitmapFillArea_IDX8_VRAM(GFLBitmap *bitmap, s16 x, s16 y, u16 width, u16 height, u8 color) {
    int xEnd = x + width;
    int yEnd;
    int tilesX;
    int i, j;

    if (xEnd > bitmap->width) {
        xEnd = bitmap->width;
    }
    yEnd = y + height;
    if (yEnd > bitmap->height) {
        yEnd = bitmap->height;
    }
    tilesX = PIXELS_TO_TILES(bitmap->width);
    for (j = y; j < yEnd; j++) {
        for (i = x; i < xEnd; i++) {
            if (i >= 0 && j >= 0) {
                *PIXEL_ADDR_256(bitmap->pixels, i, j, tilesX) = color;
            }
        }
    }
}

GFLBitmap *GFL_BitmapMakeLinear(GFLBitmap *bitmap, BOOL keepAsNew, HeapID heapId) {
    u16 tileSize = bitmap->tileSize;
    u16 bytesPerTileRow = tileSize / 8;
    u16 tilesY = bitmap->height / 8;
    u16 tilesX = bitmap->width / 8;
    GFLBitmap *linear = GFL_BitmapCreate(tilesX, tilesY, tileSize, heapId);
    int byteIndex;

    for (byteIndex = 0; byteIndex < tilesX * tilesY * tileSize; byteIndex++) {
        int y = byteIndex / (tilesX * bytesPerTileRow);
        int rowByteOffset = byteIndex % (tilesX * bytesPerTileRow);
        int byteInTileRow = rowByteOffset % bytesPerTileRow;
        int tileColumn = rowByteOffset / bytesPerTileRow;

        linear->pixels[byteIndex] =
            bitmap->pixels[byteInTileRow + (y % 8) * bytesPerTileRow + (tileColumn + (y / 8) * tilesX) * tileSize];
    }
    if (keepAsNew == FALSE) {
        GFL_BitmapCopy(linear, bitmap);
        GFL_BitmapFree(linear);
        linear = NULL;
    }
    return linear;
}
