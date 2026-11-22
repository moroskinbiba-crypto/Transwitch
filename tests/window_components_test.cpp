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

    // Both Settings windows share close-button and row geometry across input/render.
    const auto& settings=LauncherSettingsDialogControls;
    assert(settings.surface.contains(300,104));
    assert(settings.closeButton.inside(settings.surface));
    assert(settings.rows.itemCount==12);
    assert(settings.rows.bounds.x==340&&settings.rows.bounds.y==214);
    assert(settings.rows.itemAt(340,214)==0);
    assert(settings.rows.itemAt(939,239)==0);
    assert(settings.rows.itemAt(340,240)==-1);
    assert(settings.rows.itemAt(340,242)==1);
    assert(settings.rows.itemAt(940,242)==-1);
    assert(settings.rows.itemAt(340,548)==-1);
    assert(settings.closeButton.contains(842,132));
    assert(settings.closeButton.contains(933,163));
    assert(!settings.closeButton.contains(934,163));
    assert(!settings.closeButton.contains(933,164));

    const auto& workspaceSettings=WorkspaceSettingsDialogControls;
    assert(workspaceSettings.surface.x==settings.surface.x);
    assert(workspaceSettings.surface.y==settings.surface.y);
    assert(workspaceSettings.closeButton.x==settings.closeButton.x);
    assert(workspaceSettings.rows.itemCount==7);
    assert(workspaceSettings.rows.bounds.x==340&&workspaceSettings.rows.bounds.y==216);
    assert(workspaceSettings.rows.itemAt(340,216)==0);
    assert(workspaceSettings.rows.itemAt(939,255)==0);
    assert(workspaceSettings.rows.itemAt(340,256)==-1);
    assert(workspaceSettings.rows.itemAt(340,264)==1);
    assert(workspaceSettings.rows.itemAt(940,300)==-1);
    assert(workspaceSettings.rows.itemAt(340,552)==-1);
    assert(workspaceSettings.rows.itemRect(6).y==504);
    assert(workspaceSettings.rows.itemRect(6).height==40);
    assert(settings.itemAt(340,214)==0);
    assert(settings.itemAt(939,239)==0);
    assert(settings.itemAt(340,240)==-1); // 2 px row gap
    assert(settings.itemAt(340,242)==1);
    assert(settings.itemAt(340,522)==11);
    assert(settings.itemAt(939,547)==11);
    assert(settings.itemAt(340,548)==-1);
    assert(settings.itemAt(940,214)==-1);
    assert(settings.itemRect(11).inside({340,104,680,520}));

    // App Library toolbar: every visible control has one exact hit rectangle.
    const auto& toolbar=AppLibraryToolbarControls;
    assert(AppLibraryToolbarLayout::ButtonCount==7);
    assert(toolbar.itemAt(138,122)==0);
    assert(toolbar.itemAt(233,151)==0);
    assert(toolbar.itemAt(234,140)==-1);
    assert(toolbar.itemAt(242,122)==1);
    assert(toolbar.itemAt(323,151)==1);
    assert(toolbar.itemAt(324,140)==-1);
    assert(toolbar.itemAt(332,122)==2);
    assert(toolbar.itemAt(443,151)==2);
    assert(toolbar.itemAt(444,140)==-1);
    assert(toolbar.itemAt(542,122)==4);
    assert(toolbar.itemAt(667,151)==4);
    assert(toolbar.itemAt(668,140)==-1);
    assert(toolbar.itemAt(756,122)==6);
    assert(toolbar.itemAt(827,151)==6);
    assert(toolbar.itemAt(828,140)==-1);
    assert(toolbar.itemAt(138,152)==-1);
    assert(toolbar.itemAt(235,151)==-1); // inert gutter
    assert(toolbar.buttonRect(6).width==72&&toolbar.buttonRect(6).height==30);
    assert(toolbar.buttonRect(-1).width==0);

    // Recent dialog: ten visible history rows and two exact header buttons.
    const auto& recent=RecentWindowControls;
    assert(RecentWindowLayout::VisibleEntries==10);
    assert(recent.surface.contains(230,78));
    assert(recent.backButton.inside(recent.surface));
    assert(recent.launchButton.inside(recent.surface));
    assert(recent.entries.bounds.inside(recent.surface));
    assert(recent.entries.itemAt(278,190)==0);
    assert(recent.entries.itemAt(1001,224)==0);
    assert(recent.entries.itemAt(278,225)==-1);
    assert(recent.entries.itemAt(278,228)==-1);
    assert(recent.entries.itemAt(278,229)==1);
    assert(recent.entries.itemAt(1001,575)==9);
    assert(recent.entries.itemAt(1001,576)==-1);
    assert(recent.entries.itemAt(1002,220)==-1);
    assert(recent.entries.itemAt(277,220)==-1);
    assert(recent.backButton.contains(838,108));
    assert(recent.backButton.contains(915,137));
    assert(!recent.backButton.contains(916,137));
    assert(!recent.backButton.contains(915,138));
    assert(recent.launchButton.contains(924,108));
    assert(recent.launchButton.contains(1011,137));
    assert(!recent.launchButton.contains(1012,137));
    assert(!recent.launchButton.contains(1011,138));

    // User Picker: Horizon account list is capped at eight; gaps are not tappable.
    const auto& users=UserPickerDialogControls;
    assert(UserPickerDialogLayout::MaxUsers==8);
    assert(users.surface.contains(330,116));
    assert(users.closeButton.inside(users.surface));
    assert(users.users.bounds.inside(users.surface));
    assert(users.users.itemCount==8);
    assert(users.users.itemAt(370,214)==0);
    assert(users.users.itemAt(909,253)==0);
    assert(users.users.itemAt(370,254)==-1);
    assert(users.users.itemAt(370,259)==-1); // 6 px gap after the first row
    assert(users.users.itemAt(370,260)==1);
    assert(users.users.itemAt(909,575)==7);
    assert(users.users.itemAt(909,576)==-1);
    assert(users.users.itemAt(910,260)==-1);
    assert(users.users.itemAt(369,260)==-1);
    assert(users.closeButton.contains(844,136));
    assert(users.closeButton.contains(919,169));
    assert(!users.closeButton.contains(920,169));
    assert(!users.closeButton.contains(919,170));

    // Power actions, Back and Execute are separate hit targets.
    const auto& power=PowerDialogControls;
    assert(power.surface.contains(410,210));
    assert(power.closeButton.inside(power.surface));
    assert(power.actions.bounds.inside(power.surface));
    assert(power.executeButton.inside(power.surface));
    assert(power.actions.itemAt(442,289)==0);
    assert(power.actions.itemAt(837,326)==0);
    assert(power.actions.itemAt(442,327)==-1);
    assert(power.actions.itemAt(442,333)==1);
    assert(power.actions.itemAt(442,377)==2);
    assert(power.actions.itemAt(837,414)==2);
    assert(power.actions.itemAt(442,415)==-1);
    assert(power.actions.itemAt(838,300)==-1);
    assert(power.closeButton.contains(780,238));
    assert(power.closeButton.contains(831,267));
    assert(!power.closeButton.contains(832,267));
    assert(!power.closeButton.contains(831,268));
    assert(power.executeButton.contains(590,432));
    assert(power.executeButton.contains(837,473));
    assert(!power.executeButton.contains(838,473));
    assert(!power.executeButton.contains(837,474));

    // Media Picker uses shared button and list-row geometry. Row gutters are inert.
    const auto& media=MediaPickerWindowControls;
    assert(media.surface.contains(220,58));
    assert(media.directoryButton.inside(media.surface));
    assert(media.closeButton.inside(media.surface));
    assert(media.entries.bounds.inside(media.surface));
    assert(media.entries.itemAt(260,166)==0);
    assert(media.entries.itemAt(1019,195)==0);
    assert(media.entries.itemAt(260,196)==-1);
    assert(media.entries.itemAt(260,199)==-1);
    assert(media.entries.itemAt(260,200)==1);
    assert(media.entries.itemAt(1019,569)==11);
    assert(media.entries.itemAt(1019,570)==-1);
    assert(media.entries.itemAt(1020,200)==-1);
    assert(media.entries.itemAt(259,200)==-1);
    assert(media.entries.itemAt(260,574)==-1);
    assert(media.directoryButton.contains(782,86));
    assert(media.directoryButton.contains(913,119));
    assert(!media.directoryButton.contains(914,119));
    assert(!media.directoryButton.contains(913,120));
    assert(media.closeButton.contains(930,86));
    assert(media.closeButton.contains(1021,119));
    assert(!media.closeButton.contains(1022,119));
    assert(!media.closeButton.contains(1021,120));

    // System Tools cards and Back button share one drawing/hit-test layout.
    const auto& system=SystemToolsWindowControls;
    assert(system.surface.contains(110,104));
    assert(system.closeButton.inside(system.surface));
    assert(system.cardRect(0).inside(system.surface));
    assert(system.cardRect(5).inside(system.surface));
    assert(system.itemAt(156,218)==0);
    assert(system.itemAt(443,353)==0);
    assert(system.itemAt(444,300)==-1);
    assert(system.itemAt(496,218)==1);
    assert(system.itemAt(835,300)==-1);
    assert(system.itemAt(836,218)==2);
    assert(system.itemAt(156,382)==3);
    assert(system.itemAt(496,382)==4);
    assert(system.itemAt(836,382)==5);
    assert(system.itemAt(1123,517)==5);
    assert(system.itemAt(1124,500)==-1);
    assert(system.itemAt(500,354)==-1);
    assert(system.itemAt(500,381)==-1);
    assert(system.itemAt(500,518)==-1);
    assert(system.closeButton.contains(1058,136));
    assert(system.closeButton.contains(1139,169));
    assert(!system.closeButton.contains(1140,169));
    assert(!system.closeButton.contains(1139,170));

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
