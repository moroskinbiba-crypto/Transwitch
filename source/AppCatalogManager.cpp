#include "AppCatalogManager.hpp"

#include <algorithm>
#include <filesystem>
#include <cstdio>

namespace desknx::catalog {

bool AppCatalogManager::scan() {
    m_games.clear();

    if (!scanAtmosphereContents())
        fprintf(stderr, "[catalog] /atmosphere/contents/ scan had issues\n");

    if (!scanHomebrewFolder())
        fprintf(stderr, "[catalog] /switch/ homebrew scan had issues\n");

    fprintf(stderr, "[catalog] scan complete: %d games, %d homebrew\n",
            gameCount(), homebrewCount());
    return !m_games.empty();
}

bool AppCatalogManager::scanAtmosphereContents() {
    const std::string contentPath = "sdmc:/atmosphere/contents";
    std::error_code ec;

    if (!std::filesystem::is_directory(contentPath, ec)) {
        fprintf(stderr, "[catalog] %s does not exist or is not a directory\n", contentPath.c_str());
        return false;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(contentPath, ec)) {
            if (!entry.is_directory())
                continue;

            const std::string dirName = entry.path().filename().string();
            if (dirName.size() != 16)
                continue;  // Title IDs are 16 hex chars

            // Parse title ID
            std::uint64_t titleId = 0;
            try {
                titleId = std::stoull(dirName, nullptr, 16);
            } catch (...) {
                continue;
            }

            if (titleId == 0)
                continue;

            // For now, just record that this title is installed.
            // In a full implementation, you'd read the exefs/main and get metadata.
            GameEntry game;
            game.titleId = titleId;
            game.title = dirName;  // Fallback to title ID
            game.isHomebrew = false;
            m_games.push_back(std::move(game));
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[catalog] scanAtmosphereContents exception: %s\n", e.what());
        return false;
    }

    return !m_games.empty();
}

bool AppCatalogManager::scanHomebrewFolder() {
    const std::string switchPath = "sdmc:/switch";
    std::error_code ec;

    if (!std::filesystem::is_directory(switchPath, ec)) {
        fprintf(stderr, "[catalog] %s does not exist or is not a directory\n", switchPath.c_str());
        return true;  // Not an error if switch folder doesn't exist
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(switchPath, ec)) {
            if (!entry.is_regular_file())
                continue;

            const std::string filename = entry.path().filename().string();
            const std::string ext = filename.size() >= 4 ? filename.substr(filename.size() - 4) : "";

            if (ext != ".nro")
                continue;

            // Extract title from filename (without .nro extension)
            std::string title = filename.substr(0, filename.size() - 4);

            GameEntry game;
            game.titleId = 0;  // Homebrew has no title ID
            game.title = title;
            game.nroPath = entry.path().string();
            game.isHomebrew = true;
            m_games.push_back(std::move(game));
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[catalog] scanHomebrewFolder exception: %s\n", e.what());
        return false;
    }

    return true;
}

const GameEntry* AppCatalogManager::findByTitleId(std::uint64_t titleId) const {
    auto it = std::find_if(m_games.begin(), m_games.end(),
        [titleId](const GameEntry& game) { return game.titleId == titleId; });
    return it != m_games.end() ? &*it : nullptr;
}

int AppCatalogManager::gameCount() const {
    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),
        [](const GameEntry& g) { return !g.isHomebrew; }));
}

int AppCatalogManager::homebrewCount() const {
    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),
        [](const GameEntry& g) { return g.isHomebrew; }));
}

}  // namespace desknx::catalog

