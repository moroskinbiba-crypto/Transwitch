#pragma once

#include <cstdint>

namespace desknx::ipc::rules {

constexpr std::uint16_t LegacyVersion = 1;
constexpr std::uint16_t CurrentVersion = 2;
constexpr std::uint32_t MaxPayloadSize = 0x3F00;
constexpr std::uint32_t MaxBusySeconds = 120;

constexpr bool IsSupportedVersion(std::uint16_t version) {
    return version == LegacyVersion || version == CurrentVersion;
}

constexpr bool IsValidPayloadSize(std::uint32_t size) {
    return size <= MaxPayloadSize;
}

constexpr bool IsValidBusySeconds(std::uint32_t seconds) {
    return seconds >= 1 && seconds <= MaxBusySeconds;
}

} // namespace desknx::ipc::rules
