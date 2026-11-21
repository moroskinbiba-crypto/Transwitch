#pragma once

#include <SDL2/SDL.h>
#include <desknx/UiComponentRules.hpp>
#include "VisualEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace desknx::ui {

struct ThemeTokens {
    SDL_Color accent{83,157,255,255};
    SDL_Color focus{246,248,252,220};
    SDL_Color edge{255,255,255,24};
    int cornerRadius{10};
};

class UiComponents {
public:
    void bind(VisualEngine* engine, SDL_Renderer* renderer) {
        engine_ = engine;
        renderer_ = renderer;
    }

    void setThemeTokens(const ThemeTokens& tokens) {
        tokens_ = tokens;
        tokens_.cornerRadius = std::max(0, std::min(16, tokens_.cornerRadius));
    }

    bool surface(const SDL_Rect& bounds, SDL_Color fill, SDL_Color edge,
                 int radius, bool shadow = true, int opacity = 255) {
        if (!valid(bounds)) return false;
        const int alpha = std::max(0, std::min(255, opacity));
        fill.a = scaleAlpha(fill.a, alpha);
        edge.a = scaleAlpha(edge.a, alpha);
        bool ok = true;
        if (shadow) {
            const SDL_Color shade{0,0,0,scaleAlpha(54,alpha)};
            ok = fillRounded(bounds.x,bounds.y+5,bounds.w,bounds.h,
                             tokens_.cornerRadius,shade) && ok;
        }
        ok = fillRounded(bounds.x,bounds.y,bounds.w,bounds.h,radius,fill) && ok;
        if (bounds.w > 2 && bounds.h > 2) {
            edge.a = scaleAlpha(edge.a, 255);
            ok = fillRounded(bounds.x+1,bounds.y+1,bounds.w-2,bounds.h-2,
                             std::max(0,radius-1),edge) && ok;
        }
        return ok;
    }

    bool button(const SDL_Rect& bounds, ButtonVariant variant,
                ButtonState state, SDL_Color fill, SDL_Color edge,
                int radius = 10, SDL_Color accent = SDL_Color{0,0,0,0}) {
        if (!valid(bounds)) return false;
        const ButtonPaintRules paint = resolveButtonPaint(variant,state);
        fill = withScaledAlpha(fill,paint.alphaScale);
        edge = withScaledAlpha(edge,paint.alphaScale);
        accent = accent.a == 0 ? tokens_.accent : accent;
        accent = withScaledAlpha(accent,paint.alphaScale);
        bool ok = true;

        if (paint.drawFill)
            ok = fillRounded(bounds.x,bounds.y,bounds.w,bounds.h,radius,fill) && ok;
        if (paint.drawEdge)
            ok = outlineRounded(bounds.x,bounds.y,bounds.w,bounds.h,radius,edge) && ok;
        if (paint.drawAccentRail) {
            const int railHeight = variant == ButtonVariant::Rail ? 3 : 2;
            const int inset = variant == ButtonVariant::Minimal
                ? std::min(18,std::max(0,(bounds.w-4)/2)) : 0;
            ok = fillRect(bounds.x+inset,bounds.y+bounds.h-railHeight,
                          std::max(1,bounds.w-inset*2),railHeight,accent) && ok;
        }
        if (paint.drawFocusRing) {
            SDL_Color focus = withScaledAlpha(tokens_.focus,paint.alphaScale);
            ok = outlineRounded(bounds.x-2,bounds.y-2,bounds.w+4,bounds.h+4,
                                radius+2,focus) && ok;
        }
        return ok;
    }

    bool divider(int x, int y, int width, SDL_Color color) {
        return width > 0 && fillRect(x,y,width,1,color);
    }

private:
    static bool valid(const SDL_Rect& bounds) {
        return bounds.w > 0 && bounds.h > 0;
    }
    static Uint8 scaleAlpha(Uint8 alpha, int scale) {
        return static_cast<Uint8>(
            (static_cast<unsigned int>(alpha) *
             static_cast<unsigned int>(std::max(0,std::min(255,scale)))) / 255u);
    }
    static SDL_Color withScaledAlpha(SDL_Color color, std::uint8_t scale) {
        color.a = scaleAlpha(color.a,scale);
        return color;
    }

