#pragma once

#include "Model.hpp"
#include "WorkspaceOps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace desknx::homenx {

// A platform-neutral projection contract. The HomeNX adapter can turn these
// entries into AppEntry/GlossyIcon objects without changing Transwitch's
// persistent Workspace model or confusing catalogue order with desktop order.
enum class EntryKind : std::uint8_t {
    InstalledTitle,
    HomebrewNro,
    ClockWidget,
    BatteryWidget,
    SystemShortcut,
    RecentWidget,
    StorageWidget,
    CalendarWidget,
    Unsupported
};

enum class CellRole : std::uint8_t {
    Empty,
    Primary,
    Continuation
};

struct Cell {
    int entryIndex{-1};
    CellRole role{CellRole::Empty};
};

struct Entry {
    EntryKind kind{EntryKind::Unsupported};
    int sourceTileIndex{-1};
    std::string stableKey;
    std::string title;
    std::string data;
    std::string unknownTypeCode;
    std::uint64_t titleId{0}; // populated only for installed software
    int x{0};
    int y{0};
    int columns{1};
    int rows{1};
    int color{0};
};

struct WorkspaceProjection {
    std::string name;
    std::string backgroundPath;
    int backgroundFit{0};
    int artworkMode{0};
    int labelOpacity{196};
    int accentColor{-1};
    std::array<Cell, model::Columns * model::Rows> cells{};
    std::vector<Entry> entries;
};

enum class ProjectionError : std::uint8_t {
    None,
    InvalidName,
    InvalidAppearance,
    TooManyTiles,
    InvalidText,
    InvalidGameTitleId,
    InvalidHomebrewPath,
    InvalidGeometry
};

inline bool safeText(const std::string& value, std::size_t maxBytes) {
    if (value.size() > maxBytes || value.find('\0') != std::string::npos)
        return false;
    for (unsigned char byte : value) {
        if (byte == '\t' || byte == '\n' || byte == '\r')
            return false;
    }
    return true;
}

inline bool safeHomebrewPath(const std::string& path) {
    if (path.size() < 10 || path.size() > 512 ||
        path.compare(0, 6, "sdmc:/") != 0 ||
        path.find("..") != std::string::npos ||
        path.find('\\') != std::string::npos ||
        !safeText(path, 512))
        return false;

    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;

    std::string extension = path.substr(dot);
    for (char& ch : extension) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return extension == ".nro";
}

inline bool parseTitleId(const std::string& text, std::uint64_t& value) {
    if (!model::isTitleId(text))
        return false;
    value = 0;
    for (unsigned char ch : text) {
        const unsigned digit = ch >= '0' && ch <= '9'
            ? static_cast<unsigned>(ch - '0')
            : static_cast<unsigned>((ch >= 'a' && ch <= 'f')
                ? ch - 'a' + 10 : ch - 'A' + 10);
        value = (value << 4) | digit;
    }
    return value != 0;
}

inline EntryKind projectedKind(model::Kind kind) {
    switch (kind) {
        case model::Kind::Game: return EntryKind::InstalledTitle;
        case model::Kind::Nro: return EntryKind::HomebrewNro;
        case model::Kind::Clock: return EntryKind::ClockWidget;
        case model::Kind::Battery: return EntryKind::BatteryWidget;
        case model::Kind::System: return EntryKind::SystemShortcut;
        case model::Kind::Recent: return EntryKind::RecentWidget;
        case model::Kind::Storage: return EntryKind::StorageWidget;
        case model::Kind::Calendar: return EntryKind::CalendarWidget;
        case model::Kind::Unsupported: return EntryKind::Unsupported;
    }
    return EntryKind::Unsupported;
}

inline std::string entryKey(const model::Tile& tile, std::size_t sourceIndex) {
    // Coordinates are deliberately not part of the key: a drag changes a
    // tile's cell, not its identity. The source index disambiguates repeated
    // widget/shortcut instances in the same persisted workspace.
    return model::typeCode(tile) + ":" + tile.data + "#" +
           std::to_string(sourceIndex);
}

