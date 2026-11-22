#pragma once

#include <algorithm>
#include <cstdint>

namespace desknx::ui {

struct ColorToken {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};

    constexpr ColorToken() noexcept = default;
    constexpr ColorToken(std::uint8_t red, std::uint8_t green,
                         std::uint8_t blue, std::uint8_t alpha = 255) noexcept
        : r(red), g(green), b(blue), a(alpha) {}

    // Accept SDL_Color and other simple RGBA structs without including SDL here.
    template <typename ColorLike>
    constexpr ColorToken(const ColorLike& color) noexcept
        : r(static_cast<std::uint8_t>(color.r)),
          g(static_cast<std::uint8_t>(color.g)),
          b(static_cast<std::uint8_t>(color.b)),
          a(static_cast<std::uint8_t>(color.a)) {}
};

// Keep the first four fields stable: existing aggregate initializers supply
// accent, focus, edge and the base corner radius in this order.
struct ThemeTokens {
    ColorToken accent{83,157,255,255};
    ColorToken focus{246,248,252,220};
    ColorToken edge{255,255,255,28};
    int cornerRadius{10};

    ColorToken surface{20,29,43,238};
    ColorToken surfaceElevated{27,38,54,246};
    ColorToken surfaceInset{9,16,26,190};
    ColorToken divider{255,255,255,24};
    ColorToken textPrimary{246,248,252,255};
    ColorToken textSecondary{183,195,211,255};
    ColorToken textTertiary{132,148,169,255};
    ColorToken success{83,205,151,255};
    ColorToken warning{245,184,93,255};
    ColorToken danger{244,105,117,255};
    ColorToken shadow{0,0,0,68};
    ColorToken scrim{3,7,14,174};

    int radiusXs{5};
    int radiusSm{8};
    int radiusLg{16};
    int radiusXl{22};
    int spaceXs{4};
    int spaceSm{8};
    int spaceMd{12};
    int spaceLg{16};
    int spaceXl{24};
    int minimumTouchTarget{44};
    int iconSm{16};
    int iconMd{24};
    int iconLg{32};
    int focusRingWidth{2};
    std::uint8_t glowOpacity{54};
};

inline int clampToken(int value, int low, int high) noexcept {
    return std::max(low, std::min(high, value));
}

inline ThemeTokens normalizeThemeTokens(ThemeTokens tokens) noexcept {
    tokens.cornerRadius = clampToken(tokens.cornerRadius, 0, 16);
    tokens.radiusXs = clampToken(tokens.radiusXs, 0, 16);
    tokens.radiusSm = clampToken(tokens.radiusSm, tokens.radiusXs, 16);
    tokens.radiusLg = clampToken(tokens.radiusLg, tokens.radiusSm, 16);
    tokens.radiusXl = clampToken(tokens.radiusXl, tokens.radiusLg, 24);
    tokens.spaceXs = clampToken(tokens.spaceXs, 0, 32);
    tokens.spaceSm = clampToken(tokens.spaceSm, tokens.spaceXs, 40);
    tokens.spaceMd = clampToken(tokens.spaceMd, tokens.spaceSm, 48);
    tokens.spaceLg = clampToken(tokens.spaceLg, tokens.spaceMd, 56);
    tokens.spaceXl = clampToken(tokens.spaceXl, tokens.spaceLg, 64);
    tokens.minimumTouchTarget = clampToken(tokens.minimumTouchTarget, 32, 96);
    tokens.iconSm = clampToken(tokens.iconSm, 12, 32);
    tokens.iconMd = clampToken(tokens.iconMd, tokens.iconSm, 40);
    tokens.iconLg = clampToken(tokens.iconLg, tokens.iconMd, 48);
    tokens.focusRingWidth = clampToken(tokens.focusRingWidth, 1, 4);
    return tokens;
}

} // namespace desknx::ui
