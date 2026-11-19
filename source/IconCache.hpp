#pragma once

#include "../include/desknx/Model.hpp"
#include "SteamGridDbCache.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Artwork reads and image decoding run on one low-pressure worker. SDL texture
// creation/destruction stays on the renderer thread: this is the same split
// used by HomeNX's IconStreamer, adapted to DeskNX's SDL2 renderer.
class IconCache {
public:
    void setRenderer(SDL_Renderer* renderer) {
        renderer_ = renderer;
        if (renderer_ && !ensureWorker())
            std::fprintf(stderr, "[icon-cache] worker unavailable; icons will use placeholders\n");
    }

    // Called once at the beginning of a frame. At most one completed image is
    // uploaded to the GPU per frame, so a large desktop cannot monopolise draw.
    void beginFrame() {
        loadBudget_ = 1;
        uploadCompleted();
    }

    // Called on the renderer thread after replacing a cached image file.
    // Pending worker results are invalidated before textures are discarded.
    void invalidateAll() {
        if(mutex_) {
            SDL_LockMutex(mutex_);
            ++generation_;
            requests_.clear();
            completed_.clear();
            pendingKeys_.clear();
            SDL_CondBroadcast(condition_);
            SDL_UnlockMutex(mutex_);
        } else {
            ++generation_;
            pendingKeys_.clear();
        }
        failedUntil_.clear();
        clearTextures();
    }

    void setCustomIconPackDir(const std::string& path) {
        if (path == customIconPackDir_) return;
        customIconPackDir_ = path;

        // Any result already being decoded belongs to the old icon pack. Bump
        // the generation and discard queued/completed work without joining the
        // worker or blocking the HOME thread.
        if (mutex_) {
            SDL_LockMutex(mutex_);
            ++generation_;
            requests_.clear();
            completed_.clear();
            pendingKeys_.clear();
            SDL_CondBroadcast(condition_);
            SDL_UnlockMutex(mutex_);
        } else {
            ++generation_;
            pendingKeys_.clear();
        }

        failedUntil_.clear();
        clearTextures();
    }

    SDL_Texture* get(const desknx::model::Tile& tile) {
        if (!renderer_) return nullptr;

        // Hot path: resident textures are found by stable tile identity before
        // any path lookup, filesystem call or temporary allocation.
        for (auto& entry : entries_) {
            if (entry.kind == tile.kind && entry.data == tile.data) {
                entry.lastUse = ++clock_;
                return entry.texture;
            }
        }

        const std::string defaultPath =
            desknx::model::iconCachePath(tile.kind, tile.data);
        if (defaultPath.empty()) return nullptr;

        const std::string key = makeKey(tile.kind, tile.data);
        const Uint32 now = SDL_GetTicks();
        const auto failed = failedUntil_.find(key);
        if (failed != failedUntil_.end()) {
            if (static_cast<Sint32>(now - failed->second) < 0) return nullptr;
            failedUntil_.erase(failed);
        }

        // Queueing is non-blocking. Missing artwork is represented by the
        // caller's placeholder until the worker finishes decoding it.
        enqueue(tile, key);
        return nullptr;
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

        pendingKeys_.clear();
        requests_.clear();
        completed_.clear();
        failedUntil_.clear();
        clearTextures();
        renderer_ = nullptr;
    }

private:
    struct Request {
        desknx::model::Kind kind{desknx::model::Kind::Game};
        std::string title;
        std::string data;
        std::string key;
        std::string customDirectory;
        std::uint64_t generation{0};
    };

    struct Decoded {
        desknx::model::Kind kind{desknx::model::Kind::Game};
        std::string data;
        std::string key;
        std::string path;
        std::uint64_t generation{0};
        int width{0};
        int height{0};
        bool isCustom{false};
        bool success{false};
        std::vector<Uint8> rgba;
    };

    struct Entry {
        desknx::model::Kind kind;
        std::string data;
        std::string path;
        SDL_Texture* texture;
        std::uint64_t lastUse;
        std::size_t bytes;
    };

    static constexpr std::size_t MaxEntries = 64;
    static constexpr std::size_t MaxTextureBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t MaxCompressedIconBytes = 2u * 1024u * 1024u;
    static constexpr std::size_t MaxQueuedRequests = 48;
    // Bound decoded CPU-side pixels while the renderer is busy. One 1024x1024
    // RGBA image is at most 4 MiB, so this queue has a predictable upper bound.
    static constexpr std::size_t MaxCompletedImages = 2;
    static constexpr int MaxIconDimension = 1024;

