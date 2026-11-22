#pragma once
// Horizon service polling isolated from the SDL renderer thread.
// The worker owns the PSM session for its entire lifetime. The UI only reads
// a small snapshot and never invokes PSM/statvfs directly.

#include <switch.h>
#include <SDL2/SDL.h>
#include <cerrno>
#include <cstdint>
#include <sys/statvfs.h>

namespace desknx::runtime {

struct SystemSnapshot {
    bool batteryAvailable{false};
    bool batteryFresh{false};
    std::uint8_t batteryPercent{0};
    std::uint32_t psmInitResult{0};
    std::uint32_t batteryResult{0};
    bool storageAvailable{false};
    bool storageFresh{false};
    std::uint64_t sdFreeBytes{0};
    std::uint64_t sdTotalBytes{0};
    int storageError{0};
    std::uint64_t capturedAtMs{0};
    std::uint64_t generation{0};
};

class SensorService {
public:
    SensorService() = default;
    SensorService(const SensorService&) = delete;
    SensorService& operator=(const SensorService&) = delete;

    bool start(bool sdMounted) {
        if (worker_) return true;
        sdMounted_ = sdMounted;
        mutex_ = SDL_CreateMutex();
        condition_ = SDL_CreateCond();
        if (!mutex_ || !condition_) {
            releaseSyncObjects();
            return false;
        }
        stopRequested_ = false;
        worker_ = SDL_CreateThread(&SensorService::threadEntry,
                                   "DeskNX system sensors", this);
        if (!worker_) {
            releaseSyncObjects();
            return false;
        }
        return true;
    }

    bool read(SystemSnapshot& destination) {
        if (!mutex_) return false;
        SDL_LockMutex(mutex_);
        destination = snapshot_;
        SDL_UnlockMutex(mutex_);
        return destination.generation != 0;
    }

    void stop() {
        if (mutex_) {
            SDL_LockMutex(mutex_);
            stopRequested_ = true;
            if (condition_) SDL_CondBroadcast(condition_);
            SDL_UnlockMutex(mutex_);
        }
        if (worker_) {
            SDL_WaitThread(worker_, nullptr);
            worker_ = nullptr;
        }
        releaseSyncObjects();
    }

    ~SensorService() { stop(); }

private:
    static int SDLCALL threadEntry(void* context) {
        return static_cast<SensorService*>(context)->run();
    }

    bool waitForNextSample() {
        SDL_LockMutex(mutex_);
        if (!stopRequested_)
            (void)SDL_CondWaitTimeout(condition_, mutex_, SampleIntervalMs);
        const bool keepRunning = !stopRequested_;
        SDL_UnlockMutex(mutex_);
        return keepRunning;
    }

    void publish(const SystemSnapshot& next) {
        SDL_LockMutex(mutex_);
        snapshot_ = next;
        SDL_UnlockMutex(mutex_);
    }

    int run() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        bool psmReady = false;
        unsigned int consecutiveBatteryFailures = 0;
        std::uint64_t nextPsmRetryAt = 0;
        std::uint64_t generation = 0;
        std::uint32_t lastPsmInitResult = 0;

        do {
            const std::uint64_t now = SDL_GetTicks64();
            SystemSnapshot next{};
            SDL_LockMutex(mutex_);
            next = snapshot_; // retain last-known values when a sample is stale
            SDL_UnlockMutex(mutex_);
            next.batteryFresh = false;
            next.storageFresh = false;

            if (!psmReady && now >= nextPsmRetryAt) {
                const Result initResult = psmInitialize();
                lastPsmInitResult = static_cast<std::uint32_t>(initResult);
                next.psmInitResult = lastPsmInitResult;
                if (R_SUCCEEDED(initResult)) {
                    psmReady = true;
                    consecutiveBatteryFailures = 0;
                } else {
                    // Do not spin on a service unavailable during boot/resume.
                    nextPsmRetryAt = now + PsmRetryIntervalMs;
                }
            } else {
                next.psmInitResult = lastPsmInitResult;
            }

            if (psmReady) {
                u32 percentage = 0;
                const Result batteryResult = psmGetBatteryChargePercentage(&percentage);
                next.batteryResult = static_cast<std::uint32_t>(batteryResult);
                if (R_SUCCEEDED(batteryResult) && percentage <= 100) {
                    next.batteryAvailable = true;
                    next.batteryFresh = true;
                    next.batteryPercent = static_cast<std::uint8_t>(percentage);
                    consecutiveBatteryFailures = 0;
                } else {
                    ++consecutiveBatteryFailures;
                    // Recreate a failing service session after repeated errors.
                    if (consecutiveBatteryFailures >= MaxBatteryFailures) {
                        psmExit();
                        psmReady = false;
                        nextPsmRetryAt = now + PsmRetryIntervalMs;
                        consecutiveBatteryFailures = 0;
                    }
                }
            } else if (next.psmInitResult != 0) {
                next.batteryResult = next.psmInitResult;
            }

            if (sdMounted_) {
                struct statvfs filesystem {};
                if (statvfs("sdmc:/", &filesystem) == 0 && filesystem.f_frsize > 0) {
                    const std::uint64_t blockSize =
                        static_cast<std::uint64_t>(filesystem.f_frsize);
                    next.sdTotalBytes =
                        static_cast<std::uint64_t>(filesystem.f_blocks) * blockSize;
                    next.sdFreeBytes =
                        static_cast<std::uint64_t>(filesystem.f_bavail) * blockSize;
                    next.storageAvailable = next.sdTotalBytes > 0;
                    next.storageFresh = next.storageAvailable;
                    next.storageError = 0;
                } else {
                    next.storageFresh = false;
                    next.storageError = errno;
                }
            } else {
                next.storageAvailable = false;
                next.storageFresh = false;
                next.sdFreeBytes = 0;
                next.sdTotalBytes = 0;
                next.storageError = 0;
            }

            next.capturedAtMs = SDL_GetTicks64();
            next.generation = ++generation;
            publish(next);
        } while (waitForNextSample());

        if (psmReady) psmExit();
        return 0;
    }

    void releaseSyncObjects() {
        if (condition_) {
            SDL_DestroyCond(condition_);
            condition_ = nullptr;
        }
        if (mutex_) {
            SDL_DestroyMutex(mutex_);
            mutex_ = nullptr;
        }
    }

    static constexpr Uint32 SampleIntervalMs = 10000;
    static constexpr std::uint64_t PsmRetryIntervalMs = 30000;
    static constexpr unsigned int MaxBatteryFailures = 3;
    SDL_Thread* worker_{nullptr};
    SDL_mutex* mutex_{nullptr};
    SDL_cond* condition_{nullptr};
    bool stopRequested_{false}; // protected by mutex_
    bool sdMounted_{false};
    SystemSnapshot snapshot_{};
};

} // namespace desknx::runtime
