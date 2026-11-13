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

{"content":"#pragma once\r\n\r\n#include <cstdint>\r\n#include <cstdlib>\r\n\r\nnamespace desknx::touch {\r\n\r\n// Android-style icon dragging: a stationary press starts a drag; movement\r\n// before the hold delay cancels the gesture so ordinary taps stay taps.\r\nclass HoldToDrag {\r\npublic:\r\n    static constexpr std::uint32_t HoldDelayMs = 420;\r\n    static constexpr int MoveCancelDistance = 12;\r\n\r\n    void begin(int x, int y, std::uint32_t now) {\r\n        active_ = true;\r\n        cancelled_ = false;\r\n        dragging_ = false;\r\n        startX_ = x;\r\n        startY_ = y;\r\n        startedAt_ = now;\r\n    }\r\n\r\n    void observeMotion(int x, int y, std::uint32_t now) {\r\n        if (!active_ || cancelled_ || dragging_) return;\r\n        const std::uint32_t elapsed = now - startedAt_;\r\n        if (elapsed < HoldDelayMs &&\r\n            std::abs(x - startX_) + std::abs(y - startY_) >= MoveCancelDistance) {\r\n            cancelled_ = true;\r\n        }\r\n    }\r\n\r\n    bool tick(std::uint32_t now) {\r\n        if (!active_ || cancelled_ || dragging_ ||\r\n            static_cast<std::uint32_t>(now - startedAt_) < HoldDelayMs) {\r\n            return false;\r\n        }\r\n        dragging_ = true;\r\n        return true;\r\n    }\r\n\r\n    void finish() {\r\n        active_ = false;\r\n        cancelled_ = false;\r\n        dragging_ = false;\r\n        startX_ = startY_ = 0;\r\n        startedAt_ = 0;\r\n    }\r\n\r\n    bool active() const { return active_; }\r\n    bool cancelled() const { return cancelled_; }\r\n    bool dragging() const { return dragging_; }\r\n\r\nprivate:\r\n    bool active_{false};\r\n    bool cancelled_{false};\r\n    bool dragging_{false};\r\n    int startX_{0};\r\n    int startY_{0};\r\n    std::uint32_t startedAt_{0};\r\n};\r\n\r\n// One-step horizontal desktop navigation. A single swipe cannot skip several\r\n// workspaces if the controller or touchscreen reports many motion events.\r\nclass HorizontalPageSwipe {\r\npublic:\r\n    static constexpr int DistancePx = 56;\r\n    static constexpr int AxisBiasPx = 18;\r\n\r\n    void begin(int x, int y) {\r\n        active_ = true;\r\n        completed_ = false;\r\n        startX_ = x;\r\n        startY_ = y;\r\n    }\r\n\r\n    // Returns +1 for a left swipe (next desktop), -1 for a right swipe.\r\n    int observe(int x, int y) {\r\n        if (!active_ || completed_) return 0;\r\n        const int dx = x - startX_;\r\n        const int dy = y - startY_;\r\n        if (std::abs(dx) < DistancePx ||\r\n            std::abs(dx) < std::abs(dy) + AxisBiasPx) {\r\n            return 0;\r\n        }\r\n        completed_ = true;\r\n        return dx < 0 ? 1 : -1;\r\n    }\r\n\r\n    void finish() {\r\n        active_ = false;\r\n        completed_ = false;\r\n        startX_ = startY_ = 0;\r\n    }\r\n\r\n    bool completed() const { return completed_; }\r\n\r\nprivate:\r\n    bool active_{false};\r\n    bool completed_{false};\r\n    int startX_{0};\r\n    int startY_{0};\r\n};\r\n\r\n} // namespace desknx::touch\r\n"}