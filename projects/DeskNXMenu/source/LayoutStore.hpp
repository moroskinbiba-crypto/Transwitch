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
