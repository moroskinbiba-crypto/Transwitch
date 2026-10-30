#pragma once

// Low-overhead runtime diagnostics for DeskNX.
// Writes one bounded, append-only session log to the SD card. Never records
// controller input contents, user/account identifiers, save data or file contents.

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace desknx::diag {
namespace detail {
inline volatile int gLock = 0;
inline bool gReady = false;
inline constexpr const char* Directory = "sdmc:/switch/DeskNX";
inline constexpr const char* File = "sdmc:/switch/DeskNX/diagnostics.log";
inline constexpr long MaxLogBytes = 512 * 1024;

inline void lock() { while (__sync_lock_test_and_set(&gLock, 1)) {} }
inline void unlock() { __sync_lock_release(&gLock); }
inline void ensureDirectory() {
    mkdir("sdmc:/switch", 0777);
    mkdir(Directory, 0777);
}
}

inline void Initialize(bool sdMounted) {
    detail::lock();
    detail::gReady = sdMounted;
    if (sdMounted) {
        detail::ensureDirectory();
        struct stat st{};
        if (stat(detail::File, &st) == 0 && st.st_size > detail::MaxLogBytes) {
            std::remove("sdmc:/switch/DeskNX/diagnostics.log.2");
            std::rename("sdmc:/switch/DeskNX/diagnostics.log.1",
                        "sdmc:/switch/DeskNX/diagnostics.log.2");
            std::rename(detail::File, "sdmc:/switch/DeskNX/diagnostics.log.1");
        }
    }
    detail::unlock();
}

inline void Write(const char* area, const char* event, const char* format, ...) {
    detail::lock();
    if (!detail::gReady) { detail::unlock(); return; }
    detail::ensureDirectory();
    struct stat st{};
    if (stat(detail::File, &st) == 0 && st.st_size > detail::MaxLogBytes) {
        std::remove("sdmc:/switch/DeskNX/diagnostics.log.2");
        std::rename("sdmc:/switch/DeskNX/diagnostics.log.1",
                    "sdmc:/switch/DeskNX/diagnostics.log.2");
        std::rename(detail::File, "sdmc:/switch/DeskNX/diagnostics.log.1");
    }
    FILE* file = std::fopen(detail::File, "a");
    if (!file) { detail::unlock(); return; }
    const std::time_t now = std::time(nullptr);
    std::fprintf(file, "[%lld] [%s] [%s] ",
                 static_cast<long long>(now),
                 area ? area : "general", event ? event : "event");
    va_list args;
    va_start(args, format);
    if (format) std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
    detail::unlock();
}
} // namespace desknx::diag
