#pragma once

#include <desknx/Model.hpp>
#include <desknx/HomeNXWorkspaceProvider.hpp>
#include <string>
#include <vector>

namespace desknx::menu::storage {

// Persists workspace layouts to SD card in TSV format.
// Atomic writes: temp file → commit → rename to final location.
class LayoutStore {
public:
    static constexpr const char* kLayoutPath = "sdmc:/switch/DeskNX/layout.tsv";
    static constexpr const char* kTempPath = "sdmc:/switch/DeskNX/layout.tmp";
    static constexpr const char* kBackupPath = "sdmc:/switch/DeskNX/layout.bak";
    
    /// Load layout from SD card. Returns false if no file exists (not an error).
    static bool load(homenx::WorkspaceProvider& provider) {
        std::string serialized;
        if (!readFile(kLayoutPath, serialized)) {
            return false; // No file yet; will use default workspace.
        }
        return provider.loadSerialized(serialized);
    }
    
    /// Save layout to SD card atomically.
    static bool save(const homenx::WorkspaceProvider& provider) {
        std::string serialized = provider.serializeLayout();
        if (serialized.empty()) return false;
        
        // Write to temp file
        if (!writeFile(kTempPath, serialized)) return false;
        
        // Check if original exists
        bool hadOriginal = fileExists(kLayoutPath);
        
        if (hadOriginal) {
            // Backup original
            deleteFile(kBackupPath);
            if (!renameFile(kLayoutPath, kBackupPath)) return false;
        }
        
        // Rename temp to final
        if (!renameFile(kTempPath, kLayoutPath)) {
            // Restore backup on failure
            if (hadOriginal) renameFile(kBackupPath, kLayoutPath);
            return false;
        }
        
        // Clean up backup
        if (hadOriginal) deleteFile(kBackupPath);
        
        return true;
    }
    
private:
    static bool fileExists(const std::string& path);
    static bool readFile(const std::string& path, std::string& out);
    static bool writeFile(const std::string& path, const std::string& data);
    static bool renameFile(const std::string& from, const std::string& to);
    static bool deleteFile(const std::string& path);
};

} // namespace desknx::menu::storage

{"content":"#pragma once\n\n#include <desknx/Model.hpp>\n#include <desknx/HomeNXWorkspaceProvider.hpp>\n#include <string>\n#include <vector>\n\nnamespace desknx::menu::storage {\n\n// Persists workspace layouts to SD card in TSV format.\n// Atomic writes: temp file → commit → rename to final location.\nclass LayoutStore {\npublic:\n    static constexpr const char* kLayoutPath = \"sdmc:/switch/DeskNX/layout.tsv\";\n    static constexpr const char* kTempPath = \"sdmc:/switch/DeskNX/layout.tmp\";\n    static constexpr const char* kBackupPath = \"sdmc:/switch/DeskNX/layout.bak\";\n    \n    /// Load layout from SD card. Returns false if no file exists (not an error).\n    static bool load(homenx::WorkspaceProvider& provider) {\n        std::string serialized;\n        if (!readFile(kLayoutPath, serialized)) {\n            return false; // No file yet; will use default workspace.\n        }\n        return provider.loadSerialized(serialized);\n    }\n    \n    /// Save layout to SD card atomically.\n    static bool save(const homenx::WorkspaceProvider& provider) {\n        std::string serialized = provider.serializeLayout();\n        if (serialized.empty()) return false;\n        \n        // Write to temp file\n        if (!writeFile(kTempPath, serialized)) return false;\n        \n        // Check if original exists\n        bool hadOriginal = fileExists(kLayoutPath);\n        \n        if (hadOriginal) {\n            // Backup original\n            deleteFile(kBackupPath);\n            if (!renameFile(kLayoutPath, kBackupPath)) return false;\n        }\n        \n        // Rename temp to final\n        if (!renameFile(kTempPath, kLayoutPath)) {\n            // Restore backup on failure\n            if (hadOriginal) renameFile(kBackupPath, kLayoutPath);\n            return false;\n        }\n        \n        // Clean up backup\n        if (hadOriginal) deleteFile(kBackupPath);\n        \n        return true;\n    }\n    \nprivate:\n    static bool fileExists(const std::string& path);\n    static bool readFile(const std::string& path, std::string& out);\n    static bool writeFile(const std::string& path, const std::string& data);\n    static bool renameFile(const std::string& from, const std::string& to);\n    static bool deleteFile(const std::string& path);\n};\n\n} // namespace desknx::menu::storage\n"}