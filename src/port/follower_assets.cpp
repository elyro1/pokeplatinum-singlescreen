#include "port/follower_assets.h"

#include <atomic>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "bwbattle/bwbattle.h"

extern "C" {
#include "overworld_pokemon_assets.h"
}

namespace {
using Bytes = std::vector<unsigned char>;

struct FollowerBw2Sprite {
    unsigned short species, sex, form;
    unsigned short shiny[16];
};
#include "follower_bw2_data.inc"

// BW2's walking Pokémon: a/2/0/8 maps species, sex and form to an object code, a/0/4/7 holds the
// object codes' records, a/0/4/8 their textures (the pokebw2 decomp's tpoke_data.c and field_actor.h)
constexpr const char *kTPoke = BWBATTLE_ASSET_DIR "/a/2/0/8";
constexpr const char *kModelTable = BWBATTLE_ASSET_DIR "/a/0/4/7";
constexpr const char *kModelGraphics = BWBATTLE_ASSET_DIR "/a/0/4/8";
constexpr unsigned kTPokeSexAny = 2;
constexpr unsigned kPokemonObjCodeFirst = 0x1000, kPokemonObjCodeEnd = 0x126c;
// GetIndexOfObjID: the Pokémon's records follow the 0x179 NPC records
constexpr unsigned kPokemonRecordBase = 0x179;
constexpr unsigned kRecordSize = 28, kRecordTextureOffset = 16;
constexpr unsigned kFrames = 8;

Bytes sTextures;
bool sChecked;
std::atomic<bool> sReload;

unsigned U16(const Bytes &b, size_t p)
{
    if (p > b.size() || b.size() - p < 2) throw std::runtime_error("truncated");
    return b[p] | (unsigned(b[p + 1]) << 8);
}
unsigned U32(const Bytes &b, size_t p)
{
    return U16(b, p) | (U16(b, p + 2) << 16);
}

Bytes ReadFile(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("missing");
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) throw std::runtime_error("size");
    Bytes data(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(data.data()), size)) throw std::runtime_error("read");
    return data;
}

// The members of a NARC, as (offset, size) into its image block
struct Narc {
    Bytes data;
    size_t images = 0, imageSize = 0, table = 0;
    unsigned count = 0;

    explicit Narc(const char *path) : data(ReadFile(path))
    {
        if (data.size() < 16 || std::memcmp(data.data(), "NARC", 4)) throw std::runtime_error("narc");
        size_t pos = U16(data, 12);
        for (unsigned block = 0; block < U16(data, 14); ++block) {
            unsigned size = U32(data, pos + 4);
            if (size < 8 || size > data.size() - pos) throw std::runtime_error("narc");
            if (!std::memcmp(data.data() + pos, "BTAF", 4)) {
                count = U16(data, pos + 8);
                table = pos + 12;
                if (size < 12 || size_t(count) * 8 > size - 12) throw std::runtime_error("narc");
            } else if (!std::memcmp(data.data() + pos, "GMIF", 4)) {
                images = pos + 8;
                imageSize = size - 8;
            }
            pos += size;
        }
        if (!table || !images) throw std::runtime_error("narc");
    }

    Bytes Member(unsigned i) const
    {
        if (i >= count) throw std::runtime_error("member");
        unsigned begin = U32(data, table + i * 8), end = U32(data, table + i * 8 + 4);
        if (end < begin || end > imageSize) throw std::runtime_error("member");
        return Bytes(data.begin() + images + begin, data.begin() + images + end);
    }
};

void Mirror(unsigned char *frame, unsigned size)
{
    // 4bpp rows, low nibble first
    for (unsigned y = 0; y < size; ++y) {
        unsigned char *row = frame + y * size / 2;
        for (unsigned x = 0; x < size / 2; ++x) {
            unsigned a = x, b = size - 1 - x;
            unsigned pa = row[a / 2] >> (a & 1) * 4 & 15, pb = row[b / 2] >> (b & 1) * 4 & 15;
            row[a / 2] = (row[a / 2] & ~(15 << (a & 1) * 4)) | pb << (a & 1) * 4;
            row[b / 2] = (row[b / 2] & ~(15 << (b & 1) * 4)) | pa << (b & 1) * 4;
        }
    }
}

