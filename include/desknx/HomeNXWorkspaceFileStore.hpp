#pragma once

#include "HomeNXWorkspaceProvider.hpp"
#include <desknx/SafeReplace.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>

namespace desknx::homenx {

struct WorkspaceFilePaths {
    std::string directory{"sdmc:/switch/DeskNX"};
    std::string layout{"sdmc:/switch/DeskNX/layout.tsv"};
    std::string layoutTemp{"sdmc:/switch/DeskNX/layout.tmp"};
    std::string layoutBackup{"sdmc:/switch/DeskNX/layout.bak"};
    std::string active{"sdmc:/switch/DeskNX/active-workspace.dat"};
    std::string activeTemp{"sdmc:/switch/DeskNX/active-workspace.tmp"};
    std::string activeBackup{"sdmc:/switch/DeskNX/active-workspace.bak"};

    static WorkspaceFilePaths under(const std::string& root) {
        WorkspaceFilePaths paths;
        paths.directory = root;
        const std::string slash = !root.empty() && root.back() == '/' ? "" : "/";
        paths.layout = root + slash + "layout.tsv";
        paths.layoutTemp = root + slash + "layout.tmp";
        paths.layoutBackup = root + slash + "layout.bak";
        paths.active = root + slash + "active-workspace.dat";
        paths.activeTemp = root + slash + "active-workspace.tmp";
        paths.activeBackup = root + slash + "active-workspace.bak";
        return paths;
    }
};

struct WorkspaceLoadInfo {
    bool restoredLayoutBackup{false};
    bool restoredActiveBackup{false};
    bool usedActiveSidecar{false};
};

class WorkspaceFileStore final {
public:
    using CommitCallback = std::function<bool()>;

    explicit WorkspaceFileStore(WorkspaceFilePaths paths = {})
        : m_paths(std::move(paths)) {}

    const WorkspaceFilePaths& paths() const { return m_paths; }

