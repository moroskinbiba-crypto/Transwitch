#include "../include/desknx/ModalStack.hpp"

#include <cassert>

using desknx::ui::ModalFlags;
using desknx::ui::ModalLayer;
using desknx::ui::ModalStack;

int main() {
    const ModalStack empty = ModalStack::build({});
    assert(empty.empty());
    assert(empty.top() == ModalLayer::None);

    ModalFlags one{};
    one.appLibrary = true;
    auto stack = ModalStack::build(one);
    assert(stack.size() == 1);
    assert(stack.top() == ModalLayer::AppLibrary);
    assert(stack.contains(ModalLayer::AppLibrary));

    // A child surface remains layered over its parent; only the child owns input.
    ModalFlags settingsWithPicker{};
    settingsWithPicker.launcherSettings = true;
    settingsWithPicker.mediaPicker = true;
    stack = ModalStack::build(settingsWithPicker);
    assert(stack.size() == 2);
    assert(stack.at(0) == ModalLayer::LauncherSettings);
    assert(stack.top() == ModalLayer::MediaPicker);
    assert(stack.contains(ModalLayer::LauncherSettings));

    // Verify pairs whose old input order differed from their render z-order.
    ModalFlags powerOverMedia{};
    powerOverMedia.mediaPicker = true;
    powerOverMedia.power = true;
    assert(ModalStack::build(powerOverMedia).top() == ModalLayer::Power);

    ModalFlags recentOverDelete{};
    recentOverDelete.deleteConfirmation = true;
    recentOverDelete.recent = true;
    assert(ModalStack::build(recentOverDelete).top() == ModalLayer::Recent);

    ModalFlags systemOverWorkspaceSettings{};
    systemOverWorkspaceSettings.workspaceSettings = true;
    systemOverWorkspaceSettings.systemTools = true;
    assert(ModalStack::build(systemOverWorkspaceSettings).top() == ModalLayer::SystemTools);

    // User picker is the top-most surface in the current render order.
    ModalFlags all{};
    all.appLibrary = true;
    all.workspaceMenu = true;
    all.workspaceSettings = true;
    all.workspacePicker = true;
    all.deleteConfirmation = true;
    all.recent = true;
    all.systemTools = true;
    all.launcherSettings = true;
    all.mediaPicker = true;
    all.power = true;
    all.userPicker = true;
    stack = ModalStack::build(all);
    assert(stack.size() == ModalStack::Capacity);
    assert(stack.top() == ModalLayer::UserPicker);
    assert(stack.at(0) == ModalLayer::AppLibrary);
    assert(stack.at(ModalStack::Capacity - 1) == ModalLayer::UserPicker);
    return 0;
}