// BW2's sprite has six frames (west mirrored for east) or eight, and the normal palette only
void AppendTexture(Bytes &out, const Bytes &btx, const FollowerBw2Sprite &sprite, unsigned fileSize)
{
    const bool large = fileSize == TEXTURE_FILE_SIZE_64;
    if (!large && fileSize != TEXTURE_FILE_SIZE_32) throw std::runtime_error("layout");
    const unsigned size = large ? 64 : 32, frameBytes = size * size / 2;
    const unsigned char *header = large ? sTextureHeader64 : sTextureHeader32;
    const size_t headerSize = large ? sizeof(sTextureHeader64) : sizeof(sTextureHeader32);

    if (btx.size() < 0x14 + 0x3c || std::memcmp(btx.data(), "BTX0", 4)) throw std::runtime_error("texture");
    size_t tex = U32(btx, 16);
    if (tex > btx.size() - 0x3c || std::memcmp(btx.data() + tex, "TEX0", 4)) throw std::runtime_error("texture");
    size_t texDict = tex + U16(btx, tex + 14), texData = tex + U32(btx, tex + 20);
    size_t palDict = tex + U16(btx, tex + 52), palData = tex + U32(btx, tex + 56);
    if (texDict + 8 > btx.size() || palDict + 8 > btx.size()) throw std::runtime_error("texture");
    unsigned frames = btx[texDict + 1];
    if ((frames != 6 && frames != kFrames) || btx[palDict + 1] < 1) throw std::runtime_error("texture");
    size_t texEntries = texDict + U16(btx, texDict + 6) + 4, palEntries = palDict + U16(btx, palDict + 6) + 4;

    size_t start = out.size();
    out.insert(out.end(), header, header + headerSize);
    for (unsigned i = 0; i < kFrames; ++i) {
        unsigned source = i < frames ? i : i - 2;
        unsigned param = U32(btx, texEntries + source * 8);
        unsigned width = 8u << (param >> 20 & 7), height = 8u << (param >> 23 & 7);
        size_t offset = texData + ((param & 0xffff) << 3);
        if (width != size || height != size || (param >> 26 & 7) != 3 || offset > btx.size()
            || btx.size() - offset < frameBytes)
            throw std::runtime_error("frame");
        out.insert(out.end(), btx.begin() + offset, btx.begin() + offset + frameBytes);
        if (source != i) Mirror(out.data() + out.size() - frameBytes, size);
    }
    size_t palette = palData + (U16(btx, palEntries) << 3);
    if (palette > btx.size() || btx.size() - palette < 32) throw std::runtime_error("palette");
    out.insert(out.end(), btx.begin() + palette, btx.begin() + palette + 32);
    for (unsigned short colour : sprite.shiny) {
        out.push_back(colour & 0xff);
        out.push_back(colour >> 8);
    }
    if (out.size() - start != fileSize) throw std::runtime_error("layout");
}

Bytes Convert()
{
    static_assert(sizeof(sBw2Sprites) / sizeof(sBw2Sprites[0]) == OVERWORLD_POKEMON_VARIANTS);
    Narc tpokeArc(kTPoke), tableArc(kModelTable), graphics(kModelGraphics);
    Bytes tpoke = tpokeArc.Member(0), table = tableArc.Member(0);
    Bytes bundle;
    for (unsigned i = 0; i < OVERWORLD_POKEMON_VARIANTS; ++i) {
        const FollowerBw2Sprite &sprite = sBw2Sprites[i];
        // GetFieldPokemonMMdlLUTIndex_
        unsigned objCode = 0;
        for (size_t e = 0; e + 8 <= tpoke.size(); e += 8) {
            unsigned sex = U16(tpoke, e + 2);
            if (U16(tpoke, e) == sprite.species && (sex == kTPokeSexAny || sex == sprite.sex)
                && U16(tpoke, e + 4) == sprite.form) {
                objCode = U16(tpoke, e + 6);
                break;
            }
        }
        if (objCode < kPokemonObjCodeFirst || objCode >= kPokemonObjCodeEnd) throw std::runtime_error("species");
        size_t record = 4 + (objCode - kPokemonObjCodeFirst + kPokemonRecordBase) * kRecordSize;
        AppendTexture(bundle, graphics.Member(U16(table, record + kRecordTextureOffset)), sprite,
            OverworldPokemonAssets_GetResourceSize(OVERWORLD_POKEMON_RESOURCE_BASE + i));
    }
    return bundle;
}
}

extern "C" int FollowerAssets_Ready(void)
{
    if (sReload.exchange(false)) sChecked = false;
    if (!sChecked) {
        sChecked = true;
        try {
            sTextures = Convert();
        } catch (const std::exception &) {
            sTextures.clear();
        }
    }
    return !sTextures.empty();
}

extern "C" int FollowerAssets_ReadTexture(unsigned offset, unsigned size, void *dest)
{
    if (!FollowerAssets_Ready() || offset > sTextures.size() || size > sTextures.size() - offset) return 0;
    std::memcpy(dest, sTextures.data() + offset, size);
    return 1;
}

extern "C" void FollowerAssets_Reload(void)
{
    sReload = true;
}
