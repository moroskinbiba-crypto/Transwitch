#pragma once

namespace desknx::runtime {

// AM's IsCompleted result is the final ownership barrier before an
// IApplicationAccessor may be joined and closed. pm:shell, when available,
// provides an additional process-exit confirmation; when it is unavailable,
// AM completion remains sufficient and avoids retaining dead titles forever.
constexpr bool ShouldReleaseApplicationHolder(
    bool processMonitorAvailable,
    bool processConfirmedMissing,
    bool completionQuerySucceeded,
    bool applicationCompleted) noexcept {
    return completionQuerySucceeded && applicationCompleted &&
           (!processMonitorAvailable || processConfirmedMissing);
}

} // namespace desknx::runtime
