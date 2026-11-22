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

    const auto headerNormal=ResolveButtonStyle(ButtonVariant::HeaderToolbar,{});
    assert(headerNormal.background.r==37&&headerNormal.background.g==47);
    assert(headerNormal.background.a==230&&!headerNormal.drawBorder);
    assert(headerNormal.foreground.r==179&&headerNormal.foreground.g==191);
    const auto headerSelected=ResolveButtonStyle(ButtonVariant::HeaderToolbarSelected,{});
    assert(headerSelected.background.r==48&&headerSelected.background.g==82);
    assert(headerSelected.drawBorder&&headerSelected.border.g==174);
    const auto headerPressed=ResolveButtonStyle(ButtonVariant::HeaderToolbar,
        ControlState{false,true,false});
    assert(headerPressed.background.r==28);
    const auto headerFocused=ResolveButtonStyle(ButtonVariant::HeaderToolbarSelected,
        ControlState{true,false,false});
    assert(headerFocused.drawFocusRing);

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

    const auto infoBadge=ResolveBadgeStyle(BadgeVariant::Info);
    const auto warningBadge=ResolveBadgeStyle(BadgeVariant::Warning);
    assert(infoBadge.background.b>infoBadge.background.r);
    assert(warningBadge.foreground.r==255&&warningBadge.foreground.g==202);

    const ModalRect toggleBounds{20,30,44,22};
    const ToggleControl onToggle{toggleBounds,true,{}};
    assert(onToggle.hitTest(20,30)&&onToggle.hitTest(63,51));
    assert(!onToggle.hitTest(64,51));
    assert(ResolveToggleStyle(true,{}).track.g==132);
    assert(ResolveToggleStyle(false,{}).track.r==48);
    assert(ResolveToggleStyle(true,ControlState{true,false,false}).drawFocusRing);
    assert(ResolveToggleStyle(false,ControlState{false,true,false}).track.r==39);
    const ToggleControl disabledToggle{
        toggleBounds,true,ControlState{false,false,true}};
    assert(!disabledToggle.hitTest(30,40));
    assert(ResolveToggleStyle(true,disabledToggle.state).knob.r==112);

    const ValueFieldControl field{{100,100,120,24},FieldVisualState::Focused};
    assert(field.hitTest(100,100)&&!field.hitTest(220,100));
    const auto focusedFieldStyle=ResolveValueFieldStyle(FieldVisualState::Focused);
    assert(focusedFieldStyle.drawFocusRing&&focusedFieldStyle.focusRing.r==83);
    assert(!ResolveValueFieldStyle(FieldVisualState::Disabled).drawFocusRing);
    const ValueFieldControl disabledField{{100,100,120,24},FieldVisualState::Disabled};
    assert(!disabledField.hitTest(110,110));

    const auto normalCard=ResolveCardStyle(CardVisualState::Normal);
    const auto focusCard=ResolveCardStyle(CardVisualState::Focused);
    const auto panelCard=ResolveCardStyle(CardVisualState::Panel);
    assert(normalCard.shadow&&focusCard.shadow);
    assert(focusCard.background.r>normalCard.background.r);
    assert(!panelCard.shadow&&panelCard.background.r==22);

    const auto readyIndicator=ResolveIndicatorStyle(IndicatorTone::Ready);
    const auto loadingIndicator=ResolveIndicatorStyle(IndicatorTone::Loading);
    const auto offlineIndicator=ResolveIndicatorStyle(IndicatorTone::Offline);
    const auto errorIndicator=ResolveIndicatorStyle(IndicatorTone::Error);
    assert(readyIndicator.dot.g==211&&!readyIndicator.drawHalo);
    assert(loadingIndicator.dot.r==255&&loadingIndicator.drawHalo);
    assert(offlineIndicator.dot.r==128&&!offlineIndicator.drawHalo);
    assert(errorIndicator.dot.r==255&&errorIndicator.dot.g==99);

    return 0;
}
