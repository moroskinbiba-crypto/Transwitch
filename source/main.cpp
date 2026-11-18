#include <switch.h>
#include <switch/applets/hid_la.h>
#include <switch/applets/album_la.h>
#include <switch/applets/friends_la.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <desknx/Protocol.hpp>
#include <desknx/SafeReplace.hpp>
#include "../include/desknx/Model.hpp"
#include "../include/desknx/WorkspaceOps.hpp"
#include "../include/desknx/Layout.hpp"
#include "catalog.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/Diagnostics.hpp"
#include "../include/desknx/Localization.hpp"
#include "../include/desknx/NextendoPlayers.hpp"
#include "catalog_view.hpp"
#include "../include/desknx/Artwork.hpp"
#include "../include/desknx/CardsLayout.hpp"
#include "CardsHomeRenderer.hpp"
#include "FontRenderer.hpp"
#include "IconCache.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>
#include <unordered_map>
#include <vector>
#include <map>
#include <memory>

namespace {
using namespace desknx::model;
constexpr int SW = 1280, SH = 720, COLS = Columns, ROWS = Rows;
constexpr int CW = 154, CH = 112, GX = 14, GY = 14, LEFT = 141, TOP = 150;
constexpr int DeskTabX=50, DeskTabWidth=132, DeskTabStep=142, VisibleDeskTabs=5;
constexpr int ToolbarStartX=64, ToolbarButtonWidth=82, ToolbarButtonStep=90, ToolbarButtonCount=4;
constexpr int toolbarButtonX(int index) { return ToolbarStartX + index*ToolbarButtonStep; }
constexpr int MAX_ITEMS = MaxItems, MAX_DESKS = MaxDesks;
// NsApplicationView flag bits follow SLaunch's documented mapping.
constexpr u32 NsViewFlagIsGameCard = BIT(6);
constexpr u32 NsViewFlagNeedsUpdate = BIT(9);
constexpr const char* CFG_DIR = "sdmc:/switch/DeskNX";
constexpr const char* CFG_FILE = "sdmc:/switch/DeskNX/layout.tsv";
constexpr const char* CFG_TMP = "sdmc:/switch/DeskNX/layout.tmp";
constexpr const char* CFG_BACKUP_BASE = "sdmc:/switch/DeskNX/layout.corrupt";
constexpr const char* CFG_BAK_FILE = "sdmc:/switch/DeskNX/layout.bak";
constexpr const char* ACTIVE_WORKSPACE_FILE = "sdmc:/switch/DeskNX/active-workspace.dat";
constexpr const char* ACTIVE_WORKSPACE_TMP = "sdmc:/switch/DeskNX/active-workspace.tmp";
constexpr const char* ACTIVE_WORKSPACE_BAK = "sdmc:/switch/DeskNX/active-workspace.bak";
constexpr int UiCornerRadius = 10;
constexpr int AppSettingsRows = 12;
size_t layoutRecoveredTiles = 0, layoutDroppedTiles = 0;
// CardsNX-style showcase: large cover art on a rail, with the editable 6x3
// grid one button away. Both views read the same workspaces, so nothing is
// duplicated and no layout is rewritten when the view changes.
desknx::cards::HomeRenderer cardsRenderer;
bool cardsView = true;
int cardsSelected = 0;
float cardsAnchor = 0.f;
bool cardsViewBeforeEditing = false;
Uint32 cardsAnimationTick = 0;
int cardsTouchFingerStartX = 0, cardsTouchFingerStartY = 0;
bool cardsTouchActive = false;
SDL_FingerID cardsTouchFingerId = 0;
constexpr const char* HISTORY_FILE = "sdmc:/switch/DeskNX/recent.tsv";
constexpr const char* HISTORY_TMP = "sdmc:/switch/DeskNX/recent.tmp";
constexpr const char* HISTORY_BAK_FILE = "sdmc:/switch/DeskNX/recent.bak";
constexpr const char* SETTINGS_FILE = "sdmc:/switch/DeskNX/settings.ini";
constexpr const char* SETTINGS_TMP = "sdmc:/switch/DeskNX/settings.tmp";
constexpr const char* SETTINGS_BAK = "sdmc:/switch/DeskNX/settings.bak";
constexpr const char* MEDIA_ROOT = "sdmc:/switch/DeskNX/media";
constexpr const char* ALBUM_ROOT = "sdmc:/Nintendo/Album";

struct Color { Uint8 r, g, b, a; };
Color BG{10,14,22,255};
constexpr Color WHITE{246,248,252,255};
Color MUTED{145,158,180,255}, BLUE{83,157,255,255};
Color GREEN{52,211,153,255}, RED{255,99,112,255};
Color THEME_TOP{16,27,44,255}, THEME_BOTTOM{9,17,31,255};
std::array<Color,12> COLORS{{
    {91,145,226,255},{53,169,151,255},{155,116,226,255},{222,151,83,255},
    {218,105,133,255},{79,169,206,255},{147,171,94,255},{113,130,158,255},
    {242,85,65,255},{45,212,191,255},{236,72,153,255},{250,204,21,255}
}};
int themeIndex=1;
bool languageRussian=true;
int iconStyle=0; // 0 classic, 1 Tabler-inspired outline, 2 filled material-style
std::string wallpaperPath, fontPath, iconPackPath;
SDL_Texture* wallpaperTexture=nullptr;
int wallpaperWidth=0, wallpaperHeight=0;
SDL_Texture* roundedCornerMaskTexture=nullptr;
int roundedCornerMaskRadius=0;

enum class MediaPickerType { Wallpaper, Font, IconPack, WorkspaceBackground };
struct MediaEntry { std::string name, path; bool directory{false}; };
bool mediaPickerOpen=false;
MediaPickerType mediaPickerType=MediaPickerType::Wallpaper;
struct WorkspaceBackgroundTexture {
    std::string path;
    SDL_Texture* texture{nullptr};
    int width{0}, height{0};
    unsigned long long lastUse{0};
};
std::vector<WorkspaceBackgroundTexture> workspaceBackgroundTextures;
unsigned long long workspaceBackgroundClock=0;
std::string mediaPickerDirectory;
std::vector<MediaEntry> mediaPickerEntries;
int mediaPickerIndex=0, mediaPickerOffset=0;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
bool rendererVsync=false;
std::vector<Workspace> desks;
std::vector<Candidate> catalogue;
SDL_mutex* catalogueMutex = nullptr;
SDL_mutex* ipcMutex = nullptr;
SDL_mutex* storageCommitMutex = nullptr;
SDL_Thread* catalogueThread = nullptr;
SDL_Thread* nextendoThread = nullptr;
SDL_mutex* nextendoMutex = nullptr;
SDL_atomic_t nextendoStop{};
desknx::nextendo::PresenceFeed nextendoFeed;
struct CatalogueScanRequest { bool forceRefresh; bool reinitializeNs; };
const CatalogueScanRequest catalogueRefreshRequest{true, true};
#ifdef DESKNX_SYSTEM_APPLET
SDL_Thread* heartbeatThread = nullptr;
SDL_atomic_t heartbeatRunning{};
SDL_atomic_t heartbeatError{};
#endif
bool catalogueReady = false, catalogueScanComplete = false, catalogueTruncated = false;
bool catalogueNsAvailable = false;
Uint32 catalogueNsError = 0;
size_t catalogueGameCount = 0, catalogueNroCount = 0;
uint64_t catalogueRevision = 0, catalogueViewRevision = 0, recentLaunchRevision = 0;
std::vector<Candidate> visibleCatalogueCache;
uint64_t visibleCacheCatalogueRevision = UINT64_MAX;
uint64_t visibleCacheViewRevision = UINT64_MAX;
uint64_t visibleCacheRecentRevision = UINT64_MAX;
bool layoutLoaded = false, layoutNeedsMigration = false, catalogueWarningShown = false;
bool layoutSaveBlocked = false;
bool layoutUserModified = false;
bool defaultLayoutPending = false;
uint64_t persistedLayoutShapeHash = 0;
bool persistedLayoutShapeKnown = false;
bool layoutPrimaryNeedsRewrite = true;
bool activeWorkspaceMarkerPending=false;
Uint32 activeWorkspaceMarkerDue=0;
bool activeWorkspaceMarkerKnown=false;
std::string persistedActiveWorkspaceMarker;
struct LayoutSaveJob {
    std::string serializedLayout;
    std::string activeWorkspaceState;
    std::string previousActiveMarker;
    uint64_t shapeHash{0};
    bool rewritePrimary{false};
    bool markerOnly{false};
    bool previousMarkerKnown{false};
};
struct LayoutSaveOutcome {
    bool success{false};
    bool blocked{false};
    bool primaryWritten{false};
    bool markerWritten{false};
    bool markerOnly{false};
    uint64_t shapeHash{0};
    char message[128]{};
};
std::unique_ptr<LayoutSaveJob> activeLayoutSaveJob;
std::unique_ptr<LayoutSaveJob> pendingLayoutSaveJob;
SDL_Thread* layoutSaveThread=nullptr;
SDL_atomic_t layoutSaveDone{};
LayoutSaveOutcome layoutSaveOutcome{};
bool enqueueLayoutSave(bool markerOnly=false);
void pumpAsyncLayoutSaver(bool waitForCompletion=false);
void drainAsyncLayoutSaver();
struct ConsoleUserProfile { AccountUid uid{}; std::string nickname; };
std::vector<ConsoleUserProfile> consoleUsers;
bool consoleUsersLoaded=false, userPickerOpen=false, uiHasSelectedUser=false;
bool accountServiceReady=false;
int userPickerIndex=0;
AccountUid uiSelectedUser{};
std::string selectedUserLabel="SELECT PLAYER";
bool sameAccountUid(const AccountUid& a,const AccountUid& b);
FontRenderer gFontRenderer;
IconCache gIconCache;
struct HistoryEntry { Kind kind{Kind::Game}; std::string title, data; };
std::vector<HistoryEntry> recentLaunches;
struct PdmQuerySnapshot {
    char titleId[17]{};
    bool available{false};
    uint64_t playtimeSeconds{0}, lastPlayed{0};
    uint32_t launchCount{0}, initRc{0}, queryRc{0};
};
SDL_Thread* playStatsThread=nullptr;
SDL_mutex* playStatsMutex=nullptr;
SDL_atomic_t playStatsDone{};
PdmQuerySnapshot playStatsResult{};
std::string pendingPlayStatsTitle;
Uint32 pendingPlayStatsDue=0;
bool playStatsPending=false;
std::string catalogueQuery;
desknx::catalog_view::FilterMode catalogueFilter=desknx::catalog_view::FilterMode::All;
desknx::catalog_view::SortMode catalogueSort=desknx::catalog_view::SortMode::Name;
int deskIndex=0, selectedX=0, selectedY=0, listIndex=0, listOffset=0, battery=-1;
uint64_t sdFreeBytes=0, sdTotalBytes=0;
Uint32 lastStorageUpdate=0;
bool storageStatsAvailable=false;
std::unordered_map<std::string,uint64_t> nroSizeCache;
std::unordered_map<std::string,std::vector<std::string>> tileTitleWrapCache;
bool widgetPickerMode=false;
desknx::catalog_view::FilterMode widgetPickerPreviousFilter=desknx::catalog_view::FilterMode::All;
desknx::catalog_view::SortMode widgetPickerPreviousSort=desknx::catalog_view::SortMode::Name;
std::string widgetPickerPreviousQuery;
SDL_FingerID catalogueTouchFingerId=0;
bool catalogueTouchActive=false, catalogueTouchMoved=false;
bool touchPressActive=false;
SDL_FingerID touchPressFingerId=0;
int touchPressStartX=0, touchPressStartY=0;
int touchDragOffsetX=0, touchDragOffsetY=0;
int catalogueTouchStartY=0, catalogueTouchRow=0, lastCatalogueTap=-1;
Uint32 lastCatalogueTapTick=0;
bool editing=false, dragging=false, listOpen=false, recentOpen=false, systemMenu=false, appSettingsMenu=false, workspaceMenu=false, workspaceSettingsMenu=false, workspacePicker=false, workspacePickerMoveTile=false, deletePrompt=false, running=true;
int recentIndex=0, systemMenuIndex=0, appSettingsIndex=0, workspaceMenuIndex=0, workspaceSettingsIndex=0, workspacePickerIndex=0, lastWorkspaceTabTap=-1;
int touchDragTabStart=-1;
Uint32 lastWorkspaceTabTapTick=0;
// PowerMenu modal (ТЗ 6.1: "- opens the power menu"). In system mode the
// action is delegated to the backend (PWR-03); in standalone we honour it
// locally through the applet power APIs, which are available to NROs too.
bool powerMenu=false;
s32 powerIndex=0;
bool sdMounted=false, psmReady=false, touchDragging=false;
std::string statusText="Y ADD   X EDIT   L3 TOOLS", lastLaunched="Nothing launched yet";
Uint32 statusUntil=0, lastBatteryTick=0;
#ifdef DESKNX_SYSTEM_APPLET
// Backend -> menu state (IPC-06/§4.4). The menu keeps no app state of its own:
// everything here comes from the initial block or live events.
AccountUid g_backendUser{};         // zero uid = backend decides (LNC-03)
bool g_backendHasUser=false;
u64 g_backendAppTitle=0;            // suspended/foreground title id
desknx::ipc::InitialState g_initialState{};
#endif

void setStatus(const std::string& s, Uint32 duration=2500) {
    statusText=s; statusUntil=SDL_GetTicks()+duration;
    desknx::diag::Write("ui","status","message=%s",s.c_str());
}

bool commitSdCard(const char* operation) {
    if (!sdMounted) return false;
    if(storageCommitMutex) SDL_LockMutex(storageCommitMutex);
    const Result rc = fsdevCommitDevice("sdmc");
    if(storageCommitMutex) SDL_UnlockMutex(storageCommitMutex);
    if (R_FAILED(rc)) {
        desknx::diag::Write("storage", "commit_failed",
            "operation=%s rc=0x%08X", operation ? operation : "unknown",
            static_cast<unsigned int>(rc));
        return false;
    }
    desknx::diag::Write("storage", "commit_ok", "operation=%s",
                        operation ? operation : "unknown");
    return true;
}
void color(Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
void rect(int x,int y,int w,int h,Color c) {
    if(w<=0||h<=0) return;
    color(c);
    SDL_Rect r{x,y,w,h};
    SDL_RenderFillRect(renderer,&r);
}
void outline(int x,int y,int w,int h,Color c) {
    if(w<=0||h<=0) return;
    const int r=std::max(0,std::min(UiCornerRadius,std::min(w,h)/2));
    color(c);
    if(r==0) {
        SDL_Rect bounds{x,y,w,h};
        SDL_RenderDrawRect(renderer,&bounds);
        return;
    }
    if(w>2*r) {
        SDL_RenderDrawLine(renderer,x+r,y,x+w-r-1,y);
        SDL_RenderDrawLine(renderer,x+r,y+h-1,x+w-r-1,y+h-1);
    }
    if(h>2*r) {
        SDL_RenderDrawLine(renderer,x,y+r,x,y+h-r-1);
        SDL_RenderDrawLine(renderer,x+w-1,y+r,x+w-1,y+h-r-1);
    }
    for(int row=0;row<r;++row) {
        const double dy=static_cast<double>(r-row);
        const int inset=static_cast<int>(std::ceil(r-std::sqrt(
            std::max(0.0,static_cast<double>(r*r)-dy*dy))));
        SDL_RenderDrawPoint(renderer,x+inset,y+row);
        SDL_RenderDrawPoint(renderer,x+w-1-inset,y+row);
        SDL_RenderDrawPoint(renderer,x+inset,y+h-1-row);
        SDL_RenderDrawPoint(renderer,x+w-1-inset,y+h-1-row);
    }
}

void applyThemePalette() {
    switch(themeIndex) {
                case 1: // Frosted glass
                    BG={63,69,78,255}; THEME_TOP={74,79,89,255}; THEME_BOTTOM={45,91,137,255};
                    MUTED={215,222,231,255}; BLUE={193,147,255,255};
                    GREEN={73,173,12,255}; RED={255,115,123,255};
                    COLORS={{{110,151,207,255},{99,172,180,255},{163,138,206,255},{220,158,126,255},
                             {205,123,150,255},{104,162,199,255},{142,176,116,255},{135,143,159,255},
                             {222,111,102,255},{86,190,177,255},{203,128,171,255},{230,190,112,255}}};
                    break;
                case 2: // Graphite
                    BG={15,17,21,255}; THEME_TOP={34,37,43,255}; THEME_BOTTOM={12,14,18,255};
                    MUTED={164,169,180,255}; BLUE={187,151,255,255};
                    GREEN={120,209,148,255}; RED={255,118,128,255};
                    COLORS={{{142,151,166,255},{105,160,173,255},{153,132,194,255},{187,148,112,255},
                             {177,121,143,255},{115,160,180,255},{148,169,126,255},{118,126,142,255},
                             {190,91,91,255},{80,164,151,255},{170,116,157,255},{190,167,107,255}}};
                    break;
                case 3: // Violet
                    BG={17,12,27,255}; THEME_TOP={48,27,68,255}; THEME_BOTTOM={12,17,39,255};
                    MUTED={174,160,193,255}; BLUE={200,153,255,255};
                    GREEN={127,222,181,255}; RED={255,116,147,255};
                    COLORS={{{148,103,212,255},{80,155,169,255},{187,114,221,255},{209,140,111,255},
                             {209,102,153,255},{83,154,205,255},{133,175,127,255},{127,118,160,255},
                             {231,101,132,255},{75,202,183,255},{194,127,236,255},{235,190,116,255}}};
                    break;
                case 4: // Sunset
                    BG={25,17,20,255}; THEME_TOP={70,37,44,255}; THEME_BOTTOM={29,36,61,255};
                    MUTED={198,172,168,255}; BLUE={255,172,133,255};
                    GREEN={153,210,119,255}; RED={255,105,105,255};
                    COLORS={{{218,128,104,255},{104,168,145,255},{177,129,190,255},{235,162,95,255},
                             {217,110,126,255},{113,171,196,255},{170,180,101,255},{145,130,136,255},
                             {239,99,79,255},{70,195,174,255},{223,111,150,255},{246,196,105,255}}};
                    break;
        case 5: // Emerald
            BG={7,20,17,255}; THEME_TOP={10,47,39,255}; THEME_BOTTOM={5,18,18,255};
            MUTED={150,190,176,255}; BLUE={91,226,177,255};
            GREEN={82,220,150,255}; RED={255,105,123,255};
            COLORS={{{47,157,112,255},{52,190,155,255},{96,162,133,255},{190,174,91,255},
                     {190,111,132,255},{74,168,188,255},{144,184,103,255},{111,143,130,255},
                     {231,97,94,255},{45,212,191,255},{193,120,181,255},{231,194,91,255}}};
            break;
        case 6: // Rose
            BG={24,12,20,255}; THEME_TOP={61,25,43,255}; THEME_BOTTOM={23,12,27,255};
            MUTED={202,165,184,255}; BLUE={255,139,182,255};
            GREEN={112,213,171,255}; RED={255,99,132,255};
            COLORS={{{192,91,140,255},{86,166,153,255},{164,115,195,255},{212,147,112,255},
                     {227,101,151,255},{111,153,207,255},{151,174,115,255},{143,121,143,255},
                     {255,112,132,255},{63,196,178,255},{219,132,209,255},{239,184,124,255}}};
            break;
        case 7: // Arctic
            BG={8,18,28,255}; THEME_TOP={18,48,66,255}; THEME_BOTTOM={7,18,34,255};
            MUTED={164,195,212,255}; BLUE={117,208,255,255};
            GREEN={100,220,191,255}; RED={255,119,138,255};
            COLORS={{{73,150,208,255},{70,177,184,255},{140,135,213,255},{203,164,103,255},
                     {210,116,144,255},{85,187,220,255},{144,185,129,255},{122,150,171,255},
                     {244,110,128,255},{55,207,204,255},{183,142,236,255},{231,204,130,255}}};
            break;
        default: // Ocean
            themeIndex=0;
            BG={10,14,22,255}; THEME_TOP={16,27,44,255}; THEME_BOTTOM={9,17,31,255};
            MUTED={145,158,180,255}; BLUE={83,157,255,255};
            GREEN={52,211,153,255}; RED={255,99,112,255};
            COLORS={{{91,145,226,255},{53,169,151,255},{155,116,226,255},{222,151,83,255},
                     {218,105,133,255},{79,169,206,255},{147,171,94,255},{113,130,158,255},
                     {242,85,65,255},{45,212,191,255},{236,72,153,255},{250,204,21,255}}};
            break;
    }
}

bool hasExtension(const std::string& path,const char* const* extensions,size_t count) {
    const size_t dot=path.find_last_of('.');
    if(dot==std::string::npos)return false;
    std::string ext=path.substr(dot);
    for(char& ch:ext)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for(size_t i=0;i<count;++i)if(ext==extensions[i])return true;
    return false;
}

bool isAllowedMediaPath(const std::string& path,MediaPickerType type) {
    const std::string mediaPrefix=std::string(MEDIA_ROOT)+"/";
    const std::string albumPrefix=std::string(ALBUM_ROOT)+"/";
    const bool mediaPath=path.compare(0,mediaPrefix.size(),mediaPrefix)==0;
    const bool albumPath=type==MediaPickerType::WorkspaceBackground &&
        path.compare(0,albumPrefix.size(),albumPrefix)==0;
    if((!mediaPath&&!albumPath)||path.size()>240||
       path.find("..")!=std::string::npos||path.find('\0')!=std::string::npos)
        return false;
    static const char* imageExts[]={".jpg",".jpeg",".png"};
    static const char* fontExts[]={".ttf",".otf",".ttc"};
    return type==MediaPickerType::Font
        ?hasExtension(path,fontExts,3):hasExtension(path,imageExts,3);
}

void loadLauncherSettings() {
    if(!sdMounted){applyThemePalette();return;}
    languageRussian=true;
    iconStyle=0;
    cardsView=true;
    std::string selectedPath;
    const char* candidates[]={SETTINGS_FILE, SETTINGS_BAK};
    for(const char* candidate:candidates) {
        struct stat st{};
        if(stat(candidate,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>4096)
            continue;
        std::ifstream in(candidate,std::ios::binary);
        std::string header;
        if(in && std::getline(in,header) && header=="DESKNXSET1") {
            selectedPath=candidate;
            break;
        }
    }
    if(selectedPath.empty()) {
        applyThemePalette();
        return;
    }
    if(selectedPath!=SETTINGS_FILE) {
        desknx::diag::Write("settings","backup_restored","path=%s",selectedPath.c_str());
        setStatus("APPEARANCE SETTINGS RESTORED FROM BACKUP",4000);
    }
    std::ifstream in(selectedPath,std::ios::binary);
    std::string line;
    if(!std::getline(in,line)||line!="DESKNXSET1") {
        applyThemePalette();
        return;
    }
    while(std::getline(in,line)) {
        const size_t eq=line.find('=');
        if(eq==std::string::npos)continue;
        const std::string key=line.substr(0,eq),value=line.substr(eq+1);
        if(key=="theme") {
            char* end=nullptr;
            const long valueLong=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&valueLong>=0&&valueLong<=7)
                themeIndex=static_cast<int>(valueLong);
        } else if(key=="wallpaper"&&isAllowedMediaPath(value,MediaPickerType::Wallpaper)) {
            wallpaperPath=value;
        } else if(key=="font"&&isAllowedMediaPath(value,MediaPickerType::Font)) {
            fontPath=value;
        } else if(key=="language") {
            languageRussian=(value=="ru" || value=="russian");
        } else if(key=="iconstyle") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=0&&parsed<=2)iconStyle=static_cast<int>(parsed);
        } else if(key=="view") {
            cardsView=(value=="cards");
        } else if(key=="iconpack") {
            struct stat packStat{};
            const std::string prefix=std::string(MEDIA_ROOT)+"/iconpacks/";
            if(value.compare(0,prefix.size(),prefix)==0&&value.find("..")==std::string::npos&&
               value.size()<240&&stat(value.c_str(),&packStat)==0&&S_ISDIR(packStat.st_mode))
                iconPackPath=value;
        }
    }
    applyThemePalette();
}

