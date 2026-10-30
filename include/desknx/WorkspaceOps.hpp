#pragma once

#include "Model.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace desknx::workspace_ops {

struct CellPoint {
    int x{0};
    int y{0};
};

// Convert a touch coordinate to a logical grid cell. Gaps can be accepted while
// dragging so a finger may move naturally between cells; taps must land on a
// real cell or on a tile's actual bounds.
inline bool cellFromPoint(int px, int py, int left, int top,
                          int cellWidth, int cellHeight,
                          int gapX, int gapY, bool allowGaps,
                          CellPoint& point) {
    if (cellWidth <= 0 || cellHeight <= 0 || gapX < 0 || gapY < 0)
        return false;
    const int strideX = cellWidth + gapX;
    const int strideY = cellHeight + gapY;
    const int relativeX = px - left;
    const int relativeY = py - top;
    const int totalWidth = model::Columns * cellWidth +
                           (model::Columns - 1) * gapX;
    const int totalHeight = model::Rows * cellHeight +
                            (model::Rows - 1) * gapY;
    if (relativeX < 0 || relativeY < 0 ||
        relativeX >= totalWidth || relativeY >= totalHeight)
        return false;
    const int column = relativeX / strideX;
    const int row = relativeY / strideY;
    if (column < 0 || column >= model::Columns ||
        row < 0 || row >= model::Rows)
        return false;
    if (!allowGaps &&
        (relativeX % strideX >= cellWidth ||
         relativeY % strideY >= cellHeight))
        return false;
    point = {column, row};
    return true;
}

// Resolve a touch against the rendered rectangle of a tile. Spanning tiles
// include their internal grid gutters in the same way as their SDL rectangle.
inline int tileAtPoint(const model::Workspace& desk, int px, int py,
                       int left, int top, int cellWidth, int cellHeight,
                       int gapX, int gapY) {
    for (size_t i = 0; i < desk.tiles.size(); ++i) {
        const auto& tile = desk.tiles[i];
        const int x = left + tile.x * (cellWidth + gapX);
        const int y = top + tile.y * (cellHeight + gapY);
        const int width = tile.w * cellWidth + (tile.w - 1) * gapX;
        const int height = tile.h * cellHeight + (tile.h - 1) * gapY;
        if (px >= x && px < x + width && py >= y && py < y + height)
            return static_cast<int>(i);
    }
    return -1;
}

inline bool moveSelectedTile(model::Workspace& desk, int& selectedX,
                             int& selectedY, int dx, int dy) {
    const int index = model::itemAt(desk, selectedX, selectedY);
    if (index < 0 || (dx == 0 && dy == 0)) return false;
    auto& tile = desk.tiles[static_cast<size_t>(index)];
    const int nextX = tile.x + dx;
    const int nextY = tile.y + dy;
    if (!model::placeable(desk, tile, nextX, nextY, index)) return false;
    tile.x = nextX;
    tile.y = nextY;
    selectedX = nextX;
    selectedY = nextY;
    return true;
}

inline bool resizeSelectedTile(model::Workspace& desk, int& selectedX,
                               int& selectedY, bool bigger) {
    const int index = model::itemAt(desk, selectedX, selectedY);
    if (index < 0) return false;
    constexpr std::array<std::pair<int, int>, 4> sizes{{
        {1, 1}, {2, 1}, {2, 2}, {3, 2}
    }};
    auto& tile = desk.tiles[static_cast<size_t>(index)];
    size_t current = 0;
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i].first == tile.w && sizes[i].second == tile.h) {
            current = i;
            break;
        }
    }
    const size_t next = bigger
        ? std::min(sizes.size() - 1, current + 1)
        : (current == 0 ? 0 : current - 1);
    if (next == current) return false;
    model::Tile proposed = tile;
    proposed.w = sizes[next].first;
    proposed.h = sizes[next].second;
    if (!model::placeable(desk, proposed, tile.x, tile.y, index)) return false;
    tile.w = proposed.w;
    tile.h = proposed.h;
    // Keep focus on the tile itself even when the old cursor cell is removed
    // by shrinking a 2x2 or 3x2 tile.
    selectedX = tile.x;
    selectedY = tile.y;
    return true;
}

inline bool moveSelectedTileToWorkspace(std::vector<model::Workspace>& workspaces,
                                        int& activeWorkspace,
                                        int& selectedX, int& selectedY,
                                        int targetWorkspace) {
    if (activeWorkspace < 0 ||
        activeWorkspace >= static_cast<int>(workspaces.size()) ||
        targetWorkspace < 0 ||
        targetWorkspace >= static_cast<int>(workspaces.size()) ||
        targetWorkspace == activeWorkspace)
        return false;
    auto& source = workspaces[static_cast<size_t>(activeWorkspace)];
    const int index = model::itemAt(source, selectedX, selectedY);
    if (index < 0) return false;
    model::Tile tile = source.tiles[static_cast<size_t>(index)];
    auto& destination = workspaces[static_cast<size_t>(targetWorkspace)];
    int nextX = tile.x;
    int nextY = tile.y;
    if (!model::placeable(destination, tile, nextX, nextY) &&
        !model::firstFree(destination, tile, nextX, nextY))
        return false;
    source.tiles.erase(source.tiles.begin() + index);
    tile.x = nextX;
    tile.y = nextY;
    destination.tiles.push_back(std::move(tile));
    activeWorkspace = targetWorkspace;
    selectedX = nextX;
    selectedY = nextY;
    return true;
}

} // namespace desknx::workspace_ops
