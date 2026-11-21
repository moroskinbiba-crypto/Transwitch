#pragma once

namespace desknx::persistence {

// First-run desks are intentionally populated only after the application
// catalogue scan completes. Do not persist an untouched placeholder layout,
// but do persist edits made while that scan is still running.
constexpr bool shouldSaveLayoutOnShutdown(bool sdMounted,
                                          bool saveBlocked,
                                          bool hasWorkspaces,
                                          bool layoutLoaded,
                                          bool defaultLayoutPending,
                                          bool layoutUserModified) {
    if (!sdMounted || saveBlocked || !hasWorkspaces) return false;
    return (layoutLoaded && !defaultLayoutPending) || layoutUserModified;
}

} // namespace desknx::persistence
