#include <switch.h>
#include <switch/applets/hid_la.h>
#include <switch/applets/album_la.h>
#include <switch/applets/friends_la.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <desknx/Protocol.hpp>
#include <desknx/SafeReplace.hpp>
#include <desknx/PersistentFiles.hpp>
#include "../include/desknx/SettingsFile.hpp"
#include "../include/desknx/Model.hpp"
#include "../include/desknx/LaunchHistory.hpp"
#include "../include/desknx/WorkspaceOps.hpp"
#include "../include/desknx/Layout.hpp"
#include "catalog.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/Diagnostics.hpp"
#include "../include/desknx/Localization.hpp"
#include "../include/desknx/NextendoPlayers.hpp"
#include "catalog_view.hpp"
#include "../include/desknx/Artwork.hpp"
#include "FontRenderer.hpp"
#include "IconCache.hpp"
#include "SteamGridDbClient.hpp"
#include "SteamGridDbCache.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <climits>
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
#include <new>

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
size_t layoutRecoveredTiles = 0, layoutDroppedTiles = 0;
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
int themeIndex=1; // v0.1 palette preset; retained for backwards compatibility
int visualThemeIndex=0; // 0 = v0.1 legacy renderer, 1 = socoma renderer
bool useSocomaTheme() { return visualThemeIndex==1; }
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
std::string steamGridDbApiKey;

namespace sgdb = desknx::steamgriddb_api;
struct SteamGridArtworkChoice {
    sgdb::IconAsset asset;
    std::string type;
};
enum class SteamGridPickerStage { Closed, SearchingGames, ChooseGame, LoadingArtwork, ChooseArtwork, Applying };
enum class SteamGridJob { None, SearchGames, LoadArtwork, PreviewArtwork, ApplyArtwork };
struct SteamGridTask {
    SteamGridJob job{SteamGridJob::None};
    std::string apiKey, query, gameName, assetUrl;
    std::uint64_t titleId{0}, gameId{0}, assetId{0};
};
struct SteamGridWorkerResult {
    bool success{false};
    bool applied{false};
    std::string error;
    std::vector<sgdb::GameMatch> games;
    std::vector<SteamGridArtworkChoice> artwork;
    std::vector<unsigned char> previewBytes;
    std::uint64_t previewAssetId{0};
};
SDL_mutex* steamGridMutex=nullptr;
SDL_Thread* steamGridThread=nullptr;
SDL_atomic_t steamGridDone{};
SteamGridJob steamGridActiveJob=SteamGridJob::None;
SteamGridWorkerResult steamGridWorkerResult;
bool steamGridPickerOpen=false;
SteamGridPickerStage steamGridPickerStage=SteamGridPickerStage::Closed;
std::uint64_t steamGridTargetTitleId=0;
std::string steamGridTargetTitle, steamGridSearchQuery, steamGridSelectedGameName;
std::vector<sgdb::GameMatch> steamGridGames;
std::vector<SteamGridArtworkChoice> steamGridArtwork;
std::string steamGridError;
SDL_Texture* steamGridPreviewTexture=nullptr;
int steamGridPreviewWidth=0, steamGridPreviewHeight=0;
std::uint64_t steamGridPreviewAssetId=0, steamGridPreviewWantedAssetId=0;
bool steamGridPreviewLoading=false;
std::string steamGridPreviewError;
int steamGridListIndex=0, steamGridListOffset=0;
bool startSteamGridJob(SteamGridTask* task);
void requestSteamGridPreview();
void clearSteamGridPreview();
bool sameAccountUid(const AccountUid& a,const AccountUid& b);
FontRenderer gFontRenderer;
IconCache gIconCache;
using HistoryEntry = desknx::launch_history::Entry;
std::vector<HistoryEntry> recentLaunches;
std::vector<HistoryEntry> recentUniqueCache;
uint64_t recentUniqueCacheRevision=UINT64_MAX;
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
bool editing=false, dragging=false, listOpen=false, recentOpen=false, systemMenu=false, appSettingsMenu=false, workspaceMenu=false, workspaceSettingsMenu=false, workspacePicker=false, workspacePickerMoveTile=false, workspaceReorderOpen=false, consoleSettingsOpen=false, deletePrompt=false, running=true;
int workspaceReorderIndex=0, consoleSettingsIndex=0;
bool consoleWifiEnabled=true, consoleBluetoothEnabled=true, consoleAirplaneMode=false;
bool consoleSettingsLblReady=false;
float consoleBrightness=0.5f;
int recentIndex=0, recentOffset=0, systemMenuIndex=0, appSettingsIndex=0, workspaceMenuIndex=0, workspaceSettingsIndex=0, workspacePickerIndex=0, lastWorkspaceTabTap=-1;
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

Result setConsoleAirplaneMode(bool enabled) {
    Service root{},general{};
    Result rc=smGetService(&root,"nifm:s");
    if(R_SUCCEEDED(rc))rc=serviceConvertToDomain(&root);
    if(R_SUCCEEDED(rc)) {
        serviceAssumeDomain(&root);
        const u64 reserved=0;
        rc=serviceDispatchIn(&root,5,reserved,.in_send_pid=true,
            .out_num_objects=1,.out_objects=&general);
    }
    if(R_SUCCEEDED(rc)) {
        serviceAssumeDomain(&general);
        const u8 wirelessEnabled=enabled?0:1;
        rc=serviceDispatchIn(&general,16,wirelessEnabled);
    }
    serviceClose(&general);
    serviceClose(&root);
    return rc;
}

bool readConsoleAirplaneMode(bool& enabled) {
    Service root{},general{};
    Result rc=smGetService(&root,"nifm:s");
    if(R_SUCCEEDED(rc))rc=serviceConvertToDomain(&root);
    if(R_SUCCEEDED(rc)) {
        serviceAssumeDomain(&root);
        const u64 reserved=0;
        rc=serviceDispatchIn(&root,5,reserved,.in_send_pid=true,
            .out_num_objects=1,.out_objects=&general);
    }
    if(R_SUCCEEDED(rc)) {
        serviceAssumeDomain(&general);
        u8 wirelessEnabled=1;
        rc=serviceDispatchOut(&general,17,wirelessEnabled);
        if(R_SUCCEEDED(rc))enabled=(wirelessEnabled&1)==0;
    }
    serviceClose(&general);
    serviceClose(&root);
    return R_SUCCEEDED(rc);
}

void refreshConsoleSettings() {
    (void)setsysGetWirelessLanEnableFlag(&consoleWifiEnabled);
    (void)setsysGetBluetoothEnableFlag(&consoleBluetoothEnabled);
    (void)readConsoleAirplaneMode(consoleAirplaneMode);
    if(!consoleSettingsLblReady&&R_SUCCEEDED(lblInitialize()))
        consoleSettingsLblReady=true;
    if(consoleSettingsLblReady) {
        float value=consoleBrightness;
        if(R_SUCCEEDED(lblGetCurrentBrightnessSetting(&value)))
            consoleBrightness=std::max(0.05f,std::min(1.0f,value));
    }
}

void adjustConsoleBrightness(float delta) {
    if(!consoleSettingsLblReady&&R_SUCCEEDED(lblInitialize()))
        consoleSettingsLblReady=true;
    if(!consoleSettingsLblReady) {
        setStatus("DISPLAY CONTROL UNAVAILABLE");
        return;
    }
    consoleBrightness=std::max(0.05f,std::min(1.0f,consoleBrightness+delta));
    const Result rc=lblSetCurrentBrightnessSetting(consoleBrightness);
    if(R_FAILED(rc))setStatus("BRIGHTNESS CHANGE FAILED");
}

