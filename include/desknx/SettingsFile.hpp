#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace desknx::settings_file {

struct Entry {
    std::string key;
    std::string value;
};

constexpr std::size_t MaxBytes = 4096;
constexpr std::size_t MaxLineBytes = 512;

inline bool parse(const std::string& contents, std::vector<Entry>& output) {
    if (contents.empty() || contents.size() > MaxBytes) return false;

    std::size_t position = 0;
    const std::size_t headerEnd = contents.find('\n');
    if (headerEnd == std::string::npos ||
        contents.substr(0, headerEnd) != "DESKNXSET1")
        return false;
    position = headerEnd + 1;

    std::vector<Entry> parsed;
    while (position < contents.size()) {
        const std::size_t newline = contents.find('\n', position);
        const std::size_t end = newline == std::string::npos ? contents.size() : newline;
        const std::size_t length = end - position;
        if (length > MaxLineBytes) return false;
        const std::string line = contents.substr(position, length);
        position = newline == std::string::npos ? contents.size() : newline + 1;
        if (line.empty()) continue;
        if (line.find('\0') != std::string::npos) return false;

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0) continue;
        if (separator > 64) continue;
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
