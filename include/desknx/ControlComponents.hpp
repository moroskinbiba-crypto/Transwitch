#pragma once

#include "ConfirmationDialogLayout.hpp"

#include <cstdint>

namespace desknx::ui {

enum class ButtonVariant : std::uint8_t {
    Primary,
    Secondary,
    Destructive,
    Quiet
};

struct ControlState {
    bool focused{false};
    bool pressed{false};
    bool disabled{false};
};

struct ControlColor {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{0};
};

struct ButtonStyle {
    ControlColor background;
    ControlColor border;
    ControlColor foreground;
    ControlColor focusRing;
    bool drawBorder{false};
    bool drawFocusRing{false};
};

inline constexpr int ButtonCornerRadius=10;

constexpr ButtonStyle ResolveButtonStyle(ButtonVariant variant,
                                         ControlState state) noexcept {
    if(state.disabled) {
        return {
            {35,40,49,255},{70,78,90,180},{113,122,137,255},
            {0,0,0,0},true,false
        };
    }

    switch(variant) {
        case ButtonVariant::Primary:
            if(state.pressed)
                return {{8,91,181,255},{65,143,220,255},{255,255,255,255},
                        {0,0,0,0},true,false};
            if(state.focused)
                return {{22,146,255,255},{214,234,255,255},{255,255,255,255},
                        {83,157,255,190},true,true};
            return {{10,132,255,255},{95,174,255,180},{255,255,255,255},
                    {0,0,0,0},true,false};

        case ButtonVariant::Secondary:
            if(state.pressed)
                return {{37,43,53,255},{110,130,158,190},{246,248,252,255},
                        {0,0,0,0},true,false};
            if(state.focused)
                return {{45,61,82,255},{116,164,224,220},{246,248,252,255},
                        {83,157,255,180},true,true};
            return {{54,57,66,255},{255,255,255,28},{246,248,252,255},
                    {0,0,0,0},true,false};

        case ButtonVariant::Destructive:
            if(state.pressed)
                return {{82,27,32,255},{255,99,112,220},{255,255,255,255},
                        {0,0,0,0},true,false};
            if(state.focused)
                return {{138,42,48,255},{255,190,194,255},{255,255,255,255},
                        {255,99,112,190},true,true};
            return {{112,38,42,255},{255,69,58,160},{255,255,255,255},
                    {0,0,0,0},true,false};

        case ButtonVariant::Quiet:
            if(state.pressed)
                return {{39,52,70,220},{90,120,160,100},{246,248,252,255},
                        {0,0,0,0},true,false};
            if(state.focused)
                return {{32,52,76,230},{116,164,224,180},{246,248,252,255},
                        {83,157,255,160},true,true};
            return {{0,0,0,0},{0,0,0,0},{145,158,180,255},
                    {0,0,0,0},false,false};
    }
    return {};
}

struct ButtonControl {
    ModalRect bounds;
    ButtonVariant variant{ButtonVariant::Secondary};
    ControlState state{};

    constexpr bool hitTest(int x,int y) const noexcept {
        return !state.disabled&&bounds.contains(x,y);
    }
};

enum class SelectableRowState : std::uint8_t {
    Normal,
    Current,
    Focused,
    Disabled
};

struct SelectableRowStyle {
    ControlColor background;
    ControlColor foreground;
};

constexpr SelectableRowStyle ResolveSelectableRowStyle(
    SelectableRowState state) noexcept {
    switch(state) {
        case SelectableRowState::Current:
            return {{30,43,60,255},{83,157,255,255}};
        case SelectableRowState::Focused:
            return {{45,61,82,255},{246,248,252,255}};
        case SelectableRowState::Disabled:
            return {{0,0,0,0},{91,101,118,255}};
        case SelectableRowState::Normal:
        default:
            return {{0,0,0,0},{145,158,180,255}};
    }
}

} // namespace desknx::ui
