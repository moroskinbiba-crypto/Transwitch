#include <desknx/VisualGeometry.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>

using desknx::visual::geometry::MaxRoundedFillSpans;
using desknx::visual::geometry::SpanRect;
using desknx::visual::geometry::roundedFillSpans;

int main() {
    SpanRect spans[MaxRoundedFillSpans]{};
    std::size_t count = roundedFillSpans(7, 11, 100, 40, 0, spans, MaxRoundedFillSpans);
    assert(count == 1);
    assert(spans[0].x == 7 && spans[0].y == 11);
    assert(spans[0].w == 100 && spans[0].h == 40);
    assert(roundedFillSpans(0, 0, 0, 20, 8, spans, MaxRoundedFillSpans) == 0);
    assert(roundedFillSpans(0, 0, 20, -1, 8, spans, MaxRoundedFillSpans) == 0);
    assert(roundedFillSpans(0, 0, 20, 20, 8, nullptr, 0) == 0);

    for (int width = 1; width <= 96; ++width) {
        for (int height = 1; height <= 72; ++height) {
            for (int radius = -2; radius <= 24; ++radius) {
                count = roundedFillSpans(13, 17, width, height, radius,
                    spans, MaxRoundedFillSpans);
                assert(count <= MaxRoundedFillSpans);
                for (std::size_t i = 0; i < count; ++i) {
                    const SpanRect& r = spans[i];
                    assert(r.w > 0 && r.h > 0);
                    assert(r.x >= 13 && r.y >= 17);
                    assert(r.x + r.w <= 13 + width);
                    assert(r.y + r.h <= 17 + height);
                }
            }
        }
    }

    count = roundedFillSpans(10, 20, 120, 60, 10, spans, MaxRoundedFillSpans);
    assert(count == 21);
    for (std::size_t i = 0; i < 10; ++i) {
        const SpanRect& top = spans[1 + i * 2];
        const SpanRect& bottom = spans[2 + i * 2];
        assert(top.x == bottom.x && top.w == bottom.w && top.h == bottom.h);
        assert(top.y + bottom.y + top.h == 2 * 20 + 60);
    }

    // Batched spans must paint exactly the same pixels as the old scanline
    // implementation, including narrow and degenerate card dimensions.
    for (int width = 1; width <= 48; ++width) {
        for (int height = 1; height <= 40; ++height) {
            for (int radius = 0; radius <= 16; ++radius) {
                unsigned char expected[48 * 40]{};
                unsigned char actual[48 * 40]{};
                const int corner = std::max(
                    0, std::min(radius, std::min(width / 2, height / 2)));
                if (height > corner * 2) {
                    for (int py = corner; py < height - corner; ++py)
                        for (int px = 0; px < width; ++px)
                            expected[py * width + px] = 1;
                }
                for (int row = 0; row < corner; ++row) {
                    const double dy = static_cast<double>(corner - row);
                    const int inset = static_cast<int>(std::ceil(
                        static_cast<double>(corner) - std::sqrt(std::max(
                            0.0, static_cast<double>(corner * corner) - dy * dy))));
                    for (int px = inset; px < width - inset; ++px) {
                        expected[row * width + px] = 1;
                        expected[(height - 1 - row) * width + px] = 1;
                    }
                }

                count = roundedFillSpans(0, 0, width, height, radius,
                    spans, MaxRoundedFillSpans);
                for (std::size_t i = 0; i < count; ++i) {
                    const SpanRect& r = spans[i];
                    for (int py = r.y; py < r.y + r.h; ++py)
                        for (int px = r.x; px < r.x + r.w; ++px)
                            actual[py * width + px] = 1;
                }
                for (int py = 0; py < height; ++py)
                    for (int px = 0; px < width; ++px)
                        assert(actual[py * width + px] == expected[py * width + px]);
            }
        }
    }

    SpanRect shortBuffer[2]{};
    assert(roundedFillSpans(0, 0, 80, 40, 10, shortBuffer, 2) == 2);
    std::cout << "Visual geometry tests passed\n";
    return 0;
}
