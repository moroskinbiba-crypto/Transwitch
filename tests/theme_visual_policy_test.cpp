#include <desknx/ThemeVisualPolicy.hpp>

#include <cassert>
#include <cstdio>

int main() {
    const auto legacy = desknx::ui::ThemePolicyForIndex(0);
    assert(!legacy.flatSurfaces);
    assert(!legacy.squareButtons);
    assert(!legacy.persistentWorkspaceGrid);
    assert(!legacy.accentRails);
    assert(legacy.maximumButtonRadius == 16);
    assert(legacy.maximumSurfaceRadius == 16);

    const auto socoma = desknx::ui::ThemePolicyForIndex(1);
    assert(socoma.flatSurfaces);
    assert(socoma.squareButtons);
    assert(socoma.persistentWorkspaceGrid);
    assert(socoma.accentRails);
    assert(socoma.maximumButtonRadius == 4);
    assert(socoma.maximumSurfaceRadius == 0);

    const auto unknown = desknx::ui::ThemePolicyForIndex(99);
    assert(!unknown.flatSurfaces);
    assert(!unknown.squareButtons);
    std::puts("theme_visual_policy_test: ok");
    return 0;
}
