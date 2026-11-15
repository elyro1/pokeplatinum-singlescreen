// Apply embedded resource deltas to the extracted ROM filesystem.
#include "port/map_patches.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#endif
#ifndef MAP_PATCH_TEST
#include <SDL2/SDL.h>
#include "simulator/sim_RomExtractor.h"
#include "port/rom_setup.h"
#endif

namespace {
using Bytes = std::vector<uint8_t>;
struct MapPatch {
    const char *path;
    unsigned member;
    unsigned sourceMember;
    const char *before;
    const char *after;
    size_t outputSize;
    const unsigned char *delta;
    size_t deltaSize;
};
#include "map_patches.inc"

uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
std::string sha256(const Bytes &input)
{
    static const uint32_t k[] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    Bytes data(input);
    uint64_t bits = uint64_t(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(bits >> (i * 8));
    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i) {
            w[i] = 0;
            for (unsigned j = 0; j < 4; ++j) w[i] = (w[i] << 8) | data[offset + i * 4 + j];
        }
        for (unsigned i = 16; i < 64; ++i)
            w[i] = w[i-16] + (rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3)) + w[i-7]
                + (rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for (unsigned i = 0; i < 64; ++i) {
            uint32_t t = v + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + k[i] + w[i];
            uint32_t u = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
            v=g; g=f; f=e; e=d+t; d=c; c=b; b=a; a=t+u;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=v;
    }
    char result[65];
    for (unsigned i = 0; i < 8; ++i) std::snprintf(result + i*8, 9, "%08x", h[i]);
    return result;
}
uint32_t get(const Bytes &b, size_t pos, unsigned n = 4) {
    if (pos > b.size() || n > b.size() - pos) throw std::runtime_error("Truncated map archive or patch");
    uint32_t v = 0;
    for (unsigned i=0; i<n; ++i) v |= uint32_t(b[pos+i]) << (8*i);
    return v;
}
void put(Bytes &b, size_t pos, uint32_t v, unsigned n = 4) {
    if (pos > b.size() || n > b.size() - pos) throw std::runtime_error("Invalid archive offset");
    for (unsigned i=0; i<n; ++i) b[pos+i] = v >> (8*i);
}
Bytes read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read " + path.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    if (size < 0 || size > 256*1024*1024) throw std::runtime_error("Invalid map archive size");
    Bytes b(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(b.data()), b.size())) throw std::runtime_error("Cannot read " + path.string());
    return b;
}
void writeAtomic(const std::filesystem::path &path, const Bytes &b) {
    auto temp = path; temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(b.data()), b.size());
        file.close();
        if (!file) throw std::runtime_error("Cannot write " + temp.string());
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace " + path.string());
#else
    std::filesystem::rename(temp, path);
