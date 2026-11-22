#pragma once

#include <desknx/HomeNXWorkspaceFileStore.hpp>
#include <desknx/HomeNXWorkspaceProvider.hpp>

namespace desknx::menu::storage {

// Compatibility facade for the experimental HOME applet. All filesystem,
// backup and active-workspace behavior lives in the shared workspace store.
class LayoutStore final {
public:
    static constexpr const char* kLayoutPath = "sdmc:/switch/DeskNX/layout.tsv";
    static constexpr const char* kTempPath = "sdmc:/switch/DeskNX/layout.tmp";
    static constexpr const char* kBackupPath = "sdmc:/switch/DeskNX/layout.bak";

    static bool load(homenx::WorkspaceProvider& provider) {
        return homenx::WorkspaceFileStore{}.load(provider);
    }

    static bool save(const homenx::WorkspaceProvider& provider) {
        return homenx::WorkspaceFileStore{}.save(provider);
    }
};

} // namespace desknx::menu::storage
