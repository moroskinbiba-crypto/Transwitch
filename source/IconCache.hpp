#pragma once

#include "../include/desknx/Model.hpp"
#include "SteamGridDbCache.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    struct Stats {
        std::size_t residentTextures{0};
        std::size_t textureBytes{0};
        std::size_t pendingRequests{0};
        std::size_t completedImages{0};
        std::size_t failedKeys{0};
        bool workerRunning{false};
    };

    Stats stats() const {
        Stats result;
        result.residentTextures = entries_.size();
        result.textureBytes = textureBytes_;
        result.failedKeys = failedUntil_.size();
        if (mutex_) {
            SDL_LockMutex(mutex_);
            result.pendingRequests = requests_.size();
            result.completedImages = completed_.size();
            result.workerRunning = worker_ != nullptr && !stopWorker_;
            SDL_UnlockMutex(mutex_);
        }
        return result;
    }

    void prefetch(const desknx::model::Tile& tile) {
        // get() is deliberately non-blocking; the worker performs file I/O and
        // decoding, while beginFrame() uploads no more than one texture.
        (void)get(tile);
    }

    // Validate dimensions from image headers before asking SDL_image to decode.
    // Used by the large preview path as well as by the asynchronous tile cache.
    static bool validateImagePath(const std::string& path) {
        struct stat st{};
        return !path.empty() && stat(path.c_str(),&st)==0 && S_ISREG(st.st_mode) &&
            st.st_size>0 && static_cast<std::uint64_t>(st.st_size)<=MaxCompressedIconBytes &&
            imageDimensionsWithinLimit(path);
    }

    void setRenderer(SDL_Renderer* renderer) {
        renderer_ = renderer;
        if (renderer_ && !ensureWorker())
            std::fprintf(stderr, "[icon-cache] worker unavailable; icons will use placeholders\n");
    }

    // Called once at the beginning of a frame. At most one completed image is
    // uploaded to the GPU per frame, so a large desktop cannot monopolise draw.
    void beginFrame() {
        loadBudget_ = 1;
        const Uint32 now = SDL_GetTicks();
        if (lastPruneTick_ == 0 || now - lastPruneTick_ >= CachePruneIntervalMs) {
            pruneUnused(now);
            lastPruneTick_ = now;
        }
        uploadCompleted();
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

    void setGameIconOverridesDir(const std::string& path) {
        if (path == gameIconOverridesDir_) return;
        gameIconOverridesDir_ = path;
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

    SDL_Texture* get(const desknx::model::Tile& tile, bool highPriority = false) {
        if (!renderer_) return nullptr;

        // Hot path: resident textures are found by stable tile identity before
        // any path lookup, filesystem call or temporary allocation.
        for (auto& entry : entries_) {
            if (entry.kind == tile.kind && entry.data == tile.data) {
                entry.lastUse = ++clock_;
                entry.lastUseMs = now;
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
        enqueue(tile, key, highPriority ? 2 : 0);
        return nullptr;
    }

    // Call after a persistent artwork override is applied or removed. Pending
    // decodes are invalidated so an older system icon cannot replace the new art.
    void invalidate() {
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

    void invalidate(desknx::model::Kind kind, const std::string& data) {
        const std::string key = makeKey(kind, data);
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
        failedUntil_.erase(key);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->kind == kind && it->data == data) {
                if (it->texture) SDL_DestroyTexture(it->texture);
                textureBytes_ -= std::min(textureBytes_, it->bytes);
                it = entries_.erase(it);
            } else ++it;
        }
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
        std::string gameOverrideDirectory;
        std::uint64_t generation{0};
        int priority{0};
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
        Uint32 lastUseMs;
        std::size_t bytes;
    };

    static constexpr std::size_t MaxEntries = 64;
    static constexpr std::size_t MaxTextureBytes = 32u * 1024u * 1024u;
    static constexpr Uint32 CacheIdleLifetimeMs = 45000;
    static constexpr Uint32 CachePruneIntervalMs = 5000;
    static constexpr std::size_t MaxPrunePerFrame = 8;
    static constexpr std::size_t MaxCompressedIconBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t MaxQueuedRequests = 48;
    // Bound decoded CPU-side pixels while the renderer is busy. One 1024x1024
    // RGBA image is at most 4 MiB, so this queue has a predictable upper bound.
    static constexpr std::size_t MaxCompletedImages = 2;
    static constexpr int MaxIconDimension = 2048;
    static constexpr std::uint32_t MaxDecodedPixels = 3u * 1024u * 1024u;

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

    void enqueue(const desknx::model::Tile& tile, const std::string& key,
                 int priority) {
        if (!ensureWorker() || pendingKeys_.find(key) != pendingKeys_.end())
            return;

        Request request;
        request.kind = tile.kind;
        request.title = tile.title;
        request.data = tile.data;
        request.key = key;
        request.customDirectory = customIconPackDir_;
        request.gameOverrideDirectory = gameIconOverridesDir_;
        request.priority = priority;

        SDL_LockMutex(mutex_);
        if (stopWorker_ || pendingKeys_.find(key) != pendingKeys_.end()) {
            SDL_UnlockMutex(mutex_);
            return;
        }
        if (requests_.size() >= MaxQueuedRequests) {
            // Keep the queue bounded while allowing the focused/selected tile
            // to replace an unstarted background prefetch.
            if (requests_.empty() || priority <= requests_.back().priority) {
                SDL_UnlockMutex(mutex_);
                return;
            }
            pendingKeys_.erase(requests_.back().key);
            requests_.pop_back();
        }
        request.generation = generation_;
        const auto position = std::find_if(
            requests_.begin(), requests_.end(),
            [&](const Request& queued) { return queued.priority < priority; });
        requests_.insert(position, std::move(request));
        pendingKeys_.insert(key);
        SDL_CondSignal(condition_);
        SDL_UnlockMutex(mutex_);
    }

    static Decoded decode(const Request& request) {
        Decoded result;
        result.kind=request.kind;
        result.data=request.data;
        result.key=request.key;
        result.generation=request.generation;
        const std::string defaultPath=desknx::model::iconCachePath(request.kind,request.data);
        if(defaultPath.empty())return result;
        // A one-off user-selected cover must be authoritative. Keep it separate
        // from the shared pack so SteamGridDB cannot silently override it.
        const std::string gameOverridePath=customPathFor(request.kind,request.data,
            std::string{},request.gameOverrideDirectory);
        const std::string customPath=customPathFor(request.kind,request.data,
            request.customDirectory,std::string{});

        std::string steamGridPath;
        if(request.kind==desknx::model::Kind::Game&&desknx::model::isTitleId(request.data)){
            char* end=nullptr;
            const unsigned long long id=std::strtoull(request.data.c_str(),&end,16);
            if(end==request.data.c_str()+request.data.size()&&id){
                desknx::steamgriddb::SteamGridDbCache cache;
                steamGridPath=cache.selectedPath(static_cast<std::uint64_t>(id));
            }
        }

        auto tryLoad=[&](const std::string& path,bool custom)->bool{
            if(path.empty())return false;
            struct stat st{};
            if(stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
            if(st.st_size<=0||static_cast<std::uint64_t>(st.st_size)>MaxCompressedIconBytes||
               !imageDimensionsWithinLimit(path)){
                if(!custom)std::remove(path.c_str());
                return false;
            }
            SDL_RWops* stream=SDL_RWFromFile(path.c_str(),"rb");
            SDL_Surface* source=stream?IMG_Load_RW(stream,1):nullptr;
            if(!source){if(!custom)std::remove(path.c_str());return false;}
            const bool dimensionsOk=source->w>0&&source->h>0&&
                source->w<=MaxIconDimension&&source->h<=MaxIconDimension&&
                static_cast<std::uint64_t>(source->w)*static_cast<std::uint64_t>(source->h)<=MaxDecodedPixels;
            if(!dimensionsOk){SDL_FreeSurface(source);if(!custom)std::remove(path.c_str());return false;}
            SDL_Surface* rgba=SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_RGBA32,0);
            SDL_FreeSurface(source);
            if(!rgba){if(!custom)std::remove(path.c_str());return false;}
            const std::size_t rowBytes=static_cast<std::size_t>(rgba->w)*4u;
            const std::size_t totalBytes=rowBytes*static_cast<std::size_t>(rgba->h);
            if(rgba->pitch<0||static_cast<std::size_t>(rgba->pitch)<rowBytes||totalBytes>MaxTextureBytes){
                SDL_FreeSurface(rgba);return false;
            }
            result.path=path;result.isCustom=custom;result.width=rgba->w;result.height=rgba->h;
            result.rgba.resize(totalBytes);
            const auto* pixels=static_cast<const Uint8*>(rgba->pixels);
            for(int y=0;y<rgba->h;++y){
                std::memcpy(result.rgba.data()+static_cast<std::size_t>(y)*rowBytes,
                    pixels+static_cast<std::size_t>(y)*static_cast<std::size_t>(rgba->pitch),rowBytes);
            }
            SDL_FreeSurface(rgba);result.success=true;return true;
        };
        // A selected SteamGridDB image takes precedence; a corrupt override
        // gracefully falls back to custom pack art and then the system icon.
        if(tryLoad(gameOverridePath,true))return result;
        // SteamGridDB files are cache-owned, not user originals: remove a
        // corrupt download once so every future frame does not retry it.
        if(tryLoad(steamGridPath,false))return result;
        if(tryLoad(customPath,true))return result;
        (void)tryLoad(defaultPath,false);
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
                                std::move(decoded.path), texture, ++clock_,
                                SDL_GetTicks(), bytes});
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

    static std::string customPathFor(desknx::model::Kind kind,
                                     const std::string& data,
                                     const std::string& customDirectory,
                                     const std::string& gameOverrideDirectory) {
        if (customDirectory.empty() && gameOverrideDirectory.empty()) return {};

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

        static const char* const extensions[] = {".svg", ".jpg", ".png", ".jpeg"};
        auto findInDirectory = [&](const std::string& directory,
                                   const std::string& base) -> std::string {
            if (directory.empty()) return {};
            for (const char* extension : extensions) {
                const std::string candidate = directory + "/" + base + extension;
                struct stat st{};
                if (stat(candidate.c_str(), &st) == 0 &&
                    S_ISREG(st.st_mode) && st.st_size > 0)
                    return candidate;
            }
            return {};
        };
        auto findFile = [&](const std::string& base) -> std::string {
            return findInDirectory(customDirectory, base);
        };

        if (kind == desknx::model::Kind::Game) {
            // One-off per-title artwork overrides a shared pack or SteamGridDB.
            const std::string perGame = findInDirectory(gameOverrideDirectory, stem);
            if (!perGame.empty()) return perGame;
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

    static double svgNumber(const std::string& text, const std::string& key) {
        std::size_t at = text.find(key);
        while (at != std::string::npos) {
            if (at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at-1])) ||
                             text[at-1] == '-' || text[at-1] == '_'))
                break;
            at = text.find(key, at + key.size());
        }
        if (at == std::string::npos) return 0.0;
        at += key.size();
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
        if (at >= text.size() || text[at++] != '=') return 0.0;
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
        if (at >= text.size() || (text[at] != '"' && text[at] != '\'')) return 0.0;
        const char quote = text[at++];
        char* end = nullptr;
        const double value = std::strtod(text.c_str() + at, &end);
        if (!end || end == text.c_str() + at || value <= 0.0 || !std::isfinite(value)) return 0.0;
        if (static_cast<std::size_t>(end - text.c_str()) >= text.size() ||
            text[static_cast<std::size_t>(end - text.c_str())] != quote) return 0.0;
        return value;
    }

    static bool svgDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        std::string header(8192, '\0');
        input.read(&header[0], static_cast<std::streamsize>(header.size()));
        header.resize(static_cast<std::size_t>(input.gcount()));
        const std::size_t start = header.find("<svg");
        if (start == std::string::npos) return false;
        const std::size_t end = header.find('>', start);
        if (end == std::string::npos) return false;
        const std::string tag = header.substr(start, end - start + 1);
        double width = svgNumber(tag, "width");
        double height = svgNumber(tag, "height");
        if (width <= 0.0 || height <= 0.0) {
            const std::size_t view = tag.find("viewBox");
            if (view == std::string::npos) return false;
            const std::size_t eq = tag.find('=', view);
            if (eq == std::string::npos) return false;
            const std::size_t quoteAt = tag.find_first_of("\"'", eq + 1);
            if (quoteAt == std::string::npos) return false;
            const char quote = tag[quoteAt];
            const std::size_t close = tag.find(quote, quoteAt + 1);
            if (close == std::string::npos) return false;
            const std::string values = tag.substr(quoteAt + 1, close - quoteAt - 1);
            double x = 0.0, y = 0.0;
            if (std::sscanf(values.c_str(), "%lf %lf %lf %lf", &x, &y, &width, &height) != 4)
                return false;
        }
        return width >= 1.0 && height >= 1.0 &&
            width <= 512.0 && height <= 512.0 && width * height <= 1024.0 * 1024.0;
    }

    static bool imageDimensionsWithinLimit(const std::string& path) {
        const std::size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            std::string extension = path.substr(dot);
            for (char& ch : extension)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (extension == ".svg") {
                struct stat st{};
                return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
                    st.st_size > 0 && st.st_size <= 128 * 1024 &&
                    svgDimensionsWithinLimit(path);
            }
        }
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
                   width * height <= MaxDecodedPixels;
        }
        if(header[0]==0&&header[1]==0&&header[2]==1)
            return icoDimensionsWithinLimit(path);
        return jpegDimensionsWithinLimit(path);
    }

    static bool icoDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path,std::ios::binary);
        unsigned char header[22]{};
        if(!input.read(reinterpret_cast<char*>(header),sizeof(header)))return false;
        if(header[0]!=0||header[1]!=0||header[2]!=1||header[3]!=0)return false;
        const unsigned count=static_cast<unsigned>(header[4])|
            (static_cast<unsigned>(header[5])<<8);
        if(count==0||count>64)return false;
        const std::uint32_t width=header[6]?header[6]:256u;
        const std::uint32_t height=header[7]?header[7]:256u;
        return width>0&&height>0&&width<=MaxIconDimension&&
            height<=MaxIconDimension&&width*height<=MaxDecodedPixels;
    }

    static bool jpegDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        unsigned char soi[2]{};
        if (!input.read(reinterpret_cast<char*>(soi), sizeof(soi)) ||
            soi[0] != 0xFF || soi[1] != 0xD8)
            return false;

        constexpr int MaxSegments = 256;
        constexpr std::uint32_t MaxPixels = MaxDecodedPixels;
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
    std::string gameIconOverridesDir_;
    std::unordered_map<std::string, Uint32> failedUntil_;
    std::unordered_set<std::string> pendingKeys_;
    std::deque<Request> requests_;
    std::deque<Decoded> completed_;

    std::uint64_t clock_{0};
    Uint32 lastPruneTick_{0};
    std::size_t loadBudget_{1};
    std::size_t textureBytes_{0};
    std::vector<Entry> entries_;
};