    bool load(WorkspaceProvider& provider, WorkspaceLoadInfo* info = nullptr) const {
        WorkspaceLoadInfo loaded{};
        std::string layoutText;
        WorkspaceProvider candidate;

        // A primary file can exist and still be truncated/corrupt. Validate it
        // before deciding whether the last known-good backup is required.
        const bool primaryReadable = readBounded(m_paths.layout, 1024 * 1024, layoutText);
        const bool primaryValid = primaryReadable && candidate.loadSerialized(layoutText);
        if (!primaryValid) {
            if (!readBounded(m_paths.layoutBackup, 1024 * 1024, layoutText))
                return false;
            WorkspaceProvider backupCandidate;
            if (!backupCandidate.loadSerialized(layoutText))
                return false;
            candidate = std::move(backupCandidate);
            loaded.restoredLayoutBackup = true;
        }

        // A sidecar is valid only for the exact workspace shape it was saved
        // against. Prefer the primary marker, then a matching backup marker.
        std::string activeText;
        int sidecarIndex = 0;
        if (readBounded(m_paths.active, 512, activeText) &&
            layout::parseActiveWorkspaceState(
                activeText, candidate.workspaces(), sidecarIndex)) {
            candidate.switchTo(sidecarIndex);
            loaded.usedActiveSidecar = true;
        } else {
            std::string backupActiveText;
            if (readBounded(m_paths.activeBackup, 512, backupActiveText) &&
                layout::parseActiveWorkspaceState(
                    backupActiveText, candidate.workspaces(), sidecarIndex)) {
                candidate.switchTo(sidecarIndex);
                loaded.restoredActiveBackup = true;
                loaded.usedActiveSidecar = true;
            }
        }

        provider = std::move(candidate);
        if (info)
            *info = loaded;
        return true;
    }
    bool save(const WorkspaceProvider& provider,
              const CommitCallback& commit = {}) const {
        const std::string layoutText = provider.serializeLayout();
        const std::string activeText = provider.serializeActiveState();
        if (layoutText.empty() || activeText.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(m_paths.directory, ec);
        if (ec)
            return false;

        if (!writeBounded(m_paths.layoutTemp, layoutText) ||
            !writeBounded(m_paths.activeTemp, activeText)) {
            removeTemps();
            return false;
        }

        // The callback lets the Switch integration call
        // switchu::commitSdCard() without tying this class to HomeNX headers.
        if (commit && !commit()) {
            removeTemps();
            return false;
        }

        // Publish the active marker first. If publishing the full layout then
        // fails, its shape hash prevents this marker from being applied to a
        // different old layout; when only the active desk changed, the marker
        // remains a valid and intentional update.
        const auto activeStatus = safeio::replaceFilePreservingOnFailure(
            m_paths.activeTemp.c_str(), m_paths.active.c_str(),
            m_paths.activeBackup.c_str(), true);
        if (activeStatus != safeio::ReplaceStatus::Success) {
            removeTemps();
            return false;
        }

        const auto layoutStatus = safeio::replaceFilePreservingOnFailure(
            m_paths.layoutTemp.c_str(), m_paths.layout.c_str(),
            m_paths.layoutBackup.c_str(), true);
        if (layoutStatus != safeio::ReplaceStatus::Success) {
            removeTemps();
            return false;
        }

        if (commit && !commit())
            return false;
        return true;
    }

    bool saveActiveState(const WorkspaceProvider& provider,
                         const CommitCallback& commit = {}) const {
        const std::string activeText = provider.serializeActiveState();
        if (activeText.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(m_paths.directory, ec);
        if (ec || !writeBounded(m_paths.activeTemp, activeText)) {
            removeTemps();
            return false;
        }

        if (commit && !commit()) {
            removeTemps();
            return false;
        }

        const auto status = safeio::replaceFilePreservingOnFailure(
            m_paths.activeTemp.c_str(), m_paths.active.c_str(),
            m_paths.activeBackup.c_str(), true);
        if (status != safeio::ReplaceStatus::Success) {
            removeTemps();
            return false;
        }

        return !commit || commit();
    }

private:
    static bool readBounded(const std::string& path, std::size_t limit,
                            std::string& output) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > limit)
            return false;
        file.seekg(0, std::ios::beg);
        std::string data(static_cast<std::size_t>(size), '\0');
        file.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file.good() && !file.eof())
            return false;
        if (static_cast<std::size_t>(file.gcount()) != data.size())
            return false;
        output = std::move(data);
        return true;
    }

    static bool writeBounded(const std::string& path, const std::string& input) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file.write(input.data(), static_cast<std::streamsize>(input.size()));
        file.flush();
        const bool good = file.good();
        file.close();
        return good && !file.fail();
    }

    void removeTemps() const {
        std::error_code ignored;
        std::filesystem::remove(m_paths.layoutTemp, ignored);
        ignored.clear();
        std::filesystem::remove(m_paths.activeTemp, ignored);
    }

    WorkspaceFilePaths m_paths;
};

} // namespace desknx::homenx

