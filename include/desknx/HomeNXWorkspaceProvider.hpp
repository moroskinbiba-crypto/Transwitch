#pragma once

#include "HomeNXWorkspaceAdapter.hpp"
#include "Layout.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace desknx::homenx {

// Owns the Android-style workspace collection independently of HomeNX's
// catalogue and paginated GridModel. Persistence is supplied as text so this
// class remains host-testable and the Switch filesystem adapter can stay thin.
class WorkspaceProvider final {
public:
    WorkspaceProvider() {
        m_workspaces.push_back(model::Workspace{"Home", {}});
    }

    bool loadSerialized(const std::string& layoutText,
                        const std::string& activeStateText = {}) {
        layout::LoadResult parsed{};
        if (!layout::parse(layoutText, parsed) || parsed.workspaces.empty() ||
            parsed.workspaces.size() > static_cast<std::size_t>(model::MaxDesks))
            return false;

        int active = parsed.activeWorkspace;
        int sidecarActive = active;
        if (!activeStateText.empty() &&
            layout::parseActiveWorkspaceState(activeStateText, parsed.workspaces,
                                              sidecarActive))
            active = sidecarActive;

        if (active < 0 || active >= static_cast<int>(parsed.workspaces.size()))
            active = 0;

        // Commit only after both the primary layout and active index have
        // passed validation. A malformed load never clears the live session.
        m_workspaces = std::move(parsed.workspaces);
        m_active = active;
        return true;
    }

    bool load(const std::vector<model::Workspace>& workspaces, int activeWorkspace) {
        if (workspaces.empty() ||
            workspaces.size() > static_cast<std::size_t>(model::MaxDesks) ||
            activeWorkspace < 0 ||
            activeWorkspace >= static_cast<int>(workspaces.size()))
            return false;

        const std::string encoded = layout::serialize(workspaces, activeWorkspace);
        if (encoded.empty())
            return false;

        layout::LoadResult validated{};
        if (!layout::parse(encoded, validated))
            return false;

        m_workspaces = std::move(validated.workspaces);
        m_active = validated.activeWorkspace;
        return true;
    }

    const std::vector<model::Workspace>& workspaces() const { return m_workspaces; }
    std::vector<model::Workspace>& workspacesForEditing() { return m_workspaces; }
    int activeIndex() const { return m_active; }
    std::size_t count() const { return m_workspaces.size(); }
    const model::Workspace* activeWorkspace() const {
        return m_active >= 0 && m_active < static_cast<int>(m_workspaces.size())
            ? &m_workspaces[static_cast<std::size_t>(m_active)] : nullptr;
    }
    model::Workspace* activeWorkspaceForEditing() {
        return m_active >= 0 && m_active < static_cast<int>(m_workspaces.size())
            ? &m_workspaces[static_cast<std::size_t>(m_active)] : nullptr;
    }

    std::string serializeLayout() const {
        return layout::serialize(m_workspaces, m_active);
    }

    std::string serializeActiveState() const {
        return layout::serializeActiveWorkspaceState(m_workspaces, m_active);
    }

    bool switchTo(int index) {
        if (index < 0 || index >= static_cast<int>(m_workspaces.size()) ||
            index == m_active)
            return false;
        m_active = index;
        return true;
    }

    bool next() {
        if (m_workspaces.size() < 2)
            return false;
        m_active = (m_active + 1) % static_cast<int>(m_workspaces.size());
        return true;
    }

    bool previous() {
        if (m_workspaces.size() < 2)
            return false;
        m_active = (m_active + static_cast<int>(m_workspaces.size()) - 1) %
                   static_cast<int>(m_workspaces.size());
        return true;
    }

    bool addWorkspace(std::string name) {
        if (m_workspaces.size() >= static_cast<std::size_t>(model::MaxDesks) ||
            !workspace_ops::isValidWorkspaceName(name))
            return false;
        if (std::any_of(m_workspaces.begin(), m_workspaces.end(),
                        [&](const model::Workspace& item) { return item.name == name; }))
            return false;
        m_workspaces.push_back(model::Workspace{std::move(name), {}});
        m_active = static_cast<int>(m_workspaces.size()) - 1;
        return true;
    }

