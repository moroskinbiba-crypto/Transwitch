#pragma once

#include "Model.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace desknx::launch_history {

struct Entry {
    model::Kind kind{model::Kind::Game};
    std::string title;
    std::string data;
};
constexpr std::size_t MaxEntries = 10;
constexpr std::size_t MaxFileBytes = 64u * 1024u;
constexpr std::size_t MaxLineBytes = 2048;
using NroPathValidator = bool (*)(const std::string&, void*);

inline bool safeNroPathSyntax(const std::string& path) {
    constexpr char prefix[] = "sdmc:/switch/";
    if (path.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
        path.find("..") != std::string::npos ||
        path.find('\0') != std::string::npos ||
        path.size() < (sizeof(prefix) - 1) + 5) return false;
    std::string extension = path.substr(path.size() - 4);
    for (char& c : extension)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return extension == ".nro";
}

inline bool parse(const std::string& contents, std::vector<Entry>& output,
                  NroPathValidator validateNroPath = nullptr, void* context = nullptr) {
    if (contents.empty() || contents.size() > MaxFileBytes ||
        contents.find('\0') != std::string::npos) return false;
    const std::size_t headerEnd = contents.find('\n');
    if (headerEnd == std::string::npos || headerEnd > 32 ||
        contents.compare(0, headerEnd, "DESKNXH1") != 0) return false;

    std::vector<Entry> parsed;
    std::size_t position = headerEnd + 1;
    while (position < contents.size() && parsed.size() < MaxEntries) {
        const std::size_t newline = contents.find('\n', position);
        const std::size_t end = newline == std::string::npos ? contents.size() : newline;
        if (end - position > MaxLineBytes) {
            position = newline == std::string::npos ? contents.size() : newline + 1;
            continue;
        }
        const std::string line = contents.substr(position, end - position);
        position = newline == std::string::npos ? contents.size() : newline + 1;
        if (line.empty()) continue;
        const auto fields = model::splitTabs(line);
        if (fields.size() != 3 || fields[0].size() > 16 ||
            fields[1].empty() || fields[1].size() > 512 ||
            fields[2].empty() || fields[2].size() > 512) continue;
        const model::Kind kind = model::parseKind(fields[0]);
        if (kind != model::Kind::Game && kind != model::Kind::Nro) continue;
        if (kind == model::Kind::Game && !model::isTitleId(fields[2])) continue;
        if (kind == model::Kind::Nro) {
            const bool safe = validateNroPath
                ? validateNroPath(fields[2], context) : safeNroPathSyntax(fields[2]);
            if (!safe) continue;
        }
        parsed.push_back({kind, fields[1], fields[2]});
    }
    output.swap(parsed);
    return true;
}

inline std::string serialize(const std::vector<Entry>& entries) {
    if (entries.size() > MaxEntries) return {};
    std::string output = "DESKNXH1\n";
    for (const auto& entry : entries) {
        if ((entry.kind != model::Kind::Game && entry.kind != model::Kind::Nro) ||
            entry.title.empty() || entry.title.size() > 512 ||
            entry.data.empty() || entry.data.size() > 512 ||
            (entry.kind == model::Kind::Game && !model::isTitleId(entry.data)) ||
            (entry.kind == model::Kind::Nro && !safeNroPathSyntax(entry.data))) return {};
        output += model::typeCode(entry.kind);
        output += '\t';
        output += model::safeField(entry.title);
        output += '\t';
        output += model::safeField(entry.data);
        output += '\n';
    }
    return output.size() <= MaxFileBytes ? output : std::string{};
}

} // namespace desknx::launch_history