inline bool projectWorkspace(const model::Workspace& workspace,
                             WorkspaceProjection& output,
                             ProjectionError* error = nullptr) {
    output = WorkspaceProjection{};
    if (error)
        *error = ProjectionError::None;

    const auto fail = [&](ProjectionError reason) {
        output = WorkspaceProjection{};
        if (error)
            *error = reason;
        return false;
    };

    if (!workspace_ops::isValidWorkspaceName(workspace.name))
        return fail(ProjectionError::InvalidName);
    if (!model::isValidWorkspaceAppearance(workspace))
        return fail(ProjectionError::InvalidAppearance);
    if (workspace.tiles.size() > static_cast<std::size_t>(model::MaxItems))
        return fail(ProjectionError::TooManyTiles);

    output.name = workspace.name;
    output.backgroundPath = workspace.backgroundPath;
    output.backgroundFit = workspace.backgroundFit;
    output.artworkMode = workspace.artworkMode;
    output.labelOpacity = workspace.labelOpacity;
    output.accentColor = workspace.accentColor;
    output.entries.reserve(workspace.tiles.size());

    model::Workspace occupied;
    occupied.name = workspace.name;

    for (std::size_t i = 0; i < workspace.tiles.size(); ++i) {
        const model::Tile& tile = workspace.tiles[i];
        if (!safeText(tile.title, 512) || !safeText(tile.data, 512) ||
            !safeText(tile.unknownTypeCode, 16))
            return fail(ProjectionError::InvalidText);

        if (!model::placeable(occupied, tile, tile.x, tile.y))
            return fail(ProjectionError::InvalidGeometry);

        Entry entry;
        entry.kind = projectedKind(tile.kind);
        entry.sourceTileIndex = static_cast<int>(i);
        entry.stableKey = entryKey(tile, i);
        entry.title = tile.title;
        entry.data = tile.data;
        entry.unknownTypeCode = tile.unknownTypeCode;
        entry.x = tile.x;
        entry.y = tile.y;
        entry.columns = tile.w;
        entry.rows = tile.h;
        entry.color = tile.color;

        if (tile.kind == model::Kind::Game) {
            if (!parseTitleId(tile.data, entry.titleId))
                return fail(ProjectionError::InvalidGameTitleId);
        } else if (tile.kind == model::Kind::Nro) {
            if (!safeHomebrewPath(tile.data))
                return fail(ProjectionError::InvalidHomebrewPath);
        }

        const int entryIndex = static_cast<int>(output.entries.size());
        for (int dy = 0; dy < tile.h; ++dy) {
            for (int dx = 0; dx < tile.w; ++dx) {
                const int cellIndex = (tile.y + dy) * model::Columns + tile.x + dx;
                Cell& cell = output.cells[static_cast<std::size_t>(cellIndex)];
                if (cell.role != CellRole::Empty)
                    return fail(ProjectionError::InvalidGeometry);
                cell.entryIndex = entryIndex;
                cell.role = (dx == 0 && dy == 0)
                    ? CellRole::Primary : CellRole::Continuation;
            }
        }

        output.entries.push_back(std::move(entry));
        occupied.tiles.push_back(tile);
    }

    return true;
}

} // namespace desknx::homenx

