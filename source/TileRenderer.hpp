#pragma once

#include <SDL2/SDL.h>
#include <desknx/TileVisualRules.hpp>
#include "VisualEngine.hpp"
#include "UiComponents.hpp"

#include <algorithm>
#include <cstdint>

namespace desknx::ui {

// Artwork tile rendering contract. Texture lookup, crop policy, title shaping,
// badges and all input hitboxes remain with the launcher/content layer; this
// component owns only the reusable visual shell and label contrast treatment.
class TileRenderer final {
public:
    void bind(desknx::visual::VisualEngine* engine,
              UiComponents* components,
              SDL_Renderer* renderer) noexcept {
        engine_ = engine;
        components_ = components;
        renderer_ = renderer;
    }

    bool artworkFrame(const SDL_Rect& bounds,
                      SDL_Color fill,
                      SDL_Color edge,
                      SDL_Color accent,
                      bool selected,
                      int surfaceOpacity = 255,
                      int radius = 10) {
        if (!valid(bounds)) return false;
        bool ok = true;
        if (selected && engine_ && engine_->ready()) {
            SDL_Color glow = accent;
            glow.a = static_cast<Uint8>(
                (static_cast<unsigned int>(glow.a) * 62u) / 255u);
            const SDL_Rect glowBounds{
                bounds.x + bounds.w / 8,
                bounds.y - 6,
                std::max(24, bounds.w * 3 / 4),
                std::max(24, bounds.h / 2)
            };
            ok = engine_->drawRadialGlow(glowBounds, glow) && ok;
        }

        if (components_) {
            ok = components_->surface(bounds, fill, edge, radius, true,
                                      surfaceOpacity) && ok;
        } else if (renderer_) {
            if (SDL_SetRenderDrawColor(renderer_, fill.r, fill.g, fill.b, fill.a) != 0)
                return false;
            const SDL_Rect fillRect = bounds;
            ok = SDL_RenderFillRect(renderer_, &fillRect) == 0 && ok;
            if (SDL_SetRenderDrawColor(renderer_, edge.r, edge.g, edge.b, edge.a) != 0)
                return false;
            const SDL_Rect border = bounds;
            ok = SDL_RenderDrawRect(renderer_, &border) == 0 && ok;
        } else {
            return false;
        }

        if (selected && engine_ && engine_->ready()) {
            SDL_Color focus{accent.r,accent.g,accent.b,210};
            ok = engine_->drawRoundedOutline(bounds.x, bounds.y, bounds.w,
                bounds.h, radius, focus) && ok;
        }
        return ok;
    }

    // Draw a bottom-up alpha gradient behind the game title. The cached ramp is
    // reused for every tile with the same label opacity. A flat SDL scrim is
    // retained as a safe fallback if texture creation is unavailable.
    bool labelScrim(const SDL_Rect& tileBounds, bool twoLines,
                    int opacity, int radius = 10) {
        if (!valid(tileBounds) || opacity <= 0) return true;
        const TileLabelLayout layout = measureTileLabel(
            tileBounds.w, tileBounds.h, twoLines, radius);
        const SDL_Rect strip{
            tileBounds.x,
            tileBounds.y + tileBounds.h - layout.stripHeight,
            tileBounds.w,
            layout.stripHeight
        };
        const int alpha = clampTileLabelOpacity(opacity);
        if (engine_ && engine_->ready()) {
            const SDL_Color top{5,9,16,0};
            const SDL_Color bottom{5,9,16,static_cast<Uint8>(alpha)};
            if (engine_->drawLinearGradient(strip, top, bottom,
                    desknx::visual::VisualEngine::GradientAxis::Vertical))
                return true;
        }

        if (!renderer_) return false;
        if (SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND) != 0)
            return false;
        if (SDL_SetRenderDrawColor(renderer_, 5, 9, 16,
                                   static_cast<Uint8>(alpha)) != 0)
            return false;
        return SDL_RenderFillRect(renderer_, &strip) == 0;
    }

    static TileLabelLayout labelLayout(int width, int height,
                                       bool twoLines, int radius = 10) noexcept {
        return measureTileLabel(width, height, twoLines, radius);
    }

private:
    static bool valid(const SDL_Rect& bounds) noexcept {
        return bounds.w > 0 && bounds.h > 0;
    }

    desknx::visual::VisualEngine* engine_{nullptr};
    UiComponents* components_{nullptr};
    SDL_Renderer* renderer_{nullptr};
};

} // namespace desknx::ui
