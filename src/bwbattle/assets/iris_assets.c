// Optional native-view resources, derived in memory from the player's BW2
// extraction. No BW2 artwork or audio is compiled into Platinum's resources.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bwbattle.h"

typedef unsigned char Byte;
typedef struct {
    Byte *data;
    unsigned size;
} Resource;
static Resource sResources[12];
static bool sReady;
static const char *sField = "data/mmodel/mmodel.narc";
static const char *sTrainer = "poketool/trgra/trfgra.narc";
static const char *sVs = "graphic/field_encounteffect.narc";
static const char *sPokemon = "poketool/pokegra/pl_otherpoke.narc";

static unsigned R16(const Byte *p)
{
    return p[0] | p[1] << 8;
}
static unsigned R32(const Byte *p)
{
    return R16(p) | (uint32_t)R16(p + 2) << 16;
}
static void W16(Byte *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static void W32(Byte *p, unsigned v)
{
    W16(p, v);
    W16(p + 2, v >> 16);
}
static bool Exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

// Read only the requested member, including bounds checks for optional files.
static Resource Member(const char *path, unsigned index)
{
    Resource out = { 0 };
    FILE *f = fopen(path, "rb");
    Byte h[16], entry[8];
    if (!f) {
        return out;
    }
    if (fread(h, 1, 16, f) != 16 || memcmp(h, "NARC", 4)) {
        goto done;
    }
    unsigned total = R32(h + 8), pos = R16(h + 12), start = 0, end = 0;
    bool found = false;
    while (pos <= total && total - pos >= 8) {
        if (fseek(f, pos, SEEK_SET) || fread(h, 1, 8, f) != 8) {
            goto done;
        }
        unsigned size = R32(h + 4);
        if (size < 8 || size > total - pos) {
            goto done;
        }
        if (!memcmp(h, "BTAF", 4)) {
            if (fread(h + 8, 1, 4, f) != 4 || index >= R16(h + 8)
                || 12 + (uint64_t)(index + 1) * 8 > size) {
                goto done;
            }
            if (fseek(f, pos + 12 + index * 8, SEEK_SET) || fread(entry, 1, 8, f) != 8) {
                goto done;
            }
            start = R32(entry);
            end = R32(entry + 4);
            found = true;
        } else if (!memcmp(h, "GMIF", 4) && found) {
            if (end <= start || end > size - 8 || end - start > 0x100000) {
                goto done;
            }
            out.size = end - start;
            out.data = malloc(out.size);
            if (!out.data || fseek(f, pos + 8 + start, SEEK_SET) || fread(out.data, 1, out.size, f) != out.size) {
                free(out.data);
                out = (Resource) { 0 };
            }
            break;
        }
        pos += size;
    }
done:
    fclose(f);
    return out;
}

// BW2 character members use either Nitro LZ10 or LZ11.
static Resource Unpack(Resource in)
{
    if (!in.data || in.size < 4 || (in.data[0] != 0x10 && in.data[0] != 0x11)) {
        return in;
    }
    unsigned length = R32(in.data) >> 8, p = 4, n = 0;
    Resource out = { NULL, length };
    if (!length || length > 0x100000 || !(out.data = malloc(length))) {
        goto fail;
    }
    while (n < length && p < in.size) {
        unsigned flags = in.data[p++];
        for (unsigned bit = 128; bit && n < length; bit >>= 1) {
            if (p >= in.size) {
                goto fail;
            }
            if (!(flags & bit)) {
                out.data[n++] = in.data[p++];
                continue;
            }
            unsigned a = in.data[p++], count = a >> 4;
            if (in.data[0] == 0x10) {
                count += 3;
            } else if (count > 1) {
                count++;
            } else {
                bool wide = count == 1;
                count = (a & 15) << 4;
                if (p >= in.size) {
                    goto fail;
                }
                if (wide) {
                    count = (count << 8) + (in.data[p++] << 4) + 0x100;
                    if (p >= in.size) {
                        goto fail;
                    }
                }
                a = in.data[p++];
                count += 0x11 + (a >> 4);
            }
            if (p >= in.size) {
                goto fail;
            }
            unsigned distance = ((a & 15) << 8) + in.data[p++] + 1;
            if (distance > n || count > length - n) {
                goto fail;
            }
            while (count--) {
                out.data[n] = out.data[n - distance];
                n++;
            }
        }
    }
    if (n != length) {
        goto fail;
    }
    free(in.data);
    return out;
fail:
    free(in.data);
    free(out.data);
    return (Resource) { 0 };
}

static Resource Character(unsigned bytes, unsigned width, unsigned height, bool scan)
{
    Resource r = { calloc(1, bytes + 48), bytes + 48 };
    if (!r.data) {
        return (Resource) { 0 };
    }
    memcpy(r.data, "RGCN\xff\xfe\x00\x01", 8);
    W32(r.data + 8, r.size);
    W16(r.data + 12, 16);
    W16(r.data + 14, 1);
    memcpy(r.data + 16, "RAHC", 4);
    W32(r.data + 20, bytes + 32);
    W16(r.data + 24, height);
    W16(r.data + 26, width);
    W32(r.data + 28, 3);
    W32(r.data + 36, scan ? 1 : 0);
    W32(r.data + 40, bytes);
    W32(r.data + 44, 24);
    return r;
}
static void Encrypt(Byte *raw, unsigned size)
{
    // Platinum's scan sprites use the first encrypted word as the RNG seed.
    uint32_t seed = 0;
    W16(raw, 0);
    for (unsigned i = 0; i < size; i += 2) {
        W16(raw + i, R16(raw + i) ^ seed);
        seed = seed * 1103515245u + 24691;
    }
}
static unsigned Pixel(const Byte *raw, unsigned width, unsigned x, unsigned y)
{
    unsigned p = ((y / 8) * (width / 8) + x / 8) * 64 + (y % 8) * 8 + x % 8;
    return raw[p / 2] >> ((p & 1) * 4) & 15;
}
static void PutPixel(Byte *raw, unsigned width, unsigned x, unsigned y, unsigned color)
{
    unsigned p = y * width + x;
    raw[p / 2] |= color << ((p & 1) * 4);
}
static Resource Palette(const char *arc, unsigned member)
{
    Resource r = Unpack(Member(arc, member));
    if (r.size < 72 || memcmp(r.data, "RLCN", 4)) {
        free(r.data);
        return (Resource) { 0 };
    }
    return r;
}

static Resource Overworld(void)
{
    Resource base = Member(sField, 470), bw = Member(BWBATTLE_ASSET_DIR "/a/0/4/8", 276), out = { 0 };
    if (base.size < 80 || bw.size < 80 || memcmp(base.data, "BTX0", 4) || memcmp(bw.data, "BTX0", 4)) {
        goto done;
    }
    unsigned bt = R32(base.data + 16), st = R32(bw.data + 16);
    if (bt > base.size - 60 || st > bw.size - 60) {
        goto done;
    }
    if (R32(base.data + bt + 20) > base.size - bt || R32(bw.data + st + 20) > bw.size - st
        || R32(bw.data + st + 56) > bw.size - st || R16(bw.data + st + 14) > bw.size - st
        || R16(base.data + bt + 14) > base.size - bt) {
        goto done;
    }
    unsigned dst = bt + R32(base.data + bt + 20), src = st + R32(bw.data + st + 20);
    unsigned pal = st + R32(bw.data + st + 56);
    unsigned dict = st + R16(bw.data + st + 14), bd = bt + R16(base.data + bt + 14);
    if (bw.size - dict < 8 || base.size - bd < 8 || bw.data[dict + 1] < 12 || base.data[bd + 1] != 16) {
        goto done;
    }
    if (R16(bw.data + dict + 6) > bw.size - dict - 4 || R16(base.data + bd + 6) > base.size - bd - 4) {
        goto done;
    }
    unsigned entries = dict + R16(bw.data + dict + 6) + 4;
    unsigned be = bd + R16(base.data + bd + 6) + 4;
    if (entries + 12 * 8 > bw.size || be + 16 * 8 > dst || dst > base.size || pal + 32 > bw.size) {
        goto done;
    }
    out.size = dst + 16 * 512 + 32;
    out.data = calloc(1, out.size);
    if (!out.data) {
        out.size = 0;
        goto done;
    }
    memcpy(out.data, base.data, dst);
    // Platinum orders directions north, south, west, east; Iris's BW2 sheet
    // orders them north, west, east, south. Expand each three-frame walk to four.
    static const Byte frames[] = { 0, 1, 0, 2, 9, 10, 9, 11, 3, 4, 3, 5, 6, 7, 6, 8 };
    for (unsigned i = 0; i < 16; i++) {
        unsigned offset = src + R16(bw.data + entries + frames[i] * 8) * 8;
        if (offset + 512 > bw.size) {
            free(out.data);
            out = (Resource) { 0 };
            goto done;
        }
        memcpy(out.data + dst + i * 512, bw.data + offset, 512);
        W16(out.data + be + i * 8, i * 64);
    }
    memcpy(out.data + dst + 8192, bw.data + pal, 32);
    W32(out.data + 8, out.size);
    W32(out.data + bt + 4, out.size - bt);
    W16(out.data + bt + 12, 1024);
    W32(out.data + bt + 36, dst + 8192 - bt);
    W32(out.data + bt + 40, dst + 8192 - bt);
    W16(out.data + bt + 48, 4);
    W32(out.data + bt + 56, dst + 8192 - bt);
done:
    free(base.data);
    free(bw.data);
    return out;
}

static Resource Hydreigon(unsigned face)
{
    Resource bw = Unpack(Member(BWBATTLE_ASSET_DIR "/a/0/0/4", 635 * 20 + (face ? 0 : 9))), out = { 0 };
    if (bw.size < 48 + 4608 || memcmp(bw.data, "RGCN", 4)) {
        goto done;
    }
    Byte tiles[4608] = { 0 };
    const Byte *raw = bw.data + 48;
    unsigned off = 0;
    for (unsigned y = 0; y < 8; y++, off += 256) {
        memcpy(tiles + y * 384, raw + off, 256);
    }
    for (unsigned y = 0; y < 8; y++, off += 128) {
        memcpy(tiles + (y * 12 + 8) * 32, raw + off, 128);
    }
    for (unsigned y = 0; y < 4; y++, off += 256) {
        memcpy(tiles + y * 384 + 3072, raw + off, 256);
    }
    for (unsigned y = 0; y < 4; y++, off += 128) {
        memcpy(tiles + (y * 12 + 104) * 32, raw + off, 128);
    }
    unsigned x0 = 96, y0 = 96, x1 = 0, y1 = 0;
    for (unsigned y = 0; y < 96; y++) {
        for (unsigned x = 0; x < 96; x++) {
            if (Pixel(tiles, 96, x, y)) {
                if (x < x0) {
                    x0 = x;
                }
                if (y < y0) {
                    y0 = y;
                }
                if (x + 1 > x1) {
                    x1 = x + 1;
                }
                if (y + 1 > y1) {
                    y1 = y + 1;
                }
            }
        }
    }
    if (x0 >= x1 || y0 >= y1) {
        goto done;
    }
    unsigned w = x1 - x0, h = y1 - y0, dw = w, dh = h;
    if (w > 80 || h > 80) {
        if (w >= h) {
            dw = 80;
            dh = (h * 80 + w / 2) / w;
        } else {
            dh = 80;
            dw = (w * 80 + h / 2) / h;
        }
    }
    out = Character(6400, 20, 10, true);
    if (!out.data) {
        goto done;
    }
    for (unsigned y = 0; y < dh; y++) {
        for (unsigned x = 0; x < dw; x++) {
            unsigned color = Pixel(tiles, 96, x0 + ((2 * x + 1) * w) / (2 * dw), y0 + ((2 * y + 1) * h) / (2 * dh));
            PutPixel(out.data + 48, 160, (80 - dw) / 2 + x, 80 - dh + y, color);
            PutPixel(out.data + 48, 160, 80 + (80 - dw) / 2 + x, 80 - dh + y, color);
        }
    }
    Encrypt(out.data + 48, 6400);
done:
    free(bw.data);
    return out;
}

static Resource Trainer(bool scan)
{
    Resource bw = Unpack(Member(BWBATTLE_ASSET_DIR "/a/0/7/1", 155 * 8)), out = { 0 };
    if (bw.size < 3248 || memcmp(bw.data, "RGCN", 4)) {
        goto done;
    }
    if (!scan) {
        out = Member(sTrainer, 525);
        if (out.size < 16048 || memcmp(out.data, "RGCN", 4)) {
            free(out.data);
            out = (Resource) { 0 };
            goto done;
        }
        for (unsigned i = 0; i < 5; i++) {
            memcpy(out.data + 48 + i * 3200, bw.data + 48, 3200);
        }
    } else {
        Byte tiles[3200] = { 0 };
        const Byte *b = bw.data + 48;
        for (unsigned y = 0; y < 8; y++) {
            memcpy(tiles + y * 320, b + y * 256, 256);
            memcpy(tiles + y * 320 + 256, b + 2048 + y * 64, 64);
        }
        for (unsigned y = 0; y < 2; y++) {
            memcpy(tiles + (y + 8) * 320, b + 2560 + y * 128, 128);
            memcpy(tiles + (y + 8) * 320 + 128, b + 2816 + y * 128, 128);
            memcpy(tiles + (y + 8) * 320 + 256, b + 3072 + y * 64, 64);
        }
        out = Character(6400, 20, 10, true);
        if (!out.data) {
            goto done;
        }
        for (unsigned y = 0; y < 80; y++) {
            for (unsigned x = 0; x < 80; x++) {
                unsigned c = Pixel(tiles, 80, x, y);
                PutPixel(out.data + 48, 160, x, y, c);
                PutPixel(out.data + 48, 160, x + 80, y, c);
            }
        }
        Encrypt(out.data + 48, 6400);
    }
done:
    free(bw.data);
    return out;
}

bool BwBattle_IrisAssetsReady(void)
{
    // A partial/older extraction must not expose an encounter whose assets fail.
    if (!Exists(BWBATTLE_ASSET_DIR "/a/0/4/8") || !Exists(BWBATTLE_SOUND_ARCHIVE) || !BwBattle_AssetsReady()) {
        return false;
    }
    if (sReady) {
        return true;
    }
    sResources[0] = Overworld();
    sResources[1] = Trainer(false);
    sResources[2] = Palette(BWBATTLE_ASSET_DIR "/a/0/7/1", 155 * 8 + 7);
    sResources[3] = Trainer(true);
    sResources[4] = Hydreigon(0);
    sResources[5] = Hydreigon(1);
    sResources[6] = Palette(BWBATTLE_ASSET_DIR "/a/0/0/4", 635 * 20 + 18);
    sResources[7] = Palette(BWBATTLE_ASSET_DIR "/a/0/0/4", 635 * 20 + 19);
    for (unsigned i = 0; i < 8; i++) {
        if (!sResources[i].data) {
            for (unsigned j = 0; j < 8; j++) {
                free(sResources[j].data);
                sResources[j] = (Resource) { 0 };
            }
            return false;
        }
    }
    sReady = true;
    return true;
}

// The BW2 mugshot archive has Iris's portrait at 13 and its palette at 66.
// Reframe its 128x128 art for Platinum's 96x64 Champion cut-in, preserving the
// existing two-cell OAM layout (64x64 followed by 32x64).
static Resource VsPortrait(void)
{
    Resource bw = Unpack(Member(BWBATTLE_ASSET_DIR "/a/2/6/7", 13));
    Resource out = { 0 };
    if (bw.size < 48 + 8192 || memcmp(bw.data, "RGCN", 4)) {
        goto done;
    }
    out = Member(sVs, 104);
    if (out.size != 48 + 3072 || memcmp(out.data, "RGCN", 4)) {
        free(out.data);
        out = (Resource) { 0 };
        goto done;
    }
    memset(out.data + 48, 0, 3072);
    for (unsigned y = 0; y < 64; y++) {
        for (unsigned x = 0; x < 96; x++) {
            unsigned color = Pixel(bw.data + 48, 128, x * 4 / 3, (y + 16) * 4 / 3);
            unsigned tile = x < 64 ? (y / 8) * 8 + x / 8 : 64 + (y / 8) * 4 + (x - 64) / 8;
            unsigned pixel = tile * 64 + (y % 8) * 8 + x % 8;
            out.data[48 + pixel / 2] |= color << ((pixel & 1) * 4);
        }
    }
done:
    free(bw.data);
    return out;
}

bool BwBattle_IrisVsReady(void)
{
    if (!BwBattle_IrisAssetsReady() || !Exists(BWBATTLE_ASSET_DIR "/a/2/6/7")) {
        return false;
    }
    if (sResources[8].data) {
        return true;
    }
    sResources[8] = Palette(BWBATTLE_ASSET_DIR "/a/2/6/7", 66);
    sResources[9] = VsPortrait();
    sResources[10] = Member(sVs, 105);
    sResources[11] = Member(sVs, 106);
    for (unsigned i = 8; i < 12; i++) {
        if (!sResources[i].data) {
            for (unsigned j = 8; j < 12; j++) {
                free(sResources[j].data);
                sResources[j] = (Resource) { 0 };
            }
            return false;
        }
    }
    return true;
}

void *BwBattle_ReadMember(const char *path, unsigned member, unsigned *size)
{
    Resource resource = Unpack(Member(path, member));
    if (size) {
        *size = resource.size;
    }
    return resource.data;
}

const void *BwBattle_IrisResource(const char *path, unsigned member, unsigned *size)
{
    int index = -1;
    if (!strcmp(path, "poketool/icongra/pl_poke_icon.narc")) {
        return BwMonIcons_Resource(member, size);
    }
    if (!strcmp(path, sVs) && member >= BWBATTLE_IRIS_VS_PALETTE && member <= BWBATTLE_IRIS_VS_ANIM) {
        if (!BwBattle_IrisVsReady()) {
            return NULL;
        }
        index = 8 + member - BWBATTLE_IRIS_VS_PALETTE;
        *size = sResources[index].size;
        return sResources[index].data;
    }
    if (!strcmp(path, sField) && member == 470) {
        index = 0;
    } else if (!strcmp(path, sTrainer)) {
        if (member == 525) {
            index = 1;
        } else if (member == 526) {
            index = 2;
        } else if (member == 529) {
            index = 3;
        }
    } else if (!strcmp(path, sPokemon) && member >= 253 && member <= 256) {
        index = 4 + member - 253;
    }
    if (index < 0 || !BwBattle_IrisAssetsReady()) {
        return NULL;
    }
    *size = sResources[index].size;
    return sResources[index].data;
}
