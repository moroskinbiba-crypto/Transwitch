#pragma once

#include <switch.h>
#include "../include/desknx/FontFileValidation.hpp"
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
        fontLoadMutex_ = SDL_CreateMutex();
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

        if (path.empty()) {
            if (fontLoadThread_) return false;
            closeCustomFonts();
            customFontData_.clear();
            customFontPath_.clear();
            customWarmupIndex_ = 8;
            warmupFrameCounter_ = 0;
            ++fontGeneration_;
            if (fontGeneration_ == 0) fontGeneration_ = 1;
            fittedTextCache_.clear();
            return true;
        }

        // SD reads run outside the UI thread. Only one file transaction may be
        // pending; the active face is kept until a validated replacement opens.
        if (path.size() > 512 || !fontLoadMutex_ || fontLoadThread_) return false;
        if (SDL_LockMutex(fontLoadMutex_) != 0) return false;
        fontLoadRequestedPath_ = path;
        fontLoadResultPath_.clear();
        fontLoadData_.clear();
        fontLoadSuccess_ = false;
        SDL_UnlockMutex(fontLoadMutex_);
        fontLoadFailed_ = false;
        SDL_AtomicSet(&fontLoadDone_, 0);
        fontLoadThread_ = SDL_CreateThread(fontLoadWorker, "DeskNX font file reader", this);
        if (!fontLoadThread_) {
            if (SDL_LockMutex(fontLoadMutex_) == 0) {
                fontLoadRequestedPath_.clear();
                SDL_UnlockMutex(fontLoadMutex_);
            }
            return false;
        }
        return true;
    }

    const std::string& customFontPath() const { return customFontPath_; }
    bool isLoadingCustomFont() const { return fontLoadThread_ != nullptr; }
    bool consumeLoadFailure() {
        const bool failed = fontLoadFailed_;
        fontLoadFailed_ = false;
        return failed;
    }

    void beginFrame() { newTexturesRemaining_ = NewTextureBudget; }

    // Open one additional point size from RAM every three frames. Until that
    // size is ready, rendering falls back to the shared system font rather
    // than doing disk I/O or parsing a large font synchronously in draw().
    bool pumpWarmup() {
        if (fontLoadThread_ && SDL_AtomicGet(&fontLoadDone_)) {
            SDL_WaitThread(fontLoadThread_, nullptr);
            fontLoadThread_ = nullptr;
            std::vector<Uint8> candidateData;
            std::string candidatePath;
            bool loaded = false;
            if (fontLoadMutex_ && SDL_LockMutex(fontLoadMutex_) == 0) {
                candidateData = std::move(fontLoadData_);
                candidatePath = fontLoadResultPath_;
                loaded = fontLoadSuccess_;
                fontLoadRequestedPath_.clear();
                fontLoadResultPath_.clear();
                fontLoadSuccess_ = false;
                SDL_UnlockMutex(fontLoadMutex_);
            }
            if (loaded && !candidateData.empty()) {
                SDL_RWops* stream = SDL_RWFromConstMem(
                    candidateData.data(), static_cast<int>(candidateData.size()));
                TTF_Font* candidate = stream
                    ? TTF_OpenFontRW(stream, 1, fontPixelSize(0)) : nullptr;
                if (candidate) {
                    closeCustomFonts();
                    customFontData_ = std::move(candidateData);
                    customFonts_[0] = candidate;
                    customFontPath_ = candidatePath;
                    customWarmupIndex_ = 1;
                    warmupFrameCounter_ = 0;
                    ++fontGeneration_;
                    if (fontGeneration_ == 0) fontGeneration_ = 1;
                    fittedTextCache_.clear();
                } else {
                    fontLoadFailed_ = true;
                }
            } else {
                fontLoadFailed_ = true;
            }
        }
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
        if (fontLoadThread_) {
            SDL_WaitThread(fontLoadThread_, nullptr);
            fontLoadThread_ = nullptr;
        }
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
        if (fontLoadMutex_) SDL_DestroyMutex(fontLoadMutex_);
        fontLoadMutex_ = nullptr;
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
    SDL_Thread* fontLoadThread_{nullptr};
    SDL_mutex* fontLoadMutex_{nullptr};
    SDL_atomic_t fontLoadDone_{};
    std::string fontLoadRequestedPath_;
    std::string fontLoadResultPath_;
    std::vector<Uint8> fontLoadData_;
    bool fontLoadSuccess_{false};
    bool fontLoadFailed_{false};

    static int SDLCALL fontLoadWorker(void* context) {
        auto* self = static_cast<FontRenderer*>(context);
        if (!self) return 1;
        if (!self->fontLoadMutex_) {
            SDL_AtomicSet(&self->fontLoadDone_, 1);
            return 1;
        }
        std::string path;
        if (SDL_LockMutex(self->fontLoadMutex_) != 0) {
            SDL_AtomicSet(&self->fontLoadDone_, 1);
            return 1;
        }
        path = self->fontLoadRequestedPath_;
        SDL_UnlockMutex(self->fontLoadMutex_);

        std::vector<Uint8> bytes;
        bool success = false;
        SDL_RWops* file = SDL_RWFromFile(path.c_str(), "rb");
        if (file) {
            const Sint64 fileSize = SDL_RWsize(file);
            constexpr Sint64 MaxCustomFontBytes = 16 * 1024 * 1024;
            if (fileSize >= 12 && fileSize <= MaxCustomFontBytes) {
                bytes.resize(static_cast<std::size_t>(fileSize));
                std::size_t offset = 0;
                while (offset < bytes.size()) {
                    const std::size_t chunk = std::min<std::size_t>(
                        64u * 1024u, bytes.size() - offset);
                    const std::size_t got = SDL_RWread(
                        file, bytes.data() + offset, 1, chunk);
                    if (got == 0) break;
                    offset += got;
                }
                success = offset == bytes.size() &&
                    desknx::font_file::validate(bytes.data(), bytes.size());
                if (!success) bytes.clear();
            }
            SDL_RWclose(file);
        }
        if (SDL_LockMutex(self->fontLoadMutex_) == 0) {
            self->fontLoadData_ = std::move(bytes);
            self->fontLoadResultPath_ = path;
            self->fontLoadSuccess_ = success;
            SDL_UnlockMutex(self->fontLoadMutex_);
        }
        SDL_AtomicSet(&self->fontLoadDone_, 1);
        return success ? 0 : 1;
    }

    void closeCustomFonts() {
        for (auto*& font : customFonts_) {
            if (font) TTF_CloseFont(font);
            font = nullptr;
        }
    }

    void closeFonts() {
        for (auto*& font : fonts_) {
            if (font) TTF_CloseFont(font);
            font = nullptr;
        }
        closeCustomFonts();
    }

    bool isCustomFace(const TTF_Font* font) const {
        for (const auto* candidate : customFonts_)
            if (candidate == font) return true;
        return false;
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
        const int fontIndex = std::max(0, std::min(7, scale - 1));
        TTF_Font* font = fontForScale(scale);
        if (!font) return false;
        const std::string* fitted = &fitToWidth(value, font, scale, maxWidth);
        if (fitted->empty()) return true;

        std::string key = makeKey(*fitted, scale, color, font);
        CacheEntry* entry = find(key);
        if (!entry && newTexturesRemaining_ <= 0 &&
            isCustomFace(font) && fonts_[fontIndex]) {
            // During the post-change warm-up, reuse the already cached system
            // face for strings whose new-font texture has not been rebuilt.
            // This avoids falling through to bitmap placeholders.
            font = fonts_[fontIndex];
            fitted = &fitToWidth(value, font, scale, maxWidth);
            key = makeKey(*fitted, scale, color, font);
            entry = find(key);
        }
        if (!entry) {
            if (newTexturesRemaining_ <= 0) return false;
            SDL_Surface* surface = TTF_RenderUTF8_Blended(font, fitted->c_str(), color);
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
                // Build UTF-8 character boundaries once, then find the longest
                // fitting prefix in logarithmic measurements. This avoids the
                // old O(n^2) repeated substring/measure loop for long filenames.
                std::vector<std::size_t> ends;
                ends.reserve(value.size());
                for (std::size_t pos = 0; pos < value.size();) {
                    std::size_t next = pos + 1;
                    while (next < value.size() &&
                           (static_cast<unsigned char>(value[next]) & 0xC0u) == 0x80u)
                        ++next;
                    ends.push_back(next);
                    pos = next;
                }

                std::size_t low = 0;
                std::size_t high = ends.size();
                while (low < high) {
                    const std::size_t mid = low + (high - low + 1) / 2;
                    const std::size_t cut = mid == 0 ? 0 : ends[mid - 1];
                    std::string candidate = value.substr(0, cut) + ellipsis;
                    const bool fits =
                        TTF_SizeUTF8(font, candidate.c_str(),
                                     &measuredWidth, &measuredHeight) == 0 &&
                        measuredWidth <= maxWidth;
                    if (fits) low = mid;
                    else high = mid - 1;
                }
                const std::size_t cut = low == 0 ? 0 : ends[low - 1];
                fitted = value.substr(0, cut) + ellipsis;
            }
        }

        if (fittedTextCache_.size() >= FitCacheLimit) fittedTextCache_.clear();
        return fittedTextCache_.emplace(fitKey, std::move(fitted)).first->second;
    }

    std::string makeKey(const std::string& text, int scale, SDL_Color c,
                        const TTF_Font* font) const {
        // Shared/system faces remain alive across custom-font changes. Their
        // existing cached textures can be used while new custom glyphs warm.
        const std::string identity = isCustomFace(font)
            ? std::to_string(fontGeneration_) : std::string("system");
        return identity + ":" +
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
