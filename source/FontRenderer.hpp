#pragma once

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <climits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdint>

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
        if (!ttfReady_) return false;

        // Keep one bounded in-memory copy so other point sizes can be opened
        // gradually. The previous active face remains intact unless the
        // replacement has been read completely and its first size opens.
        if (path.empty()) {
            closeFonts();
            customFontData_.clear();
            customFontPath_.clear();
            customWarmupIndex_ = 8;
            warmupFrameCounter_ = 0;
            ++fontGeneration_;
            if (fontGeneration_ == 0) fontGeneration_ = 1;
            fittedTextCache_.clear();
            return true;
        }

        constexpr Sint64 MaxCustomFontBytes = 16 * 1024 * 1024;
        if (path.size() > 512) return false;
        SDL_RWops* file = SDL_RWFromFile(path.c_str(), "rb");
        if (!file) return false;
        const Sint64 fileSize = SDL_RWsize(file);
        if (fileSize <= 0 || fileSize > MaxCustomFontBytes) {
            SDL_RWclose(file);
            return false;
        }

        std::vector<Uint8> candidateData(static_cast<size_t>(fileSize));
        const size_t bytesRead = SDL_RWread(
            file, candidateData.data(), 1, candidateData.size());
        SDL_RWclose(file);
        if (bytesRead != candidateData.size()) return false;

        SDL_RWops* stream = SDL_RWFromConstMem(
            candidateData.data(), static_cast<int>(candidateData.size()));
        if (!stream) return false;
        TTF_Font* candidate = TTF_OpenFontRW(stream, 1, fontPixelSize(0));
        if (!candidate) return false;

        closeFonts();
        customFontData_ = std::move(candidateData);
        customFonts_[0] = candidate;
        customFontPath_ = path;
        customWarmupIndex_ = 1;
        warmupFrameCounter_ = 0;
        ++fontGeneration_;
        if (fontGeneration_ == 0) fontGeneration_ = 1;
        fittedTextCache_.clear();
        // Keep old textures in the bounded LRU. Font generation/pointer are
        // part of the key, so stale glyphs cannot be shown after replacement.
        return true;
    }

    const std::string& customFontPath() const { return customFontPath_; }

    void beginFrame() { newTexturesRemaining_ = NewTextureBudget; }

    // Open one additional point size from RAM every three frames. Until that
    // size is ready, rendering falls back to the shared system font rather
    // than doing disk I/O or parsing a large font synchronously in draw().
    bool pumpWarmup() {
        if (customFontPath_.empty() || customFontData_.empty() ||
            customWarmupIndex_ >= 8)
            return false;
        if (++warmupFrameCounter_ < 3) return true;
        warmupFrameCounter_ = 0;
        const int index = customWarmupIndex_++;
        SDL_RWops* stream = SDL_RWFromConstMem(
            customFontData_.data(), static_cast<int>(customFontData_.size()));
        if (stream)
            customFonts_[index] = TTF_OpenFontRW(stream, 1, fontPixelSize(index));
        return customWarmupIndex_ < 8;
    }

    bool draw(const std::string& value, int x, int y, int scale,
              SDL_Color color, int right) {
        return drawImpl(value, x, y, scale, color, x, right, false, 0);
    }

    bool drawCentered(const std::string& value, int centerX, int y, int scale,
                      SDL_Color color, int left, int right) {
        return drawImpl(value, centerX, y, scale, color,
                        left, right, true, centerX);
    }

    void shutdown() {
        clearCache();
        fittedTextCache_.clear();
        closeFonts();
        customFontData_.clear();
        customFontPath_.clear();
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
    static constexpr int NewTextureBudget = 12;
    static int fontPixelSize(int index) {
        // Small labels are noticeably clearer at 10 px; larger scale steps
        // retain a consistent, predictable hierarchy for cards and headings.
        return 10 + std::max(0, std::min(7, index)) * 8;
    }

    SDL_Renderer* renderer_{nullptr};
    PlFontData sharedFont_{};
    TTF_Font* fonts_[8]{};
    TTF_Font* customFonts_[8]{};
    std::string customFontPath_;
    std::vector<Uint8> customFontData_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, std::string> fittedTextCache_;
    unsigned long long clock_{0};
    uint64_t fontGeneration_{1};
    int customWarmupIndex_{8};
    int warmupFrameCounter_{0};
    int newTexturesRemaining_{NewTextureBudget};
    bool ttfReady_{false};
    bool plReady_{false};

    void closeFonts() {
        for (auto*& font : fonts_) {
            if (font) TTF_CloseFont(font);
            font = nullptr;
        }
        for (auto*& font : customFonts_) {
            if (font) TTF_CloseFont(font);
            font = nullptr;
        }
    }

    TTF_Font* fontForScale(int scale) {
        const int index = std::max(0, std::min(7, scale - 1));
        if (customFonts_[index]) return customFonts_[index];
        if (fonts_[index]) return fonts_[index];
        if (!sharedFont_.address || sharedFont_.size > static_cast<u32>(INT_MAX)) return nullptr;
        SDL_RWops* rw = SDL_RWFromConstMem(
            sharedFont_.address, static_cast<int>(sharedFont_.size));
        if (!rw) return nullptr;
        fonts_[index] = TTF_OpenFontRW(rw, 1, fontPixelSize(index));
        return fonts_[index];
    }

    bool drawImpl(const std::string& value, int x, int y, int scale,
                  SDL_Color color, int left, int right,
                  bool centered, int centerX) {
        if (!ttfReady_ || !renderer_ || value.empty()) return false;
        const int maxWidth = std::max(0, right - left);
        if (maxWidth == 0) return true;
        TTF_Font* font = fontForScale(scale);
        if (!font) return false;
        const std::string& fitted = fitToWidth(value, font, scale, maxWidth);
        if (fitted.empty()) return true;

        const std::string key = makeKey(fitted, scale, color, font);
        CacheEntry* entry = find(key);
        if (!entry) {
            if (newTexturesRemaining_ <= 0) return false;
            SDL_Surface* surface = TTF_RenderUTF8_Blended(font, fitted.c_str(), color);
            if (!surface) return false;
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
            const int width = surface->w, height = surface->h;
            SDL_FreeSurface(surface);
            if (!texture) return false;
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            entry = insert(key, texture, width, height);
            --newTexturesRemaining_;
        }
        entry->lastUse = ++clock_;
        const int drawWidth = std::min(entry->width, maxWidth);
        const int destinationX = centered
            ? std::max(left, std::min(right - drawWidth, centerX - drawWidth / 2))
            : x;
        SDL_Rect source{0, 0, drawWidth, entry->height};
        SDL_Rect destination{destinationX, y, drawWidth, entry->height};
        SDL_RenderCopy(renderer_, entry->texture, &source, &destination);
        return true;
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
        const std::string fitKey = std::to_string(fontGeneration_) + ":" +
            std::to_string(reinterpret_cast<std::uintptr_t>(font)) + ":" +
            std::to_string(scale) + ":" + std::to_string(maxWidth) + ":" + value;
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

    std::string makeKey(const std::string& text, int scale, SDL_Color c,
                        const TTF_Font* font) const {
        return std::to_string(fontGeneration_) + ":" +
               std::to_string(reinterpret_cast<std::uintptr_t>(font)) + ":" +
               std::to_string(scale) + ":" + std::to_string(c.r) + ":" +
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
