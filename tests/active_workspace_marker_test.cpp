#include <desknx/Layout.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using desknx::model::Workspace;

    std::vector<Workspace> workspaces{
        Workspace{"Games", {}},
        Workspace{"Homebrew", {}},
        Workspace{"Widgets", {}}
    };
    workspaces[0].tiles.push_back(
        desknx::model::Tile{desknx::model::Kind::Game,
            "Example", "0100000000000001", 0, 0, 1, 1, 0});

    const std::string shape = desknx::layout::serialize(workspaces, 0);
    assert(!shape.empty());
    const std::uint64_t shapeHash = desknx::model::stableHash(shape);

    // The fast path must produce the exact same validated marker as the legacy
    // full-model path, without requiring a workspace-vector copy.
    const std::string full =
        desknx::layout::serializeActiveWorkspaceState(workspaces, 2);
    const std::string fast =
        desknx::layout::serializeActiveWorkspaceState(
            2, workspaces.size(), shapeHash, "Widgets");
    assert(!full.empty());
    assert(fast == full);

    assert(desknx::layout::serializeActiveWorkspaceState(
        -1, workspaces.size(), shapeHash, "Widgets").empty());
    assert(desknx::layout::serializeActiveWorkspaceState(
        3, workspaces.size(), shapeHash, "Widgets").empty());
    assert(desknx::layout::serializeActiveWorkspaceState(
        0, 0, shapeHash, "Games").empty());
    assert(desknx::layout::serializeActiveWorkspaceState(
        0, static_cast<std::size_t>(desknx::model::MaxDesks + 1),
        shapeHash, "Games").empty());
    assert(desknx::layout::serializeActiveWorkspaceState(
        0, workspaces.size(), shapeHash, "Invalid\nName").empty());

    int restored = 0;
    assert(desknx::layout::parseActiveWorkspaceState(fast, workspaces, restored));
    assert(restored == 2);

    std::cout << "Active workspace marker tests passed\n";
    return 0;
}