{"content":"#pragma once\r\n\r\n#include \"Model.hpp\"\r\n#include \"WorkspaceOps.hpp\"\r\n\r\n#include <array>\r\n#include <cstddef>\r\n#include <cstdint>\r\n#include <string>\r\n#include <vector>\r\n\r\nnamespace desknx::homenx {\r\n\r\n// A platform-neutral projection contract. The HomeNX adapter can turn these\r\n// entries into AppEntry/GlossyIcon objects without changing Transwitch's\r\n// persistent Workspace model or confusing catalogue order with desktop order.\r\nenum class EntryKind : std::uint8_t {\r\n    InstalledTitle,\r\n    HomebrewNro,\r\n    ClockWidget,\r\n    BatteryWidget,\r\n    SystemShortcut,\r\n    RecentWidget,\r\n    StorageWidget,\r\n    CalendarWidget,\r\n    Unsupported\r\n};\r\n\r\nenum class CellRole : std::uint8_t {\r\n    Empty,\r\n    Primary,\r\n    Continuation\r\n};\r\n\r\nstruct Cell {\r\n    int entryIndex{-1};\r\n    CellRole role{CellRole::Empty};\r\n};\r\n\r\nstruct Entry {\r\n    EntryKind kind{EntryKind::Unsupported};\r\n    int sourceTileIndex{-1};\r\n    std::string stableKey;\r\n    std::string title;\r\n    std::string data;\r\n    std::string unknownTypeCode;\r\n    std::uint64_t titleId{0}; // populated only for installed software\r\n    int x{0};\r\n    int y{0};\r\n    int columns{1};\r\n    int rows{1};\r\n    int color{0};\r\n};\r\n\r\nstruct WorkspaceProjection {\r\n    std::string name;\r\n    std::string backgroundPath;\r\n    int backgroundFit{0};\r\n    int artworkMode{0};\r\n    int labelOpacity{196};\r\n    int accentColor{-1};\r\n    std::array<Cell, model::Columns * model::Rows> cells{};\r\n    std::vector<Entry> entries;\r\n};\r\n\r\nenum class ProjectionError : std::uint8_t {\r\n    None,\r\n    InvalidName,\r\n    InvalidAppearance,\r\n    TooManyTiles,\r\n    InvalidText,\r\n    InvalidGameTitleId,\r\n    InvalidHomebrewPath,\r\n    InvalidGeometry\r\n};\r\n\r\ninline bool safeText(const std::string& value, std::size_t maxBytes) {\r\n    if (value.size() > maxBytes || value.find('\\0') != std::string::npos)\r\n        return false;\r\n    for (unsigned char byte : value) {\r\n        if (byte == '\\t' || byte == '\\n' || byte == '\\r')\r\n            return false;\r\n    }\r\n    return true;\r\n}\r\n\r\ninline bool safeHomebrewPath(const std::string& path) {\r\n    if (path.size() < 10 || path.size() > 512 ||\r\n        path.compare(0, 6, \"sdmc:/\") != 0 ||\r\n        path.find(\"..\") != std::string::npos ||\r\n        path.find('\\\\') != std::string::npos ||\r\n        !safeText(path, 512))\r\n        return false;\r\n\r\n    const std::size_t dot = path.find_last_of('.');\r\n    if (dot == std::string::npos)\r\n        return false;\r\n\r\n    std::string extension = path.substr(dot);\r\n    for (char& ch : extension) {\r\n        if (ch >= 'A' && ch <= 'Z')\r\n            ch = static_cast<char>(ch - 'A' + 'a');\r\n    }\r\n    return extension == \".nro\";\r\n}\r\n\r\ninline bool parseTitleId(const std::string& text, std::uint64_t& value) {\r\n    if (!model::isTitleId(text))\r\n        return false;\r\n    value = 0;\r\n    for (unsigned char ch : text) {\r\n        const unsigned digit = ch >= '0' && ch <= '9'\r\n            ? static_cast<unsigned>(ch - '0')\r\n            : static_cast<unsigned>((ch >= 'a' && ch <= 'f')\r\n                ? ch - 'a' + 10 : ch - 'A' + 10);\r\n        value = (value << 4) | digit;\r\n    }\r\n    return value != 0;\r\n}\r\n\r\ninline EntryKind projectedKind(model::Kind kind) {\r\n    switch (kind) {\r\n        case model::Kind::Game: return EntryKind::InstalledTitle;\r\n        case model::Kind::Nro: return EntryKind::HomebrewNro;\r\n        case model::Kind::Clock: return EntryKind::ClockWidget;\r\n        case model::Kind::Battery: return EntryKind::BatteryWidget;\r\n        case model::Kind::System: return EntryKind::SystemShortcut;\r\n        case model::Kind::Recent: return EntryKind::RecentWidget;\r\n        case model::Kind::Storage: return EntryKind::StorageWidget;\r\n        case model::Kind::Calendar: return EntryKind::CalendarWidget;\r\n        case model::Kind::Unsupported: return EntryKind::Unsupported;\r\n    }\r\n    return EntryKind::Unsupported;\r\n}\r\n\r\ninline std::string entryKey(const model::Tile& tile, std::size_t sourceIndex) {\r\n    // Coordinates are deliberately not part of the key: a drag changes a\r\n    // tile's cell, not its identity. The source index disambiguates repeated\r\n    // widget/shortcut instances in the same persisted workspace.\r\n    return model::typeCode(tile) + \":\" + tile.data + \"#\" +\r\n           std::to_string(sourceIndex);\r\n}\r\n\r\ninline bool projectWorkspace(const model::Workspace& workspace,\r\n                             WorkspaceProjection& output,\r\n                             ProjectionError* error = nullptr) {\r\n    output = WorkspaceProjection{};\r\n    if (error)\r\n        *error = ProjectionError::None;\r\n\r\n    const auto fail = [&](ProjectionError reason) {\r\n        output = WorkspaceProjection{};\r\n        if (error)\r\n            *error = reason;\r\n        return false;\r\n    };\r\n\r\n    if (!workspace_ops::isValidWorkspaceName(workspace.name))\r\n        return fail(ProjectionError::InvalidName);\r\n    if (!model::isValidWorkspaceAppearance(workspace))\r\n        return fail(ProjectionError::InvalidAppearance);\r\n    if (workspace.tiles.size() > static_cast<std::size_t>(model::MaxItems))\r\n        return fail(ProjectionError::TooManyTiles);\r\n\r\n    output.name = workspace.name;\r\n    output.backgroundPath = workspace.backgroundPath;\r\n    output.backgroundFit = workspace.backgroundFit;\r\n    output.artworkMode = workspace.artworkMode;\r\n    output.labelOpacity = workspace.labelOpacity;\r\n    output.accentColor = workspace.accentColor;\r\n    output.entries.reserve(workspace.tiles.size());\r\n\r\n    model::Workspace occupied;\r\n    occupied.name = workspace.name;\r\n\r\n    for (std::size_t i = 0; i < workspace.tiles.size(); ++i) {\r\n        const model::Tile& tile = workspace.tiles[i];\r\n        if (!safeText(tile.title, 512) || !safeText(tile.data, 512) ||\r\n            !safeText(tile.unknownTypeCode, 16))\r\n            return fail(ProjectionError::InvalidText);\r\n\r\n        if (!model::placeable(occupied, tile, tile.x, tile.y))\r\n            return fail(ProjectionError::InvalidGeometry);\r\n\r\n        Entry entry;\r\n        entry.kind = projectedKind(tile.kind);\r\n        entry.sourceTileIndex = static_cast<int>(i);\r\n        entry.stableKey = entryKey(tile, i);\r\n        entry.title = tile.title;\r\n        entry.data = tile.data;\r\n        entry.unknownTypeCode = tile.unknownTypeCode;\r\n        entry.x = tile.x;\r\n        entry.y = tile.y;\r\n        entry.columns = tile.w;\r\n        entry.rows = tile.h;\r\n        entry.color = tile.color;\r\n\r\n        if (tile.kind == model::Kind::Game) {\r\n            if (!parseTitleId(tile.data, entry.titleId))\r\n                return fail(ProjectionError::InvalidGameTitleId);\r\n        } else if (tile.kind == model::Kind::Nro) {\r\n            if (!safeHomebrewPath(tile.data))\r\n                return fail(ProjectionError::InvalidHomebrewPath);\r\n        }\r\n\r\n        const int entryIndex = static_cast<int>(output.entries.size());\r\n        for (int dy = 0; dy < tile.h; ++dy) {\r\n            for (int dx = 0; dx < tile.w; ++dx) {\r\n                const int cellIndex = (tile.y + dy) * model::Columns + tile.x + dx;\r\n                Cell& cell = output.cells[static_cast<std::size_t>(cellIndex)];\r\n                if (cell.role != CellRole::Empty)\r\n                    return fail(ProjectionError::InvalidGeometry);\r\n                cell.entryIndex = entryIndex;\r\n                cell.role = (dx == 0 && dy == 0)\r\n                    ? CellRole::Primary : CellRole::Continuation;\r\n            }\r\n        }\r\n\r\n        output.entries.push_back(std::move(entry));\r\n        occupied.tiles.push_back(tile);\r\n    }\r\n\r\n    return true;\r\n}\r\n\r\n} // namespace desknx::homenx\r\n"}