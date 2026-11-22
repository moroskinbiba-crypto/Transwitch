#include "../include/desknx/VisualContracts.hpp"
#include "../include/desknx/BitmapFont.hpp"
#include <cassert>
#include <limits>
int main() {
 using namespace desknx::ui;
 assert(TypographyFor(TextRole::Display).fontScale==5);
 assert(TypographyFor(TextRole::Heading).fontScale==3);
 assert(TypographyFor(TextRole::Subheading).fontScale==2);
 assert(TypographyFor(TextRole::Body).fontScale==1);
 assert(TypographyFor(TextRole::Caption).fontScale==1);
 assert(TypographyFor(TextRole::Metadata).fontScale==1);
 assert(TypographyFor(TextRole::Metadata).maxLines==1);
 assert(TypographyFor(TextRole::Body).maxLines==3);

 const auto measure=[](const std::string& s){return MeasureBitmapTextWidth(s,1);};
 assert(MeasureBitmapTextWidth("ABC",1)==18);
 assert(MeasureBitmapTextWidth(u8"Ёж",1)==12);
 assert(MeasureBitmapTextWidth("A\tB",1)==36);
 assert(MeasureBitmapTextWidth("A\r\nB",1)==12);
 assert(MeasureBitmapTextWidth("A",std::numeric_limits<int>::max())==std::numeric_limits<int>::max());
 const auto wrapped=WrapUtf8Text(u8"Привет мир",36,2,measure);
 assert(wrapped.size()==2&&wrapped[0]==u8"Привет"&&wrapped[1]==u8"мир");
 const auto truncated=WrapUtf8Text(u8"Привет мир снова",36,2,measure);
 assert(truncated.size()==2&&truncated[0]==u8"Привет"&&truncated[1]==u8"мир…");

 using Dock=WorkspaceDockLayout; using Footer=WorkspaceFooterLayout;
 assert(Footer::surface.contains(28,558)&&Footer::surface.contains(1251,613));
 assert(!Footer::surface.contains(1252,613));
 assert(Footer::actionAt(650,571)==WorkspaceFooterAction::DecreaseTileSize);
 assert(Footer::actionAt(815,585)==WorkspaceFooterAction::None);
 assert(Footer::actionAt(826,571)==WorkspaceFooterAction::IncreaseTileSize);
 assert(Footer::actionAt(991,585)==WorkspaceFooterAction::None);
 assert(Footer::actionAt(1002,571)==WorkspaceFooterAction::RemoveTile);
 assert(Footer::actionAt(1230,585)==WorkspaceFooterAction::None);
 assert(Dock::visibleStart(0,12)==0&&Dock::visibleStart(4,12)==2);
 assert(Dock::visibleStart(11,12)==7&&Dock::visibleStart(8,12,1)==1);
 assert(Dock::tab(0,0).y==641&&Dock::tab(0,0).height==48);
 assert(Dock::tab(0,1).y==642&&Dock::tab(0,1).height==42);
 assert(Dock::tab(0,2).y==641&&Dock::tab(0,2).height==45);
 assert(Dock::tabSlotAt(50,641,0,0,12)==0);
 assert(Dock::tabSlotAt(182,660,0,0,12)==-1);
 assert(Dock::dragBand.contains(0,638)&&Dock::dragBand.contains(1279,694));
 assert(!Dock::dragBand.contains(1279,695));
 assert(Dock::actionAt(50,641,0,0,12)==WorkspaceDockAction::SelectWorkspace);
 assert(Dock::actionAt(770,641,0,0,12)==WorkspaceDockAction::AddWorkspace);
 assert(Dock::actionAt(770,641,0,0,12,12)==WorkspaceDockAction::None);
 assert(Dock::actionAt(832,641,0,0,12)==WorkspaceDockAction::OpenWorkspaceMenu);
 assert(Dock::actionAt(900,641,0,0,12)==WorkspaceDockAction::OpenWorkspacePicker);
 assert(Dock::actionAt(1096,638,0,0,12)==WorkspaceDockAction::ToggleEditMode);
 assert(Dock::actionAt(192,660,0,0,1)==WorkspaceDockAction::None);
 return 0;
}