void activateConsoleSetting(int action) {
    Result rc=0;
    switch(action) {
        case 0:
            if(!consoleWifiEnabled&&consoleAirplaneMode) {
                rc=setConsoleAirplaneMode(false);
                if(R_FAILED(rc))break;
                consoleAirplaneMode=false;
            }
            rc=setsysSetWirelessLanEnableFlag(!consoleWifiEnabled);
            break;
        case 1:
            rc=setsysSetBluetoothEnableFlag(!consoleBluetoothEnabled);
            break;
        case 2:
            rc=setConsoleAirplaneMode(!consoleAirplaneMode);
            break;
        case 3:
            adjustConsoleBrightness(0.1f);
            return;
        case 4: {
            HidLaControllerSupportArg arg{};
            hidLaCreateControllerSupportArg(&arg);
            HidLaControllerSupportResultInfo result{};
            rc=hidLaShowControllerSupport(&result,&arg);
            break;
        }
        default:return;
    }
    if(R_FAILED(rc)) {
        char message[64]{};
        std::snprintf(message,sizeof(message),"CONSOLE SETTING FAILED: 0X%08X",
                      static_cast<unsigned int>(rc));
        setStatus(message,3500);
    } else {
        refreshConsoleSettings();
        setStatus("CONSOLE SETTINGS UPDATED");
    }
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
bool commitPersistentFile(void*,const char* phase) {
    return commitSdCard(phase);
}

bool validateSettingsFile(const std::string& contents,void*) {
    std::vector<desknx::settings_file::Entry> entries;
    return desknx::settings_file::parse(contents,entries);
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
    if(useSocomaTheme()) {
        BG={7,12,21,255};
        THEME_TOP={13,25,42,255};
        THEME_BOTTOM={5,9,18,255};
        MUTED={166,181,201,255};
        // BLUE/GREEN/RED and COLORS remain controlled by the familiar palette picker.
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
    std::string serialized;
    const auto source=desknx::persist::readValidatedWithBackup(
        SETTINGS_FILE,SETTINGS_BAK,desknx::settings_file::MaxBytes,
        validateSettingsFile,nullptr,serialized);
    if(source==desknx::persist::FileSource::None) {
        applyThemePalette();
        return;
    }
    if(source==desknx::persist::FileSource::Backup) {
        desknx::diag::Write("settings","backup_restored","path=%s",SETTINGS_BAK);
        setStatus("APPEARANCE SETTINGS RESTORED FROM BACKUP",4000);
    }
    std::vector<desknx::settings_file::Entry> entries;
    if(!desknx::settings_file::parse(serialized,entries)) {
        applyThemePalette();
        return;
    }
    for(const auto& entry:entries) {
        const std::string& key=entry.key;
        const std::string& value=entry.value;
        if(key=="theme") {
            char* end=nullptr;
            const long valueLong=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&valueLong>=0&&valueLong<=7)
                themeIndex=static_cast<int>(valueLong);
        } else if(key=="visual_theme") {
            if(value=="socoma")visualThemeIndex=1;
            else if(value=="v0.1"||value=="legacy")visualThemeIndex=0;
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
        } else if(key=="steamgriddb_key") {
            bool valid=value.size()<=160;
            for(unsigned char ch:value)
                if(ch<0x21||ch>0x7E)valid=false;
            if(valid)steamGridDbApiKey=value;
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
    std::ostringstream contents;
    contents<<"DESKNXSET1\n"
       <<"theme="<<themeIndex<<"\n"
       <<"visual_theme="<<(useSocomaTheme()?"socoma":"v0.1")<<"\n"
       <<"wallpaper="<<safeField(wallpaperPath)<<"\n"
       <<"font="<<safeField(fontPath)<<"\n"
       <<"iconpack="<<safeField(iconPackPath)<<"\n"
       <<"language="<<(languageRussian?"ru":"en")<<"\n"
       <<"iconstyle="<<iconStyle<<"\n"
       <<"steamgriddb_key="<<safeField(steamGridDbApiKey)<<"\n";
    const auto status=desknx::persist::writeAtomically(
        SETTINGS_TMP,SETTINGS_FILE,SETTINGS_BAK,contents.str(),
        commitPersistentFile,nullptr,"settings-temp","settings-publish",true);
    if(status!=desknx::persist::WriteStatus::Success) {
        if(status==desknx::persist::WriteStatus::TempCommitFailed)
            setStatus("SETTINGS SD COMMIT FAILED",3000);
        else if(status==desknx::persist::WriteStatus::PublishCommitFailed)
            setStatus("SETTINGS COMMIT FAILED; BACKUP KEPT",4000);
        else if(status==desknx::persist::WriteStatus::RollbackFailed)
            setStatus("SETTINGS RECOVERY NEEDED; KEEP settings.bak",4500);
        else if(status==desknx::persist::WriteStatus::OpenFailed)
            setStatus("SETTINGS SAVE FAILED",3000);
        else if(status==desknx::persist::WriteStatus::WriteFailed)
            setStatus("SETTINGS WRITE FAILED",3000);
        else
            setStatus("SETTINGS PUBLISH FAILED",3000);
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
    SDL_SetRenderDrawColor(renderer,3,7,14,useSocomaTheme()?58:92);
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
    SDL_SetRenderDrawColor(renderer,3,7,14,useSocomaTheme()?58:92);
    SDL_RenderFillRect(renderer,&viewport);
}

void drawSocomaAtmosphere() {
    // Lightweight atmospheric shading: no framebuffer blur or shader dependency.
    // The bands are a fixed, small number of draw calls and do not touch texture caches.
    for(int i=0;i<7;++i) {
        const Uint8 topAlpha=static_cast<Uint8>(42-i*5);
        const Uint8 bottomAlpha=static_cast<Uint8>(24+i*6);
        rect(0,i*12,SW,12,Color{2,7,16,topAlpha});
        rect(0,SH-(i+1)*12,SW,12,Color{2,6,14,bottomAlpha});
    }
    for(int i=0;i<4;++i) {
        const Uint8 alpha=static_cast<Uint8>(18-i*3);
        const int width=12+i*5;
        rect(0,88,width,SH-176,Color{1,5,12,alpha});
        rect(SW-width,88,width,SH-176,Color{1,5,12,alpha});
    }
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
    if(useSocomaTheme()) {
        roundRect(x,y+6,w,h,UiCornerRadius,Color{0,0,0,82});
        Color glass=fill;
        glass.r=static_cast<Uint8>((static_cast<unsigned>(fill.r)*55U+18U*45U)/100U);
        glass.g=static_cast<Uint8>((static_cast<unsigned>(fill.g)*55U+31U*45U)/100U);
        glass.b=static_cast<Uint8>((static_cast<unsigned>(fill.b)*55U+49U*45U)/100U);
        glass.a=std::min<Uint8>(fill.a,218);
        roundRect(x,y,w,h,UiCornerRadius,glass);
        Color stroke{std::max<Uint8>(edge.r,132),std::max<Uint8>(edge.g,151),
                     std::max<Uint8>(edge.b,174),std::max<Uint8>(edge.a,42)};
        roundRect(x+1,y+1,w-2,h-2,UiCornerRadius-1,stroke);
        if(w>36)rect(x+UiCornerRadius+4,y+1,w-2*(UiCornerRadius+4),1,
                     Color{255,255,255,22});
        return;
    }
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
const std::vector<HistoryEntry>& recentUniqueLaunches() {
    if(recentUniqueCacheRevision!=recentLaunchRevision) {
        recentUniqueCache=desknx::launch_history::uniqueRecent(recentLaunches);
        recentUniqueCacheRevision=recentLaunchRevision;
    }
    return recentUniqueCache;
}

std::string historyTimestampText(std::uint64_t timestamp,bool compact=false) {
    if(timestamp==0)return compact?"TIME UNKNOWN":"DATE UNKNOWN";
    const std::time_t stamp=static_cast<std::time_t>(timestamp);
    std::tm local{};
    if(!localtime_r(&stamp,&local))return compact?"TIME UNKNOWN":"DATE UNKNOWN";
    char value[40]{};
    const char* format=compact?"%d.%m %H:%M":"%d.%m.%Y %H:%M";
    return std::strftime(value,sizeof(value),format,&local)?std::string(value):std::string("DATE UNKNOWN");
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
    for(const auto& item:recentUniqueLaunches()) recent.push_back({item.kind,item.data});
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
void editSteamGridDbApiKey() {
    SwkbdConfig config{};
    const Result createRc=swkbdCreate(&config,0);
    if(R_FAILED(createRc)){setStatus("STEAMGRIDDB KEYBOARD UNAVAILABLE");return;}
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,"SteamGridDB API key (empty clears it)");
    swkbdConfigSetInitialText(&config,steamGridDbApiKey.c_str());
    char value[192]{};
    std::snprintf(value,sizeof(value),"%s",steamGridDbApiKey.c_str());
    const Result rc=showKeyboard(&config,value,sizeof(value));
    swkbdClose(&config);
    if(R_FAILED(rc))return;
    std::string next=value;
    bool valid=next.size()<=160;
    for(unsigned char ch:next)if(ch<0x21||ch>0x7E)valid=false;
    if(!valid){setStatus("INVALID API KEY FORMAT",3000);return;}
    steamGridDbApiKey=next;
    if(saveLauncherSettings())setStatus(steamGridDbApiKey.empty()
        ?"STEAMGRIDDB KEY CLEARED":"STEAMGRIDDB KEY SAVED");
}

int SDLCALL steamGridWorkerEntry(void* context) {
    auto* task=static_cast<SteamGridTask*>(context);
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    SteamGridWorkerResult result;
    if(task->job==SteamGridJob::SearchGames) {
        result.success=sgdb::searchGames(task->apiKey,task->query,result.games,result.error);
        if(result.success&&result.games.empty())result.success=false;
    } else if(task->job==SteamGridJob::LoadArtwork) {
        const char* types[]={"heroes","grids","icons"};
        for(const char* type:types) {
            std::vector<sgdb::IconAsset> found;
            std::string error;
            if(sgdb::listArtwork(task->apiKey,task->gameId,type,found,error)) {
                for(auto& item:found) {
                    result.artwork.push_back({std::move(item),type});
                    if(result.artwork.size()>=36)break;
                }
            } else if(result.error.empty())result.error=error;
            if(result.artwork.size()>=36)break;
        }
        result.success=!result.artwork.empty();
        if(!result.success&&result.error.empty())
            result.error="No SteamGridDB artwork found for this game.";
    } else if(task->job==SteamGridJob::PreviewArtwork) {
        result.previewAssetId=task->assetId;
        result.success=sgdb::downloadImage(task->assetUrl,result.previewBytes,result.error);
    } else if(task->job==SteamGridJob::ApplyArtwork) {
        std::vector<unsigned char> bytes;
        if(sgdb::downloadImage(task->assetUrl,bytes,result.error)) {
            desknx::steamgriddb::SteamGridDbCache cache;
            if(cache.ensureDirectories()) {
                char id[32]{};
                std::snprintf(id,sizeof(id),"%016llX",
                    static_cast<unsigned long long>(task->titleId));
                const std::string temporary=std::string(
                    desknx::steamgriddb::SteamGridDbCache::kCacheRoot)+
                    "/download-"+id+".tmp";
                FILE* output=std::fopen(temporary.c_str(),"wb");
                bool written=false;
                if(output) {
                    written=bytes.empty()||
                        std::fwrite(bytes.data(),1,bytes.size(),output)==bytes.size();
                    if(std::fclose(output)!=0)written=false;
                }
                if(written) {
                    result.applied=cache.saveArtworkFromFile(
                        task->titleId,
                        desknx::steamgriddb::SteamGridDbCache::ArtworkKind::Hero,
                        temporary);
                    if(!result.applied)result.error="Could not publish artwork to SD cache.";
                } else result.error="Could not write downloaded artwork to SD card.";
                std::remove(temporary.c_str());
                result.success=result.applied;
            } else {
                result.error="Cannot create SteamGridDB cache on SD card.";
            }
        }
    }
    if(steamGridMutex)SDL_LockMutex(steamGridMutex);
    steamGridWorkerResult=std::move(result);
    if(steamGridMutex)SDL_UnlockMutex(steamGridMutex);
    SDL_AtomicSet(&steamGridDone,1);
    delete task;
    return 0;
}

bool startSteamGridJob(SteamGridTask* task) {
    if(!task||steamGridThread||!steamGridMutex) {
        if(task)delete task;
        steamGridError="SteamGridDB worker is busy or unavailable.";
        if(steamGridPickerStage==SteamGridPickerStage::SearchingGames)
            steamGridPickerStage=SteamGridPickerStage::ChooseGame;
        else if(steamGridPickerStage==SteamGridPickerStage::LoadingArtwork||
                steamGridPickerStage==SteamGridPickerStage::Applying)
            steamGridPickerStage=SteamGridPickerStage::ChooseArtwork;
        setStatus("STEAMGRIDDB WORKER BUSY OR UNAVAILABLE",3000);
        return false;
    }
    steamGridActiveJob=task->job;
    steamGridWorkerResult=SteamGridWorkerResult{};
    SDL_AtomicSet(&steamGridDone,0);
    steamGridThread=SDL_CreateThread(steamGridWorkerEntry,"Transwitch SteamGridDB",task);
    if(!steamGridThread) {
        delete task;
        steamGridActiveJob=SteamGridJob::None;
        steamGridError="Could not start SteamGridDB worker thread.";
        if(steamGridPickerStage==SteamGridPickerStage::SearchingGames)
            steamGridPickerStage=SteamGridPickerStage::ChooseGame;
        else if(steamGridPickerStage==SteamGridPickerStage::LoadingArtwork||
                steamGridPickerStage==SteamGridPickerStage::Applying)
            steamGridPickerStage=SteamGridPickerStage::ChooseArtwork;
        setStatus("STEAMGRIDDB WORKER COULD NOT START",3000);
        return false;
    }
    return true;
}

void clearSteamGridPreview() {
    if(steamGridPreviewTexture) {
        SDL_DestroyTexture(steamGridPreviewTexture);
        steamGridPreviewTexture=nullptr;
    }
    steamGridPreviewWidth=steamGridPreviewHeight=0;
    steamGridPreviewAssetId=steamGridPreviewWantedAssetId=0;
    steamGridPreviewLoading=false;
    steamGridPreviewError.clear();
}

void requestSteamGridPreview() {
    if(!steamGridPickerOpen||steamGridPickerStage!=SteamGridPickerStage::ChooseArtwork||
       steamGridListIndex<0||steamGridListIndex>=static_cast<int>(steamGridArtwork.size())) {
        steamGridPreviewLoading=false;
        return;
    }
    const auto& choice=steamGridArtwork[static_cast<size_t>(steamGridListIndex)];
    const std::uint64_t wanted=choice.asset.id;
    steamGridPreviewWantedAssetId=wanted;
    if(wanted==0) {
        steamGridPreviewLoading=false;
        steamGridPreviewError="Preview is not available for this artwork.";
        return;
    }
    if(steamGridPreviewTexture&&steamGridPreviewAssetId==wanted) {
        steamGridPreviewLoading=false;
        steamGridPreviewError.clear();
        return;
    }
    if(steamGridThread) {
        // The single network worker is shared with search and download. Its
        // completion pump will request the latest selected preview.
        steamGridPreviewLoading=true;
        return;
    }
    auto* task=new(std::nothrow) SteamGridTask{};
    if(!task) {
        steamGridPreviewLoading=false;
        steamGridPreviewError="Not enough memory to load preview.";
        return;
    }
    task->job=SteamGridJob::PreviewArtwork;
    task->assetId=wanted;
    task->assetUrl=choice.asset.thumb.empty()?choice.asset.url:choice.asset.thumb;
    steamGridPreviewLoading=true;
    steamGridPreviewError.clear();
    if(!startSteamGridJob(task)) {
        steamGridPreviewLoading=false;
        steamGridPreviewError="Preview request could not be started.";
    }
}

void pumpSteamGridWorker() {
    if(!steamGridThread||!SDL_AtomicGet(&steamGridDone))return;
    SDL_WaitThread(steamGridThread,nullptr);
    steamGridThread=nullptr;
    SteamGridWorkerResult result;
    if(steamGridMutex)SDL_LockMutex(steamGridMutex);
    result=std::move(steamGridWorkerResult);
    steamGridWorkerResult=SteamGridWorkerResult{};
    if(steamGridMutex)SDL_UnlockMutex(steamGridMutex);
    const SteamGridJob completed=steamGridActiveJob;
    steamGridActiveJob=SteamGridJob::None;
    if(!steamGridPickerOpen)return;

    if(completed==SteamGridJob::PreviewArtwork) {
        steamGridPreviewLoading=false;
        if(result.previewAssetId==steamGridPreviewWantedAssetId &&
           steamGridPickerStage==SteamGridPickerStage::ChooseArtwork) {
            if(!result.success||result.previewBytes.empty()) {
                steamGridPreviewError=result.error.empty()?"Preview download failed.":result.error;
            } else if(result.previewBytes.size()>static_cast<size_t>(INT_MAX)) {
                steamGridPreviewError="Preview image is too large.";
            } else {
                SDL_RWops* rw=SDL_RWFromConstMem(result.previewBytes.data(),
                    static_cast<int>(result.previewBytes.size()));
                SDL_Surface* surface=rw?IMG_Load_RW(rw,1):nullptr;
                if(!surface) {
                    steamGridPreviewError="Preview image could not be decoded.";
                } else {
                    const std::uint64_t pixels=surface->w>0&&surface->h>0
                        ?static_cast<std::uint64_t>(surface->w)*static_cast<std::uint64_t>(surface->h):0;
                    if(surface->w<=0||surface->h<=0||surface->w>2048||surface->h>2048||
                       pixels>4000000u) {
                        steamGridPreviewError="Preview dimensions exceed the safe limit.";
                    } else {
                        SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,surface);
                        if(texture) {
                            if(steamGridPreviewTexture)SDL_DestroyTexture(steamGridPreviewTexture);
                            steamGridPreviewTexture=texture;
                            steamGridPreviewWidth=surface->w;
                            steamGridPreviewHeight=surface->h;
                            steamGridPreviewAssetId=result.previewAssetId;
                            steamGridPreviewError.clear();
                        } else {
                            steamGridPreviewError="Preview texture could not be created.";
                        }
                    }
                    SDL_FreeSurface(surface);
                }
            }
        }
        if(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork &&
           steamGridPreviewWantedAssetId!=result.previewAssetId)
            requestSteamGridPreview();
        return;
    }

    if(!result.success) {
        steamGridError=result.error.empty()?"SteamGridDB request failed.":result.error;
        if(completed==SteamGridJob::SearchGames)steamGridPickerStage=SteamGridPickerStage::ChooseGame;
        else if(completed==SteamGridJob::LoadArtwork)steamGridPickerStage=SteamGridPickerStage::ChooseArtwork;
        else steamGridPickerStage=SteamGridPickerStage::ChooseArtwork;
        setStatus("STEAMGRIDDB: "+steamGridError,4000);
        return;
    }
    steamGridError.clear();
    if(completed==SteamGridJob::SearchGames) {
        steamGridGames=std::move(result.games);
        steamGridListIndex=steamGridListOffset=0;
        steamGridPickerStage=SteamGridPickerStage::ChooseGame;
    } else if(completed==SteamGridJob::LoadArtwork) {
        steamGridArtwork=std::move(result.artwork);
        steamGridListIndex=steamGridListOffset=0;
        steamGridPickerStage=SteamGridPickerStage::ChooseArtwork;
        requestSteamGridPreview();
    } else if(completed==SteamGridJob::ApplyArtwork) {
        gIconCache.invalidateAll();
        const bool committed=!sdMounted||commitSdCard("steamgriddb-artwork");
        clearSteamGridPreview();
        steamGridPickerOpen=false;
        steamGridPickerStage=SteamGridPickerStage::Closed;
        setStatus(committed?"STEAMGRIDDB COVER SAVED - AVAILABLE OFFLINE":
            "COVER SAVED, BUT SD COMMIT FAILED",4500);
    }
}

void requestSteamGridSearch(bool editQuery=true) {
    if(steamGridThread) {
        setStatus("WAIT FOR THE PREVIOUS STEAMGRIDDB REQUEST",3000);
        return;
    }
    if(steamGridDbApiKey.empty()) {
        steamGridError="ADD A STEAMGRIDDB API KEY IN LAUNCHER SETTINGS";
        steamGridPickerOpen=true;
        steamGridPickerStage=SteamGridPickerStage::ChooseGame;
        setStatus(steamGridError,4000);
        return;
    }
    // Opening the picker with ZL must be immediate. The selected game's own
    // title is already a useful search query; only Y opens the keyboard.
    if(editQuery) {
        SwkbdConfig config{};
        const Result createRc=swkbdCreate(&config,0);
        if(R_FAILED(createRc)){setStatus("SEARCH KEYBOARD UNAVAILABLE");return;}
        swkbdConfigMakePresetDefault(&config);
        swkbdConfigSetHeaderText(&config,"Search SteamGridDB by game title");
        swkbdConfigSetInitialText(&config,steamGridSearchQuery.c_str());
        char query[192]{};
        std::snprintf(query,sizeof(query),"%s",steamGridSearchQuery.c_str());
        const Result rc=showKeyboard(&config,query,sizeof(query));
        swkbdClose(&config);
        if(R_FAILED(rc))return;
        steamGridSearchQuery=query;
        if(steamGridSearchQuery.empty()){setStatus("ENTER A GAME TITLE");return;}
    }
    clearSteamGridPreview();
    auto* task=new(std::nothrow) SteamGridTask{};
    if(!task){setStatus("STEAMGRIDDB OUT OF MEMORY");return;}
    task->job=SteamGridJob::SearchGames;
    task->apiKey=steamGridDbApiKey;
    task->query=steamGridSearchQuery;
    steamGridPickerOpen=true;
    steamGridPickerStage=SteamGridPickerStage::SearchingGames;
    steamGridError.clear();
    steamGridListIndex=steamGridListOffset=0;
    desknx::diag::Write("steamgriddb","search_begin","title_id=%016llX prompt=%d query=%s",
        static_cast<unsigned long long>(steamGridTargetTitleId),editQuery?1:0,
        steamGridSearchQuery.c_str());
    (void)startSteamGridJob(task);
}

void openSteamGridPicker(const Candidate& candidate) {
    if(steamGridThread) {
        setStatus("WAIT FOR THE PREVIOUS STEAMGRIDDB REQUEST",3000);
        return;
    }
    if(!sdMounted) {
        setStatus("SD CARD REQUIRED FOR OFFLINE ARTWORK CACHE",3500);
        return;
    }
    if(candidate.kind!=Kind::Game||!desknx::model::isTitleId(candidate.data)) {
        setStatus("STEAMGRIDDB ARTWORK IS AVAILABLE FOR INSTALLED GAMES",3000);
        return;
    }
    clearSteamGridPreview();
    steamGridTargetTitleId=std::strtoull(candidate.data.c_str(),nullptr,16);
    steamGridTargetTitle=candidate.title;
    steamGridSearchQuery=candidate.title;
    steamGridSelectedGameName.clear();
    steamGridGames.clear();steamGridArtwork.clear();steamGridError.clear();
    steamGridListIndex=steamGridListOffset=0;
    steamGridPickerOpen=true;
    steamGridPickerStage=SteamGridPickerStage::ChooseGame;
    desknx::diag::Write("steamgriddb","picker_open","title_id=%016llX title=%s",
        static_cast<unsigned long long>(steamGridTargetTitleId),steamGridTargetTitle.c_str());
    requestSteamGridSearch(false);
}

void chooseSteamGridGame() {
    if(steamGridThread) {
        setStatus("WAIT FOR THE PREVIOUS STEAMGRIDDB REQUEST",1800);
        return;
    }
    if(steamGridListIndex<0||steamGridListIndex>=static_cast<int>(steamGridGames.size()))return;
    const auto game=steamGridGames[static_cast<size_t>(steamGridListIndex)];
    auto* task=new(std::nothrow) SteamGridTask{};
    if(!task){setStatus("STEAMGRIDDB OUT OF MEMORY");return;}
    task->job=SteamGridJob::LoadArtwork;
    task->apiKey=steamGridDbApiKey;
    task->titleId=steamGridTargetTitleId;
    task->gameId=game.id;
    task->gameName=game.name;
    clearSteamGridPreview();
    steamGridSelectedGameName=game.name;
    steamGridPickerStage=SteamGridPickerStage::LoadingArtwork;
    steamGridError.clear();
    (void)startSteamGridJob(task);
}

void applySelectedSteamGridArtwork() {
    if(steamGridThread) {
        setStatus("WAIT FOR THE ARTWORK PREVIEW",1800);
        return;
    }
    if(steamGridListIndex<0||steamGridListIndex>=static_cast<int>(steamGridArtwork.size()))return;
    const auto choice=steamGridArtwork[static_cast<size_t>(steamGridListIndex)];
    auto* task=new(std::nothrow) SteamGridTask{};
    if(!task){setStatus("STEAMGRIDDB OUT OF MEMORY");return;}
    task->job=SteamGridJob::ApplyArtwork;
    task->apiKey=steamGridDbApiKey;
    task->titleId=steamGridTargetTitleId;
    task->assetUrl=choice.asset.url;
    steamGridPickerStage=SteamGridPickerStage::Applying;
    steamGridError.clear();
    (void)startSteamGridJob(task);
}

void cycleSteamGridSelection(int direction) {
    const size_t count=steamGridPickerStage==SteamGridPickerStage::ChooseGame
        ?steamGridGames.size():steamGridArtwork.size();
    if(count==0)return;
    steamGridListIndex=(steamGridListIndex+direction+static_cast<int>(count))%
        static_cast<int>(count);
    constexpr int visible=9;
    if(steamGridListIndex<steamGridListOffset)steamGridListOffset=steamGridListIndex;
    if(steamGridListIndex>=steamGridListOffset+visible)
        steamGridListOffset=steamGridListIndex-visible+1;
    if(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork)
        requestSteamGridPreview();
}

void handleSteamGridPickerInput(u64 keys) {
    if(keys&HidNpadButton_B) {
        if(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork) {
            clearSteamGridPreview();
            steamGridArtwork.clear();
            steamGridPickerStage=SteamGridPickerStage::ChooseGame;
            steamGridListIndex=steamGridListOffset=0;
        } else {
            clearSteamGridPreview();
            steamGridPickerOpen=false;
            steamGridPickerStage=SteamGridPickerStage::Closed;
        }
        return;
    }
    if(steamGridPickerStage==SteamGridPickerStage::SearchingGames||
       steamGridPickerStage==SteamGridPickerStage::LoadingArtwork||
       steamGridPickerStage==SteamGridPickerStage::Applying)return;
    if(keys&HidNpadButton_Y){requestSteamGridSearch();return;}
    if(keys&HidNpadButton_Up)cycleSteamGridSelection(-1);
    if(keys&HidNpadButton_Down)cycleSteamGridSelection(1);
    if(keys&HidNpadButton_A) {
        if(steamGridPickerStage==SteamGridPickerStage::ChooseGame)chooseSteamGridGame();
        else if(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork) {
            if(steamGridPreviewLoading) {
                setStatus("WAIT FOR THE ARTWORK PREVIEW",1800);
                return;
            }
            applySelectedSteamGridArtwork();
        }
    }
}

void cycleCatalogueFilter(int direction) {
    constexpr int count=5;
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
    unsigned consecutiveFailures = 0;
    std::uint64_t nextAttempt = SDL_GetTicks64() + 8000;
    while (!SDL_AtomicGet(&nextendoStop)) {
        const std::uint64_t now = SDL_GetTicks64();
        if (now >= nextAttempt) {
            if (!networkReady) {
                networkReady = desknx::nextendo::initializeNetwork();
                desknx::diag::Write("nextendo", networkReady ? "network_ready" : "network_init_failed",
                    "retry_ms=%u", networkReady ? 0u : 15000u);
                if (!networkReady) {
                    nextAttempt = now + 15000;
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
                consecutiveFailures = applied ? 0u : consecutiveFailures + 1u;
                desknx::diag::Write("nextendo", applied ? "snapshot_updated" : "snapshot_rejected",
                    "titles=%zu total=%u failures=%u", snapshot.perTitle.size(),
                    snapshot.total, consecutiveFailures);
                nextAttempt = SDL_GetTicks64() + desknx::nextendo::RefreshIntervalMs;
            } else {
                ++consecutiveFailures;
                desknx::diag::Write("nextendo", "snapshot_failed",
                    "network_ready=%d failures=%u retry_ms=%u",
                    networkReady ? 1 : 0, consecutiveFailures,
                    consecutiveFailures >= 3u ? 5000u : 15000u);
                // A transient outage should not leave the widget stuck on UNKNOWN
                // for an entire three-minute polling interval. Recreate the
                // socket/curl transport after repeated failures and retry quickly.
                if (consecutiveFailures >= 3u) {
                    desknx::nextendo::shutdownNetwork();
                    networkReady = false;
                    consecutiveFailures = 0;
                    nextAttempt = SDL_GetTicks64() + 5000;
                } else {
                    nextAttempt = SDL_GetTicks64() + 15000;
                }
            }
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
std::string formatBytes(uint64_t bytes) {
    char value[40]{};
    if(bytes>=1024ULL*1024ULL*1024ULL)
        std::snprintf(value,sizeof(value),"%.1f GB",
            static_cast<double>(bytes)/(1024.0*1024.0*1024.0));
    else if(bytes>=1024ULL*1024ULL)
        std::snprintf(value,sizeof(value),"%.0f MB",
            static_cast<double>(bytes)/(1024.0*1024.0));
    else if(bytes>=1024ULL)
        std::snprintf(value,sizeof(value),"%.0f KB",
            static_cast<double>(bytes)/1024.0);
    else
        std::snprintf(value,sizeof(value),"%llu B",
            static_cast<unsigned long long>(bytes));
    return value;
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
    while(SDL_AtomicGet(&heartbeatRunning)) {
        SDL_Delay(1000);
        if(!SDL_AtomicGet(&heartbeatRunning)) break;
        if(R_FAILED(sendIpc([]{return desknx::ipc::SendHeartbeat();})))
            SDL_AtomicSet(&heartbeatError,1);
    }
    return 0;
}
// IPC-06: read the initial state block the backend pushed at launch. A v1
// backend sends no block (or a legacy-sized one) - that is not an error; the
// menu simply keeps "backend decides" semantics for the user and title.
void consumeInitialState() {
    AppletStorage storage{};
    const Result rc=appletPopInData(&storage);
    if(R_FAILED(rc)) return; // nothing queued yet
    desknx::ipc::InitialState state{};
    s64 storage_size=0;
    const Result sizeRc=appletStorageGetSize(&storage,&storage_size);
    if(R_FAILED(sizeRc)||storage_size<static_cast<s64>(sizeof(state))) {
        appletStorageClose(&storage);
        return;
    }
    const Result readRc=appletStorageRead(&storage,0,&state,sizeof(state));
    appletStorageClose(&storage);
    if(R_FAILED(readRc)) return;
    if(state.magic!=desknx::ipc::Magic||state.header_size!=desknx::ipc::InitialStateSize)
        return; // not our block (e.g. v1 backend): ignore, stay compatible
    g_initialState=state;
    if(accountUidIsValid(&state.selected_user)) {
        g_backendUser=state.selected_user; g_backendHasUser=true;
        uiSelectedUser=state.selected_user;
        uiHasSelectedUser=true;
        for(const auto& user:consoleUsers) {
            if(sameAccountUid(user.uid,uiSelectedUser)) {
                selectedUserLabel=user.nickname;
                break;
            }
        }
    }
    if(state.app_state>=2&&state.app_title_id!=0) g_backendAppTitle=state.app_title_id;
    else g_backendAppTitle=0;
    if(state.degraded) setStatus("DEGRADED MODE: SD UNAVAILABLE",6000); // REC-04
    if(state.last_launch_rc!=0) {
        char detail[96]{};
        std::snprintf(detail,sizeof(detail),"LAST LAUNCH FAILED: 0X%08X TITLE 0X%016llX",
                      static_cast<unsigned int>(state.last_launch_rc),
                      static_cast<unsigned long long>(state.last_launch_title));
        setStatus(detail,7000);
    }
}
// §4.4 live events. Same header framing as commands but carrying Event ids;
// unknown events are ignored so future backends stay forward-compatible.
void pollBackendEvents() {
    AppletStorage storage{};
    const Result rc=appletPopInData(&storage);
    if(R_FAILED(rc)) return;
    desknx::ipc::Header hdr{};
    s64 storage_size=0;
    const Result sizeRc=appletStorageGetSize(&storage,&storage_size);
    if(R_FAILED(sizeRc)||storage_size<static_cast<s64>(sizeof(hdr))) {
        appletStorageClose(&storage); return;
    }
    const Result readRc=appletStorageRead(&storage,0,&hdr,sizeof(hdr));
    if(R_FAILED(readRc)||hdr.magic!=desknx::ipc::Magic||
       !desknx::ipc::rules::IsSupportedVersion(hdr.version)||
       hdr.header_size!=sizeof(hdr)||
       !desknx::ipc::rules::IsValidPayloadSize(hdr.payload_size)||
       static_cast<u64>(storage_size)<static_cast<u64>(sizeof(hdr))+hdr.payload_size) {
        appletStorageClose(&storage); return;
    }
    const u32 command=hdr.command; // actually an Event id in this direction
    if(command==static_cast<u32>(desknx::ipc::Event::AppExited)&&
       hdr.payload_size==sizeof(desknx::ipc::AppExitedEvent)) {
        desknx::ipc::AppExitedEvent payload{};
        if(R_SUCCEEDED(appletStorageRead(&storage,sizeof(hdr),&payload,sizeof(payload)))) {
            if(payload.title_id==g_backendAppTitle) g_backendAppTitle=0;
            setStatus(payload.reason?"GAME CLOSED BY SYSTEM":"GAME EXITED",4000);
        }
    } else if(command==static_cast<u32>(desknx::ipc::Event::AppLaunchFailed)&&
              hdr.payload_size==sizeof(desknx::ipc::AppLaunchFailedEvent)) {
        desknx::ipc::AppLaunchFailedEvent payload{};
        if(R_SUCCEEDED(appletStorageRead(&storage,sizeof(hdr),&payload,sizeof(payload)))) {
            char detail[96]{};
            std::snprintf(detail,sizeof(detail),"LAUNCH FAILED: 0X%08X",
                          static_cast<unsigned int>(payload.rc));
            setStatus(detail,6000); // LNC-07: code shown, human text by table later
        }
    } else if(command==static_cast<u32>(desknx::ipc::Event::SdRemoved)) {
        setStatus("SD CARD REMOVED - HOME BREW UNAVAILABLE",8000); // REC-05
    } else if(command==static_cast<u32>(desknx::ipc::Event::LowBattery)) {
        setStatus("LOW BATTERY - SYSTEM WILL SLEEP SOON",8000);
    }
    appletStorageClose(&storage);
}
void startHeartbeatThread() {
    SDL_AtomicSet(&heartbeatError,0); SDL_AtomicSet(&heartbeatRunning,1);
    heartbeatThread=SDL_CreateThread(heartbeatThreadMain,"DeskNX heartbeat",nullptr);
    if(!heartbeatThread){SDL_AtomicSet(&heartbeatRunning,0);setStatus("HEARTBEAT THREAD FAILED",4000);}
}
void stopHeartbeatThread() {
    SDL_AtomicSet(&heartbeatRunning,0);
    if(heartbeatThread){SDL_WaitThread(heartbeatThread,nullptr);heartbeatThread=nullptr;}
}
#else
Result showKeyboard(SwkbdConfig* config, char* output, size_t outputSize) {
    const Uint64 started=SDL_GetTicks64();
    const Result rc=swkbdShow(config,output,outputSize);
    desknx::diag::Write("keyboard","dialog_return","rc=0x%08X elapsed_ms=%llu",
        static_cast<unsigned int>(rc),static_cast<unsigned long long>(SDL_GetTicks64()-started));
    return rc;
}
#endif
bool isSafeNroPath(const std::string& path) {
    constexpr char prefix[] = "sdmc:/switch/";
    if (path.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
        path.find("..") != std::string::npos || path.find('\0') != std::string::npos ||
        path.size() < (sizeof(prefix) - 1) + 5)
        return false;
    std::string extension = path.substr(path.size() - 4);
    for (char& c : extension)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (extension != ".nro") return false;
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool validateHistoryNroPath(const std::string& path,void*) {
    return isSafeNroPath(path);
}

bool validateHistoryContents(const std::string& contents,void* opaque) {
    auto* parsed=static_cast<std::vector<HistoryEntry>*>(opaque);
    return parsed&&desknx::launch_history::parse(
        contents,*parsed,validateHistoryNroPath,nullptr);
}

void loadHistory() {
    if(!sdMounted)return;
    std::vector<HistoryEntry> loaded;
    std::string serialized;
    const auto source=desknx::persist::readValidatedWithBackup(
        HISTORY_FILE,HISTORY_BAK_FILE,desknx::launch_history::MaxFileBytes,
        validateHistoryContents,&loaded,serialized);
    if(source==desknx::persist::FileSource::None) {
        desknx::diag::Write("history","load_empty","primary=%s backup=%s",
            HISTORY_FILE,HISTORY_BAK_FILE);
        return;
    }
    recentLaunches=std::move(loaded);
    if(source==desknx::persist::FileSource::Backup) {
        desknx::diag::Write("history","backup_restored","path=%s entries=%zu",
            HISTORY_BAK_FILE,recentLaunches.size());
        setStatus("RECENT HISTORY RESTORED FROM BACKUP",4000);
    } else {
        desknx::diag::Write("history","loaded","path=%s entries=%zu",
            HISTORY_FILE,recentLaunches.size());
    }
    ++recentLaunchRevision;
    recentUniqueCacheRevision=UINT64_MAX;
    if(!recentLaunches.empty())lastLaunched=recentLaunches.front().title;
}
void saveHistory() {
    if(!sdMounted)return;
    const Uint64 started=SDL_GetTicks64();
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    const std::string serialized=desknx::launch_history::serialize(recentLaunches);
    if(serialized.empty()) {
        setStatus("RECENT HISTORY INVALID; SAVE REFUSED",3000);
        return;
    }
    const auto status=desknx::persist::writeAtomically(
        HISTORY_TMP,HISTORY_FILE,HISTORY_BAK_FILE,serialized,
        commitPersistentFile,nullptr,"history-temp","history-publish",true);
    if(status!=desknx::persist::WriteStatus::Success) {
        if(status==desknx::persist::WriteStatus::TempCommitFailed)
            setStatus("RECENT HISTORY SD COMMIT FAILED",3000);
        else if(status==desknx::persist::WriteStatus::PublishCommitFailed) {
            desknx::diag::Write("history","save_failed","phase=publish_commit entries=%zu",recentLaunches.size());
            setStatus("RECENT HISTORY COMMIT FAILED; BACKUP KEPT",4000);
        } else if(status==desknx::persist::WriteStatus::RollbackFailed)
            setStatus("RECENT HISTORY RECOVERY NEEDED: KEEP recent.bak",5000);
        else if(status==desknx::persist::WriteStatus::OpenFailed)
            setStatus("RECENT HISTORY SAVE FAILED",3000);
        else if(status==desknx::persist::WriteStatus::WriteFailed)
            setStatus("RECENT HISTORY WRITE FAILED",3000);
        else
            setStatus("RECENT HISTORY RENAME FAILED",3000);
        return;
    }
    desknx::diag::Write("history","saved","entries=%zu elapsed_ms=%llu",
        recentLaunches.size(),static_cast<unsigned long long>(SDL_GetTicks64()-started));
}
void recordRecentLaunch(const Tile& tile) {
    if(tile.kind!=Kind::Game&&tile.kind!=Kind::Nro)return;
    const std::time_t now=std::time(nullptr);
    const std::uint64_t timestamp=now>0?static_cast<std::uint64_t>(now):0;
    recentLaunches.insert(recentLaunches.begin(),{tile.kind,tile.title,tile.data,timestamp});
    ++recentLaunchRevision;
    recentUniqueCacheRevision=UINT64_MAX;
    if(recentLaunches.size()>desknx::launch_history::MaxEntries)
        recentLaunches.resize(desknx::launch_history::MaxEntries);
    lastLaunched=tile.title;saveHistory();
}
bool backupBrokenLayout() {
    struct stat st{};
    if (stat(CFG_FILE, &st) != 0) return errno == ENOENT;
    if (!S_ISREG(st.st_mode)) {
        setStatus("LAYOUT PATH IS NOT A REGULAR FILE", 4500);
        return false;
    }
    std::time_t now = std::time(nullptr);
    std::tm* tmv = std::localtime(&now);
    char stamp[32]{};
    if (tmv) std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%S", tmv);
    else std::snprintf(stamp, sizeof(stamp), "%u", static_cast<unsigned int>(SDL_GetTicks()));

    char dest[192]{};
    for (int i = 0; i < 100; ++i) {
        if (i == 0)
            std::snprintf(dest, sizeof(dest), "%s-%s.tsv", CFG_BACKUP_BASE, stamp);
        else
            std::snprintf(dest, sizeof(dest), "%s-%s-%d.tsv", CFG_BACKUP_BASE, stamp, i);
        struct stat existing{};
        if (stat(dest, &existing) == 0) continue;
        if (std::rename(CFG_FILE, dest) == 0) {
            if(!commitSdCard("corrupt-layout-preserve")) {
                setStatus("LAYOUT BACKUP CREATED BUT COMMIT FAILED",5000);
                return false;
            }
            setStatus("INVALID LAYOUT PRESERVED", 4500);
            return true;
        }
        break;
    }
    setStatus("LAYOUT INVALID; BACKUP FAILED", 4500);
    return false;
}

bool saveActiveWorkspaceState() {
    if(!sdMounted || desks.empty() || deskIndex<0 ||
       deskIndex>=static_cast<int>(desks.size())) return false;
    const std::string state=
        desknx::layout::serializeActiveWorkspaceState(desks,deskIndex);
    if(state.empty()) return false;
    if(activeWorkspaceMarkerKnown&&persistedActiveWorkspaceMarker==state) {
        struct stat markerStat{};
        if(stat(ACTIVE_WORKSPACE_FILE,&markerStat)==0&&
           S_ISREG(markerStat.st_mode)&&
           markerStat.st_size==static_cast<off_t>(state.size())) {
            activeWorkspaceMarkerPending=false;
            activeWorkspaceMarkerDue=0;
            return true;
        }
    }
    // The marker is a tiny navigation hint. Commit once after its atomic
    // publication; the retained backup allows recovery if the commit fails.
    const auto status=desknx::persist::writeAtomically(
        ACTIVE_WORKSPACE_TMP,ACTIVE_WORKSPACE_FILE,ACTIVE_WORKSPACE_BAK,state,
        commitPersistentFile,nullptr,"active-workspace-temp","active-workspace-publish",false);
    if(status!=desknx::persist::WriteStatus::Success) return false;
    persistedActiveWorkspaceMarker=state;
    activeWorkspaceMarkerKnown=true;
    activeWorkspaceMarkerPending=false;
    activeWorkspaceMarkerDue=0;
    return true;
}

void scheduleActiveWorkspaceSave() {
    if(!sdMounted || desks.empty() || deskIndex<0 ||
       deskIndex>=static_cast<int>(desks.size())) return;
    activeWorkspaceMarkerPending=true;
    activeWorkspaceMarkerDue=SDL_GetTicks()+550;
}

bool flushActiveWorkspaceSave() {
    if(!activeWorkspaceMarkerPending) return true;
    if(enqueueLayoutSave(true)) {
        activeWorkspaceMarkerPending=false;
        activeWorkspaceMarkerDue=0;
        return true;
    }
    // Queue creation failure must not block navigation. Retry after a quiet
    // interval; launch and shutdown still force a synchronous, checked save.
    activeWorkspaceMarkerDue=SDL_GetTicks()+2500;
    desknx::diag::Write("layout","active_workspace_debounce_retry",
                        "index=%d",deskIndex);
    return false;
}

struct ActiveMarkerValidation {
    const std::vector<Workspace>* workspaces{nullptr};
    int restoredIndex{0};
};
bool validateActiveMarkerFile(const std::string& contents,void* opaque) {
    auto* context=static_cast<ActiveMarkerValidation*>(opaque);
    if(!context||!context->workspaces)return false;
    int candidate=context->restoredIndex;
    if(!desknx::layout::parseActiveWorkspaceState(
           contents,*context->workspaces,candidate))return false;
    context->restoredIndex=candidate;
    return true;
}

bool loadActiveWorkspaceState() {
    if(!sdMounted || desks.empty()) return false;
    ActiveMarkerValidation validation{&desks,deskIndex};
    std::string contents;
    const auto source=desknx::persist::readValidatedWithBackup(
        ACTIVE_WORKSPACE_FILE,ACTIVE_WORKSPACE_BAK,512,
        validateActiveMarkerFile,&validation,contents);
    if(source==desknx::persist::FileSource::None)return false;

    deskIndex=validation.restoredIndex;
    if(source==desknx::persist::FileSource::Primary) {
        persistedActiveWorkspaceMarker=contents;
        activeWorkspaceMarkerKnown=true;
    } else {
        persistedActiveWorkspaceMarker.clear();
        activeWorkspaceMarkerKnown=false;
    }
    activeWorkspaceMarkerPending=false;
    activeWorkspaceMarkerDue=0;
    selectedX=selectedY=0;
    if(source==desknx::persist::FileSource::Backup) {
        desknx::diag::Write("layout","active_workspace_backup_restored",
            "index=%d name=%s",deskIndex,desks[static_cast<size_t>(deskIndex)].name.c_str());
        setStatus("ACTIVE DESKTOP RESTORED FROM BACKUP",4000);
    } else {
        desknx::diag::Write("layout","active_workspace_restored",
            "index=%d name=%s",deskIndex,desks[static_cast<size_t>(deskIndex)].name.c_str());
    }
    return true;
}

bool saveLayout() {
    const Uint64 saveStarted=SDL_GetTicks64();
    drainAsyncLayoutSaver();
    if(!sdMounted) {
        setStatus("SAVE FAILED: SD CARD NOT MOUNTED",3500);
        return false;
    }
    if (layoutSaveBlocked) {
        setStatus("SAVE BLOCKED: PRESERVE LAYOUT BACKUP", 4500);
        return false;
    }
    const std::string serialized=desknx::layout::serialize(desks,deskIndex);
    const std::string shape=desknx::layout::serialize(desks,0);
    if(serialized.empty()||shape.empty()) {
        setStatus("LAYOUT INVALID; SAVE REFUSED",3500);
        return false;
    }
    const uint64_t shapeHash=desknx::model::stableHash(shape);
    struct stat primaryStat{};
    const bool primaryExists=stat(CFG_FILE,&primaryStat)==0&&S_ISREG(primaryStat.st_mode);
    const bool mustRewritePrimary=layoutPrimaryNeedsRewrite||!primaryExists||
        !persistedLayoutShapeKnown||persistedLayoutShapeHash!=shapeHash||
        layoutNeedsMigration;

    // The active desk has its own validated, atomic marker. When only deskIndex
    // changed, do not rewrite every tile or rotate the full-layout backup.
    if(!mustRewritePrimary && saveActiveWorkspaceState()) {
        layoutLoaded=true;
        size_t tileCount=0;for(const auto& workspace:desks)tileCount+=workspace.tiles.size();
        desknx::diag::Write("layout","saved",
            "mode=active_marker workspaces=%zu tiles=%zu active=%d elapsed_ms=%llu",
            desks.size(),tileCount,deskIndex,
            static_cast<unsigned long long>(SDL_GetTicks64()-saveStarted));
        return true;
    }

    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    const auto writeStatus=desknx::persist::writeAtomically(
        CFG_TMP,CFG_FILE,CFG_BAK_FILE,serialized,
        commitPersistentFile,nullptr,"layout-temp","layout-publish",true);
    if(writeStatus!=desknx::persist::WriteStatus::Success) {
        if(writeStatus==desknx::persist::WriteStatus::TempCommitFailed)
            setStatus("LAYOUT SD COMMIT FAILED; OLD FILE KEPT",4000);
        else if(writeStatus==desknx::persist::WriteStatus::PublishCommitFailed) {
            layoutSaveBlocked=true;
            setStatus("LAYOUT COMMIT FAILED; KEEP layout.bak",5000);
        } else if(writeStatus==desknx::persist::WriteStatus::RollbackFailed) {
            layoutSaveBlocked=true;
            setStatus("LAYOUT RECOVERY NEEDED: KEEP layout.bak",5000);
        } else if(writeStatus==desknx::persist::WriteStatus::OpenFailed)
            setStatus("SAVE FAILED: SD CARD NOT WRITABLE",3500);
        else if(writeStatus==desknx::persist::WriteStatus::WriteFailed)
            setStatus("SAVE FAILED: WRITE ERROR",3500);
        else
            setStatus("SAVE FAILED: RENAME ERROR",3500);
        return false;
    }
    persistedLayoutShapeHash=shapeHash;
    persistedLayoutShapeKnown=true;
    layoutPrimaryNeedsRewrite=false;
    layoutNeedsMigration=false;
    // If the marker cannot be published, the main layout already carries the
    // current index. Remove the stale marker rather than restoring an old desk.
    if(!saveActiveWorkspaceState()) {
        desknx::diag::Write("layout","active_workspace_marker_write_failed",
                            "index=%d",deskIndex);
        (void)std::remove(ACTIVE_WORKSPACE_FILE);
    }
    layoutLoaded=true;
    size_t tileCount=0;for(const auto& workspace:desks)tileCount+=workspace.tiles.size();
    desknx::diag::Write("layout","saved",
        "mode=full workspaces=%zu tiles=%zu active=%d bytes=%zu elapsed_ms=%llu",
        desks.size(),tileCount,deskIndex,serialized.size(),
        static_cast<unsigned long long>(SDL_GetTicks64()-saveStarted));
    setStatus("LAYOUT SAVED",1600);
    return true;
}

namespace {
bool writeActiveWorkspaceSnapshot(const LayoutSaveJob& job) {
    if(job.activeWorkspaceState.empty()) return false;
    if(job.previousMarkerKnown &&
       job.previousActiveMarker == job.activeWorkspaceState) {
        struct stat markerStat{};
        if(stat(ACTIVE_WORKSPACE_FILE,&markerStat)==0 &&
           S_ISREG(markerStat.st_mode) &&
           markerStat.st_size==static_cast<off_t>(job.activeWorkspaceState.size()))
            return true;
    }
    mkdir("sdmc:/switch",0777);
    mkdir(CFG_DIR,0777);
    return desknx::persist::writeAtomically(
        ACTIVE_WORKSPACE_TMP,ACTIVE_WORKSPACE_FILE,ACTIVE_WORKSPACE_BAK,
        job.activeWorkspaceState,commitPersistentFile,nullptr,
        "active-workspace-async-temp","active-workspace-async-publish",false)==
        desknx::persist::WriteStatus::Success;
}

int layoutSaveThreadMain(void* context) {
    const auto* job=static_cast<const LayoutSaveJob*>(context);
    LayoutSaveOutcome outcome{};
    outcome.markerOnly=job->markerOnly;
    outcome.shapeHash=job->shapeHash;

    bool primaryOk=true;
    if(job->rewritePrimary) {
        mkdir("sdmc:/switch",0777);
        mkdir(CFG_DIR,0777);
        const auto writeStatus=desknx::persist::writeAtomically(
            CFG_TMP,CFG_FILE,CFG_BAK_FILE,job->serializedLayout,
            commitPersistentFile,nullptr,"layout-async-temp","layout-async-publish",true);
        if(writeStatus==desknx::persist::WriteStatus::Success) {
            outcome.primaryWritten=true;
        } else {
            primaryOk=false;
            outcome.blocked=writeStatus==desknx::persist::WriteStatus::RollbackFailed||
                writeStatus==desknx::persist::WriteStatus::PublishCommitFailed;
            if(writeStatus==desknx::persist::WriteStatus::TempCommitFailed)
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "LAYOUT SD COMMIT FAILED; OLD FILE KEPT");
            else if(writeStatus==desknx::persist::WriteStatus::PublishCommitFailed)
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "LAYOUT COMMIT FAILED; KEEP layout.bak");
            else if(writeStatus==desknx::persist::WriteStatus::RollbackFailed)
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "LAYOUT RECOVERY NEEDED: KEEP layout.bak");
            else if(writeStatus==desknx::persist::WriteStatus::OpenFailed)
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "SAVE FAILED: SD CARD NOT WRITABLE");
            else if(writeStatus==desknx::persist::WriteStatus::WriteFailed)
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "SAVE FAILED: WRITE ERROR");
            else
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "SAVE FAILED: RENAME ERROR");
        }
    }

    if(primaryOk) {
        outcome.markerWritten=writeActiveWorkspaceSnapshot(*job);
        // A full layout snapshot already contains the active index. If its
        // separate marker cannot be published, remove the stale marker so it
        // cannot override the freshly written layout on the next boot.
        if(job->rewritePrimary && !outcome.markerWritten)
            (void)std::remove(ACTIVE_WORKSPACE_FILE);
        outcome.success=outcome.primaryWritten||outcome.markerWritten;
        if(outcome.success) {
            std::snprintf(outcome.message,sizeof(outcome.message),
                outcome.markerWritten ? "LAYOUT SAVED" :
                (outcome.primaryWritten ? "LAYOUT SAVED; WORKSPACE MARKER RETRY" :
                                          "WORKSPACE SAVE COMPLETE"));
        } else if(outcome.message[0]=='\0') {
            std::snprintf(outcome.message,sizeof(outcome.message),
                          "WORKSPACE SAVE FAILED");
        }
    }

    layoutSaveOutcome=outcome;
    SDL_AtomicSet(&layoutSaveDone,1);
    return 0;
}
} // namespace

bool enqueueLayoutSave(bool markerOnly) {
    if(!sdMounted) {
        setStatus("SAVE FAILED: SD CARD NOT MOUNTED",3000);
        return false;
    }
    if(layoutSaveBlocked) {
        setStatus("SAVE BLOCKED: PRESERVE LAYOUT BACKUP",3500);
        return false;
    }
    if(desks.empty() || deskIndex<0 ||
       deskIndex>=static_cast<int>(desks.size())) return false;

    // First retire a finished request. A worker only receives immutable
    // serialized strings; it never touches the live workspace vector.
    pumpAsyncLayoutSaver(false);

    const std::string serialized=desknx::layout::serialize(desks,deskIndex);
    const std::string shape=desknx::layout::serialize(desks,0);
    const std::string activeState=
        desknx::layout::serializeActiveWorkspaceState(desks,deskIndex);
    if(serialized.empty()||shape.empty()||activeState.empty()) {
        setStatus("LAYOUT INVALID; SAVE REFUSED",3000);
        return false;
    }

    const uint64_t shapeHash=desknx::model::stableHash(shape);
    struct stat primaryStat{};
    const bool primaryExists=
        stat(CFG_FILE,&primaryStat)==0&&S_ISREG(primaryStat.st_mode);
    const bool rewritePrimary=!markerOnly &&
        (layoutPrimaryNeedsRewrite||!primaryExists||
         !persistedLayoutShapeKnown||persistedLayoutShapeHash!=shapeHash||
         layoutNeedsMigration);

    auto job=std::make_unique<LayoutSaveJob>();
    job->serializedLayout=serialized;
    job->activeWorkspaceState=activeState;
    job->previousActiveMarker=persistedActiveWorkspaceMarker;
    job->previousMarkerKnown=activeWorkspaceMarkerKnown;
    job->shapeHash=shapeHash;
    job->rewritePrimary=rewritePrimary;
    job->markerOnly=markerOnly;

    if(layoutSaveThread || activeLayoutSaveJob) {
        // Coalesce bursts of tile edits to the latest complete layout. The
        // active snapshot remains immutable until its worker exits.
        const bool replaced=static_cast<bool>(pendingLayoutSaveJob);
        pendingLayoutSaveJob=std::move(job);
        desknx::diag::Write("layout","async_save_queued",
            "marker_only=%d pending_replaced=%d",
            markerOnly?1:0,replaced?1:0);
        return true;
    }

    activeLayoutSaveJob=std::move(job);
    layoutSaveOutcome=LayoutSaveOutcome{};
    SDL_AtomicSet(&layoutSaveDone,0);
    layoutSaveThread=SDL_CreateThread(layoutSaveThreadMain,
        "DeskNX layout persistence",activeLayoutSaveJob.get());
    if(!layoutSaveThread) {
        activeLayoutSaveJob.reset();
        setStatus("BACKGROUND SAVE THREAD UNAVAILABLE",3500);
        desknx::diag::Write("layout","async_save_thread_failed",
                            "fallback=not_started");
        return false;
    }
    return true;
}

void pumpAsyncLayoutSaver(bool waitForCompletion) {
    if(layoutSaveThread &&
       (waitForCompletion || SDL_AtomicGet(&layoutSaveDone))) {
        if(waitForCompletion && !SDL_AtomicGet(&layoutSaveDone))
            SDL_WaitThread(layoutSaveThread,nullptr);
        else
            SDL_WaitThread(layoutSaveThread,nullptr);
        layoutSaveThread=nullptr;

        const LayoutSaveOutcome outcome=layoutSaveOutcome;
        const bool hadJob=static_cast<bool>(activeLayoutSaveJob);
        const bool markerOnly=hadJob&&activeLayoutSaveJob->markerOnly;
        const std::string completedMarker=
            hadJob?activeLayoutSaveJob->activeWorkspaceState:std::string{};
        if(outcome.success) {
            if(outcome.primaryWritten) {
                persistedLayoutShapeHash=outcome.shapeHash;
                persistedLayoutShapeKnown=true;
                layoutPrimaryNeedsRewrite=false;
                layoutNeedsMigration=false;
                layoutLoaded=true;
            }
            if(outcome.markerWritten) {
                persistedActiveWorkspaceMarker=completedMarker;
                activeWorkspaceMarkerKnown=true;
            } else {
                activeWorkspaceMarkerPending=true;
                activeWorkspaceMarkerDue=SDL_GetTicks()+2500;
            }
            if(outcome.markerWritten && activeWorkspaceMarkerPending &&
               desknx::layout::serializeActiveWorkspaceState(desks,deskIndex)==
                    completedMarker) {
                activeWorkspaceMarkerPending=false;
                activeWorkspaceMarkerDue=0;
            }
            if(outcome.message[0])setStatus(outcome.message,1800);
        } else {
            if(outcome.blocked)layoutSaveBlocked=true;
            if(markerOnly || (outcome.primaryWritten && !outcome.markerWritten)) {
                activeWorkspaceMarkerPending=true;
                activeWorkspaceMarkerDue=SDL_GetTicks()+2500;
            }
            setStatus(outcome.message[0]?outcome.message:"BACKGROUND SAVE FAILED",4000);
        }
        desknx::diag::Write("layout","async_save_finished",
            "success=%d primary=%d marker=%d blocked=%d marker_only=%d",
            outcome.success?1:0,outcome.primaryWritten?1:0,
            outcome.markerWritten?1:0,outcome.blocked?1:0,
            markerOnly?1:0);
        activeLayoutSaveJob.reset();
    }

    if(!layoutSaveThread && !activeLayoutSaveJob && pendingLayoutSaveJob) {
        activeLayoutSaveJob=std::move(pendingLayoutSaveJob);
        pendingLayoutSaveJob.reset();
        layoutSaveOutcome=LayoutSaveOutcome{};
        SDL_AtomicSet(&layoutSaveDone,0);
        layoutSaveThread=SDL_CreateThread(layoutSaveThreadMain,
            "DeskNX layout persistence",activeLayoutSaveJob.get());
        if(!layoutSaveThread) {
            activeLayoutSaveJob.reset();
            pendingLayoutSaveJob.reset();
            setStatus("BACKGROUND SAVE THREAD UNAVAILABLE",3500);
            desknx::diag::Write("layout","async_save_thread_failed",
                                "fallback=queued_request_dropped");
        }
    }
}

void drainAsyncLayoutSaver() {
    while(layoutSaveThread || activeLayoutSaveJob || pendingLayoutSaveJob)
        pumpAsyncLayoutSaver(true);
}

bool loadLayoutFrom(const char* path) {
    const Uint64 started=SDL_GetTicks64();
    std::string serialized;
    if(!desknx::persist::readBoundedFile(path,1024*1024,serialized)) {
        desknx::diag::Write("layout","load_failed","path=%s reason=stat_or_read",
            path?path:"null");
        return false;
    }
    desknx::layout::LoadResult loaded{};
    if (!desknx::layout::parse(serialized, loaded)) {
        desknx::diag::Write("layout","load_failed","path=%s reason=parse bytes=%zu",
            path,serialized.size());
        return false;
    }
    desks = std::move(loaded.workspaces);
    deskIndex = loaded.activeWorkspace;
    layoutNeedsMigration = loaded.needsMigration;
    layoutRecoveredTiles = loaded.recoveredTiles;
    layoutDroppedTiles = loaded.droppedTiles;
    const std::string shape=desknx::layout::serialize(desks,0);
    persistedLayoutShapeKnown=!shape.empty();
    persistedLayoutShapeHash=persistedLayoutShapeKnown?
        desknx::model::stableHash(shape):0;
    // A backup restore or legacy format must be published back to layout.tsv,
    // even when the restored workspace contents themselves have not changed.
    layoutPrimaryNeedsRewrite=(std::string(path)!=CFG_FILE)||layoutNeedsMigration;
    selectedX = selectedY = 0;
    size_t tileCount=0;
    for(const auto& workspace:desks)tileCount+=workspace.tiles.size();
    desknx::diag::Write("layout","loaded",
        "path=%s bytes=%zu workspaces=%zu tiles=%zu active=%d migration=%d recovered=%zu dropped=%zu elapsed_ms=%llu",
        path,serialized.size(),desks.size(),tileCount,deskIndex,
        layoutNeedsMigration?1:0,layoutRecoveredTiles,layoutDroppedTiles,
        static_cast<unsigned long long>(SDL_GetTicks64()-started));
    return true;
}
bool loadLayout() { return loadLayoutFrom(CFG_FILE); }
void defaultLayout() {
    const auto available=catalogueSnapshot();
    desks={{"Games",{}},{"Homebrew",{}},{"Widgets",{}}};int gi=0,hi=0;
    for(const auto& c:available) {
        if(c.kind==Kind::Game&&gi<12) {desks[0].tiles.push_back(Tile{c.kind,c.title,c.data,gi%COLS,gi/COLS,1,1,c.color});++gi;}
        if(c.kind==Kind::Nro&&hi<12) {desks[1].tiles.push_back(Tile{c.kind,c.title,c.data,hi%COLS,hi/COLS,1,1,c.color});++hi;}
    }
    desks[2].tiles.push_back(Tile{Kind::Clock,"Clock","",0,0,2,1,1});
    desks[2].tiles.push_back(Tile{Kind::Battery,"Battery","",2,0,2,1,2});
    desks[2].tiles.push_back(Tile{Kind::System,"System info","",4,0,2,1,5});
    desks[2].tiles.push_back(Tile{Kind::Recent,"Recent launches","",0,1,3,1,6});
    desks[2].tiles.push_back(Tile{Kind::Storage,"SD storage","",0,2,3,1,3});
    desks[2].tiles.push_back(Tile{Kind::Calendar,"Calendar","",3,2,3,1,4});
}
void refreshBattery() {
    Uint32 now=SDL_GetTicks();if(now-lastBatteryTick<10000)return;lastBatteryTick=now;
    if(!psmReady&&R_SUCCEEDED(psmInitialize()))psmReady=true;
    if(psmReady){u32 pct=0;if(R_SUCCEEDED(psmGetBatteryChargePercentage(&pct)))battery=static_cast<int>(pct);}
}
void refreshStorageStats() {
    const Uint32 now=SDL_GetTicks();
    if(lastStorageUpdate!=0&&now-lastStorageUpdate<10000)return;
    lastStorageUpdate=now;
    struct statvfs fs{};
    if(statvfs("sdmc:/",&fs)==0&&fs.f_frsize>0) {
        const uint64_t blockSize=static_cast<uint64_t>(fs.f_frsize);
        sdTotalBytes=static_cast<uint64_t>(fs.f_blocks)*blockSize;
        sdFreeBytes=static_cast<uint64_t>(fs.f_bavail)*blockSize;
        storageStatsAvailable=sdTotalBytes>0;
    } else {
        sdTotalBytes=0;
        sdFreeBytes=0;
        storageStatsAvailable=false;
    }
}
std::string initials(const std::string& name) {
    // UI-20: take initials from the raw UTF-8 string; ASCII letters pass
    // through, multibyte sequences contribute their lead byte which yields
    // '?' via fallbackAscii and is skipped as a word separator below.
    std::string s=fallbackAscii(name),out;bool begin=true;
    for(char c:s){if(c==' '||c=='-'||c=='_'||c=='.'){begin=true;continue;}if(begin){out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));begin=false;if(out.size()==2)break;}}
    return out.empty()?"APP":out;
}
SDL_Rect tileRect(const Tile&t) {
    return SDL_Rect{LEFT+t.x*(CW+GX),TOP+t.y*(CH+GY),t.w*CW+(t.w-1)*GX,t.h*CH+(t.h-1)*GY};
}

void drawSystemIcon(int kind,int x,int y,int size,Color c) {
    if(iconStyle==2) {
        roundRect(x-3,y-3,size+6,size+6,std::max(7,size/4),
                  Color{static_cast<Uint8>(c.r/5),static_cast<Uint8>(c.g/5),
                        static_cast<Uint8>(c.b/5),220});
        c=WHITE; // material-like filled treatment with a high-contrast glyph
    }
    color(c);
    const int cx=x+size/2,cy=y+size/2;
    auto line=[&](int x1,int y1,int x2,int y2) {
        SDL_RenderDrawLine(renderer,x1,y1,x2,y2);
        if(iconStyle==1) SDL_RenderDrawLine(renderer,x1,y1+1,x2,y2+1);
    };
    auto circle=[&](int radius,bool filled) {
        for(int dy=-radius;dy<=radius;++dy) {
            const int dx=static_cast<int>(std::sqrt(std::max(0,radius*radius-dy*dy)));
            if(filled)line(cx-dx,cy+dy,cx+dx,cy+dy);
            else {line(cx-dx,cy+dy,cx-dx+2,cy+dy);line(cx+dx-2,cy+dy,cx+dx,cy+dy);}
        }
    };
    if(kind==0) { // Settings: gear and hub
        circle(size/3,false);circle(std::max(2,size/9),true);
        for(int a=0;a<8;++a) {
            const double angle=a*0.78539816339;
            const int x1=cx+static_cast<int>(std::cos(angle)*(size/3-1));
            const int y1=cy+static_cast<int>(std::sin(angle)*(size/3-1));
            const int x2=cx+static_cast<int>(std::cos(angle)*(size/2-2));
            const int y2=cy+static_cast<int>(std::sin(angle)*(size/2-2));
            line(x1,y1,x2,y2);
        }
    } else if(kind==1) { // Controller
        roundRect(x+3,y+size/3,size-6,size/2,8,Color{0,0,0,0});
        outline(x+3,y+size/3,size-6,size/2,c);
        line(x+size/4,y+size/2-4,x+size/4,y+size/2+5);
        line(x+size/4-5,y+size/2,x+size/4+5,y+size/2);
        circle(std::max(2,size/13),true);
        const int bx=x+size*3/4,by=y+size/2;
        color(c);SDL_Rect dot{bx-2,by-2,4,4};SDL_RenderFillRect(renderer,&dot);
        SDL_Rect dot2{bx+5,by-7,4,4};SDL_RenderFillRect(renderer,&dot2);
    } else if(kind==2) { // Album / photos
        outline(x+4,y+6,size-8,size-12,c);
        circle(std::max(2,size/14),true);
        line(x+8,y+size-10,x+size/3,y+size/2);
        line(x+size/3,y+size/2,x+size/2,y+size*2/3);
        line(x+size/2,y+size*2/3,x+size*3/4,y+size/3);
        line(x+size*3/4,y+size/3,x+size-8,y+size-10);
    } else if(kind==3) { // Power
        circle(size/3,false);
        line(cx, y+2, cx, cy+2);
        line(cx-size/5,y+size/4,cx-size/3,y+size/3);
        line(cx+size/5,y+size/4,cx+size/3,y+size/3);
    } else if(kind==4) { // Library
        for(int i=0;i<3;++i) {
            roundRect(x+5+i*(size-14)/3,y+6, std::max(5,(size-20)/3),size-12,3,c);
        }
    } else if(kind==5) { // Home / desktop
        line(x+4,cy,x+size/2,y+4);line(x+size/2,y+4,x+size-4,cy);
        outline(x+9,cy,size-18,size/2-5,c);
        outline(cx-5,cy+size/7,10,size/4,c);
    } else if(kind==6) { // Recent / history
        circle(size/3,false);line(cx,cy,cx,cy-size/5);line(cx,cy,cx+size/5,cy+size/8);
        line(x+3,y+size/4,x+3,y+size/2);line(x+3,y+size/4,x+size/5,y+size/4);
    } else if(kind==7) { // User profile
        circle(size/8,true);
        for(int dy=0;dy<size/3;++dy) {
            const int half=static_cast<int>(std::sqrt(std::max(0,(size/3)*(size/3)-dy*dy)));
            line(cx-half,cy+size/8+dy,cx+half,cy+size/8+dy);
        }
        line(cx-size/3,cy+size/3,cx-size/4,cy+size/2);
        line(cx+size/3,cy+size/3,cx+size/4,cy+size/2);
    } else if(kind==8) { // Tools / sliders
        for(int row=0;row<3;++row) {
            const int yy=y+size/4+row*(size/4);
            line(x+5,yy,x+size-5,yy);
            const int knobX=x+size*(row==0?1:row==1?2:3)/4;
            roundRect(knobX-3,yy-4,7,8,3,c);
        }
    }
}
void drawTileIcon(const Tile&t,int x,int y,int size) {
    Color c=COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    roundRect(x,y,size,size,std::max(8,size/5),Color{255,255,255,12});
    if(SDL_Texture* texture=gIconCache.get(t)) {
        int width=0,height=0;
        if(SDL_QueryTexture(texture,nullptr,nullptr,&width,&height)==0) {
            const int inset=std::max(3,size/24),inner=std::max(1,size-inset*2);
            const float factor=std::min(static_cast<float>(inner)/std::max(1,width),static_cast<float>(inner)/std::max(1,height));
            const int dw=std::max(1,static_cast<int>(width*factor)),dh=std::max(1,static_cast<int>(height*factor));
            SDL_Rect dst{x+(size-dw)/2,y+(size-dh)/2,dw,dh};SDL_RenderCopy(renderer,texture,nullptr,&dst);
            outline(x,y,size,size,Color{255,255,255,64});return;
        }
    }
    roundRect(x,y,size,size,std::max(8,size/5),c);
    roundRect(x+1,y+1,size-2,size-2,std::max(7,size/5-1),Color{255,255,255,18});
    std::string mark=initials(t.title);int s=size>=70?3:2;int w=static_cast<int>(mark.size())*6*s-s;
    text(mark,x+(size-w)/2,y+(size-7*s)/2,s,WHITE,x+size-2);
}
std::string utf8Prefix(const std::string& value,size_t maxChars) {
    size_t pos=0,count=0;
    while(pos<value.size()&&count<maxChars) {
        const unsigned char lead=static_cast<unsigned char>(value[pos]);
        size_t n=(lead<0x80)?1:((lead&0xE0)==0xC0)?2:((lead&0xF0)==0xE0)?3:((lead&0xF8)==0xF0)?4:1;
        if(pos+n>value.size())n=1;
        pos+=n;
        ++count;
    }
    return value.substr(0,pos);
}
size_t utf8Count(const std::string& value) {
    size_t count=0;
    for(size_t pos=0;pos<value.size();++count) {
        const unsigned char lead=static_cast<unsigned char>(value[pos]);
        size_t n=(lead<0x80)?1:((lead&0xE0)==0xC0)?2:((lead&0xF0)==0xE0)?3:((lead&0xF8)==0xF0)?4:1;
        pos+=(pos+n<=value.size())?n:1;
    }
    return count;
}
std::vector<std::string> wrapTileTitle(const std::string& title,size_t maxChars) {
    std::vector<std::string> words;
    for(size_t pos=0;pos<title.size();) {
        while(pos<title.size()&&std::isspace(static_cast<unsigned char>(title[pos])))++pos;
        if(pos>=title.size())break;
        size_t end=pos;
        while(end<title.size()&&!std::isspace(static_cast<unsigned char>(title[end]))) {
            const unsigned char lead=static_cast<unsigned char>(title[end]);
            size_t n=(lead<0x80)?1:((lead&0xE0)==0xC0)?2:((lead&0xF0)==0xE0)?3:((lead&0xF8)==0xF0)?4:1;
            end+=(end+n<=title.size())?n:1;
        }
        words.push_back(title.substr(pos,end-pos));
        pos=end;
    }
    std::vector<std::string> lines;
    std::string line;
    bool truncated=false;
    for(const auto& word:words) {
        const size_t wordChars=utf8Count(word);
        if(line.empty()) {
            if(wordChars<=maxChars)line=word;
            else {
                line=utf8Prefix(word,maxChars>3?maxChars-3:maxChars)+"...";
                lines.push_back(line);truncated=true;break;
            }
        } else if(utf8Count(line)+1+wordChars<=maxChars) {
            line+=" "+word;
        } else {
            lines.push_back(line);
            if(lines.size()>=2){truncated=true;break;}
            if(wordChars<=maxChars)line=word;
            else {
                line=utf8Prefix(word,maxChars>3?maxChars-3:maxChars)+"...";
                lines.push_back(line);truncated=true;break;
            }
        }
    }
    if(!line.empty()&&lines.size()<2)lines.push_back(line);
    if(truncated&&!lines.empty()) {
        std::string& last=lines.back();
        if(last.size()<3||last.substr(last.size()-3)!="...") {
            last=utf8Prefix(last,maxChars>3?maxChars-3:maxChars)+"...";
        }
    }
    if(lines.empty())lines.push_back(title.empty()?"APP":utf8Prefix(title,maxChars));
    return lines;
}
const std::vector<std::string>& cachedTileTitleLines(const std::string& title,size_t maxChars) {
    const std::string key=std::to_string(maxChars)+"\t"+title;
    auto found=tileTitleWrapCache.find(key);
    if(found!=tileTitleWrapCache.end())return found->second;
    if(tileTitleWrapCache.size()>=512)tileTitleWrapCache.clear();
    return tileTitleWrapCache.emplace(key,wrapTileTitle(title,maxChars)).first->second;
}
SDL_Texture* getRoundedCornerMask(int radius) {
    if(!renderer||radius<=0)return nullptr;
    if(roundedCornerMaskTexture&&roundedCornerMaskRadius==radius)
        return roundedCornerMaskTexture;
    if(roundedCornerMaskTexture) {
        SDL_DestroyTexture(roundedCornerMaskTexture);
        roundedCornerMaskTexture=nullptr;
        roundedCornerMaskRadius=0;
    }
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(
        0,radius,radius,32,SDL_PIXELFORMAT_RGBA32);
    if(!surface)return nullptr;
    if(SDL_MUSTLOCK(surface)&&SDL_LockSurface(surface)!=0) {
        SDL_FreeSurface(surface);
        return nullptr;
    }
    for(int y=0;y<radius;++y) {
        Uint32* row=reinterpret_cast<Uint32*>(
            static_cast<Uint8*>(surface->pixels)+y*surface->pitch);
        for(int x=0;x<radius;++x) {
            const double dx=radius-(x+0.5),dy=radius-(y+0.5);
            const bool outside=(dx*dx+dy*dy)>
                static_cast<double>(radius)*radius;
            row[x]=SDL_MapRGBA(surface->format,255,255,255,outside?255:0);
        }
    }
    if(SDL_MUSTLOCK(surface))SDL_UnlockSurface(surface);
    roundedCornerMaskTexture=SDL_CreateTextureFromSurface(renderer,surface);
    SDL_FreeSurface(surface);
    if(!roundedCornerMaskTexture)return nullptr;
    SDL_SetTextureBlendMode(roundedCornerMaskTexture,SDL_BLENDMODE_BLEND);
    roundedCornerMaskRadius=radius;
    return roundedCornerMaskTexture;
}

void maskRoundedImageCorners(const SDL_Rect& bounds,int radius,Color fill) {
    const int r=std::max(0,std::min(radius,std::min(bounds.w,bounds.h)/2));
    SDL_Texture* mask=getRoundedCornerMask(r);
    if(!mask)return;
    SDL_SetTextureColorMod(mask,fill.r,fill.g,fill.b);
    SDL_SetTextureAlphaMod(mask,fill.a);
    const SDL_Rect corners[]={
        {bounds.x,bounds.y,r,r},
        {bounds.x+bounds.w-r,bounds.y,r,r},
        {bounds.x,bounds.y+bounds.h-r,r,r},
        {bounds.x+bounds.w-r,bounds.y+bounds.h-r,r,r}
    };
    const SDL_RendererFlip flips[]={
        SDL_FLIP_NONE,SDL_FLIP_HORIZONTAL,SDL_FLIP_VERTICAL,
        static_cast<SDL_RendererFlip>(SDL_FLIP_HORIZONTAL|SDL_FLIP_VERTICAL)
    };
    for(int i=0;i<4;++i)
        SDL_RenderCopyEx(renderer,mask,nullptr,&corners[i],0.0,nullptr,flips[i]);
    SDL_SetTextureColorMod(mask,255,255,255);
    SDL_SetTextureAlphaMod(mask,255);
}

void drawTileCover(const Tile& tile,const Workspace& workspace,
                   const SDL_Rect& bounds,Color cornerFill) {
    const Color fallback=COLORS[static_cast<size_t>(tile.color)%COLORS.size()];
    const SDL_Rect viewport{bounds.x+1,bounds.y+1,
        std::max(1,bounds.w-2),std::max(1,bounds.h-2)};
    roundRect(viewport.x,viewport.y,viewport.w,viewport.h,UiCornerRadius,fallback);

    SDL_Texture* texture=gIconCache.get(tile);
    if(texture) {
        int sourceWidth=0,sourceHeight=0;
        if(SDL_QueryTexture(texture,nullptr,nullptr,&sourceWidth,&sourceHeight)==0 &&
           sourceWidth>0 && sourceHeight>0) {
            const auto mode=static_cast<desknx::artwork::FitMode>(workspace.artworkMode);
            const auto fit=desknx::artwork::destination(
                viewport.x,viewport.y,viewport.w,viewport.h,sourceWidth,sourceHeight,mode);
            if(mode==desknx::artwork::FitMode::Contain)
                roundRect(viewport.x,viewport.y,viewport.w,viewport.h,UiCornerRadius,
                          Color{static_cast<Uint8>(fallback.r/3),static_cast<Uint8>(fallback.g/3),static_cast<Uint8>(fallback.b/3),255});
            SDL_Rect clip=viewport,target{fit.x,fit.y,fit.w,fit.h};
            // Cover scales the entire image beyond the tile and clips it to
            // the tile viewport. This avoids source-rect rounding gaps.
            SDL_RenderSetClipRect(renderer,&clip);
            SDL_RenderCopy(renderer,texture,nullptr,&target);
            SDL_RenderSetClipRect(renderer,nullptr);
        } else {
            roundRect(viewport.x,viewport.y,viewport.w,viewport.h,UiCornerRadius,fallback);
        }
    } else {
        const std::string mark=initials(tile.title);
        const int scale=std::max(2,std::min(5,bounds.h/24));
        const int width=static_cast<int>(mark.size())*6*scale-scale;
        text(mark,bounds.x+(bounds.w-width)/2,
             bounds.y+(bounds.h-7*scale)/2,scale,WHITE,bounds.x+bounds.w-8);
    }

    const size_t maxChars=static_cast<size_t>(std::max(8,
        useSocomaTheme()?(bounds.w-18)/9:(bounds.w-20)/12));
    const auto& titleLines=cachedTileTitleLines(tile.title,maxChars);
    const int stripHeight=titleLines.size()>1?44:32;
    const int overlay=std::max(0,std::min(255,workspace.labelOpacity));
    if(useSocomaTheme()) {
        if(overlay>0) {
            const int gradientHeight=std::min(viewport.h,std::max(36,viewport.h/2));
            constexpr int gradientSteps=7;
            for(int step=0;step<gradientSteps;++step) {
                const int bandTop=viewport.y+viewport.h-gradientHeight+
                    gradientHeight*step/gradientSteps;
                const int bandBottom=viewport.y+viewport.h-gradientHeight+
                    gradientHeight*(step+1)/gradientSteps;
                const int alpha=(18+step*17)*overlay/196;
                rect(viewport.x,bandTop,viewport.w,std::max(1,bandBottom-bandTop),
                     Color{3,7,15,static_cast<Uint8>(std::min(150,alpha))});
            }
        }
        if(titleLines.size()==1) {
            text(titleLines[0],bounds.x+10,bounds.y+bounds.h-22,1,WHITE,bounds.x+bounds.w-10);
        } else {
            text(titleLines[0],bounds.x+10,bounds.y+bounds.h-30,1,WHITE,bounds.x+bounds.w-10);
            text(titleLines[1],bounds.x+10,bounds.y+bounds.h-17,1,WHITE,bounds.x+bounds.w-10);
        }
    } else {
        if(overlay>0)
            rect(viewport.x,viewport.y+viewport.h-stripHeight,viewport.w,stripHeight,
                 Color{5,9,16,static_cast<Uint8>(overlay)});
        if(titleLines.size()==1) {
            text(titleLines[0],bounds.x+10,bounds.y+bounds.h-stripHeight+8,
                 2,WHITE,bounds.x+bounds.w-10);
        } else {
            text(titleLines[0],bounds.x+10,bounds.y+bounds.h-stripHeight+3,
                 2,WHITE,bounds.x+bounds.w-10);
            text(titleLines[1],bounds.x+10,bounds.y+bounds.h-stripHeight+22,
                 2,WHITE,bounds.x+bounds.w-10);
        }
    }
    // Texture rendering uses a rectangular clip; paint only the four corner
    // wedges afterwards so the actual game artwork follows the tile's 10px
    // rounded silhouette rather than exposing square image corners.
    maskRoundedImageCorners(viewport,UiCornerRadius,cornerFill);
    if (tile.kind == Kind::Game) {
        const unsigned online = nextendoPlayersForTitle(tile.data);
        if (online > 0) {
            const std::string count = std::to_string(online);
            const int badgeWidth = 38 + static_cast<int>(count.size()) * 8;
            roundRect(viewport.x + 8, viewport.y + 8, badgeWidth, 25, 13,
                      Color{5,18,15,224});
            roundRect(viewport.x + 15, viewport.y + 16, 9, 9, 5, GREEN);
            text(count, viewport.x + 29, viewport.y + 15, 1, WHITE,
                 viewport.x + badgeWidth - 5);
        }
    }
}
void drawTile(const Tile&t,bool selected,const Workspace& workspace) {
    SDL_Rect r=tileRect(t);
    if(useSocomaTheme()&&selected) {
        const double pulse=0.5+0.5*std::sin(static_cast<double>(SDL_GetTicks())*0.0042);
        const int grow=1+static_cast<int>(pulse*2.0);
        r.x-=grow;r.y-=grow;r.w+=grow*2;r.h+=grow*2;
    }
    const Color accent=COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    const Color fill=useSocomaTheme()
        ?(selected?Color{19,39,62,218}:Color{12,23,38,205})
        :(selected?Color{29,48,73,255}:Color{22,29,42,255});
    softCard(r.x,r.y,r.w,r.h,fill,
             selected?Color{92,164,255,210}:Color{255,255,255,18});

    if(t.kind==Kind::Game||t.kind==Kind::Nro) {
        drawTileCover(t,workspace,r,fill);
        if(selected) {
            if(useSocomaTheme()) {
                const double pulse=0.5+0.5*std::sin(static_cast<double>(SDL_GetTicks())*0.0042);
                const Uint8 glow=static_cast<Uint8>(150.0+pulse*85.0);
                outline(r.x-1,r.y-1,r.w+2,r.h+2,Color{BLUE.r,BLUE.g,BLUE.b,glow});
                outline(r.x+1,r.y+1,r.w-2,r.h-2,Color{255,255,255,105});
            } else {
                outline(r.x,r.y,r.w,r.h,Color{139,190,255,255});
                outline(r.x+1,r.y+1,r.w-2,r.h-2,Color{255,255,255,90});
            }
        }
    } else {
        roundRect(r.x+14,r.y+10,std::min(42,r.w-28),3,2,selected?BLUE:accent);
        const bool compact=t.w==1&&t.h==1;
        const bool wide=t.h==1&&t.w>=2;
        const int left=r.x+18, right=r.x+r.w-14;
        const int innerWidth=std::max(1,r.w-36);
        auto label=[&](const std::string& value,int y) {
            text(value,left,y,1,MUTED,right);
        };
        auto progress=[&](int y,int percent) {
            const int barWidth=std::max(16,innerWidth);
            const int bounded=std::max(0,std::min(100,percent));
            roundRect(left,y,barWidth,8,4,Color{48,62,79,255});
            roundRect(left,y,std::max(2,barWidth*bounded/100),8,4,
                      bounded>=80?RED:GREEN);
        };
        auto freeSpaceText=[&]() {
            char value[64]{};
            std::snprintf(value,sizeof(value),"%.1f GB",
                static_cast<double>(sdFreeBytes)/(1024.0*1024.0*1024.0));
            return std::string(value);
        };
        const auto& recentApps=recentUniqueLaunches();
        auto recentLine=[&](size_t index,int y,int iconSize) {
            if(index>=recentApps.size())return;
            const auto& entry=recentApps[index];
            Tile iconTile{entry.kind,entry.title,entry.data,0,0,1,1,static_cast<int>(index)};
            drawTileIcon(iconTile,left,y,iconSize);
            text(entry.title,left+iconSize+8,y+(iconSize>28?6:4),1,WHITE,right);
        };

        if(t.kind==Kind::Clock) {
            const std::string time=clockText();
            const std::string date=clockText(true).substr(6);
            if(compact) {
                text(time,r.x+10,r.y+20,3,WHITE,right);
                label(date,r.y+66);
                text("CLOCK",left,r.y+r.h-18,1,BLUE,right);
            } else if(wide) {
                text(time,left,r.y+23,5,WHITE,right);
                label(date,r.y+78);
                if(t.w>=3) {
                    drawSystemIcon(6,r.x+r.w-68,r.y+28,38,accent);
                    text("LOCAL TIME",r.x+r.w-118,r.y+78,1,BLUE,right);
                }
            } else {
                text(time,left,r.y+24,6,WHITE,right);
                text(date,left,r.y+105,2,MUTED,right);
                text(languageRussian?"ТЕКУЩЕЕ ВРЕМЯ":"LOCAL TIME",left,r.y+160,1,BLUE,right);
                drawSystemIcon(6,r.x+r.w-76,r.y+152,48,accent);
            }
        } else if(t.kind==Kind::Battery) {
            if(compact) {
                label("BATTERY",r.y+13);
                text(battery>=0?std::to_string(battery)+"%":"N/A",
                     left,r.y+34,3,WHITE,right);
                progress(r.y+82,battery);
            } else if(wide) {
                label("BATTERY",r.y+13);
                text(battery>=0?std::to_string(battery)+"%":"N/A",
                     left,r.y+33,t.w>=3?4:3,WHITE,right-70);
                progress(r.y+82,battery);
                drawSystemIcon(3,r.x+r.w-62,r.y+28,38,battery>=20?GREEN:RED);
                if(t.w>=3)text(languageRussian?"ЗАРЯД":"CHARGE",
                    r.x+r.w-112,r.y+75,1,MUTED,right);
            } else {
                label("BATTERY STATUS",r.y+14);
                drawSystemIcon(3,r.x+r.w-86,r.y+26,58,battery>=20?GREEN:RED);
                text(battery>=0?std::to_string(battery)+"%":"N/A",
                     left,r.y+50,6,WHITE,right-70);
                progress(r.y+145,battery);
                text(battery<0?(languageRussian?"НЕТ ДАННЫХ":"UNAVAILABLE"):
                     battery>=80?(languageRussian?"ЗАРЯЖЕНО":"CHARGED"):
                     battery>=20?(languageRussian?"НОРМАЛЬНО":"NORMAL"):
                                 (languageRussian?"НИЗКИЙ ЗАРЯД":"LOW BATTERY"),
                     left,r.y+172,2,battery>=20?GREEN:RED,right);
            }
        } else if(t.kind==Kind::System) {
            const u32 firmware=hosversionGet();
            char firmwareText[48]{};
            std::snprintf(firmwareText,sizeof(firmwareText),"%u.%u.%u",
                HOSVER_MAJOR(firmware),HOSVER_MINOR(firmware),HOSVER_MICRO(firmware));
            if(compact) {
                drawSystemIcon(0,left,r.y+24,40,accent);
                label("SYSTEM",r.y+13);
                text("HOS "+std::string(firmwareText),left,r.y+70,1,WHITE,right);
            } else if(wide) {
                drawSystemIcon(0,left,r.y+31,48,accent);
                label("SYSTEM INFO",r.y+13);
                text("HOS "+std::string(firmwareText),left+62,r.y+34,2,WHITE,right);
                text(languageRussian?"DESKNX • NATIVE":"DESKNX • NATIVE",
                     left+62,r.y+67,1,BLUE,right);
            } else {
                roundRect(left,r.y+34,66,66,18,Color{37,61,91,255});
                drawSystemIcon(0,left+12,r.y+46,42,WHITE);
                label("SYSTEM INFORMATION",r.y+20);
                text("HOS "+std::string(firmwareText),left+82,r.y+54,2,WHITE,right);
                text(languageRussian?"НАТИВНЫЙ ЛАУНЧЕР":"NATIVE LAUNCHER",
                     left+82,r.y+86,1,BLUE,right);
                text(languageRussian?"12 РАБОЧИХ СТОЛОВ":"12 WORKSPACES",
                     left,r.y+132,2,MUTED,right);
                text("SDL2 / LIBNX",left,r.y+168,1,MUTED,right);
            }
        } else if(t.kind==Kind::Recent) {
            if(compact) {
                label("RECENT",r.y+13);
                if(recentApps.empty())
                    text(languageRussian?"ПУСТО":"EMPTY",left,r.y+50,1,WHITE,right);
                else recentLine(0,r.y+42,26);
            } else if(wide) {
                label("RECENTLY OPENED",r.y+12);
                if(recentApps.empty())
                    text(languageRussian?"ПОКА НИЧЕГО НЕТ":"NOTHING HERE YET",
                         left,r.y+48,2,WHITE,right);
                else {
                    recentLine(0,r.y+34,26);
                    if(t.w>=3)recentLine(1,r.y+67,26);
                }
            } else {
                label(languageRussian?"НЕДАВНИЕ ЗАПУСКИ":"RECENTLY OPENED",r.y+17);
                if(recentApps.empty()) {
                    text(languageRussian?"ПОКА НИЧЕГО НЕТ":"NOTHING HERE YET",
                         left,r.y+86,2,WHITE,right);
                } else {
                    const size_t count=std::min<size_t>(4,recentApps.size());
                    for(size_t i=0;i<count;++i)recentLine(i,r.y+50+static_cast<int>(i)*42,32);
                }
                text(languageRussian?"ИСТОРИЯ ЭТОЙ КОНСОЛИ":"HISTORY ON THIS CONSOLE",
                     left,r.y+r.h-24,1,BLUE,right);
            }
        } else if(t.kind==Kind::Storage) {
            const uint64_t used=sdTotalBytes>sdFreeBytes?sdTotalBytes-sdFreeBytes:0;
            const int percent=storageStatsAvailable
                ?static_cast<int>(std::min<uint64_t>(100,(used*100)/std::max<uint64_t>(1,sdTotalBytes)))
                :-1;
            if(compact) {
                label("SD STORAGE",r.y+13);
                text(storageStatsAvailable?freeSpaceText():"N/A",left,r.y+39,2,WHITE,right);
                progress(r.y+82,percent);
            } else if(wide) {
                label("SD CARD STORAGE",r.y+12);
                text(storageStatsAvailable?freeSpaceText()+" FREE":"N/A",
                     left,r.y+34,t.w>=3?3:2,WHITE,right-78);
                if(percent>=0)text(std::to_string(percent)+"% USED",
                    right-70,r.y+63,1,MUTED,right);
                progress(r.y+86,percent);
            } else {
                label("SD CARD STORAGE",r.y+18);
                if(storageStatsAvailable) {
                    text(freeSpaceText()+" FREE",left,r.y+50,4,WHITE,right);
                    if(sdTotalBytes>0) {
                        char total[64]{};
                        std::snprintf(total,sizeof(total),"%.1f GB TOTAL",
                            static_cast<double>(sdTotalBytes)/(1024.0*1024.0*1024.0));
                        text(total,left,r.y+112,2,MUTED,right);
                    }
                    progress(r.y+157,percent);
                    text(std::to_string(percent)+"% USED",left,r.y+184,1,BLUE,right);
                } else text(languageRussian?"SD-КАРТА НЕДОСТУПНА":"SD STATUS UNAVAILABLE",
                            left,r.y+90,2,RED,right);
            }
        } else if(t.kind==Kind::Calendar) {
            std::time_t now=std::time(nullptr);
            std::tm current{};
            std::tm* nowTm=std::localtime(&now);
            if(nowTm)current=*nowTm;
            static const char* enMonths[]={"January","February","March","April","May","June",
                "July","August","September","October","November","December"};
            static const char* ruMonths[]={"ЯНВАРЬ","ФЕВРАЛЬ","МАРТ","АПРЕЛЬ","МАЙ","ИЮНЬ",
                "ИЮЛЬ","АВГУСТ","СЕНТЯБРЬ","ОКТЯБРЬ","НОЯБРЬ","ДЕКАБРЬ"};
            static const char* enWeek[]={"Mo","Tu","We","Th","Fr","Sa","Su"};
            static const char* ruWeek[]={"Пн","Вт","Ср","Чт","Пт","Сб","Вс"};
            const int month=std::max(0,std::min(11,current.tm_mon));
            const char* monthName=languageRussian?ruMonths[month]:enMonths[month];
            if(compact) {
                text(std::to_string(current.tm_mday),left,r.y+17,5,WHITE,right);
                text(monthName,left,r.y+78,1,BLUE,right);
            } else if(wide) {
                label(languageRussian?"СЕГОДНЯ":"TODAY",r.y+13);
                text(std::to_string(current.tm_mday)+" "+monthName+" "+std::to_string(current.tm_year+1900),
                     left,r.y+42,3,WHITE,right);
                text(languageRussian?ruWeek[(current.tm_wday+6)%7]:enWeek[(current.tm_wday+6)%7],
                     left,r.y+83,1,BLUE,right);
                drawSystemIcon(6,right-34,r.y+38,26,accent);
            } else {
                std::tm first=current;
                first.tm_mday=1; first.tm_hour=12; first.tm_min=0; first.tm_sec=0;
                (void)std::mktime(&first);
                std::tm lastDay=first;
                lastDay.tm_mon+=1;
                lastDay.tm_mday=0;
                (void)std::mktime(&lastDay);
                const int daysInMonth=lastDay.tm_mday;
                const int startColumn=(first.tm_wday+6)%7;
                label(monthName,r.y+15);
                text(std::to_string(current.tm_year+1900),right-45,r.y+15,1,MUTED,right);
                const int gridX=left,gridY=r.y+52;
                const int cellW=std::max(24,innerWidth/7),cellH=25;
                for(int col=0;col<7;++col)
                    text(languageRussian?ruWeek[col]:enWeek[col],
                         gridX+col*cellW+2,gridY,1,BLUE,right);
                for(int day=1;day<=daysInMonth;++day) {
                    const int index=startColumn+day-1;
                    const int col=index%7,row=index/7;
                    const int dx=gridX+col*cellW,dy=gridY+24+row*cellH;
                    if(day==current.tm_mday)
                        roundRect(dx,dy-2,std::max(20,cellW-3),22,6,
                                  Color{42,91,145,255});
                    text(std::to_string(day),dx+4,dy+3,1,
                         day==current.tm_mday?WHITE:MUTED,right);
                }
                text(languageRussian?"ТЕКУЩАЯ ДАТА":"CURRENT DATE",
                     left,r.y+r.h-22,1,accent,right);
            }
        } else if(t.kind==Kind::Unsupported) {
            roundRect(r.x+3,r.y+3,r.w-6,r.h-6,14,Color{39,46,58,255});
            text("UNSUPPORTED ITEM",r.x+14,r.y+18,2,WHITE,right);
            text(t.unknownTypeCode.empty()?"?":t.unknownTypeCode,
                 r.x+14,r.y+52,2,MUTED,right);
        }
    }
    if(selected)roundRect(r.x+r.w-20,r.y+13,7,7,4,BLUE);
}
void drawDotted(int x,int y,int w,int h) {
    const Color dot=useSocomaTheme()?Color{137,165,198,92}:Color{67,79,98,180};
    for(int xx=x;xx<x+w;xx+=12){rect(xx,y,6,2,dot);rect(xx,y+h-2,6,2,dot);}
    for(int yy=y;yy<y+h;yy+=12){rect(x,yy,2,6,dot);rect(x+w-2,yy,2,6,dot);}
}
int visibleStart() {
    if(touchDragging&&touchDragTabStart>=0)return touchDragTabStart;
    return std::max(0,std::min(deskIndex-2,static_cast<int>(desks.size())-VisibleDeskTabs));
}
void drawScreen() {
    gIconCache.beginFrame();
    const std::vector<Candidate>& available = listOpen ? visibleCatalogueSnapshot() : visibleCatalogueCache;
    bool catalogReadyNow=false,catalogCompleteNow=false,catalogTruncatedNow=false;
    bool catalogueNsAvailableNow=false;
    std::uint32_t catalogueNsErrorNow=0;
    size_t catalogueGameCountNow=0,catalogueNroCountNow=0;
    readCatalogueState(catalogReadyNow,catalogCompleteNow,catalogTruncatedNow,
        &catalogueNsAvailableNow,&catalogueNsErrorNow,
        &catalogueGameCountNow,&catalogueNroCountNow);
    refreshBattery();
    refreshStorageStats();
    rect(0,0,SW,SH,BG);
    for(int band=0;band<24;++band) {
        const double t=static_cast<double>(band)/23.0;
        const auto blend=[&](Uint8 a,Uint8 b) {
            return static_cast<Uint8>(static_cast<double>(a)+
                (static_cast<double>(b)-static_cast<double>(a))*t);
        };
        rect(0,band*30,SW,31,Color{blend(THEME_TOP.r,THEME_BOTTOM.r),
            blend(THEME_TOP.g,THEME_BOTTOM.g),blend(THEME_TOP.b,THEME_BOTTOM.b),255});
    }
    if(!desks.empty())
        drawWorkspaceBackground(desks[static_cast<size_t>(std::max(0,
            std::min(deskIndex,static_cast<int>(desks.size())-1)))]);
    else
        drawWallpaper();
    if(useSocomaTheme())drawSocomaAtmosphere();

    // Floating system bar. Hit targets remain identical for touch and controller users.
    softCard(56,16,1168,64,Color{24,32,45,224},Color{255,255,255,28});
    struct HeaderTool { int icon; bool active; const char* label; };
    const HeaderTool headerTools[]={
        {2,false,"GALLERY"},{0,appSettingsMenu,"SETTINGS"},
        {1,false,"PADS"},{8,systemMenu,"TOOLS"}
    };
    for(int i=0;i<ToolbarButtonCount;++i) {
        const auto& item=headerTools[i];
        const int x=toolbarButtonX(i);
        const Color navFill=useSocomaTheme()
            ?(item.active?Color{45,91,145,224}:Color{20,34,51,190})
            :(item.active?Color{48,82,124,255}:Color{37,47,62,230});
        roundRect(x,24,ToolbarButtonWidth,48,13,navFill);
        if(item.active)outline(x,24,ToolbarButtonWidth,48,
            useSocomaTheme()?Color{BLUE.r,BLUE.g,BLUE.b,175}:Color{120,174,235,150});
        drawSystemIcon(item.icon,x+12,31,24,item.active?WHITE:Color{204,214,228,255});
        const int labelWidth=static_cast<int>(std::string(item.label).size())*6;
        text(item.label,x+std::max(5,(ToolbarButtonWidth-labelWidth)/2),56,1,
             item.active?WHITE:Color{179,191,207,255},x+ToolbarButtonWidth-4);
    }
    softCard(470,24,392,48,Color{32,43,59,235},Color{255,255,255,24});
    drawSystemIcon(7,486,34,27,Color{215,228,246,255});
    text(selectedUserLabel,526,41,2,WHITE,760);
    text("CHANGE",780,43,1,BLUE,850);
    roundRect(870,24,142,48,18,Color{32,43,59,235});
    text(nextendoTotalLabel(),882,43,1,GREEN,1003);
    roundRect(1022,24,180,48,18,Color{32,43,59,235});
    text(clockText(),1040,37,3,WHITE,1136);
    rect(1148,34,1,26,Color{255,255,255,30});
    text(battery>=0?std::to_string(battery)+"%":"BAT --",1158,43,1,
         battery>=20?GREEN:RED,1198);

    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(useSocomaTheme()) {
        text("WORKSPACE",66,100,1,BLUE,420);
        text(d.name,66,116,2,WHITE,520);
        text(std::to_string(deskIndex+1)+" / "+std::to_string(desks.size()),
             1092,121,1,MUTED,1206);
    } else {
        // v0.1 keeps its original single desktop surface unchanged.
        softCard(56,96,1168,454,Color{25,35,49,186},Color{255,255,255,30});
    }
    if(d.tiles.empty()) {
        if(useSocomaTheme()) {
            softCard(420,282,440,126,Color{17,30,48,190},Color{255,255,255,34});
            roundRect(452,309,48,48,16,Color{38,76,119,205});
            drawSystemIcon(4,465,322,22,WHITE);
            text("A SPACE FOR YOUR GAMES",520,304,2,WHITE,830);
            text("Add apps and widgets to start building this desktop.",520,338,1,MUTED,830);
            roundRect(520,365,138,30,12,Color{43,111,190,220});
            text("Y  ADD APPS",540,374,1,WHITE,650);
        } else {
            softCard(360,278,560,154,Color{20,29,43,245},Color{255,255,255,24});
            roundRect(390,302,46,46,14,Color{38,76,119,255});
            drawSystemIcon(4,402,314,22,WHITE);
            text("THIS DESKTOP IS EMPTY",454,302,2,WHITE,880);
            text("Add games, homebrew and widgets to get started.",454,337,1,MUTED,880);
            roundRect(454,370,176,38,12,Color{43,111,190,255});
            text("Y  ADD APPS",493,383,1,WHITE,620);
            text("+  CREATE ANOTHER DESKTOP",650,384,1,MUTED,890);
        }
    }
    if(editing)for(int y=0;y<ROWS;++y)for(int x=0;x<COLS;++x)if(itemAt(d,x,y)<0) {
        int px=LEFT+x*(CW+GX),py=TOP+y*(CH+GY);drawDotted(px,py,CW,CH);
        if(x==selectedX&&y==selectedY) {
            roundRect(px+2,py+2,CW-4,CH-4,16,Color{34,63,99,100});
            outline(px+2,py+2,CW-4,CH-4,BLUE);
            text("+",px+CW/2-4,py+CH/2-10,2,WHITE,px+CW-3);
        }
    }
    for(const auto&t:d.tiles)drawTile(t,selectedX>=t.x&&selectedX<t.x+t.w&&selectedY>=t.y&&selectedY<t.y+t.h,d);
    if(editing&&dragging)text("MOVING",1040,118,1,BLUE,1260);
    softCard(28,558,1224,56,Color{18,26,39,238},Color{255,255,255,16});
    const bool show=statusUntil==0||SDL_GetTicks()<statusUntil;
    roundRect(48,576,7,7,4,show?BLUE:GREEN);
    text(show?statusText:(editing?"ARRANGE MODE  /  EMPTY CELLS STAY EMPTY":"READY"),
         68,575,1,show?WHITE:MUTED,620);
    struct Hint { const char* key; const char* action; };
    const Hint normalHints[]={{"A","OPEN"},{"Y","ADD"},{"X","EDIT"},{"L/R","SPACES"}};
    const Hint editHints[]={{"A","MOVE / ADD"},{"D-PAD","POSITION"},{"ZL/ZR","SIZE"},{"-","REMOVE"}};
    const Hint dragHints[]={{"D-PAD","MOVE"},{"L/R","CHANGE DESK"},{"A / B","DROP"},{"-","REMOVE"}};
    const int selectedTileIndex=itemAt(d,selectedX,selectedY);
    if(editing&&!dragging&&selectedTileIndex>=0) {
        // Touchscreen users need the same tile-management actions as controller
        // users. Keep these real, generous hit targets in the persistent footer.
        roundRect(650,571,165,28,8,Color{35,57,83,255});
        text("SIZE -  /  ZL",665,581,1,WHITE,808);
        roundRect(826,571,165,28,8,Color{35,57,83,255});
        text("SIZE +  /  ZR",841,581,1,WHITE,984);
        roundRect(1002,571,228,28,8,Color{91,39,48,255});
        text("REMOVE TILE  /  MINUS",1018,581,1,WHITE,1220);
    } else {
        const Hint* hints=dragging?dragHints:(editing?editHints:normalHints);
        for(int i=0;i<4;++i) {
            const int hx=642+i*148;
            roundRect(hx,571,48,28,8,Color{34,45,63,255});
            text(hints[i].key,hx+5,581,1,BLUE,hx+43);
            text(hints[i].action,hx+56,581,1,MUTED,hx+144);
        }
    }
    // Workspace switcher anchors the lower edge, matching a familiar
    // console desktop rather than duplicating navigation in a second dock.
    softCard(28,624,1224,82,Color{18,26,39,242},Color{255,255,255,24});
    const int deskStart=visibleStart();
    for(int i=0;i<VisibleDeskTabs&&deskStart+i<static_cast<int>(desks.size());++i) {
        const int index=deskStart+i;
        const int x=DeskTabX+i*DeskTabStep;
        const bool active=index==deskIndex;
        roundRect(x,641,DeskTabWidth,48,14,
            useSocomaTheme()
                ?(active?Color{45,91,145,205}:Color{17,29,44,175})
                :(active?Color{53,91,139,255}:Color{34,43,58,235}));
        if(active) {
            outline(x,641,DeskTabWidth,48,useSocomaTheme()
                ?Color{BLUE.r,BLUE.g,BLUE.b,145}:Color{144,189,244,110});
            if(useSocomaTheme())roundRect(x+18,684,DeskTabWidth-36,2,1,BLUE);
        }
        text(desks[static_cast<size_t>(index)].name,x+12,658,1,
             active?WHITE:MUTED,x+DeskTabWidth-8);
    }
    roundRect(770,641,52,48,14,desks.size()<static_cast<size_t>(MAX_DESKS)?
        Color{35,70,105,255}:Color{34,43,58,235});
    text("+",787,652,3,desks.size()<static_cast<size_t>(MAX_DESKS)?BLUE:MUTED,812);
    roundRect(832,641,52,48,14,Color{34,43,58,235});
    text("...",842,660,2,WHITE,876);
    text(std::to_string(deskIndex+1)+" / "+std::to_string(desks.size()),
         916,658,1,MUTED,1040);
    roundRect(1096,638,128,54,16,
        useSocomaTheme()
            ?(editing?Color{45,91,145,220}:Color{19,34,52,195})
            :(editing?Color{53,100,156,255}:Color{34,46,64,255}));
    drawSystemIcon(5,1108,650,30,WHITE);
    text(editing?"DONE":"EDIT",1148,657,2,WHITE,1216);
    if(listOpen) {
        rect(0,0,SW,SH,Color{3,7,14,194});
        const int mx=110,my=70,mw=1060,mh=570;
        softCard(mx,my,mw,mh,Color{17,24,36,255},Color{255,255,255,25});
        text("APP LIBRARY",mx+28,my+17,3,WHITE,mx+mw-24);
        text("Installed games, homebrew and system widgets",
             mx+30,my+43,1,MUTED,mx+mw-24);
        struct DrawerChip { int x; int w; const char* label; };
        const DrawerChip chips[]={{mx+28,96,"Y  SEARCH"},{mx+132,82,"X  SORT"},
            {mx+222,112,"L/R  FILTER"},{mx+342,82,"-  CLEAR"},
            {mx+432,126,"R STICK RESCAN"},{mx+566,72,"B  CLOSE"},
            {mx+646,72,"A  ADD"}};
        for(const auto& chip:chips) {
            roundRect(chip.x,my+52,chip.w,25,9,Color{31,43,61,255});
            text(chip.label,chip.x+7,my+61,1,WHITE,chip.x+chip.w-5);
        }
        std::string viewLabel=std::string("FILTER  ")+
            desknx::catalog_view::FilterName(catalogueFilter)+"     SORT  "+
            desknx::catalog_view::SortName(catalogueSort);
        if(!catalogueQuery.empty())viewLabel+="     SEARCH  "+catalogueQuery;
        if(!catalogCompleteNow)viewLabel+="     /  UPDATING";
        if(catalogueNsErrorNow!=0) {
            char nsError[40]{};
            std::snprintf(nsError,sizeof(nsError),"     /  NS  0X%08X",
                static_cast<unsigned int>(catalogueNsErrorNow));
            viewLabel+=nsError;
        }
        text(viewLabel,mx+28,my+82,1,BLUE,mx+mw-22);
        text("SELECT AN APP TO SEE DETAILS",mx+28,my+96,1,MUTED,mx+760);
        constexpr int vis=10;
        if(available.empty()) {
            listIndex=0;
            listOffset=0;
        } else {
            listIndex=std::max(0,std::min(listIndex,static_cast<int>(available.size())-1));
            const int maxOffset=std::max(0,static_cast<int>(available.size())-vis);
            listOffset=std::max(0,std::min(listOffset,maxOffset));
            if(listIndex<listOffset)listOffset=listIndex;
            if(listIndex>=listOffset+vis)listOffset=listIndex-vis+1;
        }
        roundRect(mx+20,my+110,744,410,16,Color{13,19,29,255});
        if(!catalogCompleteNow&&available.empty()) {
            text("PREPARING YOUR LIBRARY",mx+42,my+150,2,WHITE,mx+720);
            text("The first scan reads installed apps and SD-card homebrew.",
                 mx+42,my+184,1,MUTED,mx+720);
        } else if(catalogCompleteNow&&available.empty()) {
            if(!catalogueNsAvailableNow) {
                char errorText[96]{};
                std::snprintf(errorText,sizeof(errorText),"GAME SERVICE UNAVAILABLE  0X%08X",
                    static_cast<unsigned int>(catalogueNsErrorNow));
                text(errorText,mx+42,my+150,1,RED,mx+720);
                text("A saved library will appear here when a valid cache is available.",
                     mx+42,my+180,1,MUTED,mx+720);
            } else {
                text("NO MATCHING APPS",mx+42,my+150,2,WHITE,mx+720);
                text("Change the filter or clear your search.",
                     mx+42,my+184,1,MUTED,mx+720);
            }
        } else {
            for(int row=0;row<vis;++row) {
                const int i=listOffset+row;
                if(i>=static_cast<int>(available.size()))break;
                const int yy=my+110+row*40;
                const Candidate& candidate=available[static_cast<size_t>(i)];
                if(i==listIndex)roundRect(mx+27,yy,730,35,10,Color{35,58,88,255});
                Tile iconTile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
                drawTileIcon(iconTile,mx+38,yy+4,27);
                text(candidate.title,mx+78,yy+4,2,
                     i==listIndex?WHITE:MUTED,mx+592);
                if(candidate.kind==Kind::Game) {
                    std::string meta=candidate.sizeBytes
                        ?formatBytes(candidate.sizeBytes):"SIZE UNKNOWN";
                    const unsigned online=nextendoPlayersForTitle(candidate.data);
                    if(online>0)meta+="   /   "+std::to_string(online)+" ONLINE";
                    text(meta,mx+78,yy+24,1,BLUE,mx+592);
                }
                std::string appBadge=typeName(candidate.kind);
                if(candidate.kind==Kind::Game) {
                    if(candidate.viewFlags&NsViewFlagNeedsUpdate)appBadge="UPDATE";
                    else if(candidate.viewFlags&NsViewFlagIsGameCard)appBadge="GAME CARD";
                }
                text(appBadge,mx+615,yy+11,1,
                    (candidate.viewFlags&NsViewFlagNeedsUpdate)?Color{255,177,74,255}:
                    (i==listIndex?BLUE:MUTED),mx+750);
            }
        }
        roundRect(mx+780,my+110,252,410,16,Color{22,31,45,255});
        if(!available.empty()) {
            const Candidate& selected=available[static_cast<size_t>(
                std::max(0,std::min(listIndex,static_cast<int>(available.size())-1)))];
            Tile preview{selected.kind,selected.title,selected.data,0,0,1,1,selected.color};
            drawTileIcon(preview,mx+866,my+128,78);
            text(selected.title,mx+798,my+218,2,WHITE,mx+mw-30);
            std::string detailType=typeName(selected.kind);
            if(selected.kind==Kind::Game&&(selected.viewFlags&NsViewFlagNeedsUpdate))
                detailType="UPDATE AVAILABLE";
            else if(selected.kind==Kind::Game&&(selected.viewFlags&NsViewFlagIsGameCard))
                detailType="GAME CARD";
            text(detailType,mx+800,my+252,1,BLUE,mx+mw-30);
            if(selected.kind==Kind::Game) {
                text("TITLE ID",mx+800,my+282,1,MUTED,mx+mw-30);
                text(selected.data,mx+800,my+300,1,WHITE,mx+mw-30);
                text(selected.sizeBytes?("SIZE  "+formatBytes(selected.sizeBytes)):"SIZE  UNKNOWN",
                     mx+800,my+326,1,MUTED,mx+mw-30);
                const unsigned online=nextendoPlayersForTitle(selected.data);
                text(online?("ONLINE NOW  "+std::to_string(online)):"ONLINE NOW  —",
                     mx+800,my+346,1,online?GREEN:MUTED,mx+mw-30);
                if(!selected.playStatsQueried) {
                    text("PLAY STATS NOT LOADED",mx+800,my+374,1,MUTED,mx+mw-30);
                } else if(!selected.playStatsAvailable) {
                    text("PLAY STATS UNAVAILABLE",mx+800,my+374,1,RED,mx+mw-30);
                } else {
                    char playLabel[48]{};
                    const unsigned long long minutes=
                        static_cast<unsigned long long>(selected.playtimeSeconds/60);
                    if(minutes<60)
                        std::snprintf(playLabel,sizeof(playLabel),"PLAY TIME  %llu MIN",minutes);
                    else
                        std::snprintf(playLabel,sizeof(playLabel),"PLAY TIME  %lluH %02lluM",
                                      minutes/60,minutes%60);
                    text(playLabel,mx+800,my+374,1,MUTED,mx+mw-30);
                    char launchLabel[48]{};
                    std::snprintf(launchLabel,sizeof(launchLabel),"SYSTEM LAUNCHES  %u",selected.launchCount);
                    text(launchLabel,mx+800,my+394,1,MUTED,mx+mw-30);
                    if(selected.lastPlayed!=0) {
                        const std::time_t stamp=static_cast<std::time_t>(selected.lastPlayed);
                        std::tm local{};
                        char dateLabel[48]{};
                        if(localtime_r(&stamp,&local)&&
                           std::strftime(dateLabel,sizeof(dateLabel),"LAST PLAYED  %d.%m.%Y",&local))
                            text(dateLabel,mx+800,my+414,1,MUTED,mx+mw-30);
                    } else {
                        text("NOT PLAYED YET",mx+800,my+414,1,MUTED,mx+mw-30);
                    }
                }
                const std::size_t localRuns=desknx::launch_history::count(
                    recentLaunches,Kind::Game,selected.data);
                const std::uint64_t localLast=desknx::launch_history::lastTimestamp(
                    recentLaunches,Kind::Game,selected.data);
                std::string localHistory="TRANSWITCH  "+std::to_string(localRuns)+" RECORDED";
                if(localLast!=0)localHistory+="  /  "+historyTimestampText(localLast,true);
                text(localHistory,mx+800,my+434,1,BLUE,mx+mw-30);
            } else if(selected.kind==Kind::Nro) {
                std::string fileName=selected.data;
                const size_t slash=fileName.find_last_of('/');
                const std::string folder=slash==std::string::npos?std::string{}:fileName.substr(0,slash);
                if(slash!=std::string::npos)fileName=fileName.substr(slash+1);
                text("FILE",mx+800,my+282,1,MUTED,mx+mw-30);
                text(fileName,mx+800,my+300,1,WHITE,mx+mw-30);
                text("FOLDER",mx+800,my+326,1,MUTED,mx+mw-30);
                text(folder,mx+800,my+344,1,WHITE,mx+mw-30);
                const uint64_t bytes=cachedNroFileSize(selected.data);
                if(bytes>0) {
                    char sizeLabel[48]{};
                    if(bytes>=1024ULL*1024ULL)
                        std::snprintf(sizeLabel,sizeof(sizeLabel),"SIZE  %llu MB",bytes/(1024ULL*1024ULL));
                    else
                        std::snprintf(sizeLabel,sizeof(sizeLabel),"SIZE  %llu KB",bytes/1024ULL);
                    text(sizeLabel,mx+800,my+370,1,MUTED,mx+mw-30);
                }
                const std::size_t localRuns=desknx::launch_history::count(
                    recentLaunches,Kind::Nro,selected.data);
                const std::uint64_t localLast=desknx::launch_history::lastTimestamp(
                    recentLaunches,Kind::Nro,selected.data);
                std::string localHistory="TRANSWITCH  "+std::to_string(localRuns)+" RECORDED";
                if(localLast!=0)localHistory+="  /  "+historyTimestampText(localLast,true);
                text(localHistory,mx+800,my+414,1,BLUE,mx+mw-30);
            } else {
                text("SYSTEM WIDGET",mx+800,my+282,1,MUTED,mx+mw-30);
                text("Adds live information to your desktop.",mx+800,my+302,1,WHITE,mx+mw-30);
            }
            if(selected.kind==Kind::Game) {
                roundRect(mx+792,my+462,112,42,12,Color{46,91,139,255});
                text("ZL  COVER",mx+810,my+477,1,WHITE,mx+904);
                roundRect(mx+916,my+462,104,42,12,Color{49,119,199,255});
                text("A  ADD",mx+941,my+477,1,WHITE,mx+1020);
            } else {
                roundRect(mx+792,my+462,228,42,12,Color{49,119,199,255});
                text("A  ADD TO WORKSPACE",mx+823,my+477,1,WHITE,mx+mw-30);
            }
        }
        const std::string counts=std::to_string(catalogueGameCountNow)+" GAMES  /  "+
            std::to_string(catalogueNroCountNow)+" HOMEBREW  /  "+
            std::to_string(available.size())+" SHOWN";
        text(counts,mx+28,my+mh-26,1,MUTED,mx+mw-30);
    }
    if(steamGridPickerOpen) {
        rect(0,0,SW,SH,Color{2,6,13,214});
        softCard(230,92,820,536,Color{15,24,38,252},Color{255,255,255,38});
        text("STEAMGRIDDB ARTWORK",270,119,3,WHITE,980);
        text(steamGridTargetTitle,272,158,1,MUTED,980);
        roundRect(928,112,82,34,10,Color{48,57,72,255});
        text("B BACK",945,123,1,WHITE,1004);
        const bool choosingGames=steamGridPickerStage==SteamGridPickerStage::ChooseGame||
            steamGridPickerStage==SteamGridPickerStage::SearchingGames;
        const bool choosingArt=steamGridPickerStage==SteamGridPickerStage::ChooseArtwork||
            steamGridPickerStage==SteamGridPickerStage::LoadingArtwork||
            steamGridPickerStage==SteamGridPickerStage::Applying;
        text(choosingGames?"1 / 2  SELECT THE MATCH":"2 / 2  SELECT THE ARTWORK",
             272,184,1,BLUE,980);
        if(steamGridPickerStage==SteamGridPickerStage::SearchingGames)
            text("SEARCHING STEAMGRIDDB...",290,248,2,WHITE,970);
        else if(steamGridPickerStage==SteamGridPickerStage::LoadingArtwork)
            text("LOADING AVAILABLE ARTWORK...",290,248,2,WHITE,970);
        else if(steamGridPickerStage==SteamGridPickerStage::Applying)
            text("DOWNLOADING AND SAVING TO SD CARD...",290,248,2,WHITE,970);
        else if(choosingGames) {
            if(steamGridGames.empty())text("NO MATCHES FOUND. PRESS Y TO SEARCH AGAIN.",290,246,1,WHITE,970);
            const int count=static_cast<int>(steamGridGames.size());
            steamGridListIndex=std::max(0,std::min(steamGridListIndex,std::max(0,count-1)));
            const int maxOffset=std::max(0,count-9);
            steamGridListOffset=std::max(0,std::min(steamGridListOffset,maxOffset));
            if(steamGridListIndex<steamGridListOffset)steamGridListOffset=steamGridListIndex;
            if(steamGridListIndex>=steamGridListOffset+9)steamGridListOffset=steamGridListIndex-8;
            for(int row=0;row<9;++row) {
                const int index=steamGridListOffset+row;
                if(index>=count)break;
                const int yy=218+row*36;
                if(index==steamGridListIndex)roundRect(274,yy,732,31,9,Color{38,72,111,255});
                text(steamGridGames[static_cast<size_t>(index)].name,294,yy+10,1,
                     index==steamGridListIndex?WHITE:MUTED,980);
                text("#"+std::to_string(steamGridGames[static_cast<size_t>(index)].id),
                     868,yy+10,1,index==steamGridListIndex?BLUE:MUTED,994);
            }
        } else if(choosingArt) {
            const int count=static_cast<int>(steamGridArtwork.size());
            steamGridListIndex=std::max(0,std::min(steamGridListIndex,std::max(0,count-1)));
            const int maxOffset=std::max(0,count-9);
            steamGridListOffset=std::max(0,std::min(steamGridListOffset,maxOffset));
            if(steamGridListIndex<steamGridListOffset)steamGridListOffset=steamGridListIndex;
            if(steamGridListIndex>=steamGridListOffset+9)steamGridListOffset=steamGridListIndex-8;
            text("MATCH: "+steamGridSelectedGameName,274,208,1,MUTED,980);
            if(steamGridArtwork.empty()&&steamGridPickerStage==SteamGridPickerStage::ChooseArtwork)
                text("NO ARTWORK FOUND. PRESS B TO CHOOSE ANOTHER MATCH.",290,248,1,WHITE,970);
            for(int row=0;row<9;++row) {
                const int index=steamGridListOffset+row;
                if(index>=count)break;
                const auto& item=steamGridArtwork[static_cast<size_t>(index)];
                const int yy=238+row*34;
                if(index==steamGridListIndex)roundRect(274,yy,424,30,9,Color{38,72,111,255});
                const char* kind=item.type=="heroes"?"HERO":item.type=="grids"?"GRID":"ICON";
                text(kind,294,yy+9,1,index==steamGridListIndex?BLUE:MUTED,390);
                text(std::to_string(item.asset.width)+" x "+std::to_string(item.asset.height),
                     410,yy+9,1,index==steamGridListIndex?WHITE:MUTED,690);
            }
            softCard(716,226,290,310,Color{22,31,45,255},Color{255,255,255,25});
            rect(728,238,266,214,Color{9,14,22,255});
            if(count>0&&steamGridListIndex>=0&&steamGridListIndex<count) {
                const auto& selected=steamGridArtwork[static_cast<size_t>(steamGridListIndex)];
                if(steamGridPreviewTexture&&
                   steamGridPreviewAssetId==selected.asset.id&&
                   steamGridPreviewWidth>0&&steamGridPreviewHeight>0) {
                    const double scale=std::min(258.0/static_cast<double>(steamGridPreviewWidth),
                                                206.0/static_cast<double>(steamGridPreviewHeight));
                    const int width=std::max(1,static_cast<int>(steamGridPreviewWidth*scale));
                    const int height=std::max(1,static_cast<int>(steamGridPreviewHeight*scale));
                    SDL_Rect destination{732+(258-width)/2,242+(206-height)/2,width,height};
                    SDL_RenderCopy(renderer,steamGridPreviewTexture,nullptr,&destination);
                } else if(steamGridPreviewLoading) {
                    text("LOADING PREVIEW...",758,330,1,WHITE,980);
                } else if(!steamGridPreviewError.empty()) {
                    text("PREVIEW UNAVAILABLE",750,330,1,MUTED,980);
                    text("A STILL DOWNLOADS FULL IMAGE",738,356,1,MUTED,990);
                } else {
                    text("SELECT AN ARTWORK",770,330,1,MUTED,980);
                }
                const char* selectedKind=selected.type=="heroes"?"HERO":
                    selected.type=="grids"?"GRID":"ICON";
                text(selectedKind,738,468,2,BLUE,980);
                text(std::to_string(selected.asset.width)+" x "+
                     std::to_string(selected.asset.height),738,496,1,WHITE,980);
                text("ARTWORK ID  #"+std::to_string(selected.asset.id),738,516,1,MUTED,990);
            } else {
                text("NO PREVIEW AVAILABLE",750,330,1,MUTED,980);
            }
            if(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork) {
                const bool canApply=!steamGridPreviewLoading;
                roundRect(820,548,186,38,11,canApply?Color{49,119,199,255}:Color{48,62,79,255});
                text("A  APPLY COVER",852,560,1,canApply?WHITE:MUTED,994);
            }
        }
        if(!steamGridError.empty())
            text(steamGridError,274,555,1,RED,810);
        else if(!steamGridPreviewError.empty()&&steamGridPickerStage==SteamGridPickerStage::ChooseArtwork)
            text("THUMBNAIL UNAVAILABLE - FULL IMAGE CAN STILL BE APPLIED",274,555,1,MUTED,810);
        if(steamGridPickerStage==SteamGridPickerStage::ChooseGame||
           steamGridPickerStage==SteamGridPickerStage::ChooseArtwork)
            text(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork
                ?"UP / DOWN PREVIEW     A APPLY     Y SEARCH AGAIN     B BACK"
                :"UP / DOWN SELECT     A CHOOSE     Y SEARCH AGAIN     B BACK",
                 274,585,1,BLUE,1004);
    }
    if(workspaceMenu) {
        rect(0,0,SW,SH,Color{3,7,14,190});
        softCard(340,150,600,420,Color{18,25,37,255},Color{255,255,255,26});
        text("WORKSPACE OPTIONS",390,170,3,WHITE,900);
        const Workspace& current=desks[static_cast<size_t>(deskIndex)];
        const bool canMoveTile=editing&&itemAt(current,selectedX,selectedY)>=0;
        const char* actions[]={"OPEN WORKSPACE","MOVE SELECTED TILE","CUSTOMIZE DESKTOP",
                               "RENAME WORKSPACE","DELETE WORKSPACE","REORDER WORKSPACES","CANCEL"};
        for(int i=0;i<7;++i) {
            const int yy=214+i*42;
            if(i==workspaceMenuIndex)roundRect(390,yy,500,36,10,Color{45,61,82,255});
            const Color labelColor=(i==1&&!canMoveTile)?Color{91,101,118,255}:
                                   (i==workspaceMenuIndex?WHITE:MUTED);
            text(actions[i],412,yy+12,1,labelColor,875);
        }
        text("REORDER DESKTOPS WITHOUT MOVING THEIR CONTENTS",390,520,1,MUTED,900);
    }
    if(workspaceSettingsMenu) {
        rect(0,0,SW,SH,Color{3,7,14,198});
        softCard(300,104,680,520,Color{17,24,36,255},Color{255,255,255,28});
        const Workspace& desk=desks[static_cast<size_t>(deskIndex)];
        text("CUSTOMIZE DESKTOP",344,130,3,WHITE,930);
        text(desk.name+"  /  PERSONAL APPEARANCE",346,168,1,MUTED,930);
        roundRect(842,132,92,32,10,Color{54,57,66,255});
        text("B  BACK",856,143,1,WHITE,926);
        const char* fitNames[]={"COVER","CONTAIN","STRETCH"};
        const char* labels[]={"CHOOSE DESKTOP BACKGROUND","BACKGROUND FIT","GAME ART FIT",
                              "TITLE OVERLAY","RESET DESKTOP STYLE","DONE"};
        const std::string values[]={
            desk.backgroundPath.empty()?"GLOBAL WALLPAPER":"CUSTOM IMAGE",
            fitNames[std::max(0,std::min(2,desk.backgroundFit))],
            fitNames[std::max(0,std::min(2,desk.artworkMode))],
            std::to_string(desk.labelOpacity)+" / 255",
            "RESTORE DEFAULTS","CLOSE THIS MENU"
        };
        for(int i=0;i<6;++i) {
            const int yy=216+i*48;
            if(i==workspaceSettingsIndex)roundRect(340,yy,600,40,10,Color{35,61,93,255});
            text(labels[i],360,yy+8,1,i==workspaceSettingsIndex?WHITE:MUTED,700);
            text(values[i],710,yy+8,1,i==workspaceSettingsIndex?BLUE:MUTED,920);
        }
        text("COVER FILLS THE TILE; CONTAIN SHOWS THE WHOLE IMAGE",346,568,1,MUTED,930);
        text("UP / DOWN SELECT     A CHANGE     B CLOSE",346,606,1,BLUE,930);
    }
    if(workspaceReorderOpen) {
        rect(0,0,SW,SH,Color{3,7,14,205});
        softCard(350,110,580,500,Color{18,25,37,248},Color{255,255,255,30});
        text("REORDER WORKSPACES",394,138,3,WHITE,900);
        text("Choose where the current desktop should appear.",396,176,1,MUTED,900);
        for(size_t i=0;i<desks.size();++i) {
            const int yy=205+static_cast<int>(i)*28;
            const bool selected=static_cast<int>(i)==workspaceReorderIndex;
            const bool active=static_cast<int>(i)==deskIndex;
            if(selected)roundRect(390,yy,500,25,7,Color{45,68,99,255});
            text(desks[i].name,410,yy+7,1,selected?WHITE:(active?BLUE:MUTED),760);
            text(active?"CURRENT":(selected?"TARGET":std::to_string(i+1)),
                 790,yy+7,1,selected?BLUE:MUTED,875);
        }
        text("UP / DOWN SELECT TARGET     A MOVE HERE     B CANCEL",394,574,1,BLUE,906);
    }
    if(workspacePicker) {
        rect(0,0,SW,SH,Color{3,7,14,194});
        softCard(320,72,640,580,Color{18,25,37,255},Color{255,255,255,26});
        text(workspacePickerMoveTile?"MOVE TILE TO WORKSPACE":"OPEN WORKSPACE",
             360,100,3,WHITE,920);
        text("SELECT ANY OF YOUR "+std::to_string(desks.size())+" WORKSPACES",
             360,136,1,MUTED,920);
        for(size_t i=0;i<desks.size();++i) {
            const int yy=168+static_cast<int>(i)*32;
            const bool selected=static_cast<int>(i)==workspacePickerIndex;
            const bool current=static_cast<int>(i)==deskIndex;
            if(selected)roundRect(360,yy,560,29,8,Color{45,61,82,255});
            else if(current)roundRect(360,yy,560,29,8,Color{30,43,60,255});
            text(desks[i].name,382,yy+9,1,
                 selected?WHITE:(current?BLUE:MUTED),760);
            const std::string detail=current?"CURRENT":
                std::to_string(desks[i].tiles.size())+" ITEMS";
            text(detail,804,yy+9,1,current?BLUE:MUTED,902);
        }
        roundRect(744,590,176,38,10,Color{54,57,66,255});
        text("CANCEL",795,603,1,WHITE,875);
        text("UP / DOWN SELECT     A OPEN     B BACK",360,608,1,MUTED,730);
    }
    if(deletePrompt) {
        rect(0,0,SW,SH,Color{3,7,14,194});
        const size_t tileCount=desks[static_cast<size_t>(deskIndex)].tiles.size();
        softCard(360,242,560,240,Color{24,27,38,255},Color{255,99,112,100});
        text("DELETE WORKSPACE?",405,270,3,WHITE,890);
        text(desks[static_cast<size_t>(deskIndex)].name,405,314,2,MUTED,890);
        text(std::to_string(tileCount)+" SHORTCUTS WILL BE REMOVED; INSTALLED APPS STAY INSTALLED",
             405,346,1,MUTED,890);
        roundRect(405,382,210,48,12,Color{112,38,42,255});
        outline(405,382,210,48,Color{255,69,58,130});
        text("DELETE",466,399,2,WHITE,600);
        roundRect(635,382,240,48,12,Color{54,57,66,255});
        text("CANCEL",706,399,2,WHITE,865);
        text("A CONFIRM     B CANCEL",405,444,1,MUTED,890);
    }
    // PowerMenu modal (ТЗ 6.1/6.2: "-" opens the power menu; PWR-03).
    if(recentOpen) {
        rect(0,0,SW,SH,Color{3,7,14,190});
        softCard(230,78,820,544,Color{17,24,36,255},Color{255,255,255,25});
        text("LAUNCH HISTORY",266,104,3,WHITE,850);
        roundRect(838,108,78,30,9,Color{49,52,61,255});
        text("B BACK",850,118,1,WHITE,912);
        roundRect(924,108,88,30,9,Color{10,132,255,255});
        text("LAUNCH",936,118,1,WHITE,1006);
        text("UP / DOWN SCROLL     A RE-LAUNCH     B CLOSE",282,153,1,MUTED,850);
        if(recentLaunches.empty()) {
            text("NO LAUNCH HISTORY YET",310,245,2,WHITE,970);
            text("LAUNCH A GAME OR HOMEBREW APP TO BUILD YOUR HISTORY",310,280,1,MUTED,970);
        } else {
            recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(recentLaunches.size())-1));
            constexpr int rowsVisible=10;
            const int maxOffset=std::max(0,static_cast<int>(recentLaunches.size())-rowsVisible);
            recentOffset=std::max(0,std::min(recentOffset,maxOffset));
            if(recentIndex<recentOffset)recentOffset=recentIndex;
            if(recentIndex>=recentOffset+rowsVisible)recentOffset=recentIndex-rowsVisible+1;
            for(int row=0;row<rowsVisible;++row) {
                const int index=recentOffset+row;
                if(index>=static_cast<int>(recentLaunches.size()))break;
                const auto& entry=recentLaunches[static_cast<size_t>(index)];
                const int yy=190+row*39;
                if(index==recentIndex)roundRect(278,yy,724,35,9,Color{45,61,82,255});
                Tile iconTile{entry.kind,entry.title,entry.data,0,0,1,1,index%12};
                drawTileIcon(iconTile,290,yy+4,27);
                text(entry.title,330,yy+8,2,index==recentIndex?WHITE:MUTED,850);
                const std::string meta=std::string(entry.kind==Kind::Game?"GAME":"HOMEBREW")+
                    "  /  "+historyTimestampText(entry.timestamp,true);
                text(meta,866,yy+11,1,index==recentIndex?BLUE:MUTED,994);
            }
        }
        text("A  LAUNCH SELECTED     B  CLOSE",282,594,1,BLUE,850);
    }
    if(systemMenu) {
        rect(0,0,SW,SH,Color{3,7,14,190});
        softCard(110,104,1060,512,Color{17,24,36,255},Color{255,255,255,25});
        text("TOOLS",146,130,3,WHITE,1020);
        text("Library, recent apps and real console controls.",
             148,168,1,MUTED,1020);
        struct SystemTool { int x,y,icon; const char* title; const char* subtitle; };
        const SystemTool tools[]={
            {156,218,4,"APP LIBRARY","INSTALLED GAMES AND HOMEBREW"},
            {496,218,6,"LAUNCH HISTORY","RECENT GAME AND HOMEBREW STARTS"},
            {836,218,3,"POWER","SLEEP, REBOOT OR SHUT DOWN"},
            {156,382,5,"EDIT MODE","MOVE AND RESIZE DESKTOP TILES"},
            {496,382,0,"CONSOLE SETTINGS","WI-FI, BLUETOOTH, AIRPLANE MODE, BRIGHTNESS"},
            {836,382,4,"RESCAN LIBRARY","REFRESH INSTALLED APPLICATIONS"}
        };
        for(size_t toolIndex=0;toolIndex<sizeof(tools)/sizeof(tools[0]);++toolIndex) {
            const auto& item=tools[toolIndex];
            softCard(item.x,item.y,288,136,systemMenuIndex==static_cast<int>(toolIndex)?
                Color{34,63,98,255}:Color{23,32,46,255},
                systemMenuIndex==static_cast<int>(toolIndex)?
                Color{91,157,238,115}:Color{255,255,255,18});
            roundRect(item.x+14,item.y+14,54,54,16,
                systemMenuIndex==static_cast<int>(toolIndex)?
                Color{54,108,171,255}:Color{38,51,71,255});
            drawSystemIcon(item.icon,item.x+25,item.y+25,32,WHITE);
            text(item.title,item.x+82,item.y+23,2,WHITE,item.x+278);
            text(item.subtitle,item.x+16,item.y+91,1,MUTED,item.x+278);
        }
        roundRect(1058,136,82,34,10,Color{54,57,66,255});
        text("B BACK",1074,147,1,WHITE,1130);
        text("L3 OPEN TOOLS     A SELECT     TOUCH ANY CARD",148,568,1,BLUE,1120);
    }
    if(consoleSettingsOpen) {
        rect(0,0,SW,SH,Color{3,7,14,204});
        softCard(350,126,580,470,Color{17,24,36,248},Color{255,255,255,32});
        text("CONSOLE SETTINGS",394,154,3,WHITE,900);
        text("Switch hardware controls",396,190,1,MUTED,900);
        roundRect(812,151,78,32,9,Color{48,57,72,255});
        text("B BACK",826,162,1,WHITE,888);
        const char* labels[]={"WI-FI","BLUETOOTH","AIRPLANE MODE","SCREEN BRIGHTNESS","PAIR CONTROLLERS"};
        const std::string values[]={
            consoleWifiEnabled?"ON":"OFF",
            consoleBluetoothEnabled?"ON":"OFF",
            consoleAirplaneMode?"ON":"OFF",
            std::to_string(static_cast<int>(consoleBrightness*100.0f))+"%",
            "OPEN"
        };
        for(int i=0;i<5;++i) {
            const int yy=224+i*58;
            if(i==consoleSettingsIndex)roundRect(392,yy,496,48,11,Color{42,67,98,255});
            const bool on=(i==0?consoleWifiEnabled:i==1?consoleBluetoothEnabled:i==2?!consoleAirplaneMode:true);
            text(labels[i],414,yy+17,1,i==consoleSettingsIndex?WHITE:MUTED,700);
            text(values[i],750,yy+17,1,i==consoleSettingsIndex?BLUE:(on?GREEN:MUTED),870);
        }
        text("A TOGGLE / ADJUST     LEFT / RIGHT BRIGHTNESS     B BACK",396,548,1,BLUE,900);
    }
    if(appSettingsMenu) {
        rect(0,0,SW,SH,Color{3,7,14,198});
        softCard(300,104,680,520,Color{17,24,36,255},Color{255,255,255,28});
        text("LAUNCHER SETTINGS",344,130,3,WHITE,930);
        text("Appearance, widgets and SteamGridDB integration",346,168,1,MUTED,930);
        roundRect(842,132,92,32,10,Color{54,57,66,255});
        text("B  BACK",856,143,1,WHITE,926);
        text("WALLPAPER  "+(wallpaperPath.empty()?std::string("DEFAULT"):std::string("CUSTOM")),
             346,190,1,wallpaperPath.empty()?MUTED:BLUE,630);
        const size_t fontSlash=fontPath.find_last_of('/');
        const std::string fontLabel=fontPath.empty()?std::string("SYSTEM"):
            (fontSlash==std::string::npos?fontPath:fontPath.substr(fontSlash+1));
        text("FONT  "+fontLabel,646,190,1,fontPath.empty()?MUTED:BLUE,930);
        text("ICONS  "+(iconPackPath.empty()?std::string("DEFAULT"):std::string("CUSTOM")),
             346,202,1,iconPackPath.empty()?MUTED:BLUE,930);
        const char* actions[]={"CHOOSE BACKGROUND IMAGE","CHOOSE CUSTOM FONT","CHOOSE ICON PACK",
            "CHANGE COLOR PALETTE","ADD WIDGET TO WORKSPACE","RESCAN GAME LIBRARY",
            "WORKSPACE MANAGEMENT","TOGGLE EDIT / ARRANGE MODE","RESET APPEARANCE",
            languageRussian?"LANGUAGE: RUSSIAN":"LANGUAGE: ENGLISH","ICON STYLE","VISUAL THEME",
            "STEAMGRIDDB API KEY"};
        const char* iconStyles[]={"Classic","Tabler outline","Material filled"};
        for(int i=0;i<13;++i) {
            const int yy=214+i*26;
            if(i==appSettingsIndex)roundRect(340,yy,600,24,8,Color{35,61,93,255});
            text(actions[i],362,yy+6,1,i==appSettingsIndex?WHITE:MUTED,730);
            if(i==3)text("PALETTE "+std::to_string(themeIndex+1)+"/8",790,yy+6,1,BLUE,920);
            if(i==10)text(iconStyles[static_cast<size_t>(iconStyle)],755,yy+6,1,BLUE,920);
            if(i==11)text(useSocomaTheme()?"SOCOMA":"V0.1",790,yy+6,1,BLUE,920);
            if(i==12)text(steamGridDbApiKey.empty()?"NOT SET":"CONFIGURED",
                          790,yy+6,1,steamGridDbApiKey.empty()?MUTED:GREEN,920);
        }
        char nsInfo[80]{};
        if(!catalogReadyNow&&!catalogCompleteNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY INITIALIZING");
        else if(!catalogueNsAvailableNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"GAME SERVICE OFFLINE  0X%08X",
                          static_cast<unsigned int>(catalogueNsErrorNow));
        else std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY READY");
        text(nsInfo,346,574,1,catalogueNsAvailableNow?GREEN:RED,650);
        text("MEDIA LIBRARY  •  WALLPAPERS / FONTS / ICON PACKS",
             346,592,1,MUTED,930);
        text("UP / DOWN SELECT     A APPLY     B CLOSE",346,610,1,BLUE,930);
    }
    if(mediaPickerOpen) {
        rect(0,0,SW,SH,Color{3,7,14,205});
        softCard(220,58,840,604,Color{17,24,36,255},Color{255,255,255,25});
        text(mediaPickerType==MediaPickerType::Wallpaper?"CHOOSE WALLPAPER":
             mediaPickerType==MediaPickerType::WorkspaceBackground?"CHOOSE DESKTOP BACKGROUND":
             mediaPickerType==MediaPickerType::Font?"CHOOSE FONT":"CHOOSE ICON PACK",
             264,88,3,WHITE,1000);
        const bool browsingAlbum=mediaPickerDirectory.compare(
            0,std::string(ALBUM_ROOT).size(),ALBUM_ROOT)==0;
        text(browsingAlbum?"NINTENDO ALBUM":"MEDIA LIBRARY",266,128,1,MUTED,1000);
        if(mediaPickerType==MediaPickerType::WorkspaceBackground) {
            roundRect(782,86,132,34,10,Color{38,76,119,255});
            text(browsingAlbum?"MEDIA":"ALBUM",808,98,1,WHITE,908);
        }
        roundRect(930,86,92,34,10,Color{54,57,66,255});
        text("B  BACK",944,98,1,WHITE,1012);
        if(mediaPickerEntries.empty()) {
            text("NO SUPPORTED FILES FOUND",286,220,2,WHITE,980);
            text(mediaPickerType==MediaPickerType::IconPack
                 ?"CREATE A FOLDER HERE FOR YOUR ICON PACK"
                 :"COPY JPG / PNG OR TTF / OTF FILES INTO THIS FOLDER",
                 286,256,1,MUTED,1000);
        }
        for(int row=0;row<12;++row) {
            const int index=mediaPickerOffset+row;
            if(index>=static_cast<int>(mediaPickerEntries.size()))break;
            const auto& entry=mediaPickerEntries[static_cast<size_t>(index)];
            const int yy=166+row*34;
            if(index==mediaPickerIndex)roundRect(260,yy,760,30,8,Color{35,61,93,255});
            const char* entryType=entry.directory?"FOLDER":
                (mediaPickerType==MediaPickerType::Font?"FONT":"IMAGE");
            text(entryType,278,yy+10,1,entry.directory?BLUE:MUTED,370);
            text(entry.name,388,yy+10,1,index==mediaPickerIndex?WHITE:MUTED,1004);
        }
        text(std::to_string(mediaPickerIndex+1)+" / "+std::to_string(mediaPickerEntries.size())+
             "    A OPEN / APPLY",266,590,1,MUTED,650);
        text(mediaPickerType==MediaPickerType::WorkspaceBackground
             ?"UP/DOWN SELECT   A OPEN/APPLY   X ALBUM   B BACK"
             :"UP / DOWN SELECT     B BACK",
             682,590,1,BLUE,1018);
    }
    if(powerMenu) {
        static const char* kPowerItems[]={"SLEEP","REBOOT","SHUTDOWN"};
        rect(0,0,SW,SH,Color{3,7,14,194});
        softCard(410,210,460,300,Color{18,25,37,255},Color{255,255,255,24});
        text("POWER",454,238,3,WHITE,820);
        roundRect(780,238,52,30,9,Color{54,57,66,255});
        text("B",798,247,1,WHITE,828);
        for(int i=0;i<3;++i){
            int yy=294+i*44;
            if(i==powerIndex)roundRect(442,yy-5,396,38,9,Color{45,61,82,255});
            text(kPowerItems[i],460,yy+2,2,i==powerIndex?WHITE:MUTED,810);
        }
        roundRect(590,432,248,42,11,Color{10,132,255,255});
        text("EXECUTE",674,445,2,WHITE,820);
        text("SELECT AN ACTION FIRST",440,478,1,MUTED,820);
    }
    if(userPickerOpen) {
        rect(0,0,SW,SH,Color{3,7,14,210});
        softCard(330,116,620,508,Color{17,24,36,255},Color{255,255,255,32});
        text("SELECT PLAYER",374,142,3,WHITE,900);
        text("The selected profile is used for games that request a player account.",
             376,180,1,MUTED,914);
        roundRect(844,136,76,34,10,Color{54,57,66,255});
        text("B BACK",856,147,1,WHITE,912);
        if(consoleUsers.empty()) {
            text("NO NINTENDO ACCOUNT PROFILES FOUND",382,268,2,WHITE,906);
            text("Create a user in System Settings, then reopen this menu.",
                 382,306,1,MUTED,906);
        } else {
            for(size_t i=0;i<consoleUsers.size();++i) {
                const int yy=214+static_cast<int>(i)*46;
                const bool active=uiHasSelectedUser&&
                    sameAccountUid(consoleUsers[i].uid,uiSelectedUser);
                if(static_cast<int>(i)==userPickerIndex)
                    roundRect(370,yy,540,40,10,Color{42,74,111,255});
                else if(active)
                    roundRect(370,yy,540,40,10,Color{28,47,66,255});
                drawSystemIcon(7,386,yy+8,24,active?BLUE:MUTED);
                text(consoleUsers[i].nickname,424,yy+13,2,
                     static_cast<int>(i)==userPickerIndex?WHITE:MUTED,790);
                if(active)text("ACTIVE",816,yy+14,1,GREEN,898);
            }
        }
        text("UP / DOWN SELECT     A USE PROFILE     B CLOSE",374,588,1,BLUE,914);
    }
    SDL_RenderPresent(renderer);
}
bool saveNow(){return enqueueLayoutSave(false);}
bool finishTileDrag(const char* message="POSITION SAVED") {
    dragging=false;
    touchDragging=false;
    touchPressActive=false;
    touchPressFingerId=0;
    touchDragOffsetX=touchDragOffsetY=0;
    touchDragTabStart=-1;
    const bool saved=saveNow();
    if(saved&&message&&*message)setStatus(message);
    return saved;
}
void updateCursor(int dx,int dy){selectedX=std::max(0,std::min(COLS-1,selectedX+dx));selectedY=std::max(0,std::min(ROWS-1,selectedY+dy));}
bool moveTile(int dx,int dy) {
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(!desknx::workspace_ops::moveSelectedTile(d,selectedX,selectedY,dx,dy)) {
        if(itemAt(d,selectedX,selectedY)>=0)
            setStatus("MOVE BLOCKED: CELL OCCUPIED");
        return false;
    }
    layoutUserModified=true;
    return true;
}
bool moveToDesk(int target) {
    if(target<0||target>=static_cast<int>(desks.size())||target==deskIndex)return false;
    const std::string targetName=desks[static_cast<size_t>(target)].name;
    if(!desknx::workspace_ops::moveSelectedTileToWorkspace(
           desks,deskIndex,selectedX,selectedY,target)) {
        setStatus("TARGET WORKSPACE IS FULL");
        return false;
    }
    layoutUserModified=true;
    setStatus("MOVED TO "+targetName);
    return true;
}
bool moveAcrossDesk(int step) {
    if(desks.size()<2)return false;
    const int next=(deskIndex+step+static_cast<int>(desks.size()))%static_cast<int>(desks.size());
    return moveToDesk(next);
}
void resizeTile(bool bigger) {
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(!desknx::workspace_ops::resizeSelectedTile(d,selectedX,selectedY,bigger)) {
        setStatus(itemAt(d,selectedX,selectedY)<0?
                  "SELECT A TILE FIRST":"RESIZE BLOCKED OR SIZE LIMIT REACHED");
        return;
    }
    layoutUserModified=true;
    saveNow();
}
void removeTile() {
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    const int i=itemAt(d,selectedX,selectedY);
    if(i<0){setStatus("EMPTY CELL");return;}
    const std::string name=d.tiles[static_cast<size_t>(i)].title;
    d.tiles.erase(d.tiles.begin()+i);
    layoutUserModified=true;
    dragging=false;
    touchDragging=false;
    touchPressActive=false;
    touchPressFingerId=0;
    touchDragTabStart=-1;
    touchDragOffsetX=touchDragOffsetY=0;
    if(saveNow())setStatus("REMOVED: "+name);
}
void closeList();
void addSelected() {
    const auto& available=visibleCatalogueSnapshot();bool ready=false,complete=false,truncated=false;
    readCatalogueState(ready,complete,truncated);
    if(available.empty()){setStatus(complete?"NO APPS FOUND":"CATALOGUE IS STILL SCANNING",2200);return;}
    if(listIndex<0||static_cast<size_t>(listIndex)>=available.size())listIndex=0;
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    const Candidate& c=available[static_cast<size_t>(listIndex)];
    for(const auto& existing:d.tiles) {
        if(existing.kind==c.kind && existing.data==c.data) {
            // Focus the existing shortcut instead of jumping to an unrelated
            // empty cell. Close the library so the user can immediately see it.
            selectedX=existing.x;
            selectedY=existing.y;
            editing=true;
            closeList();
            lastCatalogueTap=-1;
            setStatus("ALREADY ON THIS WORKSPACE: "+c.title,2200);
            return;
        }
    }
    Tile t{c.kind,c.title,c.data,selectedX,selectedY,c.w,c.h,c.color};
    if(!placeable(d,t,t.x,t.y) && !firstFree(d,t,t.x,t.y)) {
        setStatus("WORKSPACE FULL - REMOVE A TILE BEFORE ADDING APPS",3500);
        return;
    }
    d.tiles.push_back(t);editing=true;
    layoutUserModified=true;
    std::string addStatus;
    Tile placeholder{Kind::Game,"","",0,0,1,1,0};
    int freeX=0,freeY=0;
    if(firstFree(d,placeholder,freeX,freeY)) {
        selectedX=freeX;selectedY=freeY;
        addStatus="ADDED: "+t.title+"  -  SELECT ANOTHER OR PRESS B";
    } else {
        selectedX=t.x;selectedY=t.y;closeList();
        addStatus="DESKTOP FULL - ITEM ADDED";
    }
    if(saveNow())setStatus(addStatus,2600);
}
int playStatsThreadMain(void* context) {
    const char* title=static_cast<const char*>(context);
    PdmQuerySnapshot snapshot{};
    if(title)std::snprintf(snapshot.titleId,sizeof(snapshot.titleId),"%s",title);
    const u64 titleId=title?std::strtoull(title,nullptr,16):0;
    std::free(context);
    PdmPlayStatistics stats{};
    const Result initRc=pdmqryInitialize();
    Result queryRc=initRc;
    if(titleId!=0&&R_SUCCEEDED(initRc)) {
        queryRc=pdmqryQueryPlayStatisticsByApplicationId(titleId,false,&stats);
        pdmqryExit();
    }
    snapshot.initRc=static_cast<uint32_t>(initRc);
    snapshot.queryRc=static_cast<uint32_t>(queryRc);
    snapshot.available=titleId!=0&&R_SUCCEEDED(queryRc);
    if(snapshot.available) {
        snapshot.playtimeSeconds=stats.playtime/1000000000ULL;
        snapshot.lastPlayed=stats.last_timestamp_user;
        snapshot.launchCount=stats.total_launches;
    }
    if(playStatsMutex)SDL_LockMutex(playStatsMutex);
    playStatsResult=snapshot;
    if(playStatsMutex)SDL_UnlockMutex(playStatsMutex);
    SDL_AtomicSet(&playStatsDone,1);
    return 0;
}
void applyPlayStatsResult(const PdmQuerySnapshot& snapshot) {
    const std::string id=snapshot.titleId;
    if(id.empty())return;
    if(catalogueMutex)SDL_LockMutex(catalogueMutex);
    auto update=[&](Candidate& item) {
        if(item.kind!=Kind::Game||item.data!=id)return;
        item.playStatsQueried=true;
        item.playStatsAvailable=snapshot.available;
        if(snapshot.available) {
            item.playtimeSeconds=snapshot.playtimeSeconds;
            item.lastPlayed=snapshot.lastPlayed;
            item.launchCount=snapshot.launchCount;
        }
    };
    for(auto& item:catalogue)update(item);
    if(catalogueMutex)SDL_UnlockMutex(catalogueMutex);
    for(auto& item:visibleCatalogueCache)update(item);
    desknx::diag::Write("catalogue","play_stats",
        "title_id=%s init_rc=0x%08X query_rc=0x%08X available=%d",
        snapshot.titleId,snapshot.initRc,snapshot.queryRc,snapshot.available?1:0);
}
void loadPlayStatsForSelection() {
    const auto& available=visibleCatalogueSnapshot();
    if(listIndex<0||static_cast<size_t>(listIndex)>=available.size())return;
    const Candidate& selected=available[static_cast<size_t>(listIndex)];
    if(selected.kind!=Kind::Game||selected.playStatsQueried||
       !desknx::model::isTitleId(selected.data))return;
    // Debounce selection; the UI never waits for PDM IPC during navigation.
    pendingPlayStatsTitle=selected.data;
    pendingPlayStatsDue=SDL_GetTicks()+300;
    playStatsPending=true;
}
void pumpPlayStatsQuery() {
    if(playStatsThread&&SDL_AtomicGet(&playStatsDone)) {
        SDL_WaitThread(playStatsThread,nullptr);
        playStatsThread=nullptr;
        PdmQuerySnapshot result{};
        if(playStatsMutex)SDL_LockMutex(playStatsMutex);
        result=playStatsResult;
        if(playStatsMutex)SDL_UnlockMutex(playStatsMutex);
        applyPlayStatsResult(result);
    }
    if(playStatsThread||!playStatsPending||
       SDL_GetTicks()<pendingPlayStatsDue||pendingPlayStatsTitle.empty())return;
    char* request=static_cast<char*>(std::malloc(17));
    if(!request) {playStatsPending=false;return;}
    std::snprintf(request,17,"%s",pendingPlayStatsTitle.c_str());
    playStatsPending=false;
    SDL_AtomicSet(&playStatsDone,0);
    playStatsThread=SDL_CreateThread(playStatsThreadMain,"DeskNX play statistics",request);
    if(!playStatsThread) {
        std::free(request);
        setStatus("PLAY STATISTICS UNAVAILABLE",1800);
    }
}
void openList(){
    // Opening the catalogue is also useful when a workspace is full: the user
    // can inspect/filter the library before deciding what to remove. addSelected
    // performs the definitive capacity check when an app is actually added.
    listOpen=true;
    listIndex=0;
    listOffset=0;
    lastCatalogueTap=-1;
    loadPlayStatsForSelection();
}
void closeList() {
    listOpen=false;
    catalogueTouchActive=false;
    catalogueTouchMoved=false;
    catalogueTouchFingerId=0;
    if(widgetPickerMode) {
        catalogueFilter=widgetPickerPreviousFilter;
        catalogueSort=widgetPickerPreviousSort;
        catalogueQuery=widgetPickerPreviousQuery;
        widgetPickerMode=false;
        ++catalogueViewRevision;
        listIndex=0;
        listOffset=0;
    }
}
void newDesk() {
    if(dragging || touchDragging) {
        setStatus("DROP THE MOVING TILE BEFORE CREATING A WORKSPACE");
        return;
    }
    if(desks.size()>=MAX_DESKS){setStatus("MAXIMUM OF 12 WORKSPACES");return;}
    int suffix=1;
    std::string name;
    bool exists=true;
    while(exists) {
        name="Workspace "+std::to_string(suffix++);
        exists=false;
        for(const auto& desk:desks) if(desk.name==name){exists=true;break;}
    }
    desks.push_back(Workspace{name,{}});deskIndex=static_cast<int>(desks.size())-1;
    layoutUserModified=true;
    selectedX=selectedY=0;editing=true;
    if(saveNow())setStatus("NEW WORKSPACE CREATED - PRESS Y TO ADD ITEMS");
}
void renameDesk() {
    if(dragging || touchDragging) {
        setStatus("DROP THE MOVING TILE BEFORE RENAMING");
        return;
    }
    const Uint64 keyboardStarted=SDL_GetTicks64();
    SwkbdConfig kbd{};
    const Result keyboardCreateRc=swkbdCreate(&kbd,0);
    desknx::diag::Write("keyboard","rename_create","rc=0x%08X elapsed_ms=%llu",
        static_cast<unsigned int>(keyboardCreateRc),static_cast<unsigned long long>(SDL_GetTicks64()-keyboardStarted));
    if(R_FAILED(keyboardCreateRc)){setStatus("KEYBOARD UNAVAILABLE");return;}
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd,languageRussian?"Переименовать рабочий стол":"Rename workspace");
    swkbdConfigSetInitialText(&kbd,desks[static_cast<size_t>(deskIndex)].name.c_str());
    // DESKNX3 stores up to 31 UTF-8 codepoints (124 bytes). The larger
    // input buffer permits 31 four-byte characters without truncation.
    char result[128]{};
    const Result rc=showKeyboard(&kbd,result,sizeof(result));
    swkbdClose(&kbd);
    if(R_FAILED(rc)||!result[0])return;
    std::string name=result;
    const size_t first=name.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) {
        setStatus("WORKSPACE NAME CANNOT BE EMPTY");
        return;
    }
    const size_t last=name.find_last_not_of(" \t\r\n");
    name=name.substr(first,last-first+1);
    if(!desknx::workspace_ops::isValidWorkspaceName(name)) {
        setStatus("NAME LIMIT: 31 CHARACTERS / 124 UTF-8 BYTES");
        return;
    }
    for(size_t i=0;i<desks.size();++i) {
        if(i!=static_cast<size_t>(deskIndex)&&desks[i].name==name) {
            setStatus("WORKSPACE NAME ALREADY EXISTS");
            return;
        }
    }
    if(desks[static_cast<size_t>(deskIndex)].name==name)return;
    desks[static_cast<size_t>(deskIndex)].name=name;
    layoutUserModified=true;
    if(saveNow())setStatus("WORKSPACE RENAMED");
}
void deleteDesk() {
    if(dragging || touchDragging) {
        setStatus("DROP THE MOVING TILE BEFORE DELETING A WORKSPACE");
        return;
    }
    if(desks.size()<=1){setStatus("AT LEAST ONE WORKSPACE IS REQUIRED");return;}
    deletePrompt=true;
}
void openWorkspacePicker(bool moveTile) {
    if(moveTile) {
        const Workspace& current=desks[static_cast<size_t>(deskIndex)];
        if(!editing || itemAt(current,selectedX,selectedY)<0) {
            setStatus("SELECT A TILE IN EDIT MODE FIRST");
            return;
        }
    }
    workspacePickerMoveTile=moveTile;
    workspacePickerIndex=deskIndex;
    workspacePicker=true;
}
void selectWorkspace(int target) {
    if(target<0||target>=static_cast<int>(desks.size()))return;
    const std::string targetName=desks[static_cast<size_t>(target)].name;
    if(workspacePickerMoveTile) {
        if(target==deskIndex) {
            setStatus("CHOOSE A DIFFERENT WORKSPACE");
            return;
        }
        if(!moveToDesk(target))return;
        workspacePicker=false;
        if(saveNow())setStatus("MOVED TO "+targetName);
        return;
    }
    workspacePicker=false;
    if(target==deskIndex) {
        setStatus("ALREADY ON "+targetName);
        return;
    }
    deskIndex=target;
    scheduleActiveWorkspaceSave();
    setStatus(targetName);
}
bool sameAccountUid(const AccountUid& a,const AccountUid& b) {
    return a.uid[0]==b.uid[0]&&a.uid[1]==b.uid[1];
}