bool saveLauncherSettings() {
    if(!sdMounted){setStatus("SD CARD REQUIRED TO SAVE APPEARANCE",3000);return false;}
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);mkdir(MEDIA_ROOT,0777);
    std::ofstream out(SETTINGS_TMP,std::ios::binary|std::ios::trunc);
    if(!out){setStatus("SETTINGS SAVE FAILED",3000);return false;}
    out<<"DESKNXSET1\n"
       <<"theme="<<themeIndex<<"\n"
       <<"wallpaper="<<safeField(wallpaperPath)<<"\n"
       <<"font="<<safeField(fontPath)<<"\n"
       <<"iconpack="<<safeField(iconPackPath)<<"\n"
       <<"language="<<(languageRussian?"ru":"en")<<"\n"
       <<"iconstyle="<<iconStyle<<"\n"
       <<"view="<<(cardsView?"cards":"grid")<<"\n";
    out.flush();const bool good=out.good();out.close();
    if(!good){std::remove(SETTINGS_TMP);setStatus("SETTINGS WRITE FAILED",3000);return false;}
    if(!commitSdCard("settings-temp")) {
        std::remove(SETTINGS_TMP);
        setStatus("SETTINGS SD COMMIT FAILED",3000);
        return false;
    }
    const auto rc=desknx::safeio::replaceFilePreservingOnFailure(
        SETTINGS_TMP,SETTINGS_FILE,SETTINGS_BAK,true);
    if(rc!=desknx::safeio::ReplaceStatus::Success) {
        std::remove(SETTINGS_TMP);
        setStatus("SETTINGS PUBLISH FAILED",3000);
        return false;
    }
    if(!commitSdCard("settings-publish")) {
        setStatus("SETTINGS COMMIT FAILED; BACKUP KEPT",4000);
        return false;
    }
    return true;
}

