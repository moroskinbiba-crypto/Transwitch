#pragma once

#include <algorithm>
#include <cstdint>

namespace desknx::ui {

struct TileLabelLayout {
    int stripHeight{32};
    int firstLineOffset{8};
    int secondLineOffset{22};
    int maxCharacters{8};
    int cornerRadius{10};
    bool twoLines{false};
};

inline TileLabelLayout measureTileLabel(int width, int height,
                                        bool twoLines,
                                        int cornerRadius = 10) noexcept {
    const int safeWidth = std::max(1, width);
    const int safeHeight = std::max(1, height);
    TileLabelLayout layout;
    layout.twoLines = twoLines && safeHeight >= 52;
    layout.stripHeight = layout.twoLines ? 44 : 32;
    layout.stripHeight = std::min(layout.stripHeight, std::max(20, safeHeight));
    layout.firstLineOffset = layout.twoLines ? 3 : 8;
    layout.secondLineOffset = 22;
    layout.maxCharacters = std::max(8, (safeWidth - 20) / 12);
    layout.cornerRadius = std::max(0, std::min(
        cornerRadius, std::min(safeWidth / 2, safeHeight / 2)));
    return layout;
}

inline int clampTileLabelOpacity(int opacity) noexcept {
    return std::max(0, std::min(255, opacity));
}

} // namespace desknx::ui
