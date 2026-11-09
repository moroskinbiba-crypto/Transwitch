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
