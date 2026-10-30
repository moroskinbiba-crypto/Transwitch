#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace desknx::model {

constexpr int Columns = 6;
constexpr int Rows = 3;
constexpr int MaxItems = 600;
constexpr int MaxDesks = 12;

enum class Kind { Game, Nro, Clock, Battery, System, Recent, Storage, Calendar, Unsupported };

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
    std::string backgroundPath{};
    int backgroundFit{0};   // 0 = cover, 1 = contain, 2 = stretch
    int artworkMode{0};    // 0 = cover, 1 = contain, 2 = stretch
    int labelOpacity{196}; // 0..255
    int accentColor{-1};   // -1 = tile default, 0..7 = palette entry
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
        case Kind::Storage: return "V";
        case Kind::Calendar: return "A";
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
    if (value == "V") return Kind::Storage;
    if (value == "A") return Kind::Calendar;
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

inline bool isValidWorkspaceBackgroundPath(const std::string& path) {
    if (path.empty()) return true;
    constexpr char prefix[] = "sdmc:/switch/DeskNX/media/";
    if (path.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
        path.size() > 240 || path.find("..") != std::string::npos ||
        path.find('\0') != std::string::npos ||
        path.find('\t') != std::string::npos ||
        path.find('\n') != std::string::npos ||
        path.find('\r') != std::string::npos)
        return false;
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

inline bool isValidWorkspaceAppearance(const Workspace& workspace) {
    return isValidWorkspaceBackgroundPath(workspace.backgroundPath) &&
           workspace.backgroundFit >= 0 && workspace.backgroundFit <= 2 &&
           workspace.artworkMode >= 0 && workspace.artworkMode <= 2 &&
           workspace.labelOpacity >= 0 && workspace.labelOpacity <= 255 &&
           workspace.accentColor >= -1 && workspace.accentColor <= 7;
}

inline bool isSupportedTileSize(int w, int h) {
    return (w == 1 && h == 1) || (w == 2 && h == 1) ||
           (w == 3 && h == 1) || (w == 2 && h == 2) ||
           (w == 3 && h == 2);
}

inline bool overlap(const Tile& a, const Tile& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

inline bool placeable(const Workspace& desk, const Tile& tile, int x, int y, int ignore = -1) {
    // Check against the grid before adding dimensions; x + w can overflow for
    // malformed coordinates loaded from a user-editable layout file.
    if (x < 0 || y < 0 || x >= Columns || y >= Rows ||
        !isSupportedTileSize(tile.w, tile.h) ||
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
