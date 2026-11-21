#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "Model.hpp"

namespace desknx::widgets {

struct Config {
    int style{0};
    int refreshSeconds{15};
    std::uint32_t flags{3u};
    int itemCount{4};
};

inline int clampRefresh(int value) {
    static constexpr int allowed[] = {5, 15, 30, 60, 180};
    int best = allowed[0];
    int distance = std::abs(value - best);
    for (int candidate : allowed) {
        const int next = std::abs(value - candidate);
        if (next < distance) { best = candidate; distance = next; }
    }
    return best;
}

inline int clampItemCount(int value) {
    static constexpr int allowed[] = {2, 3, 4, 6};
    for (int candidate : allowed) if (candidate == value) return value;
    return value < 3 ? 2 : value < 4 ? 3 : value < 6 ? 4 : 6;
}

inline Config defaults(model::Kind kind) {
    switch (kind) {
        case model::Kind::Clock:       return {0, 5, 3u, 4};
        case model::Kind::Battery:     return {0, 15, 3u, 4};
        case model::Kind::System:      return {0, 5, 3u, 4};
        case model::Kind::Recent:      return {1, 15, 3u, 4};
        case model::Kind::Storage:     return {0, 30, 3u, 4};
        case model::Kind::Calendar:    return {0, 60, 3u, 4};
        case model::Kind::Nextendo:    return {0, 180, 3u, 4};
        case model::Kind::Favorites:   return {1, 30, 3u, 4};
        case model::Kind::GameLibrary: return {1, 30, 3u, 4};
        case model::Kind::QuickSettings: return {0, 15, 3u, 4};
        default: return {};
    }
}

// Empty data remains valid for layouts saved by older Transwitch versions.
// Format: W1;S=1;R=30;F=3;N=4
inline Config parse(model::Kind kind, const std::string& value) {
    Config config = defaults(kind);
    if (value.empty() || value.size() > 64 || value.compare(0, 3, "W1;") != 0)
        return config;

    std::size_t start = 3;
    while (start <= value.size()) {
        const std::size_t end = value.find(';', start);
        const std::string field = value.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        const std::size_t equals = field.find('=');
        if (equals == 1 && field.size() <= 8) {
            const std::string digits = field.substr(2);
            if (!digits.empty() && digits.size() <= 3 &&
                std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
                    return c >= '0' && c <= '9';
                })) {
                const int number = std::atoi(digits.c_str());
                switch (field[0]) {
                    case 'S': config.style = std::max(0, std::min(2, number)); break;
                    case 'R': config.refreshSeconds = clampRefresh(number); break;
                    case 'F': config.flags = static_cast<std::uint32_t>(number) & 7u; break;
                    case 'N': config.itemCount = clampItemCount(number); break;
                    default: break;
                }
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return config;
}

inline std::string serialize(const Config& config) {
    return "W1;S=" + std::to_string(std::max(0, std::min(2, config.style))) +
        ";R=" + std::to_string(clampRefresh(config.refreshSeconds)) +
        ";F=" + std::to_string(config.flags & 7u) +
        ";N=" + std::to_string(clampItemCount(config.itemCount));
}

inline bool enabled(const Config& config, unsigned bit) {
    return bit < 3u && (config.flags & (1u << bit)) != 0;
}
inline void toggle(Config& config, unsigned bit) {
    if (bit < 3u) config.flags ^= (1u << bit);
}

} // namespace desknx::widgets
