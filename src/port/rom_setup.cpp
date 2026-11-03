#include "port/rom_setup.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bwbattle/bwbattle.h"
#include "port/follower_assets.h"
#include "port/sim_config_prj.h"

// The simulator's existing Platinum extractor.
int SIM_NDSExtract(std::string ndsFilePath);

namespace {
namespace fs = std::filesystem;

std::vector<fs::path> FindRoms(bool bw2)
{
    std::vector<fs::path> directories;
    if (char *base = SDL_GetBasePath()) {
        directories.emplace_back(base);
        SDL_free(base);
    }
    std::error_code error;
    auto working = fs::current_path(error);
    if (!error && (directories.empty() || !fs::equivalent(directories.front(), working, error)))
        directories.push_back(working);

    std::vector<fs::path> roms;
    for (const auto &directory : directories) {
        std::vector<fs::path> matches;
        fs::directory_iterator it(directory, error), end;
        for (; !error && it != end; it.increment(error)) {
            if (!it->is_regular_file(error)) continue;
            auto extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (extension != ".nds") continue;
            std::ifstream file(it->path(), std::ios::binary);
            char code[4];
            file.seekg(0x0c);
            if (!file.read(code, sizeof(code))) continue;
            std::string game(code, sizeof(code));
            if (bw2 ? (game == "IREO" || game == "IRDO") : game == "CPUE")
                matches.push_back(it->path());
        }
        std::sort(matches.begin(), matches.end());
        roms.insert(roms.end(), matches.begin(), matches.end());
    }
    return roms;
}
}

extern "C" int RomSetup_TryExtractPlatinum(void)
{
    for (const auto &rom : FindRoms(false)) {
        std::fprintf(stdout, "Automatically extracting Platinum ROM: %s\n", rom.string().c_str());
        try {
            if (SIM_NDSExtract(rom.string()) == 0) return 1;
        } catch (const std::exception &error) {
            std::fprintf(stderr, "Platinum extraction failed: %s\n", error.what());
        }
    }
    return 0;
}

extern "C" void RomSetup_AutoExtractBw2(void)
{
    for (const auto &rom : FindRoms(true)) {
        // The extractor skips existing files, including on subsequent launches.
        auto result = BwBattle_ExtractAssets(rom.string().c_str());
        FollowerAssets_Reload();
        if (result != BWBATTLE_EXTRACT_OK || !BwBattle_AssetsReady()) {
            std::fprintf(stderr, "Automatic BW2 extraction (%s): %s\n",
                rom.string().c_str(), BwBattle_ExtractResultMessage(result));
            continue;
        }
        auto *config = SIM_Config_prj_GetConfig();
        if (!config->bw2BattleVisuals) {
            config->bw2BattleVisuals = TRUE;
            SIM_Config_prj_SaveConfigFile(config);
        }
        return;
    }
}
