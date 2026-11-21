#include <desknx/IconRegistry.hpp>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>

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
    static_assert(iconIndex(IconId::Clock) == 11, "Clock id must follow calendar");
    static_assert(iconIndex(IconId::Monitor) == 12, "Monitor id must follow clock");
    static_assert(iconIndex(IconId::Storage) == 13, "Storage id must follow monitor");
    static_assert(iconIndex(IconId::Network) == 14, "Network id must follow storage");
    static_assert(iconIndex(IconId::Heart) == 15, "Heart id must follow network");

    assert(isValidIcon(IconId::Settings));
    assert(isValidIcon(IconId::Battery));
    assert(isValidIcon(IconId::Calendar));
    assert(isValidIcon(IconId::Clock));
    assert(isValidIcon(IconId::Monitor));
    assert(isValidIcon(IconId::Storage));
    assert(isValidIcon(IconId::Network));
    assert(isValidIcon(IconId::Heart));
    assert(!isValidIcon(IconId::Count));

    assert(iconInfo(IconId::Settings).asset == std::string("settings.svg"));
    assert(iconInfo(IconId::Battery).asset == std::string("battery.svg"));
    assert(iconInfo(IconId::Calendar).asset == std::string("calendar.svg"));
    assert(iconInfo(IconId::Clock).asset == std::string("clock.svg"));
    assert(iconInfo(IconId::Monitor).asset == std::string("monitor.svg"));
    assert(iconInfo(IconId::Storage).asset == std::string("storage.svg"));
    assert(iconInfo(IconId::Network).asset == std::string("network.svg"));
    assert(iconInfo(IconId::Heart).asset == std::string("heart.svg"));
    assert(iconInfo(IconId::Battery).category == IconCategory::Widget);
    assert(iconInfo(IconId::Calendar).category == IconCategory::Widget);
    assert(iconInfo(IconId::Clock).category == IconCategory::Widget);
    assert(iconInfo(IconId::Monitor).category == IconCategory::Widget);
    assert(iconInfo(IconId::Storage).category == IconCategory::Widget);
    assert(iconInfo(IconId::Network).category == IconCategory::Widget);
    assert(iconInfo(IconId::Heart).category == IconCategory::Widget);
    assert(iconInfo(IconId::Library).category == IconCategory::Navigation);
    assert(iconInfo(IconId::Gamepad).category == IconCategory::System);

    assert(iconIdFromLegacyIndex(0) == IconId::Settings);
    assert(iconIdFromLegacyIndex(8) == IconId::Sliders);
    assert(iconIdFromLegacyIndex(-1) == IconId::Count);
    assert(iconIdFromLegacyIndex(8) == IconId::Sliders);
    assert(iconIdFromLegacyIndex(16) == IconId::Count);

    std::cout << "Icon registry tests passed\n";
    return 0;
}
