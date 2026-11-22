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
#if defined(DT_DIR) && defined(DT_REG)
    dirent dirEntry{};dirEntry.d_type=DT_DIR;
    dirent fileEntry{};fileEntry.d_type=DT_REG;
    assert(desknx::nro_scan::classifyEntryType(dirEntry)==desknx::nro_scan::EntryType::Directory);
    assert(desknx::nro_scan::classifyEntryType(fileEntry)==desknx::nro_scan::EntryType::Regular);
#endif
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

    // A directory above the former 8192-entry budget should now be complete.
    const fs::path crowded = root / "crowded";
    fs::create_directories(crowded);
    for (std::size_t i=0; i<8193; ++i)
        writeFile(crowded / ("entry-" + std::to_string(i) + ".txt"));
    report = desknx::nro_scan::Collect(crowded.string(), files);
    assert(report.rootOpened && report.complete && !report.limitReached);
    assert(report.entriesVisited > 8192);
    assert(report.fallbackStatChecks <= report.statChecks);

    // Exactly 512 NROs remain a complete inventory even if an additional
    // empty directory follows them. A real 513th NRO must mark it incomplete.
    const fs::path exactRoot=root/"exact-cap";
    const fs::path exactFiles=exactRoot/"first";
    const fs::path nextDirectory=exactRoot/"second";
    fs::create_directories(exactFiles);
    fs::create_directories(nextDirectory);
    for(std::size_t i=0;i<desknx::nro_scan::MaxFiles;++i)
        writeFile(exactFiles/("game-"+std::to_string(i)+".nro"));
    report=desknx::nro_scan::Collect(exactRoot.string(),files);
    assert(report.complete&&!report.limitReached);
    assert(files.size()==desknx::nro_scan::MaxFiles);
    writeFile(nextDirectory/"over-limit.nro");
    report=desknx::nro_scan::Collect(exactRoot.string(),files);
    assert(!report.complete&&report.limitReached);
    assert(files.size()==desknx::nro_scan::MaxFiles);

    // An unavailable root is not a successful empty scan.
    report = desknx::nro_scan::Collect((root / "missing").string(), files);
    assert(!report.rootOpened && !report.complete);
    assert(files.empty());

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::puts("nro_scanner_test: ok");
    return 0;
}
