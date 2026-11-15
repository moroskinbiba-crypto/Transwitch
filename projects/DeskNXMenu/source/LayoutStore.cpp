#include "LayoutStore.hpp"
#include <fstream>
#include <filesystem>
#include <system_error>

namespace desknx::menu::storage {

bool LayoutStore::fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool LayoutStore::readFile(const std::string& path, std::string& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    if (!file.good()) return false;
    
    out = buffer.str();
    return true;
}

bool LayoutStore::writeFile(const std::string& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;
    
    file << data;
    file.flush();
    return static_cast<bool>(file.good());
}

bool LayoutStore::renameFile(const std::string& from, const std::string& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}

bool LayoutStore::deleteFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
}

} // namespace desknx::menu::storage
