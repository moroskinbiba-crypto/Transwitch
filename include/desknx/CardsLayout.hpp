#pragma once

#include "Model.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace desknx::cards {

struct Box {
    int x, y, w, h;
    constexpr bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

inline constexpr int Width = 1280;
inline constexpr int Height = 720;
inline constexpr int RailTop = 143;
inline constexpr int RailBottom = 484;
inline constexpr int MaxCards = model::Columns * model::Rows;
inline constexpr Box Profile{314, 22, 554, 54};
inline constexpr Box Detail{174, 494, 932, 126};
inline constexpr Box Primary{814, 519, 194, 55};
inline constexpr Box Actions{1022, 519, 56, 55};
inline constexpr Box Footer{28, 635, 1224, 64};
inline constexpr Box AddDesk{770, 642, 52, 48};
inline constexpr Box Workspaces{832, 642, 52, 48};
inline constexpr Box WorkspaceCount{900, 642, 150, 48};
inline constexpr Box Arrange{1096, 642, 128, 48};
inline constexpr int VisibleTabs = 5;
inline constexpr Box tab(int slot) { return {50 + slot * 142, 642, 132, 48}; }

enum class Tool { Library, Gallery, Controllers, Tools, Power, Settings, Count };
inline constexpr Box tool(Tool which) {
    return {892 + static_cast<int>(which) * 59, 23, 50, 50};
}

// A view of the saved workspace ordered by grid origin, never by insertion
// order. Resizing, moving or editing a shortcut keeps a single source of truth.
struct Deck {
    std::array<int, MaxCards> indices{};
    int size = 0;
    int positionOf(int tileIndex) const {
        for (int i = 0; i < size; ++i)
            if (indices[static_cast<std::size_t>(i)] == tileIndex) return i;
        return 0; // A focused empty grid cell selects the first card in browse mode.
    }
    int step(int current, int delta) const {
        if (size == 0) return -1;
        return std::clamp(std::clamp(current, 0, size - 1) + delta, 0, size - 1);
    }
};

inline Deck makeDeck(const model::Workspace& workspace) {
    std::array<int, MaxCards> origins{};
    origins.fill(-1);
    for (std::size_t i = 0; i < workspace.tiles.size(); ++i) {
        const auto& tile = workspace.tiles[i];
        if (tile.x < 0 || tile.y < 0 || tile.x >= model::Columns || tile.y >= model::Rows)
            continue;
        int& origin = origins[static_cast<std::size_t>(tile.y * model::Columns + tile.x)];
        if (origin < 0) origin = static_cast<int>(i);
    }
    Deck deck;
    for (int index : origins)
        if (index >= 0) deck.indices[static_cast<std::size_t>(deck.size++)] = index;
    return deck;
}

inline bool hasApp(const model::Workspace& workspace) {
    return std::any_of(workspace.tiles.begin(), workspace.tiles.end(), [](const model::Tile& tile) {
        return tile.kind == model::Kind::Game || tile.kind == model::Kind::Nro;
    });
}

inline Box card(int position, float anchor) {
    const float distance = static_cast<float>(position) - anchor;
    const float lift = std::max(0.f, 1.f - std::abs(distance));
    const int w = 195 + static_cast<int>(31.f * lift);
    const int h = 291 + static_cast<int>(39.f * lift);
    const int center = Width / 2 + static_cast<int>(distance * 223.f);
    return {center - w / 2, RailTop + 8 + (330 - h) / 2, w, h};
}

inline bool visible(Box box) { return box.x < Width && box.x + box.w > 0; }

inline int hitCard(int size, float anchor, int x, int y) {
    if (x < 0 || x >= Width || y < RailTop || y >= RailBottom) return -1;
    for (int i = 0; i < size; ++i)
        if (card(i, anchor).contains(x, y)) return i;
    return -1;
}

inline int swipeStep(int dx, int dy) {
    const int horizontal = std::abs(dx);
    if (horizontal < 44 || horizontal <= std::abs(dy)) return 0;
    const int steps = std::clamp((horizontal + 100) / 170, 1, 3);
    return dx < 0 ? steps : -steps;
}

} // namespace desknx::cards