{"content":"#include \"AppCatalogManager.hpp\"\n\n#include <algorithm>\n#include <filesystem>\n#include <cstdio>\n\nnamespace desknx::catalog {\n\nbool AppCatalogManager::scan() {\n    m_games.clear();\n\n    if (!scanAtmosphereContents())\n        fprintf(stderr, \"[catalog] /atmosphere/contents/ scan had issues\\n\");\n\n    if (!scanHomebrewFolder())\n        fprintf(stderr, \"[catalog] /switch/ homebrew scan had issues\\n\");\n\n    fprintf(stderr, \"[catalog] scan complete: %d games, %d homebrew\\n\",\n            gameCount(), homebrewCount());\n    return !m_games.empty();\n}\n\nbool AppCatalogManager::scanAtmosphereContents() {\n    const std::string contentPath = \"sdmc:/atmosphere/contents\";\n    std::error_code ec;\n\n    if (!std::filesystem::is_directory(contentPath, ec)) {\n        fprintf(stderr, \"[catalog] %s does not exist or is not a directory\\n\", contentPath.c_str());\n        return false;\n    }\n\n    try {\n        for (const auto& entry : std::filesystem::directory_iterator(contentPath, ec)) {\n            if (!entry.is_directory())\n                continue;\n\n            const std::string dirName = entry.path().filename().string();\n            if (dirName.size() != 16)\n                continue;  // Title IDs are 16 hex chars\n\n            // Parse title ID\n            std::uint64_t titleId = 0;\n            try {\n                titleId = std::stoull(dirName, nullptr, 16);\n            } catch (...) {\n                continue;\n            }\n\n            if (titleId == 0)\n                continue;\n\n            // For now, just record that this title is installed.\n            // In a full implementation, you'd read the exefs/main and get metadata.\n            GameEntry game;\n            game.titleId = titleId;\n            game.title = dirName;  // Fallback to title ID\n            game.isHomebrew = false;\n            m_games.push_back(std::move(game));\n        }\n    } catch (const std::exception& e) {\n        fprintf(stderr, \"[catalog] scanAtmosphereContents exception: %s\\n\", e.what());\n        return false;\n    }\n\n    return !m_games.empty();\n}\n\nbool AppCatalogManager::scanHomebrewFolder() {\n    const std::string switchPath = \"sdmc:/switch\";\n    std::error_code ec;\n\n    if (!std::filesystem::is_directory(switchPath, ec)) {\n        fprintf(stderr, \"[catalog] %s does not exist or is not a directory\\n\", switchPath.c_str());\n        return true;  // Not an error if switch folder doesn't exist\n    }\n\n    try {\n        for (const auto& entry : std::filesystem::directory_iterator(switchPath, ec)) {\n            if (!entry.is_regular_file())\n                continue;\n\n            const std::string filename = entry.path().filename().string();\n            const std::string ext = filename.size() >= 4 ? filename.substr(filename.size() - 4) : \"\";\n\n            if (ext != \".nro\")\n                continue;\n\n            // Extract title from filename (without .nro extension)\n            std::string title = filename.substr(0, filename.size() - 4);\n\n            GameEntry game;\n            game.titleId = 0;  // Homebrew has no title ID\n            game.title = title;\n            game.nroPath = entry.path().string();\n            game.isHomebrew = true;\n            m_games.push_back(std::move(game));\n        }\n    } catch (const std::exception& e) {\n        fprintf(stderr, \"[catalog] scanHomebrewFolder exception: %s\\n\", e.what());\n        return false;\n    }\n\n    return true;\n}\n\nconst GameEntry* AppCatalogManager::findByTitleId(std::uint64_t titleId) const {\n    auto it = std::find_if(m_games.begin(), m_games.end(),\n        [titleId](const GameEntry& game) { return game.titleId == titleId; });\n    return it != m_games.end() ? &*it : nullptr;\n}\n\nint AppCatalogManager::gameCount() const {\n    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),\n        [](const GameEntry& g) { return !g.isHomebrew; }));\n}\n\nint AppCatalogManager::homebrewCount() const {\n    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),\n        [](const GameEntry& g) { return g.isHomebrew; }));\n}\n\n}  // namespace desknx::catalog\n"}