    static std::string makeKey(desknx::model::Kind kind, const std::string& data) {
        return desknx::model::typeCode(kind) + "\t" + data;
    }

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
        worker_ = SDL_CreateThread(&IconCache::workerEntry,
                                   "DeskNX icon decoder", this);
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
        return static_cast<IconCache*>(context)->workerLoop();
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

            // The Switch target is built with -fno-exceptions. Input file
            // size and decoded dimensions are bounded before allocating RGBA
            // storage, so decoder failures are reported through Decoded::success.
            Decoded decoded = decode(request);

            SDL_LockMutex(mutex_);
            if (!stopWorker_ && request.generation == generation_) {
                completed_.push_back(std::move(decoded));
            }
            SDL_CondBroadcast(condition_);
            SDL_UnlockMutex(mutex_);
        }
        return 0;
    }

    void enqueue(const desknx::model::Tile& tile, const std::string& key) {
        if (!ensureWorker() || pendingKeys_.find(key) != pendingKeys_.end())
            return;

        Request request;
        request.kind = tile.kind;
        request.title = tile.title;
        request.data = tile.data;
        request.key = key;
        request.customDirectory = customIconPackDir_;

        SDL_LockMutex(mutex_);
        if (stopWorker_ || requests_.size() >= MaxQueuedRequests) {
            SDL_UnlockMutex(mutex_);
            return;
        }
        request.generation = generation_;
        requests_.push_back(std::move(request));
        pendingKeys_.insert(key);
        SDL_CondSignal(condition_);
        SDL_UnlockMutex(mutex_);
    }

    static Decoded decode(const Request& request) {
        Decoded result;
        result.kind = request.kind;
        result.data = request.data;
        result.key = request.key;
        result.generation = request.generation;

        const std::string defaultPath =
            desknx::model::iconCachePath(request.kind, request.data);
        if (defaultPath.empty()) return result;

        // A selected SteamGridDB hero is the preferred game-card artwork.
        // It stays on SD and therefore remains available without a network.
        std::string steamGridPath;
        if (request.kind == desknx::model::Kind::Game &&
            desknx::model::isTitleId(request.data)) {
            const auto titleId = static_cast<std::uint64_t>(
                std::strtoull(request.data.c_str(), nullptr, 16));
            const std::string candidate =
                desknx::steamgriddb::SteamGridDbCache::heroPath(titleId);
            struct stat artStat{};
            if (titleId != 0 && stat(candidate.c_str(), &artStat) == 0 &&
                S_ISREG(artStat.st_mode) && artStat.st_size > 0 &&
                static_cast<std::uint64_t>(artStat.st_size) <= MaxCompressedIconBytes)
                steamGridPath = candidate;
        }

        const std::string customPath =
            customPathFor(request.kind, request.data, request.customDirectory);
        result.isCustom = steamGridPath.empty() && !customPath.empty();
        result.path = !steamGridPath.empty() ? steamGridPath
                    : result.isCustom ? customPath : defaultPath;

        struct stat st{};
        if (stat(result.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            return result;

        if (st.st_size <= 0 ||
            static_cast<std::uint64_t>(st.st_size) > MaxCompressedIconBytes ||
            !imageDimensionsWithinLimit(result.path)) {
            // A broken generated cache entry can be regenerated by the
            // catalogue. Never remove a file owned by the user's icon pack.
            if (!result.isCustom) std::remove(result.path.c_str());
            return result;
        }

        SDL_Surface* source = IMG_Load(result.path.c_str());
        if (!source) {
            if (!result.isCustom) std::remove(result.path.c_str());
            return result;
        }

        const bool dimensionsOk = source->w > 0 && source->h > 0 &&
            source->w <= MaxIconDimension && source->h <= MaxIconDimension &&
            static_cast<std::uint64_t>(source->w) *
                static_cast<std::uint64_t>(source->h) <=
                static_cast<std::uint64_t>(MaxIconDimension) * MaxIconDimension;
        if (!dimensionsOk) {
            SDL_FreeSurface(source);
            if (!result.isCustom) std::remove(result.path.c_str());
            return result;
        }

        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(
            source, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(source);
        if (!rgba) {
            if (!result.isCustom) std::remove(result.path.c_str());
            return result;
        }

        const std::size_t rowBytes = static_cast<std::size_t>(rgba->w) * 4u;
        const std::size_t totalBytes = rowBytes * static_cast<std::size_t>(rgba->h);
        if (rgba->pitch < 0 ||
            static_cast<std::size_t>(rgba->pitch) < rowBytes ||
            totalBytes > MaxTextureBytes) {
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
                                     static_cast<std::size_t>(rgba->pitch),
                        rowBytes);
        }
        SDL_FreeSurface(rgba);
        result.success = true;
        return result;
    }

    void uploadCompleted() {
        if (!renderer_ || loadBudget_ == 0 || !mutex_) return;

        // Ignore obsolete/failed results without spending the one-per-frame GPU
        // upload budget. The worker may finish an old request after a theme or
        // icon-pack change; generation tags prevent it entering the live cache.
        while (loadBudget_ > 0) {
            Decoded decoded;
            SDL_LockMutex(mutex_);
            if (completed_.empty()) {
                SDL_UnlockMutex(mutex_);
                return;
            }
            decoded = std::move(completed_.front());
            completed_.pop_front();
            SDL_CondSignal(condition_);
            const bool currentGeneration = decoded.generation == generation_;
            SDL_UnlockMutex(mutex_);

            pendingKeys_.erase(decoded.key);
            if (!currentGeneration) continue;

            if (!decoded.success || decoded.rgba.empty() ||
                decoded.width <= 0 || decoded.height <= 0) {
                rememberFailure(decoded.key);
                continue;
            }

            const std::size_t bytes = decoded.rgba.size();
            if (bytes == 0 || bytes > MaxTextureBytes ||
                !makeRoom(bytes)) {
                rememberFailure(decoded.key);
                continue;
            }

            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
                decoded.rgba.data(), decoded.width, decoded.height, 32,
                decoded.width * 4, SDL_PIXELFORMAT_RGBA32);
            if (!surface) {
                rememberFailure(decoded.key);
                continue;
            }
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
            SDL_FreeSurface(surface);
            if (!texture) {
                rememberFailure(decoded.key);
                continue;
            }

            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            entries_.push_back({decoded.kind, std::move(decoded.data),
                                std::move(decoded.path), texture, ++clock_, bytes});
            textureBytes_ += bytes;
            failedUntil_.erase(decoded.key);
            --loadBudget_;
        }
    }

    void rememberFailure(const std::string& key) {
        if (failedUntil_.size() >= 128) failedUntil_.erase(failedUntil_.begin());
        failedUntil_[key] = SDL_GetTicks() + 5000;
    }

    bool makeRoom(std::size_t requiredBytes) {
        if (requiredBytes > MaxTextureBytes) return false;
        while (!entries_.empty() &&
               (entries_.size() >= MaxEntries ||
                textureBytes_ > MaxTextureBytes - requiredBytes)) {
            auto oldest = std::min_element(
                entries_.begin(), entries_.end(),
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

    void clearTextures() {
        for (auto& entry : entries_) {
            if (entry.texture) SDL_DestroyTexture(entry.texture);
        }
        entries_.clear();
        textureBytes_ = 0;
        clock_ = 0;
    }

    static std::string customPathFor(desknx::model::Kind kind,
                                     const std::string& data,
                                     const std::string& customDirectory) {
        if (customDirectory.empty()) return {};

        std::string stem;
        if (kind == desknx::model::Kind::Game) {
            if (!desknx::model::isTitleId(data)) return {};
            stem = data;
            for (char& ch : stem) {
                if (ch >= 'a' && ch <= 'f')
                    ch = static_cast<char>(ch - 'a' + 'A');
            }
        } else if (kind == desknx::model::Kind::Nro) {
            const size_t slash = data.find_last_of('/');
            const size_t dot = data.find_last_of('.');
            if (slash != std::string::npos) {
                const size_t nameStart = slash + 1;
                const size_t nameEnd =
                    (dot != std::string::npos && dot > nameStart)
                        ? dot : data.size();
                stem = data.substr(nameStart, nameEnd - nameStart);
            }
            if (stem.empty()) {
                char key[32]{};
                std::snprintf(key, sizeof(key), "nro-%016llX",
                    static_cast<unsigned long long>(
                        desknx::model::stableHash(data)));
                stem = key;
            }
        } else {
            return {};
        }

        static const char* const extensions[] = {".jpg", ".png", ".jpeg"};
        auto findFile = [&](const std::string& base) -> std::string {
            for (const char* extension : extensions) {
                const std::string candidate =
                    customDirectory + "/" + base + extension;
                struct stat st{};
                if (stat(candidate.c_str(), &st) == 0 &&
                    S_ISREG(st.st_mode) && st.st_size > 0)
                    return candidate;
            }
            return {};
        };

        if (kind == desknx::model::Kind::Game) {
            // Prefer real landscape box art over the square NACP system icon.
            const std::string coverBases[] = {
                "cover-" + stem, stem + "-cover",
                stem + "_cover", "covers/" + stem
            };
            for (const auto& base : coverBases) {
                const std::string cover = findFile(base);
                if (!cover.empty()) return cover;
            }
        }

        std::string found = findFile(stem);
        if (!found.empty()) return found;
        if (kind == desknx::model::Kind::Nro && stem.rfind("nro-", 0) != 0) {
            char key[32]{};
            std::snprintf(key, sizeof(key), "nro-%016llX",
                static_cast<unsigned long long>(
                    desknx::model::stableHash(data)));
            return findFile(key);
        }
        return {};
    }

    static bool imageDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        unsigned char header[24]{};
        if (!input.read(reinterpret_cast<char*>(header), 8)) return false;

        static const unsigned char pngSignature[] = {
            137, 80, 78, 71, 13, 10, 26, 10
        };
        bool png = true;
        for (size_t i = 0; i < 8; ++i) {
            if (header[i] != pngSignature[i]) png = false;
        }
        if (png) {
            if (!input.read(reinterpret_cast<char*>(header + 8), 16))
                return false;
            const std::uint32_t width =
                (static_cast<std::uint32_t>(header[16]) << 24) |
                (static_cast<std::uint32_t>(header[17]) << 16) |
                (static_cast<std::uint32_t>(header[18]) << 8) |
                static_cast<std::uint32_t>(header[19]);
            const std::uint32_t height =
                (static_cast<std::uint32_t>(header[20]) << 24) |
                (static_cast<std::uint32_t>(header[21]) << 16) |
                (static_cast<std::uint32_t>(header[22]) << 8) |
                static_cast<std::uint32_t>(header[23]);
            return width > 0 && height > 0 &&
                   width <= MaxIconDimension && height <= MaxIconDimension &&
                   width * height <= 1024u * 1024u;
        }
        return jpegDimensionsWithinLimit(path);
    }

    static bool jpegDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        unsigned char soi[2]{};
        if (!input.read(reinterpret_cast<char*>(soi), sizeof(soi)) ||
            soi[0] != 0xFF || soi[1] != 0xD8)
            return false;

        constexpr int MaxSegments = 256;
        constexpr std::uint32_t MaxPixels = 1024u * 1024u;
        for (int count = 0; count < MaxSegments; ++count) {
            int byte = input.get();
            while (byte != std::char_traits<char>::eof() && byte != 0xFF)
                byte = input.get();
            if (byte == std::char_traits<char>::eof()) return false;

            do {
                byte = input.get();
            } while (byte == 0xFF);
            if (byte == std::char_traits<char>::eof()) return false;

            const unsigned char marker = static_cast<unsigned char>(byte);
            if (marker == 0xD9 || marker == 0xDA) return false;
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
                continue;

            unsigned char lengthBytes[2]{};
            if (!input.read(reinterpret_cast<char*>(lengthBytes),
                            sizeof(lengthBytes)))
                return false;
            const std::uint16_t length = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(lengthBytes[0]) << 8) |
                 static_cast<std::uint16_t>(lengthBytes[1]));
            if (length < 2) return false;

            const bool isSof =
                marker == 0xC0 || marker == 0xC1 || marker == 0xC2 ||
                marker == 0xC3 || marker == 0xC5 || marker == 0xC6 ||
                marker == 0xC7 || marker == 0xC9 || marker == 0xCA ||
                marker == 0xCB || marker == 0xCD || marker == 0xCE ||
                marker == 0xCF;
            if (isSof) {
                if (length < 7) return false;
                unsigned char sof[5]{};
                if (!input.read(reinterpret_cast<char*>(sof), sizeof(sof)))
                    return false;
                const std::uint32_t height =
                    (static_cast<std::uint32_t>(sof[1]) << 8) | sof[2];
                const std::uint32_t width =
                    (static_cast<std::uint32_t>(sof[3]) << 8) | sof[4];
                return width > 0 && height > 0 &&
                       width <= MaxIconDimension &&
                       height <= MaxIconDimension &&
                       width * height <= MaxPixels;
            }

            input.seekg(static_cast<std::streamoff>(length - 2),
                        std::ios::cur);
            if (!input) return false;
        }
        return false;
    }

    SDL_Renderer* renderer_{nullptr};
    SDL_mutex* mutex_{nullptr};
    SDL_cond* condition_{nullptr};
    SDL_Thread* worker_{nullptr};
    bool stopWorker_{false};
    std::uint64_t generation_{1};

    std::string customIconPackDir_;
    std::unordered_map<std::string, Uint32> failedUntil_;
    std::unordered_set<std::string> pendingKeys_;
    std::deque<Request> requests_;
    std::deque<Decoded> completed_;

    std::uint64_t clock_{0};
    std::size_t loadBudget_{1};
    std::size_t textureBytes_{0};
    std::vector<Entry> entries_;
};
