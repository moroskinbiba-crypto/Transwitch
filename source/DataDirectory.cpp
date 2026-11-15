#include "DataDirectory.hpp"

#include <filesystem>
#include <system_error>
#include <cstdio>
#include <fstream>

namespace desknx::data {

static constexpr const char* kDefaultLauncherMarker = "sdmc:/switch/DeskNX/.is_default_launcher";

bool DataDirectory::createDirectory(const std::string& path, bool logError) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);

    if (ec && ec.value() != 0) {
        // EEXIST is okay (directory already exists)
        if (ec.value() == EEXIST) {
            fprintf(stderr, "[data-dir] %s already exists (ok)\n", path.c_str());
            return true;
        }
        if (logError) {
            fprintf(stderr, "[data-dir] failed to create %s: %s\n", path.c_str(),
                    ec.message().c_str());
        }
        return false;
    }

    fprintf(stderr, "[data-dir] created %s\n", path.c_str());
    return true;
}

bool DataDirectory::ensureAll() {
    fprintf(stderr, "[data-dir] initializing SD card directories\n");

    bool allOk = true;

    // DeskNX config directories (title override)
    if (!createDirectory(configDir()))
        allOk = false;

    // DeskNX main folder
    if (!createDirectory(desknxDir()))
        allOk = false;

    // SteamGridDB cache
    if (!createDirectory(steamGridDbDir()))
        allOk = false;

    // Atmosphere base (may not exist on fresh install, but we try anyway)
    createDirectory(atmosphereDir(), false);

    // Atmosphere contents (where games are installed)
    createDirectory(contentsDir(), false);

    fprintf(stderr, "[data-dir] initialization %s\n", allOk ? "complete" : "had issues");
    return allOk;
}

bool DataDirectory::isDefaultLauncher() {
    std::ifstream markerFile(kDefaultLauncherMarker);
    return markerFile.good();
}

void DataDirectory::toggleDefaultLauncher() {
    if (isDefaultLauncher()) {
        // Disable: remove marker
        if (std::remove(kDefaultLauncherMarker) == 0) {
            fprintf(stderr, "[data-dir] default launcher registration disabled\n");
        } else {
            fprintf(stderr, "[data-dir] failed to remove marker file\n");
        }
    } else {
        // Enable: create marker
        std::ofstream markerFile(kDefaultLauncherMarker);
        if (markerFile.is_open()) {
            markerFile << "DeskNX is registered as the default launcher\n";
            markerFile.close();
            fprintf(stderr, "[data-dir] default launcher registration enabled\n");
        } else {
            fprintf(stderr, "[data-dir] failed to create marker file\n");
        }
    }
}

}  // namespace desknx::data
