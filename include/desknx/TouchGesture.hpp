#pragma once

#include <cstdint>
#include <cstdlib>

namespace desknx::touch {

// Android-style icon dragging: a stationary press starts a drag; movement
// before the hold delay cancels the gesture so ordinary taps stay taps.
class HoldToDrag {
public:
    static constexpr std::uint32_t HoldDelayMs = 420;
    static constexpr int MoveCancelDistance = 12;

    void begin(int x, int y, std::uint32_t now) {
        active_ = true;
        cancelled_ = false;
        dragging_ = false;
        startX_ = x;
        startY_ = y;
        startedAt_ = now;
    }

    void observeMotion(int x, int y, std::uint32_t now) {
        if (!active_ || cancelled_ || dragging_) return;
        const std::uint32_t elapsed = now - startedAt_;
        if (elapsed < HoldDelayMs &&
            std::abs(x - startX_) + std::abs(y - startY_) >= MoveCancelDistance) {
            cancelled_ = true;
        }
    }

    bool tick(std::uint32_t now) {
        if (!active_ || cancelled_ || dragging_ ||
            static_cast<std::uint32_t>(now - startedAt_) < HoldDelayMs) {
            return false;
        }
        dragging_ = true;
        return true;
    }

    void finish() {
        active_ = false;
        cancelled_ = false;
        dragging_ = false;
        startX_ = startY_ = 0;
        startedAt_ = 0;
    }

    bool active() const { return active_; }
    bool cancelled() const { return cancelled_; }
    bool dragging() const { return dragging_; }

private:
    bool active_{false};
    bool cancelled_{false};
    bool dragging_{false};
    int startX_{0};
    int startY_{0};
    std::uint32_t startedAt_{0};
};

} // namespace desknx::touch
