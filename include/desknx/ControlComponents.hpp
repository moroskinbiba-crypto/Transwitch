#pragma once

#include "ConfirmationDialogLayout.hpp"

#include <cstdint>

namespace desknx::ui {

enum class ButtonVariant : std::uint8_t {
    Primary,
    Secondary,
    Destructive,
    Quiet,
    Toolbar
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
        case ButtonVariant::Toolbar:
            if(state.disabled)
                return {{27,34,45,255},{62,73,91,160},{105,117,136,255},
                        {0,0,0,0},true,false};
            if(state.pressed)
                return {{42,59,82,255},{108,148,199,190},{255,255,255,255},
                        {0,0,0,0},true,false};
            if(state.focused)
                return {{48,78,115,255},{131,183,242,230},{255,255,255,255},
                        {83,157,255,175},true,true};
            return {{31,43,61,255},{255,255,255,22},{246,248,252,255},
                    {0,0,0,0},true,false};
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

constexpr bool IsSelectableRowInteractive(
    SelectableRowState state) noexcept {
    return state!=SelectableRowState::Disabled;
}

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


enum class BadgeVariant : std::uint8_t {
    Neutral,
    Info,
    Success,
    Warning,
    Danger
};

struct BadgeStyle {
    ControlColor background;
    ControlColor border;
    ControlColor foreground;
};

constexpr BadgeStyle ResolveBadgeStyle(BadgeVariant variant) noexcept {
    switch(variant) {
        case BadgeVariant::Info:
            return {{31,53,79,255},{88,143,205,150},{160,202,255,255}};
        case BadgeVariant::Success:
            return {{22,63,50,255},{62,166,126,150},{112,225,177,255}};
        case BadgeVariant::Warning:
            return {{75,52,25,255},{213,151,64,160},{255,202,120,255}};
        case BadgeVariant::Danger:
            return {{78,35,42,255},{210,91,105,160},{255,157,169,255}};
        case BadgeVariant::Neutral:
        default:
            return {{39,49,65,255},{255,255,255,22},{180,194,212,255}};
    }
}

struct ToggleControl {
    ModalRect bounds;
    bool value{false};
    ControlState state{};

    constexpr bool hitTest(int x,int y) const noexcept {
        return !state.disabled&&bounds.contains(x,y);
    }
};

struct ToggleStyle {
    ControlColor track;
    ControlColor border;
    ControlColor knob;
    ControlColor focusRing;
    bool drawFocusRing{false};
};

constexpr ToggleStyle ResolveToggleStyle(bool value,
                                         ControlState state) noexcept {
    if(state.disabled) {
        return {{43,48,57,255},{71,80,94,180},{112,121,136,255},
                {0,0,0,0},false};
    }
    if(value) {
        if(state.pressed)
            return {{14,101,70,255},{61,187,139,255},{255,255,255,255},
                    {0,0,0,0},false};
        return {{20,132,89,255},{79,205,158,255},{255,255,255,255},
                {83,157,255,170},state.focused};
    }
    if(state.pressed)
        return {{39,48,61,255},{120,139,165,210},{255,255,255,255},
                {0,0,0,0},false};
    return {{48,58,73,255},{112,128,151,210},{228,235,245,255},
            {83,157,255,170},state.focused};
}

enum class FieldVisualState : std::uint8_t {
    Normal,
    Focused,
    Disabled
};

struct ValueFieldControl {
    ModalRect bounds;
    FieldVisualState visualState{FieldVisualState::Normal};

    constexpr bool hitTest(int x,int y) const noexcept {
        return visualState!=FieldVisualState::Disabled&&bounds.contains(x,y);
    }
};

struct ValueFieldStyle {
    ControlColor background;
    ControlColor border;
    ControlColor foreground;
    ControlColor focusRing;
    bool drawFocusRing{false};
};

constexpr ValueFieldStyle ResolveValueFieldStyle(
    FieldVisualState state) noexcept {
    switch(state) {
        case FieldVisualState::Focused:
            return {{30,47,69,255},{101,166,232,220},{187,216,250,255},
                    {83,157,255,150},true};
        case FieldVisualState::Disabled:
            return {{28,33,42,255},{62,71,84,150},{103,113,128,255},
                    {0,0,0,0},false};
        case FieldVisualState::Normal:
        default:
            return {{27,38,54,255},{255,255,255,30},{145,177,213,255},
                    {0,0,0,0},false};
    }
}

enum class CardVisualState : std::uint8_t {
    Normal,
    Focused,
    Pressed,
    Disabled,
    Panel
};

struct CardStyle {
    ControlColor background;
    ControlColor border;
    bool shadow{true};
};

constexpr CardStyle ResolveCardStyle(CardVisualState state) noexcept {
    switch(state) {
        case CardVisualState::Focused:
            return {{34,63,98,255},{91,157,238,115},true};
        case CardVisualState::Pressed:
            return {{26,48,76,255},{111,174,245,170},true};
        case CardVisualState::Disabled:
            return {{19,25,35,255},{255,255,255,10},false};
        case CardVisualState::Panel:
            return {{22,31,45,255},{255,255,255,0},false};
        case CardVisualState::Normal:
        default:
            return {{23,32,46,255},{255,255,255,18},true};
    }
}

enum class IndicatorTone : std::uint8_t {
    Idle,
    Active,
    Ready,
    Loading,
    Stale,
    Offline,
    Unavailable,
    Error
};

struct IndicatorStyle {
    ControlColor dot;
    ControlColor halo;
    bool drawHalo{false};
};

constexpr IndicatorStyle ResolveIndicatorStyle(IndicatorTone tone) noexcept {
    switch(tone) {
        case IndicatorTone::Active:
            return {{83,157,255,255},{83,157,255,70},true};
        case IndicatorTone::Ready:
            return {{52,211,153,255},{52,211,153,60},false};
        case IndicatorTone::Loading:
            return {{255,190,92,255},{255,190,92,70},true};
        case IndicatorTone::Stale:
            return {{244,184,77,255},{244,184,77,60},true};
        case IndicatorTone::Offline:
            return {{128,139,155,255},{0,0,0,0},false};
        case IndicatorTone::Unavailable:
        case IndicatorTone::Error:
            return {{255,99,112,255},{255,99,112,65},true};
        case IndicatorTone::Idle:
        default:
            return {{105,119,139,255},{0,0,0,0},false};
    }
}

} // namespace desknx::ui
