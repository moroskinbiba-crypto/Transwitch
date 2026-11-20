#pragma once

#include "Model.hpp"

#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

namespace desknx::launch_history {

struct Entry {
    model::Kind kind{model::Kind::Game};
    std::string title;
    std::string data;
    std::uint64_t timestamp{0}; // POSIX seconds; zero denotes a legacy entry without a timestamp.
};

constexpr std::size_t MaxEntries = 100;
constexpr std::size_t MaxFileBytes = 128u * 1024u;

using NroPathValidator = bool (*)(const std::string& path, void* context);

inline bool parse(const std::string& contents, std::vector<Entry>& output,
                  NroPathValidator validateNroPath = nullptr,
                  void* context = nullptr) {
    if (contents.empty() || contents.size() > MaxFileBytes) return false;
    const std::size_t headerEnd = contents.find('\n');
    if (headerEnd == std::string::npos) return false;
    const std::string header = contents.substr(0, headerEnd);
    if (header != "DESKNXH1" && header != "DESKNXH2") return false;
    const bool hasTimestamps = header == "DESKNXH2";

    std::vector<Entry> parsed;
    std::size_t position = headerEnd + 1;
    while (position < contents.size() && parsed.size() < MaxEntries) {
        const std::size_t newline = contents.find('\n', position);
        const std::size_t end = newline == std::string::npos ? contents.size() : newline;
        if (end - position > 2048) {
            position = newline == std::string::npos ? contents.size() : newline + 1;
            continue;
        }
        const std::string line = contents.substr(position, end - position);
        position = newline == std::string::npos ? contents.size() : newline + 1;
        if (line.empty()) continue;

        const auto fields = model::splitTabs(line);
        if (fields.size() != (hasTimestamps ? 4u : 3u) ||
            fields[0].size() > 16 || fields[1].empty() || fields[1].size() > 512 ||
            fields[2].empty() || fields[2].size() > 512)
            continue;

        const model::Kind kind = model::parseKind(fields[0]);
        if (kind != model::Kind::Game && kind != model::Kind::Nro) continue;
        if (kind == model::Kind::Game && !model::isTitleId(fields[2])) continue;
        if (kind == model::Kind::Nro) {
            const bool safe = validateNroPath
                ? validateNroPath(fields[2], context)
                : (fields[2].compare(0, 13, "sdmc:/switch/") == 0 &&
                   fields[2].find("..") == std::string::npos &&
                   fields[2].size() >= 17 &&
                   fields[2].compare(fields[2].size() - 4, 4, ".nro") == 0);
            if (!safe) continue;
        }

        std::uint64_t timestamp = 0;
        if (hasTimestamps) {
            if (fields[3].empty() || fields[3][0] == '-') continue;
            errno = 0;
            char* parsedEnd = nullptr;
            const unsigned long long value = std::strtoull(fields[3].c_str(), &parsedEnd, 10);
            if (errno == ERANGE || parsedEnd == fields[3].c_str() ||
                !parsedEnd || *parsedEnd != '\0')
                continue;
            timestamp = static_cast<std::uint64_t>(value);
        }
        parsed.push_back({kind, fields[1], fields[2], timestamp});
    }
    output.swap(parsed);
    return true;
}

inline std::string serialize(const std::vector<Entry>& events) {
    if (events.size() > MaxEntries) return {};
    std::string output = "DESKNXH2\n";
    for (const auto& event : events) {
        if ((event.kind != model::Kind::Game && event.kind != model::Kind::Nro) ||
            event.title.empty() || event.title.size() > 512 ||
            event.data.empty() || event.data.size() > 512 ||
            (event.kind == model::Kind::Game && !model::isTitleId(event.data)))
            return {};
        output += model::typeCode(event.kind);
        output += '\\t';
        output += model::safeField(event.title);
        output += '\\t';
        output += model::safeField(event.data);
        output += '\\t';
        output += std::to_string(event.timestamp);
        output += '\\n';
    }
    if (output.size() > MaxFileBytes) return {};
    return output;
}

// Return one entry per application, ordered by its most recent launch.
inline std::vector<Entry> uniqueRecent(const std::vector<Entry>& events) {
    std::vector<Entry> result;
    result.reserve(events.size());
    for (const auto& event : events) {
        bool seen = false;
        for (const auto& existing : result) {
            if (existing.kind == event.kind && existing.data == event.data) {
                seen = true;
                break;
            }
        }
        if (!seen) result.push_back(event);
    }
    return result;
}

inline std::size_t count(const std::vector<Entry>& events,
                         model::Kind kind,
                         const std::string& data) {
    std::size_t result = 0;
    for (const auto& event : events)
        if (event.kind == kind && event.data == data) ++result;
    return result;
}

// Events are newest-first, so the first matching timestamp is the latest.
inline std::uint64_t lastTimestamp(const std::vector<Entry>& events,
                                   model::Kind kind,
                                   const std::string& data) {
    for (const auto& event : events)
        if (event.kind == kind && event.data == data) return event.timestamp;
    return 0;
}

} // namespace desknx::launch_history
