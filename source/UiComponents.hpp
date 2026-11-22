#pragma once

#include <SDL2/SDL.h>
#include <desknx/UiComponentRules.hpp>
#include <desknx/VisualTokens.hpp>
#include "VisualEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace desknx::ui {

class UiComponents final {
public:
    void bind(desknx::visual::VisualEngine* engine, SDL_Renderer* renderer) noexcept {
        engine_ = engine;
        renderer_ = renderer;
    }

    void setThemeTokens(const ThemeTokens& tokens) noexcept {
        tokens_ = normalizeThemeTokens(tokens);
    }

    const ThemeTokens& themeTokens() const noexcept { return tokens_; }

    bool surface(const SDL_Rect& bounds, SDL_Color fill, SDL_Color edge,
                 int radius, bool shadow = true, int opacity = 255) {
        if (!valid(bounds)) return false;
        const int alpha = clampToken(opacity, 0, 255);
        fill.a = scaleAlpha(fill.a, alpha);
        edge.a = scaleAlpha(edge.a, alpha);
        bool ok = true;

        if (shadow) {
            SDL_Color shade = tokens_.shadow;
            shade.a = scaleAlpha(shade.a, alpha);
            if (engine_ && engine_->ready()) {
                // Soft silhouette layers avoid unsupported render-target blur.
                ok = engine_->drawRoundedShadow(bounds, radius, shade, 6) && ok;
            } else {
                shade.a = scaleAlpha(shade.a, 180);
                ok = fillRounded(bounds.x, bounds.y + 4, bounds.w, bounds.h,
                                 radius, shade) && ok;
            }
        }

        ok = fillRounded(bounds.x, bounds.y, bounds.w, bounds.h, radius, fill) && ok;
        // The edge is a stroke, not a translucent inset fill. This keeps card
        // content crisp and prevents the border tint from washing the whole card.
        ok = outlineRounded(bounds.x, bounds.y, bounds.w, bounds.h, radius, edge) && ok;
        return ok;
    }

    // Shared shell for every workspace widget. Focus glow is drawn before the
    // surface, so it remains a restrained halo and never obscures widget data.
    bool widgetSurface(const SDL_Rect& bounds, SDL_Color fill, SDL_Color edge,
                       SDL_Color accent, bool focused, int opacity = 255) {
        if (!valid(bounds)) return false;
        bool ok = true;
        if (focused && engine_ && engine_->ready()) {
            SDL_Color glow = accent;
            glow.a = scaleAlpha(glow.a, scaleAlpha(tokens_.glowOpacity, opacity));
            const int glowWidth = std::max(24, bounds.w / 2);
            const SDL_Rect glowBounds{
                bounds.x + (bounds.w - glowWidth) / 2,
                bounds.y - 12,
                glowWidth,
                std::max(24, bounds.h / 3)
            };
            ok = engine_->drawRadialGlow(glowBounds, glow) && ok;
        }

        ok = surface(bounds, fill, edge, tokens_.cornerRadius, true, opacity) && ok;

        accent.a = scaleAlpha(accent.a, focused ? 232 : 112);
        const int railWidth = std::min(bounds.w - 2 * tokens_.spaceLg,
                                       focused ? 44 : 28);
        if (railWidth > 0 && bounds.h >= 14) {
            ok = fillRounded(bounds.x + tokens_.spaceLg, bounds.y + tokens_.spaceSm,
                             railWidth, 3, tokens_.radiusXs, accent) && ok;
        }
        return ok;
    }

    bool widgetDivider(int x, int y, int width, SDL_Color color,
                       int opacity = 255) {
        color.a = scaleAlpha(color.a, opacity);
        return width > 0 && fillRect(x, y, width, 1, color);
    }

    bool button(const SDL_Rect& bounds, ButtonVariant variant,
                ButtonState state, SDL_Color fill, SDL_Color edge,
                int radius = 10, SDL_Color accent = SDL_Color{0,0,0,0}) {
        if (!valid(bounds)) return false;
        const ButtonPaintRules paint = resolveButtonPaint(variant, state);
        fill = withScaledAlpha(fill, paint.alphaScale);
        edge = withScaledAlpha(edge, paint.alphaScale);
        accent = accent.a == 0 ? tokens_.accent : accent;
        accent = withScaledAlpha(accent, paint.alphaScale);
        bool ok = true;

        if (paint.drawFill)
            ok = fillRounded(bounds.x, bounds.y, bounds.w, bounds.h, radius, fill) && ok;
        if (paint.drawEdge)
            ok = outlineRounded(bounds.x, bounds.y, bounds.w, bounds.h, radius, edge) && ok;
        if (paint.drawAccentRail) {
            const int railHeight = variant == ButtonVariant::Rail ? 3 : 2;
            const int inset = variant == ButtonVariant::Minimal
                ? std::min(18, std::max(0, (bounds.w - 4) / 2)) : 0;
            ok = fillRect(bounds.x + inset, bounds.y + bounds.h - railHeight,
                          std::max(1, bounds.w - inset * 2), railHeight, accent) && ok;
        }
        if (paint.drawFocusRing) {
            SDL_Color focus = withScaledAlpha(tokens_.focus, paint.alphaScale);
            const int ring = tokens_.focusRingWidth;
            for (int layer = 0; layer < ring; ++layer) {
                ok = outlineRounded(bounds.x - 2 - layer, bounds.y - 2 - layer,
                    bounds.w + 4 + layer * 2, bounds.h + 4 + layer * 2,
                    radius + 2 + layer, focus) && ok;
            }
        }
        return ok;
    }

