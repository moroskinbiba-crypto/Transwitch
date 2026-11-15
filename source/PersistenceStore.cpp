#include "PersistenceStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace desknx::persistence {

bool PersistenceStore::load() {
    // Try to load each component. If any fails, keep the defaults.
    // Load in order: config, desktops, widgets
    bool configOk = loadConfig();
    bool desktopsOk = loadDesktops();
    bool widgetsOk = loadWidgets();

    // Log what loaded
    if (!configOk)
        fprintf(stderr, "[persistence] config.json not found or invalid\n");
    if (!desktopsOk)
        fprintf(stderr, "[persistence] workspaces.json not found or invalid\n");
    if (!widgetsOk)
        fprintf(stderr, "[persistence] widgets.json not found or invalid\n");

    return configOk || desktopsOk || widgetsOk;  // Success if at least one loaded
}

bool PersistenceStore::loadConfig() {
    std::ifstream file(kConfigPath);
    if (!file.is_open())
        return false;

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("theme"))
            m_appearance.theme = j["theme"].get<std::string>();
        if (j.contains("brightness"))
            m_appearance.brightness = std::clamp(j["brightness"].get<int>(), 0, 100);
        if (j.contains("language"))
            m_appearance.language = j["language"].get<std::string>();
        if (j.contains("steamGridDbApiKey"))
            m_steamGridDbApiKey = j["steamGridDbApiKey"].get<std::string>();

        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[persistence] loadConfig parse error: %s\n", e.what());
        return false;
    }
}

bool PersistenceStore::loadDesktops() {
    std::ifstream file(kDesktopsPath);
    if (!file.is_open())
        return false;

    try {
        nlohmann::json j;
        file >> j;

        m_desktops.clear();
        if (j.contains("desktops") && j["desktops"].is_array()) {
            for (const auto& item : j["desktops"]) {
                if (!item.is_object())
                    continue;
                DesktopLayout layout;
                layout.id = item.value("id", 0u);
                layout.name = item.value("name", std::string());

                if (item.contains("tiles") && item["tiles"].is_array()) {
                    for (const auto& tile : item["tiles"]) {
                        if (tile.is_number_unsigned())
                            layout.tileIds.push_back(tile.get<std::uint64_t>());
                    }
                }
                if (layout.id > 0)
                    m_desktops.push_back(std::move(layout));
            }
        }
        return !m_desktops.empty();
    } catch (const std::exception& e) {
        fprintf(stderr, "[persistence] loadDesktops parse error: %s\n", e.what());
        return false;
    }
}

bool PersistenceStore::loadWidgets() {
    std::ifstream file(kWidgetsPath);
    if (!file.is_open())
        return false;

    try {
        nlohmann::json j;
        file >> j;

        m_widgets.clear();
        if (j.contains("widgets") && j["widgets"].is_array()) {
            for (const auto& item : j["widgets"]) {
                if (!item.is_object())
                    continue;
                WidgetState widget;
                widget.id = item.value("id", 0u);
                widget.type = item.value("type", std::string());
                widget.columns = item.value("columns", 1);
                widget.rows = item.value("rows", 1);
                widget.positionX = item.value("positionX", 0);
                widget.positionY = item.value("positionY", 0);
                widget.assetRef = item.value("asset", std::string());
                if (widget.id > 0)
                    m_widgets.push_back(std::move(widget));
            }
        }
        return !m_widgets.empty();
    } catch (const std::exception& e) {
        fprintf(stderr, "[persistence] loadWidgets parse error: %s\n", e.what());
        return false;
    }
}

bool PersistenceStore::save() {
    // Create directories if they don't exist
    std::error_code ec;
    std::filesystem::create_directories(kDataDir, ec);
    if (ec) {
        fprintf(stderr, "[persistence] failed to create data directory: %s\n", ec.message().c_str());
        return false;
    }

    // Save each component
    bool configOk = saveConfig();
    bool desktopsOk = saveDesktops();
    bool widgetsOk = saveWidgets();

    if (!configOk)
        fprintf(stderr, "[persistence] failed to save config.json\n");
    if (!desktopsOk)
        fprintf(stderr, "[persistence] failed to save workspaces.json\n");
    if (!widgetsOk)
        fprintf(stderr, "[persistence] failed to save widgets.json\n");

    return configOk && desktopsOk && widgetsOk;
}

bool PersistenceStore::saveConfig() const {
    nlohmann::json j;
    j["version"] = 1;
    j["theme"] = m_appearance.theme.empty() ? "Default Light" : m_appearance.theme;
    j["brightness"] = m_appearance.brightness;
    j["language"] = m_appearance.language.empty() ? "auto" : m_appearance.language;
    if (!m_steamGridDbApiKey.empty())
        j["steamGridDbApiKey"] = m_steamGridDbApiKey;

    const std::string temp = std::string(kConfigPath) + ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file.is_open())
            return false;
        file << j.dump(2) << '\n';
        if (!file.good())
            return false;
    }

    std::error_code ec;
    std::filesystem::remove(kConfigPath, ec);
    ec.clear();
    std::filesystem::rename(temp, kConfigPath, ec);
    return !ec;
}

bool PersistenceStore::saveDesktops() const {
    nlohmann::json j;
    j["version"] = 1;
    j["desktops"] = nlohmann::json::array();

    for (const auto& layout : m_desktops) {
        nlohmann::json item;
        item["id"] = layout.id;
        item["name"] = layout.name;
        item["tiles"] = nlohmann::json::array();
        for (std::uint64_t tileId : layout.tileIds)
            item["tiles"].push_back(tileId);
        j["desktops"].push_back(std::move(item));
    }

    const std::string temp = std::string(kDesktopsPath) + ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file.is_open())
            return false;
        file << j.dump(2) << '\n';
        if (!file.good())
            return false;
    }

    std::error_code ec;
    std::filesystem::remove(kDesktopsPath, ec);
    ec.clear();
    std::filesystem::rename(temp, kDesktopsPath, ec);
    return !ec;
}

bool PersistenceStore::saveWidgets() const {
    nlohmann::json j;
    j["version"] = 1;
    j["widgets"] = nlohmann::json::array();

    for (const auto& widget : m_widgets) {
        nlohmann::json item;
        item["id"] = widget.id;
        item["type"] = widget.type;
        item["columns"] = widget.columns;
        item["rows"] = widget.rows;
        item["positionX"] = widget.positionX;
        item["positionY"] = widget.positionY;
        item["asset"] = widget.assetRef;
        j["widgets"].push_back(std::move(item));
    }

    const std::string temp = std::string(kWidgetsPath) + ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file.is_open())
            return false;
        file << j.dump(2) << '\n';
        if (!file.good())
            return false;
    }

    std::error_code ec;
    std::filesystem::remove(kWidgetsPath, ec);
    ec.clear();
    std::filesystem::rename(temp, kWidgetsPath, ec);
    return !ec;
}

}  // namespace desknx::persistence
