#include <desknx/UiComponentRules.hpp>
#include <cassert>
#include <cstdint>
#include <iostream>

using desknx::ui::ButtonState;
using desknx::ui::ButtonVariant;
using desknx::ui::resolveButtonPaint;

int main() {
    const auto glassIdle=resolveButtonPaint(ButtonVariant::Glass,{});
    assert(glassIdle.drawFill && !glassIdle.drawEdge);
    assert(!glassIdle.drawAccentRail && !glassIdle.drawFocusRing);

    const auto glassActive=resolveButtonPaint(
        ButtonVariant::Glass,ButtonState{true,false,false});
    assert(glassActive.drawFill && glassActive.drawEdge);

    const auto railActive=resolveButtonPaint(
        ButtonVariant::Rail,ButtonState{true,false,false});
    assert(railActive.drawFill && railActive.drawAccentRail);
    assert(!railActive.drawEdge);

    const auto minimalIdle=resolveButtonPaint(ButtonVariant::Minimal,{});
    assert(!minimalIdle.drawFill && !minimalIdle.drawAccentRail);

    const auto chip=resolveButtonPaint(ButtonVariant::Chip,{});
    assert(chip.drawFill && chip.drawEdge);

    const auto focused=resolveButtonPaint(
        ButtonVariant::Solid,ButtonState{false,true,false});
    assert(focused.drawFocusRing && focused.drawEdge);

    const auto disabled=resolveButtonPaint(
        ButtonVariant::Chip,ButtonState{true,true,true});
    assert(disabled.drawFill && disabled.drawFocusRing);
    assert(disabled.alphaScale==112);

    std::cout << "UI component rules tests passed\n";
    return 0;
}
