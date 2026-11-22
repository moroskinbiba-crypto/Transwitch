#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace desknx::visual::geometry {
struct SpanRect { int x{0}; int y{0}; int w{0}; int h{0}; };
constexpr int MaxCornerRadius = 16;
constexpr std::size_t MaxRoundedFillSpans = static_cast<std::size_t>(MaxCornerRadius * 2 + 1);

// Creates non-overlapping horizontal spans for one batched SDL fill operation.
inline std::size_t roundedFillSpans(int x, int y, int width, int height, int radius,
                                    SpanRect* output, std::size_t capacity) {
    if (!output || capacity == 0 || width <= 0 || height <= 0) return 0;
    const int maxBySize = std::min(width / 2, height / 2);
    const int corner = std::max(0, std::min(radius, std::min(MaxCornerRadius, maxBySize)));
    if (corner == 0) { output[0] = {x, y, width, height}; return 1; }

    std::size_t count = 0;
    const auto append = [&](int sx, int sy, int sw, int sh) {
        if (sw <= 0 || sh <= 0 || count >= capacity) return;
        output[count++] = {sx, sy, sw, sh};
    };
    if (height > corner * 2) append(x, y + corner, width, height - corner * 2);
    for (int row = 0; row < corner; ++row) {
        const double dy = static_cast<double>(corner - row);
        const double inside = std::sqrt(std::max(
            0.0, static_cast<double>(corner * corner) - dy * dy));
        const int inset = static_cast<int>(
            std::ceil(static_cast<double>(corner) - inside));
        const int spanWidth = width - inset * 2;
        append(x + inset, y + row, spanWidth, 1);
        append(x + inset, y + height - 1 - row, spanWidth, 1);
    }
    return count;
}
} // namespace desknx::visual::geometry
