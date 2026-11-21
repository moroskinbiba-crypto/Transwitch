#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Image files are read and decoded off the UI thread. SDL textures are created,
// used and destroyed only by the renderer thread.
class WorkspaceBackgroundCache {
public:
    struct Stats {
        std::size_t residentTextures{0};
        std::size_t textureBytes{0};
        std::size_t queuedRequests{0};
        std::size_t decodedImages{0};
        bool workerRunning{false};
    };

    void setRenderer(SDL_Renderer* renderer) {
        if (renderer_ == renderer) return;
        clearTextures();
        renderer_ = renderer;
        if (renderer_ && !ensureWorker())
            std::fprintf(stderr,
                "[background-cache] worker unavailable; backgrounds stay on fallback\n");
    }

    SDL_Texture* get(const std::string& path, int& width, int& height) {
        width = height = 0;
        if (!renderer_ || path.empty() || path.size() > MaxPathBytes) return nullptr;

        for (auto& entry : entries_) {
            if (entry.path != path) continue;
            entry.lastUse = ++clock_;
            entry.lastUseMs = SDL_GetTicks();
            width = entry.width;
            height = entry.height;
            return entry.texture;
        }

        const Uint32 now = SDL_GetTicks();
        const auto failed = failedUntil_.find(path);
        if (failed != failedUntil_.end()) {
            if (static_cast<Sint32>(now - failed->second) < 0) return nullptr;
            failedUntil_.erase(failed);
        }

        enqueue(path);
        return nullptr;
    }

    // Called once per rendered frame. It performs only bounded texture uploads;
    // filesystem access and image decoding never happen here.
    void beginFrame() {
        if (!renderer_ || !mutex_) return;
        const Uint32 now = SDL_GetTicks();
        if (lastPruneTick_ == 0 || now - lastPruneTick_ >= CachePruneIntervalMs) {
            pruneUnused(now);
            lastPruneTick_ = now;
        }

        Decoded decoded;
        SDL_LockMutex(mutex_);
        if (completed_.empty()) {
            SDL_UnlockMutex(mutex_);
            return;
        }
        decoded = std::move(completed_.front());
        completed_.pop_front();
        SDL_CondBroadcast(condition_);
        SDL_UnlockMutex(mutex_);

        pending_.erase(decoded.path);
        if (!decoded.success || decoded.rgba.empty() ||
            decoded.width <= 0 || decoded.height <= 0) {
            rememberFailure(decoded.path, now);
            return;
        }

        const std::size_t bytes = decoded.rgba.size();
        if (bytes == 0 || bytes > MaxTextureBytes || !makeRoom(bytes)) {
            rememberFailure(decoded.path, now);
            return;
        }

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
            decoded.rgba.data(), decoded.width, decoded.height, 32,
            decoded.width * 4, SDL_PIXELFORMAT_RGBA32);
        if (!surface) {
            rememberFailure(decoded.path, now);
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture) {
            rememberFailure(decoded.path, now);
            return;
        }

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        entries_.push_back({std::move(decoded.path), texture,
            decoded.width, decoded.height, ++clock_, now, bytes});
        textureBytes_ += bytes;
        failedUntil_.erase(entries_.back().path);
    }

    Stats stats() const {
        Stats result;
        result.residentTextures = entries_.size();
        result.textureBytes = textureBytes_;
        if (mutex_) {
            SDL_LockMutex(mutex_);
            result.queuedRequests = requests_.size();
            result.decodedImages = completed_.size();
            result.workerRunning = worker_ != nullptr && !stopWorker_;
            SDL_UnlockMutex(mutex_);
        }
        return result;
    }

    void shutdown() {
        if (mutex_) {
            SDL_LockMutex(mutex_);
            stopWorker_ = true;
            requests_.clear();
            completed_.clear();
            SDL_CondBroadcast(condition_);
            SDL_UnlockMutex(mutex_);
        }
        if (worker_) {
            SDL_WaitThread(worker_, nullptr);
            worker_ = nullptr;
        }
        if (condition_) {
            SDL_DestroyCond(condition_);
            condition_ = nullptr;
        }
        if (mutex_) {
            SDL_DestroyMutex(mutex_);
            mutex_ = nullptr;
        }
        pending_.clear();
        requests_.clear();
        completed_.clear();
        failedUntil_.clear();
        clearTextures();
        renderer_ = nullptr;
    }

