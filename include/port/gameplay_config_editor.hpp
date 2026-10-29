#ifndef POKEPLATINUM_GAMEPLAY_CONFIG_EDITOR_HPP
#define POKEPLATINUM_GAMEPLAY_CONFIG_EDITOR_HPP

#include "port/gameplay_config.h"
#include <string>

// Edits a disk snapshot separately from the running game's rules.
class GameplayConfigEditor {
public:
    GameplayConfig values{};
    std::string error;

    bool Load();
    bool Save();
    bool IsLoaded() const { return loaded; }
    bool IsDirty() const;
    bool NeedsRestart() const;
    void RestoreDefaults();

private:
    GameplayConfig saved{};
    std::string source;
    bool existed = false;
    bool loaded = false;
};

#endif
