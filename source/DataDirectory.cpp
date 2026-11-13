#include "DataDirectory.hpp"

#include <filesystem>
#include <system_error>
#include <cstdio>

namespace desknx::data {

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

}  // namespace desknx::data

{"content":"#include \"DataDirectory.hpp\"\n\n#include <filesystem>\n#include <system_error>\n#include <cstdio>\n\nnamespace desknx::data {\n\nbool DataDirectory::createDirectory(const std::string& path, bool logError) {\n    std::error_code ec;\n    std::filesystem::create_directories(path, ec);\n\n    if (ec && ec.value() != 0) {\n        // EEXIST is okay (directory already exists)\n        if (ec.value() == EEXIST) {\n            fprintf(stderr, \"[data-dir] %s already exists (ok)\\n\", path.c_str());\n            return true;\n        }\n        if (logError) {\n            fprintf(stderr, \"[data-dir] failed to create %s: %s\\n\", path.c_str(),\n                    ec.message().c_str());\n        }\n        return false;\n    }\n\n    fprintf(stderr, \"[data-dir] created %s\\n\", path.c_str());\n    return true;\n}\n\nbool DataDirectory::ensureAll() {\n    fprintf(stderr, \"[data-dir] initializing SD card directories\\n\");\n\n    bool allOk = true;\n\n    // DeskNX config directories (title override)\n    if (!createDirectory(configDir()))\n        allOk = false;\n\n    // DeskNX main folder\n    if (!createDirectory(desknxDir()))\n        allOk = false;\n\n    // SteamGridDB cache\n    if (!createDirectory(steamGridDbDir()))\n        allOk = false;\n\n    // Atmosphere base (may not exist on fresh install, but we try anyway)\n    createDirectory(atmosphereDir(), false);\n\n    // Atmosphere contents (where games are installed)\n    createDirectory(contentsDir(), false);\n\n    fprintf(stderr, \"[data-dir] initialization %s\\n\", allOk ? \"complete\" : \"had issues\");\n    return allOk;\n}\n\n}  // namespace desknx::data\n"}