#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace desknx::catalog {

struct GameEntry {
    std::uint64_t titleId = 0;
    std::string title;
    std::string nroPath;  // Full path on SD card
    bool isHomebrew = false;
};

// Scans SD card for installed games and homebrew NRO files
class AppCatalogManager {
public:
    AppCatalogManager() = default;
    ~AppCatalogManager() = default;

    // Scan /atmosphere/contents/ and /switch/ for games
    bool scan();

    // Get all discovered games
    const std::vector<GameEntry>& games() const { return m_games; }

    // Find a game by title ID
    const GameEntry* findByTitleId(std::uint64_t titleId) const;

    // Get count of games and homebrew found
    int gameCount() const;
    int homebrewCount() const;

private:
    bool scanAtmosphereContents();
    bool scanHomebrewFolder();

    std::vector<GameEntry> m_games;
};

}  // namespace desknx::catalog

{"content":"#pragma once\n\n#include <cstdint>\n#include <string>\n#include <vector>\n\nnamespace desknx::catalog {\n\nstruct GameEntry {\n    std::uint64_t titleId = 0;\n    std::string title;\n    std::string nroPath;  // Full path on SD card\n    bool isHomebrew = false;\n};\n\n// Scans SD card for installed games and homebrew NRO files\nclass AppCatalogManager {\npublic:\n    AppCatalogManager() = default;\n    ~AppCatalogManager() = default;\n\n    // Scan /atmosphere/contents/ and /switch/ for games\n    bool scan();\n\n    // Get all discovered games\n    const std::vector<GameEntry>& games() const { return m_games; }\n\n    // Find a game by title ID\n    const GameEntry* findByTitleId(std::uint64_t titleId) const;\n\n    // Get count of games and homebrew found\n    int gameCount() const;\n    int homebrewCount() const;\n\nprivate:\n    bool scanAtmosphereContents();\n    bool scanHomebrewFolder();\n\n    std::vector<GameEntry> m_games;\n};\n\n}  // namespace desknx::catalog\n"}