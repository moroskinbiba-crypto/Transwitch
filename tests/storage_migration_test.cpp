#include <desknx/StorageMigration.hpp>

#include <cassert>
#include <string>

namespace {

bool migrate1to2(const std::string& input, std::string& output, void*) {
    if (input != "TEST1\nvalue=old\n")
        return false;
    output = "TEST2\nvalue=new\n";
    return true;
}

bool migrate2to3(const std::string& input, std::string& output, void*) {
    if (input != "TEST2\nvalue=new\n")
        return false;
    output = "TEST3\nvalue=final\n";
    return true;
}

bool validTest(const std::string& contents, void*) {
    return contents.find("value=") != std::string::npos;
}

bool invalidTransform(const std::string&, std::string& output, void*) {
    output = "TEST9\nbad\n";
    return true;
}

bool rejectOutput(const std::string&, void*) {
    return false;
}

} // namespace

int main() {
    using namespace desknx::migration;

    const SchemaDescriptor testSchema{"test", "TEST", 1, 3, 128};
    const Step steps[] = {
        {1, 2, migrate1to2, validTest},
        {2, 3, migrate2to3, validTest}
    };

    const std::string legacy = "TEST1\nvalue=old\n";
    const auto inspection = inspect(legacy, testSchema);
    assert(inspection.status == InspectionStatus::NeedsMigration);
    assert(inspection.sourceVersion == 1);
    assert(inspection.targetVersion == 3);

    // Dry-run validates that a complete chain exists but never invokes a
    // transformer or changes the caller's buffer.
    std::string migrated = "do-not-touch";
    const auto dryRun = run(
        legacy, testSchema, steps, 2, true, migrated, validTest);
    assert(dryRun.status == RunStatus::DryRunReady);
    assert(dryRun.stepsApplied == 2);
    assert(migrated == "do-not-touch"); // dry-run never mutates caller output

    const auto applied = run(
        legacy, testSchema, steps, 2, false, migrated, validTest);
    assert(applied.status == RunStatus::AppliedInMemory);
    assert(applied.stepsApplied == 2);
    assert(migrated == "TEST3\nvalue=final\n");

    const auto current = run(
        migrated, testSchema, steps, 2, false, migrated, validTest);
    assert(current.status == RunStatus::Current);
    assert(migrated == "TEST3\nvalue=final\n");

    const auto future = inspect("TEST4\nvalue=future\n", testSchema);
    assert(future.status == InspectionStatus::UnsupportedFuture);
    assert(run("TEST4\nvalue=future\n", testSchema, steps, 2, false, migrated).status ==
           RunStatus::UnsupportedFuture);

    const SchemaDescriptor newerMinimum{"test", "TEST", 2, 3, 128};
    assert(inspect(legacy, newerMinimum).status ==
           InspectionStatus::UnsupportedLegacy);
    assert(inspect("UNKNOWN1\nvalue=x\n", testSchema).status ==
           InspectionStatus::Invalid);
    assert(inspect("TEST0\nvalue=x\n", testSchema).status ==
           InspectionStatus::Invalid);
    assert(inspect(std::string(129, 'x'), testSchema).status ==
           InspectionStatus::Invalid);
    assert(inspect(std::string("TEST1\nvalue=x\n") + '\0', testSchema).status ==
           InspectionStatus::Invalid);

    assert(run(legacy, testSchema, nullptr, 0, true, migrated).status ==
           RunStatus::MissingStep);
    const Step brokenPlan[] = {{1, 1, migrate1to2, validTest}};
    assert(run(legacy, testSchema, brokenPlan, 1, true, migrated).status ==
           RunStatus::InvalidPlan);
    const std::string preservedOutput = migrated;
    const Step badOutput[] = {{1, 2, invalidTransform, validTest},
                              {2, 3, migrate2to3, validTest}};
    assert(run(legacy, testSchema, badOutput, 2, false, migrated).status ==
           RunStatus::ValidationFailed);
    assert(migrated == preservedOutput);
    const Step badValidator[] = {{1, 2, migrate1to2, rejectOutput},
                                 {2, 3, migrate2to3, validTest}};
    assert(run(legacy, testSchema, badValidator, 2, false, migrated).status ==
           RunStatus::ValidationFailed);
    assert(migrated == preservedOutput);
    assert(run(legacy, testSchema, steps, 2, false, migrated, rejectOutput).status ==
           RunStatus::ValidationFailed);
    assert(migrated == preservedOutput);

    // The registry covers the real production schema families without
    // implementing unreviewed transformations.
    assert(inspect("DESKNX1\n", WorkspaceSchema).status ==
           InspectionStatus::NeedsMigration);
    assert(inspect("DESKNX4\n", WorkspaceSchema).status ==
           InspectionStatus::Current);
    assert(inspect("DESKNXSET1\n", SettingsSchema).status ==
           InspectionStatus::NeedsMigration);
    assert(inspect("DESKNXSET2\n", SettingsSchema).status ==
           InspectionStatus::Current);
    assert(inspect("DESKNXH1\n", LaunchHistorySchema).status ==
           InspectionStatus::Current);
    assert(inspect("DESKNX-ACTIVE1\n", ActiveWorkspaceSchema).status ==
           InspectionStatus::Current);
    assert(inspect("DESKNXSET3\n", SettingsSchema).status ==
           InspectionStatus::UnsupportedFuture);

    return 0;
}
