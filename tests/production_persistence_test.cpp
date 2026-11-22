#include <desknx/ProductionPersistence.hpp>
#include <desknx/SettingsFile.hpp>
#include <desknx/LaunchHistory.hpp>
#include <desknx/Layout.hpp>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    assert(out.good());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    assert(out.good());
}

std::string readFile(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

struct CommitPlan {
    int calls{0};
    int failAt{0};
    std::vector<std::string> phases;
};

bool commitStub(void* opaque, const char* phase) {
    auto* plan = static_cast<CommitPlan*>(opaque);
    ++plan->calls;
    plan->phases.emplace_back(phase ? phase : "");
    return plan->failAt == 0 || plan->calls != plan->failAt;
}

desknx::persist::WriteStatus injectedWriteFailure(
    void*, const char*, const std::string&) {
    return desknx::persist::WriteStatus::WriteFailed;
}

struct RenamePlan {
    int calls{0};
    int failFirst{0};
    int failSecond{0};
};

int injectedRename(void* opaque, const char* from, const char* to) {
    auto* plan = static_cast<RenamePlan*>(opaque);
    ++plan->calls;
    if (plan->calls == plan->failFirst || plan->calls == plan->failSecond) {
        errno = EIO;
        return -1;
    }
    return std::rename(from, to);
}

int injectedStat(void*, const char* path, struct stat* result) {
    return ::stat(path, result);
}
int injectedRemove(void*, const char* path) {
    return std::remove(path);
}

bool validateSettings(const std::string& contents, void*) {
    std::vector<desknx::settings_file::Entry> entries;
    return desknx::settings_file::parse(contents, entries);
}

bool validateLayout(const std::string& contents, void* context) {
    auto* parsed = static_cast<desknx::layout::LoadResult*>(context);
    return parsed && desknx::layout::parse(contents, *parsed);
}

bool validateHistory(const std::string& contents, void* context) {
    auto* parsed = static_cast<std::vector<desknx::launch_history::Entry>*>(context);
    return parsed && desknx::launch_history::parse(contents, *parsed);
}

} // namespace