private:
    struct Request {
        std::string path;
    };

    struct Decoded {
        std::string path;
        int width{0};
        int height{0};
        bool success{false};
        std::vector<Uint8> rgba;
    };

    struct Entry {
        std::string path;
        SDL_Texture* texture{nullptr};
        int width{0};
        int height{0};
        std::uint64_t lastUse{0};
        Uint32 lastUseMs{0};
        std::size_t bytes{0};
    };

    static constexpr std::size_t MaxEntries = 5;
    static constexpr std::size_t MaxTextureBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t MaxSourceBytes = 24u * 1024u * 1024u;
    static constexpr std::size_t MaxPathBytes = 1024;
    static constexpr std::size_t MaxQueuedRequests = 8;
    static constexpr std::size_t MaxCompletedImages = 1;
    static constexpr std::uint32_t MaxDecodedPixels = 16u * 1024u * 1024u;
    static constexpr int MaxSourceDimension = 4096;
    static constexpr int MaxBackgroundWidth = 1600;
    static constexpr int MaxBackgroundHeight = 900;
    static constexpr Uint32 FailureRetryMs = 5000;
    static constexpr Uint32 CacheIdleLifetimeMs = 120000;
    static constexpr Uint32 CachePruneIntervalMs = 5000;
    static constexpr std::size_t MaxPrunePerFrame = 1;

    bool ensureWorker() {
        if (worker_) return true;
        if (!mutex_) mutex_ = SDL_CreateMutex();
        if (!condition_) condition_ = SDL_CreateCond();
        if (!mutex_ || !condition_) {
            if (condition_) {
                SDL_DestroyCond(condition_);
                condition_ = nullptr;
            }
            if (mutex_) {
                SDL_DestroyMutex(mutex_);
                mutex_ = nullptr;
            }
            return false;
        }
        stopWorker_ = false;
        worker_ = SDL_CreateThread(&WorkspaceBackgroundCache::workerEntry,
                                   "DeskNX background decoder", this);
        if (!worker_) {
            SDL_DestroyCond(condition_);
            SDL_DestroyMutex(mutex_);
            condition_ = nullptr;
            mutex_ = nullptr;
            return false;
        }
        return true;
    }

    static int SDLCALL workerEntry(void* context) {
        return static_cast<WorkspaceBackgroundCache*>(context)->workerLoop();
    }

    void enqueue(const std::string& path) {
        if (!ensureWorker() || pending_.find(path) != pending_.end()) return;
        SDL_LockMutex(mutex_);
        if (stopWorker_ || pending_.find(path) != pending_.end() ||
            requests_.size() >= MaxQueuedRequests) {
            SDL_UnlockMutex(mutex_);
            return;
        }
        pending_.insert(path);
        requests_.push_back({path});
        SDL_CondSignal(condition_);
        SDL_UnlockMutex(mutex_);
    }

    int workerLoop() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        for (;;) {
            Request request;
            SDL_LockMutex(mutex_);
            while (!stopWorker_ &&
                   (requests_.empty() || completed_.size() >= MaxCompletedImages)) {
                SDL_CondWait(condition_, mutex_);
            }
            if (stopWorker_) {
                SDL_UnlockMutex(mutex_);
                break;
            }
            request = std::move(requests_.front());
            requests_.pop_front();
            SDL_UnlockMutex(mutex_);

            Decoded decoded = decode(request.path);

            SDL_LockMutex(mutex_);
            if (!stopWorker_) completed_.push_back(std::move(decoded));
            SDL_CondBroadcast(condition_);
            SDL_UnlockMutex(mutex_);
        }
        return 0;
    }

    static Decoded decode(const std::string& path) {
        Decoded result;
        result.path = path;

        struct stat st{};
        if (path.empty() || path.size() > MaxPathBytes ||
            lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size <= 0 ||
            static_cast<std::uint64_t>(st.st_size) > MaxSourceBytes) {
            return result;
        }

        SDL_Surface* source = IMG_Load(path.c_str());
        if (!source) return result;
        const bool valid = source->w > 0 && source->h > 0 &&
            source->w <= MaxSourceDimension && source->h <= MaxSourceDimension &&
            static_cast<std::uint64_t>(source->w) *
                static_cast<std::uint64_t>(source->h) <= MaxDecodedPixels;
        if (!valid) {
            SDL_FreeSurface(source);
            return result;
        }

        SDL_Surface* working = source;
        if (source->w > MaxBackgroundWidth || source->h > MaxBackgroundHeight) {
            const double scale = std::min(
                static_cast<double>(MaxBackgroundWidth) / source->w,
                static_cast<double>(MaxBackgroundHeight) / source->h);
            const int width = std::max(1, static_cast<int>(source->w * scale));
            const int height = std::max(1, static_cast<int>(source->h * scale));
            SDL_Surface* resized = SDL_CreateRGBSurfaceWithFormat(
                0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
            if (resized) {
                SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
                if (SDL_BlitScaled(source, nullptr, resized, nullptr) == 0) {
                    SDL_FreeSurface(source);
                    working = resized;
                } else {
                    SDL_FreeSurface(resized);
                }
            }
        }

        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(
            working, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(working);
        if (!rgba) return result;

        const std::size_t rowBytes = static_cast<std::size_t>(rgba->w) * 4u;
        const std::size_t totalBytes = rowBytes * static_cast<std::size_t>(rgba->h);
        if (rgba->w <= 0 || rgba->h <= 0 || rgba->pitch < 0 ||
            static_cast<std::size_t>(rgba->pitch) < rowBytes ||
            totalBytes == 0 || totalBytes > MaxTextureBytes) {
            SDL_FreeSurface(rgba);
            return result;
        }

        result.width = rgba->w;
        result.height = rgba->h;
        result.rgba.resize(totalBytes);
        const auto* pixels = static_cast<const Uint8*>(rgba->pixels);
        for (int y = 0; y < rgba->h; ++y) {
            std::memcpy(result.rgba.data() + static_cast<std::size_t>(y) * rowBytes,
                pixels + static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(rgba->pitch), rowBytes);
        }
        SDL_FreeSurface(rgba);
        result.success = true;
        return result;
    }

    void rememberFailure(const std::string& path, Uint32 now) {
        if (failedUntil_.size() >= 32) failedUntil_.erase(failedUntil_.begin());
        failedUntil_[path] = now + FailureRetryMs;
    }

    bool makeRoom(std::size_t requiredBytes) {
        if (requiredBytes > MaxTextureBytes) return false;
        while (!entries_.empty() &&
               (entries_.size() >= MaxEntries ||
                textureBytes_ > MaxTextureBytes - requiredBytes)) {
            auto oldest = std::min_element(entries_.begin(), entries_.end(),
                [](const Entry& a, const Entry& b) {
                    return a.lastUse < b.lastUse;
                });
            if (oldest == entries_.end()) return false;
            if (oldest->texture) SDL_DestroyTexture(oldest->texture);
            textureBytes_ -= std::min(textureBytes_, oldest->bytes);
            entries_.erase(oldest);
        }
        return entries_.size() < MaxEntries &&
            textureBytes_ <= MaxTextureBytes - requiredBytes;
    }

    void pruneUnused(Uint32 now) {
        std::size_t removed = 0;
        for (auto it = entries_.begin();
             it != entries_.end() && removed < MaxPrunePerFrame;) {
            if (now - it->lastUseMs <= CacheIdleLifetimeMs) {
                ++it;
                continue;
            }
            if (it->texture) SDL_DestroyTexture(it->texture);
            textureBytes_ -= std::min(textureBytes_, it->bytes);
            it = entries_.erase(it);
            ++removed;
        }
    }

    void clearTextures() {
        for (auto& entry : entries_) {
            if (entry.texture) SDL_DestroyTexture(entry.texture);
        }
        entries_.clear();
        textureBytes_ = 0;
        clock_ = 0;
    }

    SDL_Renderer* renderer_{nullptr};
    SDL_mutex* mutex_{nullptr};
    SDL_cond* condition_{nullptr};
    SDL_Thread* worker_{nullptr};
    bool stopWorker_{false};
    std::unordered_set<std::string> pending_;
    std::deque<Request> requests_;
    std::deque<Decoded> completed_;
    std::unordered_map<std::string, Uint32> failedUntil_;
    std::vector<Entry> entries_;
    std::uint64_t clock_{0};
    std::size_t textureBytes_{0};
    Uint32 lastPruneTick_{0};
};
