#include <desknx/IconRegistry.hpp>
#include <cassert>
#include <cstddef>
#include <iostream>

using desknx::ui::IconCategory;
using desknx::ui::IconId;
using desknx::ui::IconManifest;
using desknx::ui::iconIdFromLegacyIndex;
using desknx::ui::iconInfo;
using desknx::ui::iconIndex;
using desknx::ui::isValidIcon;

int main() {
    static_assert(IconManifest.size() == static_cast<std::size_t>(IconId::Count),
                  "Every semantic icon id must have one manifest entry");
    static_assert(iconIndex(IconId::Settings) == 0, "Legacy settings id changed");
    static_assert(iconIndex(IconId::Sliders) == 8, "Legacy sliders id changed");
    static_assert(iconIndex(IconId::Battery) == 9, "Battery id must follow legacy ids");
    static_assert(iconIndex(IconId::Calendar) == 10, "Calendar id must follow battery");

    assert(isValidIcon(IconId::Settings));
    assert(isValidIcon(IconId::Battery));
    assert(isValidIcon(IconId::Calendar));
    assert(!isValidIcon(IconId::Count));

    assert(iconInfo(IconId::Settings).asset == std::string("settings.svg"));
    assert(iconInfo(IconId::Battery).asset == std::string("battery.svg"));
    assert(iconInfo(IconId::Calendar).asset == std::string("calendar.svg"));
    assert(iconInfo(IconId::Battery).category == IconCategory::Widget);
    assert(iconInfo(IconId::Calendar).category == IconCategory::Widget);
    assert(iconInfo(IconId::Library).category == IconCategory::Navigation);
    assert(iconInfo(IconId::Gamepad).category == IconCategory::System);

    assert(iconIdFromLegacyIndex(0) == IconId::Settings);
    assert(iconIdFromLegacyIndex(8) == IconId::Sliders);
    assert(iconIdFromLegacyIndex(-1) == IconId::Count);
    assert(iconIdFromLegacyIndex(11) == IconId::Count);

    std::cout << "Icon registry tests passed\n";
    return 0;
}
