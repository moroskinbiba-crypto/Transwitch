#include <desknx/VisualGeometry.hpp>
#include <cassert>
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
        assert(top.y + bottom.y + top.h == 20 + 60);
    }

    SpanRect shortBuffer[2]{};
    assert(roundedFillSpans(0, 0, 80, 40, 10, shortBuffer, 2) == 2);
    std::cout << "Visual geometry tests passed\n";
    return 0;
}
