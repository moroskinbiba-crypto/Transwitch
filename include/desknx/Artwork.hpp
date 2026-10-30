#pragma once

#include <algorithm>
#include <cstdint>

namespace desknx::artwork {

struct Rect { int x{0}; int y{0}; int w{0}; int h{0}; };

enum class FitMode : int { Cover = 0, Contain = 1, Stretch = 2 };

// Return a destination that fully covers the viewport while preserving the
// source aspect ratio. The renderer clips this larger destination to the tile.
// Integer ratio arithmetic avoids floating-point off-by-one seams.
inline Rect coverDestination(int x, int y, int width, int height,
                             int sourceWidth, int sourceHeight) {
    if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    int outWidth = width;
    int outHeight = height;
    if (static_cast<std::int64_t>(width) * sourceHeight >=
        static_cast<std::int64_t>(height) * sourceWidth) {
        const std::int64_t scaled =
            (static_cast<std::int64_t>(sourceHeight) * width + sourceWidth - 1) /
            sourceWidth;
        outHeight = std::max(height, static_cast<int>(scaled));
    } else {
        const std::int64_t scaled =
            (static_cast<std::int64_t>(sourceWidth) * height + sourceHeight - 1) /
            sourceHeight;
        outWidth = std::max(width, static_cast<int>(scaled));
    }
    return {x + (width - outWidth) / 2, y + (height - outHeight) / 2,
            outWidth, outHeight};
}

// Keep the whole image visible, centered inside the viewport, with unused
// space around it instead of cropping or distorting its aspect ratio.
inline Rect containDestination(int x, int y, int width, int height,
                               int sourceWidth, int sourceHeight) {
    if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    int outWidth = width;
    int outHeight = height;
    if (static_cast<std::int64_t>(width) * sourceHeight <=
        static_cast<std::int64_t>(height) * sourceWidth) {
        const std::int64_t scaled =
            static_cast<std::int64_t>(sourceHeight) * width / sourceWidth;
        outHeight = std::max(1, std::min(height, static_cast<int>(scaled)));
    } else {
        const std::int64_t scaled =
            static_cast<std::int64_t>(sourceWidth) * height / sourceHeight;
        outWidth = std::max(1, std::min(width, static_cast<int>(scaled)));
    }
    return {x + (width - outWidth) / 2, y + (height - outHeight) / 2,
            outWidth, outHeight};
}

inline Rect destination(int x, int y, int width, int height,
                        int sourceWidth, int sourceHeight, FitMode mode) {
    if (mode == FitMode::Cover)
        return coverDestination(x, y, width, height, sourceWidth, sourceHeight);
    if (mode == FitMode::Contain)
        return containDestination(x, y, width, height, sourceWidth, sourceHeight);
    if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    return {x, y, width, height};
}

} // namespace desknx::artwork
