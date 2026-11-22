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
    bool hovered{false};
    bool pressed{false};
};

struct ButtonPaintRules {
    bool drawFill{true};
    bool drawEdge{false};
    bool drawAccentRail{false};
    bool drawFocusRing{false};
    std::uint8_t alphaScale{255};
};

// Pure presentation rules: the same state always resolves to the same paint.
// Hit rectangles, input ownership and key mappings remain outside this layer.
constexpr ButtonPaintRules resolveButtonPaint(
    ButtonVariant variant, ButtonState state) {
    ButtonPaintRules result{};
    switch (variant) {
        case ButtonVariant::Glass:
            result.drawFill = true;
            result.drawEdge = state.active || state.focused || state.hovered;
            result.drawFocusRing = state.focused;
            break;
        case ButtonVariant::Rail:
            result.drawFill = state.active || state.hovered;
            result.drawAccentRail = state.active;
            result.drawFocusRing = state.focused;
            break;
        case ButtonVariant::Minimal:
            result.drawFill = state.active || state.hovered;
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
    if (state.pressed) result.alphaScale = 224;
    if (state.disabled) result.alphaScale = 112;
    return result;
}

} // namespace desknx::ui
