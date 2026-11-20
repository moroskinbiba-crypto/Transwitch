#pragma once

#include "Model.hpp"

#include <cstddef>
#include <cstdint>
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