SDL_Surface* limitBackgroundSurface(SDL_Surface* source) {
    if(!source)return nullptr;
    constexpr int MaxBackgroundWidth=1600;
    constexpr int MaxBackgroundHeight=900;
    if(source->w<=MaxBackgroundWidth&&source->h<=MaxBackgroundHeight)
        return source;
    const double scale=std::min(
        static_cast<double>(MaxBackgroundWidth)/source->w,
        static_cast<double>(MaxBackgroundHeight)/source->h);
    const int width=std::max(1,static_cast<int>(source->w*scale));
    const int height=std::max(1,static_cast<int>(source->h*scale));
    SDL_Surface* resized=SDL_CreateRGBSurfaceWithFormat(
        0,width,height,32,SDL_PIXELFORMAT_RGBA32);
    if(!resized)return source;
    // Preserve the original alpha channel while resampling PNG backgrounds.
    SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);
    if(SDL_BlitScaled(source,nullptr,resized,nullptr)!=0) {
        SDL_FreeSurface(resized);
        return source;
    }
    SDL_FreeSurface(source);
    return resized;
}

bool loadWallpaperTexture(const std::string& path) {
    if(path.empty()) {
        if(wallpaperTexture)SDL_DestroyTexture(wallpaperTexture);
        wallpaperTexture=nullptr;wallpaperWidth=wallpaperHeight=0;
        return true;
    }
    if(!isAllowedMediaPath(path,MediaPickerType::Wallpaper))return false;
    struct stat st{};
    if(stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0||
       st.st_size>24LL*1024*1024)return false;
    SDL_Surface* surface=IMG_Load(path.c_str());
    if(!surface)return false;
    const bool sizeOk=surface->w>0&&surface->h>0&&surface->w<=4096&&surface->h<=4096&&
        static_cast<uint64_t>(surface->w)*static_cast<uint64_t>(surface->h)<=16ULL*1024ULL*1024ULL;
    if(!sizeOk){SDL_FreeSurface(surface);return false;}
    surface=limitBackgroundSurface(surface);
    SDL_Texture* next=SDL_CreateTextureFromSurface(renderer,surface);
    const int width=surface->w,height=surface->h;
    SDL_FreeSurface(surface);
    if(!next)return false;
    SDL_SetTextureBlendMode(next,SDL_BLENDMODE_NONE);
    if(wallpaperTexture)SDL_DestroyTexture(wallpaperTexture);
    wallpaperTexture=next;wallpaperWidth=width;wallpaperHeight=height;
    return true;
}

