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

{"content":"#pragma once\n\n#include \"HomeNXWorkspaceAdapter.hpp\"\n\n#include <cstddef>\n#include <cstdlib>\n#include <vector>\n\nnamespace desknx::homenx {\n\n// Android-style focus model for one projected workspace.\n//\n// The menu never stores a focus order. It asks, for the cell that currently has\n// focus, which cell the D-pad should move to. Deriving the answer per keypress\n// keeps the projection the single source of truth: a tile that moved, resized\n// or was deleted needs no focus table rebuilt and cannot desynchronise from\n// what is drawn.\n//\n// Rules the native menu can rely on:\n//   * empty cells never receive focus;\n//   * a multi-cell tile is one focus stop - its continuation cells resolve to\n//     the primary cell;\n//   * movement is directional and never wraps, so focus can settle on an edge;\n//   * a candidate must overlap the current tile on the perpendicular axis; when\n//     nothing overlaps, the nearest tile in that direction is used instead of\n//     dead-ending the D-pad.\n\nenum class Direction : unsigned char { Up, Down, Left, Right };\n\nstruct GridCellRef {\n    int x{-1};\n    int y{-1};\n\n    bool valid() const { return x >= 0 && y >= 0; }\n};\n\ninline bool sameCell(const GridCellRef& a, const GridCellRef& b) {\n    return a.x == b.x && a.y == b.y;\n}\n\nnamespace grid_detail {\n\ninline std::size_t cellIndex(int x, int y) {\n    return static_cast<std::size_t>(y * model::Columns + x);\n}\n\ninline const Cell& cellAt(const WorkspaceProjection& projection, int x, int y) {\n    return projection.cells[cellIndex(x, y)];\n}\n\ninline bool inBounds(int x, int y) {\n    return x >= 0 && y >= 0 && x < model::Columns && y < model::Rows;\n}\n\n// Distance along the movement axis, or -1 when the entry is not in that\n// direction at all.\ninline int axisDistance(const Entry& entry, int fromX, int fromY, int fromW, int fromH,\n                        Direction direction) {\n    switch (direction) {\n        case Direction::Right:\n            return entry.x >= fromX + fromW ? entry.x - (fromX + fromW) : -1;\n        case Direction::Left:\n            return entry.x + entry.columns <= fromX ? fromX - (entry.x + entry.columns) : -1;\n        case Direction::Down:\n            return entry.y >= fromY + fromH ? entry.y - (fromY + fromH) : -1;\n        case Direction::Up:\n            return entry.y + entry.rows <= fromY ? fromY - (entry.y + entry.rows) : -1;\n    }\n    return -1;\n}\n\ninline bool perpendicularOverlap(const Entry& entry, int fromX, int fromY, int fromW, int fromH,\n                                 Direction direction) {\n    if (direction == Direction::Left || direction == Direction::Right) {\n        return entry.y < fromY + fromH && entry.y + entry.rows > fromY;\n    }\n    return entry.x < fromX + fromW && entry.x + entry.columns > fromX;\n}\n\ninline int centreGap(const Entry& entry, int fromX, int fromY, int fromW, int fromH,\n                     Direction direction) {\n    if (direction == Direction::Left || direction == Direction::Right) {\n        const int a = entry.y * 2 + entry.rows;\n        const int b = fromY * 2 + fromH;\n        return std::abs(a - b);\n    }\n    const int a = entry.x * 2 + entry.columns;\n    const int b = fromX * 2 + fromW;\n    return std::abs(a - b);\n}\n\n} // namespace grid_detail\n\n// Resolves any cell - primary or continuation - to the top-left cell of the\n// tile that owns it. Invalid for empty or out-of-range cells.\ninline GridCellRef primaryOf(const WorkspaceProjection& projection, int x, int y) {\n    if (!grid_detail::inBounds(x, y)) return {};\n    const Cell& cell = grid_detail::cellAt(projection, x, y);\n    if (cell.role == CellRole::Empty || cell.entryIndex < 0) return {};\n    const Entry& entry = projection.entries[static_cast<std::size_t>(cell.entryIndex)];\n    return GridCellRef{entry.x, entry.y};\n}\n\n// The entry that owns a cell (primary or continuation); nullptr for empty cells.\ninline const Entry* entryAt(const WorkspaceProjection& projection, int x, int y) {\n    if (!grid_detail::inBounds(x, y)) return nullptr;\n    const Cell& cell = grid_detail::cellAt(projection, x, y);\n    if (cell.role == CellRole::Empty || cell.entryIndex < 0) return nullptr;\n    return &projection.entries[static_cast<std::size_t>(cell.entryIndex)];\n}\n\n// Entries ordered the way a user reads the desktop: top-left to bottom-right by\n// primary cell. Continuation cells are skipped, so each tile appears once.\ninline std::vector<const Entry*> displayOrder(const WorkspaceProjection& projection) {\n    std::vector<const Entry*> ordered;\n    ordered.reserve(projection.entries.size());\n    for (int y = 0; y < model::Rows; ++y) {\n        for (int x = 0; x < model::Columns; ++x) {\n            const Cell& cell = grid_detail::cellAt(projection, x, y);\n            if (cell.role == CellRole::Primary && cell.entryIndex >= 0)\n                ordered.push_back(&projection.entries[static_cast<std::size_t>(cell.entryIndex)]);\n        }\n    }\n    return ordered;\n}\n\n// Index into projection.entries of the tile under a cell, or -1.\ninline int entryIndexAt(const WorkspaceProjection& projection, int x, int y) {\n    if (!grid_detail::inBounds(x, y)) return -1;\n    const Cell& cell = grid_detail::cellAt(projection, x, y);\n    return cell.role == CellRole::Empty ? -1 : cell.entryIndex;\n}\n\n// Cell that the D-pad should move to, or an invalid ref when there is nothing\n// further in that direction (focus then stays where it is).\ninline GridCellRef focusNeighbour(const WorkspaceProjection& projection, int x, int y,\n                                  Direction direction) {\n    const Entry* current = entryAt(projection, x, y);\n    if (current == nullptr) {\n        // Focus on an empty cell should not happen; recover to the first entry\n        // in reading order instead of dropping input on the floor.\n        const auto ordered = displayOrder(projection);\n        if (ordered.empty()) return {};\n        return GridCellRef{ordered.front()->x, ordered.front()->y};\n    }\n\n    const GridCellRef start{current->x, current->y};\n    const int startW = current->columns;\n    const int startH = current->rows;\n\n    const Entry* bestOverlapping = nullptr;\n    const Entry* bestFallback = nullptr;\n    int bestOverlappingDistance = 0;\n    int bestOverlappingGap = 0;\n    int bestFallbackDistance = 0;\n    int bestFallbackGap = 0;\n\n    for (const Entry& entry : projection.entries) {\n        if (entry.x == start.x && entry.y == start.y) continue;\n\n        const int distance =\n            grid_detail::axisDistance(entry, start.x, start.y, startW, startH, direction);\n        if (distance < 0) continue;\n\n        const int gap =\n            grid_detail::centreGap(entry, start.x, start.y, startW, startH, direction);\n        const bool overlaps = grid_detail::perpendicularOverlap(\n            entry, start.x, start.y, startW, startH, direction);\n\n        const auto betterThan = [](int distance, int gap, const Entry* best,\n                                   int bestDistance, int bestGap) {\n            if (best == nullptr) return true;\n            if (distance != bestDistance) return distance < bestDistance;\n            return gap < bestGap;\n        };\n\n        if (overlaps) {\n            if (betterThan(distance, gap, bestOverlapping, bestOverlappingDistance,\n                           bestOverlappingGap)) {\n                bestOverlapping = &entry;\n                bestOverlappingDistance = distance;\n                bestOverlappingGap = gap;\n            }\n        } else if (betterThan(distance, gap, bestFallback, bestFallbackDistance, bestFallbackGap)) {\n            bestFallback = &entry;\n            bestFallbackDistance = distance;\n            bestFallbackGap = gap;\n        }\n    }\n\n    const Entry* winner = bestOverlapping != nullptr ? bestOverlapping : bestFallback;\n    if (winner == nullptr) return {};\n    return GridCellRef{winner->x, winner->y};\n}\n\n} // namespace desknx::homenx\n"}