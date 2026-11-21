#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace desknx::ui {

enum class IconCategory : std::uint8_t {
    Navigation,
    System,
    Widget,
    Status,
};

enum class IconId : std::uint8_t {
    Settings,
    Gamepad,
    Gallery,
    Power,
    Library,
    Home,
    Recent,
    User,
    Sliders,
    Battery,
    Count,
};

struct IconInfo {
    const char* asset;
    IconCategory category;
};

inline constexpr std::array<IconInfo, static_cast<std::size_t>(IconId::Count)>
    IconManifest{{
        {"settings.svg", IconCategory::System},
        {"gamepad.svg", IconCategory::System},
        {"gallery.svg", IconCategory::System},
        {"power.svg", IconCategory::System},
        {"library.svg", IconCategory::Navigation},
        {"home.svg", IconCategory::Navigation},
        {"recent.svg", IconCategory::Navigation},
        {"user.svg", IconCategory::System},
        {"sliders.svg", IconCategory::Widget},
        {"battery.svg", IconCategory::Widget},
    }};

constexpr std::size_t iconIndex(IconId id) noexcept {
    return static_cast<std::size_t>(id);
}

constexpr bool isValidIcon(IconId id) noexcept {
    return iconIndex(id) < IconManifest.size();
}

constexpr const IconInfo& iconInfo(IconId id) noexcept {
    return IconManifest[isValidIcon(id) ? iconIndex(id) : 0];
}

// Transitional adapter for older toolbar descriptors that still store a
// compact numeric icon index. New call sites should use IconId directly.
constexpr IconId iconIdFromLegacyIndex(int index) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < IconManifest.size()
        ? static_cast<IconId>(index) : IconId::Count;
}

} // namespace desknx::ui