void drawWallpaper() {
    if(!wallpaperTexture||wallpaperWidth<=0||wallpaperHeight<=0)return;
    const auto fit=desknx::artwork::coverDestination(0,0,SW,SH,wallpaperWidth,wallpaperHeight);
    SDL_Rect dst{fit.x,fit.y,fit.w,fit.h},viewport{0,0,SW,SH};
    SDL_RenderSetClipRect(renderer,&viewport);
    SDL_RenderCopy(renderer,wallpaperTexture,nullptr,&dst);
    SDL_RenderSetClipRect(renderer,nullptr);
    SDL_SetRenderDrawColor(renderer,3,7,14,92);
    SDL_RenderFillRect(renderer,&viewport);
}

SDL_Texture* getWorkspaceBackgroundTexture(const std::string& path,int& width,int& height) {
    if(path.empty()||!isAllowedMediaPath(path,MediaPickerType::WorkspaceBackground))return nullptr;
    for(auto& entry:workspaceBackgroundTextures) {
        if(entry.path==path) {
            entry.lastUse=++workspaceBackgroundClock;
            width=entry.width;height=entry.height;return entry.texture;
        }
    }
    struct stat st{};
    if(stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0||
       st.st_size>24LL*1024*1024)return nullptr;
    SDL_Surface* surface=IMG_Load(path.c_str());
    if(!surface)return nullptr;
    const bool valid=surface->w>0&&surface->h>0&&surface->w<=4096&&surface->h<=4096&&
        static_cast<uint64_t>(surface->w)*static_cast<uint64_t>(surface->h)<=16ULL*1024ULL*1024ULL;
    if(!valid){SDL_FreeSurface(surface);return nullptr;}
    surface=limitBackgroundSurface(surface);
    SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,surface);
    const int imageWidth=surface->w,imageHeight=surface->h;
    SDL_FreeSurface(surface);
    if(!texture)return nullptr;
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
    if(workspaceBackgroundTextures.size()>=3) {
        auto oldest=workspaceBackgroundTextures.begin();
        for(auto it=workspaceBackgroundTextures.begin();it!=workspaceBackgroundTextures.end();++it)
            if(it->lastUse<oldest->lastUse)oldest=it;
        if(oldest->texture)SDL_DestroyTexture(oldest->texture);
        workspaceBackgroundTextures.erase(oldest);
    }
    workspaceBackgroundTextures.push_back({path,texture,imageWidth,imageHeight,++workspaceBackgroundClock});
    width=imageWidth;height=imageHeight;return texture;
}

