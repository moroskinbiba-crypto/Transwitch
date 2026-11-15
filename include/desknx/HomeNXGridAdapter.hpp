#pragma once

#include "HomeNXWorkspaceAdapter.hpp"

#include <cstddef>
#include <cstdlib>
#include <vector>

namespace desknx::homenx {

// Android-style focus model for one projected workspace.
//
// The menu never stores a focus order. It asks, for the cell that currently has
// focus, which cell the D-pad should move to. Deriving the answer per keypress
// keeps the projection the single source of truth: a tile that moved, resized
// or was deleted needs no focus table rebuilt and cannot desynchronise from
// what is drawn.
//
// Rules the native menu can rely on:
//   * empty cells never receive focus;
//   * a multi-cell tile is one focus stop - its continuation cells resolve to
//     the primary cell;
//   * movement is directional and never wraps, so focus can settle on an edge;
//   * a candidate must overlap the current tile on the perpendicular axis; when
//     nothing overlaps, the nearest tile in that direction is used instead of
//     dead-ending the D-pad.

enum class Direction : unsigned char { Up, Down, Left, Right };

struct GridCellRef {
    int x{-1};
    int y{-1};

    bool valid() const { return x >= 0 && y >= 0; }
};

inline bool sameCell(const GridCellRef& a, const GridCellRef& b) {
    return a.x == b.x && a.y == b.y;
}

namespace grid_detail {

inline std::size_t cellIndex(int x, int y) {
    return static_cast<std::size_t>(y * model::Columns + x);
}

inline const Cell& cellAt(const WorkspaceProjection& projection, int x, int y) {
    return projection.cells[cellIndex(x, y)];
}

inline bool inBounds(int x, int y) {
    return x >= 0 && y >= 0 && x < model::Columns && y < model::Rows;
}

// Distance along the movement axis, or -1 when the entry is not in that
// direction at all.
inline int axisDistance(const Entry& entry, int fromX, int fromY, int fromW, int fromH,
                        Direction direction) {
    switch (direction) {
        case Direction::Right:
            return entry.x >= fromX + fromW ? entry.x - (fromX + fromW) : -1;
        case Direction::Left:
            return entry.x + entry.columns <= fromX ? fromX - (entry.x + entry.columns) : -1;
        case Direction::Down:
            return entry.y >= fromY + fromH ? entry.y - (fromY + fromH) : -1;
        case Direction::Up:
            return entry.y + entry.rows <= fromY ? fromY - (entry.y + entry.rows) : -1;
    }
    return -1;
}

inline bool perpendicularOverlap(const Entry& entry, int fromX, int fromY, int fromW, int fromH,
                                 Direction direction) {
    if (direction == Direction::Left || direction == Direction::Right) {
        return entry.y < fromY + fromH && entry.y + entry.rows > fromY;
    }
    return entry.x < fromX + fromW && entry.x + entry.columns > fromX;
}

inline int centreGap(const Entry& entry, int fromX, int fromY, int fromW, int fromH,
                     Direction direction) {
    if (direction == Direction::Left || direction == Direction::Right) {
        const int a = entry.y * 2 + entry.rows;
        const int b = fromY * 2 + fromH;
        return std::abs(a - b);
    }
    const int a = entry.x * 2 + entry.columns;
    const int b = fromX * 2 + fromW;
    return std::abs(a - b);
}

} // namespace grid_detail

// Resolves any cell - primary or continuation - to the top-left cell of the
// tile that owns it. Invalid for empty or out-of-range cells.
inline GridCellRef primaryOf(const WorkspaceProjection& projection, int x, int y) {
    if (!grid_detail::inBounds(x, y)) return {};
    const Cell& cell = grid_detail::cellAt(projection, x, y);
    if (cell.role == CellRole::Empty || cell.entryIndex < 0) return {};
    const Entry& entry = projection.entries[static_cast<std::size_t>(cell.entryIndex)];
    return GridCellRef{entry.x, entry.y};
}

// The entry that owns a cell (primary or continuation); nullptr for empty cells.
inline const Entry* entryAt(const WorkspaceProjection& projection, int x, int y) {
    if (!grid_detail::inBounds(x, y)) return nullptr;
    const Cell& cell = grid_detail::cellAt(projection, x, y);
    if (cell.role == CellRole::Empty || cell.entryIndex < 0) return nullptr;
    return &projection.entries[static_cast<std::size_t>(cell.entryIndex)];
}

// Entries ordered the way a user reads the desktop: top-left to bottom-right by
// primary cell. Continuation cells are skipped, so each tile appears once.
inline std::vector<const Entry*> displayOrder(const WorkspaceProjection& projection) {
    std::vector<const Entry*> ordered;
    ordered.reserve(projection.entries.size());
    for (int y = 0; y < model::Rows; ++y) {
        for (int x = 0; x < model::Columns; ++x) {
            const Cell& cell = grid_detail::cellAt(projection, x, y);
            if (cell.role == CellRole::Primary && cell.entryIndex >= 0)
                ordered.push_back(&projection.entries[static_cast<std::size_t>(cell.entryIndex)]);
        }
    }
    return ordered;
}

// Index into projection.entries of the tile under a cell, or -1.
inline int entryIndexAt(const WorkspaceProjection& projection, int x, int y) {
    if (!grid_detail::inBounds(x, y)) return -1;
    const Cell& cell = grid_detail::cellAt(projection, x, y);
    return cell.role == CellRole::Empty ? -1 : cell.entryIndex;
}

// Cell that the D-pad should move to, or an invalid ref when there is nothing
// further in that direction (focus then stays where it is).
inline GridCellRef focusNeighbour(const WorkspaceProjection& projection, int x, int y,
                                  Direction direction) {
    const Entry* current = entryAt(projection, x, y);
    if (current == nullptr) {
        // Focus on an empty cell should not happen; recover to the first entry
        // in reading order instead of dropping input on the floor.
        const auto ordered = displayOrder(projection);
        if (ordered.empty()) return {};
        return GridCellRef{ordered.front()->x, ordered.front()->y};
    }

    const GridCellRef start{current->x, current->y};
    const int startW = current->columns;
    const int startH = current->rows;

    const Entry* bestOverlapping = nullptr;
    const Entry* bestFallback = nullptr;
    int bestOverlappingDistance = 0;
    int bestOverlappingGap = 0;
    int bestFallbackDistance = 0;
    int bestFallbackGap = 0;

    for (const Entry& entry : projection.entries) {
        if (entry.x == start.x && entry.y == start.y) continue;

        const int distance =
            grid_detail::axisDistance(entry, start.x, start.y, startW, startH, direction);
        if (distance < 0) continue;

        const int gap =
            grid_detail::centreGap(entry, start.x, start.y, startW, startH, direction);
        const bool overlaps = grid_detail::perpendicularOverlap(
            entry, start.x, start.y, startW, startH, direction);

        const auto betterThan = [](int distance, int gap, const Entry* best,
                                   int bestDistance, int bestGap) {
            if (best == nullptr) return true;
            if (distance != bestDistance) return distance < bestDistance;
            return gap < bestGap;
        };

        if (overlaps) {
            if (betterThan(distance, gap, bestOverlapping, bestOverlappingDistance,
                           bestOverlappingGap)) {
                bestOverlapping = &entry;
                bestOverlappingDistance = distance;
                bestOverlappingGap = gap;
            }
        } else if (betterThan(distance, gap, bestFallback, bestFallbackDistance, bestFallbackGap)) {
            bestFallback = &entry;
            bestFallbackDistance = distance;
            bestFallbackGap = gap;
        }
    }

    const Entry* winner = bestOverlapping != nullptr ? bestOverlapping : bestFallback;
    if (winner == nullptr) return {};
    return GridCellRef{winner->x, winner->y};
}

} // namespace desknx::homenx