    bool renameWorkspace(int index, std::string name) {
        if (index < 0 || index >= static_cast<int>(m_workspaces.size()) ||
            !workspace_ops::isValidWorkspaceName(name))
            return false;
        for (std::size_t i = 0; i < m_workspaces.size(); ++i) {
            if (static_cast<int>(i) != index && m_workspaces[i].name == name)
                return false;
        }
        m_workspaces[static_cast<std::size_t>(index)].name = std::move(name);
        return true;
    }

    bool deleteWorkspace(int index) {
        return workspace_ops::deleteWorkspace(m_workspaces, m_active, index);
    }

    bool moveTileToWorkspace(int selectedX, int selectedY, int targetWorkspace,
                             int& resultX, int& resultY) {
        int x = selectedX;
        int y = selectedY;
        if (!workspace_ops::moveSelectedTileToWorkspace(
                m_workspaces, m_active, x, y, targetWorkspace))
            return false;
        resultX = x;
        resultY = y;
        return true;
    }

    bool placeTile(model::Tile tile, int x, int y) {
        model::Workspace* workspace = activeWorkspaceForEditing();
        if (!workspace || !model::placeable(*workspace, tile, x, y))
            return false;
        tile.x = x;
        tile.y = y;
        workspace->tiles.push_back(std::move(tile));
        return true;
    }

    bool placeTileAtFirstFree(model::Tile tile, int& resultX, int& resultY) {
        model::Workspace* workspace = activeWorkspaceForEditing();
        if (!workspace || !model::firstFree(*workspace, tile, resultX, resultY))
            return false;
        tile.x = resultX;
        tile.y = resultY;
        workspace->tiles.push_back(std::move(tile));
        return true;
    }

    // Drag an existing tile to another cell of the active desktop. The tile
    // keeps its identity and size; only its origin moves. A refused move leaves
    // the workspace untouched, so the UI can animate a snapped-back drop.
    bool moveTile(std::size_t tileIndex, int x, int y) {
        model::Workspace* workspace = activeWorkspaceForEditing();
        if (!workspace || tileIndex >= workspace->tiles.size())
            return false;

        const model::Tile& tile = workspace->tiles[tileIndex];
        if (tile.x == x && tile.y == y)
            return false;
        if (!model::placeable(*workspace, tile, x, y, static_cast<int>(tileIndex)))
            return false;

        workspace->tiles[tileIndex].x = x;
        workspace->tiles[tileIndex].y = y;
        return true;
    }

    // Resize a tile (corner handle / context menu). Prefers the requested
    // origin; if the tile no longer fits there - expanding over a neighbour -
    // it relocates to the first cell that fits, which is what a user expects
    // from a resize handle that overshoots. Returns false and changes nothing
    // when the shape is unsupported or nothing fits at all.
    bool resizeTile(std::size_t tileIndex, int x, int y, int columns, int rows) {
        model::Workspace* workspace = activeWorkspaceForEditing();
        if (!workspace || tileIndex >= workspace->tiles.size())
            return false;
        if (!model::isSupportedTileSize(columns, rows))
            return false;

        model::Tile candidate = workspace->tiles[tileIndex];
        const model::Tile original = candidate;
        const int ignore = static_cast<int>(tileIndex);
        candidate.w = columns;
        candidate.h = rows;

        int targetX = x;
        int targetY = y;
        if (!model::placeable(*workspace, candidate, targetX, targetY, ignore)) {
            if (!model::firstFree(*workspace, candidate, targetX, targetY))
                return false;
        }

        if (targetX == original.x && targetY == original.y &&
            columns == original.w && rows == original.h) {
            return false;
        }

        workspace->tiles[tileIndex].x = targetX;
        workspace->tiles[tileIndex].y = targetY;
        workspace->tiles[tileIndex].w = columns;
        workspace->tiles[tileIndex].h = rows;
        return true;
    }

    // Removes a tile from the active desktop ("remove from Home" / the delete
    // badge in edit mode). The tile identity is the index into the vector the
    // caller just rendered, so an out-of-range index is a stale UI, not a crash.
    bool removeTile(std::size_t tileIndex) {
        model::Workspace* workspace = activeWorkspaceForEditing();
        if (!workspace || tileIndex >= workspace->tiles.size())
            return false;
        workspace->tiles.erase(workspace->tiles.begin() + static_cast<std::ptrdiff_t>(tileIndex));
        return true;
    }

