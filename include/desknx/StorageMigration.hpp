#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace desknx::migration {

enum class InspectionStatus : std::uint8_t {
    Invalid,
    Current,
    NeedsMigration,
    UnsupportedLegacy,
    UnsupportedFuture
};

enum class RunStatus : std::uint8_t {
    DryRunReady,
    Current,
    AppliedInMemory,
    InvalidInput,
    UnsupportedLegacy,
    UnsupportedFuture,
    MissingStep,
    InvalidPlan,
    TransformFailed,
    ValidationFailed
};

struct SchemaDescriptor {
    const char* name{nullptr};
    const char* headerPrefix{nullptr};
    std::uint32_t minimumReadableVersion{1};
    std::uint32_t currentVersion{1};
    std::size_t maximumBytes{0};
};

struct Inspection {
    InspectionStatus status{InspectionStatus::Invalid};
    std::uint32_t sourceVersion{0};
    std::uint32_t targetVersion{0};
};

using Transform = bool (*)(const std::string& input,
                           std::string& output, void* context);
using Validator = bool (*)(const std::string& contents, void* context);

struct Step {
    std::uint32_t fromVersion{0};
    std::uint32_t toVersion{0};
    Transform transform{nullptr};
    Validator validateOutput{nullptr};
};

struct RunResult {
    RunStatus status{RunStatus::InvalidInput};
    std::uint32_t sourceVersion{0};
    std::uint32_t targetVersion{0};
    std::uint32_t stepsApplied{0};
};

// Known production file families. Their parsers remain the authority for
// validating the full file; this registry only identifies schema generations.
inline constexpr SchemaDescriptor WorkspaceSchema{
    "workspace", "DESKNX", 1, 4, 1024u * 1024u
};
inline constexpr SchemaDescriptor SettingsSchema{
    "launcher-settings", "DESKNXSET", 1, 2, 8192u
};
inline constexpr SchemaDescriptor LaunchHistorySchema{
    "launch-history", "DESKNXH", 1, 1, 64u * 1024u
};
inline constexpr SchemaDescriptor ActiveWorkspaceSchema{
    "active-workspace", "DESKNX-ACTIVE", 1, 1, 512u
};

inline bool parseHeaderVersion(const std::string& contents,
                               const SchemaDescriptor& schema,
                               std::uint32_t& version) noexcept {
    if (!schema.headerPrefix || !schema.headerPrefix[0] ||
        !schema.maximumBytes || contents.empty() ||
        contents.size() > schema.maximumBytes ||
        contents.find('\0') != std::string::npos)
        return false;

    const std::size_t newline = contents.find('\n');
    if (newline == std::string::npos || newline == 0 || newline > 64)
        return false;

    const std::string header = contents.substr(0, newline);
    const std::string prefix(schema.headerPrefix);
    if (header.compare(0, prefix.size(), prefix) != 0 ||
        header.size() <= prefix.size())
        return false;

    std::uint32_t parsed = 0;
    for (std::size_t i = prefix.size(); i < header.size(); ++i) {
        const char ch = header[i];
        if (ch < '0' || ch > '9')
            return false;
        const std::uint32_t digit = static_cast<std::uint32_t>(ch - '0');
        if (parsed > (UINT32_MAX - digit) / 10u)
            return false;
        parsed = parsed * 10u + digit;
    }
    if (parsed == 0)
        return false;
    version = parsed;
    return true;
}

inline Inspection inspect(const std::string& contents,
                         const SchemaDescriptor& schema) noexcept {
    Inspection result{};
    result.targetVersion = schema.currentVersion;
    std::uint32_t version = 0;
    if (schema.minimumReadableVersion == 0 ||
        schema.currentVersion < schema.minimumReadableVersion ||
        !parseHeaderVersion(contents, schema, version)) {
        result.status = InspectionStatus::Invalid;
        return result;
    }

    result.sourceVersion = version;
    if (version < schema.minimumReadableVersion) {
        result.status = InspectionStatus::UnsupportedLegacy;
    } else if (version > schema.currentVersion) {
        result.status = InspectionStatus::UnsupportedFuture;
    } else if (version == schema.currentVersion) {
        result.status = InspectionStatus::Current;
    } else {
        result.status = InspectionStatus::NeedsMigration;
    }
    return result;
}

