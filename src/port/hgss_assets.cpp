#include "port/hgss_assets.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

extern "C" {
#include "overworld_pokemon_assets.h"
}

namespace {
using Bytes = std::vector<unsigned char>;
constexpr const char *kBundle = "hgss/following_pokemon.bin";
Bytes sTextures;
bool sChecked;

unsigned U16(const Bytes &b, size_t p)
{
    if (p > b.size() || b.size() - p < 2) throw std::runtime_error("Truncated HG/SS data.");
    return b[p] | (unsigned(b[p + 1]) << 8);
}
unsigned U32(const Bytes &b, size_t p)
{
    return U16(b, p) | (U16(b, p + 2) << 16);
}
Bytes Read(std::ifstream &in, unsigned offset, unsigned size)
{
    // No needed archive or filesystem table exceeds this limit.
    if (size > 64 * 1024 * 1024) throw std::runtime_error("Invalid HG/SS file size.");
    in.clear();
    in.seekg(0, std::ios::end);
    auto length = in.tellg();
    if (length < 0 || offset > static_cast<unsigned long long>(length)
        || size > static_cast<unsigned long long>(length) - offset)
        throw std::runtime_error("Truncated HG/SS ROM.");
    Bytes data(size);
    in.seekg(offset);
    if (!in.read(reinterpret_cast<char *>(data.data()), size)) throw std::runtime_error("Cannot read HG/SS data.");
    return data;
}
unsigned TextureSize(unsigned i)
{
    return OverworldPokemonAssets_GetResourceSize(OVERWORLD_POKEMON_RESOURCE_BASE + i);
}
bool ValidBundle(const Bytes &data)
{
    size_t offset = 0;
    for (unsigned i = 0; i < OVERWORLD_POKEMON_VARIANTS; ++i) {
        unsigned size = TextureSize(i);
        if (size < 20 || offset > data.size() || size > data.size() - offset
            || std::memcmp(data.data() + offset, "BTX0", 4) || U32(data, offset + 8) != size) return false;
        offset += size;
    }
    return offset == data.size();
}
// Resolve one component of the ROM's NitroFS file name table.
int Find(const Bytes &fnt, int dir, const char *name, bool directory)
{
    if (dir < 0xf000 || dir > 0xffff) return -1;
    size_t entry = (dir - 0xf000) * 8;
    unsigned pos = U32(fnt, entry), id = U16(fnt, entry + 4);
    while (pos < fnt.size()) {
        unsigned tag = fnt[pos++], len = tag & 127;
        if (!len) break;
        bool isDir = tag & 128;
        if (len + (isDir ? 2 : 0) > fnt.size() - pos) break;
        if (isDir == directory && std::strlen(name) == len && !std::memcmp(fnt.data() + pos, name, len))
            return isDir ? U16(fnt, pos + len) : id;
        pos += len;
        if (isDir) pos += 2;
        else ++id;
    }
    return -1;
}
Bytes Extract(std::ifstream &rom)
{
    Bytes h = Read(rom, 0, 0x50);
    if (std::memcmp(h.data() + 12, "IPK", 3) && std::memcmp(h.data() + 12, "IPG", 3))
        throw std::runtime_error("Select a HeartGold or SoulSilver .nds ROM.");
    Bytes fnt = Read(rom, U32(h, 0x40), U32(h, 0x44));
    Bytes fat = Read(rom, U32(h, 0x48), U32(h, 0x4c));
    int dir = 0xf000;
    for (const char *part : {"a", "0", "8"}) dir = Find(fnt, dir, part, true);
    int id = Find(fnt, dir, "1", false); // HG/SS's mmodel.narc
    if (id < 0) throw std::runtime_error("HG/SS overworld archive is missing.");
    unsigned start = U32(fat, size_t(id) * 8), end = U32(fat, size_t(id) * 8 + 4);
    if (end < start) throw std::runtime_error("Invalid HG/SS archive range.");
    Bytes narc = Read(rom, start, end - start);
    if (narc.size() < 16 || std::memcmp(narc.data(), "NARC", 4) || U32(narc, 8) != narc.size())
        throw std::runtime_error("Invalid HG/SS overworld archive.");
    size_t table = 0, images = 0, imageSize = 0;
    unsigned count = 0;
    size_t pos = U16(narc, 12);
    if (pos < 16) throw std::runtime_error("Invalid HG/SS archive header.");
    for (unsigned block = 0; block < U16(narc, 14); ++block) {
        unsigned size = U32(narc, pos + 4);
        if (size < 8 || pos > narc.size() || size > narc.size() - pos)
            throw std::runtime_error("Invalid HG/SS archive block.");
        if (!std::memcmp(narc.data() + pos, "BTAF", 4)) {
            count = U16(narc, pos + 8);
            table = pos + 12;
            if (size < 12 || size_t(count) * 8 > size - 12) throw std::runtime_error("Invalid HG/SS member table.");
        } else if (!std::memcmp(narc.data() + pos, "GMIF", 4)) {
            images = pos + 8;
            imageSize = size - 8;
        }
        pos += size;
    }
    if (!table || !images || count < 297 + OVERWORLD_POKEMON_VARIANTS)
        throw std::runtime_error("HG/SS follower textures are missing.");
    Bytes bundle;
    for (unsigned i = 0; i < OVERWORLD_POKEMON_VARIANTS; ++i) {
        size_t entry = table + (297 + i) * 8;
        unsigned begin = U32(narc, entry), end = U32(narc, entry + 4);
        if (end < begin || end > imageSize || end - begin != TextureSize(i))
            throw std::runtime_error("Unsupported or damaged HG/SS follower textures.");
        bundle.insert(bundle.end(), narc.begin() + images + begin, narc.begin() + images + end);
    }
    if (!ValidBundle(bundle)) throw std::runtime_error("Invalid HG/SS follower textures.");
    return bundle;
}
}

