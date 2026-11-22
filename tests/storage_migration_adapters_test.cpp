#include <desknx/StorageMigrations.hpp>
#include <desknx/ProductionPersistence.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

bool commit(void* opaque, const char*) {
    auto* calls = static_cast<int*>(opaque);
    ++*calls;
    return true;
}

void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.is_open());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    assert(out.good());
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    using namespace desknx;
    using namespace desknx::migration;

    // Production layout v3 -> v4 is a direct, semantic migration. The
    // canonical parser preserves supported tile data and the serializer
    // validates the new schema before any file is touched.
    const std::string legacyLayout =
        "DESKNX3\nA\t0\nD\tLegacy\n";
    std::string migratedLayout = "unchanged";
    auto result = run(legacyLayout, WorkspaceSchema,
                      WorkspaceSteps, WorkspaceStepCount,
                      true, migratedLayout, validateWorkspaceV4);
    assert(result.status == RunStatus::DryRunReady);
    assert(result.stepsApplied == 1);
    assert(migratedLayout == "unchanged");

    result = run(legacyLayout, WorkspaceSchema,
                 WorkspaceSteps, WorkspaceStepCount,
                 false, migratedLayout, validateWorkspaceV4);
    assert(result.status == RunStatus::AppliedInMemory);
    assert(result.stepsApplied == 1);
    assert(migratedLayout.rfind("DESKNX4\n", 0) == 0);
    assert(validateWorkspaceV4(migratedLayout, nullptr));

    // Settings v1 -> v2 changes only the schema header and preserves unknown
    // key/value rows, repeated keys and their order.
    const std::string legacySettings =
        "DESKNXSET1\ntheme=1\nfuture_option=preserve\ntheme=0\n";
    std::string migratedSettings;
    result = run(legacySettings, SettingsSchema,
                 SettingsSteps, SettingsStepCount,
                 true, migratedSettings, validateSettingsV2);
    assert(result.status == RunStatus::DryRunReady);
    assert(migratedSettings.empty());

    result = run(legacySettings, SettingsSchema,
                 SettingsSteps, SettingsStepCount,
                 false, migratedSettings, validateSettingsV2);
    assert(result.status == RunStatus::AppliedInMemory);
    assert(migratedSettings.rfind("DESKNXSET2\n", 0) == 0);
    assert(migratedSettings.find("future_option=preserve") != std::string::npos);
    assert(migratedSettings.find("theme=1") < migratedSettings.find("theme=0"));
    assert(validateSettingsV2(migratedSettings, nullptr));

    // The migrated bytes are not published by the migration engine itself.
    // The caller opts into publication through the shared atomic persistence
    // contract, which keeps the complete pre-migration file as .bak.
    char directoryTemplate[] = "/tmp/desknx-schema-migration-XXXXXX";
    char* directory = mkdtemp(directoryTemplate);
    assert(directory != nullptr);
    const std::string root(directory);
    const std::string primary = root + "/settings.ini";
    const std::string temporary = root + "/settings.tmp";
    const std::string backup = root + "/settings.bak";
    writeFile(primary, legacySettings);

    int commits = 0;
    const auto writeStatus = persist::writeAtomically(
        temporary.c_str(), primary.c_str(), backup.c_str(),
        migratedSettings, commit, &commits, "migration-temp", "migration-publish",
        true, true);
    assert(writeStatus == persist::WriteStatus::Success);
    assert(commits == 2);
    assert(readFile(primary) == migratedSettings);
    assert(readFile(backup) == legacySettings);

    // A failed migration leaves the caller output unchanged; unsupported
    // future versions are rejected instead of being downgraded.
    std::string untouched = "keep";
    const auto future = run(
        "DESKNXSET3\nfuture=true\n", SettingsSchema,
        SettingsSteps, SettingsStepCount, false, untouched, validateSettingsV2);
    assert(future.status == RunStatus::UnsupportedFuture);
    assert(untouched == "keep");

    std::remove(primary.c_str());
    std::remove(temporary.c_str());
    std::remove(backup.c_str());
    rmdir(root.c_str());
    return 0;
}
