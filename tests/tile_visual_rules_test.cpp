#include <desknx/TileVisualRules.hpp>

#include <cassert>
#include <iostream>

int main() {
    using desknx::ui::clampTileLabelOpacity;
    using desknx::ui::measureTileLabel;

    const auto single = measureTileLabel(154,112,false,10);
    assert(single.stripHeight == 32);
    assert(single.firstLineOffset == 8);
    assert(!single.twoLines);
    assert(single.cornerRadius == 10);
    assert(single.maxCharacters == 11);

    const auto two = measureTileLabel(322,238,true,10);
    assert(two.stripHeight == 44);
    assert(two.firstLineOffset == 3);
    assert(two.secondLineOffset == 22);
    assert(two.twoLines);
    assert(two.maxCharacters == 25);

    const auto constrained = measureTileLabel(20,24,true,10);
    assert(constrained.stripHeight == 24);
    assert(!constrained.twoLines);
    assert(constrained.cornerRadius == 10);
    assert(constrained.maxCharacters == 8);

    const auto radiusSafe = measureTileLabel(8,6,false,16);
    assert(radiusSafe.cornerRadius == 3);

    assert(clampTileLabelOpacity(-20) == 0);
    assert(clampTileLabelOpacity(0) == 0);
    assert(clampTileLabelOpacity(196) == 196);
    assert(clampTileLabelOpacity(400) == 255);

    std::cout << "Tile visual rules tests passed\n";
    return 0;
}