void drawWorkspaceBackground(const Workspace& desk) {
    if(desk.backgroundPath.empty()){drawWallpaper();return;}
    int width=0,height=0;
    SDL_Texture* texture=getWorkspaceBackgroundTexture(desk.backgroundPath,width,height);
    if(!texture||width<=0||height<=0){drawWallpaper();return;}
    const auto fit=desknx::artwork::destination(0,0,SW,SH,width,height,
        static_cast<desknx::artwork::FitMode>(desk.backgroundFit));
    SDL_Rect dst{fit.x,fit.y,fit.w,fit.h},viewport{0,0,SW,SH};
    SDL_RenderSetClipRect(renderer,&viewport);
    SDL_RenderCopy(renderer,texture,nullptr,&dst);
    SDL_RenderSetClipRect(renderer,nullptr);
    SDL_SetRenderDrawColor(renderer,3,7,14,92);
    SDL_RenderFillRect(renderer,&viewport);
}

void refreshMediaPicker() {
    const Uint64 started=SDL_GetTicks64();
    mediaPickerEntries.clear();
    DIR* dir=opendir(mediaPickerDirectory.c_str());
    if(!dir) {
        desknx::diag::Write("media","picker_scan_failed","path=%s errno=%d",
            mediaPickerDirectory.c_str(),errno);
        setStatus("CANNOT OPEN MEDIA FOLDER",2500);
        return;
    }
    while(dirent* entry=readdir(dir)) {
        const char* name=entry->d_name;
        if(!name||name[0]=='.')continue;
        const std::string path=mediaPickerDirectory+"/"+name;
        struct stat st{};
        if(lstat(path.c_str(),&st)!=0||S_ISLNK(st.st_mode))continue;
        if(S_ISDIR(st.st_mode))mediaPickerEntries.push_back({name,path,true});
        else if(S_ISREG(st.st_mode)&&isAllowedMediaPath(path,mediaPickerType))
            mediaPickerEntries.push_back({name,path,false});
        if(mediaPickerEntries.size()>=256)break;
    }
    closedir(dir);
    std::sort(mediaPickerEntries.begin(),mediaPickerEntries.end(),
        [](const MediaEntry& a,const MediaEntry& b) {
            if(a.directory!=b.directory)return a.directory>b.directory;
            return a.name<b.name;
        });
    mediaPickerIndex=std::max(0,std::min(mediaPickerIndex,
        std::max(0,static_cast<int>(mediaPickerEntries.size())-1)));
    mediaPickerOffset=std::max(0,std::min(mediaPickerOffset,
        std::max(0,static_cast<int>(mediaPickerEntries.size())-12)));
    desknx::diag::Write("media","picker_scan_complete",
        "path=%s entries=%zu elapsed_ms=%llu",
        mediaPickerDirectory.c_str(),mediaPickerEntries.size(),
        static_cast<unsigned long long>(SDL_GetTicks64()-started));
}

void openMediaPicker(MediaPickerType type) {
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);mkdir(MEDIA_ROOT,0777);
    mkdir((std::string(MEDIA_ROOT)+"/iconpacks").c_str(),0777);
    mediaPickerType=type;
    mediaPickerDirectory=(type==MediaPickerType::IconPack)
        ?std::string(MEDIA_ROOT)+"/iconpacks":std::string(MEDIA_ROOT);
    mediaPickerIndex=mediaPickerOffset=0;
    refreshMediaPicker();
    mediaPickerOpen=true;
}

void mediaPickerBack() {
    if(mediaPickerDirectory==MEDIA_ROOT) {
        mediaPickerOpen=false;
        return;
    }
    if(mediaPickerDirectory==ALBUM_ROOT) {
        mediaPickerDirectory=MEDIA_ROOT;
        mediaPickerIndex=mediaPickerOffset=0;
        refreshMediaPicker();
        return;
    }
    const size_t slash=mediaPickerDirectory.find_last_of('/');
    mediaPickerDirectory=(slash==std::string::npos)?std::string(MEDIA_ROOT):
        mediaPickerDirectory.substr(0,slash);
    mediaPickerIndex=mediaPickerOffset=0;
    refreshMediaPicker();
}
bool saveLayout();