    bool fillRect(int x, int y, int width, int height, SDL_Color color) {
        if (!renderer_ || width <= 0 || height <= 0) return false;
        if (engine_ && engine_->ready() &&
            engine_->fillRect(x,y,width,height,color)) return true;
        SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
        const SDL_Rect rect{x,y,width,height};
        return SDL_RenderFillRect(renderer_,&rect) == 0;
    }

    bool fillRounded(int x, int y, int width, int height,
                     int radius, SDL_Color color) {
        if (!renderer_ || width <= 0 || height <= 0) return false;
        if (engine_ && engine_->ready() &&
            engine_->fillRoundedRect(x,y,width,height,radius,color)) return true;

        const int corner = std::max(0,std::min(
            radius,std::min(width,height)/2));
        SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
        if (corner == 0) {
            const SDL_Rect rect{x,y,width,height};
            return SDL_RenderFillRect(renderer_,&rect) == 0;
        }
        bool ok = true;
        if (height > 2*corner) {
            const SDL_Rect middle{x,y+corner,width,height-2*corner};
            ok = SDL_RenderFillRect(renderer_,&middle) == 0 && ok;
        }
        for (int row=0; row<corner; ++row) {
            const double dy=static_cast<double>(corner-row);
            const int inset=static_cast<int>(std::ceil(
                static_cast<double>(corner)-std::sqrt(std::max(
                    0.0,static_cast<double>(corner*corner)-dy*dy))));
            ok = SDL_RenderDrawLine(renderer_,x+inset,y+row,
                x+width-inset-1,y+row) == 0 && ok;
            ok = SDL_RenderDrawLine(renderer_,x+inset,y+height-1-row,
                x+width-inset-1,y+height-1-row) == 0 && ok;
        }
        return ok;
    }

    bool outlineRounded(int x, int y, int width, int height,
                        int radius, SDL_Color color) {
        if (!renderer_ || width <= 0 || height <= 0) return false;
        if (engine_ && engine_->ready() &&
            engine_->drawRoundedOutline(x,y,width,height,radius,color)) return true;

        const int corner=std::max(0,std::min(
            radius,std::min(width,height)/2));
        SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
        if (corner==0) {
            const SDL_Rect rect{x,y,width,height};
            return SDL_RenderDrawRect(renderer_,&rect)==0;
        }
        bool ok=true;
        if(width>2*corner) {
            ok=SDL_RenderDrawLine(renderer_,x+corner,y,x+width-corner-1,y)==0&&ok;
            ok=SDL_RenderDrawLine(renderer_,x+corner,y+height-1,
                x+width-corner-1,y+height-1)==0&&ok;
        }
        if(height>2*corner) {
            ok=SDL_RenderDrawLine(renderer_,x,y+corner,x,y+height-corner-1)==0&&ok;
            ok=SDL_RenderDrawLine(renderer_,x+width-1,y+corner,
                x+width-1,y+height-corner-1)==0&&ok;
        }
        for(int row=0;row<corner;++row) {
            const double dy=static_cast<double>(corner-row);
            const int inset=static_cast<int>(std::ceil(
                static_cast<double>(corner)-std::sqrt(std::max(
                    0.0,static_cast<double>(corner*corner)-dy*dy))));
            ok=SDL_RenderDrawPoint(renderer_,x+inset,y+row)==0&&ok;
            ok=SDL_RenderDrawPoint(renderer_,x+width-1-inset,y+row)==0&&ok;
            ok=SDL_RenderDrawPoint(renderer_,x+inset,y+height-1-row)==0&&ok;
            ok=SDL_RenderDrawPoint(renderer_,x+width-1-inset,y+height-1-row)==0&&ok;
        }
        return ok;
    }

    VisualEngine* engine_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    ThemeTokens tokens_{};
};

} // namespace desknx::ui
