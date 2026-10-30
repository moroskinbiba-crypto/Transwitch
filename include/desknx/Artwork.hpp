#pragma once

#include <algorithm>
#include <cmath>

namespace desknx::artwork {

struct Rect { int x{0}; int y{0}; int w{0}; int h{0}; };

enum class FitMode : int { Cover = 0, Contain = 1, Stretch = 2 };

inline Rect coverDestination(int x, int y, int width, int height,
                             int sourceWidth, int sourceHeight) {
    if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    const double scale = std::max(static_cast<double>(width) / sourceWidth,
                                  static_cast<double>(height) / sourceHeight);
    const int outWidth = std::max(width, static_cast<int>(std::ceil(sourceWidth * scale)));
    const int outHeight = std::max(height, static_cast<int>(std::ceil(sourceHeight * scale)));
    return {x + (width - outWidth) / 2, y + (height - outHeight) / 2,
            outWidth, outHeight};
}

inline Rect containDestination(int x, int y, int width, int height,
                               int sourceWidth, int sourceHeight) {
    if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    const double scale = std::min(static_cast<double>(width) / sourceWidth,
                                  static_cast<double>(height) / sourceHeight);
    const int outWidth = std::max(1, std::min(width,
        static_cast<int>(std::floor(sourceWidth * scale))));
    const int outHeight = std::max(1, std::min(height,
        static_cast<int>(std::floor(sourceHeight * scale))));
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