void applyMediaPickerSelection(int index) {
    if(index<0||index>=static_cast<int>(mediaPickerEntries.size()))return;
    const MediaEntry selected=mediaPickerEntries[static_cast<size_t>(index)];
    if(selected.directory && mediaPickerType==MediaPickerType::IconPack) {
        const std::string prefix=std::string(MEDIA_ROOT)+"/iconpacks/";
        if(selected.path.compare(0,prefix.size(),prefix)!=0 ||
           selected.path.find("..")!=std::string::npos) {
            setStatus("INVALID ICON PACK FOLDER",2500);
            return;
        }
        iconPackPath=selected.path;
        gIconCache.setCustomIconPackDir(iconPackPath);
        if(saveLauncherSettings())setStatus("ICON PACK APPLIED");
        mediaPickerOpen=false;
        return;
    }
    if(selected.directory) {
        mediaPickerDirectory=selected.path;
        mediaPickerIndex=mediaPickerOffset=0;
        refreshMediaPicker();
        return;
    }
    if(mediaPickerType==MediaPickerType::IconPack) {
        setStatus("CHOOSE A FOLDER TO APPLY AN ICON PACK",2500);
        return;
    }
    if(mediaPickerType==MediaPickerType::WorkspaceBackground) {
        if (!isAllowedMediaPath(selected.path,MediaPickerType::WorkspaceBackground)) {
            setStatus("INVALID DESKTOP BACKGROUND",3000); return;
        }
        Workspace& desk=desks[static_cast<size_t>(deskIndex)];
        desk.backgroundPath=selected.path;
        layoutUserModified=true;
        if(saveLayout())setStatus("DESKTOP BACKGROUND APPLIED");
        mediaPickerOpen=false;
        return;
    }
    if(mediaPickerType==MediaPickerType::Wallpaper) {
        if(!loadWallpaperTexture(selected.path)) {
            setStatus("IMAGE LOAD FAILED - USE JPG OR PNG",3500);
            return;
        }
        wallpaperPath=selected.path;
    } else {
        if(!gFontRenderer.setCustomFont(selected.path)) {
            setStatus("FONT LOAD FAILED - CHOOSE ANOTHER FILE",3500);
            return;
        }
        fontPath=selected.path;
    }
    if(saveLauncherSettings())setStatus(mediaPickerType==MediaPickerType::Wallpaper
        ?"WALLPAPER APPLIED":"CUSTOM FONT APPLIED");
    mediaPickerOpen=false;
}

void roundRect(int x,int y,int w,int h,int radius,Color c) {
    if(w<=0||h<=0) return;
    // One consistent 10 px corner radius across cards, tiles, controls and modals.
    const int r=std::max(0,std::min(UiCornerRadius,std::min(radius,std::min(w,h)/2)));
    color(c);
    if(r==0) {
        SDL_Rect full{x,y,w,h};
        SDL_RenderFillRect(renderer,&full);
        return;
    }
    // Fill the centre with one SDL call and draw only the rounded corner bands.
    // The previous implementation emitted one draw call for every row of each
    // card and tile - hundreds of calls per frame on the home screen.
    if(h>2*r) {
        SDL_Rect middle{x,y+r,w,h-2*r};
        SDL_RenderFillRect(renderer,&middle);
    }
    for(int row=0;row<r;++row) {
        const double dy=static_cast<double>(r-row);
        const int inset=static_cast<int>(std::ceil(r-std::sqrt(
            std::max(0.0,static_cast<double>(r*r)-dy*dy))));
        const int left=x+inset,right=x+w-inset-1;
        SDL_RenderDrawLine(renderer,left,y+row,right,y+row);
        SDL_RenderDrawLine(renderer,left,y+h-1-row,right,y+h-1-row);
    }
}
void softCard(int x,int y,int w,int h,Color fill,Color edge=Color{255,255,255,20}) {
    roundRect(x,y+5,w,h,UiCornerRadius,Color{0,0,0,54});
    roundRect(x,y,w,h,UiCornerRadius,fill);
    roundRect(x+1,y+1,w-2,h-2,UiCornerRadius-1,edge);
}
std::array<Uint8,7> glyph(char input) {
    const char c=static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
    switch(c) {
        case 'A':return {{14,17,17,31,17,17,17}}; case 'B':return {{30,17,17,30,17,17,30}};
        case 'C':return {{14,17,16,16,16,17,14}}; case 'D':return {{30,17,17,17,17,17,30}};
        case 'E':return {{31,16,16,30,16,16,31}}; case 'F':return {{31,16,16,30,16,16,16}};
        case 'G':return {{14,17,16,23,17,17,15}}; case 'H':return {{17,17,17,31,17,17,17}};
        case 'I':return {{31,4,4,4,4,4,31}}; case 'J':return {{7,2,2,2,18,18,12}};
        case 'K':return {{17,18,20,24,20,18,17}}; case 'L':return {{16,16,16,16,16,16,31}};
        case 'M':return {{17,27,21,21,17,17,17}}; case 'N':return {{17,25,21,19,17,17,17}};
        case 'O':return {{14,17,17,17,17,17,14}}; case 'P':return {{30,17,17,30,16,16,16}};
        case 'Q':return {{14,17,17,17,21,18,13}}; case 'R':return {{30,17,17,30,20,18,17}};
        case 'S':return {{15,16,16,14,1,1,30}}; case 'T':return {{31,4,4,4,4,4,4}};
        case 'U':return {{17,17,17,17,17,17,14}}; case 'V':return {{17,17,17,17,17,10,4}};
        case 'W':return {{17,17,17,21,21,21,10}}; case 'X':return {{17,17,10,4,10,17,17}};
        case 'Y':return {{17,17,10,4,4,4,4}}; case 'Z':return {{31,1,2,4,8,16,31}};
        case '0':return {{14,17,19,21,25,17,14}}; case '1':return {{4,12,4,4,4,4,14}};
        case '2':return {{14,17,1,2,4,8,31}}; case '3':return {{30,1,1,14,1,1,30}};
        case '4':return {{2,6,10,18,31,2,2}}; case '5':return {{31,16,16,30,1,1,30}};
        case '6':return {{14,16,16,30,17,17,14}}; case '7':return {{31,1,2,4,8,8,8}};
        case '8':return {{14,17,17,14,17,17,14}}; case '9':return {{14,17,17,15,1,1,14}};
        case ':':return {{0,4,4,0,4,4,0}}; case '.':return {{0,0,0,0,0,12,12}};
        case ',':return {{0,0,0,0,4,4,8}}; case '-':return {{0,0,0,31,0,0,0}};
        case '_':return {{0,0,0,0,0,0,31}}; case '/':return {{1,2,2,4,8,8,16}};
        case '+':return {{0,4,4,31,4,4,0}}; case '%':return {{17,2,4,8,17,0,0}};
        case '!':return {{4,4,4,4,4,0,4}}; case '?':return {{14,17,1,2,4,0,4}};
        case '(':return {{2,4,8,8,8,4,2}}; case ')':return {{8,4,2,2,2,4,8}};
        case '[':return {{14,8,8,8,8,8,14}}; case ']':return {{14,2,2,2,2,2,14}};
        case '#':return {{10,31,10,10,31,10,0}}; case '=':return {{0,31,0,31,0,0,0}};
        case ' ':return {{0,0,0,0,0,0,0}};
        default:return {{31,17,1,2,4,0,4}};
    }
}
// Bitmap fallback pre-processing: keep printable ASCII as-is, map every other
// UTF-8 sequence to a single '?' replacement cell (glyph() draws it as a box).
std::string fallbackAscii(const std::string& input) {
    std::string out;
    for(size_t i=0;i<input.size();) {
        const unsigned char b=static_cast<unsigned char>(input[i]);
        if(b<128) { out.push_back(static_cast<char>(b)); ++i; continue; }
        size_t n=1;
        if((b&0xE0)==0xC0) n=2; else if((b&0xF0)==0xE0) n=3; else if((b&0xF8)==0xF0) n=4;
        if(i+n>input.size()) n=input.size()-i;
        i+=n; out.push_back('?');
    }
    return out;
}
// UI-20: the transliteration helper is removed. When the shared-font path is
// unavailable (pl/TTF init failed) we fall back to the bitmap glyphs, which
// pass ASCII through and draw U+FFFD-style replacement boxes for anything
// else (default glyph in glyph()) instead of silently dropping characters.
void text(const std::string& value,int x,int y,int scale,Color c,int right=1260) {
    const std::string localized=desknx::i18n::translate(value,languageRussian);
    if(gFontRenderer.draw(localized,x,y,scale,SDL_Color{c.r,c.g,c.b,c.a},right))return;
    const std::string s=fallbackAscii(localized); color(c); int px=x;
    for(char ch:s) {
        if(px+5*scale>right) break;
        const auto rows=glyph(ch);
        for(int ry=0;ry<7;++ry) for(int rx=0;rx<5;++rx) if(rows[ry]&(1u<<(4-rx))) {
            SDL_Rect p{px+rx*scale,y+ry*scale,scale,scale}; SDL_RenderFillRect(renderer,&p);
        }
        px+=6*scale;
    }
}
std::string clockText(bool date=false) {
    std::time_t t=std::time(nullptr); std::tm* tmv=std::localtime(&t); if(!tmv) return "--:--";
    char b[64]{}; std::strftime(b,sizeof(b),date?"%H:%M  %d-%m-%Y":"%H:%M",tmv); return b;
}
std::string typeName(Kind k) {
    switch(k) { case Kind::Game:return "GAME"; case Kind::Nro:return "HOMEBREW"; case Kind::Clock:return "WIDGET / CLOCK";
        case Kind::Battery:return "WIDGET / BATTERY"; case Kind::System:return "WIDGET / SYSTEM"; case Kind::Recent:return "WIDGET / RECENT";
        case Kind::Storage:return "WIDGET / STORAGE"; case Kind::Calendar:return "WIDGET / CALENDAR";
        case Kind::Unsupported:return "UNSUPPORTED"; }
    return "ITEM";
}

