#include "port/gameplay_config_editor.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
bool Equal(const GameplayConfig &a, const GameplayConfig &b)
{
    for (unsigned i = 0; i < GameplayConfig_OptionCount(); ++i)
        if (GameplayConfig_GetOptionValue(&a, i) != GameplayConfig_GetOptionValue(&b, i)) return false;
    return true;
}

bool ReadSource(std::string &text, bool &exists, std::string &error)
{
    text.clear();
    FILE *file = std::fopen(GAMEPLAY_CONFIG_FILE_PATH, "rb");
    if (!file) {
        exists = false;
        if (errno == ENOENT) return true;
        error = std::string("Could not read gameplay_config.ini: ") + std::strerror(errno);
        return false;
    }
    exists = true;
    char buffer[4096];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
        text.append(buffer, count);
    const bool failed = std::ferror(file);
    const int closed = std::fclose(file);
    if (failed || closed != 0) {
        error = "Could not read gameplay_config.ini completely. The file was not changed.";
        return false;
    }
    return true;
}

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::string Value(const GameplayConfig &config, unsigned index)
{
    GameplayConfigOption option;
    GameplayConfig_GetOption(index, &option);
    const unsigned value = GameplayConfig_GetOptionValue(&config, index);
    return option.isBoolean ? (value ? "true" : "false") : std::to_string(value);
}

// Update every duplicate of a known key. Preserve comments, unknown keys,
// unrelated sections, BOM and the existing line endings byte for byte.
std::string Rewrite(const std::string &source, const GameplayConfig &config)
{
    std::string result;
    std::string section;
    std::vector<bool> seen(GameplayConfig_OptionCount(), false);
    const std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    for (size_t begin = 0; begin < source.size();) {
        size_t end = source.find('\n', begin);
        if (end == std::string::npos) end = source.size();
        size_t bodyEnd = end;
        if (bodyEnd > begin && source[bodyEnd - 1] == '\r') --bodyEnd;
        const std::string_view body(source.data() + begin, bodyEnd - begin);
        std::string_view line = body;
        if (begin == 0 && line.substr(0, 3) == "\xef\xbb\xbf") line.remove_prefix(3);
        line = Trim(line);
        bool replaced = false;
        if (!line.empty() && line.front() == '[') {
            const size_t close = line.find(']');
            if (close != std::string_view::npos) section = line.substr(1, close - 1);
        } else if (section == "Gameplay" && !line.empty() && line.front() != ';' && line.front() != '#') {
            const size_t delimiter = line.find_first_of("=:");
            if (delimiter != std::string_view::npos) {
                const std::string_view name = Trim(line.substr(0, delimiter));
                for (unsigned i = 0; i < GameplayConfig_OptionCount(); ++i) {
                    GameplayConfigOption option;
                    GameplayConfig_GetOption(i, &option);
                    if (name != option.name) continue;
                    // Positions in the untrimmed line retain indentation/key spacing.
                    const size_t split = (line.data() - body.data()) + delimiter;
                    size_t valueStart = split + 1;
                    while (valueStart < body.size() && std::isspace(static_cast<unsigned char>(body[valueStart]))) ++valueStart;
                    size_t valueEnd = body.size();
                    for (size_t c = split + 1; c < body.size(); ++c) {
                        if (body[c] == ';' && c > 0 && std::isspace(static_cast<unsigned char>(body[c - 1]))) {
                            valueEnd = c;
                            break;
                        }
                    }
                    while (valueEnd > split + 1 && std::isspace(static_cast<unsigned char>(body[valueEnd - 1]))) --valueEnd;
                    result.append(body.substr(0, valueStart));
                    result += Value(config, i);
                    if (valueEnd < valueStart) {
                        result += ' ';
                        valueEnd = valueStart;
                    }
                    result.append(body.substr(valueEnd));
                    seen[i] = replaced = true;
                    break;
                }
            }
        }
        if (!replaced) result.append(body);
        result.append(source, bodyEnd, end - bodyEnd + (end < source.size() ? 1 : 0));
        begin = end < source.size() ? end + 1 : source.size();
    }
    if (std::find(seen.begin(), seen.end(), false) != seen.end()) {
        if (!result.empty() && result.back() != '\n') result += newline;
        if (section != "Gameplay") result += newline + "[Gameplay]" + newline;
        for (unsigned i = 0; i < seen.size(); ++i) {
            if (seen[i]) continue;
            GameplayConfigOption option;
            GameplayConfig_GetOption(i, &option);
            result += std::string(option.name) + '=' + Value(config, i) + newline;
        }
    }
    return result;
}

