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
