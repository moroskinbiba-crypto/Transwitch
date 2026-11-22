#include <desknx/WorkspaceStorage.hpp>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output.is_open());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    assert(output.good());
}

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

struct CommitPlan {
    int calls{0};
    int failAt{0};
    std::vector<std::string> phases;
};

bool commit(void* opaque, const char* phase) {
    auto* plan = static_cast<CommitPlan*>(opaque);
    if (!plan)
        return false;
    ++plan->calls;
    plan->phases.emplace_back(phase ? phase : "");
    return plan->failAt == 0 || plan->calls != plan->failAt;
}

struct WritePlan {
    int calls{0};
    int failAt{0};
};

desknx::persist::WriteStatus injectedWriter(
    void* opaque, const char* path, const std::string& bytes) {
    auto* plan = static_cast<WritePlan*>(opaque);
    if (!plan)
        return desknx::persist::WriteStatus::InvalidArgument;
    ++plan->calls;
    if (plan->calls == plan->failAt)
        return desknx::persist::WriteStatus::WriteFailed;
    return desknx::persist::writeTemporaryFile(nullptr, path, bytes);
}

} // namespace

int main() {
    using namespace desknx;
    using namespace desknx::model;

    char rootTemplate[] = "/tmp/desknx-workspace-storage-XXXXXX";
    char* rootValue = mkdtemp(rootTemplate);
    assert(rootValue != nullptr);
    const std::string root(rootValue);
    const workspace_storage::Paths paths = workspace_storage::Paths::under(root);

    std::vector<Workspace> workspaces{
        Workspace{"Games", {
            Tile{Kind::Game, "Game", "0100000000001000", 0, 0, 2, 2, 0}
        }},
        Workspace{"Homebrew", {
            Tile{Kind::Nro, "Tool", "sdmc:/switch/apps/tool.nro", 1, 1, 2, 1, 1}
        }}
    };

    // A complete generation has exactly two durability boundaries: temporary
    // pair committed before publication, then the published pair committed.
    CommitPlan firstCommit{};
    auto written = workspace_storage::writeSnapshot(
        paths, workspaces, 1, commit, &firstCommit, "layout-temp", "layout-publish");
    assert(written.success());
    assert(written.activeMarkerPublished && written.layoutPublished);
    assert(firstCommit.calls == 2);
    assert(firstCommit.phases[0] == "layout-temp");
    assert(firstCommit.phases[1] == "layout-publish");

    workspace_storage::Snapshot loaded{};
    assert(workspace_storage::loadSnapshot(paths, loaded));
    assert(loaded.layoutSource == persist::FileSource::Primary);
    assert(loaded.activeSource == persist::FileSource::Primary);
    assert(loaded.parsed.workspaces.size() == 2);
    assert(loaded.parsed.activeWorkspace == 1);
    assert(loaded.parsed.workspaces[1].name == "Homebrew");

    // A failed second temporary write must not publish either half of the new
    // generation and must remove the first temporary file.
    const std::string oldLayout = readFile(paths.layout);
    const std::string oldMarker = readFile(paths.active);
    auto edited = workspaces;
    edited[0].name = "Games edited";
    WritePlan secondWriteFailure{0, 2};
    CommitPlan untouchedCommit{};
    written = workspace_storage::writeSnapshot(
        paths, edited, 0, commit, &untouchedCommit,
        "layout-temp", "layout-publish", nullptr, injectedWriter,
        &secondWriteFailure);
    assert(written.status == persist::WriteStatus::WriteFailed);
    assert(!written.activeMarkerPublished && !written.layoutPublished);
    assert(untouchedCommit.calls == 0);
    assert(readFile(paths.layout) == oldLayout);
    assert(readFile(paths.active) == oldMarker);
    assert(!std::filesystem::exists(paths.layoutTemp));
    assert(!std::filesystem::exists(paths.activeTemp));

    // Failure at the pre-publish SD commit also leaves both primaries intact.
    CommitPlan tempCommitFailure{0, 1, {}};
    written = workspace_storage::writeSnapshot(
        paths, edited, 0, commit, &tempCommitFailure,
        "layout-temp", "layout-publish");
    assert(written.status == persist::WriteStatus::TempCommitFailed);
    assert(readFile(paths.layout) == oldLayout);
    assert(readFile(paths.active) == oldMarker);

    // A second generation rotates both files to matching backups.
    CommitPlan secondCommit{};
    written = workspace_storage::writeSnapshot(
        paths, edited, 0, commit, &secondCommit,
        "layout-temp", "layout-publish");
    assert(written.success());
    assert(readFile(paths.layoutBackup) == oldLayout);
    assert(readFile(paths.activeBackup) == oldMarker);

    // Simulate a truncated primary layout after the new generation was saved.
    // The newest sidecar is bound to the new shape, so it is rejected against
    // the restored old layout; the previous sidecar matches and is recovered.
    writeFile(paths.layout, "DESKNX4\ntruncated\n");
    workspace_storage::Snapshot recovered{};
    assert(workspace_storage::loadSnapshot(paths, recovered));
    assert(recovered.layoutSource == persist::FileSource::Backup);
    assert(recovered.activeSource == persist::FileSource::Backup);
    assert(recovered.parsed.workspaces[0].name == "Games");
    assert(recovered.parsed.activeWorkspace == 1);
    assert(recovered.parsed.workspaces[1].name == "Homebrew");

    // Active-desk changes are marker-only writes: the full tile layout remains
    // byte-identical while the new selection is restored on the next load.
    const std::string layoutBeforeMarker = readFile(paths.layoutBackup);
    CommitPlan markerCommit{};
    const auto markerStatus = workspace_storage::writeActiveWorkspace(
        paths, recovered.parsed.workspaces, 0, commit, &markerCommit,
        "active-temp", "active-publish", true);
    assert(markerStatus == persist::WriteStatus::Success);
    assert(markerCommit.calls == 2);
    assert(readFile(paths.layoutBackup) == layoutBeforeMarker);

    // The marker references the selected layout generation; when loading the
    // recovered primary layout, the independent marker is validated against it.
    const auto markerSource = workspace_storage::readActiveWorkspace(
        paths, recovered.parsed.workspaces, recovered.parsed.activeWorkspace);
    assert(markerSource == persist::FileSource::Primary);
    assert(recovered.parsed.activeWorkspace == 0);

    // The production background marker-only path already owns a compact,
    // pre-serialized marker and must not need a copy of the whole workspace.
    const std::string compactMarker =
        layout::serializeActiveWorkspaceState(recovered.parsed.workspaces, 1);
    CommitPlan compactCommit{};
    const auto compactStatus = workspace_storage::writeActiveWorkspaceBytes(
        paths, compactMarker, commit, &compactCommit,
        "marker-temp", "marker-publish", false);
    assert(compactStatus == persist::WriteStatus::Success);
    assert(compactCommit.calls == 1);
    int compactActive = 0;
    assert(workspace_storage::readActiveWorkspace(
        paths, recovered.parsed.workspaces, compactActive) ==
        persist::FileSource::Primary);
    assert(compactActive == 1);

    std::filesystem::remove_all(root);
    return 0;
}