bool AtomicWrite(const std::string &text, const std::string &expected, bool existed, std::string &error)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string temporary;
    FILE *file = nullptr;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = std::string(GAMEPLAY_CONFIG_FILE_PATH) + ".tmp-" + std::to_string(stamp) + '-' + std::to_string(attempt);
        file = std::fopen(temporary.c_str(), "wbx");
        if (file || errno != EEXIST) break;
    }
    if (!file) {
        error = std::string("Could not save gameplay_config.ini: ") + std::strerror(errno);
        return false;
    }
    bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fflush(file) != 0) written = false;
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) written = false;
#else
    if (fsync(fileno(file)) != 0) written = false;
#endif
    if (std::fclose(file) != 0) written = false;
#ifndef _WIN32
    if (written && existed) {
        std::error_code ec;
        const auto permissions = std::filesystem::status(GAMEPLAY_CONFIG_FILE_PATH, ec).permissions();
        if (!ec) std::filesystem::permissions(temporary, permissions, ec);
        if (ec) written = false;
    }
#endif
    std::string current;
    bool exists;
    if (written && (!ReadSource(current, exists, error) || exists != existed || current != expected)) {
        if (error.empty()) error = "The file changed on disk. Reload before saving.";
        written = false;
    }
    if (written) {
#ifdef _WIN32
        written = MoveFileExA(temporary.c_str(), GAMEPLAY_CONFIG_FILE_PATH,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        written = std::rename(temporary.c_str(), GAMEPLAY_CONFIG_FILE_PATH) == 0;
#endif
    }
    if (!written) {
        std::remove(temporary.c_str());
        if (error.empty()) error = "Could not finish saving. The original gameplay_config.ini was preserved.";
    }
    return written;
}
}

bool GameplayConfigEditor::Load()
{
    error.clear();
    loaded = false;
    if (!GameplayConfig_UsesFileOverrides()) {
        error = "Gameplay file overrides are disabled in this build.";
        return false;
    }
    if (!ReadSource(source, existed, error)) return false;
    if (source.find('\0') != std::string::npos || !GameplayConfig_ParseText(source.c_str(), &values)) {
        error = "gameplay_config.ini has a syntax error. Fix it in a text editor, then reload.";
        return false;
    }
    saved = values;
    loaded = true;
    return true;
}

bool GameplayConfigEditor::IsDirty() const
{
    return loaded && (!existed || !Equal(values, saved));
}

bool GameplayConfigEditor::NeedsRestart() const
{
    return loaded && !Equal(saved, *GameplayConfig_Get());
}

void GameplayConfigEditor::RestoreDefaults()
{
    if (loaded) GameplayConfig_LoadDefaults(&values);
}

bool GameplayConfigEditor::Save()
{
    error.clear();
    if (!loaded || !GameplayConfig_UsesFileOverrides()) {
        error = "Load an editable gameplay config before saving.";
        return false;
    }
    std::string current;
    bool exists;
    if (!ReadSource(current, exists, error)) return false;
    if (exists != existed || current != source) {
        error = "The file changed on disk. Reload before saving.";
        return false;
    }
    for (unsigned i = 0; i < GameplayConfig_OptionCount(); ++i) {
        GameplayConfigOption option;
        GameplayConfig_GetOption(i, &option);
        if (GameplayConfig_GetOptionValue(&values, i) > option.maximum) {
            error = std::string("Value is out of range: ") + option.label;
            return false;
        }
    }
    const std::string text = Rewrite(source, values);
    GameplayConfig verified;
    if (!GameplayConfig_ParseText(text.c_str(), &verified) || !Equal(verified, values)) {
        error = "The file uses formatting the editor cannot update. Edit it in a text editor instead.";
        return false;
    }
    if (!AtomicWrite(text, source, existed, error)) return false;
    source = text;
    saved = values;
    existed = true;
    return true;
}
