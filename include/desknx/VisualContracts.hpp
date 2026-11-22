#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace desknx::ui {

enum class TextRole : std::uint8_t { Display, Heading, Subheading, Body, Caption, Metadata };
struct TypographyStyle { int fontScale; int lineHeight; int maxLines; };

// Keep the legacy pixel scale during semantic migration. New role-specific
// sizing can be tuned later without changing screen coordinates or cache keys.
constexpr TypographyStyle TypographyFor(TextRole role) noexcept {
    switch(role) {
        case TextRole::Display: return {5,50,2};
        case TextRole::Heading: return {3,32,1};
        case TextRole::Subheading: return {2,24,2};
        case TextRole::Body: return {1,16,3};
        case TextRole::Caption: return {1,14,2};
        case TextRole::Metadata: return {1,12,1};
    }
    return {1,16,3};
}
constexpr int TypographyFontPixelSize(TextRole role) noexcept {
    return 10+(TypographyFor(role).fontScale-1)*8;
}

inline std::size_t Utf8CodepointLength(const std::string& value,std::size_t offset) noexcept {
    if(offset>=value.size())return 0;
    const auto lead=static_cast<unsigned char>(value[offset]);
    if(lead<0x80)return 1;
    std::size_t length=0;
    if(lead>=0xC2&&lead<=0xDF)length=2;
    else if(lead>=0xE0&&lead<=0xEF)length=3;
    else if(lead>=0xF0&&lead<=0xF4)length=4;
    else return 1;
    if(offset+length>value.size())return 1;
    for(std::size_t i=1;i<length;++i)
        if((static_cast<unsigned char>(value[offset+i])&0xC0)!=0x80)return 1;
    const auto second=static_cast<unsigned char>(value[offset+1]);
    if((lead==0xE0&&second<0xA0)||(lead==0xED&&second>=0xA0)||
       (lead==0xF0&&second<0x90)||(lead==0xF4&&second>=0x90))return 1;
    return length;
}
inline std::size_t PreviousUtf8CodepointStart(const std::string& value,std::size_t end) noexcept {
    if(end==0)return 0;
    std::size_t position=end-1;
    while(position>0&&(static_cast<unsigned char>(value[position])&0xC0)==0x80)--position;
    return position;
}
inline bool IsInlineTextWhitespace(char value) noexcept {
    return value==' '||value=='\t'||value=='\f'||value=='\v';
}
template<typename Measure>
std::vector<std::string> WrapUtf8Text(const std::string& value,int maxWidth,int maxLines,
                                      Measure measure,
                                      const std::string& ellipsis="\xE2\x80\xA6") {
    std::vector<std::string> lines;
    if(value.empty()||maxWidth<=0||maxLines<=0)return lines;
    std::string currentLine;
    std::size_t position=0;
    bool truncated=false;
    const auto pushLine=[&]() {
        lines.push_back(std::move(currentLine));
        currentLine.clear();
    };
    while(position<value.size()) {
        if(value[position]=='\n'||value[position]=='\r') {
            if(value[position]=='\r'&&position+1<value.size()&&value[position+1]=='\n')position+=2;
            else ++position;
            pushLine();
            if(static_cast<int>(lines.size())>=maxLines&&position<value.size()) {
                truncated=true;
                break;
            }
            continue;
        }
        while(position<value.size()&&IsInlineTextWhitespace(value[position]))++position;
        if(position>=value.size())break;
        if(value[position]=='\n'||value[position]=='\r')continue;
        const std::size_t wordStart=position;
        while(position<value.size()&&value[position]!='\n'&&value[position]!='\r'&&
              !IsInlineTextWhitespace(value[position]))
            position+=Utf8CodepointLength(value,position);
        const std::string word=value.substr(wordStart,position-wordStart);
        const std::string candidate=currentLine.empty()?word:currentLine+" "+word;
        if(measure(candidate)<=maxWidth) {
            currentLine=candidate;
            continue;
        }
        if(!currentLine.empty()) {
            pushLine();
            if(static_cast<int>(lines.size())>=maxLines) {
                truncated=true;
                break;
            }
        }
        if(measure(word)<=maxWidth) {
            currentLine=word;
            continue;
        }
        std::string chunk;
        std::size_t wordPosition=0;
        while(wordPosition<word.size()) {
            const std::size_t length=Utf8CodepointLength(word,wordPosition);
            const std::string cp=word.substr(wordPosition,length);
            if(chunk.empty()||measure(chunk+cp)<=maxWidth)chunk+=cp;
            else {
                lines.push_back(std::move(chunk));
                chunk.clear();
                if(static_cast<int>(lines.size())>=maxLines) {
                    truncated=true;
                    break;
                }
                chunk=cp;
            }
            wordPosition+=length;
        }
        if(truncated)break;
        currentLine=std::move(chunk);
    }
    if(!truncated&&(!currentLine.empty()||lines.empty()))pushLine();
    if(truncated&&!lines.empty()) {
        std::string& last=lines.back();
        if(measure(ellipsis)<=maxWidth) {
            while(!last.empty()&&measure(last+ellipsis)>maxWidth)
                last.resize(PreviousUtf8CodepointStart(last,last.size()));
            if(measure(last+ellipsis)<=maxWidth)last+=ellipsis;
            else last=ellipsis;
        } else last.clear();
    }
    return lines;
}