bool initializeAccountService() {
    if(accountServiceReady) return true;
#ifdef DESKNX_SYSTEM_APPLET
    const Result rc=accountInitialize(AccountServiceType_System);
#else
    const Result rc=accountInitialize(AccountServiceType_Application);
#endif
    accountServiceReady=R_SUCCEEDED(rc);
    desknx::diag::Write("account","service_init",
                        "rc=0x%08X ready=%d",
                        static_cast<unsigned int>(rc),accountServiceReady?1:0);
    return accountServiceReady;
}

void loadConsoleUsers() {
    consoleUsers.clear();
    if(!initializeAccountService()) {
        consoleUsersLoaded=false;
        selectedUserLabel="SELECT PLAYER";
        return;
    }
    consoleUsersLoaded=true;
    AccountUid users[8]{};
    s32 actual=0;
    const Result listRc=accountListAllUsers(users,8,&actual);
    if(R_FAILED(listRc)||actual<=0) {
        consoleUsersLoaded=false; // Retry only when the user opens the profile picker.
        selectedUserLabel="SELECT PLAYER";
        desknx::diag::Write("account","list_users_failed",
                            "rc=0x%08X count=%d",
                            static_cast<unsigned int>(listRc),actual);
        return;
    }
    const s32 count=std::min<s32>(actual,8);
    for(s32 i=0;i<count;++i) {
        if(!accountUidIsValid(&users[i])) continue;
        ConsoleUserProfile entry{};
        entry.uid=users[i];
        entry.nickname="Player "+std::to_string(i+1);
        AccountProfile profile{};
        if(R_SUCCEEDED(accountGetProfile(&profile,users[i]))) {
            AccountProfileBase base{};
            AccountUserData data{};
            if(R_SUCCEEDED(accountProfileGet(&profile,&data,&base))) {
                if(base.nickname[0]!='\0') entry.nickname=base.nickname;
            }
            accountProfileClose(&profile);
        }
        consoleUsers.push_back(std::move(entry));
    }
#ifdef DESKNX_SYSTEM_APPLET
    if(g_backendHasUser&&accountUidIsValid(&g_backendUser)) {
        uiSelectedUser=g_backendUser;
        uiHasSelectedUser=true;
    }
#endif
    if(!uiHasSelectedUser) {
        AccountUid preselected{};
        if(R_SUCCEEDED(accountGetPreselectedUser(&preselected))&&
           accountUidIsValid(&preselected)) {
            uiSelectedUser=preselected;
            uiHasSelectedUser=true;
        } else if(!consoleUsers.empty()) {
            uiSelectedUser=consoleUsers.front().uid;
            uiHasSelectedUser=true;
        }
    }
#ifdef DESKNX_SYSTEM_APPLET
    if(!g_backendHasUser&&uiHasSelectedUser) {
        g_backendUser=uiSelectedUser;
        g_backendHasUser=true;
    }
#endif
    selectedUserLabel="SELECT PLAYER";
    for(const auto& user:consoleUsers) {
        if(uiHasSelectedUser&&sameAccountUid(user.uid,uiSelectedUser)) {
            selectedUserLabel=user.nickname;
            break;
        }
    }
    desknx::diag::Write("account","profiles_loaded","count=%zu active=%s",
                        consoleUsers.size(),selectedUserLabel.c_str());
}