    // Removes the tile that occupies a cell; used by touch "long-press > delete"
    // where the gesture reports coordinates rather than an index.
    bool removeTileAt(int x, int y) {
        model::Workspace* workspace = activeWorkspaceForEditing();
        if (!workspace)
            return false;
        const int index = model::itemAt(*workspace, x, y);
        if (index < 0)
            return false;
        return removeTile(static_cast<std::size_t>(index));
    }

    bool projectActive(WorkspaceProjection& output,
                       ProjectionError* error = nullptr) const {
        const model::Workspace* workspace = activeWorkspace();
        if (!workspace) {
            if (error) *error = ProjectionError::InvalidName;
            output = WorkspaceProjection{};
            return false;
        }
        return projectWorkspace(*workspace, output, error);
    }

private:
    std::vector<model::Workspace> m_workspaces;
    int m_active{0};
};

} // namespace desknx::homenx

{"content":"#pragma once\r\n\r\n#include \"HomeNXWorkspaceAdapter.hpp\"\r\n#include \"Layout.hpp\"\r\n\r\n#include <algorithm>\r\n#include <string>\r\n#include <utility>\r\n#include <vector>\r\n\r\nnamespace desknx::homenx {\r\n\r\n// Owns the Android-style workspace collection independently of HomeNX's\r\n// catalogue and paginated GridModel. Persistence is supplied as text so this\r\n// class remains host-testable and the Switch filesystem adapter can stay thin.\r\nclass WorkspaceProvider final {\r\npublic:\r\n    WorkspaceProvider() {\r\n        m_workspaces.push_back(model::Workspace{\"Home\", {}});\r\n    }\r\n\r\n    bool loadSerialized(const std::string& layoutText,\r\n                        const std::string& activeStateText = {}) {\r\n        layout::LoadResult parsed{};\r\n        if (!layout::parse(layoutText, parsed) || parsed.workspaces.empty() ||\r\n            parsed.workspaces.size() > static_cast<std::size_t>(model::MaxDesks))\r\n            return false;\r\n\r\n        int active = parsed.activeWorkspace;\r\n        int sidecarActive = active;\r\n        if (!activeStateText.empty() &&\r\n            layout::parseActiveWorkspaceState(activeStateText, parsed.workspaces,\r\n                                              sidecarActive))\r\n            active = sidecarActive;\r\n\r\n        if (active < 0 || active >= static_cast<int>(parsed.workspaces.size()))\r\n            active = 0;\r\n\r\n        // Commit only after both the primary layout and active index have\r\n        // passed validation. A malformed load never clears the live session.\r\n        m_workspaces = std::move(parsed.workspaces);\r\n        m_active = active;\r\n        return true;\r\n    }\r\n\r\n    bool load(const std::vector<model::Workspace>& workspaces, int activeWorkspace) {\r\n        if (workspaces.empty() ||\r\n            workspaces.size() > static_cast<std::size_t>(model::MaxDesks) ||\r\n            activeWorkspace < 0 ||\r\n            activeWorkspace >= static_cast<int>(workspaces.size()))\r\n            return false;\r\n\r\n        const std::string encoded = layout::serialize(workspaces, activeWorkspace);\r\n        if (encoded.empty())\r\n            return false;\r\n\r\n        layout::LoadResult validated{};\r\n        if (!layout::parse(encoded, validated))\r\n            return false;\r\n\r\n        m_workspaces = std::move(validated.workspaces);\r\n        m_active = validated.activeWorkspace;\r\n        return true;\r\n    }\r\n\r\n    const std::vector<model::Workspace>& workspaces() const { return m_workspaces; }\r\n    std::vector<model::Workspace>& workspacesForEditing() { return m_workspaces; }\r\n    int activeIndex() const { return m_active; }\r\n    std::size_t count() const { return m_workspaces.size(); }\r\n    const model::Workspace* activeWorkspace() const {\r\n        return m_active >= 0 && m_active < static_cast<int>(m_workspaces.size())\r\n            ? &m_workspaces[static_cast<std::size_t>(m_active)] : nullptr;\r\n    }\r\n    model::Workspace* activeWorkspaceForEditing() {\r\n        return m_active >= 0 && m_active < static_cast<int>(m_workspaces.size())\r\n            ? &m_workspaces[static_cast<std::size_t>(m_active)] : nullptr;\r\n    }\r\n\r\n    std::string serializeLayout() const {\r\n        return layout::serialize(m_workspaces, m_active);\r\n    }\r\n\r\n    std::string serializeActiveState() const {\r\n        return layout::serializeActiveWorkspaceState(m_workspaces, m_active);\r\n    }\r\n\r\n    bool switchTo(int index) {\r\n        if (index < 0 || index >= static_cast<int>(m_workspaces.size()) ||\r\n            index == m_active)\r\n            return false;\r\n        m_active = index;\r\n        return true;\r\n    }\r\n\r\n    bool next() {\r\n        if (m_workspaces.size() < 2)\r\n            return false;\r\n        m_active = (m_active + 1) % static_cast<int>(m_workspaces.size());\r\n        return true;\r\n    }\r\n\r\n    bool previous() {\r\n        if (m_workspaces.size() < 2)\r\n            return false;\r\n        m_active = (m_active + static_cast<int>(m_workspaces.size()) - 1) %\r\n                   static_cast<int>(m_workspaces.size());\r\n        return true;\r\n    }\r\n\r\n    bool addWorkspace(std::string name) {\r\n        if (m_workspaces.size() >= static_cast<std::size_t>(model::MaxDesks) ||\r\n            !workspace_ops::isValidWorkspaceName(name))\r\n            return false;\r\n        if (std::any_of(m_workspaces.begin(), m_workspaces.end(),\r\n                        [&](const model::Workspace& item) { return item.name == name; }))\r\n            return false;\r\n        m_workspaces.push_back(model::Workspace{std::move(name), {}});\r\n        m_active = static_cast<int>(m_workspaces.size()) - 1;\r\n        return true;\r\n    }\r\n\r\n    bool renameWorkspace(int index, std::string name) {\r\n        if (index < 0 || index >= static_cast<int>(m_workspaces.size()) ||\r\n            !workspace_ops::isValidWorkspaceName(name))\r\n            return false;\r\n        for (std::size_t i = 0; i < m_workspaces.size(); ++i) {\r\n            if (static_cast<int>(i) != index && m_workspaces[i].name == name)\r\n                return false;\r\n        }\r\n        m_workspaces[static_cast<std::size_t>(index)].name = std::move(name);\r\n        return true;\r\n    }\r\n\r\n    bool deleteWorkspace(int index) {\r\n        return workspace_ops::deleteWorkspace(m_workspaces, m_active, index);\r\n    }\r\n\r\n    bool moveTileToWorkspace(int selectedX, int selectedY, int targetWorkspace,\r\n                             int& resultX, int& resultY) {\r\n        int x = selectedX;\r\n        int y = selectedY;\r\n        if (!workspace_ops::moveSelectedTileToWorkspace(\r\n                m_workspaces, m_active, x, y, targetWorkspace))\r\n            return false;\r\n        resultX = x;\r\n        resultY = y;\r\n        return true;\r\n    }\r\n\r\n    bool placeTile(model::Tile tile, int x, int y) {\r\n        model::Workspace* workspace = activeWorkspaceForEditing();\r\n        if (!workspace || !model::placeable(*workspace, tile, x, y))\r\n            return false;\r\n        tile.x = x;\r\n        tile.y = y;\r\n        workspace->tiles.push_back(std::move(tile));\r\n        return true;\r\n    }\r\n\r\n    bool placeTileAtFirstFree(model::Tile tile, int& resultX, int& resultY) {\r\n        model::Workspace* workspace = activeWorkspaceForEditing();\r\n        if (!workspace || !model::firstFree(*workspace, tile, resultX, resultY))\r\n            return false;\r\n        tile.x = resultX;\r\n        tile.y = resultY;\r\n        workspace->tiles.push_back(std::move(tile));\r\n        return true;\r\n    }\r\n\r\n    // Drag an existing tile to another cell of the active desktop. The tile\r\n    // keeps its identity and size; only its origin moves. A refused move leaves\r\n    // the workspace untouched, so the UI can animate a snapped-back drop.\r\n    bool moveTile(std::size_t tileIndex, int x, int y) {\r\n        model::Workspace* workspace = activeWorkspaceForEditing();\r\n        if (!workspace || tileIndex >= workspace->tiles.size())\r\n            return false;\r\n\r\n        const model::Tile& tile = workspace->tiles[tileIndex];\r\n        if (tile.x == x && tile.y == y)\r\n            return false;\r\n        if (!model::placeable(*workspace, tile, x, y, static_cast<int>(tileIndex)))\r\n            return false;\r\n\r\n        workspace->tiles[tileIndex].x = x;\r\n        workspace->tiles[tileIndex].y = y;\r\n        return true;\r\n    }\r\n\r\n    // Resize a tile (corner handle / context menu). Prefers the requested\r\n    // origin; if the tile no longer fits there - expanding over a neighbour -\r\n    // it relocates to the first cell that fits, which is what a user expects\r\n    // from a resize handle that overshoots. Returns false and changes nothing\r\n    // when the shape is unsupported or nothing fits at all.\r\n    bool resizeTile(std::size_t tileIndex, int x, int y, int columns, int rows) {\r\n        model::Workspace* workspace = activeWorkspaceForEditing();\r\n        if (!workspace || tileIndex >= workspace->tiles.size())\r\n            return false;\r\n        if (!model::isSupportedTileSize(columns, rows))\r\n            return false;\r\n\r\n        model::Tile candidate = workspace->tiles[tileIndex];\r\n        const model::Tile original = candidate;\r\n        const int ignore = static_cast<int>(tileIndex);\r\n        candidate.w = columns;\r\n        candidate.h = rows;\r\n\r\n        int targetX = x;\r\n        int targetY = y;\r\n        if (!model::placeable(*workspace, candidate, targetX, targetY, ignore)) {\r\n            if (!model::firstFree(*workspace, candidate, targetX, targetY))\r\n                return false;\r\n        }\r\n\r\n        if (targetX == original.x && targetY == original.y &&\r\n            columns == original.w && rows == original.h) {\r\n            return false;\r\n        }\r\n\r\n        workspace->tiles[tileIndex].x = targetX;\r\n        workspace->tiles[tileIndex].y = targetY;\r\n        workspace->tiles[tileIndex].w = columns;\r\n        workspace->tiles[tileIndex].h = rows;\r\n        return true;\r\n    }\r\n\r\n    // Removes a tile from the active desktop (\"remove from Home\" / the delete\r\n    // badge in edit mode). The tile identity is the index into the vector the\r\n    // caller just rendered, so an out-of-range index is a stale UI, not a crash.\r\n    bool removeTile(std::size_t tileIndex) {\r\n        model::Workspace* workspace = activeWorkspaceForEditing();\r\n        if (!workspace || tileIndex >= workspace->tiles.size())\r\n            return false;\r\n        workspace->tiles.erase(workspace->tiles.begin() + static_cast<std::ptrdiff_t>(tileIndex));\r\n        return true;\r\n    }\r\n\r\n    // Removes the tile that occupies a cell; used by touch \"long-press > delete\"\r\n    // where the gesture reports coordinates rather than an index.\r\n    bool removeTileAt(int x, int y) {\r\n        model::Workspace* workspace = activeWorkspaceForEditing();\r\n        if (!workspace)\r\n            return false;\r\n        const int index = model::itemAt(*workspace, x, y);\r\n        if (index < 0)\r\n            return false;\r\n        return removeTile(static_cast<std::size_t>(index));\r\n    }\r\n\r\n    bool projectActive(WorkspaceProjection& output,\r\n                       ProjectionError* error = nullptr) const {\r\n        const model::Workspace* workspace = activeWorkspace();\r\n        if (!workspace) {\r\n            if (error) *error = ProjectionError::InvalidName;\r\n            output = WorkspaceProjection{};\r\n            return false;\r\n        }\r\n        return projectWorkspace(*workspace, output, error);\r\n    }\r\n\r\nprivate:\r\n    std::vector<model::Workspace> m_workspaces;\r\n    int m_active{0};\r\n};\r\n\r\n} // namespace desknx::homenx\r\n"}