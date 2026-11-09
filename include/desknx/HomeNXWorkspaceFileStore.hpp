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
        if (!readBounded(m_paths.layout, 1024 * 1024, layoutText)) {
            if (!readBounded(m_paths.layoutBackup, 1024 * 1024, layoutText))
                return false;
            loaded.restoredLayoutBackup = true;
        }

        WorkspaceProvider candidate;
        std::string activeText;
        const bool hasActive = readBounded(m_paths.active, 512, activeText);
        if (!candidate.loadSerialized(layoutText, hasActive ? activeText : std::string()))
            return false;

        int sidecarIndex = 0;
        if (hasActive && layout::parseActiveWorkspaceState(
                activeText, candidate.workspaces(), sidecarIndex)) {
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

        const auto layoutStatus = safeio::replaceFilePreservingOnFailure(
            m_paths.layoutTemp.c_str(), m_paths.layout.c_str(),
            m_paths.layoutBackup.c_str(), true);
        if (layoutStatus != safeio::ReplaceStatus::Success) {
            removeTemps();
            return false;
        }

        const auto activeStatus = safeio::replaceFilePreservingOnFailure(
            m_paths.activeTemp.c_str(), m_paths.active.c_str(),
            m_paths.activeBackup.c_str(), true);
        if (activeStatus != safeio::ReplaceStatus::Success) {
            removeTemps();
            return false;
        }

        if (commit && !commit())
            return false;
        return true;
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
