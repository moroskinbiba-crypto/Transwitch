#include "../include/desknx/WindowComponents.hpp"

#include <cassert>

int main() {
    using namespace desknx::ui;

    // Workspace picker: the dialog, list rows and cancel button share geometry.
    const auto& dialog=WorkspacePickerDialogLayout;
    assert(dialog.title.inside(dialog.surface));
    assert(dialog.description.inside(dialog.surface));
    assert(dialog.items.bounds.inside(dialog.surface));
    assert(dialog.footer.inside(dialog.surface));
    assert(dialog.closeButton.inside(dialog.footer));
    assert(dialog.items.itemCount==12);
    assert(dialog.items.itemAt(360,168)==0);
    assert(dialog.items.itemAt(919,196)==0);
    assert(dialog.items.itemAt(360,197)==-1); // 3 px row gap
    assert(dialog.items.itemAt(359,168)==-1);
    assert(dialog.items.itemAt(360,552)==-1);
    assert(dialog.items.itemAt(360,520)==11);
    assert(dialog.closeButton.contains(744,590));
    assert(dialog.closeButton.contains(919,627));
    assert(!dialog.closeButton.contains(920,627));

    // Context-menu focus rows and touch targets use the same list contract.
    const auto& menu=WorkspaceOptionsContextMenuLayout;
    assert(menu.title.inside(menu.surface));
    assert(menu.items.bounds.inside(menu.surface));
    assert(menu.footer.inside(menu.surface));
    assert(menu.items.itemAt(390,238)==0);
    assert(menu.items.itemAt(889,275)==0);
    assert(menu.items.itemAt(390,276)==-1); // 6 px row gap
    assert(menu.items.itemAt(390,282)==1);
    assert(menu.items.itemAt(390,458)==5);
    assert(menu.items.itemAt(890,458)==-1);
    const auto last=menu.items.itemRect(5);
    assert(last.x==390&&last.y==458&&last.width==500&&last.height==38);
    assert(menu.items.itemAt(390,496)==-1);

    // The twelve launcher settings rows fit without overlapping the footer.
    const auto& settings=LauncherSettingsOptionsLayout;
    assert(settings.itemCount==12);
    assert(settings.bounds.x==340&&settings.bounds.y==214);
    assert(settings.itemAt(340,214)==0);
    assert(settings.itemAt(939,239)==0);
    assert(settings.itemAt(340,240)==-1); // 2 px row gap
    assert(settings.itemAt(340,242)==1);
    assert(settings.itemAt(340,522)==11);
    assert(settings.itemAt(939,547)==11);
    assert(settings.itemAt(340,548)==-1);
    assert(settings.itemAt(940,214)==-1);
    assert(settings.itemRect(11).inside({340,104,680,520}));

    // The side-panel add target exactly matches its rendered button.
    const auto& panel=AppLibraryDetailsPanelLayout;
    assert(panel.header.inside(panel.surface));
    assert(panel.content.inside(panel.surface));
    assert(panel.footer.inside(panel.surface));
    assert(panel.primaryAction.inside(panel.footer));
    assert(panel.primaryAction.contains(902,532));
    assert(panel.primaryAction.contains(1129,573));
    assert(!panel.primaryAction.contains(1130,574));
    assert(!panel.primaryAction.contains(901,532));

    return 0;
}