inline RunResult run(const std::string& input, const SchemaDescriptor& schema,
                     const Step* steps, std::size_t stepCount,
                     bool dryRun, std::string& migratedOutput,
                     Validator validateCurrent = nullptr,
                     void* context = nullptr) {
    const Inspection inspection = inspect(input, schema);
    RunResult result{};
    result.sourceVersion = inspection.sourceVersion;
    result.targetVersion = inspection.targetVersion;

    switch (inspection.status) {
        case InspectionStatus::Invalid:
            result.status = RunStatus::InvalidInput;
            return result;
        case InspectionStatus::Current:
            if (validateCurrent && !validateCurrent(input, context)) {
                result.status = RunStatus::ValidationFailed;
                return result;
            }
            migratedOutput = input;
            result.status = RunStatus::Current;
            return result;
        case InspectionStatus::UnsupportedLegacy:
            result.status = RunStatus::UnsupportedLegacy;
            return result;
        case InspectionStatus::UnsupportedFuture:
            result.status = RunStatus::UnsupportedFuture;
            return result;
        case InspectionStatus::NeedsMigration:
            break;
    }

    std::uint32_t version = inspection.sourceVersion;
    std::uint32_t plannedSteps = 0;
    while (version < schema.currentVersion) {
        const Step* selected = nullptr;
        for (std::size_t i = 0; i < stepCount; ++i) {
            if (steps && steps[i].fromVersion == version) {
                if (selected) {
                    result.status = RunStatus::InvalidPlan;
                    return result;
                }
                selected = &steps[i];
            }
        }
        if (!selected) {
            result.status = RunStatus::MissingStep;
            return result;
        }
        if (selected->toVersion != version + 1 ||
            !selected->transform || !selected->validateOutput) {
            result.status = RunStatus::InvalidPlan;
            return result;
        }
        ++plannedSteps;
        version = selected->toVersion;
    }

    if (version != schema.currentVersion) {
        result.status = RunStatus::InvalidPlan;
        return result;
    }
    if (dryRun) {
        result.status = RunStatus::DryRunReady;
        result.stepsApplied = plannedSteps;
        return result;
    }

    std::string current = input;
    version = inspection.sourceVersion;
    std::uint32_t applied = 0;
    while (version < schema.currentVersion) {
        const Step* selected = nullptr;
        for (std::size_t i = 0; i < stepCount; ++i) {
            if (steps[i].fromVersion == version) {
                selected = &steps[i];
                break;
            }
        }

        std::string transformed;
        if (!selected->transform(current, transformed, context) ||
            transformed.empty() || transformed.size() > schema.maximumBytes ||
            transformed.find('\0') != std::string::npos) {
            result.status = RunStatus::TransformFailed;
            return result;
        }
        std::uint32_t outputVersion = 0;
        if (!parseHeaderVersion(transformed, schema, outputVersion) ||
            outputVersion != selected->toVersion ||
            !selected->validateOutput(transformed, context)) {
            result.status = RunStatus::ValidationFailed;
            return result;
        }
        current.swap(transformed);
        version = selected->toVersion;
        ++applied;
    }

    if (validateCurrent && !validateCurrent(current, context)) {
        result.status = RunStatus::ValidationFailed;
        return result;
    }

    // The caller publishes this buffer with ProductionPersistence::writeAtomically
    // so the pre-migration source remains in the normal .bak recovery slot.
    migratedOutput.swap(current);
    result.status = RunStatus::AppliedInMemory;
    result.stepsApplied = applied;
    return result;
}

} // namespace desknx::migration
