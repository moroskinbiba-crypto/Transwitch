#pragma once

#include <switch.h>
#include "../include/desknx/FontFileValidation.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cstddef>
#include <climits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <deque>
#include <unordered_set>
#include <cstdint>

class FontRenderer {
public:
    struct FrameStats {
        std::uint32_t drawCalls{0};
        std::uint32_t cacheHits{0};
        std::uint32_t fallbackHits{0};
        std::uint32_t cacheMisses{0};
        std::uint32_t staleJobsDiscarded{0};
        std::uint32_t obsoleteTexturesPruned{0};
        std::uint32_t texturesCreated{0};
        std::uint32_t deferredByBudget{0};
        std::uint32_t rasterFailures{0};
        std::uint32_t uploadFailures{0};
        double textPreparationMs{0.0};
        double rasterizationMs{0.0};
        double textureUploadMs{0.0};
        double maximumRasterizationMs{0.0};
        double maximumTextureUploadMs{0.0};
        double fontOpenMs{0.0};
        double maximumFontOpenMs{0.0};
        std::uint32_t fontOpenOperations{0};
        std::uint32_t queuedRequests{0};
        std::uint32_t cacheEvictions{0};
        double textureDestroyMs{0.0};
        std::size_t pendingRasterJobs{0};
    };

    FrameStats lastFrameStats() const noexcept { return lastFrameStats_; }
    double lastCacheClearMs() const noexcept { return lastCacheClearMs_; }
    std::uint32_t lastCacheClearTextures() const noexcept {
        return lastCacheClearTextures_;
    }