void selectConsoleUser(int index) {
    if(index<0||index>=static_cast<int>(consoleUsers.size())) return;
    const ConsoleUserProfile selected=consoleUsers[static_cast<size_t>(index)];
    if(!accountUidIsValid(&selected.uid)) {
        setStatus("INVALID USER PROFILE",3000);
        return;
    }
#ifdef DESKNX_SYSTEM_APPLET
    const Result rc=sendIpc([&]{return desknx::ipc::SendSelectUser(selected.uid);});
    if(R_FAILED(rc)) {
        setStatus("USER SELECTION FAILED",3500);
        desknx::diag::Write("account","select_user_failed","rc=0x%08X",
                            static_cast<unsigned int>(rc));
        return;
    }
    g_backendUser=selected.uid;
    g_backendHasUser=true;
#endif
    uiSelectedUser=selected.uid;
    uiHasSelectedUser=true;
    selectedUserLabel=selected.nickname;
    userPickerOpen=false;
    setStatus("PLAYER: "+selected.nickname,3000);
    desknx::diag::Write("account","selected_user","profile=%s",
                        selected.nickname.c_str());
}

void openUserProfile() {
    if(!consoleUsersLoaded||!accountServiceReady) loadConsoleUsers();
    if(consoleUsers.empty()) {
        setStatus("ACCOUNT PROFILES UNAVAILABLE",3500);
        return;
    }
    userPickerIndex=0;
    if(uiHasSelectedUser) {
        for(size_t i=0;i<consoleUsers.size();++i) {
            if(sameAccountUid(consoleUsers[i].uid,uiSelectedUser)) {
                userPickerIndex=static_cast<int>(i);
                break;
            }
        }
    }
    userPickerOpen=true;
}
void activateWorkspaceAppearance(int action) {
    if(deskIndex<0||deskIndex>=static_cast<int>(desks.size()))return;
    Workspace& desk=desks[static_cast<size_t>(deskIndex)];
    switch(action) {
        case 0: openMediaPicker(MediaPickerType::WorkspaceBackground); return;
        case 1: desk.backgroundFit=(desk.backgroundFit+1)%3; break;
        case 2: desk.artworkMode=(desk.artworkMode+1)%3; break;
        case 3: {
            static const int steps[]={0,96,160,196,232};
            int next=0;
            for(int i=0;i<5;++i)if(steps[i]==desk.labelOpacity)next=(i+1)%5;
            desk.labelOpacity=steps[next];
            break;
        }
        case 4:
            desk.backgroundPath.clear();desk.backgroundFit=0;desk.artworkMode=0;
            desk.labelOpacity=196;desk.accentColor=-1;break;
        case 5: workspaceSettingsMenu=false;return;
        default:return;
    }
    layoutUserModified=true;
    if(saveNow())setStatus("DESKTOP APPEARANCE SAVED");
}

