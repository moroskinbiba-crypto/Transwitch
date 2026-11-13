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

{"content":"#pragma once\n\n// Theme shop domain: catalogue entry, download state machine, install phases,\n// and the installed-themes list.\n//\n// Mirrors HomeNX ThemeShopScreen + ThemePackageInstaller + ThemeCatalogClient.\n// No HTTP, no file I/O, no rendering.\n\n#include <cstdint>\n#include <string>\n#include <vector>\n\nnamespace desknx::themeshop {\n\n// ---- Catalogue entry (HomeNX ThemeCatalogClient::Entry) ------------------\n\nstruct CatalogEntry {\n    std::string id;\n    std::string name;\n    std::string author;\n    std::string version;\n    std::string catalogUrl;   // base URL; path/manifest are relative to this\n    std::string path;         // path to the package file\n    std::string manifest;\n    std::string cover;        // URL to preview image\n    std::vector<std::string> screenshots;\n};\n\n// ---- Download / install state machine ------------------------------------\n//\n// Mirrors ThemePackageInstaller::Mode and the progress messages the UI shows.\n\nenum class InstallMode : unsigned char {\n    InstallOnly,\n    InstallAndApply,\n};\n\nenum class InstallStatus : unsigned char {\n    Idle,\n    Downloading,   // fetching the package\n    Unpacking,     // extracting to destination\n    Applying,      // setting as active theme\n    Done,\n    Failed,\n};\n\nstruct InstallState {\n    InstallStatus status{InstallStatus::Idle};\n    std::string   entryId;       // which entry is being installed\n    std::string   message;       // \"Downloading artwork…\", \"Applying theme…\", etc.\n    float         progress{0.f}; // 0..1\n    bool          installed{false};\n    std::string   destinationPath;\n\n    bool isActive() const {\n        return status == InstallStatus::Downloading\n            || status == InstallStatus::Unpacking\n            || status == InstallStatus::Applying;\n    }\n    bool isDone()   const { return status == InstallStatus::Done; }\n    bool isFailed() const { return status == InstallStatus::Failed; }\n\n    void setDownloading(const std::string& id) {\n        status      = InstallStatus::Downloading;\n        entryId     = id;\n        message     = \"Downloading theme...\";\n        progress    = 0.f;\n        installed   = false;\n        destinationPath.clear();\n    }\n    void setUnpacking(float p) {\n        status   = InstallStatus::Unpacking;\n        message  = \"Unpacking...\";\n        progress = p;\n    }\n    void setApplying(float p) {\n        status   = InstallStatus::Applying;\n        message  = \"Applying theme...\";\n        progress = p;\n    }\n    void setDone(const std::string& dest) {\n        status          = InstallStatus::Done;\n        message         = \"Theme installed.\";\n        progress        = 1.f;\n        installed       = true;\n        destinationPath = dest;\n    }\n    void setFailed(const std::string& reason) {\n        status   = InstallStatus::Failed;\n        message  = reason;\n        progress = 0.f;\n    }\n    void reset() { *this = InstallState{}; }\n};\n\n// ---- Installed themes list -----------------------------------------------\n\nstruct InstalledTheme {\n    std::string id;\n    std::string name;\n    std::string author;\n    std::string version;\n    std::string localPath;   // sdmc: path to the installed package\n    bool        active{false};\n};\n\nclass InstalledThemeList final {\npublic:\n    void add(InstalledTheme theme) {\n        // Replace if already present (same id = update).\n        for (auto& t : m_themes) {\n            if (t.id == theme.id) { t = std::move(theme); return; }\n        }\n        m_themes.push_back(std::move(theme));\n    }\n\n    void remove(const std::string& id) {\n        m_themes.erase(\n            std::remove_if(m_themes.begin(), m_themes.end(),\n                           [&](const InstalledTheme& t) { return t.id == id; }),\n            m_themes.end());\n    }\n\n    void setActive(const std::string& id) {\n        for (auto& t : m_themes) t.active = (t.id == id);\n    }\n\n    const InstalledTheme* findById(const std::string& id) const {\n        for (const auto& t : m_themes)\n            if (t.id == id) return &t;\n        return nullptr;\n    }\n\n    bool isInstalled(const std::string& id) const { return findById(id) != nullptr; }\n    bool isActive(const std::string& id) const {\n        const InstalledTheme* t = findById(id);\n        return t && t->active;\n    }\n\n    const std::vector<InstalledTheme>& all() const { return m_themes; }\n    std::size_t count() const { return m_themes.size(); }\n\nprivate:\n    std::vector<InstalledTheme> m_themes;\n};\n\n// ---- Catalogue state (what the community tab shows) ----------------------\n\nstruct CatalogState {\n    enum class LoadStatus : unsigned char { Idle, Loading, Ready, Failed };\n\n    LoadStatus                status{LoadStatus::Idle};\n    std::vector<CatalogEntry> entries;\n    std::string               error;\n    int                       selectedIndex{0};\n\n    bool isLoading() const { return status == LoadStatus::Loading; }\n    bool isReady()   const { return status == LoadStatus::Ready; }\n\n    void setLoading()           { status = LoadStatus::Loading; entries.clear(); error.clear(); }\n    void setReady(std::vector<CatalogEntry> e) { status = LoadStatus::Ready; entries = std::move(e); }\n    void setFailed(const std::string& msg)     { status = LoadStatus::Failed; error = msg; }\n\n    void moveSelection(int dir) {\n        if (entries.empty()) return;\n        selectedIndex += dir;\n        if (selectedIndex < 0) selectedIndex = static_cast<int>(entries.size()) - 1;\n        if (selectedIndex >= static_cast<int>(entries.size())) selectedIndex = 0;\n    }\n\n    const CatalogEntry* selected() const {\n        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))\n            return nullptr;\n        return &entries[static_cast<std::size_t>(selectedIndex)];\n    }\n};\n\n} // namespace desknx::themeshop\n\n#include <algorithm>  // std::remove_if used above\n"}