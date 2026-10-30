#include "port/gameplay_config_editor.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static void Write(const std::string &text)
{
    std::ofstream file(GAMEPLAY_CONFIG_FILE_PATH, std::ios::binary);
    file << text;
    assert(file.good());
}

static std::string Read()
{
    std::ifstream file(GAMEPLAY_CONFIG_FILE_PATH, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}

int main()
{
    GameplayConfig_Init();
    GameplayConfigEditor editor;
#ifdef GAMEPLAY_CONFIG_DEFAULTS_ONLY
    assert(!GameplayConfig_UsesFileOverrides());
    assert(!std::filesystem::exists(GAMEPLAY_CONFIG_FILE_PATH));
    assert(!editor.Load() && !editor.IsLoaded() && !editor.Save());
    assert(!std::filesystem::exists(GAMEPLAY_CONFIG_FILE_PATH));
    std::puts("Gameplay editor: defaults-only builds remain read-only.");
    return 0;
#else
    assert(GameplayConfig_UsesFileOverrides());
    assert(GameplayConfig_OptionCount() == 23);
    GameplayConfig values;
    GameplayConfig_LoadDefaults(&values);
    for (unsigned i = 0; i < GameplayConfig_OptionCount(); ++i) {
        GameplayConfigOption option;
        assert(GameplayConfig_GetOption(i, &option));
        assert(*option.name && *option.label && *option.group && *option.description);
        assert(GameplayConfig_SetOptionValue(&values, i, option.maximum));
        assert(GameplayConfig_GetOptionValue(&values, i) == option.maximum);
        assert(!GameplayConfig_SetOptionValue(&values, i, option.maximum + 1));
        assert(GameplayConfig_GetOptionValue(&values, i) == option.maximum);
        assert(GameplayConfig_SetOptionValue(&values, i, 0));
        assert(GameplayConfig_GetOptionValue(&values, i) == 0);
    }
    assert(!GameplayConfig_GetOption(23, nullptr));
    assert(!GameplayConfig_SetOptionValue(&values, 23, 0));

    const std::string unrelated = "[Other]\r\ninfiniteTMs=false\r\ncustom=keep me ; untouched\r\n";
    const std::string original = std::string("\xef\xbb\xbf") +
        "; My configuration\r\n[Gameplay]\r\n"
        "infiniteTMs = false  ; keep this comment\r\n"
        "shinyRolls: 1 ; odds\r\nunknownOption=keep this too\r\n" + unrelated +
        "[Gameplay]\r\ninfiniteTMs=false\r\n";
    Write(original);
    assert(editor.Load() && !editor.IsDirty() && !editor.NeedsRestart());
    editor.values.infiniteTMs = TRUE;
    editor.values.shinyRolls = 65535;
    editor.values.tradeEvolutionLevel = 100;
    editor.values.catchRateMultiplier = 255;
    assert(editor.IsDirty() && editor.Save());
    assert(!editor.IsDirty() && editor.NeedsRestart());
    // Saved values never modify active rules before restart.
    assert(!GameplayConfig_Get()->infiniteTMs && GameplayConfig_Get()->shinyRolls == 1);
    const std::string saved = Read();
    assert(saved.find("\xef\xbb\xbf; My configuration\r\n") == 0);
    assert(saved.find("infiniteTMs = true  ; keep this comment\r\n") != std::string::npos);
    assert(saved.find("infiniteTMs=true\r\n") != std::string::npos);
    assert(saved.find("shinyRolls: 65535 ; odds\r\n") != std::string::npos);
    assert(saved.find("unknownOption=keep this too\r\n") != std::string::npos);
    assert(saved.find(unrelated) != std::string::npos);
    for (size_t i = 0; i < saved.size(); ++i)
        if (saved[i] == '\n') assert(i > 0 && saved[i - 1] == '\r');
    GameplayConfig_Init();
    assert(GameplayConfig_Get()->infiniteTMs && GameplayConfig_Get()->shinyRolls == 65535);
    assert(GameplayConfig_Get()->tradeEvolutionLevel == 100 && GameplayConfig_Get()->catchRateMultiplier == 255);
    assert(!editor.NeedsRestart());
    assert(editor.Save() && Read() == saved); // Saving again does not append duplicate blocks.

    editor.RestoreDefaults();
    assert(editor.IsDirty());
    assert(GameplayConfig_Get()->infiniteTMs);
    assert(editor.Load() && !editor.IsDirty() && editor.values.infiniteTMs); // Discard draft.

    editor.values.infiniteTMs = FALSE;
    Write(saved + "; edited externally\r\n");
    const std::string external = Read();
    assert(!editor.Save() && Read() == external && !editor.error.empty());
    assert(editor.Load());
    editor.values.infiniteTMs = FALSE;
    assert(editor.Save() && Read().find("; edited externally\r\n") != std::string::npos);

    Write("[Gameplay]\ntradeEvolutionLevel=2\n");
    assert(editor.Load());
    editor.values.tradeEvolutionLevel = 200;
    assert(!editor.Save() && Read() == "[Gameplay]\ntradeEvolutionLevel=2\n");

    Write("[Gameplay]\ninfiniteTMs=true\ninvalid line\n");
    assert(!editor.Load() && !editor.IsLoaded());
    assert(!editor.Save() && Read() == "[Gameplay]\ninfiniteTMs=true\ninvalid line\n");

    std::string binary = "[Gameplay]\n";
    binary += '\0';
    binary += "infiniteTMs=true\n";
    Write(binary);
    assert(!editor.Load() && Read() == binary);

    // Indented INI continuations must never silently produce different rules.
    const std::string continuation = "[Gameplay]\nunknown=hello\n infiniteTMs=false\n";
    Write(continuation);
    assert(editor.Load());
    editor.values.infiniteTMs = TRUE;
    assert(!editor.Save() && Read() == continuation);

    // Add a missing Gameplay section without modifying the unrelated section.
    Write("; notes\n[Other]\nkey=value");
    assert(editor.Load());
    editor.values.freeMoveTutors = TRUE;
    assert(editor.Save());
    assert(Read().find("; notes\n[Other]\nkey=value\n\n[Gameplay]\n") == 0);
    GameplayConfig parsed;
    assert(GameplayConfig_ParseText(Read().c_str(), &parsed) && parsed.freeMoveTutors);

    std::filesystem::remove(GAMEPLAY_CONFIG_FILE_PATH);
    assert(editor.Load() && editor.IsDirty());
    Write(""); // An externally created empty file is still a conflicting change.
    assert(!editor.Save() && Read().empty());
    std::filesystem::remove(GAMEPLAY_CONFIG_FILE_PATH);
    assert(editor.Load() && editor.Save() && !editor.IsDirty());
    assert(GameplayConfig_ParseText(Read().c_str(), &parsed) && !parsed.infiniteTMs);
#ifndef _WIN32
    const auto mode = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
        std::filesystem::perms::group_read;
    std::filesystem::permissions(GAMEPLAY_CONFIG_FILE_PATH, mode);
    assert(editor.Load());
    editor.values.freeMoveTutors = TRUE;
    assert(editor.Save());
    assert(std::filesystem::status(GAMEPLAY_CONFIG_FILE_PATH).permissions() == mode);
#endif
    for (const auto &entry : std::filesystem::directory_iterator("."))
        assert(entry.path().filename().string().find("gameplay_config.ini.tmp-") != 0);
    std::filesystem::remove(GAMEPLAY_CONFIG_FILE_PATH);
    std::filesystem::create_directory(GAMEPLAY_CONFIG_FILE_PATH);
    assert(!editor.Load() && !editor.Save());
    assert(std::filesystem::is_directory(GAMEPLAY_CONFIG_FILE_PATH));
    std::puts("Gameplay editor: all options, ranges, restart behavior, preservation, conflicts and failures passed.");
#endif
}