void activateAppSettings(int action) {
    appSettingsMenu=false;
    switch(action) {
        case 0: openMediaPicker(MediaPickerType::Wallpaper); break;
        case 1: openMediaPicker(MediaPickerType::Font); break;
        case 2: openMediaPicker(MediaPickerType::IconPack); break;
        case 3:
            themeIndex=(themeIndex+1)%8;
            applyThemePalette();
            if(saveLauncherSettings())
                setStatus("PALETTE "+std::to_string(themeIndex+1)+" APPLIED");
            break;
        case 4:
            widgetPickerPreviousFilter=catalogueFilter;
            widgetPickerPreviousSort=catalogueSort;
            widgetPickerPreviousQuery=catalogueQuery;
            widgetPickerMode=true;
            editing=true;
            catalogueFilter=desknx::catalog_view::FilterMode::Widgets;
            catalogueQuery.clear();
            ++catalogueViewRevision;
            openList();
            setStatus("CHOOSE A WIDGET TO ADD");
            break;
        case 5: restartCatalogueScan(); break;
        case 6:
            workspaceMenu=true;
            workspaceMenuIndex=0;
            break;
        case 7:
            editing=!editing;
            if(!editing&&dragging){dragging=false;saveNow();}
            setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            break;
        case 8:
            wallpaperPath.clear();fontPath.clear();iconPackPath.clear();themeIndex=1;visualThemeIndex=0;iconStyle=0;
            (void)loadWallpaperTexture("");
            (void)gFontRenderer.setCustomFont("");
            gIconCache.setCustomIconPackDir("");
            applyThemePalette();
            if(saveLauncherSettings())setStatus("APPEARANCE RESET");
            break;
        case 9:
            languageRussian=!languageRussian;
            if(saveLauncherSettings())setStatus(languageRussian?"ЯЗЫК: РУССКИЙ":"LANGUAGE: ENGLISH");
            break;
        case 10:
            iconStyle=(iconStyle+1)%3;
            if(saveLauncherSettings())setStatus("ICON STYLE");
            break;
        case 11:
            visualThemeIndex=useSocomaTheme()?0:1;
            applyThemePalette();
            desknx::diag::Write("ui","visual_theme_changed","theme=%s palette=%d",
                useSocomaTheme()?"socoma":"v0.1",themeIndex);
            if(saveLauncherSettings())setStatus(useSocomaTheme()?"SOCOMA THEME APPLIED":"V0.1 THEME RESTORED");
            break;
        case 12:
            editSteamGridDbApiKey();
            break;
        default: break;
    }
}
void launch(const Tile& t) {
    desknx::diag::Write("launch", "requested", "kind=%s",
        t.kind == Kind::Game ? "game" : t.kind == Kind::Nro ? "homebrew" : "widget");
    if (t.kind == Kind::Unsupported) {
        setStatus("UNSUPPORTED TILE TYPE: " + t.unknownTypeCode, 3500);
        return;
    }
    if(t.kind==Kind::Game) {
        if(!desknx::model::isTitleId(t.data)){setStatus("INVALID TITLE ID");return;}
        const u64 id=std::strtoull(t.data.c_str(),nullptr,16);
        if(id==0){setStatus("INVALID TITLE ID");return;}
#ifdef DESKNX_SYSTEM_APPLET
        // Persist the user's workspace before sending a launch request that
        // closes this HOME applet. Never discard unsaved edits on the way out.
        if(!saveLayout())return;
        // Use the profile selected in the launcher. Respect NACP's explicit
        // "no user" value; unknown metadata keeps the safe legacy behavior and
        // receives the selected profile.
        const AccountUid uid=(g_backendHasUser&&accountUidIsValid(&g_backendUser))
            ?g_backendUser:(uiHasSelectedUser?uiSelectedUser:AccountUid{});
        NsApplicationControlData launchControl{};
        u64 launchControlSize=0;
        const Result launchControlRc=desknx::slaunch::ReadControlData(
            id,launchControl,launchControlSize);
        const bool hasChosenUser=accountUidIsValid(&uid);
        // The profile selected in the top bar is authoritative. Never request
        // an additional user chooser because an individual game's NACP asks for it.
        const u64 flags=hasChosenUser?desknx::ipc::LaunchFlagHasUser:0;
        desknx::diag::Write("launch","player_policy",
            "title_id=%016llX policy=header_selected_profile nacp_rc=0x%08X has_user=%d flags=0x%llX",
            static_cast<unsigned long long>(id),
            static_cast<unsigned int>(launchControlRc),
            hasChosenUser?1:0,static_cast<unsigned long long>(flags));
        desknx::diag::Write("launch", "game_ipc_begin", "title_id=%016llX",
                            static_cast<unsigned long long>(id));
        const Result rc=sendIpc([&]{return desknx::ipc::SendLaunchTitle(id,flags,uid);});
        desknx::diag::Write("launch", "game_ipc_end", "rc=0x%08X",
                            static_cast<unsigned int>(rc));
        if(R_FAILED(rc)) {
            char b[72]{};
            std::snprintf(b,sizeof(b),"SYSTEM BACKEND REQUEST FAILED: 0X%08X",static_cast<unsigned int>(rc));
            setStatus(b,4000);
            return;
        }
        recordRecentLaunch(t);
        appletRequestExitToSelf();
        running=false;
#else
        // Do not leave the process while a queued workspace snapshot is still
        // being written. This is a deliberate launch-time flush, not frame I/O.
        if(activeWorkspaceMarkerPending) (void)flushActiveWorkspaceSave();
        drainAsyncLayoutSaver();
        // NS was initialized once during launcher startup and is shared with the
        // catalogue worker. Do not initialize/exit the process-wide service
        // around a single launch request: doing so can invalidate the worker's
        // live session if the request is rejected and the launcher stays open.
        desknx::diag::Write("launch", "applet_launch_request_begin",
            "title_id=%016llX applet_type=%d user_policy=system_preselected",
            static_cast<unsigned long long>(id),
            static_cast<int>(appletGetAppletType()));
        // The public libnx API accepts an optional AppletStorage here, not an
        // AccountUid. User selection is only available through the HOME/system
        // backend; standalone applications use Horizon's preselected user.
        const Result rc=appletRequestLaunchApplication(id,nullptr);
        desknx::diag::Write("launch", "applet_launch_request_end",
            "title_id=%016llX rc=0x%08X",
            static_cast<unsigned long long>(id),static_cast<unsigned int>(rc));
        if(R_FAILED(rc)) {
            char b[64]{};
            std::snprintf(b,sizeof(b),"GAME LAUNCH FAILED: 0X%08X",
                          static_cast<unsigned int>(rc));
            setStatus(b,4000);
        } else {
            recordRecentLaunch(t);
            setStatus("LAUNCH REQUEST ACCEPTED");
        }
#endif
        return;
    }
    if(t.kind==Kind::Nro) {
        if(!isSafeNroPath(t.data)){setStatus("INVALID OR MISSING NRO PATH",3500);return;}
#ifdef DESKNX_SYSTEM_APPLET
        // Persist workspace changes before leaving HOME for the backend loader.
        if(!saveLayout())return;
        desknx::diag::Write("launch", "homebrew_ipc_begin", "path_valid=1");
        const Result rc=sendIpc([&]{return desknx::ipc::SendLaunchHomebrew(t.data.c_str(),"");});
        desknx::diag::Write("launch", "homebrew_ipc_end", "rc=0x%08X",
                            static_cast<unsigned int>(rc));
        if(R_FAILED(rc)) {
            char b[72]{};
            std::snprintf(b,sizeof(b),"HOMEBREW REQUEST FAILED: 0X%08X",static_cast<unsigned int>(rc));
            setStatus(b,4000);
            return;
        }
        recordRecentLaunch(t);
        appletRequestExitToSelf();
        running=false;
#else
        if(activeWorkspaceMarkerPending) (void)flushActiveWorkspaceSave();
        drainAsyncLayoutSaver();
        if(!envHasNextLoad()){
            desknx::diag::Write("launch", "homebrew_rejected", "reason=next_load_unavailable");
            setStatus("NRO CHAIN-LOAD UNAVAILABLE; USE HBMENU TAKEOVER",4500);return;
        }
        const Result nextLoadRc = envSetNextLoad(t.data.c_str(),"");
        desknx::diag::Write("launch", "homebrew_next_load", "rc=0x%08X",
                            static_cast<unsigned int>(nextLoadRc));
        if(R_FAILED(nextLoadRc)){setStatus("CANNOT PREPARE NEXT NRO");return;}
        recordRecentLaunch(t);
        appletRequestExitToSelf();
#endif
        return;
    }
    if(t.kind==Kind::Battery)setStatus(battery>=0?"BATTERY: "+std::to_string(battery)+"%":"BATTERY STATUS UNAVAILABLE");
    else if(t.kind==Kind::Storage)setStatus("SD STORAGE WIDGET");
    else if(t.kind==Kind::Calendar)setStatus("CALENDAR WIDGET");
    else if(t.kind==Kind::System) {
        const u32 firmware=hosversionGet();
        char detail[64]{};
        std::snprintf(detail,sizeof(detail),"SYSTEM FIRMWARE %u.%u.%u",
            HOSVER_MAJOR(firmware),HOSVER_MINOR(firmware),HOSVER_MICRO(firmware));
        setStatus(detail,3500);
    }
    else if(t.kind==Kind::Clock)setStatus("CLOCK WIDGET");
    else if(t.kind==Kind::Recent) {
        if(recentLaunches.empty())setStatus("NO RECENT LAUNCHES YET");
        else {recentIndex=0;recentOpen=true;}
    } else setStatus(lastLaunched);
}
void keyInput(u64 keys, bool fromRepeat) {
    // A live touch gesture owns focus until its initiating finger is released.
    // This prevents controller buttons from closing the menu or mutating the
    // workspace midway through a touch selection/drag.
    if(touchPressActive||catalogueTouchActive)return;
    if(userPickerOpen) {
        if(keys&HidNpadButton_B){userPickerOpen=false;return;}
        if(!consoleUsers.empty()) {
            if(keys&HidNpadButton_Up)
                userPickerIndex=(userPickerIndex-1+static_cast<int>(consoleUsers.size()))%
                    static_cast<int>(consoleUsers.size());
            if(keys&HidNpadButton_Down)
                userPickerIndex=(userPickerIndex+1)%static_cast<int>(consoleUsers.size());
            if(keys&HidNpadButton_A)selectConsoleUser(userPickerIndex);
        }
        return;
    }
    if(mediaPickerOpen) {
        if(keys&HidNpadButton_B){mediaPickerBack();return;}
        if((keys&HidNpadButton_X)&&
           mediaPickerType==MediaPickerType::WorkspaceBackground) {
            mediaPickerDirectory=mediaPickerDirectory.compare(
                0,std::string(ALBUM_ROOT).size(),ALBUM_ROOT)==0
                ?std::string(MEDIA_ROOT):std::string(ALBUM_ROOT);
            mediaPickerIndex=mediaPickerOffset=0;
            refreshMediaPicker();
            return;
        }
        if(!mediaPickerEntries.empty()) {
            if(keys&HidNpadButton_Up) {
                mediaPickerIndex=(mediaPickerIndex-1+static_cast<int>(mediaPickerEntries.size()))%
                    static_cast<int>(mediaPickerEntries.size());
                if(mediaPickerIndex<mediaPickerOffset)mediaPickerOffset=mediaPickerIndex;
            }
            if(keys&HidNpadButton_Down) {
                mediaPickerIndex=(mediaPickerIndex+1)%static_cast<int>(mediaPickerEntries.size());
                if(mediaPickerIndex>=mediaPickerOffset+12)mediaPickerOffset=mediaPickerIndex-11;
            }
            if(keys&HidNpadButton_A)applyMediaPickerSelection(mediaPickerIndex);
        }
        return;
    }
    if(steamGridPickerOpen) {
        handleSteamGridPickerInput(keys);
        return;
    }
    if(consoleSettingsOpen) {
        if(keys&HidNpadButton_B){consoleSettingsOpen=false;return;}
        if(keys&HidNpadButton_Up)consoleSettingsIndex=(consoleSettingsIndex+4)%5;
        if(keys&HidNpadButton_Down)consoleSettingsIndex=(consoleSettingsIndex+1)%5;
        if(consoleSettingsIndex==3&&(keys&HidNpadButton_Left||keys&HidNpadButton_Right))
            adjustConsoleBrightness((keys&HidNpadButton_Right)?0.05f:-0.05f);
        if(keys&HidNpadButton_A)activateConsoleSetting(consoleSettingsIndex);
        return;
    }
    if(appSettingsMenu) {
        if(keys&HidNpadButton_B){appSettingsMenu=false;return;}
        if(keys&HidNpadButton_Up)appSettingsIndex=(appSettingsIndex+12)%13;
        if(keys&HidNpadButton_Down)appSettingsIndex=(appSettingsIndex+1)%13;
        if(keys&HidNpadButton_A)activateAppSettings(appSettingsIndex);
        return;
    }
    if(workspaceSettingsMenu) {
        if(keys&HidNpadButton_B){workspaceSettingsMenu=false;return;}
        if(keys&HidNpadButton_Up)workspaceSettingsIndex=(workspaceSettingsIndex+5)%6;
        if(keys&HidNpadButton_Down)workspaceSettingsIndex=(workspaceSettingsIndex+1)%6;
        if(keys&HidNpadButton_A)activateWorkspaceAppearance(workspaceSettingsIndex);
        return;
    }
    if(systemMenu) {
        if(keys&HidNpadButton_B){systemMenu=false;return;}
        if(systemMenuIndex<0||systemMenuIndex>5)systemMenuIndex=0;
        int row=systemMenuIndex/3;
        int column=systemMenuIndex%3;
        if(keys&HidNpadButton_Left)column=(column+2)%3;
        if(keys&HidNpadButton_Right)column=(column+1)%3;
        if(keys&HidNpadButton_Up||keys&HidNpadButton_Down)row=(row+1)%2;
        systemMenuIndex=row*3+column;
        if(keys&HidNpadButton_A) {
            const int action=systemMenuIndex;
            systemMenu=false;
            if(action==0)openList();
            else if(action==1){recentIndex=0;recentOffset=0;recentOpen=true;}
            else if(action==2){powerMenu=true;powerIndex=0;}
            else if(action==3) {
                editing=!editing;
                if(!editing&&dragging){dragging=false;saveNow();}
                setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            } else if(action==4) {
                consoleSettingsOpen=true;consoleSettingsIndex=0;refreshConsoleSettings();
            } else if(action==5)restartCatalogueScan();
        }
        return;
    }
    if(workspaceReorderOpen) {
        const int count=static_cast<int>(desks.size());
        if(keys&HidNpadButton_B){workspaceReorderOpen=false;return;}
        if(keys&HidNpadButton_Up&&count>0)
            workspaceReorderIndex=(workspaceReorderIndex-1+count)%count;
        if(keys&HidNpadButton_Down&&count>0)
            workspaceReorderIndex=(workspaceReorderIndex+1)%count;
        if(keys&HidNpadButton_A) {
            const int from=deskIndex,to=workspaceReorderIndex;
            workspaceReorderOpen=false;
            if(desknx::workspace_ops::reorderWorkspace(desks,deskIndex,from,to)) {
                layoutUserModified=true;
                scheduleActiveWorkspaceSave();
                if(saveNow())setStatus("WORKSPACES REORDERED");
            }
        }
        return;
    }
    if(workspacePicker) {
        const int count=static_cast<int>(desks.size());
        if(keys&HidNpadButton_B){workspacePicker=false;return;}
        if(count>0&&keys&HidNpadButton_Up)
            workspacePickerIndex=(workspacePickerIndex-1+count)%count;
        if(count>0&&keys&HidNpadButton_Down)
            workspacePickerIndex=(workspacePickerIndex+1)%count;
        if(keys&HidNpadButton_A)selectWorkspace(workspacePickerIndex);
        return;
    }
    if(workspaceMenu) {
        if(keys&HidNpadButton_B){workspaceMenu=false;return;}
        if(keys&HidNpadButton_Up)
            workspaceMenuIndex=(workspaceMenuIndex+6)%7;
        if(keys&HidNpadButton_Down)
            workspaceMenuIndex=(workspaceMenuIndex+1)%7;
        if(keys&HidNpadButton_A) {
            const int action=workspaceMenuIndex;
            workspaceMenu=false;
            if(action==0)openWorkspacePicker(false);
            else if(action==1)openWorkspacePicker(true);
            else if(action==2){workspaceSettingsMenu=true;workspaceSettingsIndex=0;}
            else if(action==3)renameDesk();
            else if(action==4)deleteDesk();
            else if(action==5){workspaceReorderOpen=true;workspaceReorderIndex=deskIndex;}
        }
        return;
    }
    if(recentOpen) {
        if(keys&HidNpadButton_B){recentOpen=false;return;}
        if(recentLaunches.empty())return;
        recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(recentLaunches.size())-1));
        if(keys&HidNpadButton_Up)
            recentIndex=(recentIndex-1+static_cast<int>(recentLaunches.size()))%static_cast<int>(recentLaunches.size());
        if(keys&HidNpadButton_Down)
            recentIndex=(recentIndex+1)%static_cast<int>(recentLaunches.size());
        constexpr int rowsVisible=10;
        if(recentIndex<recentOffset)recentOffset=recentIndex;
        if(recentIndex>=recentOffset+rowsVisible)recentOffset=recentIndex-rowsVisible+1;
        if(keys&HidNpadButton_A) {
            const HistoryEntry entry=recentLaunches[static_cast<size_t>(recentIndex)];
            recentOpen=false;
            launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
        }
        return;
    }
    if(powerMenu) {
        if(keys&HidNpadButton_B){powerMenu=false;return;}
        if(keys&HidNpadButton_Up)powerIndex=(powerIndex+2)%3;
        if(keys&HidNpadButton_Down)powerIndex=(powerIndex+1)%3;
        if(keys&HidNpadButton_A) {
            const auto kind=static_cast<desknx::ipc::PowerKind>(powerIndex);
            if(sdMounted) {
                if(activeWorkspaceMarkerPending) (void)flushActiveWorkspaceSave();
                drainAsyncLayoutSaver();
            }
#ifdef DESKNX_SYSTEM_APPLET
            const Result rc=sendIpc([&]{return desknx::ipc::SendPowerAction(kind);});
            powerMenu=false;
            if(R_FAILED(rc)){setStatus("SYSTEM BACKEND REQUEST FAILED",3000);return;}
            setStatus("POWER ACTION SENT TO SYSTEM",2500);
#else
            // Standalone: same semantics, executed locally. Sleep is refused
            // here on purpose: an NRO cannot keep the console asleep while it
            // is being killed by the sleep sequence (recovery principle 1).
            powerMenu=false;
            Result rc=-1;
            if(kind==desknx::ipc::PowerKind::Reboot) rc=appletRequestToReboot();
            else if(kind==desknx::ipc::PowerKind::Shutdown) rc=appletRequestToShutdown();
            else {setStatus("SLEEP NOT SUPPORTED HERE; PRESS THE POWER BUTTON",3000);return;}
            if(R_FAILED(rc)){char b[64]{};std::snprintf(b,sizeof(b),"POWER REQUEST FAILED: 0X%08X",static_cast<unsigned int>(rc));setStatus(b,4000);}
#endif
        }
        return;
    }
    if(deletePrompt) {
        if(keys&HidNpadButton_B){deletePrompt=false;return;}
        if(keys&HidNpadButton_A) {
            deletePrompt=false;
            if(deskIndex<0||deskIndex>=static_cast<int>(desks.size()))return;
            if(!desknx::workspace_ops::deleteWorkspace(desks,deskIndex,deskIndex)) {
                setStatus("AT LEAST ONE WORKSPACE IS REQUIRED");
                return;
            }
            layoutUserModified=true;
            selectedX=selectedY=0;
            dragging=false;touchDragging=false;touchPressActive=false;
            touchPressFingerId=0;
            touchDragOffsetX=touchDragOffsetY=0;
            touchDragTabStart=-1;
            if(saveNow())setStatus("WORKSPACE DELETED");
        }
        return;
    }
    if(listOpen) {
        const auto& available=visibleCatalogueSnapshot();
        if(keys&HidNpadButton_B){closeList();return;}
        if(keys&HidNpadButton_Y){searchCatalogue();return;}
        if(keys&HidNpadButton_X){cycleCatalogueSort();return;}
        if(keys&HidNpadButton_L){cycleCatalogueFilter(-1);return;}
        if(keys&HidNpadButton_R){cycleCatalogueFilter(1);return;}
        if(keys&HidNpadButton_Minus){catalogueQuery.clear();++catalogueViewRevision;listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);return;}
        if(keys&HidNpadButton_StickR){restartCatalogueScan();return;}
        if(keys&HidNpadButton_ZL) {
            if(!available.empty()&&listIndex>=0&&listIndex<static_cast<int>(available.size()))
                openSteamGridPicker(available[static_cast<size_t>(listIndex)]);
            else setStatus("SELECT AN INSTALLED GAME FIRST");
            return;
        }
        if(available.empty())return;
        if(listIndex>=static_cast<int>(available.size()))listIndex=static_cast<int>(available.size())-1;
        if(keys&HidNpadButton_Up) {
            listIndex=(listIndex-1+static_cast<int>(available.size()))%static_cast<int>(available.size());
            loadPlayStatsForSelection();
        }
        if(keys&HidNpadButton_Down) {
            listIndex=(listIndex+1)%static_cast<int>(available.size());
            loadPlayStatsForSelection();
        }
        if(keys&HidNpadButton_A)addSelected();
        return;
    }
    // While a tile is being moved, only movement, workspace destination,
    // drop and deliberate removal are accepted. Modals cannot steal the
    // partially-finished interaction.
    if(dragging) {
        if(keys&(HidNpadButton_A|HidNpadButton_B)) {
            finishTileDrag();
            return;
        }
        if(keys&HidNpadButton_L)moveAcrossDesk(-1);
        else if(keys&HidNpadButton_R)moveAcrossDesk(1);
        if(keys&HidNpadButton_Left)moveTile(-1,0);
        else if(keys&HidNpadButton_Right)moveTile(1,0);
        if(keys&HidNpadButton_Up)moveTile(0,-1);
        else if(keys&HidNpadButton_Down)moveTile(0,1);
        if(keys&HidNpadButton_Minus)removeTile();
        if(keys&HidNpadButton_X) {
            const bool saved=finishTileDrag(nullptr);
            editing=false;
            if(saved)setStatus("EDIT MODE CLOSED");
        }
        return;
    }
    if(keys&HidNpadButton_B) {
#ifdef DESKNX_SYSTEM_APPLET
        {
            const Result rc=sendIpc([]{return desknx::ipc::Send(desknx::ipc::Command::ReturnToGame);});
            if(R_FAILED(rc)){setStatus("SYSTEM BACKEND REQUEST FAILED",3000);return;}
            appletRequestExitToSelf();
            running=false;
            return;
        }
#else
        appletRequestExitToSelf();running=false;return;
#endif
    }
    if(keys&HidNpadButton_Plus)newDesk();
    if(keys&HidNpadButton_StickL) { systemMenu=true;systemMenuIndex=0;return; }
    if(keys&HidNpadButton_StickR) {
        // The right stick opens workspace actions in every mode. Destructive
        // operations must always be selected explicitly from the menu.
        workspaceMenu=true;
        workspaceMenuIndex=0;
    }
    if(keys&HidNpadButton_X){editing=!editing;if(!editing&&dragging){dragging=false;saveNow();}setStatus(editing?"EDIT MODE - EMPTY CELLS STAY EMPTY":"EDIT MODE CLOSED");}
    if(keys&HidNpadButton_Y){editing=true;openList();}
    if(keys&HidNpadButton_L) {
        if(dragging) moveAcrossDesk(-1);
        else {
            deskIndex=(deskIndex-1+static_cast<int>(desks.size()))%static_cast<int>(desks.size());
            scheduleActiveWorkspaceSave();
            setStatus(desks[static_cast<size_t>(deskIndex)].name);
        }
    } else if(keys&HidNpadButton_R) {
        if(dragging) moveAcrossDesk(1);
        else {
            deskIndex=(deskIndex+1)%static_cast<int>(desks.size());
            scheduleActiveWorkspaceSave();
            setStatus(desks[static_cast<size_t>(deskIndex)].name);
        }
    }
    if(keys&HidNpadButton_Left){if(dragging)moveTile(-1,0);else updateCursor(-1,0);}
    if(keys&HidNpadButton_Right){if(dragging)moveTile(1,0);else updateCursor(1,0);}
    if(keys&HidNpadButton_Up){if(dragging)moveTile(0,-1);else updateCursor(0,-1);}
    if(keys&HidNpadButton_Down){if(dragging)moveTile(0,1);else updateCursor(0,1);}
    if(editing&&(keys&HidNpadButton_ZL))resizeTile(false);
    if(editing&&(keys&HidNpadButton_ZR))resizeTile(true);
    if(editing&&(keys&HidNpadButton_Minus))removeTile();
    // ТЗ 6.2: "-" in Normal mode opens the power menu (PWR-03). In Edit mode
    // Minus keeps its remove-tile meaning, handled above. Auto-repeat never
    // opens modals (UI-14 is about cursor movement only).
    if(!fromRepeat&&!editing&&!deletePrompt&&!listOpen&&(keys&HidNpadButton_Minus)) {
        powerMenu=true;powerIndex=0;return;
    }
    if(keys&HidNpadButton_A) {
        Workspace& d=desks[static_cast<size_t>(deskIndex)];int i=itemAt(d,selectedX,selectedY);
        if(editing){if(dragging){dragging=false;saveNow();setStatus("POSITION SAVED");}
            else if(i>=0){const Tile&t=d.tiles[static_cast<size_t>(i)];selectedX=t.x;selectedY=t.y;dragging=true;setStatus("MOVE WITH D-PAD; L/R CHANGES WORKSPACE");}
            else openList();}
        else if(i>=0)launch(d.tiles[static_cast<size_t>(i)]);else setStatus("EMPTY CELL - PRESS Y TO ADD");
    }
}
bool touchSelect(int x,int y) {
    Workspace& desk=desks[static_cast<size_t>(deskIndex)];
    if(dragging) {
        desknx::workspace_ops::CellPoint cell{};
        if(!desknx::workspace_ops::cellFromPoint(
               x,y,LEFT,TOP,CW,CH,GX,GY,true,cell))
            return false;
        const int index=itemAt(desk,selectedX,selectedY);
        if(index<0)return false;
        const Tile& moving=desk.tiles[static_cast<size_t>(index)];
        desknx::workspace_ops::CellPoint origin=
            desknx::workspace_ops::tileOriginForPointer(
                cell,touchDragOffsetX,touchDragOffsetY);
        origin.x=std::max(0,std::min(COLS-moving.w,origin.x));
        origin.y=std::max(0,std::min(ROWS-moving.h,origin.y));
        const int dx=origin.x-selectedX,dy=origin.y-selectedY;
        // Place diagonally in one atomic occupancy check. Moving X and then Y
        // could fail on an occupied intermediate cell even when the final
        // diagonal destination is perfectly free, leaving the tile off-target.
        if(dx||dy)moveTile(dx,dy);
        return true;
    }
    const int hit=desknx::workspace_ops::tileAtPoint(
        desk,x,y,LEFT,TOP,CW,CH,GX,GY);
    if(hit>=0) {
        selectedX=desk.tiles[static_cast<size_t>(hit)].x;
        selectedY=desk.tiles[static_cast<size_t>(hit)].y;
        return true;
    }
    desknx::workspace_ops::CellPoint cell{};
    if(!desknx::workspace_ops::cellFromPoint(
           x,y,LEFT,TOP,CW,CH,GX,GY,false,cell))
        return false;
    selectedX=cell.x;
    selectedY=cell.y;
    return true;
}
int tabStart(){
    if(touchDragging&&touchDragTabStart>=0)return touchDragTabStart;
    return std::max(0,std::min(deskIndex-2,
        static_cast<int>(desks.size())-VisibleDeskTabs));
}
void touchInput(const SDL_Event&e) {
    if(userPickerOpen) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
            if(x>=844&&x<=920&&y>=136&&y<=170) {
                userPickerOpen=false;
            } else if(!consoleUsers.empty()&&x>=370&&x<=910&&y>=214&&
                      y<214+static_cast<int>(consoleUsers.size())*46) {
                userPickerIndex=std::max(0,std::min(
                    static_cast<int>(consoleUsers.size())-1,(y-214)/46));
                selectConsoleUser(userPickerIndex);
            }
        }
        return;
    }
    // A second pointer must not steal the workspace interaction from an
    // in-progress drag, whether the drag was started by touch or controller.
    if(touchPressActive &&
       (e.type==SDL_FINGERMOTION||e.type==SDL_FINGERUP) &&
       e.tfinger.fingerId!=touchPressFingerId)return;
    if(catalogueTouchActive &&
       (e.type==SDL_FINGERMOTION||e.type==SDL_FINGERUP) &&
       e.tfinger.fingerId!=catalogueTouchFingerId)return;
    if(e.type==SDL_FINGERDOWN && (touchPressActive||catalogueTouchActive))return;
    if(e.type==SDL_FINGERDOWN && dragging && !touchDragging) {
        setStatus("FINISH MOVING THE TILE FIRST");
        return;
    }
    if(e.type==SDL_FINGERDOWN && mediaPickerOpen) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=930&&x<=1022&&y>=86&&y<=120) {
            mediaPickerBack();
        } else if(mediaPickerType==MediaPickerType::WorkspaceBackground &&
                  x>=782&&x<=914&&y>=86&&y<=120) {
            mediaPickerDirectory=mediaPickerDirectory.compare(
                0,std::string(ALBUM_ROOT).size(),ALBUM_ROOT)==0
                ?std::string(MEDIA_ROOT):std::string(ALBUM_ROOT);
            mediaPickerIndex=mediaPickerOffset=0;
            refreshMediaPicker();
        } else if(x>=260&&x<=1020&&y>=166&&y<574) {
            const int row=(y-166)/34;
            applyMediaPickerSelection(mediaPickerOffset+row);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && steamGridPickerOpen) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=928&&x<=1010&&y>=112&&y<=146) {
            handleSteamGridPickerInput(HidNpadButton_B);
        } else if(steamGridPickerStage==SteamGridPickerStage::ChooseGame) {
            if(x>=274&&x<=1006&&y>=218&&y<542) {
                const int row=(y-218)/36;
                steamGridListIndex=steamGridListOffset+row;
                handleSteamGridPickerInput(HidNpadButton_A);
            }
        } else if(steamGridPickerStage==SteamGridPickerStage::ChooseArtwork) {
            if(x>=274&&x<=700&&y>=238&&y<544) {
                const int row=(y-238)/34;
                const int selected=steamGridListOffset+row;
                if(selected>=0&&selected<static_cast<int>(steamGridArtwork.size())) {
                    steamGridListIndex=selected;
                    requestSteamGridPreview();
                }
            } else if(x>=820&&x<=1006&&y>=548&&y<=586) {
                handleSteamGridPickerInput(HidNpadButton_A);
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && consoleSettingsOpen) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=812&&x<=890&&y>=151&&y<=184)consoleSettingsOpen=false;
        else if(x>=392&&x<=888&&y>=224&&y<514) {
            consoleSettingsIndex=std::max(0,std::min(4,(y-224)/58));
            activateConsoleSetting(consoleSettingsIndex);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && appSettingsMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=842&&x<=934&&y>=132&&y<=164) {
            appSettingsMenu=false;
        } else if(x>=340&&x<=940&&y>=214&&y<552) {
            const int row=(y-214)/26;
            if(row>=0&&row<13)activateAppSettings(row);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && workspaceSettingsMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=842&&x<=934&&y>=132&&y<=164)workspaceSettingsMenu=false;
        else if(x>=340&&x<=940&&y>=216&&y<552) {
            const int row=(y-216)/48;
            if(row>=0&&row<6){workspaceSettingsIndex=row;activateWorkspaceAppearance(row);}
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && systemMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=1058&&x<=1140&&y>=136&&y<=170){systemMenu=false;return;}
        if(y>=218&&y<354) {
            if(x>=156&&x<444)systemMenuIndex=0;
            else if(x>=496&&x<784)systemMenuIndex=1;
            else if(x>=836&&x<1124)systemMenuIndex=2;
            else return;
        } else if(y>=382&&y<518) {
            if(x>=156&&x<444)systemMenuIndex=3;
            else if(x>=496&&x<784)systemMenuIndex=4;
            else if(x>=836&&x<1124)systemMenuIndex=5;
            else return;
        } else return;
        keyInput(HidNpadButton_A,false);
        return;
    }
    if(e.type==SDL_FINGERDOWN && workspaceReorderOpen) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=390&&x<=890&&y>=205&&y<205+static_cast<int>(desks.size())*28) {
            workspaceReorderIndex=std::max(0,std::min(static_cast<int>(desks.size())-1,(y-205)/28));
            keyInput(HidNpadButton_A,false);
        } else if(x>=390&&x<=910&&y>=560&&y<=600) {
            keyInput(HidNpadButton_B,false);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && workspacePicker) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=744&&x<=920&&y>=590&&y<=628) {
            workspacePicker=false;
        } else if(x>=360&&x<=920&&y>=168&&y<168+static_cast<int>(desks.size())*32) {
            const int target=(y-168)/32;
            selectWorkspace(target);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && workspaceMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=390&&x<=890&&y>=214&&y<510) {
            const int row=(y-214)/42;
            if(row>=0&&row<7) {
                workspaceMenu=false;
                if(row==0)openWorkspacePicker(false);
                else if(row==1)openWorkspacePicker(true);
                else if(row==2){workspaceSettingsMenu=true;workspaceSettingsIndex=0;}
                else if(row==3)renameDesk();
                else if(row==4)deleteDesk();
                else if(row==5){workspaceReorderOpen=true;workspaceReorderIndex=deskIndex;}
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && deletePrompt) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=382&&y<=430&&x>=405&&x<615) keyInput(HidNpadButton_A,false);
        else if(y>=382&&y<=430&&x>=635&&x<=875) keyInput(HidNpadButton_B,false);
        return;
    }
    if(e.type==SDL_FINGERDOWN && editing && !dragging && !listOpen && !recentOpen && !powerMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=566&&y<=614&&itemAt(
               desks[static_cast<size_t>(deskIndex)],selectedX,selectedY)>=0) {
            if(x>=650&&x<815) resizeTile(false);
            else if(x>=826&&x<991) resizeTile(true);
            else if(x>=1002&&x<1230) removeTile();
            return;
        }
    }
    if(e.type==SDL_FINGERDOWN && powerMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=780&&x<=832&&y>=238&&y<=270) {
            keyInput(HidNpadButton_B,false);
        } else if(y>=289&&y<421&&x>=442&&x<=838) {
            powerIndex=std::max(0,std::min(2,(y-289)/44));
        } else if(x>=590&&x<=838&&y>=432&&y<=474) {
            keyInput(HidNpadButton_A,false);
        }
        return;
    }
    if(recentOpen) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
            if(x>=838&&x<=916&&y>=104&&y<=150) {
                recentOpen=false;
            } else if(x>=924&&x<=1012&&y>=104&&y<=150) {
                if(!recentLaunches.empty()) {
                    recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(recentLaunches.size())-1));
                    const HistoryEntry entry=recentLaunches[static_cast<size_t>(recentIndex)];
                    recentOpen=false;
                    launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
                }
            } else if(y>=190&&y<580&&!recentLaunches.empty()) {
                const int index=recentOffset+(y-190)/39;
                if(index>=0&&static_cast<size_t>(index)<recentLaunches.size())
                    recentIndex=index;
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN) {
        int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(listOpen) {
            if(y>=122&&y<=151) {
                if(x>=138&&x<234) searchCatalogue();
                else if(x>=242&&x<324) cycleCatalogueSort();
                else if(x>=332&&x<444) cycleCatalogueFilter(1);
                else if(x>=452&&x<534){catalogueQuery.clear();++catalogueViewRevision;listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);}
                else if(x>=542&&x<668) restartCatalogueScan();
                else if(x>=676&&x<748) closeList();
                else if(x>=756&&x<828) addSelected();
                return;
            }
            if(y>=532&&y<=576&&x>=900&&x<=1150) {
                const auto& available=visibleCatalogueSnapshot();
                if(!available.empty()&&listIndex>=0&&listIndex<static_cast<int>(available.size())&&
                   available[static_cast<size_t>(listIndex)].kind==Kind::Game) {
                    if(x<1014)openSteamGridPicker(available[static_cast<size_t>(listIndex)]);
                    else addSelected();
                } else addSelected();
                return;
            }
            if(y>=180&&y<580&&x>=130&&x<874) {
                const int row=(y-180)/40;
                const auto& available=visibleCatalogueSnapshot();
                const int index=listOffset+row;
                if(index>=0&&static_cast<size_t>(index)<available.size()) {
                    const Uint32 now=SDL_GetTicks();
                    if(lastCatalogueTap==index&&now-lastCatalogueTapTick<=500) {
                        listIndex=index;
                        addSelected();
                        lastCatalogueTap=-1;
                        catalogueTouchActive=false;
                        catalogueTouchFingerId=0;
                        return;
                    } else {
                        listIndex=index;
                        lastCatalogueTap=index;
                        lastCatalogueTapTick=now;
                        loadPlayStatsForSelection();
                    }
                    catalogueTouchActive=true;
                    catalogueTouchFingerId=e.tfinger.fingerId;
                    catalogueTouchMoved=false;
                    catalogueTouchStartY=y;
                    catalogueTouchRow=row;
                }
                return;
            }
            return;
        }
        if(deletePrompt)return;
        // Four permanent shortcuts: Gallery, Settings, Pads and Tools.
        if(y>=16&&y<=82) {
            if(x>=470&&x<=862) {
                openUserProfile();
                return;
            }
            const int index=(x-ToolbarStartX)/ToolbarButtonStep;
            if(x>=ToolbarStartX&&index>=0&&index<ToolbarButtonCount&&
               x<toolbarButtonX(index)+ToolbarButtonWidth) {
                switch(index) {
                    case 0: {
                        const Uint64 galleryStarted=SDL_GetTicks64();
                        desknx::diag::Write("gallery","launch_begin","applet=album_all_files");
                        const Result galleryRc=albumLaShowAllAlbumFiles();
                        desknx::diag::Write("gallery","return","rc=0x%08X elapsed_ms=%llu",
                            static_cast<unsigned int>(galleryRc),
                            static_cast<unsigned long long>(SDL_GetTicks64()-galleryStarted));
                        if(R_FAILED(galleryRc))setStatus("GALLERY UNAVAILABLE",3000);
                        break;
                    }
                    case 1: appSettingsMenu=true;appSettingsIndex=0; break;
                    case 2: {
                        HidLaControllerSupportArg arg{};hidLaCreateControllerSupportArg(&arg);
                        HidLaControllerSupportResultInfo result{};
                        if(R_FAILED(hidLaShowControllerSupport(&result,&arg)))
                            setStatus("PADS MENU UNAVAILABLE",3000);
                        break;
                    }
                    case 3: systemMenu=true;systemMenuIndex=0; break;
                    default: break;
                }
            }
            return;
        }
        // Workspace tabs and their controls live at the bottom of the screen.
        if(y>=638&&y<=694) {
            if(x>=DeskTabX&&x<DeskTabX+VisibleDeskTabs*DeskTabStep) {
                const int offset=x-DeskTabX;
                const int slot=offset/DeskTabStep;
                const int tabX=DeskTabX+slot*DeskTabStep;
                const int target=tabStart()+slot;
                if(x>=tabX&&x<tabX+DeskTabWidth&&target>=0&&
                   target<static_cast<int>(desks.size())&&target<tabStart()+VisibleDeskTabs) {
                    const Uint32 now=SDL_GetTicks();
                    if(target==deskIndex&&lastWorkspaceTabTap==target&&
                       now-lastWorkspaceTabTapTick<=500) {
                        lastWorkspaceTabTap=-1;
                        renameDesk();
                    } else {
                        if(target!=deskIndex) {
                            deskIndex=target;
                            scheduleActiveWorkspaceSave();
                            setStatus(desks[static_cast<size_t>(deskIndex)].name);
                        }
                        lastWorkspaceTabTap=target;
                        lastWorkspaceTabTapTick=now;
                    }
                } else lastWorkspaceTabTap=-1;
                return;
            }
            if(x>=770&&x<822) {newDesk();return;}
            if(x>=832&&x<884) {
                workspaceMenu=true;
                workspaceMenuIndex=0;
                return;
            }
            if(x>=900&&x<1050) {
                openWorkspacePicker(false);
                return;
            }
            if(x>=1096&&x<1224) {
                editing=!editing;
                if(!editing&&dragging){dragging=false;saveNow();}
                setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
                return;
            }
        }
        if(desks[static_cast<size_t>(deskIndex)].tiles.empty()&&
           y>=370&&y<=408) {
            if(x>=454&&x<630) {
                editing=true;
                openList();
                return;
            }
            if(x>=650&&x<900) {
                newDesk();
                return;
            }
        }
        if(y>=TOP&&y<TOP+ROWS*(CH+GY)) {
            const int oldX=selectedX,oldY=selectedY;
            if(!touchSelect(x,y))return;
            Workspace& d=desks[static_cast<size_t>(deskIndex)];
            const int i=itemAt(d,selectedX,selectedY);
            if(editing) {
                if(i>=0) {
                    touchPressActive=true;
                    touchPressFingerId=e.tfinger.fingerId;
                    touchPressStartX=x;
                    touchPressStartY=y;
                    desknx::workspace_ops::CellPoint grab{};
                    if(desknx::workspace_ops::cellFromPoint(
                           x,y,LEFT,TOP,CW,CH,GX,GY,true,grab)) {
                        touchDragOffsetX=grab.x-selectedX;
                        touchDragOffsetY=grab.y-selectedY;
                    } else {
                        touchDragOffsetX=touchDragOffsetY=0;
                    }
                    if(oldX==selectedX&&oldY==selectedY) {
                        dragging=true;
                        touchDragging=true;
                        touchDragTabStart=tabStart();
                    }
                } else {
                    openList();
                }
            } else if(i>=0) {
                launch(d.tiles[static_cast<size_t>(i)]);
            } else {
                openList();
            }
            return;
        }
    } else if(e.type==SDL_FINGERMOTION&&listOpen&&catalogueTouchActive) {
        const int y=static_cast<int>(e.tfinger.y*SH);
        if(std::abs(y-catalogueTouchStartY)>=10) catalogueTouchMoved=true;
        if(catalogueTouchMoved) {
            const int delta=(catalogueTouchStartY-y)/40;
            if(delta!=0) {
                const auto available=visibleCatalogueSnapshot();
                const int maxOffset=std::max(0,static_cast<int>(available.size())-10);
                listOffset=std::max(0,std::min(maxOffset,listOffset+delta));
                listIndex=std::max(0,std::min(static_cast<int>(available.size())-1,
                                              listOffset+catalogueTouchRow));
                catalogueTouchStartY=y;
            }
        }
    } else if(e.type==SDL_FINGERUP&&listOpen&&catalogueTouchActive&&
              e.tfinger.fingerId==catalogueTouchFingerId) {
        if(catalogueTouchMoved) lastCatalogueTap=-1;
        catalogueTouchActive=false;
        catalogueTouchMoved=false;
        catalogueTouchFingerId=0;
    } else if(e.type==SDL_FINGERMOTION&&touchPressActive&&!touchDragging) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(std::abs(x-touchPressStartX)+std::abs(y-touchPressStartY)>=12) {
            dragging=true;
            touchDragging=true;
            touchDragTabStart=tabStart();
            if(y>=638&&y<=694&&x>=DeskTabX) {
                const int start=tabStart();
                const int slot=(x-DeskTabX)/DeskTabStep;
                const int target=start+slot;
                const int tabX=DeskTabX+slot*DeskTabStep;
                if(slot>=0&&slot<VisibleDeskTabs&&x<tabX+DeskTabWidth&&
                   target>=0&&target<static_cast<int>(desks.size()))
                    moveToDesk(target);
            } else {
                (void)touchSelect(x,y);
            }
        }
    } else if(e.type==SDL_FINGERMOTION&&touchDragging) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=638&&y<=694&&x>=DeskTabX) {
            const int start=tabStart();
            const int slot=(x-DeskTabX)/DeskTabStep;
            const int target=start+slot;
            const int tabX=DeskTabX+slot*DeskTabStep;
            if(slot>=0&&slot<VisibleDeskTabs&&x<tabX+DeskTabWidth&&
               target>=0&&target<static_cast<int>(desks.size()))
                moveToDesk(target);
        } else {
            touchSelect(x,y);
        }
    } else if(e.type==SDL_FINGERUP&&touchPressActive) {
        const bool moved=touchDragging;
        touchPressActive=false;
        touchPressFingerId=0;
        touchDragging=false;
        touchDragOffsetX=touchDragOffsetY=0;
        touchDragTabStart=-1;
        if(moved) {
            dragging=false;
            if(saveNow())setStatus("POSITION SAVED");
        }
    }
}
} // namespace

