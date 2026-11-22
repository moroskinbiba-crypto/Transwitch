#pragma once

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>

namespace desknx::ui {

// Theme-independent contract shared by the renderer and reusable UI components.
// Keep the first four fields stable: existing aggregate initializers in the
// launcher intentionally supply accent, focus, edge and the base corner radius.
struct ThemeTokens {
    SDL_Color accent{83,157,255,255};
    SDL_Color focus{246,248,252,220};
    SDL_Color edge{255,255,255,28};
    int cornerRadius{10};

    // Semantic surface and status roles. A theme may override these without
    // changing component code or the geometry/input model.
    SDL_Color surface{20,29,43,238};
    SDL_Color surfaceElevated{27,38,54,246};
    SDL_Color surfaceInset{9,16,26,190};
    SDL_Color divider{255,255,255,24};
    SDL_Color textPrimary{246,248,252,255};
    SDL_Color textSecondary{183,195,211,255};
    SDL_Color textTertiary{132,148,169,255};
    SDL_Color success{83,205,151,255};
    SDL_Color warning{245,184,93,255};
    SDL_Color danger{244,105,117,255};
    SDL_Color shadow{0,0,0,68};
    SDL_Color scrim{3,7,14,174};

    // Geometry and spacing scale, in logical 1280x720 units.
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
