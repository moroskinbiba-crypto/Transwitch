#include <desknx/WidgetConfig.hpp>

#include <cassert>
#include <cstdio>

int main() {
    using desknx::model::Kind;
    const auto defaults = desknx::widgets::defaults(Kind::Clock);
    assert(defaults.style == 0 && defaults.refreshSeconds == 5 && defaults.flags == 3u);

    const desknx::widgets::Config edited{2, 30, 1u, 6};
    const std::string saved = desknx::widgets::serialize(edited);
    assert(saved == "W1;S=2;R=30;F=1;N=6");
    const auto restored = desknx::widgets::parse(Kind::Favorites, saved);
    assert(restored.style == 2 && restored.refreshSeconds == 30);
    assert(restored.flags == 1u && restored.itemCount == 6);

    const auto legacy = desknx::widgets::parse(Kind::Battery, "");
    assert(legacy.refreshSeconds == 15 && legacy.flags == 3u);
    assert(desknx::model::parseKind("X") == Kind::Nextendo);
    assert(desknx::model::parseKind("F") == Kind::Favorites);
    assert(desknx::model::parseKind("L") == Kind::GameLibrary);
    assert(desknx::model::typeCode(Kind::Nextendo) == "X");
    assert(desknx::model::typeCode(Kind::Favorites) == "F");
    assert(desknx::model::typeCode(Kind::GameLibrary) == "L");
    const auto clamped = desknx::widgets::parse(Kind::Storage, "W1;S=99;R=7;F=255;N=99");
    assert(clamped.style == 2 && clamped.refreshSeconds == 5);
    assert(clamped.flags == 3u && clamped.itemCount == 6);

    desknx::widgets::Config toggled{};
    desknx::widgets::toggle(toggled, 0);
    assert(!desknx::widgets::enabled(toggled, 0));
    desknx::widgets::toggle(toggled, 1);
    assert(!desknx::widgets::enabled(toggled, 1));
    std::puts("widget_config_test: ok");
    return 0;
}