int main(int,char**) {
    const Result sdMountRc = fsdevMountSdmc();
    if(R_SUCCEEDED(sdMountRc))sdMounted=true;
    desknx::diag::Initialize(sdMounted);
#ifdef DESKNX_SYSTEM_APPLET
    constexpr const char* buildMode = "system-applet";
#else
    constexpr const char* buildMode = "standalone-nro";
#endif
    const AppletType appletType = appletGetAppletType();
    desknx::diag::Write("session", "start",
        "mode=%s applet_type=%u application_mode=%d sd_mounted=%d sd_mount_rc=0x%08X",
        buildMode, static_cast<unsigned int>(appletType),
        appletType == AppletType_Application ? 1 : 0,
        sdMounted ? 1 : 0, static_cast<unsigned int>(sdMountRc));
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){
        desknx::diag::Write("startup", "sdl_init_failed", "error=%s", SDL_GetError());
        if(sdMounted) fsdevUnmountDevice("sdmc");
        return 1;
    }
    desknx::diag::Write("startup", "sdl_init_ok", "sdl_version=%u.%u.%u",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    window=SDL_CreateWindow("Transwitch Launcher",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SW,SH,SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(!window){
        desknx::diag::Write("startup", "window_create_failed", "error=%s", SDL_GetError());
        SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 2;
    }
    desknx::diag::Write("startup", "window_created", "logical_width=%d logical_height=%d", SW, SH);
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){
        desknx::diag::Write("startup", "renderer_create_failed", "error=%s", SDL_GetError());
        SDL_DestroyWindow(window);SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 3;
    }
    SDL_RendererInfo rendererInfo{};
    const int rendererInfoRc = SDL_GetRendererInfo(renderer, &rendererInfo);
    rendererVsync=(rendererInfoRc==0)&&(rendererInfo.flags&SDL_RENDERER_PRESENTVSYNC);
    desknx::diag::Write("startup", "renderer_ready", "name=%s info_rc=%d flags=0x%08X vsync=%d",
        rendererInfo.name ? rendererInfo.name : "unknown", rendererInfoRc,
        static_cast<unsigned int>(rendererInfo.flags),rendererVsync?1:0);
    SDL_RenderSetLogicalSize(renderer,SW,SH);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    catalogueMutex=SDL_CreateMutex();ipcMutex=SDL_CreateMutex();
    steamGridMutex=SDL_CreateMutex();
    storageCommitMutex=SDL_CreateMutex();
    playStatsMutex=SDL_CreateMutex();
    nextendoMutex=SDL_CreateMutex();
    nextendoFeed.setEnabled(true);

    // Match SLauncher's startup order: query the real firmware version and
    // publish it to libnx BEFORE initializing version-gated NS services.
    // Initializing NS later from the scan worker while libnx still reports an
    // unknown/default HOS version can make NS calls fail or stall on firmware
    // newer than the build SDK.
    const Result setsysRc = setsysInitialize();
    const bool setsysReady = R_SUCCEEDED(setsysRc);
    SetSysFirmwareVersion firmware{};
    const Result firmwareRc = setsysReady ? setsysGetFirmwareVersion(&firmware) : setsysRc;
    const bool firmwareRead = setsysReady && R_SUCCEEDED(firmwareRc);
    if (firmwareRead) {
        hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor,
                                     firmware.micro) | BIT(31));
    }
    desknx::diag::Write("startup", "firmware",
        "setsys_rc=0x%08X firmware_rc=0x%08X firmware_read=%d version=%u.%u.%u",
        static_cast<unsigned int>(setsysRc), static_cast<unsigned int>(firmwareRc),
        firmwareRead ? 1 : 0, firmware.major, firmware.minor, firmware.micro);
    std::uint32_t nsInitError = 0;
    const bool nsServicesReady = desknx::catalog::InitializeServices(nsInitError);
    desknx::diag::Write("startup", "services_ready", "ns_ready=%d ns_error=0x%08X",
        nsServicesReady ? 1 : 0, static_cast<unsigned int>(nsInitError));
    if (sdMounted) {
        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/DeskNX", 0777);
        if (FILE* log = std::fopen("sdmc:/switch/DeskNX/catalogue.log", "a")) {
            std::fprintf(log,
                "[%lld] startup setsys_ready=%d firmware_read=%d firmware=%u.%u.%u ns_ready=%d ns_error=0x%08X\n",
                static_cast<long long>(std::time(nullptr)), setsysReady ? 1 : 0,
                firmwareRead ? 1 : 0, firmware.major, firmware.minor, firmware.micro,
                nsServicesReady ? 1 : 0, static_cast<unsigned int>(nsInitError));
            std::fclose(log);
        }
    }

    desknx::diag::Write("storage","persistent_paths",
        "catalogue=sdmc:/switch/DeskNX/cache/catalogue.tsv names=sdmc:/switch/DeskNX/cache/game-names.tsv icons=sdmc:/switch/DeskNX/cache/icons layout=%s active=%s settings=%s history=%s",
        CFG_FILE,ACTIVE_WORKSPACE_FILE,SETTINGS_FILE,HISTORY_FILE);
    struct stat catalogueCacheStat{}, nameCacheStat{};
    const bool catalogueCacheExists=stat("sdmc:/switch/DeskNX/cache/catalogue.tsv",&catalogueCacheStat)==0&&S_ISREG(catalogueCacheStat.st_mode);
    const bool nameCacheExists=stat("sdmc:/switch/DeskNX/cache/game-names.tsv",&nameCacheStat)==0&&S_ISREG(nameCacheStat.st_mode);
    desknx::diag::Write("catalogue","persistent_cache_state",
        "catalogue_exists=%d catalogue_bytes=%lld names_exists=%d names_bytes=%lld",
        catalogueCacheExists?1:0,
        catalogueCacheExists?static_cast<long long>(catalogueCacheStat.st_size):0LL,
        nameCacheExists?1:0,
        nameCacheExists?static_cast<long long>(nameCacheStat.st_size):0LL);
    loadHistory();
    struct stat layoutStat{}, backupStat{};
    const bool layoutExists = stat(CFG_FILE, &layoutStat) == 0;
    const bool backupExists = stat(CFG_BAK_FILE, &backupStat) == 0;
    layoutLoaded = loadLayout();
    desknx::diag::Write("layout","startup_primary_result",
        "loaded=%d primary_exists=%d primary_bytes=%lld backup_exists=%d",
        layoutLoaded?1:0,layoutExists?1:0,
        layoutExists?static_cast<long long>(layoutStat.st_size):0LL,
        backupExists?1:0);
    bool restoredFromBackup = false;
    if (!layoutLoaded && backupExists && loadLayoutFrom(CFG_BAK_FILE)) {
        // The primary may be missing as well as corrupt. Never discard the only
        // valid backup until the old primary has been preserved or is absent.
        if (layoutExists && !backupBrokenLayout())
            layoutSaveBlocked = true;
        layoutLoaded = true;
        restoredFromBackup = true;
        setStatus("LAYOUT RESTORED FROM BACKUP", 5000);
    } else if (layoutExists && !layoutLoaded) {
        if (!backupBrokenLayout())
            layoutSaveBlocked = true;
        defaultLayout();
        defaultLayoutPending=true;
        desknx::diag::Write("layout","defaults_created","reason=primary_invalid backup_unavailable");
    } else if (!layoutLoaded) {
        defaultLayout();
        defaultLayoutPending=true;
        desknx::diag::Write("layout","defaults_created","reason=no_persisted_layout");
    }
    if(layoutLoaded && loadActiveWorkspaceState()) {
        setStatus("RESTORED: "+desks[static_cast<size_t>(deskIndex)].name,2500);
    } else if(layoutLoaded) {
        // Migrate existing layouts to the separate active-workspace marker.
        (void)saveActiveWorkspaceState();
    }
    // Restore the user's last desk before publishing a recovered backup.
    // Saving the backup first would overwrite the valid marker with its older
    // active index and could silently forget the desk selected before shutdown.
    if(restoredFromBackup&&!layoutSaveBlocked) {
        if(saveLayout())setStatus("LAYOUT RESTORED FROM BACKUP",5000);
    }

    if (layoutRecoveredTiles > 0 || layoutDroppedTiles > 0) {
        char message[96]{};
        std::snprintf(message, sizeof(message), "LAYOUT RECOVERED: %zu MOVED, %zu OMITTED",
                      layoutRecoveredTiles, layoutDroppedTiles);
        setStatus(message, 6000);
    } else if (restoredFromBackup) {
        setStatus("LAYOUT RESTORED FROM BACKUP", 5000);
    }

    loadLauncherSettings();
    applyThemePalette();
    PadState pad{};padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    setStatus("STARTING TRANSWITCH...",5000);
    drawScreen(); // first frame precedes application enumeration, SD recursion and font service init
