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

{"content":"#pragma once\n\n#include <string>\n#include <vector>\n\nnamespace desknx::data {\n\n// Ensures all required SD card directories exist on first boot\nclass DataDirectory {\npublic:\n    DataDirectory() = default;\n    ~DataDirectory() = default;\n\n    // Create all required directory structure\n    bool ensureAll();\n\n    // Check if DeskNX is registered as the default launcher\n    static bool isDefaultLauncher();\n\n    // Toggle DeskNX as the default launcher\n    static void toggleDefaultLauncher();\n\n    // Get standard directory paths\n    static std::string configDir() { return \"sdmc:/01000000/00001000\"; }\n    static std::string desknxDir() { return \"sdmc:/switch/DeskNX\"; }\n    static std::string steamGridDbDir() { return \"sdmc:/switch/DeskNX/steamgriddb\"; }\n    static std::string atmosphereDir() { return \"sdmc:/atmosphere\"; }\n    static std::string contentsDir() { return \"sdmc:/atmosphere/contents\"; }\n\nprivate:\n    bool createDirectory(const std::string& path, bool logError = true);\n};\n\n}  // namespace desknx::data\n"}