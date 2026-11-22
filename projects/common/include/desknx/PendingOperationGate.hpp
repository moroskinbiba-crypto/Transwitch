#pragma once

#include <cstdint>

namespace desknx::runtime {

// A menu can submit only one operation that needs the menu holder to close.
// This prevents overlapping title/Homebrew/return/close/power requests from
// rewriting each other's payload while the backend waits for applet teardown.
enum class PendingOperation : std::uint8_t {
    None = 0,
    LaunchTitle,
    LaunchHomebrew,
    ReturnToGame,
    CloseApp,
    PowerAction,
};

class PendingOperationGate {
public:
    constexpr bool TryBegin(PendingOperation operation) noexcept {
        if (operation == PendingOperation::None ||
            current_ != PendingOperation::None) {
            return false;
        }
        current_ = operation;
        return true;
    }

    constexpr bool Complete(PendingOperation operation) noexcept {
        if (operation == PendingOperation::None || current_ != operation) {
            return false;
        }
        current_ = PendingOperation::None;
        return true;
    }

    constexpr void Reset() noexcept {
        current_ = PendingOperation::None;
    }

    constexpr bool busy() const noexcept {
        return current_ != PendingOperation::None;
    }

    constexpr PendingOperation current() const noexcept {
        return current_;
    }

private:
    PendingOperation current_{PendingOperation::None};
};

} // namespace desknx::runtime
