#pragma once

#include <string>
#include <vector>

namespace desknx::data {

// Ensures all required SD card directories exist on first boot
class DataDirectory {
public:
    DataDirectory() = default;
    ~DataDirectory() = default;

    // Create all required directory structure
    bool ensureAll();

    // Check if DeskNX is registered as the default launcher
    static bool isDefaultLauncher();

    // Toggle DeskNX as the default launcher
    static void toggleDefaultLauncher();

    // Get standard directory paths
    static std::string configDir() { return "sdmc:/01000000/00001000"; }
    static std::string desknxDir() { return "sdmc:/switch/DeskNX"; }
    static std::string steamGridDbDir() { return "sdmc:/switch/DeskNX/steamgriddb"; }
    static std::string atmosphereDir() { return "sdmc:/atmosphere"; }
    static std::string contentsDir() { return "sdmc:/atmosphere/contents"; }

private:
    bool createDirectory(const std::string& path, bool logError = true);
};

}  // namespace desknx::data