    bool progressBar(const SDL_Rect& bounds, float progress,
                     SDL_Color track, SDL_Color value,
                     int radius = 6) {
        if (!valid(bounds)) return false;
        const float bounded = std::isfinite(progress)
            ? std::max(0.0f, std::min(1.0f, progress)) : 0.0f;
        bool ok = fillRounded(bounds.x, bounds.y, bounds.w, bounds.h, radius, track);
        const int width = static_cast<int>(
            static_cast<float>(bounds.w) * bounded + 0.5f);
        if (width > 0) {
            ok = fillRounded(bounds.x, bounds.y, width, bounds.h,
                             std::min(radius, width / 2), value) && ok;
        }
        return ok;
    }

    bool divider(int x, int y, int width, SDL_Color color) {
        return width > 0 && fillRect(x, y, width, 1, color);
    }

private:
    static bool valid(const SDL_Rect& bounds) noexcept {
        return bounds.w > 0 && bounds.h > 0;
    }

    static Uint8 scaleAlpha(Uint8 alpha, int scale) noexcept {
        return static_cast<Uint8>(
            (static_cast<unsigned int>(alpha) *
             static_cast<unsigned int>(clampToken(scale, 0, 255))) / 255u);
    }

    static SDL_Color withScaledAlpha(SDL_Color color, std::uint8_t scale) noexcept {
        color.a = scaleAlpha(color.a, scale);
        return color;
    }

    int boundedRadius(int radius) const noexcept {
        return clampToken(radius, 0, tokens_.cornerRadius);
    }

    bool fillRect(int x, int y, int width, int height, SDL_Color color) {
        if (!renderer_ || width <= 0 || height <= 0) return false;
        if (engine_ && engine_->ready() &&
            engine_->fillRect(x, y, width, height, color)) return true;
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0)
            return false;
        const SDL_Rect rect{x, y, width, height};
        return SDL_RenderFillRect(renderer_, &rect) == 0;
    }

    bool fillRounded(int x, int y, int width, int height,
                     int radius, SDL_Color color) {
        if (!renderer_ || width <= 0 || height <= 0) return false;
        const int safeRadius = boundedRadius(radius);
        if (engine_ && engine_->ready() &&
            engine_->fillRoundedRect(x, y, width, height, safeRadius, color)) return true;

        const int corner = std::max(0, std::min(
            safeRadius, std::min(width / 2, height / 2)));
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0)
            return false;
        if (corner == 0) {
            const SDL_Rect rect{x, y, width, height};
            return SDL_RenderFillRect(renderer_, &rect) == 0;
        }
        bool ok = true;
        if (height > 2 * corner) {
            const SDL_Rect middle{x, y + corner, width, height - 2 * corner};
            ok = SDL_RenderFillRect(renderer_, &middle) == 0 && ok;
        }
        for (int row = 0; row < corner; ++row) {
            const double dy = static_cast<double>(corner - row);
            const int inset = static_cast<int>(std::ceil(
                static_cast<double>(corner) - std::sqrt(std::max(
                    0.0, static_cast<double>(corner * corner) - dy * dy))));
            ok = SDL_RenderDrawLine(renderer_, x + inset, y + row,
                x + width - inset - 1, y + row) == 0 && ok;
            ok = SDL_RenderDrawLine(renderer_, x + inset, y + height - 1 - row,
                x + width - inset - 1, y + height - 1 - row) == 0 && ok;
        }
        return ok;
    }

    bool outlineRounded(int x, int y, int width, int height,
                        int radius, SDL_Color color) {
        if (!renderer_ || width <= 0 || height <= 0) return false;
        const int safeRadius = boundedRadius(radius);
        if (engine_ && engine_->ready() &&
            engine_->drawRoundedOutline(x, y, width, height, safeRadius, color)) return true;

        const int corner = std::max(0, std::min(
            safeRadius, std::min(width / 2, height / 2)));
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0)
            return false;
        if (corner == 0) {
            const SDL_Rect rect{x, y, width, height};
            return SDL_RenderDrawRect(renderer_, &rect) == 0;
        }
        bool ok = true;
        if (width > 2 * corner) {
            ok = SDL_RenderDrawLine(renderer_, x + corner, y,
                x + width - corner - 1, y) == 0 && ok;
            ok = SDL_RenderDrawLine(renderer_, x + corner, y + height - 1,
                x + width - corner - 1, y + height - 1) == 0 && ok;
        }
        if (height > 2 * corner) {
            ok = SDL_RenderDrawLine(renderer_, x, y + corner,
                x, y + height - corner - 1) == 0 && ok;
            ok = SDL_RenderDrawLine(renderer_, x + width - 1, y + corner,
                x + width - 1, y + height - corner - 1) == 0 && ok;
        }
        for (int row = 0; row < corner; ++row) {
            const double dy = static_cast<double>(corner - row);
            const int inset = static_cast<int>(std::ceil(
                static_cast<double>(corner) - std::sqrt(std::max(
                    0.0, static_cast<double>(corner * corner) - dy * dy))));
            ok = SDL_RenderDrawPoint(renderer_, x + inset, y + row) == 0 && ok;
            ok = SDL_RenderDrawPoint(renderer_, x + width - 1 - inset, y + row) == 0 && ok;
            ok = SDL_RenderDrawPoint(renderer_, x + inset, y + height - 1 - row) == 0 && ok;
            ok = SDL_RenderDrawPoint(renderer_, x + width - 1 - inset,
                y + height - 1 - row) == 0 && ok;
        }
        return ok;
    }

    desknx::visual::VisualEngine* engine_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    ThemeTokens tokens_{};
};

} // namespace desknx::ui
