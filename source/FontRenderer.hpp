#pragma once

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <climits>
#include <string>
#include <unordered_map>
#include <utility>

class FontRenderer {
public:
    bool initialize() {
        if (TTF_Init() != 0) return false;
        ttfReady_ = true;
        // The system shared font is the fallback, not a hard dependency:
        // user-installed TTF/OTF fonts work even when pl is unavailable.
        if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
            plReady_ = true;
            (void)plGetSharedFontByType(&sharedFont_, PlSharedFontType_Standard);
        }
        return true;
    }

    void setRenderer(SDL_Renderer* renderer) {
        if (renderer_ != renderer) clearCache();
        renderer_ = renderer;
    }

    bool setCustomFont(const std::string& path) {
        if (path == customFontPath_) return true;
        clearCache();
        fittedTextCache_.clear();
        for (auto*& font : fonts_) {
            if (font) TTF_CloseFont(font);
            font = nullptr;
        }
        customFontPath_ = path;
        if (path.empty() || !ttfReady_) return true;
        fonts_[0] = TTF_OpenFont(path.c_str(), fontPixelSize(0));
        if (fonts_[0]) return true;
        customFontPath_.clear();
        return false;
    }

    const std::string& customFontPath() const { return customFontPath_; }

    bool draw(const std::string& value, int x, int y, int scale, SDL_Color color, int right) {
        if (!ttfReady_ || !renderer_ || value.empty()) return false;
        const int maxWidth = std::max(0, right - x);
        if (maxWidth == 0) return true;

        TTF_Font* font = fontForScale(scale);
        if (!font) return false;
        const std::string& fitted = fitToWidth(value, font, scale, maxWidth);
        if (fitted.empty()) return true;

        const std::string key = makeKey(fitted, scale, color);
        CacheEntry* entry = find(key);
        if (!entry) {
            SDL_Surface* surface = TTF_RenderUTF8_Blended(font, fitted.c_str(), color);
            if (!surface) return false;
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
            const int width = surface->w, height = surface->h;
            SDL_FreeSurface(surface);
            if (!texture) return false;
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            entry = insert(key, texture, width, height);
        }
        entry->lastUse = ++clock_;
        const int drawWidth = std::min(entry->width, maxWidth);
        SDL_Rect source{0, 0, drawWidth, entry->height};
        SDL_Rect destination{x, y, drawWidth, entry->height};
        SDL_RenderCopy(renderer_, entry->texture, &source, &destination);
        return true;
    }

    void shutdown() {
        clearCache();
        fittedTextCache_.clear();
        for (auto*& font : fonts_) {
            if (font) TTF_CloseFont(font);
            font = nullptr;
        }
        if (ttfReady_) TTF_Quit();
        ttfReady_ = false;
        if (plReady_) plExit();
        plReady_ = false;
        sharedFont_ = {};
    }

private:
    struct CacheEntry {
        SDL_Texture* texture;
        int width;
        int height;
        unsigned long long lastUse;
    };

    static constexpr size_t CacheLimit = 384;
    static constexpr size_t FitCacheLimit = 512;
    static int fontPixelSize(int index) {
        // Small labels are noticeably clearer at 10 px; larger scale steps
        // retain a consistent, predictable hierarchy for cards and headings.
        return 10 + std::max(0, std::min(7, index)) * 8;
    }

    SDL_Renderer* renderer_{nullptr};
    PlFontData sharedFont_{};
    TTF_Font* fonts_[8]{};
    std::string customFontPath_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, std::string> fittedTextCache_;
    unsigned long long clock_{0};
    bool ttfReady_{false};
    bool plReady_{false};

    TTF_Font* fontForScale(int scale) {
        const int index = std::max(0, std::min(7, scale - 1));
        if (fonts_[index]) return fonts_[index];
        const int pixelSize = fontPixelSize(index);
        if (!customFontPath_.empty()) {
            fonts_[index] = TTF_OpenFont(customFontPath_.c_str(), pixelSize);
            if (fonts_[index]) return fonts_[index];
        }
        if (!sharedFont_.address || sharedFont_.size > static_cast<u32>(INT_MAX)) return nullptr;
        SDL_RWops* rw = SDL_RWFromConstMem(sharedFont_.address, static_cast<int>(sharedFont_.size));
        if (!rw) return nullptr;
        fonts_[index] = TTF_OpenFontRW(rw, 1, pixelSize);
        return fonts_[index];
    }

    static size_t previousCodePointStart(const std::string& value, size_t end) {
        if (end == 0) return 0;
        size_t pos = end - 1;
        while (pos > 0 &&
               (static_cast<unsigned char>(value[pos]) & 0xC0) == 0x80) --pos;
        return pos;
    }

    const std::string& fitToWidth(const std::string& value, TTF_Font* font,
                                  int scale, int maxWidth) {
        const std::string fitKey = std::to_string(scale) + ":" +
                                   std::to_string(maxWidth) + ":" + value;
        auto cached = fittedTextCache_.find(fitKey);
        if (cached != fittedTextCache_.end()) return cached->second;

        std::string fitted = value;
        int measuredWidth = 0, measuredHeight = 0;
        if (TTF_SizeUTF8(font, value.c_str(), &measuredWidth, &measuredHeight) == 0 &&
            measuredWidth > maxWidth) {
            const std::string ellipsis = "\xE2\x80\xA6";
            int ellipsisWidth = 0;
            if (TTF_SizeUTF8(font, ellipsis.c_str(), &ellipsisWidth, &measuredHeight) != 0 ||
                ellipsisWidth > maxWidth) {
                fitted.clear();
            } else {
                size_t cut = value.size();
                fitted.clear();
                while (cut > 0) {
                    cut = previousCodePointStart(value, cut);
                    std::string candidate = value.substr(0, cut) + ellipsis;
                    if (TTF_SizeUTF8(font, candidate.c_str(),
                                     &measuredWidth, &measuredHeight) == 0 &&
                        measuredWidth <= maxWidth) {
                        fitted = std::move(candidate);
                        break;
                    }
                }
                if (fitted.empty() && ellipsisWidth <= maxWidth) fitted = ellipsis;
            }
        }

        if (fittedTextCache_.size() >= FitCacheLimit) fittedTextCache_.clear();
        return fittedTextCache_.emplace(fitKey, std::move(fitted)).first->second;
    }

    static std::string makeKey(const std::string& text, int scale, SDL_Color c) {
        return std::to_string(scale) + ":" + std::to_string(c.r) + ":" +
               std::to_string(c.g) + ":" + std::to_string(c.b) + ":" +
               std::to_string(c.a) + ":" + text;
    }

    CacheEntry* find(const std::string& key) {
        auto found = cache_.find(key);
        return found == cache_.end() ? nullptr : &found->second;
    }

    CacheEntry* insert(const std::string& key, SDL_Texture* texture, int width, int height) {
        if (cache_.size() >= CacheLimit) {
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
            cache_.erase(oldest);
        }
        CacheEntry entry{texture, width, height, ++clock_};
        return &cache_.emplace(key, entry).first->second;
    }

    void clearCache() {
        for (auto& pair : cache_) {
            if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
        }
        cache_.clear();
    }
};
