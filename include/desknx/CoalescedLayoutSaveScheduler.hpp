#pragma once

#include <cstdint>

namespace desknx::persistence {

enum class LayoutSaveKind : std::uint8_t {
    None,
    MarkerOnly,
    Full
};

// Coalesces bursts of workspace mutations. Debounce is measured from the most
// recent edit, but maxDeferralMs guarantees that a continuous drag/resize stream
// still produces periodic snapshots. All deadline comparisons are wrap-safe.
class CoalescedLayoutSaveScheduler final {
public:
    explicit CoalescedLayoutSaveScheduler(std::uint32_t maxDeferralMs = 2000) noexcept
        : maxDeferralMs_(maxDeferralMs) {}

    void request(std::uint32_t now, std::uint32_t debounceMs,
                 bool markerOnly) noexcept {
        if (!pending_) {
            pending_ = true;
            markerOnly_ = markerOnly;
            maximumDue_ = now + maxDeferralMs_;
        } else if (!markerOnly) {
            markerOnly_ = false; // a full layout write dominates marker-only
        }
        due_ = now + debounceMs;
    }

    bool pending() const noexcept { return pending_; }

    bool ready(std::uint32_t now) const noexcept {
        return pending_ && (deadlineReached(now, due_) ||
                            deadlineReached(now, maximumDue_));
    }

    LayoutSaveKind kind() const noexcept {
        if (!pending_) return LayoutSaveKind::None;
        return markerOnly_ ? LayoutSaveKind::MarkerOnly : LayoutSaveKind::Full;
    }

    LayoutSaveKind take() noexcept {
        const LayoutSaveKind result = kind();
        clear();
        return result;
    }

    void forceDue(std::uint32_t now) noexcept {
        if (pending_) due_ = now;
    }

    void defer(std::uint32_t now, std::uint32_t delayMs) noexcept {
        if (pending_) due_ = now + delayMs;
    }

    void clear() noexcept {
        pending_ = false;
        markerOnly_ = false;
        due_ = maximumDue_ = 0;
    }

private:
    static bool deadlineReached(std::uint32_t now, std::uint32_t deadline) noexcept {
        return static_cast<std::int32_t>(now - deadline) >= 0;
    }

    std::uint32_t maxDeferralMs_{2000};
    std::uint32_t due_{0};
    std::uint32_t maximumDue_{0};
    bool pending_{false};
    bool markerOnly_{false};
};

} // namespace desknx::persistence
