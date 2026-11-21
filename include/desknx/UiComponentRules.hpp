#pragma once

#include <cstdint>

namespace desknx::ui {

enum class ButtonVariant : std::uint8_t {
    Glass,
    Rail,
    Minimal,
    Chip,
    Solid
};

struct ButtonState {
    bool active{false};
    bool focused{false};
    bool disabled{false};
};

struct ButtonPaintRules {
    bool drawFill{true};
    bool drawEdge{false};
    bool drawAccentRail{false};
    bool drawFocusRing{false};
    std::uint8_t alphaScale{255};
};

// Pure presentation rules: state changes appearance only. Input ownership and
// hit-test rectangles remain in the existing launcher controller.
constexpr ButtonPaintRules resolveButtonPaint(
    ButtonVariant variant, ButtonState state) {
    ButtonPaintRules result{};
    switch (variant) {
        case ButtonVariant::Glass:
            result.drawFill = true;
            result.drawEdge = state.active || state.focused;
            break;
        case ButtonVariant::Rail:
            result.drawFill = state.active;
            result.drawAccentRail = state.active;
            result.drawFocusRing = state.focused;
            break;
        case ButtonVariant::Minimal:
            result.drawFill = state.active;
            result.drawAccentRail = state.active;
            result.drawFocusRing = state.focused;
            break;
        case ButtonVariant::Chip:
            result.drawFill = true;
            result.drawEdge = true;
            result.drawFocusRing = state.focused;
            break;
        case ButtonVariant::Solid:
            result.drawFill = true;
            result.drawEdge = state.focused;
            result.drawFocusRing = state.focused;
            break;
    }
    if (state.disabled) result.alphaScale = 112;
    return result;
}

} // namespace desknx::ui
