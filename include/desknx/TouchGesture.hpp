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

// One-step horizontal desktop navigation. A single swipe cannot skip several
// workspaces if the controller or touchscreen reports many motion events.
class HorizontalPageSwipe {
public:
    static constexpr int DistancePx = 56;
    static constexpr int AxisBiasPx = 18;

    void begin(int x, int y) {
        active_ = true;
        completed_ = false;
        startX_ = x;
        startY_ = y;
    }

    // Returns +1 for a left swipe (next desktop), -1 for a right swipe.
    int observe(int x, int y) {
        if (!active_ || completed_) return 0;
        const int dx = x - startX_;
        const int dy = y - startY_;
        if (std::abs(dx) < DistancePx ||
            std::abs(dx) < std::abs(dy) + AxisBiasPx) {
            return 0;
        }
        completed_ = true;
        return dx < 0 ? 1 : -1;
    }

    void finish() {
        active_ = false;
        completed_ = false;
        startX_ = startY_ = 0;
    }

    bool completed() const { return completed_; }

private:
    bool active_{false};
    bool completed_{false};
    int startX_{0};
    int startY_{0};
};

} // namespace desknx::touch
