#pragma once

#include <algorithm>

namespace desknx::widget_layout {

enum class Shape {
    Compact,   // 1x1
    Banner,    // 2x1 / 3x1
    Landscape, // square-ish wide widgets
    Portrait   // 2x3
};

struct Metrics {
    int x{0}, y{0}, width{0}, height{0};
    int left{0}, right{0}, center{0};
    int headerY{0}, bodyTop{0}, bodyBottom{0};
    int rowStep{28}, maxRows{1};
    Shape shape{Shape::Compact};

    int rowY(int index) const {
        const int desired = bodyTop + std::max(0, index) * rowStep;
        return std::min(desired, std::max(bodyTop, bodyBottom - 8));
    }

    int clampY(int value, int contentHeight = 8) const {
        const int minY = y + 8;
        const int maxY = std::max(minY, y + height - contentHeight - 8);
        return std::max(minY, std::min(maxY, value));
    }

    bool compact() const { return shape == Shape::Compact; }
    bool banner() const { return shape == Shape::Banner; }
    bool portrait() const { return shape == Shape::Portrait; }
};

inline Metrics measure(int x, int y, int width, int height,
                       int tileWidth, int tileHeight) {
    Metrics out{};
    out.x = x;
    out.y = y;
    out.width = std::max(1, width);
    out.height = std::max(1, height);
    const int pad = std::max(12, std::min(20, out.width / 12));
    out.left = x + pad;
    out.right = x + out.width - pad;
    if (out.right <= out.left) out.right = out.left + 1;
    out.center = x + out.width / 2;

    if (tileWidth == 1 && tileHeight == 1) out.shape = Shape::Compact;
    else if (tileHeight == 1) out.shape = Shape::Banner;
    else if (tileHeight > tileWidth) out.shape = Shape::Portrait;
    else out.shape = Shape::Landscape;

    out.headerY = y + std::max(10, std::min(18, out.height / 12));
    out.bodyTop = y + std::max(28, std::min(52, out.height / 4));
    out.bodyBottom = y + out.height - std::max(14, std::min(22, out.height / 10));
    if (out.bodyBottom < out.bodyTop + 8) out.bodyBottom = out.bodyTop + 8;

    const int usable = out.bodyBottom - out.bodyTop;
    out.rowStep = std::max(28, std::min(42, usable / 4));
    out.maxRows = std::max(1, std::min(6, usable / out.rowStep));
    if (out.shape == Shape::Compact) out.maxRows = 1;
    else if (out.shape == Shape::Banner) out.maxRows = std::min(2, out.maxRows);
    return out;
}

} // namespace desknx::widget_layout
