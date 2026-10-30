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

inline size_t utf8Codepoints(const std::string& value) {
    size_t count=0;
    for(unsigned char byte:value)
        if((byte&0xC0)!=0x80)++count;
    return count;
}

inline bool isValidWorkspaceName(const std::string& name) {
    if(name.empty()||name.size()>124||utf8Codepoints(name)>31)return false;
    for(unsigned char byte:name)
        if(byte=='\t'||byte=='\n'||byte=='\r'||byte=='\0')
            return false;
    return true;
}

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
    int column = relativeX / strideX;
    int row = relativeY / strideY;
    const int offsetX = relativeX % strideX;
    const int offsetY = relativeY % strideY;
    if (!allowGaps &&
        (offsetX >= cellWidth || offsetY >= cellHeight))
        return false;
    // During a drag, map the gap to the next cell. This keeps the grab point
    // stable when a finger starts on a spanning tile's internal gutter.
    if (allowGaps && offsetX >= cellWidth && column < model::Columns - 1)
        ++column;
    if (allowGaps && offsetY >= cellHeight && row < model::Rows - 1)
        ++row;
    if (column < 0 || column >= model::Columns ||
        row < 0 || row >= model::Rows)
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

inline CellPoint tileOriginForPointer(CellPoint pointerCell,
                                         int grabOffsetX, int grabOffsetY) {
    return {pointerCell.x - grabOffsetX, pointerCell.y - grabOffsetY};
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
    auto& tile = desk.tiles[static_cast<size_t>(index)];

    // Preserve the original, predictable resize ladder for all existing
    // shapes. The 3x1 banner is a separate branch used by the Recent widget:
    // it can expand vertically to 3x2 or shrink to the compact 2x1 shape.
    int nextW = tile.w;
    int nextH = tile.h;
    if (tile.w == 1 && tile.h == 1) {
        if (!bigger) return false;
        nextW = 2; nextH = 1;
    } else if (tile.w == 2 && tile.h == 1) {
        if (bigger) { nextW = 2; nextH = 2; }
        else { nextW = 1; nextH = 1; }
    } else if (tile.w == 2 && tile.h == 2) {
        if (bigger) { nextW = 3; nextH = 2; }
        else { nextW = 2; nextH = 1; }
    } else if (tile.w == 3 && tile.h == 2) {
        if (!bigger) { nextW = 2; nextH = 2; }
        else return false;
    } else if (tile.w == 3 && tile.h == 1) {
        if (bigger) { nextW = 3; nextH = 2; }
        else { nextW = 2; nextH = 1; }
    } else {
        return false;
    }

    model::Tile proposed = tile;
    proposed.w = nextW;
    proposed.h = nextH;
    if (!model::placeable(desk, proposed, tile.x, tile.y, index)) return false;
    tile.w = proposed.w;
    tile.h = proposed.h;
    // Keep focus on the tile itself even when the old cursor cell is removed.
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

inline bool deleteWorkspace(std::vector<model::Workspace>& workspaces,
                            int& activeWorkspace, int workspaceIndex) {
    if (workspaces.size() <= 1 ||
        workspaceIndex < 0 ||
        workspaceIndex >= static_cast<int>(workspaces.size()) ||
        activeWorkspace < 0 ||
        activeWorkspace >= static_cast<int>(workspaces.size()))
        return false;

    workspaces.erase(workspaces.begin() + workspaceIndex);
    if (activeWorkspace > workspaceIndex) {
        --activeWorkspace;
    } else if (activeWorkspace == workspaceIndex) {
        // Prefer the workspace that followed the deleted one; when deleting
        // the last workspace, stay on the new last workspace.
        activeWorkspace = std::min(workspaceIndex,
                                   static_cast<int>(workspaces.size()) - 1);
    }
    return true;
}

} // namespace desknx::workspace_ops