std::vector<Candidate> catalogueSnapshot() {
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    auto result=catalogue;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
    return result;
}
const std::vector<Candidate>& visibleCatalogueSnapshot() {
    std::vector<Candidate> all;
    uint64_t revision = 0;
    if(catalogueMutex) {
        SDL_LockMutex(catalogueMutex);
        revision = catalogueRevision;
        if(revision == visibleCacheCatalogueRevision &&
           catalogueViewRevision == visibleCacheViewRevision &&
           recentLaunchRevision == visibleCacheRecentRevision) {
            SDL_UnlockMutex(catalogueMutex);
            return visibleCatalogueCache;
        }
        all = catalogue;
        SDL_UnlockMutex(catalogueMutex);
    } else {
        revision = catalogueRevision;
        if(revision == visibleCacheCatalogueRevision &&
           catalogueViewRevision == visibleCacheViewRevision &&
           recentLaunchRevision == visibleCacheRecentRevision)
            return visibleCatalogueCache;
        all = catalogue;
    }
    std::vector<desknx::catalog_view::RecentKey> recent;
    recent.reserve(recentLaunches.size());
    for(const auto& item:recentLaunches) recent.push_back({item.kind,item.data});
    visibleCatalogueCache = desknx::catalog_view::FilterAndSort(
        all,catalogueQuery,catalogueFilter,catalogueSort,recent);
    visibleCacheCatalogueRevision = revision;
    visibleCacheViewRevision = catalogueViewRevision;
    visibleCacheRecentRevision = recentLaunchRevision;
    return visibleCatalogueCache;
}
Result showKeyboard(SwkbdConfig* config, char* output, size_t outputSize);
void searchCatalogue() {
    const Uint64 keyboardStarted=SDL_GetTicks64();
    SwkbdConfig config{};
    const Result createRc=swkbdCreate(&config,0);
    desknx::diag::Write("keyboard","search_create","rc=0x%08X elapsed_ms=%llu",
        static_cast<unsigned int>(createRc),static_cast<unsigned long long>(SDL_GetTicks64()-keyboardStarted));
    if(R_FAILED(createRc)){setStatus("SEARCH KEYBOARD UNAVAILABLE",2500);return;}
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,languageRussian?"Поиск игр и Homebrew":"Search games and Homebrew");
    swkbdConfigSetInitialText(&config,catalogueQuery.c_str());
    char value[128]{};
    std::snprintf(value,sizeof(value),"%s",catalogueQuery.c_str());
    const Result rc=showKeyboard(&config,value,sizeof(value));
    swkbdClose(&config);
    desknx::diag::Write("keyboard","search_complete","rc=0x%08X total_ms=%llu result_bytes=%zu",
        static_cast<unsigned int>(rc),static_cast<unsigned long long>(SDL_GetTicks64()-keyboardStarted),std::strlen(value));
    if(R_SUCCEEDED(rc)) {
        catalogueQuery=value;
        ++catalogueViewRevision;
        listIndex=0;listOffset=0;
        setStatus(catalogueQuery.empty()?"SEARCH CLEARED":"SEARCH: "+catalogueQuery,2500);
    }
}
void cycleCatalogueFilter(int direction) {
    constexpr int count=4;
    int current=static_cast<int>(catalogueFilter);
    current=(current+direction+count)%count;
    catalogueFilter=static_cast<desknx::catalog_view::FilterMode>(current);
    ++catalogueViewRevision;
    listIndex=0;listOffset=0;
    setStatus(std::string("FILTER: ")+desknx::catalog_view::FilterName(catalogueFilter),1800);
}
void cycleCatalogueSort() {
    constexpr int count=3;
    int current=(static_cast<int>(catalogueSort)+1)%count;
    catalogueSort=static_cast<desknx::catalog_view::SortMode>(current);
    ++catalogueViewRevision;
    listIndex=0;listOffset=0;
    setStatus(std::string("SORT: ")+desknx::catalog_view::SortName(catalogueSort),1800);
}
void publishCatalogue(const desknx::catalog::ScanResult& result) {
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    catalogue=result.items;
    ++catalogueRevision;
    catalogueTruncated=result.truncated;
    catalogueReady=!catalogue.empty()||result.complete;
    catalogueScanComplete=result.complete;
    catalogueNsAvailable=result.nsAvailable;
    catalogueNsError=result.nsErrorCode;
    catalogueGameCount=result.gameCount;
    catalogueNroCount=result.nroCount;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
}
void readCatalogueState(bool& ready,bool& complete,bool& truncated,
                        bool* nsAvailable=nullptr,std::uint32_t* nsError=nullptr,
                        size_t* gameCount=nullptr,size_t* nroCount=nullptr) {
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    ready=catalogueReady;
    complete=catalogueScanComplete;
    truncated=catalogueTruncated;
    if(nsAvailable)*nsAvailable=catalogueNsAvailable;
    if(nsError)*nsError=catalogueNsError;
    if(gameCount)*gameCount=catalogueGameCount;
    if(nroCount)*nroCount=catalogueNroCount;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
}
void catalogueProgress(const desknx::catalog::ScanResult& snapshot, void*) {
    publishCatalogue(snapshot);
}
int nextendoThreadMain(void*) {
    bool networkReady = false;
    std::uint64_t nextAttempt = SDL_GetTicks64() + 8000;
    while (!SDL_AtomicGet(&nextendoStop)) {
        const std::uint64_t now = SDL_GetTicks64();
        if (now >= nextAttempt) {
            if (!networkReady) {
                networkReady = desknx::nextendo::initializeNetwork();
                desknx::diag::Write("nextendo", networkReady ? "network_ready" : "network_init_failed",
                    "retry_ms=%u", networkReady ? 0u : 30000u);
                if (!networkReady) {
                    nextAttempt = now + 30000;
                    SDL_Delay(250);
                    continue;
                }
            }
            desknx::nextendo::PlayerSnapshot snapshot;
            const bool ok = desknx::nextendo::fetchSnapshot(snapshot);
            if (ok && nextendoMutex) {
                SDL_LockMutex(nextendoMutex);
                const bool applied = nextendoFeed.apply(snapshot, SDL_GetTicks64());
                SDL_UnlockMutex(nextendoMutex);
                desknx::diag::Write("nextendo", applied ? "snapshot_updated" : "snapshot_rejected",
                    "titles=%zu total=%u", snapshot.perTitle.size(), snapshot.total);
            } else {
                desknx::diag::Write("nextendo", "snapshot_failed",
                    "network_ready=%d", networkReady ? 1 : 0);
            }
            nextAttempt = now + desknx::nextendo::RefreshIntervalMs;
        }
        SDL_Delay(250);
    }
    if (networkReady) desknx::nextendo::shutdownNetwork();
    return 0;
}

