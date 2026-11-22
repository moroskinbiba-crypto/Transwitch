#pragma once

#include "Layout.hpp"
#include "SettingsFile.hpp"
#include "StorageMigration.hpp"

#include <string>
#include <cstdlib>

namespace desknx::migration {

inline bool migrateWorkspaceToV4(const std::string& input,
                                 std::string& output, void*) {
    layout::LoadResult parsed{};
    if (!layout::parse(input, parsed) || !parsed.needsMigration)
        return false;
    output = layout::serialize(parsed.workspaces, parsed.activeWorkspace);
    return !output.empty();
}

inline bool validateWorkspaceV4(const std::string& contents, void*) {
    layout::LoadResult parsed{};
    return layout::parse(contents, parsed) && !parsed.needsMigration;
}

inline bool migrateSettingsV1ToV2(const std::string& input,
                                  std::string& output, void*) {
    std::vector<settings_file::Entry> parsed;
    if (!settings_file::parse(input, parsed))
        return false;
    const std::size_t newline = input.find('\n');
    if (newline == std::string::npos)
        return false;

    output = "DESKNXSET2\n";
    std::size_t position = newline + 1;
    while (position < input.size()) {
        const std::size_t lineEnd = input.find('\n', position);
        const std::size_t end = lineEnd == std::string::npos
            ? input.size() : lineEnd;
        const std::string line = input.substr(position, end - position);
        if (line.compare(0, 6, "theme=") == 0) {
            char* valueEnd = nullptr;
            const long legacyTheme = std::strtol(line.c_str() + 6, &valueEnd, 10);
            if (valueEnd && *valueEnd == '\0' &&
                legacyTheme >= 0 && legacyTheme <= 8) {
                output += "theme=";
                output += legacyTheme == 8 ? "1" : "0";
            } else {
                output += line;
            }
        } else {
            output += line;
        }
        output.push_back('\n');
        position = lineEnd == std::string::npos ? input.size() : lineEnd + 1;
    }
    return !output.empty();
}

inline bool validateSettingsV2(const std::string& contents, void*) {
    std::vector<settings_file::Entry> parsed;
    return settings_file::parse(contents, parsed) &&
           contents.rfind("DESKNXSET2\n", 0) == 0;
}

// Layout versions 1–3 are converted directly to the canonical v4 serializer.
// The production layout parser performs semantic recovery first; its
// serializer validates geometry, IDs, names and appearance before publication.
inline const Step WorkspaceSteps[] = {
    {1, 4, migrateWorkspaceToV4, validateWorkspaceV4},
    {2, 4, migrateWorkspaceToV4, validateWorkspaceV4},
    {3, 4, migrateWorkspaceToV4, validateWorkspaceV4}
};

inline constexpr std::size_t WorkspaceStepCount =
    sizeof(WorkspaceSteps) / sizeof(WorkspaceSteps[0]);

inline const Step SettingsSteps[] = {
    {1, 2, migrateSettingsV1ToV2, validateSettingsV2}
};

inline constexpr std::size_t SettingsStepCount =
    sizeof(SettingsSteps) / sizeof(SettingsSteps[0]);

} // namespace desknx::migration
