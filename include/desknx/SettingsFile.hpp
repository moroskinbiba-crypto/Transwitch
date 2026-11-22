#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace desknx::settings_file {

struct Entry { std::string key; std::string value; };

constexpr std::size_t MaxBytes = 8192;
constexpr std::size_t MaxLineBytes = 2048;
constexpr std::size_t MaxKeyBytes = 64;

inline bool hasSupportedHeader(const std::string& contents) {
    if (contents.empty() || contents.size() > MaxBytes ||
        contents.find('\0') != std::string::npos) return false;
    const std::size_t newline = contents.find('\n');
    if (newline == std::string::npos || newline > 32) return false;
    const std::string header = contents.substr(0, newline);
    return header == "DESKNXSET1" || header == "DESKNXSET2";
}

inline bool parse(const std::string& contents, std::vector<Entry>& output) {
    if (!hasSupportedHeader(contents)) return false;
    std::vector<Entry> parsed;
    std::size_t position = contents.find('\n') + 1;
    while (position < contents.size()) {
        const std::size_t newline = contents.find('\n', position);
        const std::size_t end = newline == std::string::npos ? contents.size() : newline;
        const std::size_t length = end - position;
        if (length > MaxLineBytes) return false;
        const std::string line = contents.substr(position, length);
        position = newline == std::string::npos ? contents.size() : newline + 1;
        if (line.empty()) continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0 ||
            separator > MaxKeyBytes) continue;
        parsed.push_back({line.substr(0, separator), line.substr(separator + 1)});
    }
    output.swap(parsed);
    return true;
}

inline const std::string* find(const std::vector<Entry>& entries, const char* key) {
    if (!key) return nullptr;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (it->key == key) return &it->value;
    return nullptr;
}

} // namespace desknx::settings_file
