#include "AppCatalogManager.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>

namespace desknx::catalog {
namespace {

bool parseTitleId(const std::string& value, std::uint64_t& titleId) {
    if (value.size() != 16)
        return false;

    titleId = 0;
    for (unsigned char ch : value) {
        unsigned int digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        } else {
            titleId = 0;
            return false;
        }
        titleId = (titleId << 4) | digit;
    }

    return titleId != 0;
}

} // namespace

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
        if (ec) {
            fprintf(stderr, "[catalog] cannot inspect %s: %s\n",
                    contentPath.c_str(), ec.message().c_str());
        } else {
            fprintf(stderr, "[catalog] %s does not exist or is not a directory\n",
                    contentPath.c_str());
        }
        return false;
    }

    std::filesystem::directory_iterator it(contentPath, ec);
    const std::filesystem::directory_iterator end;
    if (ec) {
        fprintf(stderr, "[catalog] cannot enumerate %s: %s\n",
                contentPath.c_str(), ec.message().c_str());
        return false;
    }

    while (it != end) {
        const std::filesystem::directory_entry entry = *it;
        std::error_code entryError;
        const bool isDirectory = entry.is_directory(entryError);

        if (entryError) {
            fprintf(stderr, "[catalog] cannot inspect entry in %s: %s\n",
                    contentPath.c_str(), entryError.message().c_str());
        } else if (isDirectory) {
            const std::string dirName = entry.path().filename().string();
            std::uint64_t titleId = 0;
            if (parseTitleId(dirName, titleId)) {
                GameEntry game;
                game.titleId = titleId;
                game.title = dirName;
                game.isHomebrew = false;
                m_games.push_back(std::move(game));
            }
        }

        ec.clear();
        it.increment(ec);
        if (ec) {
            fprintf(stderr, "[catalog] enumeration stopped in %s: %s\n",
                    contentPath.c_str(), ec.message().c_str());
            return false;
        }
    }

    return true;
}

bool AppCatalogManager::scanHomebrewFolder() {
    const std::string switchPath = "sdmc:/switch";
    std::error_code ec;

    if (!std::filesystem::is_directory(switchPath, ec)) {
        if (ec) {
            fprintf(stderr, "[catalog] cannot inspect %s: %s\n",
                    switchPath.c_str(), ec.message().c_str());
            return false;
        }
        fprintf(stderr, "[catalog] %s does not exist or is not a directory\n",
                switchPath.c_str());
        return true;
    }

    std::filesystem::directory_iterator it(switchPath, ec);
    const std::filesystem::directory_iterator end;
    if (ec) {
        fprintf(stderr, "[catalog] cannot enumerate %s: %s\n",
                switchPath.c_str(), ec.message().c_str());
        return false;
    }

    while (it != end) {
        const std::filesystem::directory_entry entry = *it;
        std::error_code entryError;
        const bool isRegularFile = entry.is_regular_file(entryError);

        if (entryError) {
            fprintf(stderr, "[catalog] cannot inspect entry in %s: %s\n",
                    switchPath.c_str(), entryError.message().c_str());
        } else if (isRegularFile) {
            const std::string filename = entry.path().filename().string();
            if (filename.size() >= 4 &&
                filename.compare(filename.size() - 4, 4, ".nro") == 0) {
                const std::string title = filename.substr(0, filename.size() - 4);

                GameEntry game;
                game.titleId = 0;
                game.title = title;
                game.nroPath = entry.path().string();
                game.isHomebrew = true;
                m_games.push_back(std::move(game));
            }
        }

        ec.clear();
        it.increment(ec);
        if (ec) {
            fprintf(stderr, "[catalog] enumeration stopped in %s: %s\n",
                    switchPath.c_str(), ec.message().c_str());
            return false;
        }
    }

    return true;
}

const GameEntry* AppCatalogManager::findByTitleId(std::uint64_t titleId) const {
    const auto it = std::find_if(m_games.begin(), m_games.end(),
        [titleId](const GameEntry& game) { return game.titleId == titleId; });
    return it != m_games.end() ? &*it : nullptr;
}

int AppCatalogManager::gameCount() const {
    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),
        [](const GameEntry& game) { return !game.isHomebrew; }));
}

int AppCatalogManager::homebrewCount() const {
    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),
        [](const GameEntry& game) { return game.isHomebrew; }));
}

} // namespace desknx::catalog
