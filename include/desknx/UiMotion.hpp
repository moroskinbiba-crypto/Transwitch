#pragma once

#include <cstdint>

namespace desknx::ui::motion {

constexpr float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

constexpr float easeInOutCubic(float value) {
    const float t = clamp01(value);
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - ((-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) *
                  (-2.0f * t + 2.0f)) / 2.0f;
}

// Unsigned subtraction deliberately handles SDL tick counter wraparound.
constexpr float transitionProgress(
    std::uint32_t now, std::uint32_t startedAt, std::uint32_t durationMs) {
    if (durationMs == 0) return 1.0f;
    const std::uint32_t elapsed = now - startedAt;
    if (elapsed >= durationMs) return 1.0f;
    return easeInOutCubic(
        static_cast<float>(elapsed) / static_cast<float>(durationMs));
}

constexpr std::uint8_t fadeAlpha(
    std::uint32_t elapsedMs, std::uint32_t durationMs, std::uint8_t maximumAlpha) {
    if (durationMs == 0 || elapsedMs >= durationMs) return 0;
    const float progress = easeInOutCubic(
        static_cast<float>(elapsedMs) / static_cast<float>(durationMs));
    const float alpha = static_cast<float>(maximumAlpha) * (1.0f - progress);
    return static_cast<std::uint8_t>(alpha + 0.5f);
}

constexpr float pingPongEase(std::uint32_t now, std::uint32_t periodMs) {
    if (periodMs < 2) return 1.0f;
    const std::uint32_t half = periodMs / 2;
    const std::uint32_t phase = now % periodMs;
    const float progress = phase <= half
        ? static_cast<float>(phase) / static_cast<float>(half)
        : static_cast<float>(periodMs - phase) /
              static_cast<float>(periodMs - half);
    return easeInOutCubic(progress);
}

} // namespace desknx::ui::motion
