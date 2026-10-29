#pragma once

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <climits>
#include <string>
#include <vector>

class FontRenderer {
public:
    bool initialize() {
        if (TTF_Init() != 0) return false;
        ttfReady_ = true;
        if (R_FAILED(plInitialize(PlServiceType_User))) return false;
        plReady_ = true;
        if (R_FAILED(plGetSharedFontByType(&sharedFont_, PlSharedFontType_Standard)) ||
            !sharedFont_.address || sharedFont_.size == 0) return false;
        return true;
    }
    void setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }
    bool draw(const std::string& value, int x, int y, int scale, SDL_Color color, int right) {
        if (!ttfReady_ || !sharedFont_.address || value.empty()) return false;
        TTF_Font* font = fontForScale(scale);
        if (!font) return false;
        const std::string key = makeKey(value, scale, color);
        CacheEntry* entry = find(key);
        if (!entry) {
            SDL_Surface* surface = TTF_RenderUTF8_Blended(font, value.c_str(), color);
            if (!surface) return false;
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
            const int width = surface->w, height = surface->h;
            SDL_FreeSurface(surface);
            if (!texture) return false;
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            entry = insert(key, texture, width, height);
        }
        entry->lastUse = ++clock_;
        const int maxWidth = std::max(0, right - x);
        if (maxWidth == 0) return true;
        const int drawWidth = std::min(entry->width, maxWidth);
        SDL_Rect source{0, 0, drawWidth, entry->height};
        SDL_Rect destination{x, y, drawWidth, entry->height};
        SDL_RenderCopy(renderer_, entry->texture, &source, &destination);
        return true;
    }
    void shutdown() {
        clearCache();
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
        std::string key;
        SDL_Texture* texture{nullptr};
        int width{0}, height{0};
        unsigned long long lastUse{0};
    };
    static constexpr size_t CacheLimit = 256;
    SDL_Renderer* renderer_{nullptr};
    PlFontData sharedFont_{};
    TTF_Font* fonts_[8]{};
    std::vector<CacheEntry> cache_;
    unsigned long long clock_{0};
    bool ttfReady_{false};
    bool plReady_{false};

    TTF_Font* fontForScale(int scale) {
        const int index = std::max(0, std::min(7, scale - 1));
        if (fonts_[index]) return fonts_[index];
        if (!sharedFont_.address || sharedFont_.size > static_cast<u32>(INT_MAX)) return nullptr;
        SDL_RWops* rw = SDL_RWFromConstMem(sharedFont_.address, static_cast<int>(sharedFont_.size));
        if (!rw) return nullptr;
        fonts_[index] = TTF_OpenFontRW(rw, 1, (index + 1) * 8);
        return fonts_[index];
    }
    static std::string makeKey(const std::string& text, int scale, SDL_Color c) {
        return std::to_string(scale) + ":" + std::to_string(c.r) + ":" +
               std::to_string(c.g) + ":" + std::to_string(c.b) + ":" + text;
    }
    CacheEntry* find(const std::string& key) {
        for (auto& entry : cache_) if (entry.key == key) return &entry;
        return nullptr;
    }
    CacheEntry* insert(const std::string& key, SDL_Texture* texture, int width, int height) {
        if (cache_.size() >= CacheLimit) {
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it)
                if (it->lastUse < oldest->lastUse) oldest = it;
            if (oldest->texture) SDL_DestroyTexture(oldest->texture);
            cache_.erase(oldest);
        }
        cache_.push_back({key, texture, width, height, ++clock_});
        return &cache_.back();
    }
    void clearCache() {
        for (auto& entry : cache_) if (entry.texture) SDL_DestroyTexture(entry.texture);
        cache_.clear();
    }
};