#ifdef DESKNX_SYSTEM_APPLET
    const Result readyRc=sendIpc([]{return desknx::ipc::SendMenuReady();});
    if(R_FAILED(readyRc))setStatus("SYSTEM BACKEND NOT CONNECTED",5000);
    else setStatus("TRANSWITCH READY - CATALOGUE LOADING",4000);
    startHeartbeatThread();
#else
    setStatus("CATALOGUE LOADING - Y ADD  X EDIT",5000);
#endif
    IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);
    gIconCache.setRenderer(renderer);
    gFontRenderer.setRenderer(renderer);
    gFontRenderer.initialize(); // slow pl initialization cannot delay MenuReady
    if(fontPath.empty()) {
        const char* builtInFonts[]={
            "sdmc:/switch/DeskNX/media/fonts/NotoSans.ttf",
            "sdmc:/switch/DeskNX/media/fonts/OpenSans.ttf",
            "sdmc:/switch/DeskNX/media/fonts/NotoSansMono.ttf"
        };
        for(const char* candidate:builtInFonts) {
            struct stat fontStat{};
            if(stat(candidate,&fontStat)==0&&S_ISREG(fontStat.st_mode)&&fontStat.st_size>0) {
                fontPath=candidate;
                break;
            }
        }
    }
    gIconCache.setCustomIconPackDir(iconPackPath);
    if(!fontPath.empty()&&!gFontRenderer.setCustomFont(fontPath)) {
        desknx::diag::Write("font","load_failed","path=%s error=%s",
            fontPath.c_str(),TTF_GetError());
        fontPath.clear();
        setStatus("CUSTOM FONT UNAVAILABLE - USING SYSTEM FONT",3000);
        (void)saveLauncherSettings();
    } else if(!fontPath.empty()) {
        desknx::diag::Write("font","loaded","path=%s bundled=%d",fontPath.c_str(),
            fontPath.find("/media/fonts/")!=std::string::npos?1:0);
    } else {
        desknx::diag::Write("font","using_system_fallback","reason=no_builtin_font_found");
    }
    if(!wallpaperPath.empty()&&!loadWallpaperTexture(wallpaperPath)) {
        wallpaperPath.clear();
        setStatus("WALLPAPER UNAVAILABLE - USING DEFAULT",3000);
        (void)saveLauncherSettings();
    }
    if(layoutNeedsMigration&&layoutLoaded&&saveLayout())
        layoutNeedsMigration=false;

    if (!nsServicesReady) {
        char message[80]{};
        std::snprintf(message, sizeof(message), "NS INIT FAILED: 0X%08X",
                      static_cast<unsigned int>(nsInitError));
        setStatus(message, 6000);
    }
    catalogueThread=SDL_CreateThread(catalogueThreadMain,"DeskNX catalogue",nullptr);
    nextendoThread=SDL_CreateThread(nextendoThreadMain,"Nextendo online counts",nullptr);
    if(!nextendoThread)desknx::diag::Write("nextendo","thread_create_failed","online counts disabled");
    if(!catalogueThread){
        desknx::diag::Write("catalogue", "thread_create_failed", "fallback= synchronous_scan");
        setStatus("BACKGROUND SCAN UNAVAILABLE",3000);
        publishCatalogue(desknx::catalog::Scan());
    }
    if(!consoleUsersLoaded) loadConsoleUsers();
    Uint32 previousFrameTick = SDL_GetTicks();
    Uint32 lastSlowFrameLog = 0;
    while(running&&appletMainLoop()) {
        bool ready=false,complete=false,truncated=false;
        readCatalogueState(ready,complete,truncated);
        const Uint32 frameNow = SDL_GetTicks();
        const Uint32 frameDelta = frameNow - previousFrameTick;
        previousFrameTick = frameNow;
        if (frameDelta > 500 && frameNow - lastSlowFrameLog > 5000) {
            lastSlowFrameLog = frameNow;
            desknx::diag::Write("runtime", "slow_frame",
                "delta_ms=%u catalogue_ready=%d scan_complete=%d",
                frameDelta, ready ? 1 : 0, complete ? 1 : 0);
        }
        if(complete&&defaultLayoutPending) {
            // On first launch the default desks are populated only after the
            // catalogue is ready. Saving an active workspace switch must not
            // cancel this population; actual content edits must be preserved.
            if(!layoutUserModified)defaultLayout();
            const bool saved=saveLayout();
            // Finalize the first-run decision even if SD is read-only. Keep the
            // in-memory desks intact and do not rebuild them every frame.
            layoutLoaded=true;
            defaultLayoutPending=false;
            if(saved)setStatus("LAUNCHER READY - Y ADD  X EDIT",4000);
        }
        if(complete&&truncated&&!catalogueWarningShown){catalogueWarningShown=true;setStatus("CATALOGUE LIMIT REACHED - SOME ITEMS HIDDEN",5000);}
#ifdef DESKNX_SYSTEM_APPLET
        if(SDL_AtomicGet(&heartbeatError)){SDL_AtomicSet(&heartbeatError,0);setStatus("SYSTEM BACKEND IPC UNAVAILABLE",2500);}
        consumeInitialState(); // IPC-06: queued at backend launch, read once
        pollBackendEvents();   // §4.4: AppExited / AppLaunchFailed / SdRemoved / LowBattery
#endif
        padUpdate(&pad);
        const u64 buttonDown=padGetButtonsDown(&pad);
        const HidAnalogStickState leftStick=padGetStickPos(&pad,0);
        constexpr int StickThreshold=13500;
        u64 stickDirections=0;
        if(leftStick.x < -StickThreshold)stickDirections|=HidNpadButton_Left;
        else if(leftStick.x > StickThreshold)stickDirections|=HidNpadButton_Right;
        if(leftStick.y > StickThreshold)stickDirections|=HidNpadButton_Up;
        else if(leftStick.y < -StickThreshold)stickDirections|=HidNpadButton_Down;
        static u64 previousStickDirections=0;
        const u64 newlyPressedStickDirections=stickDirections&~previousStickDirections;
        previousStickDirections=stickDirections;
        const u64 keys=buttonDown|newlyPressedStickDirections;
        if(keys)keyInput(keys,false);
        // Holding a direction repeats navigation after a short, responsive delay.
        // Modal ownership remains centralized in keyInput().
        {
            static Uint32 holdStart = 0, lastRepeat = 0;
            static u64 lastHeldDirs = 0;
            const u64 held = padGetButtons(&pad)|stickDirections;
            const u64 dirs = HidNpadButton_Left|HidNpadButton_Right|HidNpadButton_Up|HidNpadButton_Down;
            // Navigation modals handle directional input themselves; keep key
            // repeat active in the library, recent list and power menu. The
            // destructive workspace-confirmation prompt remains single-shot.
            const bool modalBlocksRepeat = deletePrompt;
            if(!modalBlocksRepeat && (held&dirs) && !touchDragging) {
                const Uint32 now = SDL_GetTicks();
                if((held&dirs) != lastHeldDirs) { holdStart=now; lastRepeat=now; lastHeldDirs=held&dirs; }
                else if(now-holdStart >= 240 && now-lastRepeat >= 62) {
                    lastRepeat = now;
                    u64 rep = 0;
                    if(held&HidNpadButton_Left)  rep|=HidNpadButton_Left;
                    if(held&HidNpadButton_Right) rep|=HidNpadButton_Right;
                    if(held&HidNpadButton_Up)    rep|=HidNpadButton_Up;
                    if(held&HidNpadButton_Down)  rep|=HidNpadButton_Down;
                    keyInput(rep, true);
                }
            } else {
                lastHeldDirs = 0;
            }
        }
        pumpAsyncLayoutSaver();
        pumpPlayStatsQuery();
        pumpSteamGridWorker();
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT){appletRequestExitToSelf();running=false;}else touchInput(event);}
        const Uint32 persistenceNow=SDL_GetTicks();
        if(activeWorkspaceMarkerPending &&
           static_cast<Sint32>(persistenceNow-activeWorkspaceMarkerDue)>=0)
            (void)flushActiveWorkspaceSave();
        const Uint32 renderStarted=SDL_GetTicks();
        drawScreen();
        // Present already waits for the display when VSync is active. Sleeping
        // another 16ms here capped the accelerated path at roughly 30 FPS.
        if(!rendererVsync) {
            const Uint32 elapsed=SDL_GetTicks()-renderStarted;
            if(elapsed<16)svcSleepThread(static_cast<s64>(16-elapsed)*1'000'000LL);
        }
    }
