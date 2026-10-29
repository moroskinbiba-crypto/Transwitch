#include "../include/desknx/Layout.hpp"

#include <algorithm>
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
    return code == "G" || code == "N" || code == "C" ||
           code == "B" || code == "S" || code == "R";
}

} // namespace

bool parse(const std::string& input, LoadResult& result) {
    LoadResult parsed{};
    std::vector<std::vector<model::Tile>> pending;
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
            if (fields.size() != 2 || fields[1].empty() ||
                parsed.workspaces.size() >= static_cast<size_t>(model::MaxDesks)) {
                structuralError = true;
                break;
            }
            parsed.workspaces.push_back({fields[1], {}});
            pending.emplace_back();
        } else if (fields[0] == "I") {
            if (fields.size() != 10) {
                structuralError = true;
                break;
            }
            int desk = 0, x = 0, y = 0, w = 0, h = 0, color = 0;
            if (!parseInteger(fields[1], desk) || desk < 0 ||
                static_cast<size_t>(desk) >= parsed.workspaces.size() ||
                !validKindCode(fields[2]) ||
                !parseInteger(fields[5], x) || !parseInteger(fields[6], y) ||
                !parseInteger(fields[7], w) || !parseInteger(fields[8], h) ||
                !parseInteger(fields[9], color) || color < 0 || color > 7) {
                structuralError = true;
                break;
            }
            model::Tile tile{};
            tile.kind = model::parseKind(fields[2]);
            tile.title = fields[3];
            tile.data = fields[4];
            tile.x = x; tile.y = y; tile.w = w; tile.h = h; tile.color = color;
            if (!model::placeable(parsed.workspaces[static_cast<size_t>(desk)],
                                  tile, x, y)) {
                structuralError = true;
                break;
            }
            parsed.workspaces[static_cast<size_t>(desk)].tiles.push_back(std::move(tile));
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
            output += "I\t" + std::to_string(desk) + "\t" + model::typeCode(tile.kind) + "\t" +
                      model::safeField(tile.title) + "\t" + model::safeField(tile.data) + "\t" +
                      std::to_string(tile.x) + "\t" + std::to_string(tile.y) + "\t" +
                      std::to_string(tile.w) + "\t" + std::to_string(tile.h) + "\t" +
                      std::to_string(tile.color) + "\n";
        }
    }
    return output;
}

} // namespace desknx::layout
