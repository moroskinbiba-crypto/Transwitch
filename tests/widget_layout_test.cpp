#include <desknx/WidgetLayout.hpp>

#include <array>
#include <cassert>
#include <cstdio>

using desknx::widget_layout::Metrics;
using desknx::widget_layout::Shape;

struct Expected {
    int width;
    int height;
    Shape shape;
    int columns;
    int rows;
    int capacity;
};

int main() {
    const std::array<Expected, 6> cases{{
        {1, 1, Shape::Compact, 1, 1, 1},
        {2, 1, Shape::Banner2, 2, 1, 2},
        {3, 1, Shape::Banner3, 3, 1, 3},
        {2, 2, Shape::Standard, 2, 2, 4},
        {3, 2, Shape::Dashboard, 3, 2, 6},
        {2, 3, Shape::Portrait, 2, 3, 6}
    }};

    for (const auto& expected : cases) {
        const Metrics metrics = desknx::widget_layout::measure(
            100, 80, 180 * expected.width, 150 * expected.height,
            expected.width, expected.height);

        assert(metrics.shape == expected.shape);
        assert(metrics.gridColumns == expected.columns);
        assert(metrics.gridRows == expected.rows);
        assert(metrics.maxRows == expected.rows);
        assert(metrics.capacity == expected.capacity);
        assert(metrics.visibleItems(99) == expected.capacity);
        assert(metrics.visibleItems(1) == 1);
        assert(metrics.visibleItems(0) == 0);
        assert(metrics.left < metrics.center);
        assert(metrics.center < metrics.right);
        assert(metrics.bodyTop < metrics.bodyBottom);

        for (int row = 0; row < expected.rows; ++row) {
            const int y = metrics.rowY(row);
            assert(y >= metrics.bodyTop);
            assert(y <= metrics.bodyBottom);
        }

        assert(metrics.compact() == (expected.shape == Shape::Compact));
        assert(metrics.banner() ==
            (expected.shape == Shape::Banner2 || expected.shape == Shape::Banner3));
    }

    assert(desknx::widget_layout::shapeFor(1, 1) == Shape::Compact);
    assert(desknx::widget_layout::shapeFor(2, 1) == Shape::Banner2);
    assert(desknx::widget_layout::shapeFor(3, 1) == Shape::Banner3);
    assert(desknx::widget_layout::shapeFor(2, 2) == Shape::Standard);
    assert(desknx::widget_layout::shapeFor(3, 2) == Shape::Dashboard);
    assert(desknx::widget_layout::shapeFor(2, 3) == Shape::Portrait);

    const auto dashboard = desknx::widget_layout::measure(0, 0, 540, 300, 3, 2);
    const auto portrait = desknx::widget_layout::measure(0, 0, 360, 450, 2, 3);
    assert(dashboard.capacity == 6 && dashboard.gridColumns == 3);
    assert(portrait.capacity == 6 && portrait.gridRows == 3);

    std::puts("widget_layout_test: ok");
    return 0;
}
