#include "../include/desknx/ControlComponents.hpp"

#include <cassert>

int main() {
    using namespace desknx::ui;

    const ModalRect bounds{100,200,180,44};
    const ButtonControl primary{bounds,ButtonVariant::Primary,{}};
    assert(primary.hitTest(100,200));
    assert(primary.hitTest(279,243));
    assert(!primary.hitTest(280,243));
    assert(!primary.hitTest(279,244));
    assert(!primary.hitTest(99,220));

    const ButtonControl disabled{
        bounds,ButtonVariant::Primary,ControlState{false,false,true}};
    assert(!disabled.hitTest(120,220));
    assert(ResolveButtonStyle(ButtonVariant::Primary,disabled.state)
        .foreground.r==113);

    const auto primaryStyle=ResolveButtonStyle(ButtonVariant::Primary,{});
    assert(primaryStyle.background.r==10);
    assert(primaryStyle.background.g==132);
    assert(primaryStyle.drawBorder);
    assert(!primaryStyle.drawFocusRing);

    const auto focusedPrimary=ResolveButtonStyle(
        ButtonVariant::Primary,ControlState{true,false,false});
    assert(focusedPrimary.drawFocusRing);
    assert(focusedPrimary.border.r==214);

    const auto pressedSecondary=ResolveButtonStyle(
        ButtonVariant::Secondary,ControlState{false,true,false});
    assert(pressedSecondary.background.r==37);
    assert(!pressedSecondary.drawFocusRing);

    const auto destructive=ResolveButtonStyle(ButtonVariant::Destructive,{});
    assert(destructive.background.r==112);
    assert(destructive.background.g==38);
    const auto focusedDestructive=ResolveButtonStyle(
        ButtonVariant::Destructive,ControlState{true,false,false});
    assert(focusedDestructive.drawFocusRing);
    assert(focusedDestructive.focusRing.r==255);

    const auto quiet=ResolveButtonStyle(ButtonVariant::Quiet,{});
    assert(!quiet.drawBorder);
    assert(quiet.background.a==0);

    const auto current=ResolveSelectableRowStyle(SelectableRowState::Current);
    const auto focused=ResolveSelectableRowStyle(SelectableRowState::Focused);
    const auto disabledRow=ResolveSelectableRowStyle(SelectableRowState::Disabled);
    assert(current.background.r==30&&current.foreground.b==255);
    assert(focused.background.r==45&&focused.foreground.r==246);
    assert(disabledRow.background.a==0&&disabledRow.foreground.r==91);
    assert(IsSelectableRowInteractive(SelectableRowState::Normal));
    assert(IsSelectableRowInteractive(SelectableRowState::Current));
    assert(IsSelectableRowInteractive(SelectableRowState::Focused));
    assert(!IsSelectableRowInteractive(SelectableRowState::Disabled));

    return 0;
}