extern "C" int HgssAssets_Ready(void)
{
    if (!sChecked) {
        sChecked = true;
        try {
            std::ifstream file(kBundle, std::ios::binary);
            if (file) {
                file.seekg(0, std::ios::end);
                auto size = file.tellg();
                if (size > 0 && size <= 64 * 1024 * 1024) {
                    Bytes data = Read(file, 0, static_cast<unsigned>(size));
                    if (ValidBundle(data)) sTextures = std::move(data);
                }
            }
        } catch (const std::exception &) { sTextures.clear(); }
    }
    return !sTextures.empty();
}

extern "C" int HgssAssets_ReadTexture(unsigned offset, unsigned size, void *dest)
{
    if (!HgssAssets_Ready() || offset > sTextures.size() || size > sTextures.size() - offset) return 0;
    std::memcpy(dest, sTextures.data() + offset, size);
    return 1;
}

extern "C" const char *HgssAssets_Extract(const char *romPath)
{
    static std::string message;
    const std::filesystem::path temp = std::string(kBundle) + ".tmp";
    try {
        std::ifstream rom(romPath, std::ios::binary);
        if (!rom) throw std::runtime_error("Cannot open the HG/SS ROM.");
        Bytes bundle = Extract(rom);
        std::filesystem::create_directories("hgss");
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bundle.data()), bundle.size());
            out.close();
            if (!out) throw std::runtime_error("Cannot write HG/SS graphics to the game folder.");
        }
        // Windows rename does not replace an existing destination. Keep the old
        // bundle as a backup until the complete replacement has been installed.
        const std::filesystem::path backup = std::string(kBundle) + ".bak";
        bool hadBundle = std::filesystem::exists(kBundle);
        if (hadBundle) std::filesystem::rename(kBundle, backup);
        try { std::filesystem::rename(temp, kBundle); }
        catch (...) {
            if (hadBundle) std::filesystem::rename(backup, kBundle);
            throw;
        }
        std::error_code ignored;
        if (hadBundle) std::filesystem::remove(backup, ignored);
        sTextures = std::move(bundle);
        sChecked = true;
        return "HG/SS overworld graphics extracted. Both options are available.";
    } catch (const std::exception &error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        message = error.what();
        return message.c_str();
    }
}