int main() {
    char directoryTemplate[] = "/tmp/desknx-production-persistence-XXXXXX";
    char* directory = mkdtemp(directoryTemplate);
    assert(directory);
    const std::string root(directory);
    const std::string primary = root + "/state.dat";
    const std::string backup = root + "/state.bak";
    const std::string temporary = root + "/state.tmp";

    // The exact production settings parser supports both deployed versions.
    const std::string settingsV1 =
        "DESKNXSET1\ntheme=8\nlanguage=ru\neffects=1\n";
    std::vector<desknx::settings_file::Entry> settings;
    assert(desknx::settings_file::parse(settingsV1, settings));
    assert(desknx::settings_file::find(settings, "theme") &&
           *desknx::settings_file::find(settings, "theme") == "8");
    const std::string settingsV2 =
        "DESKNXSET2\ntheme=1\nmotion=Full\ntheme=0\n";
    assert(desknx::settings_file::parse(settingsV2, settings));
    assert(desknx::settings_file::find(settings, "theme") &&
           *desknx::settings_file::find(settings, "theme") == "0");
    assert(!desknx::settings_file::parse("CORRUPT\ntheme=1\n", settings));
    assert(!desknx::settings_file::parse(
        std::string("DESKNXSET2\nx=1\n") + '\0', settings));
    assert(!desknx::settings_file::parse(
        std::string(desknx::settings_file::MaxBytes + 1, 'X'), settings));

    // The exact production history parser/serializer preserves DESKNXH1.
    using HistoryEntry = desknx::launch_history::Entry;
    std::vector<HistoryEntry> history{
        {desknx::model::Kind::Game, "Zelda", "0100000000000001"},
        {desknx::model::Kind::Nro, "Retro", "sdmc:/switch/emulator.NRO"}
    };
    const std::string historyBytes = desknx::launch_history::serialize(history);
    assert(historyBytes.find("DESKNXH1\n") == 0);
    std::vector<HistoryEntry> parsedHistory;
    assert(desknx::launch_history::parse(historyBytes, parsedHistory));
    assert(parsedHistory.size() == 2);
    assert(parsedHistory[0].data == "0100000000000001");
    assert(desknx::launch_history::parse(
        "DESKNXH1\nG\tBad\tnot-a-title-id\n", parsedHistory));
    assert(parsedHistory.empty());
    assert(!desknx::launch_history::serialize({
        {desknx::model::Kind::Game, "Bad", "not-a-title-id"}}));

    // Layout parser and active-marker parser are the production implementations.
    using namespace desknx::model;
    std::vector<Workspace> workspaces{
        Workspace{"Games", {Tile{Kind::Game, "Test", "0100000000000001", 0, 0, 1, 1, 0}}},
        Workspace{"Homebrew", {}}
    };
    const std::string layoutBytes = desknx::layout::serialize(workspaces, 1);
    assert(!layoutBytes.empty());
    desknx::layout::LoadResult layoutResult{};
    assert(desknx::layout::parse(layoutBytes, layoutResult));
    assert(layoutResult.workspaces.size() == 2);
    assert(layoutResult.activeWorkspace == 1);
    const std::string activeMarker =
        desknx::layout::serializeActiveWorkspaceState(workspaces, 1);
    assert(!activeMarker.empty());
    int active = 0;
    assert(desknx::layout::parseActiveWorkspaceState(activeMarker, workspaces, active));
    assert(active == 1);
    assert(!desknx::layout::parseActiveWorkspaceState(
        activeMarker, std::vector<Workspace>{Workspace{"Wrong", {}}}, active));

    // Primary/backup selection uses the same bounded reader and validators as
    // production settings, history and active-workspace restore.
    writeFile(primary, "CORRUPT SETTINGS\n");
    writeFile(backup, settingsV2);
    std::string selected = "unchanged";
    auto source = desknx::persist::readValidatedWithBackup(
        primary.c_str(), backup.c_str(), desknx::settings_file::MaxBytes,
        validateSettings, nullptr, selected);
    assert(source == desknx::persist::FileSource::Backup);
    assert(selected == settingsV2);
    writeFile(primary, settingsV1);
    source = desknx::persist::readValidatedWithBackup(
        primary.c_str(), backup.c_str(), desknx::settings_file::MaxBytes,
        validateSettings, nullptr, selected);
    assert(source == desknx::persist::FileSource::Primary);
    assert(selected == settingsV1);

    writeFile(primary, "BROKEN LAYOUT\n");
    writeFile(backup, layoutBytes);
    layoutResult = {};
    source = desknx::persist::readValidatedWithBackup(
        primary.c_str(), backup.c_str(), 1024u * 1024u,
        validateLayout, &layoutResult, selected);
    assert(source == desknx::persist::FileSource::Backup);
    assert(layoutResult.activeWorkspace == 1);

    writeFile(primary, "BROKEN HISTORY\n");
    writeFile(backup, historyBytes);
    parsedHistory.clear();
    source = desknx::persist::readValidatedWithBackup(
        primary.c_str(), backup.c_str(), desknx::launch_history::MaxFileBytes,
        validateHistory, &parsedHistory, selected);
    assert(source == desknx::persist::FileSource::Backup);
    assert(parsedHistory.size() == 2);

    writeFile(primary, "BAD PRIMARY");
    writeFile(backup, "BAD BACKUP");
    selected = "preserve-me";
    source = desknx::persist::readValidatedWithBackup(
        primary.c_str(), backup.c_str(), 4096, validateSettings, nullptr, selected);
    assert(source == desknx::persist::FileSource::None);
    assert(selected == "preserve-me");

    // Open and short-write failures must not call fsdevCommitDevice or mutate
    // the current primary.
    writeFile(primary, "old-state");
    CommitPlan noCommit{};
    const std::string missingTemp = root + "/missing/state.tmp";
    auto writeStatus = desknx::persist::writeAtomically(
        missingTemp.c_str(), primary.c_str(), backup.c_str(), "new-state",
        commitStub, &noCommit, "temp", "publish");
    assert(writeStatus == desknx::persist::WriteStatus::OpenFailed);
    assert(noCommit.calls == 0);
    assert(readFile(primary) == "old-state");

    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "new-state",
        commitStub, &noCommit, "temp", "publish", true, true, nullptr,
        injectedWriteFailure, nullptr);
    assert(writeStatus == desknx::persist::WriteStatus::WriteFailed);
    assert(noCommit.calls == 0);
    assert(readFile(primary) == "old-state");
    assert(!exists(temporary));

    // Failure at the first SD durability boundary leaves the old primary intact.
    CommitPlan tempCommitFailure{0, 1, {}};
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "new-state",
        commitStub, &tempCommitFailure, "settings-temp", "settings-publish");
    assert(writeStatus == desknx::persist::WriteStatus::TempCommitFailed);
    assert(readFile(primary) == "old-state");
    assert(!exists(temporary));
    assert(tempCommitFailure.calls == 1);
    assert(tempCommitFailure.phases[0] == "settings-temp");

    // Fail the first rename (destination -> backup): neither source nor backup
    // is lost and the temp is removed by the transaction wrapper.
    RenamePlan firstRenameFailure{0, 1, 0};
    const desknx::safeio::ReplaceOperations firstRenameOps{
        &firstRenameFailure, injectedStat, injectedRemove, injectedRename
    };
    CommitPlan commits{};
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "new-state",
        commitStub, &commits, "layout-temp", "layout-publish", true, true,
        &firstRenameOps);
    assert(writeStatus == desknx::persist::WriteStatus::ReplaceFailed);
    assert(readFile(primary) == "old-state");
    assert(!exists(temporary));

    // Fail publishing the temp. SafeReplace must roll the previous primary back.
    RenamePlan publishRenameFailure{0, 2, 0};
    const desknx::safeio::ReplaceOperations publishOps{
        &publishRenameFailure, injectedStat, injectedRemove, injectedRename
    };
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "new-state",
        commitStub, &commits, "layout-temp", "layout-publish", true, true,
        &publishOps);
    assert(writeStatus == desknx::persist::WriteStatus::ReplaceFailed);
    assert(readFile(primary) == "old-state");
    assert(!exists(backup));
    assert(!exists(temporary));

    // If rollback itself fails, the previous bytes remain in .bak rather than
    // being deleted. The next normal transaction can recover from that .bak.
    RenamePlan rollbackFailure{0, 2, 3};
    const desknx::safeio::ReplaceOperations rollbackOps{
        &rollbackFailure, injectedStat, injectedRemove, injectedRename
    };
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "new-state",
        commitStub, &commits, "layout-temp", "layout-publish", true, true,
        &rollbackOps);
    assert(writeStatus == desknx::persist::WriteStatus::RollbackFailed);
    assert(!exists(primary));
    assert(readFile(backup) == "old-state");
    assert(!exists(temporary));

    CommitPlan recoverCommit{};
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "recovered-state",
        commitStub, &recoverCommit, "layout-temp", "layout-publish");
    assert(writeStatus == desknx::persist::WriteStatus::Success);
    assert(readFile(primary) == "recovered-state");
    assert(readFile(backup) == "old-state");

    // A failed final SD commit leaves the published new primary and the old
    // committed state in the backup for the next boot.
    writeFile(primary, "old-state");
    std::remove(backup.c_str());
    CommitPlan publishCommitFailure{0, 2, {}};
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "published-new-state",
        commitStub, &publishCommitFailure, "history-temp", "history-publish");
    assert(writeStatus == desknx::persist::WriteStatus::PublishCommitFailed);
    assert(readFile(primary) == "published-new-state");
    assert(readFile(backup) == "old-state");
    assert(!exists(temporary));
    assert(publishCommitFailure.calls == 2);

    // A subsequent successful write rotates the last published primary.
    CommitPlan success{};
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "latest-state",
        commitStub, &success, "layout-temp", "layout-publish");
    assert(writeStatus == desknx::persist::WriteStatus::Success);
    assert(readFile(primary) == "latest-state");
    assert(readFile(backup) == "published-new-state");
    assert(success.calls == 2);

    // Active marker policy intentionally uses one post-publication commit.
    CommitPlan markerCommitFailure{0, 1, {}};
    writeStatus = desknx::persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(), "new-marker",
        commitStub, &markerCommitFailure, "unused-temp", "marker-publish",
        false, true);
    assert(writeStatus == desknx::persist::WriteStatus::PublishCommitFailed);
    assert(markerCommitFailure.calls == 1);
    assert(markerCommitFailure.phases[0] == "marker-publish");
    assert(readFile(primary) == "new-marker");
    assert(readFile(backup) == "latest-state");

    std::remove(primary.c_str());
    std::remove(backup.c_str());
    std::remove(temporary.c_str());
    rmdir(root.c_str());
    return 0;
}
