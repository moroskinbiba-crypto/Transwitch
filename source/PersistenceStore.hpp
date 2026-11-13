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

{"content":"#pragma once\n\n#include <cstdint>\n#include <string>\n#include <vector>\n#include <utility>\n\nnamespace desknx::persistence {\n\n// Unified persistence layer wrapping Config + WidgetStore + FolderStore\n// All paths rooted at sdmc:/01000000/00001000/ for DeskNX title override\nclass PersistenceStore {\npublic:\n    struct DesktopLayout {\n        std::uint32_t id = 0;\n        std::string name;\n        std::vector<std::uint64_t> tileIds;  // Game title IDs in grid order\n    };\n\n    struct AppearanceSettings {\n        std::string theme;\n        int brightness = 50;\n        std::string language = \"auto\";\n    };\n\n    struct WidgetState {\n        std::uint32_t id = 0;\n        std::string type;\n        int columns = 1;\n        int rows = 1;\n        int positionX = 0;\n        int positionY = 0;\n        std::string assetRef;\n    };\n\n    PersistenceStore() = default;\n    ~PersistenceStore() = default;\n\n    // Load all persisted state from SD card\n    bool load();\n\n    // Save all state back to SD card (atomic: temp → verify → rename)\n    bool save();\n\n    // Appearance settings (themes, brightness, language)\n    const AppearanceSettings& appearance() const { return m_appearance; }\n    void setAppearance(const AppearanceSettings& settings) { m_appearance = settings; }\n\n    // Desktop layouts (workspaces)\n    const std::vector<DesktopLayout>& desktops() const { return m_desktops; }\n    std::vector<DesktopLayout>& desktops() { return m_desktops; }\n\n    // Widget state (clock, battery, recent games)\n    const std::vector<WidgetState>& widgets() const { return m_widgets; }\n    std::vector<WidgetState>& widgets() { return m_widgets; }\n\n    // Steam GridDB API key (persisted separately)\n    const std::string& steamGridDbApiKey() const { return m_steamGridDbApiKey; }\n    void setSteamGridDbApiKey(const std::string& key) { m_steamGridDbApiKey = key; }\n\nprivate:\n    bool loadConfig();\n    bool loadDesktops();\n    bool loadWidgets();\n    bool saveConfig() const;\n    bool saveDesktops() const;\n    bool saveWidgets() const;\n\n    // All files rooted here\n    static constexpr const char* kDataDir = \"sdmc:/01000000/00001000\";\n    static constexpr const char* kConfigPath = \"sdmc:/01000000/00001000/config.json\";\n    static constexpr const char* kDesktopsPath = \"sdmc:/01000000/00001000/workspaces.json\";\n    static constexpr const char* kWidgetsPath = \"sdmc:/01000000/00001000/widgets.json\";\n\n    AppearanceSettings m_appearance;\n    std::vector<DesktopLayout> m_desktops;\n    std::vector<WidgetState> m_widgets;\n    std::string m_steamGridDbApiKey;\n};\n\n}  // namespace desknx::persistence\n"}