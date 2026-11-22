#pragma once

#include <cstdint>

namespace desknx::storage {

enum class MountState : std::uint8_t {
    Unavailable,
    Mounted
};

enum class CommitBeginStatus : std::uint8_t {
    Acquired,
    Unavailable,
    Busy,
    Backoff
};

enum class CommitFinishStatus : std::uint8_t {
    Accepted,
    InvalidLease,
    StaleLease
};

struct CommitLease {
    std::uint64_t mountGeneration{0};
    std::uint64_t sequence{0};
    bool valid{false};
};

struct CommitBeginResult {
    CommitBeginStatus status{CommitBeginStatus::Unavailable};
    CommitLease lease{};
};

// Serializes the commit boundary shared by all persistent domains. The caller
// holds the platform mutex around beginCommit/finishCommit; the class itself is
// deliberately platform-independent and can be exercised by host tests.
class ControlPlane final {
public:
    static constexpr std::uint32_t RetryBaseMs = 250;
    static constexpr std::uint32_t RetryMaxMs = 30000;

    void setMounted(bool mounted) noexcept {
        const MountState next = mounted ? MountState::Mounted
                                        : MountState::Unavailable;
        if (next == state_)
            return;
        state_ = next;
        ++mountGeneration_;
        commitInFlight_ = false;
        activeSequence_ = 0;
        consecutiveFailures_ = 0;
        retryPending_ = false;
        retryAt_ = 0;
    }

    bool mounted() const noexcept { return state_ == MountState::Mounted; }
    MountState mountState() const noexcept { return state_; }
    std::uint64_t mountGeneration() const noexcept { return mountGeneration_; }
    bool commitInFlight() const noexcept { return commitInFlight_; }
    std::uint32_t consecutiveFailures() const noexcept { return consecutiveFailures_; }
    bool retryPending() const noexcept { return retryPending_; }
    std::uint32_t retryAt() const noexcept { return retryAt_; }

    CommitBeginResult beginCommit(std::uint32_t now) noexcept {
        if (!mounted())
            return {CommitBeginStatus::Unavailable, {}};
        if (commitInFlight_)
            return {CommitBeginStatus::Busy, {}};
        if (retryPending_ && !deadlineReached(now, retryAt_))
            return {CommitBeginStatus::Backoff, {}};

        retryPending_ = false;
        commitInFlight_ = true;
        ++sequence_;
        if (sequence_ == 0)
            ++sequence_;
        activeSequence_ = sequence_;
        return {CommitBeginStatus::Acquired,
                CommitLease{mountGeneration_, activeSequence_, true}};
    }

    CommitFinishStatus finishCommit(const CommitLease& lease, bool success,
                                    std::uint32_t now) noexcept {
        if (!lease.valid || lease.sequence == 0)
            return CommitFinishStatus::InvalidLease;
        if (!mounted() || lease.mountGeneration != mountGeneration_)
            return CommitFinishStatus::StaleLease;
        if (!commitInFlight_ || lease.sequence != activeSequence_)
            return CommitFinishStatus::StaleLease;

        commitInFlight_ = false;
        activeSequence_ = 0;
        if (success) {
            consecutiveFailures_ = 0;
            retryPending_ = false;
            retryAt_ = 0;
        } else {
            if (consecutiveFailures_ < MaxFailureExponent)
                ++consecutiveFailures_;
            const std::uint32_t delay = retryDelayMs(consecutiveFailures_);
            retryAt_ = now + delay;
            retryPending_ = true;
        }
        return CommitFinishStatus::Accepted;
    }

    static std::uint32_t retryDelayMs(std::uint32_t consecutiveFailures) noexcept {
        if (consecutiveFailures == 0)
            return 0;
        const std::uint32_t exponent =
            consecutiveFailures > MaxFailureExponent
                ? MaxFailureExponent - 1
                : consecutiveFailures - 1;
        std::uint32_t delay = RetryBaseMs;
        for (std::uint32_t i = 0; i < exponent && delay < RetryMaxMs; ++i) {
            delay = delay > RetryMaxMs / 2 ? RetryMaxMs : delay * 2;
        }
        return delay > RetryMaxMs ? RetryMaxMs : delay;
    }

private:
    static constexpr std::uint32_t MaxFailureExponent = 10;

    static bool deadlineReached(std::uint32_t now,
                                std::uint32_t deadline) noexcept {
        return static_cast<std::int32_t>(now - deadline) >= 0;
    }

    MountState state_{MountState::Unavailable};
    std::uint64_t mountGeneration_{0};
    std::uint64_t sequence_{0};
    std::uint64_t activeSequence_{0};
    std::uint32_t consecutiveFailures_{0};
    std::uint32_t retryAt_{0};
    bool commitInFlight_{false};
    bool retryPending_{false};
};

} // namespace desknx::storage
