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
#include "../include/desknx/TextLayout.hpp"
#include "../include/desknx/TextTextureBudget.hpp"
#include <cstdint>

class FontRenderer {
public:
    struct Stats {
        std::uint64_t cacheMisses{0};
        std::uint64_t deferredMisses{0};
        std::uint64_t textureBuildAttempts{0};
        std::uint64_t textureBuildTicks{0};
        std::uint32_t maxBuildAttemptsPerFrame{0};
        std::uint32_t residentTextures{0};
    };

    void beginFrame() noexcept {
        textureBudget_.beginFrame();
    }

    Stats stats() const noexcept {
        return {
            cacheMisses_,
            deferredMisses_,
            textureBuildAttempts_,
            textureBuildTicks_,
            maxBuildAttemptsPerFrame_,
            static_cast<std::uint32_t>(cache_.size())
        };
    }

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
        wrappedTextCache_.clear();
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
            ++cacheMisses_;
            if (!textureBudget_.tryConsume()) {
                ++deferredMisses_;
                return false;
            }
            ++textureBuildAttempts_;
            const Uint64 buildStarted=SDL_GetPerformanceCounter();
            SDL_Surface* surface = TTF_RenderUTF8_Blended(font, fitted.c_str(), color);
            if (!surface) {
                recordTextureBuildTime(buildStarted);
                return false;
            }
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
            const int width = surface->w, height = surface->h;
            SDL_FreeSurface(surface);
            recordTextureBuildTime(buildStarted);
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

    bool drawWrapped(const std::string& value,int x,int y,int scale,
                     SDL_Color color,int right,int maxLines,int lineHeight) {
        if(!ttfReady_||!renderer_||value.empty())return false;
        const int maxWidth=std::max(0,right-x);
        if(maxWidth==0||maxLines<=0)return true;
        TTF_Font* font=fontForScale(scale);
        if(!font)return false;
        const auto& lines=wrappedLines(value,font,scale,maxWidth,maxLines);
        // Avoid drawing only the first line when the frame budget cannot create
        // all remaining line textures. textBlock() can then use its established
        // single-line fallback, and the next frame retries with a fresh budget.
        std::uint32_t missingTextures=0;
        for(const auto& line:lines) {
            if(line.empty())continue;
            const std::string& fitted=fitToWidth(line,font,scale,maxWidth);
            if(!fitted.empty()&&find(makeKey(fitted,scale,color))==nullptr)
                ++missingTextures;
        }
        if(missingTextures>textureBudget_.remaining()) {
            deferredMisses_+=missingTextures;
            return false;
        }
        const int resolvedLineHeight=lineHeight>0?lineHeight:
            std::max(1,TTF_FontLineSkip(font));
        for(std::size_t i=0;i<lines.size();++i) {
            if(!lines[i].empty()&&!draw(lines[i],x,y+static_cast<int>(i)*resolvedLineHeight,
                scale,color,right))return false;
        }
        return true;
    }

    void shutdown() {
        clearCache();
        fittedTextCache_.clear();
        wrappedTextCache_.clear();
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
    static constexpr size_t WrappedCacheLimit = 256;
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
    std::unordered_map<std::string, std::vector<std::string>> wrappedTextCache_;
    unsigned long long clock_{0};
    desknx::ui::TextTextureFrameBudget textureBudget_{};
    std::uint64_t cacheMisses_{0};
    std::uint64_t deferredMisses_{0};
    std::uint64_t textureBuildAttempts_{0};
    std::uint64_t textureBuildTicks_{0};
    std::uint32_t maxBuildAttemptsPerFrame_{0};
    bool ttfReady_{false};
    bool plReady_{false};

    void recordTextureBuildTime(Uint64 started) noexcept {
        const Uint64 finished=SDL_GetPerformanceCounter();
        if(finished>=started)textureBuildTicks_+=finished-started;
        const auto frameAttempts=static_cast<std::uint32_t>(textureBudget_.used());
        maxBuildAttemptsPerFrame_=std::max(maxBuildAttemptsPerFrame_,frameAttempts);
    }

    const std::vector<std::string>& wrappedLines(
        const std::string& value,TTF_Font* font,int scale,
        int maxWidth,int maxLines) {
        const std::string key=std::to_string(scale)+":"+
            std::to_string(maxWidth)+":"+std::to_string(maxLines)+":"+value;
        auto cached=wrappedTextCache_.find(key);
        if(cached!=wrappedTextCache_.end())return cached->second;
        const auto measure=[font](const std::string& candidate) {
            int width=0,height=0;
            if(TTF_SizeUTF8(font,candidate.c_str(),&width,&height)!=0)
                return INT_MAX;
            return width;
        };
        auto lines=desknx::ui::WrapUtf8Text(value,maxWidth,maxLines,measure);
        if(wrappedTextCache_.size()>=WrappedCacheLimit)
            wrappedTextCache_.clear();
        return wrappedTextCache_.emplace(key,std::move(lines)).first->second;
    }

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