#endif
}
struct Archive {
    Bytes header;
    std::vector<Bytes> blocks, files;
    size_t fat = 0, image = 0;
    explicit Archive(const Bytes &data) {
        if (data.size() < 16 || std::string(data.begin(), data.begin()+4) != "NARC" || get(data,8) != data.size())
            throw std::runtime_error("Invalid map NARC");
        size_t offset = get(data,12,2);
        if (offset < 16 || offset > data.size()) throw std::runtime_error("Invalid NARC header");
        header.assign(data.begin(),data.begin()+offset);
        bool foundFat=false, foundImage=false;
        while (offset < data.size()) {
            size_t size = get(data,offset+4);
            if (size < 8 || size > data.size()-offset) throw std::runtime_error("Invalid NARC block");
            blocks.emplace_back(data.begin()+offset,data.begin()+offset+size);
            std::string tag(data.begin()+offset,data.begin()+offset+4);
            if (tag=="BTAF") { fat=blocks.size()-1; foundFat=true; }
            if (tag=="GMIF") { image=blocks.size()-1; foundImage=true; }
            offset+=size;
        }
        if (!foundFat || !foundImage || blocks.size()!=get(data,14,2)) throw std::runtime_error("Missing NARC blocks");
        for (unsigned i=0; i<get(blocks[fat],8,2); ++i) {
            size_t a=get(blocks[fat],12+i*8), b=get(blocks[fat],16+i*8);
            if (a>b || b>blocks[image].size()-8) throw std::runtime_error("Invalid NARC member");
            files.emplace_back(blocks[image].begin()+8+a,blocks[image].begin()+8+b);
        }
    }
    Bytes pack() {
        if (files.size() > 65535) throw std::runtime_error("Too many NARC members");
        blocks[fat].resize(12 + files.size()*8);
        put(blocks[fat],4,blocks[fat].size());
        put(blocks[fat],8,files.size(),2);
        blocks[image].resize(8);
        for (size_t i=0; i<files.size(); ++i) {
            put(blocks[fat],12+i*8,blocks[image].size()-8);
            blocks[image].insert(blocks[image].end(),files[i].begin(),files[i].end());
            put(blocks[fat],16+i*8,blocks[image].size()-8);
            while (blocks[image].size()%4) blocks[image].push_back(0xff);
        }
        put(blocks[image],4,blocks[image].size());
        Bytes result=header;
        for (const auto &block:blocks) result.insert(result.end(),block.begin(),block.end());
        put(result,8,result.size());
        return result;
    }
};
Bytes applyDelta(const MapPatch &p, const Bytes &original) {
    if (sha256(original)!=p.before) throw std::runtime_error("Unexpected original map member: " + std::string(p.path));
    Bytes delta(p.delta,p.delta+p.deltaSize), result;
    size_t pos=0;
    while (pos<delta.size()) {
        unsigned op=delta[pos++];
        size_t offset=0;
        if (op==0) { offset=get(delta,pos); pos+=4; }
        else if (op!=1) throw std::runtime_error("Invalid map patch operation");
        size_t count=get(delta,pos); pos+=4;
        const Bytes &source=op==0?original:delta;
        if (op==1) offset=pos;
        if (offset>source.size() || count>source.size()-offset || result.size()>p.outputSize || count>p.outputSize-result.size())
            throw std::runtime_error("Invalid map patch range");
        result.insert(result.end(),source.begin()+offset,source.begin()+offset+count);
        if (op==1) pos+=count;
    }
    if (result.size()!=p.outputSize || sha256(result)!=p.after) throw std::runtime_error("Map patch verification failed");
    return result;
}
void patchAll() {
    std::map<std::string,std::vector<const MapPatch *>> groups;
    for (const auto &p:mapPatches) groups[p.path].push_back(&p);
    // Validate and stage every archive before replacing any of them.
    std::map<std::string,Bytes> pending;
    for (const auto &group:groups) {
        const auto &path=group.first;
        if (group.second.size()==1 && group.second.front()->member==0xffffffffu) {
            const auto &patch=*group.second.front();
            Bytes current=read(path);
            if (sha256(current)==patch.after) continue;
            auto backup=std::filesystem::path(".map-patch-originals") / path;
            Bytes original=std::filesystem::exists(backup)?read(backup):current;
            Bytes patched=applyDelta(patch,original);
            if (!std::filesystem::exists(backup)) {
                std::filesystem::create_directories(backup.parent_path());
                writeAtomic(backup,original);
            }
            pending[path]=std::move(patched);
            continue;
        }
        Archive current(read(path));
        bool complete=true;
        for (auto p:group.second) {
            complete &= p->member<current.files.size() && sha256(current.files[p->member])==p->after;
        }
        if (complete) continue;
        auto applied=[&](const MapPatch *p) {
            return p->member<current.files.size() && sha256(current.files[p->member])==p->after;
        };
        auto backup=std::filesystem::path(".map-patch-originals") / path;
        bool saveBackup=!std::filesystem::exists(backup);
        Bytes original;
        if (!saveBackup) original=read(backup);
        else {
            original=read(path);
            // Without a backup, an archive patched by an older build can only
            // gain new patches whose sources are still original.
            for (auto p:group.second) {
                if (applied(p)) {
                    saveBackup=false; // Never save a modified file as the original.
                    continue;
                }
                if (p->sourceMember>=current.files.size() || sha256(current.files[p->sourceMember])!=p->before)
                    throw std::runtime_error("Original map data unavailable. Re-extract a clean US Platinum ROM into a fresh game directory.");
            }
        }
        Archive base(original);
        // Preserve unrelated members from the current archive (e.g. user mods).
        for (auto p:group.second) {
            if (applied(p)) continue;
            if (p->sourceMember>=base.files.size()) throw std::runtime_error("Missing original map member");
            if (p->member>current.files.size()) throw std::runtime_error("Non-contiguous added NARC member");
            Bytes patched=applyDelta(*p,base.files[p->sourceMember]);
            if (p->member==current.files.size()) current.files.push_back(std::move(patched));
            else current.files[p->member]=std::move(patched);
        }
        if (saveBackup) {
            std::filesystem::create_directories(backup.parent_path());
            writeAtomic(backup,original);
        }
        pending[path]=current.pack();
    }
    for (const auto &entry:pending) writeAtomic(entry.first,entry.second);
}
} // namespace

extern "C" void MapPatches_Ensure(void) {
    try {
#ifndef MAP_PATCH_TEST
    for (const auto &p:mapPatches) {
        if (!std::filesystem::exists(p.path)) {
            if (!RomSetup_TryExtractPlatinum()) SIM_ShowRomExtractionDialog();
            break;
        }
    }
#endif
        patchAll();
    }
    catch (const std::exception &error) {
#ifndef MAP_PATCH_TEST
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Game asset patch failed",error.what(),nullptr);
#endif
        std::fprintf(stderr,"Game asset patch failed: %s\n",error.what());
        std::exit(EXIT_FAILURE);
    }
}
#ifdef MAP_PATCH_TEST
int main() { MapPatches_Ensure(); }
#endif
