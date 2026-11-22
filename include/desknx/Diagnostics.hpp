#pragma once

// Low-overhead runtime diagnostics for DeskNX.
// Runtime events are queued in a bounded in-memory ring. The dedicated low-priority
// writer owns all diagnostic SD-card I/O; Write() never opens, flushes or closes
// files on the UI/input thread.

#include <SDL2/SDL.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace desknx::diag {
namespace detail {
inline bool gReady = false;
inline bool gAsyncWriterAttempted = false;
inline bool gStop = false;
inline SDL_mutex* gQueueMutex = nullptr;
inline SDL_cond* gQueueCondition = nullptr;
inline SDL_Thread* gWriterThread = nullptr;
inline constexpr const char* Directory = "sdmc:/switch/DeskNX";
inline constexpr const char* File = "sdmc:/switch/DeskNX/diagnostics.log";
inline constexpr const char* File1 = "sdmc:/switch/DeskNX/diagnostics.log.1";
inline constexpr const char* File2 = "sdmc:/switch/DeskNX/diagnostics.log.2";
inline constexpr long MaxLogBytes = 512 * 1024;
inline constexpr std::size_t QueueCapacity = 64;
inline constexpr std::size_t BatchCapacity = 16;
inline constexpr std::size_t MaxLineBytes = 512;

struct Record {
    char text[MaxLineBytes]{};
    std::size_t length{0};
};

inline std::array<Record, QueueCapacity> gQueue{};
inline std::size_t gQueueHead = 0;
inline std::size_t gQueueCount = 0;
inline std::uint64_t gDroppedRecords = 0;
inline FILE* gFile = nullptr;
inline long gCurrentBytes = 0;

inline void ensureDirectory() {
    mkdir("sdmc:/switch", 0777);
    mkdir(Directory, 0777);
}

inline void rotateLog() {
    if (gFile) {
        std::fflush(gFile);
        std::fclose(gFile);
        gFile = nullptr;
    }
    std::remove(File2);
    std::rename(File1, File2);
    std::rename(File, File1);
    gCurrentBytes = 0;
}

inline bool openWriterFile() {
    if (gFile) return true;
    ensureDirectory();
    struct stat st{};
    if (stat(File, &st) == 0) {
        gCurrentBytes = st.st_size > 0 ? static_cast<long>(st.st_size) : 0;
    } else {
        gCurrentBytes = 0;
    }
    if (gCurrentBytes > MaxLogBytes) rotateLog();
    gFile = std::fopen(File, "a");
    return gFile != nullptr;
}

inline void writeRecord(const Record& record) {
    if (!gFile || record.length == 0) return;
    if (gCurrentBytes + static_cast<long>(record.length) > MaxLogBytes) {
        rotateLog();
        (void)openWriterFile();
    }
    if (!gFile) return;
    const std::size_t written = std::fwrite(record.text, 1, record.length, gFile);
    gCurrentBytes += static_cast<long>(written);
}

inline Record makeRecord(const char* area, const char* event,
                         const char* format, va_list args) {
    Record record{};
    const int prefix = std::snprintf(record.text, sizeof(record.text),
        "[%lld] [%s] [%s] ",
        static_cast<long long>(std::time(nullptr)),
        area ? area : "general", event ? event : "event");
    if (prefix < 0) return record;
    std::size_t used = static_cast<std::size_t>(prefix);
    if (used >= sizeof(record.text) - 2)
        used = sizeof(record.text) - 2;
    if (format && used < sizeof(record.text) - 2) {
        const int body = std::vsnprintf(record.text + used,
            sizeof(record.text) - used, format, args);
        if (body > 0) {
            const std::size_t available = sizeof(record.text) - used - 2;
            used += static_cast<std::size_t>(body) > available
                ? available : static_cast<std::size_t>(body);
        }
    }
    if (used >= sizeof(record.text) - 1)
        used = sizeof(record.text) - 2;
    record.text[used++] = '\n';
    record.text[used] = '\0';
    record.length = used;
    return record;
}

inline void writeSynchronously(const Record& record) {
    // Used only during the short startup window before SDL's writer is started.
    if (record.length == 0) return;
    ensureDirectory();
    struct stat st{};
    if (stat(File, &st) == 0 && st.st_size > MaxLogBytes) {
        std::remove(File2);
        std::rename(File1, File2);
        std::rename(File, File1);
    }
    if (FILE* file = std::fopen(File, "a")) {
        (void)std::fwrite(record.text, 1, record.length, file);
        std::fflush(file);
        std::fclose(file);
    }
}

inline int SDLCALL writerMain(void*) {
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    (void)openWriterFile();

    for (;;) {
        Record batch[BatchCapacity]{};
        std::size_t batchCount = 0;
        std::uint64_t dropped = 0;

        SDL_LockMutex(gQueueMutex);
        while (gQueueCount == 0 && !gStop)
            SDL_CondWait(gQueueCondition, gQueueMutex);
        if (gQueueCount > 0 && gQueueCount < BatchCapacity && !gStop)
            (void)SDL_CondWaitTimeout(gQueueCondition, gQueueMutex, 12);
        while (gQueueCount > 0 && batchCount < BatchCapacity) {
            batch[batchCount++] = gQueue[gQueueHead];
            gQueueHead = (gQueueHead + 1) % QueueCapacity;
            --gQueueCount;
        }
        dropped = gDroppedRecords;
        gDroppedRecords = 0;
        const bool shouldStop = gStop && gQueueCount == 0 && batchCount == 0;
        SDL_UnlockMutex(gQueueMutex);

        if (dropped != 0) {
            Record droppedRecord{};
            const int count = std::snprintf(droppedRecord.text,
                sizeof(droppedRecord.text),
                "[%lld] [diagnostics] [records_dropped] count=%llu\n",
                static_cast<long long>(std::time(nullptr)),
                static_cast<unsigned long long>(dropped));
            if (count > 0) {
                droppedRecord.length = static_cast<std::size_t>(count) <
                    sizeof(droppedRecord.text)
                    ? static_cast<std::size_t>(count)
                    : sizeof(droppedRecord.text) - 1;
                writeRecord(droppedRecord);
            }
        }
        for (std::size_t i = 0; i < batchCount; ++i) {
            if (!gFile) (void)openWriterFile();
            writeRecord(batch[i]);
        }
        if (gFile && (batchCount > 0 || dropped > 0))
            std::fflush(gFile);
        if (shouldStop) break;
    }

    if (gFile) {
        std::fflush(gFile);
        std::fclose(gFile);
        gFile = nullptr;
    }
    return 0;
}
} // namespace detail

