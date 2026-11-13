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

{"content":"#include \"DataDirectory.hpp\"\n\n#include <filesystem>\n#include <system_error>\n#include <cstdio>\n#include <fstream>\n\nnamespace desknx::data {\n\nstatic constexpr const char* kDefaultLauncherMarker = \"sdmc:/switch/DeskNX/.is_default_launcher\";\n\nbool DataDirectory::createDirectory(const std::string& path, bool logError) {\n    std::error_code ec;\n    std::filesystem::create_directories(path, ec);\n\n    if (ec && ec.value() != 0) {\n        // EEXIST is okay (directory already exists)\n        if (ec.value() == EEXIST) {\n            fprintf(stderr, \"[data-dir] %s already exists (ok)\\n\", path.c_str());\n            return true;\n        }\n        if (logError) {\n            fprintf(stderr, \"[data-dir] failed to create %s: %s\\n\", path.c_str(),\n                    ec.message().c_str());\n        }\n        return false;\n    }\n\n    fprintf(stderr, \"[data-dir] created %s\\n\", path.c_str());\n    return true;\n}\n\nbool DataDirectory::ensureAll() {\n    fprintf(stderr, \"[data-dir] initializing SD card directories\\n\");\n\n    bool allOk = true;\n\n    // DeskNX config directories (title override)\n    if (!createDirectory(configDir()))\n        allOk = false;\n\n    // DeskNX main folder\n    if (!createDirectory(desknxDir()))\n        allOk = false;\n\n    // SteamGridDB cache\n    if (!createDirectory(steamGridDbDir()))\n        allOk = false;\n\n    // Atmosphere base (may not exist on fresh install, but we try anyway)\n    createDirectory(atmosphereDir(), false);\n\n    // Atmosphere contents (where games are installed)\n    createDirectory(contentsDir(), false);\n\n    fprintf(stderr, \"[data-dir] initialization %s\\n\", allOk ? \"complete\" : \"had issues\");\n    return allOk;\n}\n\nbool DataDirectory::isDefaultLauncher() {\n    std::ifstream markerFile(kDefaultLauncherMarker);\n    return markerFile.good();\n}\n\nvoid DataDirectory::toggleDefaultLauncher() {\n    if (isDefaultLauncher()) {\n        // Disable: remove marker\n        if (std::remove(kDefaultLauncherMarker) == 0) {\n            fprintf(stderr, \"[data-dir] default launcher registration disabled\\n\");\n        } else {\n            fprintf(stderr, \"[data-dir] failed to remove marker file\\n\");\n        }\n    } else {\n        // Enable: create marker\n        std::ofstream markerFile(kDefaultLauncherMarker);\n        if (markerFile.is_open()) {\n            markerFile << \"DeskNX is registered as the default launcher\\n\";\n            markerFile.close();\n            fprintf(stderr, \"[data-dir] default launcher registration enabled\\n\");\n        } else {\n            fprintf(stderr, \"[data-dir] failed to create marker file\\n\");\n        }\n    }\n}\n\n}  // namespace desknx::data\n"}