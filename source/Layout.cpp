#include "../include/desknx/Layout.hpp"
#include "../include/desknx/WorkspaceOps.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <utility>
#include <vector>

namespace desknx::layout {
namespace {

bool parseInteger(const std::string& text, int& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || !end || *end != '\0' ||
        parsed < -2147483647L - 1 || parsed > 2147483647L) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool validKindCode(const std::string& code) {
    if (code.empty() || code.size() > 16) return false;
    for (unsigned char c : code) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

int nearestAllowedDimension(int w, int h, bool wantWidth) {
    const std::array<std::pair<int,int>,5> allowed{{{1,1},{2,1},{3,1},{2,2},{3,2}}};
    size_t best = 0;
    int bestDistance = 1000;
    for (size_t i=0;i<allowed.size();++i) {
        const int distance = std::abs(allowed[i].first - w) + std::abs(allowed[i].second - h);
        if (distance < bestDistance) { bestDistance = distance; best = i; }
    }
    return wantWidth ? allowed[best].first : allowed[best].second;
}

bool hasRecoveredWorkspace(const std::vector<model::Workspace>& workspaces, size_t& index) {
    for (size_t i=0;i<workspaces.size();++i) {
        if (workspaces[i].name == "Recovered") { index = i; return true; }
    }
    return false;
}

} // namespace

bool parse(const std::string& input, LoadResult& result) {
    constexpr size_t MaxInputBytes = 1024 * 1024;
    constexpr size_t MaxLines = 4096;
    constexpr size_t MaxLineBytes = 2048;
    constexpr size_t MaxTitleBytes = 512;
    constexpr size_t MaxDataBytes = 512;
    if (input.empty() || input.size() > MaxInputBytes) return false;

    LoadResult parsed{};
    size_t lineStart = 0;
    size_t lineNumber = 0;
    bool sawHeader = false;
    bool sawActive = false;
    bool hasActiveField = false;
    bool structuralError = false;

    while (lineStart <= input.size()) {
        const size_t newline = input.find('\n', lineStart);
        const size_t lineEnd = newline == std::string::npos ? input.size() : newline;
        std::string line = input.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lineStart = newline == std::string::npos ? input.size() + 1 : newline + 1;
        ++lineNumber;
        if (lineNumber > MaxLines || line.size() > MaxLineBytes) return false;
        if (line.empty()) {
            if (newline == std::string::npos) break;
            continue;
        }

        if (!sawHeader) {
            if (line != "DESKNX1" && line != "DESKNX2" && line != "DESKNX3")
                return false;
            sawHeader = true;
            hasActiveField = line != "DESKNX1";
            parsed.needsMigration = line != "DESKNX3";
            continue;
        }

        const auto fields = model::splitTabs(line);
        if (fields.empty()) return false;
        if (fields[0] == "A") {
            if (!hasActiveField || sawActive || fields.size() != 2 ||
                !parseInteger(fields[1], parsed.activeWorkspace) ||
                parsed.activeWorkspace < 0) {
                structuralError = true;
                break;
            }
            sawActive = true;
        } else if (fields[0] == "D") {
            if (fields.size() != 2 ||
                !desknx::workspace_ops::isValidWorkspaceName(fields[1]) ||
                parsed.workspaces.size() >= static_cast<size_t>(model::MaxDesks)) {
                structuralError = true;
                break;
            }
            parsed.workspaces.push_back({model::safeField(fields[1]), {}});
        } else if (fields[0] == "I") {
            if (fields.size() != 10) {
                structuralError = true;
                break;
            }
            int desk = 0, x = 0, y = 0, w = 0, h = 0, color = 0;
            if (!parseInteger(fields[1], desk) || desk < 0 ||
                static_cast<size_t>(desk) >= parsed.workspaces.size() ||
                !validKindCode(fields[2]) ||
                fields[3].size() > MaxTitleBytes || fields[4].size() > MaxDataBytes ||
                fields[3].find('\0') != std::string::npos ||
                fields[4].find('\0') != std::string::npos ||
                !parseInteger(fields[5], x) || !parseInteger(fields[6], y) ||
                !parseInteger(fields[7], w) || !parseInteger(fields[8], h) ||
                !parseInteger(fields[9], color)) {
                structuralError = true;
                break;
            }
            model::Tile tile{};
            tile.kind = model::parseKind(fields[2]);
            if (tile.kind == model::Kind::Unsupported) tile.unknownTypeCode = fields[2];
            tile.title = model::safeField(fields[3]);
            tile.data = model::safeField(fields[4]);
            tile.x = x; tile.y = y;
            tile.w = std::max(1, std::min(3, w));
            tile.h = std::max(1, std::min(2, h));
            tile.color = std::max(0, std::min(7, color));

            // Normalize the dimensions to one of the four supported tile shapes.
            const int normalizedW = nearestAllowedDimension(tile.w, tile.h, true);
            const int normalizedH = nearestAllowedDimension(tile.w, tile.h, false);
            tile.w = normalizedW;
            tile.h = normalizedH;

            auto& targetDesk = parsed.workspaces[static_cast<size_t>(desk)];
            int nx = tile.x, ny = tile.y;
            bool placed = model::placeable(targetDesk, tile, nx, ny);
            if (!placed) placed = model::firstFree(targetDesk, tile, nx, ny);
            if (placed) {
                tile.x = nx;
                tile.y = ny;
                targetDesk.tiles.push_back(std::move(tile));
                if (nx != x || ny != y || normalizedW != w || normalizedH != h || color < 0 || color > 7)
                    ++parsed.recoveredTiles;
            } else {
                size_t recoveredIndex = 0;
                if (!hasRecoveredWorkspace(parsed.workspaces, recoveredIndex) &&
                    parsed.workspaces.size() < static_cast<size_t>(model::MaxDesks)) {
                    parsed.workspaces.push_back({"Recovered", {}});
                    recoveredIndex = parsed.workspaces.size() - 1;
                }
                if (hasRecoveredWorkspace(parsed.workspaces, recoveredIndex) &&
                    model::firstFree(parsed.workspaces[recoveredIndex], tile, nx, ny)) {
                    tile.x = nx;
                    tile.y = ny;
                    parsed.workspaces[recoveredIndex].tiles.push_back(std::move(tile));
                    ++parsed.recoveredTiles;
                } else {
                    ++parsed.droppedTiles;
                }
            }
        } else {
            structuralError = true;
            break;
        }
    }

    if (!sawHeader || structuralError || parsed.workspaces.empty()) return false;
    if (hasActiveField && !sawActive) parsed.activeWorkspace = 0;
    if (parsed.activeWorkspace >= static_cast<int>(parsed.workspaces.size()))
        parsed.activeWorkspace = static_cast<int>(parsed.workspaces.size()) - 1;
    result = std::move(parsed);
    return true;
}

std::string serialize(const std::vector<model::Workspace>& workspaces, int activeWorkspace) {
    if (workspaces.empty() || workspaces.size() > static_cast<size_t>(model::MaxDesks))
        return {};
    for (const auto& workspace : workspaces)
        if (!desknx::workspace_ops::isValidWorkspaceName(workspace.name)) return {};
    activeWorkspace = std::max(0, std::min(activeWorkspace, static_cast<int>(workspaces.size()) - 1));
    std::string output = "DESKNX3\nA\t" + std::to_string(activeWorkspace) + "\n";
    for (const auto& workspace : workspaces)
        output += "D\t" + model::safeField(workspace.name) + "\n";
    for (size_t desk = 0; desk < workspaces.size(); ++desk) {
        const auto& workspace = workspaces[desk];
        for (size_t tileIndex = 0; tileIndex < workspace.tiles.size(); ++tileIndex) {
            const auto& tile = workspace.tiles[tileIndex];
            // Skip the current tile while checking collisions, but still validate
            // its dimensions/bounds and ensure it does not overlap its siblings.
            if (!model::placeable(workspace, tile, tile.x, tile.y,
                                  static_cast<int>(tileIndex))) return {};
            output += "I\t" + std::to_string(desk) + "\t" + model::typeCode(tile) + "\t" +
                      model::safeField(tile.title) + "\t" + model::safeField(tile.data) + "\t" +
                      std::to_string(tile.x) + "\t" + std::to_string(tile.y) + "\t" +
                      std::to_string(tile.w) + "\t" + std::to_string(tile.h) + "\t" +
                      std::to_string(tile.color) + "\n";
        }
    }
    return output;
}

} // namespace desknx::layout
