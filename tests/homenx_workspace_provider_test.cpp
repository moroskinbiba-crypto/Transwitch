#include <desknx/HomeNXWorkspaceProvider.hpp>

#include <cassert>
#include <string>
#include <vector>

using namespace desknx::model;
using namespace desknx::homenx;

int main() {
    Workspace games{"Games", {}};
    games.tiles.push_back(Tile{
        Kind::Game, "Installed game", "0100000000001000", 0, 0, 2, 2, 0
    });
    Workspace homebrew{"Homebrew", {}};
    homebrew.backgroundPath = "sdmc:/switch/DeskNX/media/homebrew.png";
    homebrew.tiles.push_back(Tile{
        Kind::Nro, "Tool", "sdmc:/switch/apps/tool.nro", 4, 2, 2, 1, 1
    });

    WorkspaceProvider provider;
    assert(provider.load({games, homebrew}, 1));
    assert(provider.count() == 2);
    assert(provider.activeIndex() == 1);
    assert(provider.activeWorkspace()->name == "Homebrew");

    const std::string savedLayout = provider.serializeLayout();
    const std::string savedActive = provider.serializeActiveState();
    assert(!savedLayout.empty() && !savedActive.empty());

    WorkspaceProvider restored;
    assert(restored.loadSerialized(savedLayout, savedActive));
    assert(restored.count() == 2);
    assert(restored.activeIndex() == 1);
    assert(restored.activeWorkspace()->tiles[0].data ==
           "sdmc:/switch/apps/tool.nro");

    // A stale/corrupt active-state sidecar falls back to the active index
    // included in the main layout instead of making a valid layout unloadable.
    WorkspaceProvider sidecarRecovery;
    assert(sidecarRecovery.loadSerialized(savedLayout, "not-a-valid-sidecar"));
    assert(sidecarRecovery.activeIndex() == 1);

    // Main state parsing is transactional. A broken replacement cannot
    // destroy the previous in-memory workspace collection.
    assert(!restored.loadSerialized("DESKNX-not-valid\n"));
    assert(restored.count() == 2);
    assert(restored.activeWorkspace()->name == "Homebrew");

    assert(restored.previous());
    assert(restored.activeWorkspace()->name == "Games");
    assert(restored.next());
    assert(restored.activeWorkspace()->name == "Homebrew");
    assert(!restored.switchTo(-1));
    assert(!restored.switchTo(2));

    assert(restored.addWorkspace("Tools"));
    assert(restored.count() == 3);
    assert(restored.activeWorkspace()->name == "Tools");
    assert(!restored.addWorkspace("Tools")); // duplicate names are ambiguous
    assert(!restored.addWorkspace(""));
    assert(restored.renameWorkspace(2, "Emulators"));
    assert(!restored.renameWorkspace(2, "Games"));
    assert(restored.renameWorkspace(2, "Utilities"));

    int placedX = -1, placedY = -1;
    assert(restored.placeTileAtFirstFree(
        Tile{Kind::Clock, "Clock", "", 0, 0, 1, 1, 2}, placedX, placedY));
    assert(placedX == 0 && placedY == 0);
    assert(!restored.placeTile(
        Tile{Kind::Battery, "Battery", "", 0, 0, 1, 1, 1}, 0, 0));

    WorkspaceProjection projection;
    assert(restored.projectActive(projection));
    assert(projection.name == "Utilities");
    assert(projection.entries.size() == 1);
    assert(projection.entries[0].kind == EntryKind::ClockWidget);

    // Moving an item into a workspace places it at its previous coordinates
    // when possible, and changes the active workspace only after success.
    WorkspaceProvider moving;
    assert(moving.load({games, homebrew}, 0));
    assert(moving.moveTileToWorkspace(0, 0, 1, placedX, placedY));
    assert(moving.activeIndex() == 1);
    assert(placedX == 0 && placedY == 0); // first free cell in Homebrew workspace
    assert(moving.workspaces()[0].tiles.empty());
    assert(moving.workspaces()[1].tiles.size() == 2);
    assert(moving.workspaces()[1].tiles[1].data == "0100000000001000");

    // Deleting active workspace picks the following workspace (or the new
    // last one) and protects the final remaining desktop.
    assert(moving.addWorkspace("Third"));
    assert(moving.activeIndex() == 2);
    assert(moving.deleteWorkspace(2));
    assert(moving.activeIndex() == 1);
    assert(moving.count() == 2);
    assert(moving.deleteWorkspace(0));
    assert(moving.activeIndex() == 0);
    assert(moving.count() == 1);
    assert(!moving.deleteWorkspace(0));

    // A failed move to a completely full workspace leaves source and selection
    // untouched and does not switch the active desktop.
    Workspace full{"Full", {}};
    for (int y = 0; y < Rows; ++y) {
        for (int x = 0; x < Columns; ++x) {
            full.tiles.push_back(Tile{
                Kind::Nro, "Filler", "sdmc:/switch/filler.nro",
                x, y, 1, 1, 0
            });
        }
    }
    Workspace source{"Source", {}};
    source.tiles.push_back(Tile{
        Kind::Game, "Keep", "0100000000002000", 0, 0, 1, 1, 0
    });
    WorkspaceProvider noLoss;
    assert(noLoss.load({source, full}, 0));
    assert(!noLoss.moveTileToWorkspace(0, 0, 1, placedX, placedY));
    assert(noLoss.activeIndex() == 0);
    assert(noLoss.workspaces()[0].tiles.size() == 1);
    assert(noLoss.workspaces()[0].tiles[0].title == "Keep");

    // Bound the collection at the documented maximum.
    WorkspaceProvider maximum;
    for (int i = 1; i < MaxDesks; ++i)
        assert(maximum.addWorkspace("Desktop " + std::to_string(i + 1)));
    assert(maximum.count() == static_cast<std::size_t>(MaxDesks));
    assert(!maximum.addWorkspace("Overflow"));

    return 0;
}
