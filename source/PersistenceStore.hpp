#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace desknx::persistence {

// Unified persistence layer wrapping Config + WidgetStore + FolderStore
// All paths rooted at sdmc:/01000000/00001000/ for DeskNX title override
class PersistenceStore {
public:
    struct DesktopLayout {
        std::uint32_t id = 0;
        std::string name;
        std::vector<std::uint64_t> tileIds;  // Game title IDs in grid order
    };

    struct AppearanceSettings {
        std::string theme;
        int brightness = 50;
        std::string language = "auto";
    };

    struct WidgetState {
        std::uint32_t id = 0;
        std::string type;
        int columns = 1;
        int rows = 1;
        int positionX = 0;
        int positionY = 0;
        std::string assetRef;
    };

    PersistenceStore() = default;
    ~PersistenceStore() = default;

    // Load all persisted state from SD card
    bool load();

    // Save all state back to SD card (atomic: temp → verify → rename)
    bool save();

    // Appearance settings (themes, brightness, language)
    const AppearanceSettings& appearance() const { return m_appearance; }
    void setAppearance(const AppearanceSettings& settings) { m_appearance = settings; }

    // Desktop layouts (workspaces)
    const std::vector<DesktopLayout>& desktops() const { return m_desktops; }
    std::vector<DesktopLayout>& desktops() { return m_desktops; }

    // Widget state (clock, battery, recent games)
    const std::vector<WidgetState>& widgets() const { return m_widgets; }
    std::vector<WidgetState>& widgets() { return m_widgets; }

    // Steam GridDB API key (persisted separately)
    const std::string& steamGridDbApiKey() const { return m_steamGridDbApiKey; }
    void setSteamGridDbApiKey(const std::string& key) { m_steamGridDbApiKey = key; }

private:
    bool loadConfig();
    bool loadDesktops();
    bool loadWidgets();
    bool saveConfig() const;
    bool saveDesktops() const;
    bool saveWidgets() const;

    // All files rooted here
    static constexpr const char* kDataDir = "sdmc:/01000000/00001000";
    static constexpr const char* kConfigPath = "sdmc:/01000000/00001000/config.json";
    static constexpr const char* kDesktopsPath = "sdmc:/01000000/00001000/workspaces.json";
    static constexpr const char* kWidgetsPath = "sdmc:/01000000/00001000/widgets.json";

    AppearanceSettings m_appearance;
    std::vector<DesktopLayout> m_desktops;
    std::vector<WidgetState> m_widgets;
    std::string m_steamGridDbApiKey;
};

}  // namespace desknx::persistence