    bool initialize() {
        performanceFrequency_ = SDL_GetPerformanceFrequency();
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

    // Text misses are queued by drawImpl and rasterized here on the next frame.
    // This keeps TTF sizing/rasterization and SDL texture creation out of the
    // tile/window traversal that is responsible for presenting the current UI.
    void beginFrame() {
        lastFrameStats_ = frameStats_;
        frameStats_ = FrameStats{};
        if (++frameSerial_ == 0) frameSerial_ = 1;
        newTexturesRemaining_ = NewTextureBudget;
        pruneStaleRasterJobs();
        pruneObsoleteCustomTextures();
        pumpRasterQueue();
        frameStats_.pendingRasterJobs = rasterQueue_.size();
    }

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
                TTF_Font* candidate = openFontMeasured(
                    stream, fontPixelSize(0));
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
            customFonts_[index] = openFontMeasured(
                stream, fontPixelSize(index));
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

    bool hasCachedText(const std::string& value,int scale,SDL_Color color,int left,int right) const {
        if(value.empty())return true;
        if(!ttfReady_||!renderer_||value.size()>MaxTextBytes)return false;
        const int maxWidth=std::max(0,right-left);
        if(maxWidth==0)return true;
        const int fontIndex=std::max(0,std::min(7,scale-1));
        const bool customReady=!customFontPath_.empty()&&customFonts_[fontIndex]!=nullptr;
        const std::string key=makeKey(value,scale,color,maxWidth,customReady);
        const auto exists=[this](const std::string& candidate) {
            return cache_.find(candidate)!=cache_.end()||emptyTextKeys_.find(candidate)!=emptyTextKeys_.end();
        };
        return exists(key)||(customReady&&exists(makeKey(value,scale,color,maxWidth,false)));
    }

    bool requestText(const std::string& value,int scale,SDL_Color color,int left,int right) {
        if(hasCachedText(value,scale,color,left,right))return true;
        if(!ttfReady_||!renderer_||value.empty()||value.size()>MaxTextBytes)return false;
        const int maxWidth=std::max(0,right-left);
        if(maxWidth==0)return true;
        const int fontIndex=std::max(0,std::min(7,scale-1));
        const bool customReady=!customFontPath_.empty()&&customFonts_[fontIndex]!=nullptr;
        const std::string key=makeKey(value,scale,color,maxWidth,customReady);
        return enqueueRaster(key,value,scale,maxWidth,color,customReady);
    }

    void shutdown() {
        if (fontLoadThread_) {
            SDL_WaitThread(fontLoadThread_, nullptr);
            fontLoadThread_ = nullptr;
        }
        rasterQueue_.clear();
        pendingRasterFrames_.clear();
        emptyTextKeys_.clear();
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
        std::uint64_t fontGeneration{0};
        bool customFace{false};
    };

    struct RasterJob {
        std::string key;
        std::string text;
        int scale{1};
        int maxWidth{0};
        SDL_Color color{255,255,255,255};
        std::uint64_t fontGeneration{0};
        bool customFace{false};
    };

    static constexpr size_t CacheLimit = 384;
    static constexpr size_t FitCacheLimit = 512;
    static constexpr std::size_t MaxPendingRasterJobs = 192;
    static constexpr size_t MaxTextBytes = 2048;
    static constexpr int NewTextureBudget = 2;
    static constexpr int MaxRasterJobsPerFrame = 2;
    static constexpr std::size_t MaxObsoleteFontTexturesPerFrame = 6;
    static constexpr double RasterFrameBudgetMs = 2.5;
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
    std::deque<RasterJob> rasterQueue_;
    std::unordered_map<std::string, std::uint64_t> pendingRasterFrames_;
    std::unordered_set<std::string> emptyTextKeys_;
    std::uint64_t frameSerial_{0};
    unsigned long long clock_{0};
    uint64_t fontGeneration_{1};
    int customWarmupIndex_{8};
    int warmupFrameCounter_{0};
    int newTexturesRemaining_{NewTextureBudget};
    Uint64 performanceFrequency_{0};
    FrameStats frameStats_{};
    FrameStats lastFrameStats_{};
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
    double lastCacheClearMs_{0.0};
    std::uint32_t lastCacheClearTextures_{0};

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

    TTF_Font* openFontMeasured(SDL_RWops* stream, int pixelSize) {
        if (!stream) return nullptr;
        const Uint64 started = SDL_GetPerformanceCounter();
        TTF_Font* font = TTF_OpenFontRW(stream, 1, pixelSize);
        const double elapsed = elapsedMs(started, SDL_GetPerformanceCounter());
        frameStats_.fontOpenMs += elapsed;
        frameStats_.maximumFontOpenMs =
            std::max(frameStats_.maximumFontOpenMs, elapsed);
        ++frameStats_.fontOpenOperations;
        return font;
    }

    TTF_Font* systemFontForScale(int index) {
        if (index < 0 || index >= 8) return nullptr;
        if (fonts_[index]) return fonts_[index];
        if (!sharedFont_.address ||
            sharedFont_.size > static_cast<u32>(INT_MAX)) return nullptr;
        SDL_RWops* rw = SDL_RWFromConstMem(
            sharedFont_.address, static_cast<int>(sharedFont_.size));
        if (!rw) return nullptr;
        fonts_[index] = openFontMeasured(rw, fontPixelSize(index));
        return fonts_[index];
    }

    TTF_Font* fontForScale(int scale) {
        const int index = std::max(0, std::min(7, scale - 1));
        if (customFonts_[index]) return customFonts_[index];
        return systemFontForScale(index);
    }

    double elapsedMs(Uint64 start, Uint64 finish) const noexcept {
        if (performanceFrequency_ == 0 || finish < start) return 0.0;
        return static_cast<double>(finish - start) * 1000.0 /
               static_cast<double>(performanceFrequency_);
    }

    bool drawImpl(const std::string& value, int x, int y, int scale,
                  SDL_Color color, int left, int right,
                  bool centered, int centerX) {
        if (!ttfReady_ || !renderer_ || value.empty()) return false;
        ++frameStats_.drawCalls;
        const int maxWidth = std::max(0, right - left);
        if (maxWidth == 0) return true;
        if (value.size() > MaxTextBytes) return false;

        const int fontIndex = std::max(0, std::min(7, scale - 1));
        const bool customReady = customFontPath_.size() > 0 &&
            customFonts_[fontIndex] != nullptr;
        const Uint64 prepareStarted = SDL_GetPerformanceCounter();
        std::string key = makeKey(value, scale, color, maxWidth, customReady);
        CacheEntry* entry = find(key);
        if (!entry && emptyTextKeys_.find(key) != emptyTextKeys_.end()) {
            frameStats_.textPreparationMs +=
                elapsedMs(prepareStarted, SDL_GetPerformanceCounter());
            return true;
        }
        CacheEntry* fallbackEntry = nullptr;
        if (!entry && customReady) {
            const std::string systemKey =
                makeKey(value, scale, color, maxWidth, false);
            fallbackEntry = find(systemKey);
        }

        frameStats_.textPreparationMs +=
            elapsedMs(prepareStarted, SDL_GetPerformanceCounter());
        if (!entry) {
            ++frameStats_.cacheMisses;
            if (!enqueueRaster(key, value, scale, maxWidth, color, customReady))
                ++frameStats_.deferredByBudget;
            entry = fallbackEntry;
            if (!entry) return false;
            ++frameStats_.fallbackHits;
        } else {
            ++frameStats_.cacheHits;
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

    bool enqueueRaster(const std::string& key, const std::string& value,
                       int scale, int maxWidth, SDL_Color color,
                       bool customFace) {
        auto pending = pendingRasterFrames_.find(key);
        if (pending != pendingRasterFrames_.end()) {
            pending->second = frameSerial_;
            return true;
        }
        if (rasterQueue_.size() >= MaxPendingRasterJobs) return false;
        RasterJob job;
        job.key = key;
        job.text = value;
        job.scale = scale;
        job.maxWidth = maxWidth;
        job.color = color;
        job.fontGeneration = fontGeneration_;
        job.customFace = customFace;
        rasterQueue_.push_back(std::move(job));
        pendingRasterFrames_.emplace(key, frameSerial_);
        ++frameStats_.queuedRequests;
        return true;
    }

    void pruneStaleRasterJobs() {
        if (rasterQueue_.empty() || frameSerial_ <= 1) return;
        const std::uint64_t lastVisibleFrame = frameSerial_ - 1;
        for (auto it = rasterQueue_.begin(); it != rasterQueue_.end();) {
            const auto pending = pendingRasterFrames_.find(it->key);
            if (pending == pendingRasterFrames_.end() ||
                pending->second < lastVisibleFrame) {
                if (pending != pendingRasterFrames_.end())
                    pendingRasterFrames_.erase(pending);
                it = rasterQueue_.erase(it);
                ++frameStats_.staleJobsDiscarded;
            } else {
                ++it;
            }
        }
    }

    void pumpRasterQueue() {
        if (!renderer_ || !ttfReady_ || rasterQueue_.empty()) return;
        const Uint64 started = SDL_GetPerformanceCounter();
        int processed = 0;
        bool openedFontThisFrame = false;

        while (!rasterQueue_.empty() && processed < MaxRasterJobsPerFrame &&
               newTexturesRemaining_ > 0) {
            if (processed > 0 && elapsedMs(started, SDL_GetPerformanceCounter()) >=
                                     RasterFrameBudgetMs)
                break;

            RasterJob job = std::move(rasterQueue_.front());
            rasterQueue_.pop_front();
            if (job.fontGeneration != fontGeneration_) {
                pendingRasterFrames_.erase(job.key);
                continue;
            }

            const int fontIndex = std::max(0, std::min(7, job.scale - 1));
            TTF_Font* font = job.customFace
                ? customFonts_[fontIndex] : fonts_[fontIndex];
            if (!font && !job.customFace && !openedFontThisFrame) {
                // Font opening is also kept out of drawImpl. At most one new
                // system face is opened in a frame; unavailable faces are
                // deferred until the next frame's raster pump.
                font = systemFontForScale(fontIndex);
                openedFontThisFrame = font != nullptr;
            }
            if (!font) {
                if (job.customFace) {
                    rasterQueue_.push_front(std::move(job));
                    break;
                }
                // A missing shared face cannot become valid by retrying the
                // same system request. Drop it; a newly warmed custom face will
                // enqueue a distinct generation/key when it becomes available.
                pendingRasterFrames_.erase(job.key);
                ++processed;
                continue;
            }

            const Uint64 prepareStarted = SDL_GetPerformanceCounter();
            const std::string* fitted =
                &fitToWidth(job.text, font, job.scale, job.maxWidth);
            frameStats_.textPreparationMs +=
                elapsedMs(prepareStarted, SDL_GetPerformanceCounter());
            if (fitted->empty()) {
                pendingRasterFrames_.erase(job.key);
                if (emptyTextKeys_.size() >= CacheLimit) emptyTextKeys_.clear();
                emptyTextKeys_.insert(job.key);
                ++processed;
                continue;
            }

            const Uint64 rasterStarted = SDL_GetPerformanceCounter();
            SDL_Surface* surface =
                TTF_RenderUTF8_Blended(font, fitted->c_str(), job.color);
            const double rasterMs =
                elapsedMs(rasterStarted, SDL_GetPerformanceCounter());
            frameStats_.rasterizationMs += rasterMs;
            frameStats_.maximumRasterizationMs =
                std::max(frameStats_.maximumRasterizationMs, rasterMs);
            if (!surface) {
                ++frameStats_.rasterFailures;
                pendingRasterFrames_.erase(job.key);
                ++processed;
                continue;
            }

            const int width = surface->w;
            const int height = surface->h;
            const Uint64 uploadStarted = SDL_GetPerformanceCounter();
            SDL_Texture* texture =
                SDL_CreateTextureFromSurface(renderer_, surface);
            SDL_FreeSurface(surface);
            const double uploadMs =
                elapsedMs(uploadStarted, SDL_GetPerformanceCounter());
            frameStats_.textureUploadMs += uploadMs;
            frameStats_.maximumTextureUploadMs =
                std::max(frameStats_.maximumTextureUploadMs, uploadMs);
            pendingRasterFrames_.erase(job.key);
            if (!texture) {
                ++frameStats_.uploadFailures;
                ++processed;
                continue;
            }

            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            (void)insert(job.key, texture, width, height,
                         job.customFace, job.fontGeneration);
            ++frameStats_.texturesCreated;
            --newTexturesRemaining_;
            ++processed;
        }
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
                        int maxWidth, bool customFace) const {
        // Key from the draw request rather than the measured/truncated output:
        // cache lookup therefore requires no TTF_SizeUTF8 call on the UI path.
        const std::string identity = customFace
            ? "custom:" + std::to_string(fontGeneration_) : "system";
        return identity + ":" + std::to_string(scale) + ":" +
               std::to_string(maxWidth) + ":" + std::to_string(c.r) + ":" +
               std::to_string(c.g) + ":" + std::to_string(c.b) + ":" +
               std::to_string(c.a) + ":" + text;
    }

    CacheEntry* find(const std::string& key) {
        auto found = cache_.find(key);
        return found == cache_.end() ? nullptr : &found->second;
    }

    CacheEntry* insert(const std::string& key, SDL_Texture* texture,
                        int width, int height, bool customFace,
                        std::uint64_t fontGeneration) {
        if (cache_.size() >= CacheLimit) {
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            if (oldest->second.texture) {
                const Uint64 destroyStarted = SDL_GetPerformanceCounter();
                SDL_DestroyTexture(oldest->second.texture);
                frameStats_.textureDestroyMs +=
                    elapsedMs(destroyStarted, SDL_GetPerformanceCounter());
            }
            cache_.erase(oldest);
            ++frameStats_.cacheEvictions;
        }
        CacheEntry entry{texture, width, height, ++clock_,
                         fontGeneration, customFace};
        return &cache_.emplace(key, entry).first->second;
    }

    void pruneObsoleteCustomTextures() {
        std::size_t removed = 0;
        for (auto it = cache_.begin(); it != cache_.end() &&
             removed < MaxObsoleteFontTexturesPerFrame;) {
            const CacheEntry& entry = it->second;
            if (!entry.customFace || entry.fontGeneration == fontGeneration_) {
                ++it;
                continue;
            }
            if (entry.texture) {
                const Uint64 destroyStarted = SDL_GetPerformanceCounter();
                SDL_DestroyTexture(entry.texture);
                frameStats_.textureDestroyMs +=
                    elapsedMs(destroyStarted, SDL_GetPerformanceCounter());
            }
            it = cache_.erase(it);
            ++removed;
            ++frameStats_.obsoleteTexturesPruned;
        }
    }

    void clearCache() {
        const Uint64 started = SDL_GetPerformanceCounter();
        std::uint32_t destroyed = 0;
        for (auto& pair : cache_) {
            if (pair.second.texture) {
                SDL_DestroyTexture(pair.second.texture);
                ++destroyed;
            }
        }
        cache_.clear();
        lastCacheClearTextures_ = destroyed;
        lastCacheClearMs_ = elapsedMs(started, SDL_GetPerformanceCounter());
    }
};