struct ChromeRect {
    int x{0},y{0},width{0},height{0};
    constexpr bool contains(int px,int py) const noexcept {
        return width>0&&height>0&&px>=x&&px<x+width&&py>=y&&py<y+height;
    }
    constexpr bool inside(const ChromeRect& outer) const noexcept {
        return x>=outer.x&&y>=outer.y&&x+width<=outer.x+outer.width&&
            y+height<=outer.y+outer.height;
    }
};
enum class WorkspaceFooterAction { None,DecreaseTileSize,IncreaseTileSize,RemoveTile };
struct WorkspaceFooterLayout {
    static constexpr ChromeRect surface{28,558,1224,56};
    static constexpr ChromeRect statusIndicator{48,576,7,7};
    static constexpr ChromeRect statusLabel{68,575,552,18};
    static constexpr ChromeRect widgetSettingsButton{642,571,190,28};
    static constexpr ChromeRect tileSizeDecreaseButton{650,571,165,28};
    static constexpr ChromeRect tileSizeIncreaseButton{826,571,165,28};
    static constexpr ChromeRect removeTileButton{1002,571,228,28};
    static constexpr WorkspaceFooterAction actionAt(int x,int y) noexcept {
        if(tileSizeDecreaseButton.contains(x,y))return WorkspaceFooterAction::DecreaseTileSize;
        if(tileSizeIncreaseButton.contains(x,y))return WorkspaceFooterAction::IncreaseTileSize;
        if(removeTileButton.contains(x,y))return WorkspaceFooterAction::RemoveTile;
        return WorkspaceFooterAction::None;
    }
};
enum class WorkspaceDockAction { None,SelectWorkspace,AddWorkspace,OpenWorkspaceMenu,OpenWorkspacePicker,ToggleEditMode };

struct ConfirmationDialogLayout {
    ChromeRect surface;
    ChromeRect primaryButton;
    ChromeRect secondaryButton;
    ChromeRect footer;
    int titleX{0},titleY{0};
    int subtitleX{0},subtitleY{0};
    int descriptionX{0},descriptionY{0};
    int primaryLabelX{0},primaryLabelY{0};
    int secondaryLabelX{0},secondaryLabelY{0};
};

inline constexpr ConfirmationDialogLayout WorkspaceDeleteConfirmationLayout{
    {360,242,560,240},
    {405,382,210,48},
    {635,382,240,48},
    {405,444,485,18},
    405,270,
    405,314,
    405,346,
    466,399,
    706,399
};

