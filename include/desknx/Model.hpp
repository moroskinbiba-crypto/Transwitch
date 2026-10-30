#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace desknx::model {

constexpr int Columns = 6;
constexpr int Rows = 3;
constexpr int MaxItems = 600;
constexpr int MaxDesks = 12;

enum class Kind { Game, Nro, Clock, Battery, System, Recent, Unsupported };

struct Tile {
    Kind kind{Kind::Game};
    std::string title;
    std::string data;
    int x{0}, y{0}, w{1}, h{1}, color{0};
    std::string unknownTypeCode{};
};

struct Workspace {
    std::string name;
    std::vector<Tile> tiles;
};

struct Candidate {
    Kind kind{Kind::Game};
    std::string title;
    std::string data;
    int w{1}, h{1}, color{0};
    uint32_t viewFlags{0}; // NS live metadata; never used to hide installed titles
    uint64_t playtimeSeconds{0};
    uint64_t lastPlayed{0}; // POSIX seconds
    uint32_t launchCount{0};
    bool playStatsQueried{false};
    bool playStatsAvailable{false};
};

inline bool isTitleId(const std::string& value) {
    if (value.size() != 16) return false;
    for (unsigned char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
              (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

inline std::string typeCode(Kind kind) {
    switch (kind) {
        case Kind::Game: return "G";
        case Kind::Nro: return "N";
        case Kind::Clock: return "C";
        case Kind::Battery: return "B";
        case Kind::System: return "S";
        case Kind::Recent: return "R";
        case Kind::Unsupported: return "U";
    }
    return "U";
}

inline Kind parseKind(const std::string& value) {
    if (value == "G") return Kind::Game;
    if (value == "N") return Kind::Nro;
    if (value == "C") return Kind::Clock;
    if (value == "B") return Kind::Battery;
    if (value == "S") return Kind::System;
    if (value == "R") return Kind::Recent;
    return Kind::Unsupported;
}

inline std::string typeCode(const Tile& tile) {
    if (tile.kind == Kind::Unsupported && !tile.unknownTypeCode.empty())
        return tile.unknownTypeCode;
    return typeCode(tile.kind);
}

inline std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string part;
    for (char c : line) {
        if (c == '\t') {
            out.push_back(part);
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    out.push_back(part);
    return out;
}

inline std::string safeField(std::string value) {
    for (char& c : value) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return value;
}

inline bool overlap(const Tile& a, const Tile& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

inline bool placeable(const Workspace& desk, const Tile& tile, int x, int y, int ignore = -1) {
    // Check against the grid before adding dimensions; x + w can overflow for
    // malformed coordinates loaded from a user-editable layout file.
    if (x < 0 || y < 0 || x >= Columns || y >= Rows ||
        tile.w < 1 || tile.h < 1 ||
        tile.w > Columns - x || tile.h > Rows - y) return false;
    Tile candidate = tile;
    candidate.x = x;
    candidate.y = y;
    for (size_t i = 0; i < desk.tiles.size(); ++i) {
        if (static_cast<int>(i) != ignore && overlap(candidate, desk.tiles[i])) return false;
    }
    return true;
}

inline int itemAt(const Workspace& desk, int x, int y) {
    for (size_t i = 0; i < desk.tiles.size(); ++i) {
        const Tile& tile = desk.tiles[i];
        if (x >= tile.x && x < tile.x + tile.w &&
            y >= tile.y && y < tile.y + tile.h) return static_cast<int>(i);
    }
    return -1;
}

inline bool firstFree(const Workspace& desk, const Tile& tile, int& x, int& y) {
    for (int yy = 0; yy < Rows; ++yy) {
        for (int xx = 0; xx < Columns; ++xx) {
            if (placeable(desk, tile, xx, yy)) {
                x = xx;
                y = yy;
                return true;
            }
        }
    }
    return false;
}

inline uint64_t stableHash(const std::string& value) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::string iconCachePath(Kind kind, const std::string& data) {
    char key[32]{};
    if (kind == Kind::Game) {
        // Game IDs are used in a filesystem path. Do not let malformed layout
        // data introduce separators or traversal components.
        if (!isTitleId(data)) return {};
        std::string normalized = data;
        for (char& c : normalized) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        return "sdmc:/switch/DeskNX/cache/icons/game-" + normalized + ".jpg";
    }
    if (kind == Kind::Nro) {
        std::snprintf(key, sizeof(key), "%016llX",
                      static_cast<unsigned long long>(stableHash(data)));
        return std::string("sdmc:/switch/DeskNX/cache/icons/nro-") + key + ".jpg";
    }
    return {};
}

} // namespace desknx::model