unsigned nextendoPlayersForTitle(const std::string& titleId) {
    if (!nextendoMutex || !desknx::model::isTitleId(titleId)) return 0;
    char* end = nullptr;
    const unsigned long long id = std::strtoull(titleId.c_str(), &end, 16);
    if (end != titleId.c_str() + titleId.size() || id == 0) return 0;
    SDL_LockMutex(nextendoMutex);
    const unsigned count = nextendoFeed.playersFor(static_cast<std::uint64_t>(id), SDL_GetTicks64());
    SDL_UnlockMutex(nextendoMutex);
    return count;
}

std::string nextendoTotalLabel() {
    if (!nextendoMutex) return "NEXTENDO --";
    SDL_LockMutex(nextendoMutex);
    const std::uint64_t now = SDL_GetTicks64();
    const bool fresh = nextendoFeed.hasTotal(now);
    const unsigned total = nextendoFeed.total(now);
    SDL_UnlockMutex(nextendoMutex);
    return fresh ? "NEXTENDO " + std::to_string(total) : "NEXTENDO --";
}

int catalogueThreadMain(void* context) {
    const auto* request=static_cast<const CatalogueScanRequest*>(context);
    const bool forceRefresh=request&&request->forceRefresh;
    if(request&&request->reinitializeNs) {
        std::uint32_t serviceError=0;
        const bool ready=desknx::catalog::RefreshNsService(serviceError);
        desknx::diag::Write("catalogue","ns_service_refresh",
            "ready=%d rc=0x%08X",ready?1:0,
            static_cast<unsigned int>(serviceError));
    }
    desknx::diag::Write("catalogue", "thread_begin",
        "background scan started force_refresh=%d", forceRefresh ? 1 : 0);
    const auto result = desknx::catalog::Scan(catalogueProgress,nullptr,forceRefresh);
    desknx::diag::Write("catalogue", "thread_end",
        "complete=%d games=%zu nros=%zu items=%zu ns_available=%d ns_error=0x%08X truncated=%d",
        result.complete ? 1 : 0, result.gameCount, result.nroCount, result.items.size(),
        result.nsAvailable ? 1 : 0, static_cast<unsigned int>(result.nsErrorCode),
        result.truncated ? 1 : 0);
    return 0;
}
uint64_t cachedNroFileSize(const std::string& path) {
    const auto cached=nroSizeCache.find(path);
    if(cached!=nroSizeCache.end())return cached->second;
    struct stat st{};
    uint64_t bytes=0;
    if(stat(path.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0)
        bytes=static_cast<uint64_t>(st.st_size);
    if(nroSizeCache.size()>=1024)nroSizeCache.clear();
    nroSizeCache.emplace(path,bytes);
    return bytes;
}

void restartCatalogueScan() {
    bool complete = false;
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    complete = catalogueScanComplete;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
    if(!complete) {
        setStatus("LIBRARY SCAN IS ALREADY RUNNING",2500);
        return;
    }
    if(catalogueThread) {
        SDL_WaitThread(catalogueThread,nullptr);
        catalogueThread=nullptr;
    }
    // Keep the last good catalogue on screen. NS reinitialization and
    // enumeration happen inside the catalogue worker, not the render thread.
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    catalogueScanComplete=false;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
    catalogueWarningShown=false;
    nroSizeCache.clear();
    tileTitleWrapCache.clear();
    listIndex=0;
    listOffset=0;
    catalogueThread=SDL_CreateThread(catalogueThreadMain,
        "DeskNX catalogue refresh",
        const_cast<CatalogueScanRequest*>(&catalogueRefreshRequest));
    if(!catalogueThread) {
        if(catalogueMutex) SDL_LockMutex(catalogueMutex);
        catalogueScanComplete=true;
        if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
        setStatus("BACKGROUND RESCAN UNAVAILABLE; OLD LIBRARY KEPT",4000);
    } else {
        setStatus("RESCANNING LIBRARY IN BACKGROUND",3500);
    }
}
#ifdef DESKNX_SYSTEM_APPLET
template<typename Sender>
Result sendIpc(Sender sender) {
    if(ipcMutex) SDL_LockMutex(ipcMutex);
    const Result rc=sender();
    if(ipcMutex) SDL_UnlockMutex(ipcMutex);
    return rc;
}
Result showKeyboard(SwkbdConfig* config, char* output, size_t outputSize) {
    // The independent heartbeat thread remains active while swkbdShow owns the
    // foreground. A synchronous BUSY IPC round-trip delayed keyboard startup.
    const Uint64 started=SDL_GetTicks64();
    const Result dialogRc=swkbdShow(config,output,outputSize);
    desknx::diag::Write("keyboard","dialog_return","rc=0x%08X elapsed_ms=%llu",
        static_cast<unsigned int>(dialogRc),static_cast<unsigned long long>(SDL_GetTicks64()-started));
    return dialogRc;
}
int heartbeatThreadMain(void*) {
