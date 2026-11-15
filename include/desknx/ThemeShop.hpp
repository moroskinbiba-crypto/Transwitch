#pragma once

// Theme shop domain: catalogue entry, download state machine, install phases,
// and the installed-themes list.
//
// Mirrors HomeNX ThemeShopScreen + ThemePackageInstaller + ThemeCatalogClient.
// No HTTP, no file I/O, no rendering.

#include <cstdint>
#include <string>
#include <vector>

namespace desknx::themeshop {

// ---- Catalogue entry (HomeNX ThemeCatalogClient::Entry) ------------------

struct CatalogEntry {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string catalogUrl;   // base URL; path/manifest are relative to this
    std::string path;         // path to the package file
    std::string manifest;
    std::string cover;        // URL to preview image
    std::vector<std::string> screenshots;
};

// ---- Download / install state machine ------------------------------------
//
// Mirrors ThemePackageInstaller::Mode and the progress messages the UI shows.

enum class InstallMode : unsigned char {
    InstallOnly,
    InstallAndApply,
};

enum class InstallStatus : unsigned char {
    Idle,
    Downloading,   // fetching the package
    Unpacking,     // extracting to destination
    Applying,      // setting as active theme
    Done,
    Failed,
};

struct InstallState {
    InstallStatus status{InstallStatus::Idle};
    std::string   entryId;       // which entry is being installed
    std::string   message;       // "Downloading artwork…", "Applying theme…", etc.
    float         progress{0.f}; // 0..1
    bool          installed{false};
    std::string   destinationPath;

    bool isActive() const {
        return status == InstallStatus::Downloading
            || status == InstallStatus::Unpacking
            || status == InstallStatus::Applying;
    }
    bool isDone()   const { return status == InstallStatus::Done; }
    bool isFailed() const { return status == InstallStatus::Failed; }

    void setDownloading(const std::string& id) {
        status      = InstallStatus::Downloading;
        entryId     = id;
        message     = "Downloading theme...";
        progress    = 0.f;
        installed   = false;
        destinationPath.clear();
    }
    void setUnpacking(float p) {
        status   = InstallStatus::Unpacking;
        message  = "Unpacking...";
        progress = p;
    }
    void setApplying(float p) {
        status   = InstallStatus::Applying;
        message  = "Applying theme...";
        progress = p;
    }
    void setDone(const std::string& dest) {
        status          = InstallStatus::Done;
        message         = "Theme installed.";
        progress        = 1.f;
        installed       = true;
        destinationPath = dest;
    }
    void setFailed(const std::string& reason) {
        status   = InstallStatus::Failed;
        message  = reason;
        progress = 0.f;
    }
    void reset() { *this = InstallState{}; }
};

// ---- Installed themes list -----------------------------------------------

struct InstalledTheme {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string localPath;   // sdmc: path to the installed package
    bool        active{false};
};

class InstalledThemeList final {
public:
    void add(InstalledTheme theme) {
        // Replace if already present (same id = update).
        for (auto& t : m_themes) {
            if (t.id == theme.id) { t = std::move(theme); return; }
        }
        m_themes.push_back(std::move(theme));
    }

    void remove(const std::string& id) {
        m_themes.erase(
            std::remove_if(m_themes.begin(), m_themes.end(),
                           [&](const InstalledTheme& t) { return t.id == id; }),
            m_themes.end());
    }

    void setActive(const std::string& id) {
        for (auto& t : m_themes) t.active = (t.id == id);
    }

    const InstalledTheme* findById(const std::string& id) const {
        for (const auto& t : m_themes)
            if (t.id == id) return &t;
        return nullptr;
    }

    bool isInstalled(const std::string& id) const { return findById(id) != nullptr; }
    bool isActive(const std::string& id) const {
        const InstalledTheme* t = findById(id);
        return t && t->active;
    }

    const std::vector<InstalledTheme>& all() const { return m_themes; }
    std::size_t count() const { return m_themes.size(); }

private:
    std::vector<InstalledTheme> m_themes;
};

// ---- Catalogue state (what the community tab shows) ----------------------

struct CatalogState {
    enum class LoadStatus : unsigned char { Idle, Loading, Ready, Failed };

    LoadStatus                status{LoadStatus::Idle};
    std::vector<CatalogEntry> entries;
    std::string               error;
    int                       selectedIndex{0};

    bool isLoading() const { return status == LoadStatus::Loading; }
    bool isReady()   const { return status == LoadStatus::Ready; }

    void setLoading()           { status = LoadStatus::Loading; entries.clear(); error.clear(); }
    void setReady(std::vector<CatalogEntry> e) { status = LoadStatus::Ready; entries = std::move(e); }
    void setFailed(const std::string& msg)     { status = LoadStatus::Failed; error = msg; }

    void moveSelection(int dir) {
        if (entries.empty()) return;
        selectedIndex += dir;
        if (selectedIndex < 0) selectedIndex = static_cast<int>(entries.size()) - 1;
        if (selectedIndex >= static_cast<int>(entries.size())) selectedIndex = 0;
    }

    const CatalogEntry* selected() const {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
            return nullptr;
        return &entries[static_cast<std::size_t>(selectedIndex)];
    }
};

} // namespace desknx::themeshop

#include <algorithm>  // std::remove_if used above
