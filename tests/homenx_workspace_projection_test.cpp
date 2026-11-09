#include <desknx/HomeNXWorkspaceAdapter.hpp>

#include <cassert>
#include <cstdint>
#include <string>

using namespace desknx::model;
using namespace desknx::homenx;

int main() {
    Workspace source{"Games", {}};
    source.backgroundPath = "sdmc:/switch/DeskNX/media/backgrounds/night.jpg";
    source.backgroundFit = 1;
    source.artworkMode = 2;
    source.labelOpacity = 177;
    source.accentColor = 4;

    source.tiles.push_back(Tile{
        Kind::Game, "Installed game", "0100000000001000", 0, 0, 2, 2, 3
    });
    source.tiles.push_back(Tile{
        Kind::Nro, "Homebrew", "sdmc:/switch/apps/tool.nro", 2, 0, 3, 1, 2
    });
    source.tiles.push_back(Tile{
        Kind::Clock, "Clock", "", 5, 0, 1, 1, 1
    });
    source.tiles.push_back(Tile{
        Kind::Recent, "Recent launches", "", 0, 2, 3, 1, 5
    });
    source.tiles.push_back(Tile{
        Kind::System, "Settings", "settings", 3, 2, 1, 1, 0
    });

    WorkspaceProjection view;
    ProjectionError error = ProjectionError::InvalidText;
    assert(projectWorkspace(source, view, &error));
    assert(error == ProjectionError::None);
    assert(view.name == source.name);
    assert(view.backgroundPath == source.backgroundPath);
    assert(view.backgroundFit == 1 && view.artworkMode == 2);
    assert(view.labelOpacity == 177 && view.accentColor == 4);
    assert(view.entries.size() == source.tiles.size());

    // 2x2 game occupies cells 0, 1, 6 and 7.
    assert(view.cells[0].role == CellRole::Primary);
    assert(view.cells[0].entryIndex == 0);
    assert(view.cells[1].role == CellRole::Continuation);
    assert(view.cells[6].role == CellRole::Continuation);
    assert(view.cells[7].role == CellRole::Continuation);
    assert(view.entries[0].kind == EntryKind::InstalledTitle);
    assert(view.entries[0].titleId == 0x0100000000001000ULL);

    // 3x1 Homebrew item preserves its path, dimensions and colour.
    assert(view.cells[2].role == CellRole::Primary);
    assert(view.cells[3].role == CellRole::Continuation);
    assert(view.cells[4].role == CellRole::Continuation);
    assert(view.entries[1].kind == EntryKind::HomebrewNro);
    assert(view.entries[1].data == "sdmc:/switch/apps/tool.nro");
    assert(view.entries[1].columns == 3 && view.entries[1].rows == 1);
    assert(view.entries[1].color == 2);

    assert(view.entries[2].kind == EntryKind::ClockWidget);
    assert(view.entries[3].kind == EntryKind::RecentWidget);
    assert(view.entries[4].kind == EntryKind::SystemShortcut);
    assert(view.cells[17].role == CellRole::Empty);
    assert(view.cells[17].entryIndex == -1);

    // Same source item type/data at another location still has a distinct key.
    Workspace duplicate{"Duplicate widgets", {}};
    duplicate.tiles.push_back(Tile{Kind::Clock, "Clock", "", 0, 0, 1, 1, 0});
    duplicate.tiles.push_back(Tile{Kind::Clock, "Clock", "", 1, 0, 1, 1, 0});
    assert(projectWorkspace(duplicate, view));
    assert(view.entries.size() == 2);
    assert(view.entries[0].stableKey != view.entries[1].stableKey);
    const std::string firstWidgetKey = view.entries[0].stableKey;
    duplicate.tiles[0].x = 4; // moving a tile must not change its identity
    assert(projectWorkspace(duplicate, view));
    assert(view.entries[0].stableKey == firstWidgetKey);

    // Invalid title IDs and paths must never reach the HomeNX app model.
    Workspace invalidGame{"Bad game", {}};
    invalidGame.tiles.push_back(Tile{Kind::Game, "Bad", "not-a-title-id", 0, 0, 1, 1, 0});
    assert(!projectWorkspace(invalidGame, view, &error));
    assert(error == ProjectionError::InvalidGameTitleId);
    assert(view.entries.empty());

    Workspace invalidNro{"Bad NRO", {}};
    invalidNro.tiles.push_back(Tile{Kind::Nro, "Traversal", "sdmc:/switch/../boot.nro", 0, 0, 1, 1, 0});
    assert(!projectWorkspace(invalidNro, view, &error));
    assert(error == ProjectionError::InvalidHomebrewPath);

    Workspace invalidExtension{"Bad extension", {}};
    invalidExtension.tiles.push_back(Tile{Kind::Nro, "Not NRO", "sdmc:/switch/apps/readme.txt", 0, 0, 1, 1, 0});
    assert(!projectWorkspace(invalidExtension, view, &error));
    assert(error == ProjectionError::InvalidHomebrewPath);

    // Overlap and off-grid spans are rejected before a GridModel is built.
    Workspace overlap{"Overlap", {}};
    overlap.tiles.push_back(Tile{Kind::Game, "One", "0100000000000001", 0, 0, 2, 1, 0});
    overlap.tiles.push_back(Tile{Kind::Game, "Two", "0100000000000002", 1, 0, 1, 1, 0});
    assert(!projectWorkspace(overlap, view, &error));
    assert(error == ProjectionError::InvalidGeometry);

    Workspace offGrid{"Off grid", {}};
    offGrid.tiles.push_back(Tile{Kind::Game, "Wide", "0100000000000003", 5, 2, 2, 1, 0});
    assert(!projectWorkspace(offGrid, view, &error));
    assert(error == ProjectionError::InvalidGeometry);

    Workspace badAppearance{"Bad appearance", {}};
    badAppearance.backgroundPath = "sdmc:/switch/other/background.jpg";
    assert(!projectWorkspace(badAppearance, view, &error));
    assert(error == ProjectionError::InvalidAppearance);

    Workspace badName{"", {}};
    assert(!projectWorkspace(badName, view, &error));
    assert(error == ProjectionError::InvalidName);

    return 0;
}
