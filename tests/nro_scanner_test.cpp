#include <desknx/NroScanner.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

void writeFile(const fs::path& path, const std::string& contents = "nro") {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    assert(output.good());
}

bool hasPath(const std::vector<desknx::nro_scan::File>& files,
             const fs::path& expected) {
    return std::any_of(files.begin(), files.end(), [&](const auto& item) {
        return item.path == expected.string();
    });
}

int main() {
    char templatePath[] = "/tmp/desknx-nro-scan-XXXXXX";
    char* directory = mkdtemp(templatePath);
    assert(directory != nullptr);
    const fs::path root(directory);

    fs::create_directories(root / "apps" / "emulators");
    fs::create_directories(root / "DeskNX");
    fs::create_directories(root / ".hidden");
    writeFile(root / "top.nro", "top-level");
    writeFile(root / "apps" / "demo.NRO", "nested");
    writeFile(root / "apps" / "emulators" / "retro.nro", "deeper");
    writeFile(root / "DeskNX" / "internal.nro");
    writeFile(root / ".hidden" / "hidden.nro");
    writeFile(root / "readme.txt");

    std::vector<desknx::nro_scan::File> files;
    auto report = desknx::nro_scan::Collect(root.string(), files);
    assert(report.rootOpened && report.complete);
    assert(report.filesFound == 3 && files.size() == 3);
    assert(hasPath(files, root / "top.nro"));
    assert(hasPath(files, root / "apps" / "demo.NRO"));
    assert(hasPath(files, root / "apps" / "emulators" / "retro.nro"));
    assert(!hasPath(files, root / "DeskNX" / "internal.nro"));
    assert(!hasPath(files, root / ".hidden" / "hidden.nro"));

    // A valid folder tree deeper than the scan limit is reported as partial,
    // never as a complete empty catalogue that could overwrite the old cache.
    fs::path deep = root;
    for (int i = 0; i <= desknx::nro_scan::MaxDepth; ++i) {
        deep /= "level";
        fs::create_directories(deep);
    }
    writeFile(deep / "too-deep.nro");
    report = desknx::nro_scan::Collect(root.string(), files);
    assert(report.rootOpened);
    assert(!report.complete && report.directoriesBeyondDepth > 0);
    assert(!hasPath(files, deep / "too-deep.nro"));

    // An unavailable root is not a successful empty scan.
    report = desknx::nro_scan::Collect((root / "missing").string(), files);
    assert(!report.rootOpened && !report.complete);
    assert(files.empty());

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::puts("nro_scanner_test: ok");
    return 0;
}
