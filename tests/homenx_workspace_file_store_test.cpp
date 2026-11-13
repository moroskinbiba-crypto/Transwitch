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

{"content":"#include <desknx/HomeNXWorkspaceFileStore.hpp>\r\n\r\n#include <cassert>\r\n#include <chrono>\r\n#include <cstdio>\r\n#include <cstdlib>\r\n#include <filesystem>\r\n#include <fstream>\r\n#include <string>\r\n\r\n#if !defined(_WIN32)\r\n#include <unistd.h>\r\n#endif\r\n\r\nusing namespace desknx::model;\r\nusing namespace desknx::homenx;\r\n\r\nnamespace {\r\nvoid writeFile(const std::string& path, const std::string& value) {\r\n    std::ofstream out(path, std::ios::binary | std::ios::trunc);\r\n    out.write(value.data(), static_cast<std::streamsize>(value.size()));\r\n    out.close();\r\n    assert(out.good());\r\n}\r\n\r\nstd::string readFile(const std::string& path) {\r\n    std::ifstream in(path, std::ios::binary);\r\n    return std::string(std::istreambuf_iterator<char>(in),\r\n                       std::istreambuf_iterator<char>());\r\n}\r\n\r\n// mkdtemp is POSIX-only. CI runs on Linux, but the suite must stay runnable on\r\n// a developer's Windows machine too; otherwise the only way to check this code\r\n// is the remote build.\r\nstd::string makeTempRoot() {\r\n#if defined(_WIN32)\r\n    const char* base = std::getenv(\"TEMP\");\r\n    if (base == nullptr) base = std::getenv(\"TMP\");\r\n    const std::filesystem::path basePath = base != nullptr ? base : \".\";\r\n    for (int attempt = 0; attempt < 64; ++attempt) {\r\n        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();\r\n        const auto candidate = basePath / (\"desknx-workspaces-\" +\r\n            std::to_string(stamp) + \"-\" + std::to_string(attempt));\r\n        std::error_code ec;\r\n        if (std::filesystem::create_directories(candidate, ec) && !ec)\r\n            return candidate.string();\r\n    }\r\n    assert(false && \"cannot create a temporary directory\");\r\n    return {};\r\n#else\r\n    char rootTemplate[] = \"/tmp/desknx-homenx-workspaces-XXXXXX\";\r\n    char* rootValue = mkdtemp(rootTemplate);\r\n    assert(rootValue != nullptr);\r\n    return std::string(rootValue);\r\n#endif\r\n}\r\n}\r\n\r\nint main() {\r\n    const std::string root = makeTempRoot();\r\n    const WorkspaceFilePaths paths = WorkspaceFilePaths::under(root);\r\n    WorkspaceFileStore store(paths);\r\n\r\n    WorkspaceProvider original;\r\n    Workspace games{\"Games\", {}};\r\n    games.tiles.push_back(Tile{\r\n        Kind::Game, \"Game\", \"0100000000001000\", 0, 0, 2, 2, 0\r\n    });\r\n    Workspace homebrew{\"Homebrew\", {}};\r\n    homebrew.tiles.push_back(Tile{\r\n        Kind::Nro, \"Tool\", \"sdmc:/switch/apps/tool.nro\", 1, 1, 2, 1, 1\r\n    });\r\n    assert(original.load({games, homebrew}, 1));\r\n\r\n    int commits = 0;\r\n    const auto commit = [&]() { ++commits; return true; };\r\n    assert(store.save(original, commit));\r\n    assert(commits == 2);\r\n    assert(!readFile(paths.layout).empty());\r\n    assert(!readFile(paths.active).empty());\r\n\r\n    WorkspaceProvider loaded;\r\n    WorkspaceLoadInfo info{};\r\n    assert(store.load(loaded, &info));\r\n    assert(!info.restoredLayoutBackup);\r\n    assert(info.usedActiveSidecar);\r\n    assert(loaded.count() == 2 && loaded.activeIndex() == 1);\r\n    assert(loaded.activeWorkspace()->name == \"Homebrew\");\r\n\r\n    // A desktop switch only replaces the active sidecar; the larger layout\r\n    // remains untouched and the sidecar is enough to restore the selection.\r\n    const std::string layoutBeforeSwitch = readFile(paths.layout);\r\n    assert(loaded.switchTo(0));\r\n    assert(store.saveActiveState(loaded, commit));\r\n    assert(readFile(paths.layout) == layoutBeforeSwitch);\r\n    WorkspaceProvider switchedReload;\r\n    assert(store.load(switchedReload));\r\n    assert(switchedReload.activeIndex() == 0);\r\n    assert(switchedReload.activeWorkspace()->name == \"Games\");\r\n    assert(loaded.switchTo(1));\r\n\r\n    // Replacing the active sidecar is optional because the main layout also\r\n    // contains the active index. Failed pre-commit must not change either file.\r\n    const std::string originalLayout = readFile(paths.layout);\r\n    const std::string originalActive = readFile(paths.active);\r\n    assert(!store.save(original, [] { return false; }));\r\n    assert(readFile(paths.layout) == originalLayout);\r\n    assert(readFile(paths.active) == originalActive);\r\n\r\n    // A second successful save rotates the first complete generation to .bak.\r\n    assert(loaded.switchTo(0));\r\n    assert(loaded.renameWorkspace(0, \"Games renamed\"));\r\n    assert(store.save(loaded, commit));\r\n    assert(readFile(paths.layoutBackup) == originalLayout);\r\n    assert(readFile(paths.activeBackup) == originalActive);\r\n\r\n    // Corrupt the current primary layout: recover the previous valid copy.\r\n    writeFile(paths.layout, \"not a DeskNX layout\\n\");\r\n    WorkspaceProvider recovered;\r\n    WorkspaceLoadInfo recoveredInfo{};\r\n    assert(store.load(recovered, &recoveredInfo));\r\n    assert(recoveredInfo.restoredLayoutBackup);\r\n    assert(recovered.count() == 2);\r\n    assert(recovered.workspaces()[0].name == \"Games\");\r\n\r\n    // An oversized primary is treated as corrupt and recovered from the\r\n    // valid backup. Without a backup, load fails without replacing live state.\r\n    writeFile(paths.layout, std::string(1024 * 1024 + 1, 'x'));\r\n    WorkspaceProvider oversizedRecovery;\r\n    WorkspaceLoadInfo oversizedInfo{};\r\n    assert(store.load(oversizedRecovery, &oversizedInfo));\r\n    assert(oversizedInfo.restoredLayoutBackup);\r\n    assert(oversizedRecovery.workspaces()[0].name == \"Games\");\r\n\r\n    std::remove(paths.layoutBackup.c_str());\r\n    WorkspaceProvider untouched;\r\n    assert(!store.load(untouched));\r\n    assert(untouched.count() == 1);\r\n    assert(untouched.activeWorkspace()->name == \"Home\");\r\n\r\n    std::remove(paths.layout.c_str());\r\n    std::remove(paths.layoutBackup.c_str());\r\n    std::remove(paths.active.c_str());\r\n    std::remove(paths.activeBackup.c_str());\r\n    std::remove(paths.layoutTemp.c_str());\r\n    std::remove(paths.activeTemp.c_str());\r\n    rmdir(root.c_str());\r\n    return 0;\r\n}\r\n"}