#ifdef DESKNX_SYSTEM_APPLET
    stopHeartbeatThread();
#endif
    SDL_AtomicSet(&nextendoStop,1);
    if(nextendoThread){SDL_WaitThread(nextendoThread,nullptr);nextendoThread=nullptr;}
    if(layoutLoaded && !defaultLayoutPending && !layoutSaveBlocked && sdMounted)
        (void)saveLayout();
    for(auto& entry:workspaceBackgroundTextures)
        if(entry.texture)SDL_DestroyTexture(entry.texture);
    workspaceBackgroundTextures.clear();
    if(roundedCornerMaskTexture){SDL_DestroyTexture(roundedCornerMaskTexture);roundedCornerMaskTexture=nullptr;}
    desknx::diag::Write("session", "shutdown_begin", "catalogue_thread=%d",
                        catalogueThread ? 1 : 0);
    playStatsPending=false;
    if(steamGridThread){SDL_WaitThread(steamGridThread,nullptr);steamGridThread=nullptr;}
    clearSteamGridPreview();
    if(steamGridMutex){SDL_DestroyMutex(steamGridMutex);steamGridMutex=nullptr;}
    if(playStatsThread) {
        SDL_WaitThread(playStatsThread,nullptr);
        playStatsThread=nullptr;
    }
    if(catalogueThread){SDL_WaitThread(catalogueThread,nullptr);catalogueThread=nullptr;}
    desknx::catalog::ShutdownServices();
    if(accountServiceReady) {
        accountExit();
        accountServiceReady=false;
    }
    if (setsysReady) setsysExit();
    if(consoleSettingsLblReady){lblExit();consoleSettingsLblReady=false;}
    gIconCache.shutdown();gFontRenderer.shutdown();
    if(wallpaperTexture){SDL_DestroyTexture(wallpaperTexture);wallpaperTexture=nullptr;}
    IMG_Quit();
    if(psmReady)psmExit();
    if (playStatsMutex) SDL_DestroyMutex(playStatsMutex);
    if (nextendoMutex) SDL_DestroyMutex(nextendoMutex);
    if (ipcMutex) SDL_DestroyMutex(ipcMutex);
    if (storageCommitMutex) SDL_DestroyMutex(storageCommitMutex);
    if (catalogueMutex) SDL_DestroyMutex(catalogueMutex);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    desknx::diag::Write("session", "end", "normal_exit=1");
    if(sdMounted)fsdevUnmountDevice("sdmc");
    return 0;
}
