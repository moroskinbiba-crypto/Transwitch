#pragma once

#include <algorithm>

namespace desknx::widget_layout {

// These are the six and only six tile sizes supported by Model.hpp.
// Keep this mapping explicit: 2x2, 3x2 and 2x3 have different content
// affordances and must not collapse into a single generic "large" layout.
enum class Shape {
    Compact,   // 1x1 — one key metric
    Banner2,   // 2x1 — key metric + one secondary item
    Banner3,   // 3x1 — key metric + two secondary items
    Standard,  // 2x2 — balanced two-column / two-row layout
    Dashboard, // 3x2 — up to six compact data cells
    Portrait   // 2x3 — vertical detail list
};

struct Metrics {
    int x{0}, y{0}, width{0}, height{0};
    int left{0}, right{0}, center{0};
    int headerY{0}, bodyTop{0}, bodyBottom{0};
    int rowStep{28}, maxRows{1};
    int gridColumns{1}, gridRows{1}, capacity{1};
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

    int visibleItems(int requested) const {
        return std::max(0, std::min(capacity, requested));
    }

    bool compact() const { return shape == Shape::Compact; }
    bool banner() const { return shape == Shape::Banner2 || shape == Shape::Banner3; }
    bool banner2() const { return shape == Shape::Banner2; }
    bool banner3() const { return shape == Shape::Banner3; }
    bool standard() const { return shape == Shape::Standard; }
    bool dashboard() const { return shape == Shape::Dashboard; }
    bool portrait() const { return shape == Shape::Portrait; }
    bool horizontal() const { return banner(); }
};

inline Shape shapeFor(int tileWidth, int tileHeight) {
    if (tileWidth == 1 && tileHeight == 1) return Shape::Compact;
    if (tileWidth == 2 && tileHeight == 1) return Shape::Banner2;
    if (tileWidth == 3 && tileHeight == 1) return Shape::Banner3;
    if (tileWidth == 2 && tileHeight == 2) return Shape::Standard;
    if (tileWidth == 3 && tileHeight == 2) return Shape::Dashboard;
    if (tileWidth == 2 && tileHeight == 3) return Shape::Portrait;
    return Shape::Compact;
}

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
    out.shape = shapeFor(tileWidth, tileHeight);

    switch (out.shape) {
        case Shape::Compact:
            out.gridColumns = 1; out.gridRows = 1; out.capacity = 1;
            break;
        case Shape::Banner2:
            out.gridColumns = 2; out.gridRows = 1; out.capacity = 2;
            break;
        case Shape::Banner3:
            out.gridColumns = 3; out.gridRows = 1; out.capacity = 3;
            break;
        case Shape::Standard:
            out.gridColumns = 2; out.gridRows = 2; out.capacity = 4;
            break;
        case Shape::Dashboard:
            out.gridColumns = 3; out.gridRows = 2; out.capacity = 6;
            break;
        case Shape::Portrait:
            out.gridColumns = 2; out.gridRows = 3; out.capacity = 6;
            break;
    }

    out.headerY = y + std::max(10, std::min(18, out.height / 12));
    out.bodyTop = y + std::max(28, std::min(52, out.height / 4));
    out.bodyBottom = y + out.height - std::max(14, std::min(22, out.height / 10));
    if (out.bodyBottom < out.bodyTop + 8) out.bodyBottom = out.bodyTop + 8;

    out.maxRows = out.gridRows;
    const int usable = std::max(1, out.bodyBottom - out.bodyTop);
    const int minimumRowStep = out.portrait() ? 22 : (out.dashboard() ? 24 : 28);
    out.rowStep = std::max(minimumRowStep,
        std::min(42, usable / std::max(1, out.gridRows)));
    return out;
}

} // namespace desknx::widget_layout