struct WorkspaceDockLayout {
    static constexpr int TabX=50,TabWidth=132,TabStep=142,VisibleTabCount=5,MaximumWorkspaceCount=12;
    // Intentionally broad for hold-to-drag and page-swipe continuity.
    static constexpr ChromeRect dragBand{0,638,1280,57};
    static constexpr ChromeRect addButton{770,641,52,48};
    static constexpr ChromeRect menuButton{832,641,52,48};
    static constexpr ChromeRect pagePicker{900,641,150,48};
    static constexpr ChromeRect editButton{1096,638,128,54};
    static constexpr ChromeRect surface(int style) noexcept {
        if(style==1)return {0,620,1280,100};
        if(style==2)return {28,630,1224,70};
        return {28,624,1224,82};
    }
    static constexpr ChromeRect tab(int slot,int style) noexcept {
        const int n=slot<0?0:slot;
        return {TabX+n*TabStep,style==1?642:641,TabWidth,style==1?42:(style==2?45:48)};
    }
    static constexpr int visibleStart(int active,int count,int dragStart=-1) noexcept {
        if(dragStart>=0)return dragStart;
        return std::max(0,std::min(active-2,std::max(0,count-VisibleTabCount)));
    }
    static constexpr int tabSlotAt(int x,int y,int style,int first,int count) noexcept {
        for(int slot=0;slot<VisibleTabCount;++slot) {
            if(first+slot>=count)break;
            if(tab(slot,style).contains(x,y))return slot;
        }
        return -1;
    }
    static constexpr WorkspaceDockAction actionAt(int x,int y,int style,int first,int count,
                                                   int maximum=MaximumWorkspaceCount) noexcept {
        if(tabSlotAt(x,y,style,first,count)>=0)return WorkspaceDockAction::SelectWorkspace;
        if(addButton.contains(x,y))return count<maximum?WorkspaceDockAction::AddWorkspace:WorkspaceDockAction::None;
        if(menuButton.contains(x,y))return WorkspaceDockAction::OpenWorkspaceMenu;
        if(pagePicker.contains(x,y))return WorkspaceDockAction::OpenWorkspacePicker;
        if(editButton.contains(x,y))return WorkspaceDockAction::ToggleEditMode;
        return WorkspaceDockAction::None;
    }
};

enum class MotionMode : std::uint8_t { Full, Reduced, LowPerformance };
enum class MotionCurve : std::uint8_t { Linear, EaseOutCubic, EaseInOutCubic };

struct MotionTrack {
    float from{0.0f};
    float to{1.0f};
    std::uint32_t startedAt{0};
    std::uint32_t durationMs{0};
    MotionCurve curve{MotionCurve::EaseOutCubic};
};

inline const char* MotionModeSettingName(MotionMode mode) noexcept {
    switch(mode) {
        case MotionMode::Reduced:return "reduced";
        case MotionMode::LowPerformance:return "lowperformance";
        case MotionMode::Full:
        default:return "full";
    }
}

inline bool ParseMotionModeSetting(const char* value,MotionMode& result) noexcept {
    if(!value)return false;
    if(std::strcmp(value,"full")==0)result=MotionMode::Full;
    else if(std::strcmp(value,"reduced")==0)result=MotionMode::Reduced;
    else if(std::strcmp(value,"lowperformance")==0)result=MotionMode::LowPerformance;
    else return false;
    return true;
}

class MotionController {
public:
    void setMode(MotionMode mode) noexcept { mode_=mode; }
    MotionMode mode() const noexcept { return mode_; }

    std::uint32_t resolveDuration(std::uint32_t requestedMs) const noexcept {
        switch(mode_) {
            case MotionMode::Reduced:return 0;
            case MotionMode::LowPerformance:return std::min<std::uint32_t>(requestedMs,80);
            case MotionMode::Full:
            default:return requestedMs;
        }
    }

    MotionTrack begin(float from,float to,std::uint32_t now,std::uint32_t durationMs,
                      MotionCurve curve=MotionCurve::EaseOutCubic) const noexcept {
        return {from,to,now,resolveDuration(durationMs),curve};
    }

    float sample(const MotionTrack& track,std::uint32_t now) const noexcept {
        if(track.durationMs==0)return track.to;
        const std::uint32_t elapsed=now-track.startedAt;
        if(elapsed>=track.durationMs)return track.to;
        const float linear=static_cast<float>(elapsed)/static_cast<float>(track.durationMs);
        float eased=linear;
        switch(track.curve) {
            case MotionCurve::Linear:break;
            case MotionCurve::EaseInOutCubic:
                eased=linear<0.5f?4.0f*linear*linear*linear:
                    1.0f-((-2.0f*linear+2.0f)*(-2.0f*linear+2.0f)*
                          (-2.0f*linear+2.0f))/2.0f;
                break;
            case MotionCurve::EaseOutCubic:
            default: {
                const float inverse=1.0f-linear;
                eased=1.0f-inverse*inverse*inverse;
                break;
            }
        }
        return track.from+(track.to-track.from)*eased;
    }

    bool active(const MotionTrack& track,std::uint32_t now) const noexcept {
        return track.durationMs>0&&static_cast<std::uint32_t>(now-track.startedAt)<track.durationMs;
    }

private:
    MotionMode mode_{MotionMode::Full};
};

} // namespace desknx::ui