{"content":"#pragma once\r\n\r\n#include \"HomeNXWorkspaceProvider.hpp\"\r\n#include <desknx/SafeReplace.hpp>\r\n\r\n#include <filesystem>\r\n#include <fstream>\r\n#include <functional>\r\n#include <string>\r\n#include <system_error>\r\n\r\nnamespace desknx::homenx {\r\n\r\nstruct WorkspaceFilePaths {\r\n    std::string directory{\"sdmc:/switch/DeskNX\"};\r\n    std::string layout{\"sdmc:/switch/DeskNX/layout.tsv\"};\r\n    std::string layoutTemp{\"sdmc:/switch/DeskNX/layout.tmp\"};\r\n    std::string layoutBackup{\"sdmc:/switch/DeskNX/layout.bak\"};\r\n    std::string active{\"sdmc:/switch/DeskNX/active-workspace.dat\"};\r\n    std::string activeTemp{\"sdmc:/switch/DeskNX/active-workspace.tmp\"};\r\n    std::string activeBackup{\"sdmc:/switch/DeskNX/active-workspace.bak\"};\r\n\r\n    static WorkspaceFilePaths under(const std::string& root) {\r\n        WorkspaceFilePaths paths;\r\n        paths.directory = root;\r\n        const std::string slash = !root.empty() && root.back() == '/' ? \"\" : \"/\";\r\n        paths.layout = root + slash + \"layout.tsv\";\r\n        paths.layoutTemp = root + slash + \"layout.tmp\";\r\n        paths.layoutBackup = root + slash + \"layout.bak\";\r\n        paths.active = root + slash + \"active-workspace.dat\";\r\n        paths.activeTemp = root + slash + \"active-workspace.tmp\";\r\n        paths.activeBackup = root + slash + \"active-workspace.bak\";\r\n        return paths;\r\n    }\r\n};\r\n\r\nstruct WorkspaceLoadInfo {\r\n    bool restoredLayoutBackup{false};\r\n    bool restoredActiveBackup{false};\r\n    bool usedActiveSidecar{false};\r\n};\r\n\r\nclass WorkspaceFileStore final {\r\npublic:\r\n    using CommitCallback = std::function<bool()>;\r\n\r\n    explicit WorkspaceFileStore(WorkspaceFilePaths paths = {})\r\n        : m_paths(std::move(paths)) {}\r\n\r\n    const WorkspaceFilePaths& paths() const { return m_paths; }\r\n\r\n    bool load(WorkspaceProvider& provider, WorkspaceLoadInfo* info = nullptr) const {\r\n        WorkspaceLoadInfo loaded{};\r\n        std::string layoutText;\r\n        WorkspaceProvider candidate;\r\n\r\n        // A primary file can exist and still be truncated/corrupt. Validate it\r\n        // before deciding whether the last known-good backup is required.\r\n        const bool primaryReadable = readBounded(m_paths.layout, 1024 * 1024, layoutText);\r\n        const bool primaryValid = primaryReadable && candidate.loadSerialized(layoutText);\r\n        if (!primaryValid) {\r\n            if (!readBounded(m_paths.layoutBackup, 1024 * 1024, layoutText))\r\n                return false;\r\n            WorkspaceProvider backupCandidate;\r\n            if (!backupCandidate.loadSerialized(layoutText))\r\n                return false;\r\n            candidate = std::move(backupCandidate);\r\n            loaded.restoredLayoutBackup = true;\r\n        }\r\n\r\n        // A sidecar is valid only for the exact workspace shape it was saved\r\n        // against. Prefer the primary marker, then a matching backup marker.\r\n        std::string activeText;\r\n        int sidecarIndex = 0;\r\n        if (readBounded(m_paths.active, 512, activeText) &&\r\n            layout::parseActiveWorkspaceState(\r\n                activeText, candidate.workspaces(), sidecarIndex)) {\r\n            candidate.switchTo(sidecarIndex);\r\n            loaded.usedActiveSidecar = true;\r\n        } else {\r\n            std::string backupActiveText;\r\n            if (readBounded(m_paths.activeBackup, 512, backupActiveText) &&\r\n                layout::parseActiveWorkspaceState(\r\n                    backupActiveText, candidate.workspaces(), sidecarIndex)) {\r\n                candidate.switchTo(sidecarIndex);\r\n                loaded.restoredActiveBackup = true;\r\n                loaded.usedActiveSidecar = true;\r\n            }\r\n        }\r\n\r\n        provider = std::move(candidate);\r\n        if (info)\r\n            *info = loaded;\r\n        return true;\r\n    }\r\n    bool save(const WorkspaceProvider& provider,\r\n              const CommitCallback& commit = {}) const {\r\n        const std::string layoutText = provider.serializeLayout();\r\n        const std::string activeText = provider.serializeActiveState();\r\n        if (layoutText.empty() || activeText.empty())\r\n            return false;\r\n\r\n        std::error_code ec;\r\n        std::filesystem::create_directories(m_paths.directory, ec);\r\n        if (ec)\r\n            return false;\r\n\r\n        if (!writeBounded(m_paths.layoutTemp, layoutText) ||\r\n            !writeBounded(m_paths.activeTemp, activeText)) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        // The callback lets the Switch integration call\r\n        // switchu::commitSdCard() without tying this class to HomeNX headers.\r\n        if (commit && !commit()) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        // Publish the active marker first. If publishing the full layout then\r\n        // fails, its shape hash prevents this marker from being applied to a\r\n        // different old layout; when only the active desk changed, the marker\r\n        // remains a valid and intentional update.\r\n        const auto activeStatus = safeio::replaceFilePreservingOnFailure(\r\n            m_paths.activeTemp.c_str(), m_paths.active.c_str(),\r\n            m_paths.activeBackup.c_str(), true);\r\n        if (activeStatus != safeio::ReplaceStatus::Success) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        const auto layoutStatus = safeio::replaceFilePreservingOnFailure(\r\n            m_paths.layoutTemp.c_str(), m_paths.layout.c_str(),\r\n            m_paths.layoutBackup.c_str(), true);\r\n        if (layoutStatus != safeio::ReplaceStatus::Success) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        if (commit && !commit())\r\n            return false;\r\n        return true;\r\n    }\r\n\r\n    bool saveActiveState(const WorkspaceProvider& provider,\r\n                         const CommitCallback& commit = {}) const {\r\n        const std::string activeText = provider.serializeActiveState();\r\n        if (activeText.empty())\r\n            return false;\r\n\r\n        std::error_code ec;\r\n        std::filesystem::create_directories(m_paths.directory, ec);\r\n        if (ec || !writeBounded(m_paths.activeTemp, activeText)) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        if (commit && !commit()) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        const auto status = safeio::replaceFilePreservingOnFailure(\r\n            m_paths.activeTemp.c_str(), m_paths.active.c_str(),\r\n            m_paths.activeBackup.c_str(), true);\r\n        if (status != safeio::ReplaceStatus::Success) {\r\n            removeTemps();\r\n            return false;\r\n        }\r\n\r\n        return !commit || commit();\r\n    }\r\n\r\nprivate:\r\n    static bool readBounded(const std::string& path, std::size_t limit,\r\n                            std::string& output) {\r\n        std::ifstream file(path, std::ios::binary);\r\n        if (!file.is_open())\r\n            return false;\r\n        file.seekg(0, std::ios::end);\r\n        const std::streamoff size = file.tellg();\r\n        if (size <= 0 || static_cast<std::uint64_t>(size) > limit)\r\n            return false;\r\n        file.seekg(0, std::ios::beg);\r\n        std::string data(static_cast<std::size_t>(size), '\\0');\r\n        file.read(data.data(), static_cast<std::streamsize>(data.size()));\r\n        if (!file.good() && !file.eof())\r\n            return false;\r\n        if (static_cast<std::size_t>(file.gcount()) != data.size())\r\n            return false;\r\n        output = std::move(data);\r\n        return true;\r\n    }\r\n\r\n    static bool writeBounded(const std::string& path, const std::string& input) {\r\n        std::ofstream file(path, std::ios::binary | std::ios::trunc);\r\n        if (!file.is_open())\r\n            return false;\r\n        file.write(input.data(), static_cast<std::streamsize>(input.size()));\r\n        file.flush();\r\n        const bool good = file.good();\r\n        file.close();\r\n        return good && !file.fail();\r\n    }\r\n\r\n    void removeTemps() const {\r\n        std::error_code ignored;\r\n        std::filesystem::remove(m_paths.layoutTemp, ignored);\r\n        ignored.clear();\r\n        std::filesystem::remove(m_paths.activeTemp, ignored);\r\n    }\r\n\r\n    WorkspaceFilePaths m_paths;\r\n};\r\n\r\n} // namespace desknx::homenx\r\n"}