#include <desknx/VisualTokens.hpp>
#include <cassert>
#include <iostream>

int main() {
    using desknx::ui::ThemeTokens;
    using desknx::ui::normalizeThemeTokens;

    const ThemeTokens defaults{};
    assert(defaults.cornerRadius == 10);
    assert(defaults.radiusXs < defaults.radiusSm);
    assert(defaults.radiusSm < defaults.radiusLg);
    assert(defaults.minimumTouchTarget >= 44);
    assert(defaults.iconSm < defaults.iconMd);
    assert(defaults.iconMd < defaults.iconLg);

    ThemeTokens custom{};
    custom.cornerRadius = 100;
    custom.radiusXs = -10;
    custom.radiusSm = -5;
    custom.radiusLg = 40;
    custom.radiusXl = 80;
    custom.spaceXs = -1;
    custom.spaceSm = 200;
    custom.spaceMd = 1;
    custom.spaceLg = 90;
    custom.spaceXl = 120;
    custom.minimumTouchTarget = 1;
    custom.iconSm = 1;
    custom.iconMd = 8;
    custom.iconLg = 100;
    custom.focusRingWidth = 10;

    const ThemeTokens safe = normalizeThemeTokens(custom);
    assert(safe.cornerRadius == 16);
    assert(safe.radiusXs == 0);
    assert(safe.radiusXs <= safe.radiusSm);
    assert(safe.radiusSm <= safe.radiusLg);
    assert(safe.radiusLg <= safe.radiusXl);
    assert(safe.radiusXl <= 24);
    assert(safe.spaceXs == 0);
    assert(safe.spaceXs <= safe.spaceSm);
    assert(safe.spaceSm <= safe.spaceMd);
    assert(safe.spaceMd <= safe.spaceLg);
    assert(safe.spaceLg <= safe.spaceXl);
    assert(safe.spaceXl <= 64);
    assert(safe.minimumTouchTarget == 32);
    assert(safe.iconSm == 12);
    assert(safe.iconSm <= safe.iconMd);
    assert(safe.iconMd <= safe.iconLg);
    assert(safe.iconLg == 48);
    assert(safe.focusRingWidth == 4);

    assert(safe.accent.r == custom.accent.r);
    assert(safe.surface.g == custom.surface.g);
    assert(safe.textPrimary.a == custom.textPrimary.a);

    std::cout << "Visual token tests passed\n";
    return 0;
}