inline void Initialize(bool sdMounted) {
    detail::gReady = sdMounted;
    detail::gAsyncWriterAttempted = false;
    detail::gStop = false;
    detail::gQueueHead = detail::gQueueCount = 0;
    detail::gDroppedRecords = 0;
    if (!sdMounted) return;

    // Startup file creation/rotation is intentionally outside the interactive
    // loop. Later events are handled by the asynchronous writer.
    detail::ensureDirectory();
    struct stat st{};
    if (stat(detail::File, &st) == 0 && st.st_size > detail::MaxLogBytes) {
        std::remove(detail::File2);
        std::rename(detail::File1, detail::File2);
        std::rename(detail::File, detail::File1);
    }
    if (FILE* file = std::fopen(detail::File, "a")) {
        std::fprintf(file, "[%lld] [diagnostics] [logger_ready] sd_mounted=1\n",
            static_cast<long long>(std::time(nullptr)));
        std::fflush(file);
        std::fclose(file);
    }
}

inline bool StartAsyncWriter() {
    if (!detail::gReady || detail::gWriterThread) return false;
    detail::gAsyncWriterAttempted = true;
    detail::gQueueMutex = SDL_CreateMutex();
    detail::gQueueCondition = SDL_CreateCond();
    if (!detail::gQueueMutex || !detail::gQueueCondition) {
        if (detail::gQueueCondition) SDL_DestroyCond(detail::gQueueCondition);
        if (detail::gQueueMutex) SDL_DestroyMutex(detail::gQueueMutex);
        detail::gQueueCondition = nullptr;
        detail::gQueueMutex = nullptr;
        detail::gReady = false; // Never fall back to SD I/O on an input thread.
        return false;
    }
    detail::gStop = false;
    detail::gWriterThread = SDL_CreateThread(
        detail::writerMain, "DeskNX diagnostics", nullptr);
    if (!detail::gWriterThread) {
        SDL_DestroyCond(detail::gQueueCondition);
        SDL_DestroyMutex(detail::gQueueMutex);
        detail::gQueueCondition = nullptr;
        detail::gQueueMutex = nullptr;
        detail::gReady = false;
        return false;
    }
    return true;
}

inline void Write(const char* area, const char* event, const char* format, ...) {
    if (!detail::gReady) return;
    va_list args;
    va_start(args, format);
    const detail::Record record =
        detail::makeRecord(area, event, format, args);
    va_end(args);
    if (record.length == 0) return;

    if (detail::gWriterThread && detail::gQueueMutex && detail::gQueueCondition) {
        if (SDL_LockMutex(detail::gQueueMutex) != 0) return;
        if (detail::gQueueCount < detail::QueueCapacity) {
            const std::size_t tail =
                (detail::gQueueHead + detail::gQueueCount) % detail::QueueCapacity;
            detail::gQueue[tail] = record;
            ++detail::gQueueCount;
            SDL_CondSignal(detail::gQueueCondition);
        } else {
            ++detail::gDroppedRecords;
        }
        SDL_UnlockMutex(detail::gQueueMutex);
        return;
    }

    if (!detail::gAsyncWriterAttempted)
        detail::writeSynchronously(record);
}

inline void ShutdownAsyncWriter() {
    if (!detail::gWriterThread) {
        detail::gReady = false;
        return;
    }
    SDL_LockMutex(detail::gQueueMutex);
    detail::gStop = true;
    SDL_CondBroadcast(detail::gQueueCondition);
    SDL_UnlockMutex(detail::gQueueMutex);
    SDL_WaitThread(detail::gWriterThread, nullptr);
    detail::gWriterThread = nullptr;
    SDL_DestroyCond(detail::gQueueCondition);
    SDL_DestroyMutex(detail::gQueueMutex);
    detail::gQueueCondition = nullptr;
    detail::gQueueMutex = nullptr;
    detail::gReady = false;
}
} // namespace desknx::diag
