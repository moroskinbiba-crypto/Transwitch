#include <desknx/TileVisualRules.hpp>
#include <desknx/Artwork.hpp>

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

    // Library cards use the same two-line label rules as desktop cover tiles.
    const auto libraryCard = measureTileLabel(230,118,true,10);
    assert(libraryCard.twoLines);
    assert(libraryCard.stripHeight == 44);
    assert(libraryCard.maxCharacters == 17);
    assert(libraryCard.cornerRadius == 10);

    // Cover must fill the card viewport without distorting the source ratio.
    const auto cover = desknx::artwork::destination(
        10,20,100,50,50,100,desknx::artwork::FitMode::Cover);
    assert(cover.x == 10 && cover.y == -55);
    assert(cover.w == 100 && cover.h == 200);

    // Contain keeps the full source visible and centers the letterbox region.
    const auto contain = desknx::artwork::destination(
        10,20,100,50,50,100,desknx::artwork::FitMode::Contain);
    assert(contain.x == 47 && contain.y == 20);
    assert(contain.w == 25 && contain.h == 50);

    const auto stretch = desknx::artwork::destination(
        10,20,100,50,50,100,desknx::artwork::FitMode::Stretch);
    assert(stretch.x == 10 && stretch.y == 20);
    assert(stretch.w == 100 && stretch.h == 50);

    const auto invalidFit = desknx::artwork::destination(
        10,20,0,50,50,100,desknx::artwork::FitMode::Cover);
    assert(invalidFit.w == 0 && invalidFit.h == 0);

    assert(clampTileLabelOpacity(-20) == 0);
    assert(clampTileLabelOpacity(0) == 0);
    assert(clampTileLabelOpacity(196) == 196);
    assert(clampTileLabelOpacity(400) == 255);

    std::cout << "Tile visual rules tests passed\n";
    return 0;
}
