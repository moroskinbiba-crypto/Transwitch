#include <desknx/HomeNXWorkspaceFileStore.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace desknx::model;
using namespace desknx::homenx;

namespace {
void writeFile(const std::string& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    out.close();
    assert(out.good());
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

// mkdtemp is POSIX-only. CI runs on Linux, but the suite must stay runnable on
// a developer's Windows machine too; otherwise the only way to check this code
// is the remote build.
std::string makeTempRoot() {
#if defined(_WIN32)
    const char* base = std::getenv("TEMP");
    if (base == nullptr) base = std::getenv("TMP");
    const std::filesystem::path basePath = base != nullptr ? base : ".";
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto candidate = basePath / ("desknx-workspaces-" +
            std::to_string(stamp) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (std::filesystem::create_directories(candidate, ec) && !ec)
            return candidate.string();
    }
    assert(false && "cannot create a temporary directory");
    return {};
#else
    char rootTemplate[] = "/tmp/desknx-homenx-workspaces-XXXXXX";
    char* rootValue = mkdtemp(rootTemplate);
    assert(rootValue != nullptr);
    return std::string(rootValue);
#endif
}
}

int main() {
    const std::string root = makeTempRoot();
    const WorkspaceFilePaths paths = WorkspaceFilePaths::under(root);
    WorkspaceFileStore store(paths);

    WorkspaceProvider original;
    Workspace games{"Games", {}};
    games.tiles.push_back(Tile{
        Kind::Game, "Game", "0100000000001000", 0, 0, 2, 2, 0
    });
    Workspace homebrew{"Homebrew", {}};
    homebrew.tiles.push_back(Tile{
        Kind::Nro, "Tool", "sdmc:/switch/apps/tool.nro", 1, 1, 2, 1, 1
    });
    assert(original.load({games, homebrew}, 1));

    int commits = 0;
    const auto commit = [&]() { ++commits; return true; };
    assert(store.save(original, commit));
    assert(commits == 2);
    assert(!readFile(paths.layout).empty());
    assert(!readFile(paths.active).empty());

    WorkspaceProvider loaded;
    WorkspaceLoadInfo info{};
    assert(store.load(loaded, &info));
    assert(!info.restoredLayoutBackup);
    assert(info.usedActiveSidecar);
    assert(loaded.count() == 2 && loaded.activeIndex() == 1);
    assert(loaded.activeWorkspace()->name == "Homebrew");

    // A desktop switch only replaces the active sidecar; the larger layout
    // remains untouched and the sidecar is enough to restore the selection.
    const std::string layoutBeforeSwitch = readFile(paths.layout);
    assert(loaded.switchTo(0));
    assert(store.saveActiveState(loaded, commit));
    assert(readFile(paths.layout) == layoutBeforeSwitch);
    WorkspaceProvider switchedReload;
    assert(store.load(switchedReload));
    assert(switchedReload.activeIndex() == 0);
    assert(switchedReload.activeWorkspace()->name == "Games");
    assert(loaded.switchTo(1));

    // Replacing the active sidecar is optional because the main layout also
    // contains the active index. Failed pre-commit must not change either file.
    const std::string originalLayout = readFile(paths.layout);
    const std::string originalActive = readFile(paths.active);
    assert(!store.save(original, [] { return false; }));
    assert(readFile(paths.layout) == originalLayout);
    assert(readFile(paths.active) == originalActive);

    // A second successful save rotates the first complete generation to .bak.
    assert(loaded.switchTo(0));
    assert(loaded.renameWorkspace(0, "Games renamed"));
    assert(store.save(loaded, commit));
    assert(readFile(paths.layoutBackup) == originalLayout);
    assert(readFile(paths.activeBackup) == originalActive);

    // Corrupt the current primary layout: recover the previous valid copy.
    writeFile(paths.layout, "not a DeskNX layout\n");
    WorkspaceProvider recovered;
    WorkspaceLoadInfo recoveredInfo{};
    assert(store.load(recovered, &recoveredInfo));
    assert(recoveredInfo.restoredLayoutBackup);
    assert(recovered.count() == 2);
    assert(recovered.workspaces()[0].name == "Games");

    // An oversized primary is treated as corrupt and recovered from the
    // valid backup. Without a backup, load fails without replacing live state.
    writeFile(paths.layout, std::string(1024 * 1024 + 1, 'x'));
    WorkspaceProvider oversizedRecovery;
    WorkspaceLoadInfo oversizedInfo{};
    assert(store.load(oversizedRecovery, &oversizedInfo));
    assert(oversizedInfo.restoredLayoutBackup);
    assert(oversizedRecovery.workspaces()[0].name == "Games");

    std::remove(paths.layoutBackup.c_str());
    WorkspaceProvider untouched;
    assert(!store.load(untouched));
    assert(untouched.count() == 1);
    assert(untouched.activeWorkspace()->name == "Home");

    std::remove(paths.layout.c_str());
    std::remove(paths.layoutBackup.c_str());
    std::remove(paths.active.c_str());
    std::remove(paths.activeBackup.c_str());
    std::remove(paths.layoutTemp.c_str());
    std::remove(paths.activeTemp.c_str());
    rmdir(root.c_str());
    return 0;
}
