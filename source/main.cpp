#include <switch.h>
#include <switch/applets/hid_la.h>
#include <switch/applets/album_la.h>
#include <switch/applets/friends_la.h>
#include <switch/services/audctl.h>
#include <switch/services/nifm.h>
#include <switch/services/lbl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <desknx/Protocol.hpp>
#include <desknx/SafeReplace.hpp>
#include "../include/desknx/Model.hpp"
#include "../include/desknx/LibraryGrid.hpp"
#include "../include/desknx/WorkspaceOps.hpp"
#include "../include/desknx/HomeNXGridAdapter.hpp"
#include "../include/desknx/ShutdownPersistence.hpp"
#include "../include/desknx/Layout.hpp"
#include "catalog.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/Diagnostics.hpp"
#include "../include/desknx/Localization.hpp"
#include "../include/desknx/NextendoPlayers.hpp"
#include "../include/desknx/WidgetLayout.hpp"
#include "../include/desknx/WidgetSensors.hpp"
#include "catalog_view.hpp"
#include "../include/desknx/Artwork.hpp"
#include "FontRenderer.hpp"
#include "IconCache.hpp"
#include "WorkspaceBackgroundCache.hpp"
#include "SteamGridDbService.hpp"
#include "desknx/WidgetConfig.hpp"
#include "SvgIconCache.hpp"
#include "VisualEngine.hpp"
#include "UiComponents.hpp"
#include "../include/desknx/UiMotion.hpp"
#include "../include/desknx/ModalLayer.hpp"
#include "../include/desknx/SurfaceManager.hpp"

#include <algorithm>
#include <iterator>
#include <array>
#include <cctype>
#include <cerrno>
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
constexpr int ToolbarStartX=64, ToolbarButtonWidth=100, ToolbarButtonStep=108, ToolbarButtonCount=3;
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
constexpr const char* FAVORITES_FILE = "sdmc:/switch/DeskNX/favorites.tsv";
constexpr const char* FAVORITES_TMP = "sdmc:/switch/DeskNX/favorites.tmp";
constexpr const char* FAVORITES_BAK = "sdmc:/switch/DeskNX/favorites.bak";
constexpr const char* SETTINGS_FILE = "sdmc:/switch/DeskNX/settings.ini";
constexpr const char* SETTINGS_TMP = "sdmc:/switch/DeskNX/settings.tmp";
constexpr const char* SETTINGS_BAK = "sdmc:/switch/DeskNX/settings.bak";
constexpr const char* MEDIA_ROOT = "sdmc:/switch/DeskNX/media";
constexpr const char* GAME_ICON_OVERRIDES_DIR = "sdmc:/switch/DeskNX/media/game-icons";
constexpr const char* GAME_NAME_OVERRIDES_FILE = "sdmc:/switch/DeskNX/cache/game-overrides.tsv";
constexpr const char* GAME_NAME_OVERRIDES_TMP = "sdmc:/switch/DeskNX/cache/game-overrides.tmp";
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
int themeIndex=0; // 0 = v0.1, 1 = SOCOMA; accent colours are independent.
int accentColorIndex=-1; // -1 = follow selected theme, 0..11 = explicit palette colour.
bool languageRussian=true;
int iconStyle=0; // 0 clean SVG, 1 soft SVG, 2 SVG badge
int headerStyle=0; // 0 glass bar, 1 tactical rail, 2 minimal floating
int dockStyle=0;   // 0 glass dock, 1 segmented rail, 2 floating dock
int surfaceOpacity=255; // 0..255; scales all soft surfaces and widget cards
int selectionStyle=0;   // 0 outline, 1 accent rail, 2 pulse, 3 corner brackets
bool motionEffects=false; // optional low-cost screen fade and selection pulse
Uint32 uiTransitionStart=0;
std::uint32_t lastUiScreenState=UINT32_MAX;
std::string wallpaperPath, fontPath, iconPackPath, steamGridDbApiKey;
bool launcherSettingsSavePending=false;
Uint32 launcherSettingsSaveDue=0;
struct LauncherSettingsSaveJob { std::string contents; };
enum class LauncherSettingsSaveStatus { Success, SdUnavailable, OpenFailed, WriteFailed, TempCommitFailed, PublishFailed, FinalCommitFailed };
SDL_Thread* launcherSettingsSaveThread=nullptr;
std::unique_ptr<LauncherSettingsSaveJob> activeLauncherSettingsSaveJob;
SDL_atomic_t launcherSettingsSaveDone{};
LauncherSettingsSaveStatus launcherSettingsSaveOutcome=LauncherSettingsSaveStatus::Success;
std::uint64_t layoutMutationRevision=0;
std::unordered_map<std::string,std::string> gameNameOverrides;
std::string pendingGameIconTitleId;
SDL_Texture* roundedCornerMaskTexture=nullptr;
int roundedCornerMaskRadius=0;

enum class MediaPickerType { Wallpaper, Font, IconPack, WorkspaceBackground, GameIcon };
struct MediaEntry { std::string name, path; bool directory{false}; };
bool mediaPickerOpen=false;
MediaPickerType mediaPickerType=MediaPickerType::Wallpaper;
std::string mediaPickerDirectory;
std::vector<MediaEntry> mediaPickerEntries;
int mediaPickerIndex=0, mediaPickerOffset=0;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
desknx::visual::VisualEngine gVisualEngine;
desknx::ui::UiComponents gUiComponents;
desknx::ui::SurfaceManager gSurfaceManager;
desknx::ui::ModalLayer gModalLayer;
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
SDL_atomic_t nextendoConfiguredRefreshMs{};
SDL_atomic_t nextendoNetworkReady{};
SDL_atomic_t nextendoConsecutiveFailures{};
SDL_atomic_t nextendoLastSuccessTick{};
desknx::nextendo::PresenceFeed nextendoFeed;
desknx::nextendo::PopularGames nextendoPopularCache{};
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
bool setsysServiceReady=false;
int userPickerIndex=0;
AccountUid uiSelectedUser{};
std::string selectedUserLabel="SELECT PLAYER";
bool sameAccountUid(const AccountUid& a,const AccountUid& b);
FontRenderer gFontRenderer;
IconCache gIconCache;
WorkspaceBackgroundCache gWorkspaceBackgroundCache;
desknx::steamgriddb::SteamGridDbService gSteamGridDbService;
enum class SteamGridDbPickerView { Matches, Assets };
bool steamGridDbPickerOpen=false, steamGridDbOfflineFallback=false, steamGridDbApplyPending=false, steamGridDbStartupPruned=false;
bool steamGridDbPreviewAppliedNotified=false;
bool steamGridDbTouchTracking=false, steamGridDbTouchDragged=false;
int steamGridDbTouchStartX=0, steamGridDbTouchStartY=0;
int steamGridDbMatchIndex=0, steamGridDbMatchOffset=0;
int steamGridDbKindIndex=1, steamGridDbAssetOffset=0;
std::string steamGridDbPreviewLoadedPath;
SDL_Texture* steamGridDbPreviewTexture=nullptr;
uint64_t steamGridDbPreviewRevision=UINT64_MAX;
SteamGridDbPickerView steamGridDbPickerView=SteamGridDbPickerView::Matches;
VectorIconCache gVectorIconCache;
struct HistoryEntry { Kind kind{Kind::Game}; std::string title, data; };
std::vector<HistoryEntry> recentLaunches;
std::vector<std::string> favoriteTitleIds;
std::vector<Tile> favoritesView;
bool favoritesOpen=false, recentGamesOnly=false, gameLibraryMode=false;
int favoritesIndex=0, favoritesOffset=0;
bool widgetEditorOpen=false;
int widgetEditorRow=0;
Kind widgetEditorKind=Kind::Clock;
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
int batteryChargerType=-1;
struct JoyconBatteryState {
    bool connected{false};
    int level{-1}; // libnx reports 0..4, not an exact percentage.
    bool charging{false};
};
JoyconBatteryState joyconLeft{},joyconRight{};
Uint32 lastJoyconBatteryTick=0;
bool joyconBatteryAvailable=false;
std::string atmosphereVersion;
bool atmosphereVersionAvailable=false,atmosphereVersionChecked=false;
uint64_t sdFreeBytes=0, sdTotalBytes=0;
uint64_t processRamUsed=0,processRamTotal=0;
size_t installedGameCount=0,installedHomebrewCount=0;
bool storageStatsAvailable=false,processRamStatsAvailable=false;
Uint32 lastStorageUpdate=0;
SDL_Thread* storageStatsThread=nullptr;
SDL_mutex* storageStatsMutex=nullptr;
SDL_atomic_t storageStatsStop{};
uint64_t backgroundSdFreeBytes=0,backgroundSdTotalBytes=0;
bool backgroundStorageStatsAvailable=false;

bool tsServiceReady=false, tsInternalOpen=false, tsSocOpen=false;
TsSession tsInternalSession{}, tsSocSession{};
bool systemPcbTemperatureAvailable=false, systemSocTemperatureAvailable=false;
float systemPcbTemperature=-1.0f, systemSocTemperature=-1.0f;
Uint32 lastSystemMonitorUpdate=0;
bool clkrstServiceReady=false, cpuClockOpen=false, gpuClockOpen=false;
ClkrstSession cpuClockSession{}, gpuClockSession{};
bool systemCpuClockAvailable=false, systemGpuClockAvailable=false;
std::uint32_t systemCpuClockHz=0, systemGpuClockHz=0;

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
bool editing=false, dragging=false, listOpen=false, recentOpen=false, systemMenu=false, appSettingsMenu=false, diagnosticsOpen=false, workspaceMenu=false, workspaceSettingsMenu=false, workspacePicker=false, workspacePickerMoveTile=false, deletePrompt=false, running=true;
int recentIndex=0, systemMenuIndex=0, appSettingsIndex=0, appSettingsOffset=0, workspaceMenuIndex=0, workspaceSettingsIndex=0, workspacePickerIndex=0, lastWorkspaceTabTap=-1;
bool nextendoOpen=false;
int nextendoSelected=0;
bool quickSettingsOpen=false;
int quickSettingsIndex=0;
bool quickSettingsSnapshotLoaded=false;
Uint32 quickSettingsSnapshotTick=0;
bool quickAirplaneMode=false,quickWifiEnabled=false,quickBluetoothEnabled=false,quickSoundMuted=false;
bool quickAirplaneAvailable=false,quickWifiAvailable=false,quickBluetoothAvailable=false,
     quickSoundAvailable=false,quickBrightnessAvailable=false,quickInternetConnected=false;
float quickBrightness=0.5f;
unsigned quickWifiStrength=0;
AudioTarget quickAudioTarget=AudioTarget_Speaker;
std::string quickNetworkStatus="NETWORK STATUS UNAVAILABLE";
struct RuntimeFrameMetrics {
    double fps{0.0};
    double averageFrameMs{0.0};
    double averageRenderMs{0.0};
    double maximumFrameMs{0.0};
    std::uint64_t slowFrames{0};
    Uint32 updatedAt{0};
};
RuntimeFrameMetrics runtimeFrameMetrics;
Uint32 lastDiagnosticsRefresh=0;
std::uint64_t diagnosticsCacheBytes=0;
std::size_t diagnosticsCacheFiles=0;
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
u64 g_backendAppTitle=0;            // current foreground/suspended title id
bool g_backendAppSuspended=false;   // true only after a confirmed app_state == 3 snapshot
desknx::ipc::InitialState g_initialState{};
#endif

void setStatus(const std::string& s, Uint32 duration=2500) {
    statusText=s; statusUntil=SDL_GetTicks()+duration;
    desknx::diag::Write("ui","status","message=%s",s.c_str());
}


void refreshQuickSettingsSnapshot(bool force=false) {
    const Uint32 now=SDL_GetTicks();
    if(!force&&quickSettingsSnapshotLoaded&&now-quickSettingsSnapshotTick<3500)return;
    quickSettingsSnapshotTick=now;
    quickNetworkStatus=languageRussian?"СТАТУС СЕТИ НЕДОСТУПЕН":"NETWORK STATUS UNAVAILABLE";
    quickInternetConnected=false;
    quickWifiStrength=0;
    quickAirplaneAvailable=quickWifiAvailable=quickBluetoothAvailable=false;
    quickSoundAvailable=quickBrightnessAvailable=false;

    if(setsysServiceReady) {
        quickWifiAvailable=R_SUCCEEDED(setsysGetWirelessLanEnableFlag(&quickWifiEnabled));
        quickBluetoothAvailable=R_SUCCEEDED(setsysGetBluetoothEnableFlag(&quickBluetoothEnabled));
    }

    NifmInternetConnectionType connectionType=NifmInternetConnectionType_WiFi;
    if(R_SUCCEEDED(nifmInitialize(NifmServiceType_System))) {
        bool wirelessEnabled=true;
        quickAirplaneAvailable=R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&wirelessEnabled));
        if(quickAirplaneAvailable)quickAirplaneMode=!wirelessEnabled;
        NifmInternetConnectionStatus connectionStatus=NifmInternetConnectionStatus_ConnectingUnknown1;
        u32 strength=0;
        if(R_SUCCEEDED(nifmGetInternetConnectionStatus(&connectionType,&strength,&connectionStatus))) {
            quickInternetConnected=connectionStatus==NifmInternetConnectionStatus_Connected;
            quickWifiStrength=std::min<u32>(strength,3);
        }
        nifmExit();
    }

    if(R_SUCCEEDED(audctlInitialize())) {
        AudioTarget target=AudioTarget_Invalid;
        if(R_FAILED(audctlGetActiveOutputTarget(&target))||target==AudioTarget_Invalid)
            (void)audctlGetDefaultTarget(&target);
        if(target==AudioTarget_Invalid)target=AudioTarget_Speaker;
        quickAudioTarget=target;
        quickSoundAvailable=R_SUCCEEDED(audctlIsTargetMute(&quickSoundMuted,target));
        audctlExit();
    }

    if(R_SUCCEEDED(lblInitialize())) {
        quickBrightnessAvailable=R_SUCCEEDED(lblGetCurrentBrightnessSetting(&quickBrightness));
        lblExit();
    }
    if(quickAirplaneAvailable&&quickAirplaneMode)
        quickNetworkStatus=languageRussian?"АВИАРЕЖИМ ВКЛЮЧЁН":"AIRPLANE MODE ON";
    else if(quickInternetConnected)
        quickNetworkStatus=connectionType==NifmInternetConnectionType_Ethernet
            ?(languageRussian?"ПРОВОДНАЯ СЕТЬ ПОДКЛЮЧЕНА":"ETHERNET CONNECTED")
            :(languageRussian?"WI-FI ПОДКЛЮЧЁН  /  СИГНАЛ ":"WI-FI CONNECTED  /  SIGNAL ")+std::to_string(quickWifiStrength)+"/3";
    else if(quickWifiAvailable&&quickWifiEnabled)
        quickNetworkStatus=languageRussian?"WI-FI ВКЛЮЧЁН  /  НЕТ ИНТЕРНЕТА":"WI-FI ENABLED  /  NO INTERNET";
    else if(quickWifiAvailable)
        quickNetworkStatus=languageRussian?"WI-FI ВЫКЛЮЧЕН":"WI-FI DISABLED";
    quickSettingsSnapshotLoaded=true;
}

void openQuickSettings() {
    quickSettingsIndex=0;
    quickSettingsOpen=true;
    refreshQuickSettingsSnapshot(true);
}

void closeQuickSettings() {
    quickSettingsOpen=false;
}

void adjustQuickBrightness(int direction) {
    if(!quickBrightnessAvailable) {
        setStatus("BRIGHTNESS CONTROL UNAVAILABLE",2200);
        return;
    }
    if(R_FAILED(lblInitialize())) {
        setStatus("BRIGHTNESS SERVICE UNAVAILABLE",2200);
        return;
    }
    const float next=std::max(0.10f,std::min(1.0f,
        quickBrightness+static_cast<float>(direction)*0.05f));
    Result rc=lblSetCurrentBrightnessSetting(next);
    if(R_SUCCEEDED(rc))rc=lblApplyCurrentBrightnessSettingToBacklight();
    if(R_SUCCEEDED(rc))(void)lblSaveCurrentSetting();
    lblExit();
    if(R_FAILED(rc))setStatus("BRIGHTNESS CHANGE FAILED",2500);
    else {
        quickBrightness=next;
        quickBrightnessAvailable=true;
        quickSettingsSnapshotLoaded=true;
        quickSettingsSnapshotTick=SDL_GetTicks();
        setStatus("BRIGHTNESS  "+std::to_string(static_cast<int>(std::lround(next*100.0f)))+"%",1600);
    }
}

void toggleQuickSetting(int index) {
    refreshQuickSettingsSnapshot();
    Result rc=0;
    if(index==0) {
        if(!quickSoundAvailable) {
            setStatus("SYSTEM SOUND CONTROL UNAVAILABLE",2500);
            return;
        }
        if(R_FAILED(audctlInitialize())) {
            setStatus("AUDIO SERVICE UNAVAILABLE",2500);
            return;
        }
        rc=audctlSetTargetMute(quickAudioTarget,!quickSoundMuted);
        audctlExit();
        if(R_SUCCEEDED(rc))setStatus(quickSoundMuted?"SOUND ENABLED":"SOUND MUTED",1800);
    } else if(index==1) {
        if(R_FAILED(nifmInitialize(NifmServiceType_System))) {
            setStatus("NETWORK SERVICE UNAVAILABLE",2500);
            return;
        }
        // NIFM's system wireless flag is the console-wide airplane/radio gate.
        rc=nifmSetWirelessCommunicationEnabled(quickAirplaneMode);
        nifmExit();
        if(R_SUCCEEDED(rc))setStatus(quickAirplaneMode?"AIRPLANE MODE DISABLED":"AIRPLANE MODE ENABLED",2000);
    } else if(index==2) {
        if(!setsysServiceReady||!quickWifiAvailable) {
            setStatus("WI-FI CONTROL UNAVAILABLE",2500);
            return;
        }
        if(!quickWifiEnabled&&quickAirplaneMode) {
            if(R_SUCCEEDED(nifmInitialize(NifmServiceType_System))) {
                rc=nifmSetWirelessCommunicationEnabled(true);
                nifmExit();
            } else rc=MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
        }
        if(R_SUCCEEDED(rc))rc=setsysSetWirelessLanEnableFlag(!quickWifiEnabled);
        if(R_SUCCEEDED(rc))setStatus(quickWifiEnabled?"WI-FI DISABLED":"WI-FI ENABLED",1800);
    } else if(index==3) {
        if(!setsysServiceReady||!quickBluetoothAvailable) {
            setStatus("BLUETOOTH CONTROL UNAVAILABLE",2500);
            return;
        }
        if(quickBluetoothEnabled&&appletGetOperationMode()!=AppletOperationMode_Handheld) {
            setStatus("BLUETOOTH IS KEPT ON WHILE DOCKED",2600);
            return;
        }
        rc=setsysSetBluetoothEnableFlag(!quickBluetoothEnabled);
        if(R_SUCCEEDED(rc))setStatus(quickBluetoothEnabled?"BLUETOOTH DISABLED":"BLUETOOTH ENABLED",1800);
    } else if(index==4) {
        adjustQuickBrightness(1);
        return;
    }
    if(R_FAILED(rc))setStatus("QUICK SETTING CHANGE FAILED  0X"+
        [] (Result value) { char buffer[9]{};std::snprintf(buffer,sizeof(buffer),"%08X",static_cast<unsigned int>(value));return std::string(buffer); }(rc),2800);
    else {
        // Reflect the confirmed toggle immediately. The full service snapshot is
        // refreshed on the normal 3.5s cadence, not synchronously after every tap.
        if(index==0)quickSoundMuted=!quickSoundMuted;
        else if(index==1) {
            quickAirplaneMode=!quickAirplaneMode;
            quickInternetConnected=false;
            quickNetworkStatus=quickAirplaneMode?"AIRPLANE MODE ON":
                (quickWifiEnabled?"WI-FI ENABLED  /  CONNECTING":"WI-FI DISABLED");
        } else if(index==2) {
            const bool enablingWifi=!quickWifiEnabled;
            quickWifiEnabled=enablingWifi;
            if(enablingWifi&&quickAirplaneMode)quickAirplaneMode=false;
            quickInternetConnected=false;
            quickNetworkStatus=quickWifiEnabled&&!quickAirplaneMode
                ?"WI-FI ENABLED  /  CONNECTING":"WI-FI DISABLED";
        } else if(index==3)quickBluetoothEnabled=!quickBluetoothEnabled;
        quickSettingsSnapshotLoaded=true;
        quickSettingsSnapshotTick=SDL_GetTicks();
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
void color(Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
void rect(int x,int y,int w,int h,Color c) {
    if(w<=0||h<=0) return;
    if(gVisualEngine.ready() &&
       gVisualEngine.fillRect(x,y,w,h,SDL_Color{c.r,c.g,c.b,c.a}))
        return;
    color(c);
    SDL_Rect r{x,y,w,h};
    SDL_RenderFillRect(renderer,&r);
}
void outline(int x,int y,int w,int h,Color c) {
    if(w<=0||h<=0) return;
    if(gVisualEngine.ready() &&
       gVisualEngine.drawRoundedOutline(
           x,y,w,h,UiCornerRadius,SDL_Color{c.r,c.g,c.b,c.a}))
        return;
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
    if(themeIndex==1) {
        BG={7,13,20,255}; THEME_TOP={16,29,42,255}; THEME_BOTTOM={7,16,25,255};
        MUTED={160,177,191,255}; BLUE={101,184,225,255};
        GREEN={125,194,159,255}; RED={232,123,104,255};
    } else {
        themeIndex=0;
        BG={63,69,78,255}; THEME_TOP={74,79,89,255}; THEME_BOTTOM={45,91,137,255};
        MUTED={215,222,231,255}; BLUE={193,147,255,255};
        GREEN={73,173,12,255}; RED={255,115,123,255};
    }
    if(accentColorIndex>=0 && accentColorIndex<static_cast<int>(COLORS.size()))
        BLUE=COLORS[static_cast<size_t>(accentColorIndex)];
}

// A low-amplitude pulse gives current focus a living highlight without
// scaling/reflowing the desktop or adding extra draw calls.
Color focusAccent() {
    const double phase=static_cast<double>(SDL_GetTicks())/360.0;
    const double pulse=(std::sin(phase)+1.0)*0.5;
    return Color{BLUE.r,BLUE.g,BLUE.b,static_cast<Uint8>(185.0+70.0*pulse)};
}

const char* themeName(int index) {
    static const char* const names[]={"0.1","SOCOMA"};
    return names[index==1?1:0];
}
const char* accentColorName(int index) {
    static const char* const namesEn[]={"Blue","Teal","Violet","Amber","Rose","Sky","Sage","Slate","Coral","Mint","Pink","Yellow"};
    static const char* const namesRu[]={"Синий","Бирюзовый","Фиолетовый","Янтарный","Розовый","Голубой","Шалфей","Сланец","Коралловый","Мятный","Малиновый","Жёлтый"};
    if(index<0||index>=static_cast<int>(COLORS.size()))return languageRussian?"Авто":"Auto";
    return languageRussian?namesRu[index]:namesEn[index];
}
const char* headerStyleName(int value) {
    static const char* const namesEn[]={"Glass","Tactical rail","Minimal"};
    static const char* const namesRu[]={"Стекло","Тактическая","Минимальная"};
    const int index=std::max(0,std::min(2,value));
    return languageRussian?namesRu[index]:namesEn[index];
}
const char* dockStyleName(int value) {
    static const char* const namesEn[]={"Glass dock","Segmented rail","Floating"};
    static const char* const namesRu[]={"Стеклянный","Сегментный","Плавающий"};
    const int index=std::max(0,std::min(2,value));
    return languageRussian?namesRu[index]:namesEn[index];
}
const char* selectionStyleName(int value) {
    static const char* const namesEn[]={"Outline","Accent rail","Pulse","Corner brackets"};
    static const char* const namesRu[]={"Контур","Акцент","Пульс","Углы"};
    const int index=std::max(0,std::min(3,value));
    return languageRussian?namesRu[index]:namesEn[index];
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
    headerStyle=0;
    dockStyle=0;
    surfaceOpacity=255;
    selectionStyle=0;
    motionEffects=false;
    std::string selectedPath;
    const char* candidates[]={SETTINGS_FILE, SETTINGS_BAK};
    for(const char* candidate:candidates) {
        struct stat st{};
        if(stat(candidate,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>8192)
            continue;
        std::ifstream in(candidate,std::ios::binary);
        std::string header;
        if(in && std::getline(in,header) && (header=="DESKNXSET1"||header=="DESKNXSET2")) {
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
    if(!std::getline(in,line)||(line!="DESKNXSET1"&&line!="DESKNXSET2")) {
        applyThemePalette();
        return;
    }
    const bool settingsV2=line=="DESKNXSET2";
    bool modernSteamGridKeySeen=false;
    while(std::getline(in,line)) {
        const size_t eq=line.find('=');
        if(eq==std::string::npos)continue;
        const std::string key=line.substr(0,eq),value=line.substr(eq+1);
        if(key=="theme") {
            char* end=nullptr;
            const long valueLong=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0') {
                if(settingsV2) {
                    if(valueLong>=0&&valueLong<=1)themeIndex=static_cast<int>(valueLong);
                } else if(valueLong>=0&&valueLong<=8)themeIndex=valueLong==8?1:0;
            }
        } else if(key=="accent") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=-1&&parsed<static_cast<long>(COLORS.size()))
                accentColorIndex=static_cast<int>(parsed);
        } else if(key=="steamgriddb_key") {
            modernSteamGridKeySeen=true;
            bool valid=value.size()<=64;
            for(unsigned char ch:value)if(!(std::isalnum(ch)||ch=='_'||ch=='-'))valid=false;
            if(valid)steamGridDbApiKey=value;
        } else if(key=="wallpaper"&&isAllowedMediaPath(value,MediaPickerType::Wallpaper)) {
            wallpaperPath=value;
        } else if(key=="font"&&isAllowedMediaPath(value,MediaPickerType::Font)) {
            fontPath=value;
        } else if(key=="steamgriddbkey"&&!modernSteamGridKeySeen) {
            bool valid=value.size()<=64;
            for(unsigned char ch:value)if(!(std::isalnum(ch)||ch=='_'||ch=='-'))valid=false;
            if(valid)steamGridDbApiKey=value;
        } else if(key=="language") {
            languageRussian=(value=="ru" || value=="russian");
        } else if(key=="iconstyle") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=0&&parsed<=2)iconStyle=static_cast<int>(parsed);
        } else if(key=="headerstyle") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=0&&parsed<=2)headerStyle=static_cast<int>(parsed);
        } else if(key=="dockstyle") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=0&&parsed<=2)dockStyle=static_cast<int>(parsed);
        } else if(key=="surfaceopacity") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=0&&parsed<=255)surfaceOpacity=static_cast<int>(parsed);
        } else if(key=="selectionstyle") {
            char* end=nullptr;
            const long parsed=std::strtol(value.c_str(),&end,10);
            if(end&&*end=='\0'&&parsed>=0&&parsed<=3)selectionStyle=static_cast<int>(parsed);
        } else if(key=="effects") {
            motionEffects=(value=="1"||value=="true"||value=="on");
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

std::string serializeLauncherSettingsSnapshot() {
    std::ostringstream out;
    out<<"DESKNXSET2\n"
       <<"theme="<<themeIndex<<"\n"
       <<"headerstyle="<<headerStyle<<"\n"
       <<"dockstyle="<<dockStyle<<"\n"
       <<"surfaceopacity="<<surfaceOpacity<<"\n"
       <<"selectionstyle="<<selectionStyle<<"\n"
       <<"effects="<<(motionEffects?1:0)<<"\n"
       <<"accent="<<accentColorIndex<<"\n"
       <<"steamgriddb_key="<<steamGridDbApiKey<<"\n"
       <<"wallpaper="<<safeField(wallpaperPath)<<"\n"
       <<"font="<<safeField(fontPath)<<"\n"
       <<"iconpack="<<safeField(iconPackPath)<<"\n"
       <<"language="<<(languageRussian?"ru":"en")<<"\n"
       <<"iconstyle="<<iconStyle<<"\n";
    return out.str();
}
LauncherSettingsSaveStatus writeLauncherSettingsSnapshot(const std::string& contents) {
    if(!sdMounted)return LauncherSettingsSaveStatus::SdUnavailable;
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);mkdir(MEDIA_ROOT,0777);
    std::ofstream out(SETTINGS_TMP,std::ios::binary|std::ios::trunc);
    if(!out)return LauncherSettingsSaveStatus::OpenFailed;
    out.write(contents.data(),static_cast<std::streamsize>(contents.size()));
    out.flush();const bool good=out.good();out.close();
    if(!good){std::remove(SETTINGS_TMP);return LauncherSettingsSaveStatus::WriteFailed;}
    if(!commitSdCard("settings-temp")){std::remove(SETTINGS_TMP);return LauncherSettingsSaveStatus::TempCommitFailed;}
    const auto rc=desknx::safeio::replaceFilePreservingOnFailure(SETTINGS_TMP,SETTINGS_FILE,SETTINGS_BAK,true);
    if(rc!=desknx::safeio::ReplaceStatus::Success){std::remove(SETTINGS_TMP);return LauncherSettingsSaveStatus::PublishFailed;}
    if(!commitSdCard("settings-publish"))return LauncherSettingsSaveStatus::FinalCommitFailed;
    return LauncherSettingsSaveStatus::Success;
}
void reportLauncherSettingsSaveFailure(LauncherSettingsSaveStatus status) {
    switch(status) {
        case LauncherSettingsSaveStatus::SdUnavailable:setStatus("SD CARD REQUIRED TO SAVE APPEARANCE",3000);break;
        case LauncherSettingsSaveStatus::OpenFailed:setStatus("SETTINGS SAVE FAILED",3000);break;
        case LauncherSettingsSaveStatus::WriteFailed:setStatus("SETTINGS WRITE FAILED",3000);break;
        case LauncherSettingsSaveStatus::TempCommitFailed:setStatus("SETTINGS SD COMMIT FAILED",3000);break;
        case LauncherSettingsSaveStatus::PublishFailed:setStatus("SETTINGS PUBLISH FAILED",3000);break;
        case LauncherSettingsSaveStatus::FinalCommitFailed:setStatus("SETTINGS COMMIT FAILED; BACKUP KEPT",4000);break;
        case LauncherSettingsSaveStatus::Success:break;
    }
}
bool saveLauncherSettings() {
    launcherSettingsSavePending=false;
    const auto result=writeLauncherSettingsSnapshot(serializeLauncherSettingsSnapshot());
    if(result!=LauncherSettingsSaveStatus::Success){reportLauncherSettingsSaveFailure(result);return false;}
    return true;
}
int launcherSettingsSaveThreadMain(void* context) {
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    const auto* job=static_cast<const LauncherSettingsSaveJob*>(context);
    launcherSettingsSaveOutcome=job?writeLauncherSettingsSnapshot(job->contents):LauncherSettingsSaveStatus::WriteFailed;
    SDL_AtomicSet(&launcherSettingsSaveDone,1);
    return 0;
}
void scheduleLauncherSettingsSave(Uint32 debounceMs=450) {
    launcherSettingsSavePending=true;launcherSettingsSaveDue=SDL_GetTicks()+debounceMs;
}
void reapLauncherSettingsSave(bool waitForCompletion=false) {
    if(!launcherSettingsSaveThread)return;
    if(!waitForCompletion&&!SDL_AtomicGet(&launcherSettingsSaveDone))return;
    SDL_WaitThread(launcherSettingsSaveThread,nullptr);
    launcherSettingsSaveThread=nullptr;activeLauncherSettingsSaveJob.reset();
    if(launcherSettingsSaveOutcome!=LauncherSettingsSaveStatus::Success) {
        reportLauncherSettingsSaveFailure(launcherSettingsSaveOutcome);
        if(!launcherSettingsSavePending){launcherSettingsSavePending=true;launcherSettingsSaveDue=SDL_GetTicks()+5000;}
    }
}
void pumpLauncherSettingsSave() {
    reapLauncherSettingsSave(false);
    if(launcherSettingsSaveThread||!launcherSettingsSavePending||
       static_cast<Sint32>(SDL_GetTicks()-launcherSettingsSaveDue)<0)return;
    if(!sdMounted){reportLauncherSettingsSaveFailure(LauncherSettingsSaveStatus::SdUnavailable);launcherSettingsSaveDue=SDL_GetTicks()+5000;return;}
    auto job=std::make_unique<LauncherSettingsSaveJob>();
    job->contents=serializeLauncherSettingsSnapshot();activeLauncherSettingsSaveJob=std::move(job);
    launcherSettingsSaveOutcome=LauncherSettingsSaveStatus::Success;SDL_AtomicSet(&launcherSettingsSaveDone,0);
    launcherSettingsSavePending=false;
    launcherSettingsSaveThread=SDL_CreateThread(launcherSettingsSaveThreadMain,"DeskNX settings persistence",activeLauncherSettingsSaveJob.get());
    if(!launcherSettingsSaveThread) {
        activeLauncherSettingsSaveJob.reset();launcherSettingsSavePending=true;
        launcherSettingsSaveDue=SDL_GetTicks()+5000;setStatus("BACKGROUND SETTINGS SAVE UNAVAILABLE",3000);
    }
}
void drainLauncherSettingsSave() {
    reapLauncherSettingsSave(true);
    if(launcherSettingsSavePending)(void)saveLauncherSettings();
}

void loadGameNameOverrides() {
    gameNameOverrides.clear();
    struct stat st{};
    if(stat(GAME_NAME_OVERRIDES_FILE,&st)!=0||!S_ISREG(st.st_mode)||
       st.st_size<=0||st.st_size>512*1024)return;
    std::ifstream input(GAME_NAME_OVERRIDES_FILE,std::ios::binary);
    if(!input)return;
    std::string line;
    size_t count=0;
    while(std::getline(input,line)) {
        if(line.size()>640||++count>4096)break;
        const auto fields=desknx::model::splitTabs(line);
        if(fields.size()!=2||!desknx::model::isTitleId(fields[0])||
           fields[1].empty()||fields[1].size()>124)continue;
        gameNameOverrides[fields[0]]=fields[1];
    }
    desknx::diag::Write("library","name_overrides_loaded","entries=%zu",gameNameOverrides.size());
}

bool saveGameNameOverrides() {
    if(!sdMounted){setStatus("SD CARD REQUIRED TO SAVE GAME NAMES",3000);return false;}
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);mkdir("sdmc:/switch/DeskNX/cache",0777);
    std::ofstream output(GAME_NAME_OVERRIDES_TMP,std::ios::binary|std::ios::trunc);
    if(!output){setStatus("GAME NAME SAVE FAILED",3000);return false;}
    for(const auto& entry:gameNameOverrides) {
        if(!desknx::model::isTitleId(entry.first)||entry.second.empty()||entry.second.size()>124)continue;
        output<<entry.first<<'\t'<<desknx::model::safeField(entry.second)<<'\n';
    }
    output.flush();const bool good=output.good();output.close();
    if(!good){std::remove(GAME_NAME_OVERRIDES_TMP);return false;}
    if(!commitSdCard("game-name-overrides-temp")){
        std::remove(GAME_NAME_OVERRIDES_TMP);return false;
    }
    const auto rc=desknx::safeio::replaceFilePreservingOnFailure(
        GAME_NAME_OVERRIDES_TMP,GAME_NAME_OVERRIDES_FILE,
        "sdmc:/switch/DeskNX/cache/game-overrides.bak",true);
    if(rc!=desknx::safeio::ReplaceStatus::Success){
        std::remove(GAME_NAME_OVERRIDES_TMP);setStatus("GAME NAME PUBLISH FAILED",3000);return false;
    }
    return commitSdCard("game-name-overrides-publish");
}

void applyGameNameOverridesToWorkspaces() {
    for(auto& desk:desks)for(auto& tile:desk.tiles) {
        if(tile.kind!=Kind::Game)continue;
        const auto found=gameNameOverrides.find(tile.data);
        if(found!=gameNameOverrides.end())tile.title=found->second;
    }
}

bool loadWallpaperTexture(const std::string& path) {
    // Validate the selection without decoding it on the UI thread. The actual
    // read/decode is queued by drawWallpaper() and processed by a low-priority
    // worker; the previous wallpaper remains visible until the new one is ready.
    if(path.empty()) return true;
    if(!isAllowedMediaPath(path,MediaPickerType::Wallpaper)) return false;
    struct stat st{};
    return stat(path.c_str(),&st)==0 && S_ISREG(st.st_mode) &&
        st.st_size>0 && st.st_size<=24LL*1024*1024;
}

void drawWallpaper() {
    if(wallpaperPath.empty()) return;
    int width=0,height=0;
    SDL_Texture* texture=gWorkspaceBackgroundCache.get(wallpaperPath,width,height);
    if(!texture||width<=0||height<=0) return;
    const auto fit=desknx::artwork::coverDestination(0,0,SW,SH,width,height);
    SDL_Rect dst{fit.x,fit.y,fit.w,fit.h},viewport{0,0,SW,SH};
    SDL_RenderSetClipRect(renderer,&viewport);
    SDL_RenderCopy(renderer,texture,nullptr,&dst);
    SDL_RenderSetClipRect(renderer,nullptr);
    SDL_SetRenderDrawColor(renderer,3,7,14,92);
    SDL_RenderFillRect(renderer,&viewport);
}

void drawWorkspaceBackground(const Workspace& desk) {
    if(desk.backgroundPath.empty()) { drawWallpaper(); return; }
    int width=0,height=0;
    SDL_Texture* texture=gWorkspaceBackgroundCache.get(
        desk.backgroundPath,width,height);
    if(!texture||width<=0||height<=0) { drawWallpaper(); return; }
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
        if(mediaPickerType==MediaPickerType::GameIcon)pendingGameIconTitleId.clear();
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
bool saveNow();

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
        scheduleLauncherSettingsSave(0);
        setStatus("ICON PACK APPLIED");
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
    if(mediaPickerType==MediaPickerType::GameIcon) {
        if(!desknx::model::isTitleId(pendingGameIconTitleId)||
           !isAllowedMediaPath(selected.path,MediaPickerType::GameIcon)) {
            setStatus("INVALID GAME ICON FILE",2500);return;
        }
        struct stat sourceStat{};
        if(stat(selected.path.c_str(),&sourceStat)!=0||!S_ISREG(sourceStat.st_mode)||
           sourceStat.st_size<4||sourceStat.st_size>2*1024*1024) {
            setStatus("ICON MUST BE A VALID IMAGE UNDER 2 MB",3000);return;
        }
        std::string extension=selected.path.substr(selected.path.find_last_of('.'));
        for(char& ch:extension)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        mkdir(GAME_ICON_OVERRIDES_DIR,0777);
        const std::string target=std::string(GAME_ICON_OVERRIDES_DIR)+"/"+
            pendingGameIconTitleId+extension;
        const std::string temporary=target+".tmp";
        std::ifstream source(selected.path,std::ios::binary);
        std::ofstream destination(temporary,std::ios::binary|std::ios::trunc);
        if(!source||!destination){setStatus("CANNOT READ OR WRITE GAME ICON",3000);return;}
        char buffer[8192];
        while(source) {
            source.read(buffer,sizeof(buffer));
            const std::streamsize bytes=source.gcount();
            if(bytes>0)destination.write(buffer,bytes);
        }
        destination.flush();const bool copied=!source.bad()&&destination.good();destination.close();
        if(!copied){std::remove(temporary.c_str());setStatus("GAME ICON COPY FAILED",3000);return;}
        if(!commitSdCard("game-icon-temp")){std::remove(temporary.c_str());return;}
        const auto replaceRc=desknx::safeio::replaceFilePreservingOnFailure(
            temporary.c_str(),target.c_str(),(target+".bak").c_str(),true);
        if(replaceRc!=desknx::safeio::ReplaceStatus::Success) {
            std::remove(temporary.c_str());setStatus("GAME ICON SAVE FAILED",3000);return;
        }
        for(const char* ext:{".svg",".jpg",".jpeg",".png"}) {
            const std::string obsolete=std::string(GAME_ICON_OVERRIDES_DIR)+"/"+
                pendingGameIconTitleId+ext;
            if(obsolete!=target)std::remove(obsolete.c_str());
        }
        gIconCache.invalidate(Kind::Game,pendingGameIconTitleId);
        pendingGameIconTitleId.clear();
        mediaPickerOpen=false;
        setStatus("CUSTOM GAME ICON ASSIGNED",2500);
        return;
    }
    if(mediaPickerType==MediaPickerType::WorkspaceBackground) {
        if (!isAllowedMediaPath(selected.path,MediaPickerType::WorkspaceBackground)) {
            setStatus("INVALID DESKTOP BACKGROUND",3000); return;
        }
        Workspace& desk=desks[static_cast<size_t>(deskIndex)];
        desk.backgroundPath=selected.path;
        layoutUserModified=true;
        if(saveNow())setStatus("DESKTOP BACKGROUND SAVE QUEUED");
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
            setStatus("FONT BUSY OR LOAD FAILED - CHOOSE AGAIN",3500);
            return;
        }
        fontPath=selected.path;
        scheduleLauncherSettingsSave(0);
        setStatus("CUSTOM FONT LOADING IN BACKGROUND");
        mediaPickerOpen=false;
        return;
    }
    scheduleLauncherSettingsSave(0);
    setStatus("WALLPAPER APPLIED");
    mediaPickerOpen=false;
}

void roundRect(int x,int y,int w,int h,int radius,Color c) {
    if(w<=0||h<=0) return;
    if(gVisualEngine.ready() &&
       gVisualEngine.fillRoundedRect(
           x,y,w,h,radius,SDL_Color{c.r,c.g,c.b,c.a}))
        return;
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
Color themeSurfaceTint(Color source) {
    const Color target=themeIndex==1?Color{17,37,52,255}:Color{112,132,154,255};
    const unsigned amount=themeIndex==1?22u:34u;
    const auto mix=[&](Uint8 from,Uint8 to){return static_cast<Uint8>((static_cast<unsigned>(from)*(255u-amount)+static_cast<unsigned>(to)*amount+127u)/255u);};
    source.r=mix(source.r,target.r);source.g=mix(source.g,target.g);source.b=mix(source.b,target.b);return source;
}
Color themeEdgeTint(Color source) {
    const Color target=themeIndex==1?Color{116,166,194,255}:Color{204,220,237,255};
    const unsigned amount=themeIndex==1?42u:48u;
    const auto mix=[&](Uint8 from,Uint8 to){return static_cast<Uint8>((static_cast<unsigned>(from)*(255u-amount)+static_cast<unsigned>(to)*amount+127u)/255u);};
    source.r=mix(source.r,target.r);source.g=mix(source.g,target.g);source.b=mix(source.b,target.b);return source;
}
void softCard(int x,int y,int w,int h,Color fill,Color edge=Color{255,255,255,20}) {
    fill=themeSurfaceTint(fill);edge=themeEdgeTint(edge);
    const SDL_Rect bounds{x,y,w,h};
    if(gUiComponents.surface(bounds,
        SDL_Color{fill.r,fill.g,fill.b,fill.a},
        SDL_Color{edge.r,edge.g,edge.b,edge.a},
        UiCornerRadius,true,surfaceOpacity)) return;
    const int opacity=std::max(0,std::min(255,surfaceOpacity));
    fill.a=static_cast<Uint8>((static_cast<unsigned int>(fill.a)*opacity)/255u);
    edge.a=static_cast<Uint8>((static_cast<unsigned int>(edge.a)*opacity)/255u);
    roundRect(x,y+5,w,h,UiCornerRadius,
              Color{0,0,0,static_cast<Uint8>((54u*static_cast<unsigned int>(opacity))/255u)});
    roundRect(x,y,w,h,UiCornerRadius,fill);
    roundRect(x+1,y+1,w-2,h-2,UiCornerRadius-1,edge);
}
void uiButton(int x,int y,int w,int h,int radius,
              desknx::ui::ButtonVariant variant,Color fill,Color edge,
              bool active=false,bool focused=false,Color accent=BLUE) {
    fill=themeSurfaceTint(fill);
    edge=themeEdgeTint(edge);
    const SDL_Rect bounds{x,y,w,h};
    (void)gUiComponents.button(bounds,variant,
        desknx::ui::ButtonState{active,focused,false},
        SDL_Color{fill.r,fill.g,fill.b,fill.a},
        SDL_Color{edge.r,edge.g,edge.b,edge.a},radius,
        SDL_Color{accent.r,accent.g,accent.b,accent.a});
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
void textCentered(const std::string& value,int centerX,int y,int scale,
                  Color c,int left,int right) {
    const std::string localized=desknx::i18n::translate(value,languageRussian);
    const SDL_Color tint{c.r,c.g,c.b,c.a};
    if(gFontRenderer.drawCentered(localized,centerX,y,scale,tint,left,right))return;
    const std::string fallback=fallbackAscii(localized);
    const int width=fallback.empty()?0:
        static_cast<int>(fallback.size())*6*scale-scale;
    int px=std::max(left,std::min(right-width,centerX-width/2));
    color(c);
    for(char ch:fallback) {
        if(px+5*scale>right)break;
        const auto rows=glyph(ch);
        for(int ry=0;ry<7;++ry)for(int rx=0;rx<5;++rx)
            if(rows[ry]&(1u<<(4-rx))) {
                SDL_Rect p{px+rx*scale,y+ry*scale,scale,scale};
                SDL_RenderFillRect(renderer,&p);
            }
        px+=6*scale;
    }
}
std::string clockText(bool date=false) {
    std::time_t t=std::time(nullptr); std::tm* tmv=std::localtime(&t); if(!tmv) return "--:--";
    char b[64]{}; std::strftime(b,sizeof(b),date?"%H:%M  %d-%m-%Y":"%H:%M",tmv); return b;
}
desknx::widgets::Config widgetConfig(const Tile& tile) {
    return desknx::widgets::parse(tile.kind,tile.data);
}
bool hasWidgetKind(Kind kind) {
    for(const auto& workspace:desks)
        for(const auto& tile:workspace.tiles)
            if(tile.kind==kind)return true;
    return false;
}
int configuredWidgetRefresh(Kind kind) {
    int seconds=180;
    bool found=false;
    for(const auto& workspace:desks)for(const auto& tile:workspace.tiles) {
        if(tile.kind!=kind)continue;
        seconds=found?std::min(seconds,widgetConfig(tile).refreshSeconds):
                       widgetConfig(tile).refreshSeconds;
        found=true;
    }
    seconds=found?seconds:desknx::widgets::defaults(kind).refreshSeconds;
    // Keep a modest minimum polling interval for the remote Nextendo endpoint.
    return kind==Kind::Nextendo?std::max(30,seconds):seconds;
}
std::string typeName(Kind k) {
    switch(k) {
        case Kind::Game:return "GAME";
        case Kind::Nro:return "HOMEBREW";
        case Kind::Clock:return "WIDGET / CLOCK";
        case Kind::Battery:return "WIDGET / BATTERY";
        case Kind::System:return "WIDGET / SYSTEM MONITOR";
        case Kind::Recent:return "WIDGET / RECENT GAMES";
        case Kind::Storage:return "WIDGET / STORAGE";
        case Kind::Calendar:return "WIDGET / CALENDAR";
        case Kind::Nextendo:return "WIDGET / NEXTENDO";
        case Kind::Favorites:return "WIDGET / FAVORITES";
        case Kind::GameLibrary:return "WIDGET / GAME LIBRARY";
        case Kind::QuickSettings:return "WIDGET / QUICK SETTINGS";
        case Kind::Unsupported:return "UNSUPPORTED";
    }
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
    for(auto& item:all) {
        if(item.kind!=Kind::Game)continue;
        const auto renamed=gameNameOverrides.find(item.data);
        if(renamed!=gameNameOverrides.end())item.title=renamed->second;
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
void cycleTheme(int direction) {
    themeIndex=(themeIndex+direction+2)%2;
    applyThemePalette();
    scheduleLauncherSettingsSave();
    setStatus(languageRussian?"ТЕМА ОБНОВЛЕНА":"THEME UPDATED",1400);
}

void cycleAccent(int direction) {
    constexpr int count=static_cast<int>(COLORS.size())+1; // AUTO + twelve palette colours.
    int index=accentColorIndex+1;
    index=(index+direction+count)%count;
    accentColorIndex=index-1;
    applyThemePalette();
    scheduleLauncherSettingsSave();
    setStatus(languageRussian?"ЦВЕТ АКЦЕНТА ОБНОВЛЁН":"ACCENT COLOUR UPDATED",1400);
}

void editSteamGridDbApiKey() {
    SwkbdConfig config{};
    const Result createRc=swkbdCreate(&config,0);
    if(R_FAILED(createRc)){setStatus("KEYBOARD UNAVAILABLE",2500);return;}
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,languageRussian?
        "Ключ SteamGridDB для обложек":"SteamGridDB artwork API key");
    swkbdConfigSetInitialText(&config,steamGridDbApiKey.c_str());
    char value[96]{};
    std::snprintf(value,sizeof(value),"%s",steamGridDbApiKey.c_str());
    const Result rc=showKeyboard(&config,value,sizeof(value));
    swkbdClose(&config);
    if(R_FAILED(rc))return;
    std::string key=value;
    while(!key.empty()&&std::isspace(static_cast<unsigned char>(key.back())))key.pop_back();
    size_t first=0;while(first<key.size()&&std::isspace(static_cast<unsigned char>(key[first])))++first;
    key.erase(0,first);
    bool valid=key.size()<=64;
    for(unsigned char ch:key)if(!(std::isalnum(ch)||ch=='_'||ch=='-'))valid=false;
    if(!valid){setStatus("INVALID API KEY FORMAT",3000);return;}
    steamGridDbApiKey=key;
    scheduleLauncherSettingsSave(0);
    setStatus(key.empty()?"STEAMGRIDDB KEY CLEARED":"STEAMGRIDDB KEY SAVED",2500);
}

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

using SteamGridArtworkKind = desknx::steamgriddb::ArtworkKind;
using SteamGridCacheKind = desknx::steamgriddb::SteamGridDbCache::ArtworkKind;
std::uint64_t steamGridDbCurrentTitleId=0;
std::string steamGridDbCurrentTitle;
std::string steamGridDbCurrentGameData;

SteamGridArtworkKind steamGridDbKindFromIndex(int index) {
    static const SteamGridArtworkKind kinds[]={SteamGridArtworkKind::Hero,
        SteamGridArtworkKind::Grid,SteamGridArtworkKind::Logo,SteamGridArtworkKind::Icon};
    return kinds[std::max(0,std::min(3,index))];
}
SteamGridCacheKind steamGridDbCacheKind(int index) {
    return static_cast<SteamGridCacheKind>(steamGridDbKindFromIndex(index));
}
const char* steamGridDbKindLabel(int index) {
    static const char* labels[]={"HERO","GRID","LOGO","ICON"};
    return labels[std::max(0,std::min(3,index))];
}
bool steamGridDbGetSelectedGame(Tile& selected,std::uint64_t& titleId) {
    if(listOpen) {
        const auto& available=visibleCatalogueSnapshot();
        if(listIndex<0||static_cast<size_t>(listIndex)>=available.size())return false;
        const Candidate& candidate=available[static_cast<size_t>(listIndex)];
        if(candidate.kind!=Kind::Game||!desknx::model::isTitleId(candidate.data))return false;
        selected=Tile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
    } else {
        if(desks.empty()||deskIndex<0||deskIndex>=static_cast<int>(desks.size()))return false;
        const Workspace& desk=desks[static_cast<size_t>(deskIndex)];
        const int index=itemAt(desk,selectedX,selectedY);
        if(index<0||index>=static_cast<int>(desk.tiles.size()))return false;
        selected=desk.tiles[static_cast<size_t>(index)];
    }
    if(selected.kind!=Kind::Game||!desknx::model::isTitleId(selected.data))return false;
    char* end=nullptr;
    const auto parsed=std::strtoull(selected.data.c_str(),&end,16);
    if(parsed==0||end!=selected.data.c_str()+selected.data.size())return false;
    titleId=static_cast<std::uint64_t>(parsed);
    return true;
}
void steamGridDbDestroyPreview() {
    if(steamGridDbPreviewTexture)SDL_DestroyTexture(steamGridDbPreviewTexture);
    steamGridDbPreviewTexture=nullptr;
    // Cached artwork is persistent; only per-request preview files are disposable.
    if(steamGridDbPreviewLoadedPath.find("/.preview-")!=std::string::npos)
        std::remove(steamGridDbPreviewLoadedPath.c_str());
    steamGridDbPreviewLoadedPath.clear();
    steamGridDbPreviewRevision=UINT64_MAX;
}
void steamGridDbClosePicker() {
    steamGridDbPickerOpen=false;
    steamGridDbOfflineFallback=false;
    steamGridDbTouchTracking=false;
    steamGridDbTouchDragged=false;
    // A confirmed apply must finish after the modal closes. Uncommitted search,
    // asset-list and preview requests are cancelled instead of wasting network
    // time or publishing late state into the next picker session.
    steamGridDbApplyPending=
        gSteamGridDbService.snapshot().status==desknx::steamgriddb::ServiceStatus::Applying;
    if(!steamGridDbApplyPending)gSteamGridDbService.cancelBrowsing();
    if(!steamGridDbApplyPending) {
        steamGridDbDestroyPreview();
    } else {
        // The worker still needs this preview file as its fast first-stage
        // artwork. Drop only the GPU texture while keeping the file alive.
        if(steamGridDbPreviewTexture)SDL_DestroyTexture(steamGridDbPreviewTexture);
        steamGridDbPreviewTexture=nullptr;
        steamGridDbPreviewRevision=UINT64_MAX;
    }
}
void steamGridDbEditApiKey() {
    // Reuse the validated Settings editor so both entry points accept exactly
    // the same API-key format and persist it through one path.
    editSteamGridDbApiKey();
}
void steamGridDbSearch(const std::string& title,bool useSavedMapping=true) {
    Tile tile;
    std::uint64_t id=0;
    if(!steamGridDbGetSelectedGame(tile,id))return;
    steamGridDbCurrentTitleId=id;
    steamGridDbCurrentTitle=title;
    steamGridDbPickerView=SteamGridDbPickerView::Matches;
    steamGridDbOfflineFallback=false;
    steamGridDbMatchIndex=steamGridDbMatchOffset=0;
    steamGridDbAssetOffset=0;
    steamGridDbKindIndex=1;
    if(!gSteamGridDbService.search(id,title,steamGridDbApiKey,useSavedMapping))
        setStatus("STEAMGRIDDB REQUEST COULD NOT START",2500);
}
void steamGridDbEditSearchTitle() {
    Tile tile;
    std::uint64_t id=0;
    if(!steamGridDbGetSelectedGame(tile,id))return;
    SwkbdConfig config{};
    if(R_FAILED(swkbdCreate(&config,0)))return;
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,"Search SteamGridDB by game title");
    swkbdConfigSetInitialText(&config,tile.title.c_str());
    char value[128]{};
    std::snprintf(value,sizeof(value),"%s",tile.title.c_str());
    const Result rc=showKeyboard(&config,value,sizeof(value));
    swkbdClose(&config);
    if(R_SUCCEEDED(rc)&&value[0])steamGridDbSearch(value,false);
}
void steamGridDbOpenPicker() {
    if(!editing){setStatus("ENTER EDIT MODE TO CHANGE GAME COVERS",2400);return;}
    Tile tile;
    std::uint64_t id=0;
    if(!steamGridDbGetSelectedGame(tile,id)){setStatus("SELECT AN INSTALLED GAME FIRST",2200);return;}
    if(!sdMounted){setStatus("STEAMGRIDDB ARTWORK NEEDS AN SD CARD",2800);return;}
    steamGridDbCurrentTitleId=id;
    steamGridDbCurrentTitle=tile.title;
    steamGridDbCurrentGameData=tile.data;
    steamGridDbPickerOpen=true;
    steamGridDbApplyPending=false;
    steamGridDbPreviewAppliedNotified=false;
    steamGridDbOfflineFallback=false;
    steamGridDbPickerView=SteamGridDbPickerView::Matches;
    steamGridDbKindIndex=1;
    steamGridDbMatchIndex=steamGridDbMatchOffset=steamGridDbAssetOffset=0;
    steamGridDbDestroyPreview();
    (void)gSteamGridDbService.cache().ensureDirectories();
    std::string cached=gSteamGridDbService.cache().selectedPath(id);
    if(steamGridDbApiKey.empty()) {
        if(cached.empty()) {
            const auto fallbackKind=gSteamGridDbService.cache().selectedKind(id);
            if(gSteamGridDbService.cache().hasArtwork(id,fallbackKind)) {
                steamGridDbKindIndex=static_cast<int>(fallbackKind);
                cached=gSteamGridDbService.cache().pathFor(id,fallbackKind);
            }
        }
        if(cached.empty()) {
            steamGridDbClosePicker();
            steamGridDbEditApiKey();
            return;
        }
        steamGridDbOfflineFallback=true;
        steamGridDbPickerView=SteamGridDbPickerView::Assets;
        steamGridDbKindIndex=static_cast<int>(gSteamGridDbService.cache().selectedKind(id));
        steamGridDbPreviewLoadedPath=cached;
        setStatus("OFFLINE - CACHED ARTWORK",2500);
        return;
    }
    steamGridDbSearch(tile.title);
}
void steamGridDbChooseMatch(int index) {
    const auto state=gSteamGridDbService.snapshot();
    if(index<0||index>=static_cast<int>(state.games.size()))return;
    const auto match=state.games[static_cast<size_t>(index)];
    steamGridDbKindIndex=1;
    steamGridDbAssetOffset=0;
    steamGridDbPickerView=SteamGridDbPickerView::Assets;
    steamGridDbOfflineFallback=false;
    if(!gSteamGridDbService.loadAssets(state.titleId,match.id,state.title,match.name,
        steamGridDbKindFromIndex(steamGridDbKindIndex),steamGridDbApiKey))
        setStatus("STEAMGRIDDB ARTWORK REQUEST FAILED",2500);
}
void steamGridDbChangeKind(int direction) {
    if(steamGridDbOfflineFallback) {
        steamGridDbKindIndex=(steamGridDbKindIndex+direction+4)%4;
        steamGridDbDestroyPreview();
        if(gSteamGridDbService.cache().hasArtwork(steamGridDbCurrentTitleId,
                                                   steamGridDbCacheKind(steamGridDbKindIndex)))
            steamGridDbPreviewLoadedPath=gSteamGridDbService.cache().pathFor(
                steamGridDbCurrentTitleId,steamGridDbCacheKind(steamGridDbKindIndex));
        return;
    }
    const auto state=gSteamGridDbService.snapshot();
    if(state.titleId==0||state.gameId==0)return;
    steamGridDbKindIndex=(steamGridDbKindIndex+direction+4)%4;
    steamGridDbAssetOffset=0;
    if(!gSteamGridDbService.loadAssets(state.titleId,state.gameId,state.title,state.gameName,
        steamGridDbKindFromIndex(steamGridDbKindIndex),steamGridDbApiKey))
        setStatus("STEAMGRIDDB ARTWORK REQUEST FAILED",2200);
}
void steamGridDbMoveAsset(int direction) {
    const auto state=gSteamGridDbService.snapshot();
    if(state.assets.empty())return;
    const int count=static_cast<int>(state.assets.size());
    const int next=((state.selectedAsset+direction)%count+count)%count;
    if(gSteamGridDbService.selectAsset(next)) {
        if(next<steamGridDbAssetOffset)steamGridDbAssetOffset=next;
        if(next>=steamGridDbAssetOffset+7)steamGridDbAssetOffset=next-6;
    }
}
void steamGridDbRestoreSystem() {
    // Restoring the system cover must not race an already-confirmed artwork
    // apply that is still committing its file and metadata.
    if(gSteamGridDbService.snapshot().status==
       desknx::steamgriddb::ServiceStatus::Applying) {
        setStatus("WAIT FOR THE CURRENT ARTWORK SAVE TO FINISH",2600);
        return;
    }
    Tile tile;
    std::uint64_t id=0;
    if(!steamGridDbGetSelectedGame(tile,id))return;
    auto& cache=gSteamGridDbService.cache();
    if(!cache.hasHero(id)&&!cache.hasGrid(id)&&!cache.hasLogo(id)&&!cache.hasIcon(id)) {
        setStatus("NO CUSTOM COVER TO RESTORE",2400);
        steamGridDbClosePicker();
        return;
    }
    if(cache.restoreSystemArtwork(id)) {
        gIconCache.invalidate(Kind::Game,tile.data);
        setStatus("SYSTEM GAME COVER RESTORED",3000);
    } else setStatus("NO CUSTOM COVER TO RESTORE",2400);
    steamGridDbClosePicker();
}
void steamGridDbUpdatePreview(const std::string& path,std::uint64_t revision) {
    if(path.empty())return;
    if(steamGridDbPreviewTexture&&steamGridDbPreviewLoadedPath==path&&
       steamGridDbPreviewRevision==revision)return;
    steamGridDbDestroyPreview();
    if(!IconCache::validateImagePath(path))return;
    SDL_Surface* surface=IMG_Load(path.c_str());
    if(!surface)return;
    if(surface->w<=0||surface->h<=0||
       static_cast<std::uint64_t>(surface->w)*static_cast<std::uint64_t>(surface->h)>3000000u) {
        SDL_FreeSurface(surface);
        return;
    }
    steamGridDbPreviewTexture=SDL_CreateTextureFromSurface(renderer,surface);
    SDL_FreeSurface(surface);
    if(steamGridDbPreviewTexture) {
        steamGridDbPreviewLoadedPath=path;
        steamGridDbPreviewRevision=revision;
    }
}
void steamGridDbApplyCurrentCached() {
    if(!steamGridDbCurrentTitleId)return;
    const auto kind=steamGridDbCacheKind(steamGridDbKindIndex);
    if(gSteamGridDbService.cache().activateCachedArtwork(steamGridDbCurrentTitleId,kind)) {
        Tile tile;
        std::uint64_t id=0;
        if(steamGridDbGetSelectedGame(tile,id))gIconCache.invalidate(Kind::Game,tile.data);
        setStatus("CACHED "+std::string(steamGridDbKindLabel(steamGridDbKindIndex))+" ARTWORK SELECTED",3000);
        steamGridDbClosePicker();
    } else setStatus("THIS ARTWORK TYPE IS NOT CACHED",2200);
}
void readCatalogueState(bool& ready,bool& complete,bool& truncated,
                        bool* nsAvailable=nullptr,std::uint32_t* nsError=nullptr,
                        size_t* gameCount=nullptr,size_t* nroCount=nullptr);

void pumpSteamGridDbCacheCleanup() {
    if(steamGridDbStartupPruned||!sdMounted||!catalogueMutex)return;
    bool ready=false,complete=false,truncated=false;
    readCatalogueState(ready,complete,truncated);
    if(!complete||truncated)return;
    std::vector<std::uint64_t> installed;
    SDL_LockMutex(catalogueMutex);
    for(const auto& entry:catalogue) {
        if(entry.kind!=Kind::Game||!desknx::model::isTitleId(entry.data))continue;
        char* end=nullptr;
        const auto id=std::strtoull(entry.data.c_str(),&end,16);
        if(id&&end==entry.data.c_str()+entry.data.size())
            installed.push_back(static_cast<std::uint64_t>(id));
    }
    SDL_UnlockMutex(catalogueMutex);
    if(gSteamGridDbService.pruneCache(
        desknx::steamgriddb::SteamGridDbCache::kDefaultMaximumBytes,installed))
        steamGridDbStartupPruned=true;
}
void pumpSteamGridDbPicker() {
    if(!steamGridDbPickerOpen&&!steamGridDbApplyPending)return;
    const auto state=gSteamGridDbService.snapshot();

    if(steamGridDbPickerOpen) {
        if(state.status==desknx::steamgriddb::ServiceStatus::MatchesReady&&
           steamGridDbPickerView==SteamGridDbPickerView::Matches) {
            if(!state.games.empty()) {
                const float first=state.games[0].matchScore;
                const float second=state.games.size()>1?state.games[1].matchScore:0.0f;
                if(first>=0.92f&&(state.games.size()==1||first-second>=0.08f))
                    steamGridDbChooseMatch(0);
                else if(first>=0.86f&&state.games.size()==1)
                    steamGridDbChooseMatch(0);
            }
        } else if(state.status==desknx::steamgriddb::ServiceStatus::LoadingAssets&&
                  state.gameId!=0&&steamGridDbPickerView==SteamGridDbPickerView::Matches) {
            steamGridDbPickerView=SteamGridDbPickerView::Assets;
        } else if(state.status==desknx::steamgriddb::ServiceStatus::Browsing&&
                  steamGridDbPickerView==SteamGridDbPickerView::Matches) {
            steamGridDbPickerView=SteamGridDbPickerView::Assets;
        } else if(state.status==desknx::steamgriddb::ServiceStatus::Error&&
                  !steamGridDbOfflineFallback) {
            const std::string cached=gSteamGridDbService.cache().selectedPath(state.titleId);
            if(!cached.empty()) {
                steamGridDbOfflineFallback=true;
                steamGridDbPickerView=SteamGridDbPickerView::Assets;
                steamGridDbKindIndex=static_cast<int>(
                    gSteamGridDbService.cache().selectedKind(state.titleId));
                steamGridDbPreviewLoadedPath=cached;
                setStatus("OFFLINE - USING CACHED STEAMGRIDDB ARTWORK",3500);
            }
        }

        if(state.previewRevision>0&&!state.previewPath.empty()&&!steamGridDbOfflineFallback)
            steamGridDbUpdatePreview(state.previewPath,state.previewRevision);
        else if(!steamGridDbPreviewLoadedPath.empty()&&!steamGridDbPreviewTexture)
            steamGridDbUpdatePreview(steamGridDbPreviewLoadedPath,0);
    }

    if(steamGridDbApplyPending&&state.status==desknx::steamgriddb::ServiceStatus::Applying&&
       state.previewApplied&&!steamGridDbPreviewAppliedNotified) {
        steamGridDbPreviewAppliedNotified=true;
        if(!steamGridDbCurrentGameData.empty())
            gIconCache.invalidate(Kind::Game,steamGridDbCurrentGameData);
        setStatus(languageRussian?"МИНИАТЮРА ПРИМЕНЕНА — ЗАГРУЖАЕТСЯ ОРИГИНАЛ":
            "PREVIEW APPLIED — FULL ARTWORK IS DOWNLOADING",4200);
        if(steamGridDbPickerOpen)steamGridDbClosePicker();
    }

    if(steamGridDbApplyPending&&state.status==desknx::steamgriddb::ServiceStatus::Error) {
        steamGridDbApplyPending=false;
        setStatus(languageRussian?"НЕ УДАЛОСЬ ПРИМЕНИТЬ ОБЛОЖКУ":
            "STEAMGRIDDB DOWNLOAD FAILED",3500);
        steamGridDbDestroyPreview();
    }

    if(state.status==desknx::steamgriddb::ServiceStatus::Applied&&steamGridDbApplyPending) {
        if(!steamGridDbCurrentGameData.empty())
            gIconCache.invalidate(Kind::Game,steamGridDbCurrentGameData);

        bool ready=false,complete=false,truncated=false;
        readCatalogueState(ready,complete,truncated);
        if(complete&&!truncated) {
            std::vector<std::uint64_t> installed;
            if(catalogueMutex)SDL_LockMutex(catalogueMutex);
            for(const auto& entry:catalogue) {
                if(entry.kind!=Kind::Game||!desknx::model::isTitleId(entry.data))continue;
                char* end=nullptr;
                const auto parsed=std::strtoull(entry.data.c_str(),&end,16);
                if(parsed&&end==entry.data.c_str()+entry.data.size())
                    installed.push_back(static_cast<std::uint64_t>(parsed));
            }
            if(catalogueMutex)SDL_UnlockMutex(catalogueMutex);
            (void)gSteamGridDbService.pruneCache(
                desknx::steamgriddb::SteamGridDbCache::kDefaultMaximumBytes,installed);
        }
        steamGridDbApplyPending=false;
        setStatus(state.fullResolutionApplied
            ?(languageRussian?"ОБЛОЖКА ПРИМЕНЕНА":"ARTWORK APPLIED")
            :(languageRussian?"МИНИАТЮРА СОХРАНЕНА — ОРИГИНАЛ НЕ ЗАГРУЗИЛСЯ":
                "PREVIEW SAVED — FULL-RES DOWNLOAD FAILED"),4000);
        if(steamGridDbPickerOpen)steamGridDbClosePicker();
        else steamGridDbDestroyPreview();
        steamGridDbPreviewAppliedNotified=false;
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
                        bool* nsAvailable,std::uint32_t* nsError,
                        size_t* gameCount,size_t* nroCount) {
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
bool nextendoCancelRequested(void*) {
    return SDL_AtomicGet(&nextendoStop) != 0;
}
int nextendoThreadMain(void*) {
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    bool networkReady = false;
    unsigned int consecutiveFailures = 0;
    SDL_AtomicSet(&nextendoNetworkReady,0);
    SDL_AtomicSet(&nextendoConsecutiveFailures,0);
    std::uint64_t nextAttempt = SDL_GetTicks64() + 8000;
    while (!SDL_AtomicGet(&nextendoStop)) {
        const std::uint64_t now = SDL_GetTicks64();
        if (now >= nextAttempt) {
            if (!networkReady) {
                networkReady = desknx::nextendo::initializeNetwork();
                SDL_AtomicSet(&nextendoNetworkReady,networkReady?1:0);
                desknx::diag::Write("nextendo", networkReady ? "network_ready" : "network_init_failed",
                    "retry_ms=%u", networkReady ? 0u : 30000u);
                if (!networkReady) {
                    nextAttempt = now + 30000;
                    SDL_Delay(250);
                    continue;
                }
            }
            desknx::nextendo::PlayerSnapshot snapshot;
            const bool ok = desknx::nextendo::fetchSnapshot(
                snapshot,nextendoCancelRequested,nullptr);
            if (ok && nextendoMutex) {
                SDL_LockMutex(nextendoMutex);
                const bool applied = nextendoFeed.apply(snapshot, SDL_GetTicks64());
                if (applied)
                    nextendoPopularCache=desknx::nextendo::rankPopularGames(snapshot);
                SDL_UnlockMutex(nextendoMutex);
                consecutiveFailures = 0;
                SDL_AtomicSet(&nextendoConsecutiveFailures,0);
                SDL_AtomicSet(&nextendoLastSuccessTick,
                    static_cast<int>(static_cast<Uint32>(SDL_GetTicks())));
                desknx::diag::Write("nextendo", applied ? "snapshot_updated" : "snapshot_rejected",
                    "titles=%zu total=%u", snapshot.perTitle.size(), snapshot.total);
            } else {
                ++consecutiveFailures;
                SDL_AtomicSet(&nextendoConsecutiveFailures,
                    static_cast<int>(consecutiveFailures));
                desknx::diag::Write("nextendo", "snapshot_failed",
                    "network_ready=%d consecutive_failures=%u",
                    networkReady ? 1 : 0,consecutiveFailures);
                // curl/socket sessions occasionally become unusable after a
                // long Wi-Fi outage. Recreate the whole network stack instead
                // of endlessly polling with the stale session.
                if(consecutiveFailures>=3) {
                    desknx::diag::Write("nextendo","network_recovery",
                        "action=reinitialize failures=%u",consecutiveFailures);
                    if(networkReady)desknx::nextendo::shutdownNetwork();
                    networkReady=false;
                    SDL_AtomicSet(&nextendoNetworkReady,0);
                    consecutiveFailures=0;
                    SDL_AtomicSet(&nextendoConsecutiveFailures,0);
                    nextAttempt=now+8000;
                    SDL_Delay(250);
                    continue;
                }
            }
            const int configuredMs=SDL_AtomicGet(&nextendoConfiguredRefreshMs);
            const std::uint32_t refreshMs=configuredMs>=30000?
                static_cast<std::uint32_t>(configuredMs):
                static_cast<std::uint32_t>(desknx::nextendo::RefreshIntervalMs);
            nextAttempt = now + refreshMs;
        }
        SDL_Delay(250);
    }
    if (networkReady) desknx::nextendo::shutdownNetwork();
    SDL_AtomicSet(&nextendoNetworkReady,0);
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

bool nextendoPlayersSnapshotForTitle(const std::string& titleId,unsigned& players) {
    players=0;
    if(!nextendoMutex||!desknx::model::isTitleId(titleId))return false;
    char* end=nullptr;
    const unsigned long long id=std::strtoull(titleId.c_str(),&end,16);
    if(end!=titleId.c_str()+titleId.size()||id==0)return false;
    bool fresh=false;
    SDL_LockMutex(nextendoMutex);
    const std::uint64_t now=SDL_GetTicks64();
    fresh=nextendoFeed.hasTitleSnapshot(static_cast<std::uint64_t>(id),now);
    if(fresh)players=nextendoFeed.playersFor(static_cast<std::uint64_t>(id),now);
    SDL_UnlockMutex(nextendoMutex);
    return fresh;
}


desknx::nextendo::PopularGames nextendoPopularSnapshot() {
    desknx::nextendo::PopularGames result{};
    if (!nextendoMutex) return result;
    SDL_LockMutex(nextendoMutex);
    if (nextendoFeed.hasTotal(SDL_GetTicks64()))
        result=nextendoPopularCache;
    SDL_UnlockMutex(nextendoMutex);
    return result;
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
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
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
    g_backendAppSuspended=state.app_state==3&&state.app_title_id!=0;
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
            if(payload.title_id==g_backendAppTitle) {
                g_backendAppTitle=0;
                g_backendAppSuspended=false;
            }
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

bool readHistoryFile(const char* path,std::vector<HistoryEntry>& output) {
    struct stat historyStat{};
    constexpr size_t MaxHistoryBytes=64*1024;
    if(!path||stat(path,&historyStat)!=0||!S_ISREG(historyStat.st_mode)||
       historyStat.st_size<=0||static_cast<uint64_t>(historyStat.st_size)>MaxHistoryBytes)
        return false;
    std::ifstream input(path,std::ios::binary);
    std::string line;
    if(!input||!std::getline(input,line)||line.size()>32||line!="DESKNXH1")
        return false;
    std::vector<HistoryEntry> parsed;
    while(std::getline(input,line)&&parsed.size()<10) {
        if(line.size()>2048)continue;
        auto fields=splitTabs(line);
        if(fields.size()!=3||fields[0].size()>16||fields[1].empty()||
           fields[1].size()>512||fields[2].empty()||fields[2].size()>512)continue;
        Kind kind=parseKind(fields[0]);
        if(kind!=Kind::Game&&kind!=Kind::Nro)continue;
        if(kind==Kind::Game&&!desknx::model::isTitleId(fields[2]))continue;
        if(kind==Kind::Nro&&!isSafeNroPath(fields[2]))continue;
        parsed.push_back({kind,fields[1],fields[2]});
    }
    if(input.bad())return false;
    output=std::move(parsed);
    return true;
}

void loadHistory() {
    if(!sdMounted)return;
    std::vector<HistoryEntry> loaded;
    if(readHistoryFile(HISTORY_FILE,loaded)) {
        recentLaunches=std::move(loaded);
        desknx::diag::Write("history","loaded","path=%s entries=%zu",HISTORY_FILE,recentLaunches.size());
    } else if(readHistoryFile(HISTORY_BAK_FILE,loaded)) {
        recentLaunches=std::move(loaded);
        desknx::diag::Write("history","backup_restored","path=%s entries=%zu",HISTORY_BAK_FILE,recentLaunches.size());
        setStatus("RECENT HISTORY RESTORED FROM BACKUP",4000);
    } else {
        desknx::diag::Write("history","load_empty","primary=%s backup=%s",
            HISTORY_FILE,HISTORY_BAK_FILE);
        return;
    }
    if(!recentLaunches.empty())lastLaunched=recentLaunches.front().title;
}
void saveHistory() {
    if(!sdMounted)return;
    const Uint64 started=SDL_GetTicks64();
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    std::ofstream out(HISTORY_TMP,std::ios::binary|std::ios::trunc);
    if(!out){setStatus("RECENT HISTORY SAVE FAILED",3000);return;}
    out<<"DESKNXH1\n";
    for(const auto& e:recentLaunches)out<<typeCode(e.kind)<<'\t'<<safeField(e.title)<<'\t'<<safeField(e.data)<<'\n';
    out.flush();bool good=out.good();out.close();
    if(!good){std::remove(HISTORY_TMP);setStatus("RECENT HISTORY WRITE FAILED",3000);return;}
    if(!commitSdCard("history-temp")) {
        std::remove(HISTORY_TMP);
        setStatus("RECENT HISTORY SD COMMIT FAILED",3000);
        return;
    }
    const auto replaceStatus = desknx::safeio::replaceFilePreservingOnFailure(
        HISTORY_TMP, HISTORY_FILE, HISTORY_BAK_FILE, true);
    if (replaceStatus != desknx::safeio::ReplaceStatus::Success) {
        std::remove(HISTORY_TMP);
        if (replaceStatus == desknx::safeio::ReplaceStatus::RollbackFailed)
            setStatus("RECENT HISTORY RECOVERY NEEDED: KEEP recent.bak", 5000);
        else
            setStatus("RECENT HISTORY RENAME FAILED", 3000);
        return;
    }
    if(!commitSdCard("history-publish")) {
        desknx::diag::Write("history","save_failed","phase=publish_commit entries=%zu",recentLaunches.size());
        setStatus("RECENT HISTORY COMMIT FAILED; BACKUP KEPT",4000);
    } else {
        desknx::diag::Write("history","saved","entries=%zu elapsed_ms=%llu",
            recentLaunches.size(),static_cast<unsigned long long>(SDL_GetTicks64()-started));
    }
}
void recordRecentLaunch(const Tile& tile) {
    if(tile.kind!=Kind::Game&&tile.kind!=Kind::Nro)return;
    recentLaunches.erase(std::remove_if(recentLaunches.begin(),recentLaunches.end(),
        [&](const HistoryEntry& e){return e.kind==tile.kind&&e.data==tile.data;}),recentLaunches.end());
    recentLaunches.insert(recentLaunches.begin(),{tile.kind,tile.title,tile.data});
    ++recentLaunchRevision;
    if(recentLaunches.size()>10)recentLaunches.resize(10);
    lastLaunched=tile.title;saveHistory();
}

bool readFavoritesFile(const char* path,std::vector<std::string>& output) {
    struct stat st{};
    constexpr std::uint64_t MaxFavoritesBytes=32u*1024u;
    if(!path||stat(path,&st)!=0||!S_ISREG(st.st_mode)||
       st.st_size<=0||static_cast<std::uint64_t>(st.st_size)>MaxFavoritesBytes)return false;
    std::ifstream input(path,std::ios::binary);
    std::string line;
    if(!input||!std::getline(input,line)||line!="DESKNXF1")return false;
    std::vector<std::string> parsed;
    while(std::getline(input,line)&&parsed.size()<256) {
        if(line.size()!=16||!desknx::model::isTitleId(line))continue;
        if(std::find(parsed.begin(),parsed.end(),line)==parsed.end())
            parsed.push_back(line);
    }
    if(input.bad())return false;
    output=std::move(parsed);
    return true;
}
void loadFavorites() {
    if(!sdMounted)return;
    std::vector<std::string> loaded;
    if(readFavoritesFile(FAVORITES_FILE,loaded)) {
        favoriteTitleIds=std::move(loaded);
    } else if(readFavoritesFile(FAVORITES_BAK,loaded)) {
        favoriteTitleIds=std::move(loaded);
        setStatus("FAVORITES RESTORED FROM BACKUP",3500);
    }
}
bool saveFavorites() {
    if(!sdMounted)return false;
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    std::ofstream out(FAVORITES_TMP,std::ios::binary|std::ios::trunc);
    if(!out){setStatus("FAVORITES SAVE FAILED",3000);return false;}
    out<<"DESKNXF1\n";
    for(const auto& id:favoriteTitleIds)
        if(desknx::model::isTitleId(id))out<<id<<"\n";
    out.flush();
    bool good=out.good();
    out.close();
    if(!good){std::remove(FAVORITES_TMP);setStatus("FAVORITES WRITE FAILED",3000);return false;}
    if(!commitSdCard("favorites-temp")) {
        std::remove(FAVORITES_TMP);
        setStatus("FAVORITES SD COMMIT FAILED",3000);
        return false;
    }
    const auto result=desknx::safeio::replaceFilePreservingOnFailure(
        FAVORITES_TMP,FAVORITES_FILE,FAVORITES_BAK,true);
    if(result!=desknx::safeio::ReplaceStatus::Success) {
        std::remove(FAVORITES_TMP);
        setStatus("FAVORITES RENAME FAILED; BACKUP KEPT",3500);
        return false;
    }
    if(!commitSdCard("favorites-publish")) {
        setStatus("FAVORITES COMMIT FAILED; BACKUP KEPT",3500);
        return false;
    }
    return true;
}
bool toggleFavorite(const Tile& tile) {
    if(tile.kind!=Kind::Game||!desknx::model::isTitleId(tile.data)) {
        setStatus("FAVORITES SUPPORT INSTALLED GAMES",2200);
        return false;
    }
    const auto found=std::find(favoriteTitleIds.begin(),favoriteTitleIds.end(),tile.data);
    if(found!=favoriteTitleIds.end()) {
        const std::size_t oldIndex=static_cast<std::size_t>(
            std::distance(favoriteTitleIds.begin(),found));
        favoriteTitleIds.erase(found);
        if(saveFavorites()) {
            setStatus("REMOVED FROM FAVORITES: "+tile.title);
        } else {
            favoriteTitleIds.insert(favoriteTitleIds.begin()+
                static_cast<std::ptrdiff_t>(std::min(oldIndex,favoriteTitleIds.size())),tile.data);
            return false;
        }
        return true;
    }
    if(favoriteTitleIds.size()>=256) {
        setStatus("FAVORITES LIMIT REACHED",2500);
        return false;
    }
    favoriteTitleIds.insert(favoriteTitleIds.begin(),tile.data);
    if(saveFavorites()) {
        setStatus("ADDED TO FAVORITES: "+tile.title);
        return true;
    }
    favoriteTitleIds.erase(std::remove(favoriteTitleIds.begin(),
        favoriteTitleIds.end(),tile.data),favoriteTitleIds.end());
    return false;
}
std::vector<Tile> favoriteTiles() {
    const auto all=catalogueSnapshot();
    std::vector<Tile> result;
    result.reserve(favoriteTitleIds.size());
    for(const auto& id:favoriteTitleIds) {
        const auto it=std::find_if(all.begin(),all.end(),[&](const Candidate& entry) {
            return entry.kind==Kind::Game&&entry.data==id;
        });
        if(it!=all.end())result.push_back(Tile{Kind::Game,it->title,it->data,0,0,1,1,it->color});
    }
    return result;
}
void openFavorites() {
    favoritesView=favoriteTiles();
    favoritesIndex=0;
    favoritesOffset=0;
    if(favoritesView.empty()) {
        setStatus(favoriteTitleIds.empty()?"NO FAVORITE GAMES YET":"FAVORITE GAMES ARE NOT IN THE CURRENT CATALOGUE");
        return;
    }
    favoritesOpen=true;
}

std::vector<Tile> gameLibraryTiles(std::size_t limit,bool recentFirst=true) {
    const auto all=catalogueSnapshot();
    std::vector<Tile> result;
    result.reserve(std::min<std::size_t>(limit,all.size()));
    auto appendById=[&](const std::string& id) {
        if(result.size()>=limit)return;
        if(std::any_of(result.begin(),result.end(),[&](const Tile& tile){return tile.data==id;}))return;
        const auto found=std::find_if(all.begin(),all.end(),[&](const Candidate& item) {
            return item.kind==Kind::Game&&item.data==id;
        });
        if(found!=all.end())
            result.push_back(Tile{Kind::Game,found->title,found->data,0,0,1,1,found->color});
    };
    // Recent launches and favourites can be surfaced first; users may switch
    // to a strictly alphabetical catalogue from the widget editor.
    if(recentFirst) {
        for(const auto& entry:recentLaunches)if(entry.kind==Kind::Game)appendById(entry.data);
        for(const auto& id:favoriteTitleIds)appendById(id);
    }
    std::vector<Candidate> games;
    for(const auto& item:all)if(item.kind==Kind::Game)games.push_back(item);
    std::sort(games.begin(),games.end(),[](const Candidate& a,const Candidate& b) {
        return a.title<b.title;
    });
    for(const auto& item:games)appendById(item.data);
    if(!recentFirst)
        for(const auto& id:favoriteTitleIds)appendById(id);
    if(result.size()>limit)result.resize(limit);
    return result;
}

struct NextendoWidgetSnapshot {
    bool fresh{false};
    unsigned total{0};
    unsigned activeTitles{0};
    unsigned topPlayers{0};
    std::string topGame;
};
NextendoWidgetSnapshot nextendoWidgetSnapshot() {
    static NextendoWidgetSnapshot cached;
    static std::uint64_t cachedAt=0;
    const std::uint64_t now=SDL_GetTicks64();
    if(cachedAt!=0&&now-cachedAt<5000)return cached;
    NextendoWidgetSnapshot result;
    const auto games=catalogueSnapshot();
    if(nextendoMutex)SDL_LockMutex(nextendoMutex);
    result.fresh=nextendoFeed.hasFreshSnapshot(now);
    if(result.fresh) {
        result.total=nextendoFeed.total(now);
        result.activeTitles=nextendoFeed.activeTitleCount(now);
        for(const auto& game:games) {
            if(game.kind!=Kind::Game||!desknx::model::isTitleId(game.data))continue;
            const auto players=nextendoFeed.playersFor(
                static_cast<desknx::nextendo::TitleId>(std::strtoull(game.data.c_str(),nullptr,16)),now);
            if(players>result.topPlayers) {
                result.topPlayers=players;
                result.topGame=game.title;
            }
        }
    }
    if(nextendoMutex)SDL_UnlockMutex(nextendoMutex);
    cached=result;
    cachedAt=now;
    return result;
}
std::vector<HistoryEntry> recentEntriesForModal() {
    if(!recentGamesOnly)return recentLaunches;
    std::vector<HistoryEntry> games;
    for(const auto& entry:recentLaunches)if(entry.kind==Kind::Game)games.push_back(entry);
    return games;
}
void openRecentGames() {
    recentGamesOnly=true;
    recentIndex=0;
    recentOpen=true;
}
void openRecentAll() {
    recentGamesOnly=false;
    recentIndex=0;
    recentOpen=true;
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
    std::ofstream out(ACTIVE_WORKSPACE_TMP,std::ios::binary|std::ios::trunc);
    if(!out) return false;
    out.write(state.data(),static_cast<std::streamsize>(state.size()));
    out.flush();
    const bool good=out.good();
    out.close();
    if(!good) {
        std::remove(ACTIVE_WORKSPACE_TMP);
        return false;
    }
    // This is a tiny, replaceable navigation marker. Flushing the temp file
    // to the SD controller and then committing it before rename caused two
    // expensive synchronous SD commits for every desktop switch. The atomic
    // replace retains the previous marker as .bak; one commit after publish
    // makes the new marker durable without blocking the UI twice.
    const auto replaceStatus = desknx::safeio::replaceFilePreservingOnFailure(
        ACTIVE_WORKSPACE_TMP,ACTIVE_WORKSPACE_FILE,ACTIVE_WORKSPACE_BAK,true);
    if(replaceStatus != desknx::safeio::ReplaceStatus::Success)
        return false;
    const bool committed=commitSdCard("active-workspace-publish");
    if(committed) {
        persistedActiveWorkspaceMarker=state;
        activeWorkspaceMarkerKnown=true;
        activeWorkspaceMarkerPending=false;
        activeWorkspaceMarkerDue=0;
    }
    return committed;
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

bool loadActiveWorkspaceState() {
    if(!sdMounted || desks.empty()) return false;
    const char* candidates[]={ACTIVE_WORKSPACE_FILE,ACTIVE_WORKSPACE_BAK};
    for(std::size_t candidateIndex=0;candidateIndex<2;++candidateIndex) {
        const char* path=candidates[candidateIndex];
        struct stat st{};
        if(stat(path,&st)!=0 || !S_ISREG(st.st_mode) ||
           st.st_size<=0 || st.st_size>512) continue;
        std::ifstream in(path,std::ios::binary);
        if(!in)continue;
        std::ostringstream contents;
        contents<<in.rdbuf();
        if(in.bad())continue;
        int restored=deskIndex;
        if(!desknx::layout::parseActiveWorkspaceState(contents.str(),desks,restored))
            continue;
        deskIndex=restored;
        if(candidateIndex==0) {
            persistedActiveWorkspaceMarker=contents.str();
            activeWorkspaceMarkerKnown=true;
        } else {
            persistedActiveWorkspaceMarker.clear();
            activeWorkspaceMarkerKnown=false;
        }
        activeWorkspaceMarkerPending=false;
        activeWorkspaceMarkerDue=0;
        selectedX=selectedY=0;
        if(candidateIndex!=0) {
            desknx::diag::Write("layout","active_workspace_backup_restored",
                "index=%d name=%s",deskIndex,desks[static_cast<size_t>(deskIndex)].name.c_str());
            setStatus("ACTIVE DESKTOP RESTORED FROM BACKUP",4000);
        } else {
            desknx::diag::Write("layout","active_workspace_restored",
                "index=%d name=%s",deskIndex,desks[static_cast<size_t>(deskIndex)].name.c_str());
        }
        return true;
    }
    return false;
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
    std::ofstream f(CFG_TMP,std::ios::binary|std::ios::trunc);
    if(!f){setStatus("SAVE FAILED: SD CARD NOT WRITABLE",3500);return false;}
    f.write(serialized.data(),static_cast<std::streamsize>(serialized.size()));
    f.flush();const bool good=f.good();f.close();
    if(!good){std::remove(CFG_TMP);setStatus("SAVE FAILED: WRITE ERROR",3500);return false;}
    if(!commitSdCard("layout-temp")) {
        std::remove(CFG_TMP);
        setStatus("LAYOUT SD COMMIT FAILED; OLD FILE KEPT",4000);
        return false;
    }
    const auto replaceStatus = desknx::safeio::replaceFilePreservingOnFailure(
        CFG_TMP, CFG_FILE, CFG_BAK_FILE, true);
    if (replaceStatus != desknx::safeio::ReplaceStatus::Success) {
        std::remove(CFG_TMP);
        if (replaceStatus == desknx::safeio::ReplaceStatus::RollbackFailed) {
            layoutSaveBlocked = true;
            setStatus("LAYOUT RECOVERY NEEDED: KEEP layout.bak", 5000);
        } else {
            setStatus("SAVE FAILED: RENAME ERROR", 3500);
        }
        return false;
    }
    if(!commitSdCard("layout-publish")) {
        layoutSaveBlocked = true; // retain layout.bak as the last known-good copy
        setStatus("LAYOUT COMMIT FAILED; KEEP layout.bak",5000);
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
    std::ofstream out(ACTIVE_WORKSPACE_TMP,std::ios::binary|std::ios::trunc);
    if(!out) return false;
    out.write(job.activeWorkspaceState.data(),
              static_cast<std::streamsize>(job.activeWorkspaceState.size()));
    out.flush();
    const bool good=out.good();
    out.close();
    if(!good) {
        std::remove(ACTIVE_WORKSPACE_TMP);
        return false;
    }
    const auto status=desknx::safeio::replaceFilePreservingOnFailure(
        ACTIVE_WORKSPACE_TMP,ACTIVE_WORKSPACE_FILE,ACTIVE_WORKSPACE_BAK,true);
    if(status!=desknx::safeio::ReplaceStatus::Success) {
        std::remove(ACTIVE_WORKSPACE_TMP);
        return false;
    }
    return commitSdCard("active-workspace-async-publish");
}

int layoutSaveThreadMain(void* context) {
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    const auto* job=static_cast<const LayoutSaveJob*>(context);
    LayoutSaveOutcome outcome{};
    outcome.markerOnly=job->markerOnly;
    outcome.shapeHash=job->shapeHash;

    bool primaryOk=true;
    if(job->rewritePrimary) {
        mkdir("sdmc:/switch",0777);
        mkdir(CFG_DIR,0777);
        std::ofstream out(CFG_TMP,std::ios::binary|std::ios::trunc);
        if(!out) {
            primaryOk=false;
            std::snprintf(outcome.message,sizeof(outcome.message),
                          "SAVE FAILED: SD CARD NOT WRITABLE");
        } else {
            out.write(job->serializedLayout.data(),
                      static_cast<std::streamsize>(job->serializedLayout.size()));
            out.flush();
            const bool good=out.good();
            out.close();
            if(!good) {
                std::remove(CFG_TMP);
                primaryOk=false;
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "SAVE FAILED: WRITE ERROR");
            } else if(!commitSdCard("layout-async-temp")) {
                std::remove(CFG_TMP);
                primaryOk=false;
                std::snprintf(outcome.message,sizeof(outcome.message),
                              "LAYOUT SD COMMIT FAILED; OLD FILE KEPT");
            } else {
                const auto replaceStatus=desknx::safeio::replaceFilePreservingOnFailure(
                    CFG_TMP,CFG_FILE,CFG_BAK_FILE,true);
                if(replaceStatus!=desknx::safeio::ReplaceStatus::Success) {
                    std::remove(CFG_TMP);
                    primaryOk=false;
                    outcome.blocked=
                        replaceStatus==desknx::safeio::ReplaceStatus::RollbackFailed;
                    std::snprintf(outcome.message,sizeof(outcome.message),
                        outcome.blocked
                            ? "LAYOUT RECOVERY NEEDED: KEEP layout.bak"
                            : "SAVE FAILED: RENAME ERROR");
                } else if(!commitSdCard("layout-async-publish")) {
                    primaryOk=false;
                    outcome.blocked=true;
                    std::snprintf(outcome.message,sizeof(outcome.message),
                                  "LAYOUT COMMIT FAILED; KEEP layout.bak");
                } else {
                    outcome.primaryWritten=true;
                }
            }
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
    struct stat fileStat{};
    if (!path || stat(path, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) ||
        fileStat.st_size <= 0 || fileStat.st_size > 1024 * 1024) {
        desknx::diag::Write("layout","load_failed","path=%s reason=stat_or_size",
            path?path:"null");
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        desknx::diag::Write("layout","load_failed","path=%s reason=open",path);
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        desknx::diag::Write("layout","load_failed","path=%s reason=read",path);
        return false;
    }
    const std::string serialized=contents.str();
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
    desks[2].tiles={
        Tile{Kind::Clock,"Clock","",0,0,2,1,1},
        Tile{Kind::Battery,"Battery","",2,0,2,1,2},
        Tile{Kind::Nextendo,"Nextendo","",4,0,2,1,0},
        Tile{Kind::Recent,"Recent games","",0,1,2,1,6},
        Tile{Kind::Favorites,"Favorites","",2,1,2,1,8},
        Tile{Kind::GameLibrary,"Game library","",4,1,2,1,9},
        Tile{Kind::Storage,"SD storage","",0,2,2,1,3},
        Tile{Kind::System,"System monitor","",2,2,2,1,5},
        Tile{Kind::Calendar,"Calendar","",4,2,2,1,4}
    };
}
void updateJoyconBatteryState(JoyconBatteryState& target,
                              const HidPowerInfo& info) {
    target.connected=true;
    target.level=static_cast<int>(std::min<u32>(info.battery_level,4));
    target.charging=info.is_charging;
}

void refreshJoyconBattery() {
    const Uint32 now=SDL_GetTicks();
    if(lastJoyconBatteryTick!=0&&now-lastJoyconBatteryTick<3000)return;
    lastJoyconBatteryTick=now;
    joyconLeft=JoyconBatteryState{};
    joyconRight=JoyconBatteryState{};

    auto inspectNpad=[&](HidNpadIdType id) {
        const u32 device=hidGetNpadDeviceType(id);
        const bool leftConnected=(device&
            (HidDeviceTypeBits_JoyLeft|HidDeviceTypeBits_HandheldLeft))!=0;
        const bool rightConnected=(device&
            (HidDeviceTypeBits_JoyRight|HidDeviceTypeBits_HandheldRight))!=0;
        if(!leftConnected&&!rightConnected)return;

        if(leftConnected&&rightConnected) {
            HidPowerInfo left{},right{};
            hidGetNpadPowerInfoSplit(id,&left,&right);
            if(!joyconLeft.connected)updateJoyconBatteryState(joyconLeft,left);
            if(!joyconRight.connected)updateJoyconBatteryState(joyconRight,right);
            return;
        }

        HidPowerInfo single{};
        hidGetNpadPowerInfoSingle(id,&single);
        if(leftConnected&&!joyconLeft.connected)
            updateJoyconBatteryState(joyconLeft,single);
        if(rightConnected&&!joyconRight.connected)
            updateJoyconBatteryState(joyconRight,single);
    };

    // Prefer the attached pair in handheld mode, then paired controllers.
    inspectNpad(HidNpadIdType_Handheld);
    for(int i=0;i<8;++i)
        inspectNpad(static_cast<HidNpadIdType>(i));
    joyconBatteryAvailable=joyconLeft.connected||joyconRight.connected;
}

void refreshAtmosphereVersion() {
    if(atmosphereVersionChecked)return;
    atmosphereVersionChecked=true;

    const Result initRc=splInitialize();
    if(R_FAILED(initRc)) {
        desknx::diag::Write("system_info","atmosphere_unavailable",
            "spl_init_rc=0x%08X",static_cast<unsigned int>(initRc));
        return;
    }

    u64 packed=0;
    const Result versionRc=splGetConfig(static_cast<SplConfigItem>(65000),&packed);
    splExit();
    if(R_FAILED(versionRc)) {
        desknx::diag::Write("system_info","atmosphere_unavailable",
            "spl_config_rc=0x%08X",static_cast<unsigned int>(versionRc));
        return;
    }

    const auto decoded=desknx::widget_sensors::decodeAtmosphereConfig(packed);
    if(!decoded.available)return;
    char version[32]{};
    std::snprintf(version,sizeof(version),"%u.%u.%u",
        decoded.major,decoded.minor,decoded.patch);
    atmosphereVersion=version;
    atmosphereVersionAvailable=true;
    desknx::diag::Write("system_info","atmosphere_version",
        "version=%s raw=0x%016llX",atmosphereVersion.c_str(),
        static_cast<unsigned long long>(packed));
}

void refreshBattery() {
    if(!hasWidgetKind(Kind::Battery))return;
    const Uint32 now=SDL_GetTicks();
    const Uint32 interval=static_cast<Uint32>(configuredWidgetRefresh(Kind::Battery))*1000u;
    if(lastBatteryTick!=0&&now-lastBatteryTick<interval)return;
    lastBatteryTick=now;
    if(!psmReady&&R_SUCCEEDED(psmInitialize()))psmReady=true;
    if(psmReady) {
        u32 pct=0;
        if(R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))&&pct<=100u)
            battery=static_cast<int>(pct);
        PsmChargerType charger=PsmChargerType_Unconnected;
        if(R_SUCCEEDED(psmGetChargerType(&charger)))
            batteryChargerType=static_cast<int>(charger);
        else batteryChargerType=-1;
    }
}

int storageStatsThreadMain(void*) {
    Uint64 nextUpdate=0;
    while(!SDL_AtomicGet(&storageStatsStop)) {
        const Uint64 now=SDL_GetTicks64();
        if(now>=nextUpdate) {
            struct statvfs fs{};
            uint64_t total=0,free=0;
            const bool available=statvfs("sdmc:/",&fs)==0&&fs.f_frsize>0;
            if(available) {
                const uint64_t blockSize=static_cast<uint64_t>(fs.f_frsize);
                total=static_cast<uint64_t>(fs.f_blocks)*blockSize;
                free=static_cast<uint64_t>(fs.f_bavail)*blockSize;
            }
            if(storageStatsMutex)SDL_LockMutex(storageStatsMutex);
            backgroundSdTotalBytes=total;
            backgroundSdFreeBytes=free;
            backgroundStorageStatsAvailable=available&&total>0;
            if(storageStatsMutex)SDL_UnlockMutex(storageStatsMutex);
            desknx::diag::Write("storage","stats_refresh",
                "available=%d total_bytes=%llu free_bytes=%llu",
                available?1:0,static_cast<unsigned long long>(total),
                static_cast<unsigned long long>(free));
            nextUpdate=now+10000;
        }
        SDL_Delay(100);
    }
    return 0;
}

void refreshStorageStats() {
    if(!hasWidgetKind(Kind::Storage))return;
    // The render thread only copies a small snapshot. statvfs runs on its own
    // worker and can no longer stall a workspace/library frame.
    if(storageStatsMutex)SDL_LockMutex(storageStatsMutex);
    sdTotalBytes=backgroundSdTotalBytes;
    sdFreeBytes=backgroundSdFreeBytes;
    storageStatsAvailable=backgroundStorageStatsAvailable;
    if(storageStatsMutex)SDL_UnlockMutex(storageStatsMutex);
}

void refreshDiagnosticsSnapshot(bool force=false) {
    const Uint32 now=SDL_GetTicks();
    const Uint32 interval=static_cast<Uint32>(configuredWidgetRefresh(Kind::Storage))*1000u;
    if(!force&&lastStorageUpdate!=0&&now-lastStorageUpdate<interval)return;
    lastStorageUpdate=now;

    refreshStorageStats();

    const auto items=catalogueSnapshot();
    installedGameCount=static_cast<size_t>(std::count_if(items.begin(),items.end(),
        [](const Candidate& item){return item.kind==Kind::Game;}));
    installedHomebrewCount=static_cast<size_t>(std::count_if(items.begin(),items.end(),
        [](const Candidate& item){return item.kind==Kind::Nro;}));

    u64 used=0,total=0;
    const Result usedRc=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    const Result totalRc=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    processRamStatsAvailable=R_SUCCEEDED(usedRc)&&R_SUCCEEDED(totalRc)&&total>0;
    if(processRamStatsAvailable) {
        processRamUsed=used;
        processRamTotal=total;
    } else {
        processRamUsed=processRamTotal=0;
    }
}
void refreshSystemMonitor() {
    if(!hasWidgetKind(Kind::System))return;
    const Uint32 now=SDL_GetTicks();
    const Uint32 interval=static_cast<Uint32>(configuredWidgetRefresh(Kind::System))*1000u;
    if(lastSystemMonitorUpdate!=0&&now-lastSystemMonitorUpdate<interval)return;
    lastSystemMonitorUpdate=now;
    systemPcbTemperatureAvailable=false;
    systemSocTemperatureAvailable=false;
    systemCpuClockAvailable=false;
    systemGpuClockAvailable=false;

    if(!tsServiceReady&&R_SUCCEEDED(tsInitialize()))tsServiceReady=true;
    if(tsServiceReady) {
        if(hosversionGet()>=MAKEHOSVERSION(10,0,0)) {
            if(!tsInternalOpen&&R_SUCCEEDED(tsOpenSession(
                &tsInternalSession,TsDeviceCode_LocationInternal)))tsInternalOpen=true;
            if(!tsSocOpen&&R_SUCCEEDED(tsOpenSession(
                &tsSocSession,TsDeviceCode_LocationExternal)))tsSocOpen=true;
            float temperature=0.0f;
            if(tsInternalOpen&&R_SUCCEEDED(tsSessionGetTemperature(
                &tsInternalSession,&temperature))&&temperature>=-20.0f&&temperature<=120.0f) {
                systemPcbTemperature=temperature;
                systemPcbTemperatureAvailable=true;
            }
            temperature=0.0f;
            if(tsSocOpen&&R_SUCCEEDED(tsSessionGetTemperature(
                &tsSocSession,&temperature))&&temperature>=-20.0f&&temperature<=120.0f) {
                systemSocTemperature=temperature;
                systemSocTemperatureAvailable=true;
            }
        } else {
            s32 temperature=0;
            if(R_SUCCEEDED(tsGetTemperature(TsLocation_Internal,&temperature))&&
               temperature>=-20&&temperature<=120) {
                systemPcbTemperature=static_cast<float>(temperature);
                systemPcbTemperatureAvailable=true;
            }
            temperature=0;
            if(R_SUCCEEDED(tsGetTemperature(TsLocation_External,&temperature))&&
               temperature>=-20&&temperature<=120) {
                systemSocTemperature=static_cast<float>(temperature);
                systemSocTemperatureAvailable=true;
            }
        }
    }

    if(hosversionGet()>=MAKEHOSVERSION(8,0,0)) {
        if(!clkrstServiceReady&&R_SUCCEEDED(clkrstInitialize()))clkrstServiceReady=true;
        if(clkrstServiceReady) {
            if(!cpuClockOpen&&R_SUCCEEDED(clkrstOpenSession(
                &cpuClockSession,PcvModuleId_CpuBus,3)))cpuClockOpen=true;
            if(!gpuClockOpen&&R_SUCCEEDED(clkrstOpenSession(
                &gpuClockSession,PcvModuleId_GPU,3)))gpuClockOpen=true;
            std::uint32_t rate=0;
            if(cpuClockOpen&&R_SUCCEEDED(clkrstGetClockRate(&cpuClockSession,&rate))&&
               rate>=1000000u&&rate<=4000000000u) {
                systemCpuClockHz=rate;
                systemCpuClockAvailable=true;
            }
            rate=0;
            if(gpuClockOpen&&R_SUCCEEDED(clkrstGetClockRate(&gpuClockSession,&rate))&&
               rate>=1000000u&&rate<=4000000000u) {
                systemGpuClockHz=rate;
                systemGpuClockAvailable=true;
            }
        }
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

void drawSystemIcon(desknx::ui::IconId icon,int x,int y,int size,Color c) {
    const int kind=static_cast<int>(icon);
    if(iconStyle==2) {
        roundRect(x-3,y-3,size+6,size+6,std::max(7,size/4),
                  Color{static_cast<Uint8>(c.r/5),static_cast<Uint8>(c.g/5),
                        static_cast<Uint8>(c.b/5),220});
        c=WHITE;
    } else if(iconStyle==1) {
        roundRect(x-3,y-3,size+6,size+6,std::max(7,size/4),
                  Color{static_cast<Uint8>(c.r/8),static_cast<Uint8>(c.g/8),
                        static_cast<Uint8>(c.b/8),54});
    }
    if(gVectorIconCache.draw(icon,x,y,size,SDL_Color{c.r,c.g,c.b,c.a}))return;
    // Geometric fallback is retained only for a broken/missing SVG asset.
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
void drawSystemIcon(int legacyKind,int x,int y,int size,Color c) {
    drawSystemIcon(desknx::ui::iconIdFromLegacyIndex(legacyKind),x,y,size,c);
}
void drawSelectionFrame(int x,int y,int w,int h,Color accent) {
    const float pulse=motionEffects
        ?desknx::ui::motion::pingPongEase(SDL_GetTicks(),1000u)
        :static_cast<float>(70.0f/95.0f);
    accent.a=static_cast<Uint8>(150.0f+pulse*95.0f+0.5f);
    switch(selectionStyle) {
        case 1:
            rect(x,y,4,h,accent);
            outline(x,y,w,h,Color{accent.r,accent.g,accent.b,static_cast<Uint8>(pulse/2)});
            break;
        case 2:
            outline(x-2,y-2,w+4,h+4,Color{accent.r,accent.g,accent.b,static_cast<Uint8>(pulse/2)});
            outline(x,y,w,h,accent);
            break;
        case 3: {
            const int length=std::max(10,std::min(28,std::min(w,h)/4));
            const Color bright{accent.r,accent.g,accent.b,255};
            rect(x,y,length,3,bright);rect(x,y,3,length,bright);
            rect(x+w-length,y,length,3,bright);rect(x+w-3,y,3,length,bright);
            rect(x,y+h-3,length,3,bright);rect(x,y+h-length,3,length,bright);
            rect(x+w-length,y+h-3,length,3,bright);rect(x+w-3,y+h-length,3,length,bright);
            break;
        }
        default:
            outline(x,y,w,h,accent);
            outline(x+1,y+1,w-2,h-2,Color{255,255,255,90});
            break;
    }
}

void drawTileIcon(const Tile&t,int x,int y,int size,bool highPriority=false) {
    Color c=COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    roundRect(x,y,size,size,std::max(8,size/5),Color{255,255,255,12});
    if(SDL_Texture* texture=gIconCache.get(t,highPriority)) {
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

bool isBackendSuspendedTile(const Tile& tile) {
#ifdef DESKNX_SYSTEM_APPLET
    if(tile.kind!=Kind::Game||!g_backendAppSuspended||g_backendAppTitle==0||
       !desknx::model::isTitleId(tile.data))return false;
    char* end=nullptr;
    const auto id=std::strtoull(tile.data.c_str(),&end,16);
    return id==g_backendAppTitle&&end==tile.data.c_str()+tile.data.size();
#else
    (void)tile;
    return false;
#endif
}

void drawTileCover(const Tile& tile,const Workspace& workspace,
                   const SDL_Rect& bounds,Color cornerFill,bool selected) {
    const Color fallback=workspace.accentColor>=0
        ?COLORS[static_cast<size_t>(workspace.accentColor)%COLORS.size()]
        :COLORS[static_cast<size_t>(tile.color)%COLORS.size()];
    const SDL_Rect viewport{bounds.x+1,bounds.y+1,
        std::max(1,bounds.w-2),std::max(1,bounds.h-2)};
    roundRect(viewport.x,viewport.y,viewport.w,viewport.h,UiCornerRadius,fallback);

    SDL_Texture* texture=gIconCache.get(tile,selected);
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

    const size_t maxChars=static_cast<size_t>(std::max(8,(bounds.w-20)/12));
    const auto& titleLines=cachedTileTitleLines(tile.title,maxChars);
    const int stripHeight=titleLines.size()>1?44:32;
    const int overlay=std::max(0,std::min(255,workspace.labelOpacity));
    if(overlay>0)
        rect(viewport.x,viewport.y+viewport.h-stripHeight,viewport.w,stripHeight,
             Color{5,9,16,static_cast<Uint8>(overlay)});
    const int titleCenter=bounds.x+bounds.w/2;
    if(titleLines.size()==1) {
        textCentered(titleLines[0],titleCenter,bounds.y+bounds.h-stripHeight+8,
                      2,WHITE,bounds.x+10,bounds.x+bounds.w-10);
    } else {
        textCentered(titleLines[0],titleCenter,bounds.y+bounds.h-stripHeight+3,
                     2,WHITE,bounds.x+10,bounds.x+bounds.w-10);
        textCentered(titleLines[1],titleCenter,bounds.y+bounds.h-stripHeight+22,
                     2,WHITE,bounds.x+10,bounds.x+bounds.w-10);
    }
    // Texture rendering uses a rectangular clip; paint only the four corner
    // wedges afterwards so the actual game artwork follows the tile's 10px
    // rounded silhouette rather than exposing square image corners.
    maskRoundedImageCorners(viewport,UiCornerRadius,cornerFill);
    if(isBackendSuspendedTile(tile)) {
        const int badgeW=languageRussian?82:78;
        const int badgeX=viewport.x+viewport.w-badgeW-8;
        roundRect(badgeX,viewport.y+8,badgeW,24,12,Color{86,58,22,238});
        text(languageRussian?"ВОЗВРАТ":"RESUME",badgeX+8,viewport.y+16,1,
             Color{255,220,158,255},badgeX+badgeW-5);
    }
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
    const Color accent=workspace.accentColor>=0
        ?COLORS[static_cast<size_t>(workspace.accentColor)%COLORS.size()]
        :COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    const bool artworkTile=t.kind==Kind::Game||t.kind==Kind::Nro;
    const Color fill=artworkTile&&selected
        ?Color{29,48,73,255}:Color{22,29,42,255};
    softCard(r.x,r.y,r.w,r.h,
             themeIndex==1&&t.kind!=Kind::Game&&t.kind!=Kind::Nro
                ?Color{13,29,41,236}:fill,
             selected?Color{92,164,255,210}:Color{255,255,255,18});
    if(themeIndex==1&&t.kind!=Kind::Game&&t.kind!=Kind::Nro)
        rect(r.x+4,r.y+18,3,std::max(12,r.h-36),
             workspace.accentColor>=0
                ?COLORS[static_cast<size_t>(workspace.accentColor)%COLORS.size()]
                :COLORS[static_cast<size_t>(t.color)%COLORS.size()]);

    if(artworkTile) {
        drawTileCover(t,workspace,r,fill,selected);
        if(selected)
            drawSelectionFrame(r.x,r.y,r.w,r.h,focusAccent());
    } else {
        roundRect(r.x+14,r.y+10,std::min(42,r.w-28),3,2,selected?BLUE:accent);
        const auto metrics=desknx::widget_layout::measure(
            r.x,r.y,r.w,r.h,t.w,t.h);
        const bool compact=metrics.compact();
        const bool banner=metrics.banner();
        const bool portrait=metrics.portrait();
        const bool wide=t.h==1&&t.w>=2;
        const auto config=widgetConfig(t);
        const bool dataA=desknx::widgets::enabled(config,0);
        const bool dataB=desknx::widgets::enabled(config,1);
        const int left=metrics.left,right=metrics.right,center=metrics.center;
        const int bodyTop=metrics.bodyTop,bottom=metrics.bodyBottom;
        const int innerWidth=std::max(1,right-left);
        auto label=[&](const std::string& value,int y) {
            textCentered(value,center,metrics.clampY(y,8),1,MUTED,left,right);
        };
        auto progress=[&](int y,int percent) {
            const int barWidth=std::max(12,right-left);
            const int bounded=std::max(0,std::min(100,percent));
            const int barY=metrics.clampY(y,8);
            roundRect(left,barY,barWidth,8,4,Color{48,62,79,255});
            roundRect(left,barY,std::max(2,barWidth*bounded/100),8,4,
                      bounded>=80?RED:GREEN);
        };
        auto freeSpaceText=[&]() {
            char value[64]{};
            std::snprintf(value,sizeof(value),"%.1f GB",
                static_cast<double>(sdFreeBytes)/(1024.0*1024.0*1024.0));
            return std::string(value);
        };
        std::vector<HistoryEntry> recentGames;
        for(const auto& entry:recentLaunches)
            if(entry.kind==Kind::Game)recentGames.push_back(entry);
        auto recentGameLine=[&](size_t index,int y,int iconSize) {
            if(index>=recentGames.size())return;
            const auto& entry=recentGames[index];
            Tile iconTile{Kind::Game,entry.title,entry.data,0,0,1,1,static_cast<int>(index)};
            if(dataB)drawTileIcon(iconTile,left,y,iconSize);
            text(entry.title,left+(dataB?iconSize+8:0),y+(iconSize>28?6:4),1,WHITE,right);
        };
        auto drawDate=[&](int y,int scale) {
            const std::string date=clockText(true);
            const std::string value=date.size()>6?date.substr(6):date;
            textCentered(value,center,metrics.clampY(y,7*scale),scale,MUTED,left,right);
        };

        if(t.kind==Kind::Clock) {
            const std::time_t stamp=std::time(nullptr);
            std::tm current{};
            if(const std::tm* value=std::localtime(&stamp))current=*value;
            char timeValue[40]{},dateValue[40]{};
            const char* format=config.style==1?"%I:%M %p":(dataA?"%H:%M:%S":"%H:%M");
            if(!std::strftime(timeValue,sizeof(timeValue),format,&current))
                std::snprintf(timeValue,sizeof(timeValue),"--:--");
            std::strftime(dateValue,sizeof(dateValue),"%d.%m.%Y",&current);
            if(config.style==2) {
                const int radius=std::min(39,std::max(23,(wide?r.h:r.w)/3));
                const int cx=left+radius+4,cy=r.y+r.h/2;
                color(Color{91,112,139,255});
                for(int tick=0;tick<60;++tick) {
                    const double angle=(static_cast<double>(tick)/60.0)*6.283185307179586-1.5707963267948966;
                    const int outer=radius-1,inner=radius-(tick%5==0?8:4);
                    SDL_RenderDrawLine(renderer,
                        cx+static_cast<int>(std::cos(angle)*inner),
                        cy+static_cast<int>(std::sin(angle)*inner),
                        cx+static_cast<int>(std::cos(angle)*outer),
                        cy+static_cast<int>(std::sin(angle)*outer));
                }
                auto hand=[&](double angle,int length,Color shade) {
                    color(shade);
                    SDL_RenderDrawLine(renderer,cx,cy,
                        cx+static_cast<int>(std::cos(angle)*length),
                        cy+static_cast<int>(std::sin(angle)*length));
                };
                const double minute=(current.tm_min+current.tm_sec/60.0)/60.0*6.283185307179586-1.5707963267948966;
                const double hour=((current.tm_hour%12)+current.tm_min/60.0)/12.0*6.283185307179586-1.5707963267948966;
                hand(hour,radius*5/10,WHITE);
                hand(minute,radius*8/10,BLUE);
                roundRect(cx-3,cy-3,6,6,3,WHITE);
                text(timeValue,cx+radius+12,r.y+29,config.style==2?2:3,WHITE,right);
                if(dataB)label(dateValue,r.y+65);
                label("LOCAL TIME",r.y+r.h-18);
            } else {
                const int scale=wide?4:6;
                text(timeValue,left,r.y+(wide?23:24),scale,WHITE,right);
                if(dataB)label(dateValue,r.y+(wide?78:105));
                label(config.style==1?"12-HOUR CLOCK":"24-HOUR CLOCK",r.y+r.h-18);
            }
        } else if(t.kind==Kind::Battery) {
            const bool powerConnected=batteryChargerType>=0&&
                batteryChargerType!=PsmChargerType_Unconnected;
            const std::string powerLabel=batteryChargerType<0?"POWER UNKNOWN":
                (powerConnected?"POWER CONNECTED":"ON BATTERY");
            const std::string batteryHeading=config.style==1?"CHARGE":
                (config.style==2?"BATTERY / POWER":"BATTERY");
            auto joyconLabel=[&](const JoyconBatteryState& joy,const char* side) {
                if(!joy.connected)return std::string(side)+"  --";
                std::string value=std::string(side)+"  "+
                    std::to_string(joy.level)+"/4";
                if(joy.charging)value+="  CHG";
                return value;
            };
            if(compact) {
                label(batteryHeading,r.y+13);
                text(battery>=0?std::to_string(battery)+"%":"N/A",
                     left,r.y+34,3,WHITE,right);
                if(dataA)progress(r.y+82,battery);
                if(dataB)label(powerLabel,r.y+92);
            } else if(wide) {
                label(batteryHeading,r.y+13);
                text(battery>=0?std::to_string(battery)+"%":"N/A",
                     left,r.y+33,3,WHITE,right-70);
                if(dataA)progress(r.y+82,battery);
                drawSystemIcon(desknx::ui::IconId::Battery,r.x+r.w-62,r.y+28,38,
                    config.style==2?(powerConnected?GREEN:BLUE):(battery>=20?GREEN:RED));
                if(dataB)text(powerLabel,left,r.y+91,1,powerConnected?GREEN:MUTED,right);
            } else {
                label(batteryHeading+" STATUS",r.y+14);
                drawSystemIcon(desknx::ui::IconId::Battery,r.x+r.w-86,r.y+26,58,battery>=20?GREEN:RED);
                text(battery>=0?std::to_string(battery)+"%":"N/A",
                     left,r.y+50,6,WHITE,right-70);
                if(dataA)progress(r.y+145,battery);
                text(battery<0?"UNAVAILABLE":
                     battery>=80?"HIGH CHARGE":
                     battery>=20?"NORMAL":"LOW BATTERY",
                     left,r.y+172,2,battery>=20?GREEN:RED,right);
                if(dataB)label(powerLabel,r.y+208);
            }
        } else if(t.kind==Kind::System) {
            const u32 firmware=hosversionGet();
            char firmwareText[48]{};
            std::snprintf(firmwareText,sizeof(firmwareText),"%u.%u.%u",
                HOSVER_MAJOR(firmware),HOSVER_MINOR(firmware),HOSVER_MICRO(firmware));
            char socText[32]{},pcbText[32]{},cpuText[32]{},gpuText[32]{};
            if(systemSocTemperatureAvailable)std::snprintf(socText,sizeof(socText),"%.1f C",systemSocTemperature);
            else std::snprintf(socText,sizeof(socText),"N/A");
            if(systemPcbTemperatureAvailable)std::snprintf(pcbText,sizeof(pcbText),"%.1f C",systemPcbTemperature);
            else std::snprintf(pcbText,sizeof(pcbText),"N/A");
            if(systemCpuClockAvailable)std::snprintf(cpuText,sizeof(cpuText),"%u MHz",systemCpuClockHz/1000000u);
            else std::snprintf(cpuText,sizeof(cpuText),"N/A");
            if(systemGpuClockAvailable)std::snprintf(gpuText,sizeof(gpuText),"%u MHz",systemGpuClockHz/1000000u);
            else std::snprintf(gpuText,sizeof(gpuText),"N/A");
            const std::string atmosphereText=atmosphereVersionAvailable
                ?"AMS "+atmosphereVersion:"AMS N/A";
            if(compact) {
                label(config.style==1?"THERMAL MONITOR":
                    (config.style==2?"CLOCK MONITOR":"SYSTEM MONITOR"),r.y+13);
                if(config.style==2&&dataB)
                    text(cpuText,left,r.y+38,2,WHITE,right);
                else text(dataA?socText:"HOS "+std::string(firmwareText),left,r.y+38,2,WHITE,right);
                if(dataB)label("GPU "+std::string(gpuText),r.y+78);
                else if(dataA)label("PCB "+std::string(pcbText),r.y+78);
            } else if(wide) {
                label(config.style==1?"THERMAL / SYSTEM":
                    (config.style==2?"CLOCK / SYSTEM":"SYSTEM MONITOR"),r.y+12);
                if(config.style==2) {
                    if(dataB) {
                        text("CPU "+std::string(cpuText),left,r.y+35,2,WHITE,right);
                        text("GPU "+std::string(gpuText),left+150,r.y+35,2,BLUE,right);
                    }
                    if(dataA)text("SOC "+std::string(socText)+"   PCB "+std::string(pcbText),left,r.y+72,1,BLUE,right);
                } else {
                    if(dataA)text("SOC "+std::string(socText),left,r.y+35,2,WHITE,right);
                    if(dataA)text("PCB "+std::string(pcbText),left,r.y+65,1,BLUE,right);
                    if(dataB) {
                        text("CPU "+std::string(cpuText),left+150,r.y+35,1,BLUE,right);
                        text("GPU "+std::string(gpuText),left+150,r.y+65,1,BLUE,right);
                    }
                }
                if(!dataA&&!dataB)text("HOS "+std::string(firmwareText),left,r.y+43,2,WHITE,right);
            } else {
                label(config.style==1?"THERMAL  /  HOS "+std::string(firmwareText):
                    (config.style==2?"FREQUENCIES  /  HOS "+std::string(firmwareText):
                     "SYSTEM MONITOR  /  HOS "+std::string(firmwareText)),r.y+14);
                if(config.style==2) {
                    if(dataB) {
                        text("CPU  "+std::string(cpuText),left,r.y+48,3,WHITE,right);
                        text("GPU  "+std::string(gpuText),left,r.y+100,3,BLUE,right);
                    }
                    if(dataA)text("SOC "+std::string(socText)+"   PCB "+std::string(pcbText),
                        left,r.y+161,1,MUTED,right);
                } else {
                    if(dataA) {
                        text("SOC",left,r.y+48,1,MUTED,right);
                        text(socText,left,r.y+66,3,WHITE,right);
                        text("PCB  "+std::string(pcbText),left,r.y+111,1,MUTED,right);
                    }
                    if(dataB) {
                        text("CPU  "+std::string(cpuText),left,r.y+151,2,BLUE,right);
                        text("GPU  "+std::string(gpuText),left,r.y+184,2,BLUE,right);
                    }
                }
                if(!dataA&&!dataB)text("NATIVE LIBNX SERVICES",left,r.y+90,2,MUTED,right);
            }
        } else if(t.kind==Kind::Recent) {
            const auto& entries=recentGames;
            if(compact) {
                label("RECENT GAMES",r.y+13);
                if(entries.empty())text("NO GAMES YET",left,r.y+50,1,WHITE,right);
                else recentGameLine(0,r.y+42,26);
            } else if(wide&&config.style==1&&dataB) {
                label("RECENT GAMES",r.y+12);
                if(entries.empty())text("LAUNCH A GAME TO START",left,r.y+48,1,MUTED,right);
                else {
                    const int count=std::min<int>(
                        std::min<int>(config.itemCount,t.w>=3?4:2),
                        static_cast<int>(entries.size()));
                    const int cellWidth=std::max(1,innerWidth/std::max(1,count));
                    for(int i=0;i<count;++i) {
                        const int x=left+i*cellWidth;
                        const auto& entry=entries[static_cast<std::size_t>(i)];
                        Tile game{Kind::Game,entry.title,entry.data,0,0,1,1,i};
                        drawTileIcon(game,x+std::max(0,(cellWidth-42)/2),r.y+30,42);
                        text(entry.title,x+2,r.y+78,1,WHITE,x+cellWidth-2);
                    }
                }
            } else if(wide) {
                label("RECENT GAMES",r.y+12);
                if(entries.empty())text("LAUNCH A GAME TO START",left,r.y+48,1,MUTED,right);
                else {
                    const size_t count=std::min<std::size_t>(
                        config.style==2?1:(dataA?config.itemCount:1),
                        std::min<std::size_t>(2,entries.size()));
                    for(size_t i=0;i<count;++i)
                        recentGameLine(i,r.y+32+static_cast<int>(i)*(config.style==2?0:35),
                            config.style==2?32:25);
                }
            } else {
                label("RECENT GAMES",r.y+17);
                if(entries.empty())text("NO GAMES YET",left,r.y+86,2,WHITE,right);
                else {
                    const size_t count=std::min<std::size_t>(config.itemCount,entries.size());
                    for(size_t i=0;i<count;++i)
                        recentGameLine(i,r.y+50+static_cast<int>(i)*42,32);
                }
                label("LOCAL PLAY HISTORY",r.y+r.h-24);
            }
        } else if(t.kind==Kind::Storage) {
            const uint64_t used=sdTotalBytes>sdFreeBytes?sdTotalBytes-sdFreeBytes:0;
            const int percent=storageStatsAvailable
                ?static_cast<int>(std::min<uint64_t>(100,(used*100)/std::max<uint64_t>(1,sdTotalBytes)))
                :-1;
            char usedText[48]{},totalText[48]{};
            std::snprintf(usedText,sizeof(usedText),"%.1f GB USED",
                static_cast<double>(used)/(1024.0*1024.0*1024.0));
            std::snprintf(totalText,sizeof(totalText),"%.1f GB TOTAL",
                static_cast<double>(sdTotalBytes)/(1024.0*1024.0*1024.0));
            const std::string mainValue=!storageStatsAvailable?"N/A":
                (config.style==2?freeSpaceText()+" / "+std::string(totalText):
                 (dataA?std::string(usedText):freeSpaceText()+" FREE"));
            if(config.style==1&&storageStatsAvailable&&!compact) {
                label("SD CAPACITY",r.y+13);
                text(std::to_string(percent)+"%",left,r.y+34,4,WHITE,right);
                if(dataA)text(usedText,left,r.y+(wide?78:92),1,MUTED,right);
                else text(freeSpaceText()+" FREE",left,r.y+(wide?78:92),1,MUTED,right);
                if(dataB)progress(r.y+(wide?100:122),percent);
            } else if(compact) {
                label("SD STORAGE",r.y+13);
                text(mainValue,left,r.y+39,2,WHITE,right);
                if(dataB)progress(r.y+82,percent);
            } else if(wide) {
                label("SD CARD STORAGE",r.y+12);
                text(mainValue,left,r.y+34,2,WHITE,right-78);
                if(dataB&&percent>=0)text(std::to_string(percent)+"% USED",
                    right-70,r.y+49,1,MUTED,right);
                if(dataB)progress(r.y+83,percent);
                else if(dataA)label(totalText,r.y+83);
            } else {
                label("SD CARD STORAGE",r.y+18);
                if(storageStatsAvailable) {
                    text(mainValue,left,r.y+50,4,WHITE,right);
                    if(dataA)text(totalText,left,r.y+112,2,MUTED,right);
                    if(dataB) {
                        progress(r.y+157,percent);
                        text(std::to_string(percent)+"% USED",left,r.y+184,1,BLUE,right);
                    }
                } else text("SD CARD UNAVAILABLE",left,r.y+90,2,RED,right);
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
            const std::string weekday=languageRussian?
                ruWeek[(current.tm_wday+6)%7]:enWeek[(current.tm_wday+6)%7];
            if(config.style==1) {
                label(languageRussian?"СЕГОДНЯ":"TODAY",r.y+13);
                const bool compactCalendar=wide||compact;
                text(std::to_string(current.tm_mday),left,r.y+(compactCalendar?22:30),
                     compactCalendar?4:5,WHITE,right);
                text(std::string(monthName)+"  "+std::to_string(current.tm_year+1900),
                     left,r.y+(compactCalendar?68:100),compactCalendar?1:2,BLUE,right);
                if(dataA&&!compact)label(weekday,r.y+(wide?94:142));
                if(dataB&&!compact)label(clockText(),r.y+(wide?104:169));
            } else if(config.style==2) {
                label(languageRussian?"ПЛАН НА СЕГОДНЯ":"TODAY / AGENDA",r.y+14);
                const bool compactCalendar=wide||compact;
                text(weekday,left,r.y+(compactCalendar?34:40),compactCalendar?1:2,BLUE,right);
                text(std::to_string(current.tm_mday)+" "+monthName,left,
                    r.y+(compactCalendar?58:68),compactCalendar?1:2,WHITE,right);
                if(dataA)text(std::to_string(current.tm_year+1900),left,
                    r.y+(compactCalendar?82:122),1,MUTED,right);
                if(dataB)text(clockText(),left,r.y+(compactCalendar?96:158),
                    compactCalendar?1:2,WHITE,right);
            } else if(compact) {
                textCentered(std::to_string(current.tm_mday),center,r.y+18,5,WHITE,left,right);
                textCentered(monthName,center,r.y+78,1,BLUE,left,right);
            } else if(banner) {
                label(languageRussian?"СЕГОДНЯ":"TODAY",r.y+13);
                text(std::to_string(current.tm_mday)+" "+monthName+" "+std::to_string(current.tm_year+1900),
                     left,r.y+42,3,WHITE,right);
                if(dataB)text(weekday,left,r.y+83,1,BLUE,right);
                drawSystemIcon(desknx::ui::IconId::Calendar,right-34,r.y+38,26,accent);
            } else {
                std::tm first=current;
                first.tm_mday=1;first.tm_hour=12;first.tm_min=0;first.tm_sec=0;
                (void)std::mktime(&first);
                std::tm lastDay=first;
                lastDay.tm_mon+=1;lastDay.tm_mday=0;
                (void)std::mktime(&lastDay);
                const int daysInMonth=lastDay.tm_mday;
                const int startColumn=(first.tm_wday+6)%7;
                label(monthName,metrics.headerY);
                text(std::to_string(current.tm_year+1900),right-42,
                     metrics.headerY,1,MUTED,right);
                const int cellW=std::max(1,(right-left)/7);
                const int gridY=metrics.bodyTop;
                const int cellH=std::max(19,std::min(28,(r.h-(gridY-r.y)-40)/6));
                for(int col=0;col<7;++col)
                    text(languageRussian?ruWeek[col]:enWeek[col],
                         left+col*cellW+2,gridY,1,BLUE,right);
                for(int day=1;day<=daysInMonth;++day) {
                    const int index=startColumn+day-1;
                    const int col=index%7,row=index/7;
                    const int dx=left+col*cellW,dy=gridY+22+row*cellH;
                    const int safeY=metrics.clampY(dy,8);
                    if(day==current.tm_mday)
                        roundRect(dx,safeY-2,std::max(14,cellW-3),18,5,
                                  Color{42,91,145,255});
                    text(std::to_string(day),dx+3,safeY+2,1,
                         day==current.tm_mday?WHITE:MUTED,right);
                }
                text(languageRussian?"ТЕКУЩАЯ ДАТА":"CURRENT DATE",
                     left,metrics.clampY(bottom-12,8),1,accent,right);
            }
        } else if(t.kind==Kind::Nextendo) {
            const auto online=nextendoWidgetSnapshot();
            label(config.style==2?"NEXTENDO  /  NETWORK":"NEXTENDO  /  ACTIVITY",r.y+12);
            if(!online.fresh) {
                text("WAITING FOR SYNC",left,r.y+39,2,WHITE,right);
                label("STATUS UNKNOWN",r.y+78);
            } else if(config.style==1&&wide) {
                if(dataB&&!online.topGame.empty()) {
                    text(online.topGame,left,r.y+34,2,WHITE,right);
                    label(std::to_string(online.topPlayers)+" PLAYERS ACTIVE",r.y+68);
                } else {
                    text(std::to_string(online.total)+" ONLINE",left,r.y+34,3,WHITE,right);
                }
                if(dataA)label(std::to_string(online.activeTitles)+" ACTIVE TITLES",r.y+95);
            } else {
                if(dataA) {
                    text(std::to_string(online.total)+" ONLINE",left,r.y+34,3,WHITE,right);
                    label(std::to_string(online.activeTitles)+" ACTIVE GAMES",r.y+76);
                } else text("SERVICE SYNCED",left,r.y+40,2,GREEN,right);
                if(dataB) {
                    if(!online.topGame.empty())
                        text(online.topGame+"  /  "+std::to_string(online.topPlayers),
                             left,r.y+93,1,BLUE,right);
                    else label("NO ACTIVE TITLE DATA",r.y+93);
                }
            }
        } else if(t.kind==Kind::Favorites || t.kind==Kind::GameLibrary) {
            const std::vector<Tile> games=t.kind==Kind::Favorites?
                favoriteTiles():gameLibraryTiles(static_cast<std::size_t>(config.itemCount),dataA);
            const bool favoritesWidget=t.kind==Kind::Favorites;
            label(favoritesWidget?"FAVORITES":"GAME LIBRARY",r.y+12);
            if(games.empty()) {
                text(favoritesWidget
                    ?(languageRussian?"НЕТ ИЗБРАННЫХ ИГР":"NO FAVORITES SAVED")
                    :(languageRussian?"НЕТ УСТАНОВЛЕННЫХ ИГР":"NO INSTALLED GAMES"),
                     left,r.y+48,1,MUTED,right);
            } else if(wide&&config.style==1) {
                const int count=std::min<int>(
                    std::min<int>(config.itemCount,4),static_cast<int>(games.size()));
                const int cellWidth=std::max(1,innerWidth/std::max(1,count));
                for(int i=0;i<count;++i) {
                    const int x=left+i*cellWidth;
                    const Tile& game=games[static_cast<std::size_t>(i)];
                    drawTileIcon(game,x+std::max(0,(cellWidth-42)/2),r.y+31,42);
                    text(game.title,x+2,r.y+81,1,WHITE,x+cellWidth-3);
                }
            } else {
                const int iconSize=wide?(config.style==2?18:24):(config.style==2?24:32);
                const int rowStep=wide?(config.style==2?22:27):(config.style==2?32:42);
                const int maxRows=wide?(config.style==2?3:2):std::min<int>(4,config.itemCount);
                const int count=std::min<int>(
                    std::min<int>(config.itemCount,maxRows),static_cast<int>(games.size()));
                for(int i=0;i<count;++i) {
                    const Tile& game=games[static_cast<std::size_t>(i)];
                    const int y=r.y+(wide?34:48)+i*rowStep;
                    const bool showCover=favoritesWidget?dataA:dataB;
                    if(showCover)drawTileIcon(game,left,y,iconSize);
                    text(game.title,left+(showCover?iconSize+8:0),y+std::max(3,(iconSize-8)/2),
                         1,WHITE,right);
                }
            }
            if(!wide&&favoritesWidget&&dataB)
                label(std::to_string(favoriteTitleIds.size())+" SAVED",r.y+r.h-22);
        } else if(t.kind==Kind::Nextendo) {
            const auto popular=nextendoPopularSnapshot();
            label("NEXTENDO  /  ONLINE",metrics.headerY);
            if(!popular.available) {
                textCentered(languageRussian?"ПОЛУЧЕНИЕ ДАННЫХ...":"LOADING LIVE DATA...",
                             center,metrics.clampY(bodyTop+8,8),1,MUTED,left,right);
            } else if(popular.count==0) {
                textCentered(languageRussian?"СЕЙЧАС НЕТ ИГР":"NO SUPPORTED GAMES ONLINE",
                             center,metrics.clampY(bodyTop+8,8),1,MUTED,left,right);
            } else {
                const int rows=compact?1:(banner?std::min(2,metrics.maxRows):metrics.maxRows);
                const int nameRight=right-43;
                for(std::size_t i=0;i<popular.count&&i<static_cast<std::size_t>(rows);++i) {
                    const auto& game=popular.games[i];
                    const int y=metrics.rowY(static_cast<int>(i));
                    text(std::to_string(i+1)+".",left,y,1,BLUE,nameRight-4);
                    text(game.title?game.title:"Unknown game",left+20,y,1,WHITE,nameRight);
                    text(std::to_string(game.players),right-38,y,1,GREEN,right);
                }
            }
            if(!compact&&!banner)
                text(languageRussian?"ИГРЫ С МАКСИМАЛЬНЫМ ОНЛАЙНОМ":
                    "MOST PLAYED RIGHT NOW",left,metrics.clampY(bottom-11,8),1,MUTED,right);
        } else if(t.kind==Kind::QuickSettings) {
            if(!quickSettingsSnapshotLoaded)refreshQuickSettingsSnapshot(true);
            label(languageRussian?"БЫСТРЫЕ НАСТРОЙКИ":"QUICK SETTINGS",metrics.headerY);
            const std::string brightnessValue=quickBrightnessAvailable
                ?std::to_string(static_cast<int>(std::lround(
                    std::max(0.0f,std::min(1.0f,quickBrightness))*100.0f)))+"%"
                :"N/A";
            const std::string networkValue=quickInternetConnected?(languageRussian?"ОНЛАЙН":"ONLINE"):
                (quickAirplaneMode?(languageRussian?"АВИАРЕЖИМ":"AIRPLANE"):
                    (quickWifiEnabled?(languageRussian?"ПОДКЛЮЧЕНИЕ":"CONNECTING"):
                        (languageRussian?"ОФЛАЙН":"OFFLINE")));
            struct QuickCell {
                std::string name;
                std::string value;
                bool available;
                bool active;
            };
            const QuickCell cells[]={
                {languageRussian?"ЗВУК":"SOUND",quickSoundAvailable?(quickSoundMuted?(languageRussian?"ВЫКЛ.":"MUTED"):(languageRussian?"ВКЛ.":"ON")):(languageRussian?"Н/Д":"N/A"),
                    quickSoundAvailable,quickSoundAvailable&&!quickSoundMuted},
                {languageRussian?"АВИАРЕЖИМ":"AIRPLANE",quickAirplaneAvailable?(quickAirplaneMode?(languageRussian?"ВКЛ.":"ON"):(languageRussian?"ВЫКЛ.":"OFF")):(languageRussian?"Н/Д":"N/A"),
                    quickAirplaneAvailable,quickAirplaneMode},
                {"WI-FI",quickWifiAvailable?(quickWifiEnabled&&!quickAirplaneMode?(languageRussian?"ВКЛ.":"ON"):(languageRussian?"ВЫКЛ.":"OFF")):(languageRussian?"Н/Д":"N/A"),
                    quickWifiAvailable,quickWifiEnabled&&!quickAirplaneMode},
                {"BLUETOOTH",quickBluetoothAvailable?(quickBluetoothEnabled?(languageRussian?"ВКЛ.":"ON"):(languageRussian?"ВЫКЛ.":"OFF")):(languageRussian?"Н/Д":"N/A"),
                    quickBluetoothAvailable,quickBluetoothEnabled},
                {languageRussian?"ЯРКОСТЬ":"BRIGHTNESS",brightnessValue,quickBrightnessAvailable,
                    quickBrightnessAvailable&&quickBrightness>=0.5f},
                {languageRussian?"СЕТЬ":"NETWORK",networkValue,quickAirplaneAvailable||quickWifiAvailable,
                    quickInternetConnected}
            };
            if(t.h<=1||t.w<=1) {
                const std::string summary=
                    "SOUND "+std::string(quickSoundAvailable?(quickSoundMuted?"MUTED":"ON"):"N/A")+
                    "   WI-FI "+(quickWifiAvailable?(quickWifiEnabled&&!quickAirplaneMode?"ON":"OFF"):"N/A");
                label(summary,metrics.clampY(metrics.bodyTop+8,8));
            } else {
                const int count=std::max(2,std::min(6,config.itemCount));
                const int rows=(count+1)/2;
                const int gap=6;
                const int availableHeight=std::max(rows*24,
                    metrics.bodyBottom-metrics.bodyTop-4-gap*(rows-1));
                const int cellH=std::max(24,std::min(42,availableHeight/rows));
                const int cellW=std::max(1,(right-left-gap)/2);
                const int gridY=metrics.bodyTop+2;
                for(int i=0;i<count;++i) {
                    const int col=i%2,row=i/2;
                    const int x=left+col*(cellW+gap),y=gridY+row*(cellH+gap);
                    const QuickCell& cell=cells[i];
                    const Color stateColor=!cell.available?MUTED:
                        (cell.active?GREEN:Color{185,157,122,255});
                    const bool compact=config.style==1;
                    const Color surface=config.style==2
                        ?Color{19,31,47,245}:Color{31,43,59,245};
                    roundRect(x,y,cellW,cellH,9,compact?Color{25,38,54,230}:surface);
                    if(config.style==2) {
                        roundRect(x+7,y+7,5,std::max(10,cellH-14),2,stateColor);
                    } else {
                        roundRect(x+7,y+7,4,std::max(10,cellH-14),2,stateColor);
                    }
                    text(cell.name,x+15,y+5,1,MUTED,right);
                    text(cell.value,x+15,y+cellH/2,1,stateColor,right);
                }
            }
        } else if(t.kind==Kind::Unsupported) {
            roundRect(r.x+3,r.y+3,std::max(1,r.w-6),std::max(1,r.h-6),14,
                      Color{39,46,58,255});
            textCentered("UNSUPPORTED ITEM",center,metrics.clampY(r.y+18,14),
                         2,WHITE,left,right);
            textCentered(t.unknownTypeCode.empty()?"?":t.unknownTypeCode,
                         center,metrics.clampY(r.y+52,14),2,MUTED,left,right);
        }
    }
}
void drawDotted(int x,int y,int w,int h) {
    for(int xx=x;xx<x+w;xx+=12){rect(xx,y,6,2,Color{67,79,98,180});rect(xx,y+h-2,6,2,Color{67,79,98,180});}
    for(int yy=y;yy<y+h;yy+=12){rect(x,yy,2,6,Color{67,79,98,180});rect(x+w-2,yy,2,6,Color{67,79,98,180});}
}
int visibleStart() {
    if(touchDragging&&touchDragTabStart>=0)return touchDragTabStart;
    return std::max(0,std::min(deskIndex-2,static_cast<int>(desks.size())-VisibleDeskTabs));
}
bool isWidgetKind(Kind kind);
Tile* selectedWidgetTile();
std::string widgetEditorStyleLabel(Kind kind,int style);
std::string widgetEditorDataLabel(unsigned bit);

bool currentWorkspaceIsEmpty() {
    return !desks.empty()&&deskIndex>=0&&deskIndex<static_cast<int>(desks.size())&&
        desks[static_cast<size_t>(deskIndex)].tiles.empty();
}

void refreshSurfaceManager() {
    using desknx::ui::SurfaceId;
    gSurfaceManager.beginFrame();
    if(currentWorkspaceIsEmpty())
        (void)gSurfaceManager.push(SurfaceId::EmptyDesktop,false,true,174);

    // Register from back to front. This matches keyInput's dispatch priority:
    // only the last modal surface is allowed to consume the current input.
    if(listOpen)(void)gSurfaceManager.push(SurfaceId::Library,true,true,174);
    if(deletePrompt)(void)gSurfaceManager.push(SurfaceId::DeleteConfirmation,true,true,174);
    if(powerMenu)(void)gSurfaceManager.push(SurfaceId::PowerMenu,true,true,174);
    if(recentOpen)(void)gSurfaceManager.push(SurfaceId::RecentGames,true,true,174);
    if(workspaceMenu)(void)gSurfaceManager.push(SurfaceId::WorkspaceMenu,true,true,174);
    if(workspacePicker)(void)gSurfaceManager.push(SurfaceId::WorkspacePicker,true,true,174);
    if(systemMenu)(void)gSurfaceManager.push(SurfaceId::SystemMenu,true,true,174);
    if(workspaceSettingsMenu)(void)gSurfaceManager.push(SurfaceId::WorkspaceSettings,true,true,174);
    if(diagnosticsOpen)(void)gSurfaceManager.push(SurfaceId::Diagnostics,true,true,174);
    if(appSettingsMenu)(void)gSurfaceManager.push(SurfaceId::LauncherSettings,true,true,174);
    if(mediaPickerOpen)(void)gSurfaceManager.push(SurfaceId::MediaPicker,true,true,174);
    if(userPickerOpen)(void)gSurfaceManager.push(SurfaceId::UserPicker,true,true,174);
    if(nextendoOpen)(void)gSurfaceManager.push(SurfaceId::Nextendo,true,true,174);
    if(steamGridDbPickerOpen)(void)gSurfaceManager.push(SurfaceId::SteamGridDb,true,true,174);
    if(favoritesOpen)(void)gSurfaceManager.push(SurfaceId::Favorites,true,true,174);
    if(widgetEditorOpen)(void)gSurfaceManager.push(SurfaceId::WidgetEditor,true,true,174);
    if(quickSettingsOpen)(void)gSurfaceManager.push(SurfaceId::QuickSettings,true,true,174);
}

void drawScreen() {
    using desknx::ui::SurfaceId;
    gVisualEngine.beginFrame();
    gUiComponents.setThemeTokens(desknx::ui::ThemeTokens{
        SDL_Color{BLUE.r,BLUE.g,BLUE.b,BLUE.a},
        SDL_Color{WHITE.r,WHITE.g,WHITE.b,220},
        SDL_Color{255,255,255,28},
        UiCornerRadius});
    const std::uint32_t screenState=
        (static_cast<std::uint32_t>(std::max(0,deskIndex))&0x0Fu) |
        (listOpen?1u<<4:0u) | (recentOpen?1u<<5:0u) |
        (systemMenu?1u<<6:0u) | (appSettingsMenu?1u<<7:0u) |
        (workspaceMenu?1u<<8:0u) | (workspaceSettingsMenu?1u<<9:0u) |
        (workspacePicker?1u<<10:0u) | (mediaPickerOpen?1u<<11:0u) |
        (powerMenu?1u<<12:0u) | (userPickerOpen?1u<<13:0u) |
        (deletePrompt?1u<<14:0u) | (quickSettingsOpen?1u<<15:0u);
    if(lastUiScreenState==UINT32_MAX) {
        lastUiScreenState=screenState;
    } else if(screenState!=lastUiScreenState) {
        lastUiScreenState=screenState;
        uiTransitionStart=motionEffects?SDL_GetTicks():0;
    }
    const bool emptyDesktopSurface=currentWorkspaceIsEmpty();
    refreshSurfaceManager();
    const bool modalSurfaceActive=gSurfaceManager.backdropActive();
    gWorkspaceBackgroundCache.beginFrame();
    gIconCache.beginFrame();
    gFontRenderer.beginFrame();
    if(quickSettingsOpen)refreshQuickSettingsSnapshot();
    gModalLayer.beginFrame(renderer,modalSurfaceActive,SW,SH);
    const std::vector<Candidate>& available = listOpen ? visibleCatalogueSnapshot() : visibleCatalogueCache;
    bool catalogReadyNow=false,catalogCompleteNow=false,catalogTruncatedNow=false;
    bool catalogueNsAvailableNow=false;
    std::uint32_t catalogueNsErrorNow=0;
    size_t catalogueGameCountNow=0,catalogueNroCountNow=0;
    readCatalogueState(catalogReadyNow,catalogCompleteNow,catalogTruncatedNow,
        &catalogueNsAvailableNow,&catalogueNsErrorNow,
        &catalogueGameCountNow,&catalogueNroCountNow);
    static bool prefetchValid=false,previousListOpen=false;
    static int previousDesk=-1,previousX=-1,previousY=-1,previousList=-1;
    static std::uint64_t previousCatalogue=UINT64_MAX,previousLayout=UINT64_MAX;
    const std::uint64_t currentCatalogue=visibleCacheCatalogueRevision^(catalogueViewRevision*0x9E3779B97F4A7C15ull);
    const bool changed=!prefetchValid||previousListOpen!=listOpen||previousDesk!=deskIndex||
        previousX!=selectedX||previousY!=selectedY||previousList!=listIndex||
        previousCatalogue!=currentCatalogue||previousLayout!=layoutMutationRevision;
    if(changed) {
        if(listOpen&&!available.empty()) {
            const int first=std::max(0,listIndex-2),last=std::min(static_cast<int>(available.size())-1,listIndex+2);
            for(int index=first;index<=last;++index) {
                const Candidate& candidate=available[static_cast<std::size_t>(index)];
                Tile tile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
                gIconCache.prefetch(tile);
            }
        } else if(!desks.empty()) {
            const Workspace& activeDesk=desks[static_cast<std::size_t>(std::max(0,std::min(deskIndex,static_cast<int>(desks.size())-1)))];
            int queued=0;
            for(const Tile& tile:activeDesk.tiles) {
                const int distance=std::abs(tile.x+tile.w/2-selectedX)+std::abs(tile.y+tile.h/2-selectedY);
                if(distance<=3&&queued<8){gIconCache.prefetch(tile);++queued;}
            }
        }
        prefetchValid=true;previousListOpen=listOpen;previousDesk=deskIndex;
        previousX=selectedX;previousY=selectedY;previousList=listIndex;
        previousCatalogue=currentCatalogue;previousLayout=layoutMutationRevision;
    }
    refreshBattery();
    refreshStorageStats();
    refreshSystemMonitor();
    rect(0,0,SW,SH,BG);
    const SDL_Color gradientTop{
        THEME_TOP.r,THEME_TOP.g,THEME_TOP.b,THEME_TOP.a};
    const SDL_Color gradientBottom{
        THEME_BOTTOM.r,THEME_BOTTOM.g,THEME_BOTTOM.b,THEME_BOTTOM.a};
    if(!gVisualEngine.drawVerticalGradient(gradientTop,gradientBottom)) {
        // Keep a resilient fallback if surface/texture allocation fails.
        for(int band=0;band<24;++band) {
            const double t=static_cast<double>(band)/23.0;
            const auto blend=[&](Uint8 a,Uint8 b) {
                return static_cast<Uint8>(static_cast<double>(a)+
                    (static_cast<double>(b)-static_cast<double>(a))*t);
            };
            rect(0,band*30,SW,31,Color{blend(THEME_TOP.r,THEME_BOTTOM.r),
                blend(THEME_TOP.g,THEME_BOTTOM.g),blend(THEME_TOP.b,THEME_BOTTOM.b),255});
        }
    }
    if(!desks.empty())
        drawWorkspaceBackground(desks[static_cast<size_t>(std::max(0,
            std::min(deskIndex,static_cast<int>(desks.size())-1)))]);
    else
        drawWallpaper();

    // Three interchangeable header compositions. The same hit targets and
    // controller mappings are retained; only the visual treatment changes.
    const Color headerSurface=themeIndex==1
        ?Color{12,24,36,232}:Color{24,32,45,224};
    const Color headerRailSurface=themeIndex==1
        ?Color{12,24,36,static_cast<Uint8>(
            (static_cast<unsigned int>(surfaceOpacity)*232u)/255u)}
        :Color{24,32,45,static_cast<Uint8>(
            (static_cast<unsigned int>(surfaceOpacity)*224u)/255u)};
    if(headerStyle==0) {
        softCard(56,16,1168,64,headerSurface,Color{255,255,255,28});
    } else if(headerStyle==1) {
        rect(0,0,SW,82,headerRailSurface);
        rect(0,79,SW,2,BLUE);
    }
    struct HeaderTool { int icon; bool active; const char* label; };
    const HeaderTool headerTools[]={
        {2,false,"GALLERY"},{0,appSettingsMenu,"SETTINGS"},
        {1,false,"PADS"}
    };
    for(int i=0;i<ToolbarButtonCount;++i) {
        const auto& item=headerTools[i];
        const int x=toolbarButtonX(i);
        const Color activeColor=themeIndex==1?Color{35,75,94,255}:Color{48,82,124,255};
        if(headerStyle==0) {
            uiButton(x,24,ToolbarButtonWidth,48,13,
                desknx::ui::ButtonVariant::Glass,
                item.active?activeColor:Color{37,47,62,230},
                Color{120,174,235,150},item.active,false,
                BLUE);
        } else if(headerStyle==1) {
            if(item.active)uiButton(x,22,ToolbarButtonWidth,46,9,
                desknx::ui::ButtonVariant::Solid,
                Color{static_cast<Uint8>(activeColor.r/2),
                      static_cast<Uint8>(activeColor.g/2),
                      static_cast<Uint8>(activeColor.b/2),180},
                Color{0,0,0,0},true,false,BLUE);
            if(item.active)gUiComponents.divider(x,76,ToolbarButtonWidth,
                SDL_Color{(BLUE).r,
                          (BLUE).g,
                          (BLUE).b,255});
        } else {
            if(item.active)uiButton(x,22,ToolbarButtonWidth,46,14,
                desknx::ui::ButtonVariant::Solid,
                Color{static_cast<Uint8>(activeColor.r/2),
                      static_cast<Uint8>(activeColor.g/2),
                      static_cast<Uint8>(activeColor.b/2),130},
                Color{0,0,0,0},true,false,BLUE);
            if(item.active)gUiComponents.divider(x+18,72,ToolbarButtonWidth-36,
                SDL_Color{(BLUE).r,
                          (BLUE).g,
                          (BLUE).b,255});
        }
        drawSystemIcon(item.icon,x+12,31,24,item.active?WHITE:Color{204,214,228,255});
        const int labelWidth=static_cast<int>(std::string(item.label).size())*6;
        text(item.label,x+std::max(5,(ToolbarButtonWidth-labelWidth)/2),56,1,
             item.active?WHITE:Color{179,191,207,255},x+ToolbarButtonWidth-4);
    }
    // Compact overflow button keeps the quick tools reachable by touch without
    // restoring a fourth top-level navigation destination.
    uiButton(392,24,44,48,14,desknx::ui::ButtonVariant::Chip,
        Color{37,47,62,230},Color{255,255,255,24});
    text("...",403,42,2,WHITE,431);
    softCard(448,24,374,48,Color{32,43,59,235},Color{255,255,255,24});
    drawSystemIcon(desknx::ui::IconId::User,464,34,27,Color{215,228,246,255});
    text(selectedUserLabel,504,41,2,WHITE,710);
    text("CHANGE",732,43,1,BLUE,806);
    uiButton(832,24,170,48,18,desknx::ui::ButtonVariant::Chip,
        Color{32,43,59,235},Color{255,255,255,18});
    text(nextendoTotalLabel(),846,43,1,GREEN,990);
    uiButton(1012,24,190,48,18,desknx::ui::ButtonVariant::Chip,
        Color{32,43,59,235},Color{255,255,255,18});
    text(clockText(),1040,37,3,WHITE,1136);
    rect(1148,34,1,26,Color{255,255,255,30});
    text(battery>=0?std::to_string(battery)+"%":"BAT --",1158,43,1,
         battery>=20?GREEN:RED,1198);

    // Single desktop surface under the tiles and widgets.
    softCard(56,96,1168,454,Color{25,35,49,186},Color{255,255,255,30});
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(editing)for(int y=0;y<ROWS;++y)for(int x=0;x<COLS;++x)if(itemAt(d,x,y)<0) {
        int px=LEFT+x*(CW+GX),py=TOP+y*(CH+GY);drawDotted(px,py,CW,CH);
        if(x==selectedX&&y==selectedY) {
            roundRect(px+2,py+2,CW-4,CH-4,16,Color{34,63,99,100});
            outline(px+2,py+2,CW-4,CH-4,focusAccent());
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
    const Hint normalHints[]={{"A","OPEN"},{"Y","ADD"},{"X","EDIT"},{"L3","QUICK MENU"}};
    const Hint resumeHints[]={{"A","RESUME"},{"Y","ADD"},{"X","EDIT"},{"L3","QUICK MENU"}};
    const Hint editHints[]={{"A","MOVE / ADD"},{"D-PAD","POSITION"},{"ZL/ZR","SIZE"},{"-","REMOVE"}};
    const Hint dragHints[]={{"D-PAD","MOVE"},{"L/R","CHANGE DESK"},{"A / B","DROP"},{"-","REMOVE"}};
    const int selectedTileIndex=itemAt(d,selectedX,selectedY);
    if(editing&&!dragging&&selectedTileIndex>=0) {
        // Touchscreen users need the same tile-management actions as controller
        // users. Keep these real, generous hit targets in the persistent footer.
        uiButton(650,571,165,28,8,desknx::ui::ButtonVariant::Chip,
            Color{35,57,83,255},Color{255,255,255,12});
        text("SIZE -  /  ZL",665,581,1,WHITE,808);
        uiButton(826,571,165,28,8,desknx::ui::ButtonVariant::Chip,
            Color{35,57,83,255},Color{255,255,255,12});
        text("SIZE +  /  ZR",841,581,1,WHITE,984);
        uiButton(1002,571,228,28,8,desknx::ui::ButtonVariant::Chip,
            Color{91,39,48,255},Color{255,255,255,12});
        text("REMOVE TILE  /  MINUS",1018,581,1,WHITE,1220);
    } else if(!editing&&!dragging&&selectedTileIndex>=0&&
              d.tiles[static_cast<size_t>(selectedTileIndex)].kind==Kind::Game) {
        for(int i=0;i<4;++i) {
            const int hx=642+i*146;
            uiButton(hx,571,48,28,8,desknx::ui::ButtonVariant::Chip,
                Color{34,45,63,255},Color{255,255,255,12});
            text(normalHints[i].key,hx+5,581,1,BLUE,hx+43);
            const bool resumeSelected=isBackendSuspendedTile(
                d.tiles[static_cast<size_t>(selectedTileIndex)]);
            text((resumeSelected?resumeHints:normalHints)[i].action,
                 hx+56,581,1,resumeSelected&&i==0?Color{255,203,117,255}:MUTED,
                 hx+144);
        }
    } else if(!editing&&!dragging&&selectedTileIndex>=0&&
              isWidgetKind(d.tiles[static_cast<size_t>(selectedTileIndex)].kind)) {
        uiButton(642,571,190,28,8,desknx::ui::ButtonVariant::Chip,
            Color{35,68,101,255},Color{255,255,255,12});
        text("WIDGET SETTINGS / ZR",654,581,1,WHITE,828);
        for(int i=0;i<3;++i) {
            const int hx=850+i*126;
            uiButton(hx,571,43,28,8,desknx::ui::ButtonVariant::Chip,
                Color{34,45,63,255},Color{255,255,255,12});
            text(normalHints[i].key,hx+4,581,1,BLUE,hx+40);
            text(normalHints[i].action,hx+49,581,1,MUTED,hx+120);
        }
    } else {
        const Hint* hints=dragging?dragHints:(editing?editHints:normalHints);
        for(int i=0;i<4;++i) {
            const int hx=642+i*148;
            uiButton(hx,571,48,28,8,desknx::ui::ButtonVariant::Chip,
                Color{34,45,63,255},Color{255,255,255,12});
            text(hints[i].key,hx+5,581,1,BLUE,hx+43);
            text(hints[i].action,hx+56,581,1,MUTED,hx+144);
        }
    }
    // Three dock treatments share the same geometry so touch hit targets and
    // workspace switching never depend on the selected visual style.
    if(dockStyle==0) {
        softCard(28,624,1224,82,Color{18,26,39,242},Color{255,255,255,24});
    } else if(dockStyle==1) {
        rect(0,620,SW,100,Color{10,20,31,static_cast<Uint8>(
            (static_cast<unsigned int>(surfaceOpacity)*220u)/255u)});
        rect(0,620,SW,2,BLUE);
    } else {
        softCard(28,630,1224,70,Color{13,24,36,226},Color{255,255,255,34});
    }
    const int deskStart=visibleStart();
    for(int i=0;i<VisibleDeskTabs&&deskStart+i<static_cast<int>(desks.size());++i) {
        const int index=deskStart+i;
        const int x=DeskTabX+i*DeskTabStep;
        const bool active=index==deskIndex;
        if(dockStyle==1) {
            uiButton(x,642,DeskTabWidth,42,6,desknx::ui::ButtonVariant::Solid,
                Color{static_cast<Uint8>(themeIndex==1?27:35),53,static_cast<Uint8>(themeIndex==1?68:78),static_cast<Uint8>(active?255:0)},
                Color{0,0,0,0},active,false,BLUE);
            if(active)gUiComponents.divider(x+12,685,DeskTabWidth-24,
                SDL_Color{(BLUE).r,
                          (BLUE).g,
                          (BLUE).b,255});
        } else if(dockStyle==2) {
            uiButton(x,641,DeskTabWidth,45,22,desknx::ui::ButtonVariant::Glass,
                active?Color{51,89,112,210}:Color{255,255,255,12},
                Color{103,184,225,130},active);
        } else {
            uiButton(x,641,DeskTabWidth,48,14,desknx::ui::ButtonVariant::Glass,
                active?Color{53,91,139,255}:Color{34,43,58,235},
                Color{144,189,244,110},active);
        }
        text(desks[static_cast<size_t>(index)].name,x+12,658,1,
             active?WHITE:MUTED,x+DeskTabWidth-8);
    }
    uiButton(770,641,52,48,14,desknx::ui::ButtonVariant::Chip,
        desks.size()<static_cast<size_t>(MAX_DESKS)?
            Color{35,70,105,255}:Color{34,43,58,235},
        Color{255,255,255,18},false,false,
        desks.size()<static_cast<size_t>(MAX_DESKS)?BLUE:MUTED);
    text("+",787,652,3,desks.size()<static_cast<size_t>(MAX_DESKS)?BLUE:MUTED,812);
    uiButton(832,641,52,48,14,desknx::ui::ButtonVariant::Chip,
        Color{34,43,58,235},Color{255,255,255,18});
    text("...",842,660,2,WHITE,876);
    text(std::to_string(deskIndex+1)+" / "+std::to_string(desks.size()),
         916,658,1,MUTED,1040);
    uiButton(1096,638,128,54,16,desknx::ui::ButtonVariant::Glass,
        editing?Color{53,100,156,255}:Color{34,46,64,255},
        Color{144,189,244,110},editing);
    drawSystemIcon(desknx::ui::IconId::Home,1108,650,30,WHITE);
    text(editing?"DONE":"EDIT",1148,657,2,WHITE,1216);
    gModalLayer.compose(gSurfaceManager.dimAlpha());
    if(emptyDesktopSurface&&!gSurfaceManager.hasBlockingSurface()) {
        softCard(350,248,580,204,Color{19,29,43,250},Color{255,255,255,38});
        roundRect(386,276,50,50,15,Color{38,76,119,255});
        drawSystemIcon(desknx::ui::IconId::Library,399,289,24,WHITE);
        text("THIS DESKTOP IS EMPTY",456,276,3,WHITE,890);
        text("Add games, homebrew or widgets to build your workspace.",
             456,319,2,MUTED,890);
        uiButton(386,376,206,48,13,desknx::ui::ButtonVariant::Solid,
            Color{43,111,190,255},Color{255,255,255,35},true,false,BLUE);
        text("Y  ADD APPS",431,391,2,WHITE,570);
        uiButton(604,376,284,48,13,desknx::ui::ButtonVariant::Chip,
            Color{32,45,63,255},Color{255,255,255,20});
        text("+  CREATE ANOTHER DESKTOP",626,391,1,WHITE,872);
    }
    if(gSurfaceManager.isTop(SurfaceId::Library)) {

        const int mx=110,my=70,mw=1060,mh=570;
        softCard(mx,my,mw,mh,
            themeIndex==1?Color{10,20,30,252}:Color{17,24,36,255},
            themeIndex==1?Color{89,144,169,110}:Color{255,255,255,25});
        if(themeIndex==1) {
            softCard(mx+18,my+14,mw-36,88,Color{15,32,46,246},
                     Color{100,157,181,68});
            rect(mx+18,my+14,4,88,BLUE);
            text("SOCOMA  /  APPLICATION CONTROL",mx+36,my+20,1,BLUE,mx+mw-30);
            text("APP LIBRARY",mx+36,my+39,3,WHITE,mx+mw-24);
        } else {
            text("APP LIBRARY",mx+28,my+17,3,WHITE,mx+mw-24);
            text("Installed games, homebrew and system widgets",
                 mx+30,my+43,1,MUTED,mx+mw-24);
        }
        struct DrawerChip { int x; int w; const char* label; };
        const DrawerChip chips[]={{mx+28,96,"Y  SEARCH"},{mx+132,82,"X  SORT"},
            {mx+222,112,"L/R  FILTER"},{mx+342,82,"-  CLEAR"},
            {mx+432,126,"R STICK RESCAN"},{mx+566,72,"B  CLOSE"},
            {mx+646,72,"A  ADD"}};
        const int chipY=my+(themeIndex==1?64:52);
        for(const auto& chip:chips) {
            uiButton(chip.x,chipY,chip.w,25,themeIndex==1?7:9,
                themeIndex==1?desknx::ui::ButtonVariant::Chip:
                              desknx::ui::ButtonVariant::Solid,
                themeIndex==1?Color{22,43,56,255}:Color{31,43,61,255},
                Color{93,143,166,90},false,false,
                BLUE);
            text(chip.label,chip.x+7,chipY+8,1,WHITE,chip.x+chip.w-5);
        }
        std::string viewLabel=std::string("FILTER: ")+
            desknx::catalog_view::FilterName(catalogueFilter)+"     SORT: "+
            desknx::catalog_view::SortName(catalogueSort);
        if(!catalogueQuery.empty())viewLabel+="     SEARCH: "+catalogueQuery;
        if(!catalogCompleteNow)viewLabel+="     /  UPDATING";
        if(catalogueNsErrorNow!=0) {
            char nsError[40]{};
            std::snprintf(nsError,sizeof(nsError),"     /  NS  0X%08X",
                static_cast<unsigned int>(catalogueNsErrorNow));
            viewLabel+=nsError;
        }
        text(viewLabel,mx+28,my+(themeIndex==1?96:82),1,
             BLUE,mx+mw-22);
        if(themeIndex!=1)
            text("SELECT AN APP TO SEE DETAILS",mx+28,my+96,1,MUTED,mx+760);
        if(available.empty()) { listIndex=0; listOffset=0; }
        else {
            listIndex=std::max(0,std::min(listIndex,static_cast<int>(available.size())-1));
            listOffset=desknx::library_grid::offsetForSelection(
                listIndex,static_cast<int>(available.size()));
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
            constexpr int cardX=138,cardY=180,cardW=232,cardH=120;
            constexpr int cardStepX=242,cardStepY=130;
            for(int row=0;row<desknx::library_grid::VisibleRows;++row) {
                for(int column=0;column<desknx::library_grid::Columns;++column) {
                    const int i=desknx::library_grid::indexAt(listOffset,row,column,
                        static_cast<int>(available.size()));
                    if(i<0)continue;
                    const Candidate& candidate=available[static_cast<size_t>(i)];
                    const int x=cardX+column*cardStepX,y=cardY+row*cardStepY;
                    const bool selectedCard=i==listIndex;
                    softCard(x,y,cardW,cardH,
                        selectedCard?Color{36,60,91,255}:Color{20,29,43,255},
                        selectedCard?Color{106,175,255,190}:Color{255,255,255,20});
                    Tile iconTile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
                    drawTileIcon(iconTile,x+(cardW-66)/2,y+7,66,selectedCard);
                    if(selectedCard)outline(x+1,y+1,cardW-2,cardH-2,focusAccent());
                    textCentered(candidate.title,x+cardW/2,y+79,2,
                        selectedCard?WHITE:Color{201,211,225,255},x+8,x+cardW-8);
                    std::string appBadge=typeName(candidate.kind);
                    Color badgeColor=selectedCard?BLUE:MUTED;
                    if(candidate.kind==Kind::Game) {
                        appBadge=candidate.displayVersion.empty()
                            ?(languageRussian?"ВЕРСИЯ Н/Д":"VERSION N/A")
                            :"v"+candidate.displayVersion;
                        badgeColor=!candidate.updateStatusAvailable?MUTED:
                            (candidate.viewFlags&NsViewFlagNeedsUpdate)
                                ?Color{255,196,87,255}:GREEN;
                    }
                    textCentered(appBadge,x+cardW/2,y+101,1,badgeColor,x+8,x+cardW-8);
                }
            }
        }
        roundRect(mx+780,my+110,252,438,16,Color{22,31,45,255});
        if(!available.empty()) {
            const Candidate& selected=available[static_cast<size_t>(
                std::max(0,std::min(listIndex,static_cast<int>(available.size())-1)))];
            Tile preview{selected.kind,selected.title,selected.data,0,0,1,1,selected.color};
            drawTileIcon(preview,mx+866,my+128,78,true);
            text(selected.title,mx+798,my+218,2,WHITE,mx+mw-30);
            std::string detailType=typeName(selected.kind);
            Color detailColor=BLUE;
            if(selected.kind==Kind::Game) {
                detailType=selected.displayVersion.empty()
                    ?(languageRussian?"УСТАНОВЛЕНА: Н/Д":"INSTALLED: N/A")
                    :(languageRussian?"УСТАНОВЛЕНА: ":"INSTALLED: ")+selected.displayVersion;
                detailColor=!selected.updateStatusAvailable?MUTED:
                    (selected.viewFlags&NsViewFlagNeedsUpdate)
                        ?Color{255,196,87,255}:GREEN;
            }
            text(detailType,mx+800,my+252,1,detailColor,mx+mw-30);
            if(selected.kind==Kind::Game) {
                text("TITLE ID",mx+800,my+278,1,MUTED,mx+mw-30);
                text(selected.data,mx+800,my+296,1,WHITE,mx+mw-30);
                if(selected.occupiedSizeAvailable&&selected.occupiedSizeBytes>0) {
                    char sizeLabel[64]{};
                    const double gib=static_cast<double>(selected.occupiedSizeBytes)/(1024.0*1024.0*1024.0);
                    if(gib>=1.0)std::snprintf(sizeLabel,sizeof(sizeLabel),"INSTALLED SIZE  %.2f GB",gib);
                    else std::snprintf(sizeLabel,sizeof(sizeLabel),"INSTALLED SIZE  %.0f MB",
                        static_cast<double>(selected.occupiedSizeBytes)/(1024.0*1024.0));
                    text(sizeLabel,mx+800,my+318,1,MUTED,mx+mw-30);
                } else {
                    text("INSTALLED SIZE  --",mx+800,my+318,1,MUTED,mx+mw-30);
                }
                unsigned onlinePlayers=0;
                const bool onlineFresh=nextendoPlayersSnapshotForTitle(selected.data,onlinePlayers);
                char onlineLabel[64]{};
                if(onlineFresh)std::snprintf(onlineLabel,sizeof(onlineLabel),"ONLINE  %u PLAYERS",onlinePlayers);
                else std::snprintf(onlineLabel,sizeof(onlineLabel),"ONLINE  UNKNOWN (NEXTENDO)");
                text(onlineLabel,mx+800,my+340,1,onlineFresh?(onlinePlayers?GREEN:MUTED):MUTED,mx+mw-30);
                if(!selected.playStatsQueried) {
                    text("PLAY STATS NOT LOADED",mx+800,my+362,1,MUTED,mx+mw-30);
                } else if(!selected.playStatsAvailable) {
                    text("PLAY STATS UNAVAILABLE",mx+800,my+362,1,RED,mx+mw-30);
                } else {
                    char playLabel[48]{};
                    const unsigned long long minutes=
                        static_cast<unsigned long long>(selected.playtimeSeconds/60);
                    if(minutes<60)
                        std::snprintf(playLabel,sizeof(playLabel),"PLAY TIME  %llu MIN",minutes);
                    else
                        std::snprintf(playLabel,sizeof(playLabel),"PLAY TIME  %lluH %02lluM",
                                      minutes/60,minutes%60);
                    text(playLabel,mx+800,my+362,1,MUTED,mx+mw-30);
                }
                char launchLabel[48]{};
                std::snprintf(launchLabel,sizeof(launchLabel),"LAUNCHES  %u",selected.launchCount);
                text(launchLabel,mx+800,my+384,1,MUTED,mx+mw-30);
                if(selected.lastPlayed!=0) {
                    const std::time_t stamp=static_cast<std::time_t>(selected.lastPlayed);
                    std::tm local{};char dateLabel[48]{};
                    if(localtime_r(&stamp,&local)&&
                       std::strftime(dateLabel,sizeof(dateLabel),"LAST PLAYED  %d.%m.%Y",&local))
                        text(dateLabel,mx+800,my+406,1,MUTED,mx+mw-30);
                } else {
                    text("NOT PLAYED YET",mx+800,my+406,1,MUTED,mx+mw-30);
                }
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
            } else {
                text("SYSTEM WIDGET",mx+800,my+282,1,MUTED,mx+mw-30);
                text("Adds live information to your desktop.",mx+800,my+302,1,WHITE,mx+mw-30);
            }
            text(std::to_string(listIndex+1)+" OF "+std::to_string(available.size()),
                 mx+800,my+438,1,MUTED,mx+mw-30);
            roundRect(mx+792,my+462,228,38,11,Color{49,119,199,255});
            text("A  ADD TO WORKSPACE",mx+823,my+475,1,WHITE,mx+mw-30);
            const int actionY=my+508;
            if(selected.kind==Kind::Game&&editing) {
                uiButton(mx+824,actionY,88,32,9,desknx::ui::ButtonVariant::Chip,
                    Color{43,61,85,255},Color{255,255,255,18});
                text("NAME",mx+850,actionY+11,1,WHITE,mx+908);
                uiButton(mx+918,actionY,100,32,9,desknx::ui::ButtonVariant::Chip,
                    Color{39,83,119,255},Color{106,175,255,80},false,false,BLUE);
                text("COVER",mx+946,actionY+11,1,WHITE,mx+1010);
            } else if(selected.kind==Kind::Game) {
                uiButton(mx+864,actionY,154,32,9,
                    desknx::ui::ButtonVariant::Chip,
                    Color{39,83,119,255},Color{106,175,255,80},false,false,BLUE);
                text("EDIT MODE",mx+908,actionY+11,1,WHITE,mx+1016);
            }
        }
        const std::string counts=languageRussian
            ?std::to_string(catalogueGameCountNow)+" ИГР  /  "+
             std::to_string(catalogueNroCountNow)+" HOMEBREW  /  "+
             std::to_string(available.size())+" ПОКАЗАНО"
            :std::to_string(catalogueGameCountNow)+" GAMES  /  "+
             std::to_string(catalogueNroCountNow)+" HOMEBREW  /  "+
             std::to_string(available.size())+" SHOWN";
        text(counts,mx+28,my+mh-26,1,MUTED,mx+mw-30);
    }
    if(gSurfaceManager.isTop(SurfaceId::WorkspaceMenu)) {

        softCard(340,176,600,370,Color{18,25,37,255},Color{255,255,255,26});
        text("WORKSPACE OPTIONS",390,194,3,WHITE,900);
        const Workspace& current=desks[static_cast<size_t>(deskIndex)];
        const bool canMoveTile=editing&&itemAt(current,selectedX,selectedY)>=0;
        const char* actions[]={"OPEN WORKSPACE","MOVE ICON TO ANOTHER DESKTOP","CUSTOMIZE DESKTOP",
                               "RENAME WORKSPACE","DELETE WORKSPACE","CANCEL"};
        for(int i=0;i<6;++i) {
            const int yy=238+i*44;
            if(i==workspaceMenuIndex)roundRect(390,yy,500,38,10,Color{45,61,82,255});
            const Color labelColor=(i==1&&!canMoveTile)?Color{91,101,118,255}:
                                   (i==workspaceMenuIndex?WHITE:MUTED);
            text(actions[i],412,yy+12,1,labelColor,875);
        }
        text("EDIT MODE: SELECT A TILE BEFORE MOVING IT",390,512,1,MUTED,900);
        text("D-PAD + L/R WHILE DRAGGING ALSO MOVES BETWEEN DESKTOPS",
             390,528,1,BLUE,920);
    }
    if(gSurfaceManager.isTop(SurfaceId::WorkspaceSettings)) {

        softCard(300,104,680,520,Color{17,24,36,255},Color{255,255,255,28});
        const Workspace& desk=desks[static_cast<size_t>(deskIndex)];
        text("CUSTOMIZE DESKTOP",344,130,3,WHITE,930);
        text(desk.name+"  /  PERSONAL APPEARANCE",346,168,1,MUTED,930);
        roundRect(842,132,92,32,10,Color{54,57,66,255});
        text("B  BACK",856,143,1,WHITE,926);
        const char* fitNames[]={"COVER","CONTAIN","STRETCH"};
        const char* labels[]={"CHOOSE DESKTOP BACKGROUND","BACKGROUND FIT","GAME ART FIT",
                              "TITLE OVERLAY","ACCENT COLOR","RESET DESKTOP STYLE","DONE"};
        const std::string values[]={
            desk.backgroundPath.empty()?"GLOBAL WALLPAPER":"CUSTOM IMAGE",
            fitNames[std::max(0,std::min(2,desk.backgroundFit))],
            fitNames[std::max(0,std::min(2,desk.artworkMode))],
            std::to_string(desk.labelOpacity)+" / 255",
            desk.accentColor<0?"AUTO / TILE COLORS":"PALETTE "+std::to_string(desk.accentColor+1),
            "RESTORE DEFAULTS","CLOSE THIS MENU"
        };
        for(int i=0;i<7;++i) {
            const int yy=216+i*48;
            if(i==workspaceSettingsIndex)roundRect(340,yy,600,40,10,Color{35,61,93,255});
            text(labels[i],360,yy+8,1,i==workspaceSettingsIndex?WHITE:MUTED,700);
            text(values[i],710,yy+8,1,i==workspaceSettingsIndex?BLUE:MUTED,920);
        }
        text("COVER FILLS THE TILE; CONTAIN SHOWS THE WHOLE IMAGE",346,568,1,MUTED,930);
        text("UP / DOWN SELECT     A CHANGE     B CLOSE",346,606,1,BLUE,930);
    }
    if(gSurfaceManager.isTop(SurfaceId::WorkspacePicker)) {

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
        text(workspacePickerMoveTile?"A MOVE HERE     B CANCEL":"A OPEN     B BACK",
             360,608,1,MUTED,730);
    }
    if(gSurfaceManager.isTop(SurfaceId::DeleteConfirmation)) {

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
    if(gSurfaceManager.isTop(SurfaceId::RecentGames)) {
        const auto entries=recentEntriesForModal();

        softCard(230,78,820,544,Color{17,24,36,255},Color{255,255,255,25});
        text(recentGamesOnly?"RECENT GAMES":"RECENTLY OPENED",266,104,3,WHITE,850);
        roundRect(838,108,78,30,9,Color{49,52,61,255});
        text("B BACK",850,118,1,WHITE,912);
        roundRect(924,108,88,30,9,Color{10,132,255,255});
        text("LAUNCH",936,118,1,WHITE,1006);
        text("TAP TO SELECT     A / LAUNCH BUTTON TO START",282,153,1,MUTED,850);
        if(entries.empty()) {
            text(recentGamesOnly?"NO RECENT GAMES YET":"NO RECENT LAUNCHES YET",310,245,2,WHITE,970);
            text("LAUNCH AN APP TO SEE IT HERE",310,280,1,MUTED,970);
        } else {
            recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(entries.size())-1));
            const int visible=std::min(10,static_cast<int>(entries.size()));
            for(int row=0;row<visible;++row) {
                const auto& entry=entries[static_cast<size_t>(row)];
                const int yy=190+row*39;
                if(row==recentIndex)roundRect(278,yy,724,35,9,Color{45,61,82,255});
                Tile iconTile{entry.kind,entry.title,entry.data,0,0,1,1,row%12};
                drawTileIcon(iconTile,290,yy+4,27,row==recentIndex);
                text(entry.title,330,yy+8,2,row==recentIndex?WHITE:MUTED,850);
                text(entry.kind==Kind::Game?"GAME":"HOMEBREW",870,yy+11,1,
                     row==recentIndex?BLUE:MUTED,990);
            }
        }
        text("A  LAUNCH SELECTED     B  CLOSE",282,594,1,BLUE,850);
    }
    if(gSurfaceManager.isTop(SurfaceId::Favorites)) {
        favoritesView=favoriteTiles();
        if(favoritesView.empty())favoritesOpen=false;
        else {
            favoritesIndex=std::max(0,std::min(favoritesIndex,
                static_cast<int>(favoritesView.size())-1));
    
            softCard(230,78,820,544,Color{17,24,36,255},Color{255,255,255,25});
            text("FAVORITE GAMES",266,104,3,WHITE,850);
            roundRect(838,108,78,30,9,Color{49,52,61,255});
            text("B BACK",850,118,1,WHITE,912);
            roundRect(924,108,88,30,9,Color{10,132,255,255});
            text("LAUNCH",936,118,1,WHITE,1006);
            text("A  LAUNCH     MINUS  REMOVE FROM FAVORITES",282,153,1,MUTED,850);
            if(favoritesIndex<favoritesOffset)favoritesOffset=favoritesIndex;
            if(favoritesIndex>=favoritesOffset+10)favoritesOffset=favoritesIndex-9;
            const int visible=std::min(10,static_cast<int>(favoritesView.size())-favoritesOffset);
            for(int row=0;row<visible;++row) {
                const int index=favoritesOffset+row;
                const auto& game=favoritesView[static_cast<std::size_t>(index)];
                const int yy=190+row*39;
                if(index==favoritesIndex)roundRect(278,yy,724,35,9,Color{45,61,82,255});
                drawTileIcon(game,290,yy+4,27,index==favoritesIndex);
                text(game.title,330,yy+8,2,index==favoritesIndex?WHITE:MUTED,950);
                text("★",970,yy+8,2,GREEN,996);
            }
            text(std::to_string(favoritesView.size())+" FAVORITES SAVED",
                 282,594,1,BLUE,850);
        }
    }
    if(gSurfaceManager.isTop(SurfaceId::WidgetEditor)) {
        Tile* tile=selectedWidgetTile();
        if(!tile)widgetEditorOpen=false;
        else {
            const auto config=widgetConfig(*tile);
            const std::string sizeValue=std::to_string(tile->w)+" x "+std::to_string(tile->h);
            const std::string refreshValue=std::to_string(config.refreshSeconds)+" SEC";
            const std::string itemValue=std::to_string(config.itemCount)+" ITEMS";
            const std::string values[]={
                sizeValue,widgetEditorStyleLabel(tile->kind,config.style),
                desknx::widgets::enabled(config,0)?"ON":"OFF",
                desknx::widgets::enabled(config,1)?"ON":"OFF",
                refreshValue,itemValue,"DONE"
            };
    
            softCard(260,82,760,548,Color{17,24,36,255},Color{255,255,255,28});
            text("WIDGET EDITOR",300,110,3,WHITE,970);
            text(typeName(widgetEditorKind)+"  /  CHANGES SAVE AUTOMATICALLY",
                 302,150,1,MUTED,970);
            roundRect(900,108,88,34,10,Color{52,61,74,255});
            text("B BACK",916,119,1,WHITE,980);
            for(int row=0;row<7;++row) {
                const int y=188+row*52;
                const bool selected=row==widgetEditorRow;
                roundRect(294,y,692,44,10,selected?
                    Color{39,69,103,255}:Color{24,34,49,255});
                const std::string label=row==0?"SIZE":
                    row==1?"STYLE":row==2?widgetEditorDataLabel(0):
                    row==3?widgetEditorDataLabel(1):row==4?"REFRESH RATE":
                    row==5?"ITEM COUNT":"FINISH";
                text(label,316,y+15,1,selected?WHITE:MUTED,660);
                text(values[row],700,y+14,1,selected?GREEN:BLUE,876);
                if(row<6) {
                    roundRect(896,y+6,38,32,8,Color{46,58,75,255});
                    text("−",908,y+13,2,WHITE,932);
                    roundRect(940,y+6,38,32,8,Color{46,58,75,255});
                    text("+",952,y+13,2,WHITE,976);
                }
            }
            text("UP / DOWN  ROW     LEFT / RIGHT  CHANGE     A  APPLY",
                 302,600,1,BLUE,970);
        }
    }
    if(gSurfaceManager.isTop(SurfaceId::Nextendo)) {
        const auto popular=nextendoPopularSnapshot();

        softCard(230,78,820,544,Color{17,24,36,255},Color{255,255,255,30});
        text(languageRussian?"NEXTENDO  /  ПОПУЛЯРНЫЕ ИГРЫ":
            "NEXTENDO  /  POPULAR GAMES",266,104,3,WHITE,850);
        text(popular.available?std::to_string(popular.totalPlayers)+" PLAYERS ONLINE":
            (languageRussian?"НЕТ СВЕЖИХ ДАННЫХ":"NO LIVE DATA"),
            268,149,1,popular.available?GREEN:MUTED,850);
        roundRect(926,108,86,32,9,Color{49,52,61,255});
        text("B BACK",940,119,1,WHITE,1003);
        if(!popular.available) {
            text(languageRussian?"ПОДКЛЮЧЕНИЕ К NEXTENDO...":
                "CONNECTING TO NEXTENDO...",290,238,2,MUTED,980);
        } else if(popular.count==0) {
            text(languageRussian?"НЕТ ИГР С АКТИВНЫМ ОНЛАЙНОМ":
                "NO SUPPORTED GAMES ARE ONLINE",290,238,2,MUTED,980);
        } else {
            for(std::size_t i=0;i<popular.count;++i) {
                const int yy=184+static_cast<int>(i)*36;
                const bool selected=static_cast<int>(i)==nextendoSelected;
                if(selected)roundRect(270,yy-4,740,32,9,Color{39,61,88,255});
                roundRect(284,yy+2,26,24,8,Color{32,66,91,255});
                text(std::to_string(i+1),292,yy+9,1,BLUE,305);
                text(popular.games[i].title?popular.games[i].title:"Unknown game",
                     326,yy+8,2,selected?WHITE:MUTED,900);
                text(std::to_string(popular.games[i].players)+" ONLINE",
                     908,yy+10,1,GREEN,996);
            }
        }
        text(languageRussian?"ДАННЫЕ NEXTENDO ОБНОВЛЯЮТСЯ АВТОМАТИЧЕСКИ":
            "NEXTENDO LIVE DATA REFRESHES AUTOMATICALLY",
            270,584,1,MUTED,990);
        text("A / B  CLOSE     UP / DOWN  SELECT",270,600,1,BLUE,990);
    }
    if(gSurfaceManager.isTop(SurfaceId::SystemMenu)) {

        softCard(110,104,1060,512,Color{17,24,36,255},Color{255,255,255,25});
        text("TOOLS",146,130,3,WHITE,1020);
        text("Library, recent apps, widgets and system actions.",
             148,168,1,MUTED,1020);
        struct SystemTool { int x,y,icon; const char* title; const char* subtitle; };
        const SystemTool tools[]={
            {156,218,4,"APP LIBRARY","INSTALLED GAMES AND HOMEBREW"},
            {496,218,6,"RECENT","RECENTLY LAUNCHED APPS"},
            {836,218,3,"POWER","SLEEP, REBOOT OR SHUT DOWN"},
            {156,382,5,"EDIT MODE","MOVE AND RESIZE DESKTOP TILES"},
            {496,382,0,"WIDGETS","ADD CLOCK, STORAGE AND SYSTEM INFO"},
            {836,382,4,"NETWORK QUICK SETTINGS","WI-FI / AIRPLANE / BLUETOOTH"}
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
    if(gSurfaceManager.isTop(SurfaceId::LauncherSettings)) {
        softCard(200,38,880,644,Color{17,24,36,255},Color{255,255,255,42});
        text("LAUNCHER SETTINGS",244,68,3,WHITE,920);
        text("PERSONALIZE  /  APPEARANCE, WIDGETS AND WORKSPACES",
             246,112,2,MUTED,970);
        roundRect(948,64,92,38,12,Color{48,59,76,255});
        text("B BACK",964,77,2,WHITE,1032);
        const char* actions[]={"CHOOSE BACKGROUND IMAGE","CHOOSE CUSTOM FONT","CHOOSE ICON PACK",
            "COLOR THEME","ACCENT COLOUR","STEAMGRIDDB API KEY",
            "ADD WIDGET TO WORKSPACE","WORKSPACE MANAGEMENT","TOGGLE EDIT / ARRANGE MODE",
            "RESCAN GAME LIBRARY","RESET APPEARANCE",
            languageRussian?"LANGUAGE: RUSSIAN":"LANGUAGE: ENGLISH",
            "ICON STYLE","RUNTIME DIAGNOSTICS","HEADER STYLE","DOCK STYLE",
            "SURFACE OPACITY","SELECTION EFFECT","MOTION EFFECTS"};
        const char* iconStyles[]={"CLEAN SVG","SOFT SVG","SVG BADGE"};
        const char* headerStyles[]={"GLASS BAR","TACTICAL RAIL","MINIMAL FLOAT"};
        const char* dockStyles[]={"GLASS DOCK","SEGMENTED RAIL","FLOATING DOCK"};
        const char* selectionStyles[]={"OUTLINE","ACCENT RAIL","PULSE","CORNER BRACKETS"};
        constexpr int VisibleSettingsRows=8;
        appSettingsIndex=std::max(0,std::min(18,appSettingsIndex));
        appSettingsOffset=std::max(0,std::min(11,appSettingsOffset));
        if(appSettingsIndex<appSettingsOffset)appSettingsOffset=appSettingsIndex;
        if(appSettingsIndex>=appSettingsOffset+VisibleSettingsRows)
            appSettingsOffset=appSettingsIndex-VisibleSettingsRows+1;
        for(int row=0;row<VisibleSettingsRows;++row) {
            const int i=appSettingsOffset+row;
            if(i>=19)break;
            const int yy=170+row*47;
            const bool selected=i==appSettingsIndex;
            if(selected)roundRect(242,yy,796,42,11,Color{35,61,93,255});
            else softCard(242,yy,796,42,Color{23,33,48,190},Color{255,255,255,10});
            text(actions[i],264,yy+12,2,selected?WHITE:MUTED,765);
            if(i==3)text(themeName(themeIndex),800,yy+14,1,BLUE,1024);
            if(i==4)text(accentColorName(accentColorIndex),800,yy+14,1,BLUE,1024);
            if(i==5)text(steamGridDbApiKey.empty()?"NOT CONFIGURED":"CONFIGURED",
                800,yy+14,1,steamGridDbApiKey.empty()?MUTED:GREEN,1024);
            if(i==12)text(iconStyles[static_cast<size_t>(iconStyle)],800,yy+14,1,BLUE,1024);
            if(i==14)text(headerStyles[static_cast<size_t>(headerStyle)],800,yy+14,1,BLUE,1024);
            if(i==15)text(dockStyles[static_cast<size_t>(dockStyle)],800,yy+14,1,BLUE,1024);
            if(i==16)text(std::to_string(surfaceOpacity*100/255)+"%",800,yy+14,1,BLUE,1024);
            if(i==17)text(selectionStyles[static_cast<size_t>(selectionStyle)],800,yy+14,1,BLUE,1024);
            if(i==18)text(motionEffects?"ON":"OFF",800,yy+14,1,motionEffects?GREEN:MUTED,1024);
        }
        uiButton(1040,170,36,42,9,desknx::ui::ButtonVariant::Chip,
            Color{34,46,64,255},Color{255,255,255,18});
        text("UP",1045,184,1,WHITE,1074);
        uiButton(1040,499,36,42,9,desknx::ui::ButtonVariant::Chip,
            Color{34,46,64,255},Color{255,255,255,18});
        text("DN",1045,513,1,WHITE,1074);
        char nsInfo[112]{};
        if(!catalogReadyNow&&!catalogCompleteNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY INITIALIZING");
        else if(!catalogueNsAvailableNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"GAME SERVICE OFFLINE  0X%08X",
                          static_cast<unsigned int>(catalogueNsErrorNow));
        else std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY READY");
        text(nsInfo,246,570,1,catalogueNsAvailableNow?GREEN:RED,690);
        text("ITEMS  "+std::to_string(appSettingsOffset+1)+"–"+
             std::to_string(std::min(19,appSettingsOffset+VisibleSettingsRows))+" / 19",
             790,570,1,MUTED,1034);
        text("UP / DOWN SCROLL     LEFT / RIGHT CHANGE     A SELECT     B CLOSE",
             246,620,1,BLUE,1036);
    }
    if(gSurfaceManager.isTop(SurfaceId::Diagnostics)) {
        refreshDiagnosticsSnapshot();
        const auto iconStats=gIconCache.stats();
        const Uint32 lastNetworkSuccess=static_cast<Uint32>(
            SDL_AtomicGet(&nextendoLastSuccessTick));
        const Uint32 networkAge=lastNetworkSuccess==0
            ?0:SDL_GetTicks()-lastNetworkSuccess;
        bool ready=false,complete=false,truncated=false,nsAvailable=false;
        std::uint32_t nsError=0;
        size_t games=0,nros=0;
        readCatalogueState(ready,complete,truncated,&nsAvailable,&nsError,&games,&nros);
        const int failures=SDL_AtomicGet(&nextendoConsecutiveFailures);
        const bool networkReady=SDL_AtomicGet(&nextendoNetworkReady)!=0;

        softCard(120,48,1040,624,Color{17,24,36,255},Color{255,255,255,35});
        text("RUNTIME DIAGNOSTICS",158,72,3,WHITE,920);
        text("B BACK",1010,84,1,BLUE,1140);
        text("X / A REFRESH",880,84,1,MUTED,1000);
        const Color okColor=Color{52,211,153,255};
        const Color warnColor=Color{255,177,74,255};
        const Color badColor=Color{255,99,112,255};
        // Storage and catalogue/cache health.
        softCard(152,126,452,132,Color{23,34,49,255},Color{255,255,255,18});
        text("SD CARD",176,146,2,WHITE,580);
        text(sdMounted?(storageStatsAvailable?"MOUNTED / READABLE":"MOUNTED / SPACE UNKNOWN"):"NOT MOUNTED",
             176,178,1,sdMounted?okColor:badColor,580);
        if(storageStatsAvailable) {
            char storageText[96]{};
            std::snprintf(storageText,sizeof(storageText),"FREE %.1f GB  /  TOTAL %.1f GB",
                static_cast<double>(sdFreeBytes)/(1024.0*1024.0*1024.0),
                static_cast<double>(sdTotalBytes)/(1024.0*1024.0*1024.0));
            text(storageText,176,202,1,MUTED,580);
        }
        char cacheText[96]{};
        std::snprintf(cacheText,sizeof(cacheText),"CACHE FILES %zu  /  %.2f MB",
            diagnosticsCacheFiles,
            static_cast<double>(diagnosticsCacheBytes)/(1024.0*1024.0));
        text(cacheText,176,226,1,BLUE,580);

        softCard(628,126,452,132,Color{23,34,49,255},Color{255,255,255,18});
        text("NETWORK / NEXTENDO",652,146,2,WHITE,1050);
        text(networkReady?"NETWORK SESSION READY":"NETWORK SESSION OFFLINE",
             652,178,1,networkReady?okColor:warnColor,1050);
        char networkText[96]{};
        if(lastNetworkSuccess!=0)
            std::snprintf(networkText,sizeof(networkText),
                "LAST UPDATE %u SEC AGO  /  FAILURES %d",networkAge/1000,failures);
        else
            std::snprintf(networkText,sizeof(networkText),
                "NO SUCCESSFUL UPDATE YET  /  FAILURES %d",failures);
        text(networkText,652,202,1,MUTED,1050);
        text(nextendoTotalLabel(),652,226,1,BLUE,1050);

        softCard(152,278,452,132,Color{23,34,49,255},Color{255,255,255,18});
        text("APPLICATION CATALOGUE",176,298,2,WHITE,580);
        char catalogueText[112]{};
        std::snprintf(catalogueText,sizeof(catalogueText),
            "%s  /  %s  /  %zu GAMES  /  %zu HOMEBREW",
            ready?"READY":"SEEDING",complete?"COMPLETE":"SCANNING",games,nros);
        text(catalogueText,176,330,1,nsAvailable?okColor:warnColor,580);
        char nsText[64]{};
        std::snprintf(nsText,sizeof(nsText),"NS %s  /  RC 0X%08X  /  TRUNCATED %s",
            nsAvailable?"ONLINE":"OFFLINE",static_cast<unsigned int>(nsError),
            truncated?"YES":"NO");
        text(nsText,176,356,1,MUTED,580);
        text("INCREMENTAL METADATA CACHE ACTIVE",176,382,1,BLUE,580);

        softCard(628,278,452,132,Color{23,34,49,255},Color{255,255,255,18});
        text("IMAGE / GPU CACHE",652,298,2,WHITE,1050);
        char gpuText[112]{};
        std::snprintf(gpuText,sizeof(gpuText),
            "%zu / 64 TEXTURES  /  %.1f / 32 MB",
            iconStats.residentTextures,
            static_cast<double>(iconStats.textureBytes)/(1024.0*1024.0));
        text(gpuText,652,330,1,okColor,1050);
        char queueText[112]{};
        std::snprintf(queueText,sizeof(queueText),
            "QUEUE %zu  /  DECODED %zu  /  FAILED %zu",
            iconStats.pendingRequests,iconStats.completedImages,iconStats.failedKeys);
        text(queueText,652,356,1,MUTED,1050);
        text(iconStats.workerRunning?"DECODER THREAD RUNNING":"DECODER THREAD STOPPED",
             652,382,1,iconStats.workerRunning?okColor:warnColor,1050);

        softCard(152,430,452,132,Color{23,34,49,255},Color{255,255,255,18});
        text("BACKGROUND TASKS",176,450,2,WHITE,580);
        char workerText[112]{};
        std::snprintf(workerText,sizeof(workerText),
            "CATALOGUE %s  /  NEXTENDO %s",
            catalogueThread?"STARTED":"OFF",nextendoThread?"RUNNING":"OFF");
        text(workerText,176,482,1,catalogueThread?okColor:warnColor,580);
        std::snprintf(workerText,sizeof(workerText),
            "STORAGE %s  /  PDM %s",
            storageStatsThread?"RUNNING":"OFF",playStatsThread?"RUNNING":"IDLE");
        text(workerText,176,506,1,WHITE,580);
#ifdef DESKNX_SYSTEM_APPLET
        std::snprintf(workerText,sizeof(workerText),
            "LAYOUT SAVE %s  /  IPC %s",
            layoutSaveThread?"RUNNING":"IDLE",heartbeatThread?"ACTIVE":"OFF");
        text(workerText,176,530,1,heartbeatThread?okColor:warnColor,580);
#else
        std::snprintf(workerText,sizeof(workerText),
            "LAYOUT SAVE %s  /  NATIVE NRO",
            layoutSaveThread?"RUNNING":"IDLE");
        text(workerText,176,530,1,MUTED,580);
#endif

        softCard(628,430,452,132,Color{23,34,49,255},Color{255,255,255,18});
        text("FRAME PERFORMANCE",652,450,2,WHITE,1050);
        char perfText[112]{};
        std::snprintf(perfText,sizeof(perfText),"FPS %.1f  /  FRAME %.2f MS AVG",
            runtimeFrameMetrics.fps,runtimeFrameMetrics.averageFrameMs);
        text(perfText,652,482,1,okColor,1050);
        std::snprintf(perfText,sizeof(perfText),"RENDER %.2f MS AVG  /  PEAK %.2F MS",
            runtimeFrameMetrics.averageRenderMs,runtimeFrameMetrics.maximumFrameMs);
        text(perfText,652,506,1,MUTED,1050);
        std::snprintf(perfText,sizeof(perfText),"SLOW FRAMES IN WINDOW %llu",
            static_cast<unsigned long long>(runtimeFrameMetrics.slowFrames));
        text(perfText,652,530,1,warnColor,1050);
        text("LIVE SNAPSHOT UPDATES EVERY 5 SECONDS  /  A OR X TO REFRESH",
             176,604,1,MUTED,1060);
        text("NO ACCOUNT IDS OR PERSONAL FILE CONTENT IS INCLUDED",
             176,628,1,MUTED,1060);
    }
    if(gSurfaceManager.isTop(SurfaceId::MediaPicker)) {

        softCard(220,58,840,604,Color{17,24,36,255},Color{255,255,255,25});
        text(mediaPickerType==MediaPickerType::Wallpaper?"CHOOSE WALLPAPER":
             mediaPickerType==MediaPickerType::WorkspaceBackground?"CHOOSE DESKTOP BACKGROUND":
             mediaPickerType==MediaPickerType::Font?"CHOOSE FONT":
             mediaPickerType==MediaPickerType::GameIcon?"CHOOSE GAME ICON":"CHOOSE ICON PACK",
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
    if(gSurfaceManager.isTop(SurfaceId::PowerMenu)) {
        static const char* kPowerItems[]={"SLEEP","REBOOT","SHUTDOWN"};

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
    if(gSurfaceManager.isTop(SurfaceId::UserPicker)) {

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
                drawSystemIcon(desknx::ui::IconId::User,386,yy+8,24,active?BLUE:MUTED);
                text(consoleUsers[i].nickname,424,yy+13,2,
                     static_cast<int>(i)==userPickerIndex?WHITE:MUTED,790);
                if(active)text("ACTIVE",816,yy+14,1,GREEN,898);
            }
        }
        text("UP / DOWN SELECT     A USE PROFILE     B CLOSE",374,588,1,BLUE,914);
    }

    if(gSurfaceManager.isTop(SurfaceId::SteamGridDb)) {
        const auto sgdb=gSteamGridDbService.snapshot();

        softCard(64,34,1152,652,Color{15,23,36,255},Color{255,255,255,38});
        text("STEAMGRIDDB  /  GAME ARTWORK",108,62,3,WHITE,900);
        text(steamGridDbCurrentTitle,110,105,2,MUTED,820);
        char sgdbIdText[48]{};
        std::snprintf(sgdbIdText,sizeof(sgdbIdText),"TITLE ID  %016llX",
            static_cast<unsigned long long>(steamGridDbCurrentTitleId));
        text(sgdbIdText,110,133,1,MUTED,650);
        roundRect(1082,62,94,38,10,Color{52,61,74,255});
        text("B  BACK",1098,75,1,WHITE,1168);

        if(steamGridDbPickerView==SteamGridDbPickerView::Matches) {
            text("CHOOSE THE CORRECT GAME ENTRY",112,171,1,BLUE,850);
            if(sgdb.status==desknx::steamgriddb::ServiceStatus::Searching) {
                roundRect(132,235,1016,86,14,Color{24,38,56,255});
                text("SEARCHING STEAMGRIDDB...",164,266,2,WHITE,1090);
            } else if(sgdb.games.empty()) {
                roundRect(132,235,1016,86,14,Color{24,38,56,255});
                text(sgdb.message.empty()?"NO MATCHES FOUND":sgdb.message,164,266,2,WHITE,1100);
            } else {
                const int count=std::min(8,static_cast<int>(sgdb.games.size())-steamGridDbMatchOffset);
                for(int row=0;row<count;++row) {
                    const int index=steamGridDbMatchOffset+row;
                    const int y=198+row*47;
                    const bool selected=index==steamGridDbMatchIndex;
                    roundRect(126,y,1028,40,10,selected?Color{40,72,111,255}:Color{25,35,49,255});
                    text(sgdb.games[static_cast<size_t>(index)].name,150,y+12,1,
                        selected?WHITE:MUTED,880);
                    char score[24]{};
                    std::snprintf(score,sizeof(score),"%u%%",
                        static_cast<unsigned>(std::max(0.0f,std::min(1.0f,
                            sgdb.games[static_cast<size_t>(index)].matchScore))*100.0f));
                    text(score,1046,y+12,1,selected?GREEN:BLUE,1126);
                }
            }
            text(sgdb.message,130,590,1,MUTED,780);
            roundRect(842,574,160,42,11,Color{37,80,128,255});
            text("A  SELECT",878,588,1,WHITE,990);
            roundRect(1016,574,136,42,11,Color{34,45,62,255});
            text("Y  SEARCH",1040,588,1,WHITE,1140);
            text("UP / DOWN  SELECT MATCH",132,635,1,BLUE,520);
        } else {
            text(steamGridDbOfflineFallback?"OFFLINE LIBRARY  /  CACHED ARTWORK":
                (sgdb.message.empty()?"CHOOSE ARTWORK TYPE AND IMAGE":sgdb.message),
                112,164,1,steamGridDbOfflineFallback?GREEN:BLUE,900);
            static const int tabX[]={116,374,632,890};
            for(int i=0;i<4;++i) {
                const int x=tabX[i];
                const auto kind=steamGridDbCacheKind(i);
                const bool cached=gSteamGridDbService.cache().hasArtwork(steamGridDbCurrentTitleId,kind);
                roundRect(x,188,238,38,10,i==steamGridDbKindIndex?
                    Color{42,82,126,255}:Color{29,39,54,255});
                text(steamGridDbKindLabel(i),x+18,200,1,
                    i==steamGridDbKindIndex?WHITE:(cached?GREEN:MUTED),x+220);
                if(cached)roundRect(x+208,202,8,8,4,GREEN);
            }
            roundRect(108,238,382,316,14,Color{22,31,45,255});
            if(steamGridDbOfflineFallback) {
                text("CACHED FILES",132,260,1,BLUE,465);
                for(int i=0;i<4;++i) {
                    const bool cached=gSteamGridDbService.cache().hasArtwork(
                        steamGridDbCurrentTitleId,steamGridDbCacheKind(i));
                    const int y=295+i*48;
                    if(i==steamGridDbKindIndex)
                        roundRect(126,y-5,340,38,9,Color{40,68,101,255});
                    text(steamGridDbKindLabel(i),148,y+7,1,
                        i==steamGridDbKindIndex?WHITE:(cached?GREEN:MUTED),290);
                    text(cached?"DOWNLOADED":"NOT CACHED",312,y+7,1,
                        cached?GREEN:MUTED,456);
                }
                text("SELECT A TYPE TO USE ITS SAVED IMAGE",132,512,1,MUTED,470);
            } else if(sgdb.status==desknx::steamgriddb::ServiceStatus::LoadingAssets) {
                text("LOADING ARTWORK...",138,365,2,WHITE,460);
            } else if(sgdb.assets.empty()) {
                text(sgdb.message.empty()?"NO ARTWORK FOUND":sgdb.message,132,360,1,MUTED,466);
            } else {
                text("AVAILABLE IMAGES",132,260,1,BLUE,465);
                const int count=std::min(7,static_cast<int>(sgdb.assets.size())-steamGridDbAssetOffset);
                for(int row=0;row<count;++row) {
                    const int index=steamGridDbAssetOffset+row;
                    const auto& asset=sgdb.assets[static_cast<size_t>(index)];
                    const int y=289+row*36;
                    const bool selected=index==sgdb.selectedAsset;
                    roundRect(124,y,350,31,8,selected?
                        Color{40,72,111,255}:Color{28,38,52,255});
                    char dimensions[48]{};
                    std::snprintf(dimensions,sizeof(dimensions),"%dx%d",
                        asset.width,asset.height);
                    text(dimensions,141,y+9,1,selected?WHITE:MUTED,225);
                    text(std::to_string(asset.score),385,y+9,1,
                        selected?GREEN:MUTED,458);
                }
                text("UP / DOWN TO BROWSE",132,542,1,MUTED,470);
            }
            roundRect(510,238,662,316,14,Color{22,31,45,255});
            if(steamGridDbPreviewTexture) {
                int width=0,height=0;
                if(SDL_QueryTexture(steamGridDbPreviewTexture,nullptr,nullptr,&width,&height)==0&&width>0&&height>0) {
                    const auto fit=desknx::artwork::containDestination(
                        530,254,622,284,width,height);
                    SDL_Rect target{fit.x,fit.y,fit.w,fit.h};
                    SDL_RenderCopy(renderer,steamGridDbPreviewTexture,nullptr,&target);
                }
            } else {
                text("LARGE PREVIEW",714,372,2,MUTED,970);
                text("SELECT AN IMAGE TO PREVIEW",682,410,1,MUTED,970);
            }
            if(!steamGridDbOfflineFallback&&sgdb.selectedAsset>=0&&
               sgdb.selectedAsset<static_cast<int>(sgdb.assets.size())) {
                const auto& asset=sgdb.assets[static_cast<size_t>(sgdb.selectedAsset)];
                text(std::to_string(asset.width)+" x "+std::to_string(asset.height)+
                    "   /   SCORE "+std::to_string(asset.score),530,526,1,BLUE,1150);
            } else if(steamGridDbOfflineFallback) {
                text("SAVED ON SD CARD  /  AVAILABLE WITHOUT INTERNET",530,526,1,GREEN,1150);
            }
            roundRect(108,574,236,42,11,Color{48,52,65,255});
            text("Y  RESTORE SYSTEM",126,588,1,WHITE,330);
            roundRect(358,574,178,42,11,Color{34,45,62,255});
            text("X  SEARCH TITLE",374,588,1,WHITE,525);
            roundRect(854,574,318,42,11,
                steamGridDbOfflineFallback?Color{30,105,79,255}:Color{37,80,128,255});
            text(steamGridDbOfflineFallback?"A  USE CACHED TYPE":"A  APPLY SELECTED",
                steamGridDbOfflineFallback?886:884,588,1,WHITE,1150);
            text("LEFT / RIGHT  ART TYPE     UP / DOWN  IMAGE",112,635,1,BLUE,700);
        }
    }

    if(gSurfaceManager.isTop(SurfaceId::QuickSettings)) {
        refreshQuickSettingsSnapshot();
        softCard(292,72,696,576,Color{16,26,40,255},Color{135,174,216,74});
        text(languageRussian?"БЫСТРЫЕ НАСТРОЙКИ":"QUICK SETTINGS",332,102,3,WHITE,920);
        text(quickNetworkStatus,334,148,2,
            quickInternetConnected?GREEN:(quickAirplaneMode?Color{255,190,96,255}:MUTED),934);
        roundRect(878,94,84,36,10,Color{48,59,76,255});
        text(languageRussian?"B НАЗАД":"B BACK",891,105,1,WHITE,950);
        const std::string labels[]={languageRussian?"ВЫХОД ЗВУКА":"SOUND OUTPUT",
            languageRussian?"АВИАРЕЖИМ":"AIRPLANE MODE","WI-FI","BLUETOOTH",
            languageRussian?"ЯРКОСТЬ ЭКРАНА":"SCREEN BRIGHTNESS"};
        const std::string values[]={
            quickSoundAvailable?(quickSoundMuted?(languageRussian?"ВЫКЛ.":"MUTED"):(languageRussian?"ВКЛ.":"SOUND ON")):(languageRussian?"НЕДОСТУПНО":"UNAVAILABLE"),
            quickAirplaneAvailable?(quickAirplaneMode?(languageRussian?"ВКЛ.":"ON"):(languageRussian?"ВЫКЛ.":"OFF")):(languageRussian?"НЕДОСТУПНО":"UNAVAILABLE"),
            quickWifiAvailable?(quickWifiEnabled&&!quickAirplaneMode?(languageRussian?"ВКЛ.":"ON"):(languageRussian?"ВЫКЛ.":"OFF")):(languageRussian?"НЕДОСТУПНО":"UNAVAILABLE"),
            quickBluetoothAvailable?(quickBluetoothEnabled?(languageRussian?"ВКЛ.":"ON"):(languageRussian?"ВЫКЛ.":"OFF")):(languageRussian?"НЕДОСТУПНО":"UNAVAILABLE"),
            quickBrightnessAvailable?std::to_string(static_cast<int>(std::lround(
                std::max(0.0f,std::min(1.0f,quickBrightness))*100.0f)))+"%":(languageRussian?"НЕДОСТУПНО":"UNAVAILABLE")
        };
        for(int i=0;i<5;++i) {
            const int y=194+i*68;
            const bool selected=i==quickSettingsIndex;
            roundRect(326,y,628,58,12,selected?Color{40,72,111,255}:Color{24,37,54,255});
            text(labels[i],348,y+10,1,MUTED,680);
            text(values[i],348,y+29,2,selected?WHITE:
                (i==1&&quickAirplaneMode?Color{255,190,96,255}:
                 (i==0&&!quickSoundMuted?GREEN:BLUE)),748);
            if(i==4&&quickBrightnessAvailable) {
                const int percent=static_cast<int>(std::lround(quickBrightness*100.0f));
                roundRect(770,y+20,150,12,6,Color{8,15,25,255});
                roundRect(770,y+20,std::max(1,percent*150/100),12,6,BLUE);
            } else if(i<4) {
                roundRect(912,y+18,18,18,9,
                    ((i==0&&!quickSoundMuted)||(i==1&&quickAirplaneMode)||
                     (i==2&&quickWifiEnabled&&!quickAirplaneMode)||
                     (i==3&&quickBluetoothEnabled))?GREEN:MUTED);
            }
        }
        text("A TOGGLE    LEFT / RIGHT ADJUST BRIGHTNESS    UP / DOWN SELECT",
             334,560,1,BLUE,930);
        text("BLUETOOTH IS KEPT ENABLED WHILE DOCKED TO PROTECT CONTROLLER LINKS",
             334,592,1,MUTED,952);
    }

    if(motionEffects&&uiTransitionStart!=0) {
        const Uint32 elapsed=SDL_GetTicks()-uiTransitionStart;
        constexpr Uint32 TransitionDurationMs=150;
        if(elapsed<TransitionDurationMs) {
            const Uint8 alpha=desknx::ui::motion::fadeAlpha(
                elapsed,TransitionDurationMs,70);
            rect(0,0,SW,SH,Color{0,0,0,alpha});
        } else {
            uiTransitionStart=0;
        }
    }
    gVisualEngine.endFrame();
    SDL_RenderPresent(renderer);
}
bool saveNow(){++layoutMutationRevision;return enqueueLayoutSave(false);}
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
void updateCursor(int dx,int dy) {
    if(desks.empty() || (dx==0 && dy==0)) return;
    const Workspace& desk=desks[static_cast<size_t>(deskIndex)];
    desknx::homenx::WorkspaceProjection projection{};
    if(!desknx::homenx::projectWorkspace(desk,projection,nullptr)) {
        // Keep navigation recoverable if an old or manually edited layout is
        // malformed; never index a projection that failed validation.
        selectedX=std::max(0,std::min(COLS-1,selectedX+dx));
        selectedY=std::max(0,std::min(ROWS-1,selectedY+dy));
        return;
    }
    desknx::homenx::Direction direction=desknx::homenx::Direction::Right;
    if(dx<0) direction=desknx::homenx::Direction::Left;
    else if(dx>0) direction=desknx::homenx::Direction::Right;
    else if(dy<0) direction=desknx::homenx::Direction::Up;
    else direction=desknx::homenx::Direction::Down;
    const auto next=desknx::homenx::focusNeighbour(
        projection,selectedX,selectedY,direction);
    if(next.valid()) {
        selectedX=next.x;
        selectedY=next.y;
    } else {
        // Empty cells are not focus stops. Keep focus on the current tile
        // when no tile exists in the requested direction.
        const auto current=desknx::homenx::primaryOf(projection,selectedX,selectedY);
        if(current.valid()) {
            selectedX=current.x;
            selectedY=current.y;
        }
    }
}
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

bool isWidgetKind(Kind kind) {
    switch(kind) {
        case Kind::Clock: case Kind::Battery: case Kind::System:
        case Kind::Recent: case Kind::Storage: case Kind::Calendar:
        case Kind::Nextendo: case Kind::Favorites: case Kind::GameLibrary:
        case Kind::QuickSettings:
            return true;
        default:return false;
    }
}
Tile* selectedWidgetTile() {
    if(desks.empty()||deskIndex<0||deskIndex>=static_cast<int>(desks.size()))return nullptr;
    Workspace& desk=desks[static_cast<size_t>(deskIndex)];
    const int index=itemAt(desk,selectedX,selectedY);
    if(index<0||index>=static_cast<int>(desk.tiles.size()))return nullptr;
    Tile& tile=desk.tiles[static_cast<size_t>(index)];
    return isWidgetKind(tile.kind)?&tile:nullptr;
}
void openWidgetEditor() {
    Tile* tile=selectedWidgetTile();
    if(!tile){setStatus("SELECT A WIDGET FIRST",2000);return;}
    widgetEditorKind=tile->kind;
    widgetEditorRow=0;
    widgetEditorOpen=true;
}
void closeWidgetEditor() {
    widgetEditorOpen=false;
}
std::string widgetEditorStyleLabel(Kind kind,int style) {
    static const char* clock[]={"DIGITAL 24H","DIGITAL 12H","ANALOG"};
    static const char* generic[]={"STANDARD","MINIMAL","DETAILED"};
    switch(kind) {
        case Kind::Clock:return clock[std::max(0,std::min(2,style))];
        case Kind::Battery:return generic[std::max(0,std::min(2,style))];
        case Kind::System:return style==0?"OVERVIEW":style==1?"THERMAL":"FREQUENCIES";
        case Kind::Recent:return style==0?"LIST":style==1?"COVERS":"COMPACT";
        case Kind::Storage:return style==0?"FREE SPACE":style==1?"USED SPACE":"CAPACITY";
        case Kind::Calendar:return style==0?"MONTH":style==1?"DAY CARD":"AGENDA";
        case Kind::Nextendo:return style==0?"ONLINE SUMMARY":style==1?"ACTIVITY FIRST":"NETWORK STATUS";
        case Kind::Favorites:return style==0?"LIST":style==1?"COVER STRIP":"COMPACT";
        case Kind::GameLibrary:return style==0?"LIST":style==1?"COVER STRIP":"COMPACT";
        case Kind::QuickSettings:return style==0?"TOGGLE GRID":style==1?"COMPACT GRID":"STATUS GRID";
        default:return generic[std::max(0,std::min(2,style))];
    }
}
std::string widgetEditorDataLabel(unsigned bit) {
    switch(widgetEditorKind) {
        case Kind::Clock: return bit==0?"SECONDS":"SHOW DATE";
        case Kind::Battery: return bit==0?"CHARGE BAR":"POWER STATE";
        case Kind::System: return bit==0?"TEMPERATURES":"CPU / GPU CLOCKS";
        case Kind::Recent: return bit==0?"MORE ENTRIES":"GAME COVERS";
        case Kind::Storage: return bit==0?"USED OR FREE":"USAGE BAR";
        case Kind::Calendar: return bit==0?"SHOW WEEKDAY":"SHOW TIME";
        case Kind::Nextendo: return bit==0?"ONLINE TOTAL":"TOP ACTIVITY";
        case Kind::Favorites: return bit==0?"SHOW COVER ICONS":"SAVED COUNT";
        case Kind::GameLibrary: return bit==0?"RECENT FIRST":"SHOW COVER ICONS";
        case Kind::QuickSettings: return bit==0?"SHOW BRIGHTNESS":"SHOW NETWORK STATE";
        default:return bit==0?"DATA A":"DATA B";
    }
}
bool resizeWidgetTo(int width,int height) {
    Workspace& desk=desks[static_cast<size_t>(deskIndex)];
    const int index=itemAt(desk,selectedX,selectedY);
    if(index<0)return false;
    Tile& tile=desk.tiles[static_cast<size_t>(index)];
    Tile proposed=tile;
    proposed.w=width;
    proposed.h=height;
    if(!desknx::model::placeable(desk,proposed,tile.x,tile.y,index)) {
        setStatus("WIDGET SIZE DOES NOT FIT HERE",2200);
        return false;
    }
    tile.w=width;
    tile.h=height;
    layoutUserModified=true;
    saveNow();
    return true;
}
void adjustWidgetEditor(int direction) {
    Tile* tile=selectedWidgetTile();
    if(!tile){closeWidgetEditor();return;}
    auto config=widgetConfig(*tile);
    if(direction==0)direction=1;
    switch(widgetEditorRow) {
        case 0: {
            static constexpr int sizes[][2]={{1,1},{2,1},{3,1},{2,2},{3,2},{2,3}};
            int current=0;
            for(int i=0;i<6;++i)if(tile->w==sizes[i][0]&&tile->h==sizes[i][1])current=i;
            for(int step=0;step<6;++step) {
                current=(current+direction+6)%6;
                if(resizeWidgetTo(sizes[current][0],sizes[current][1]))return;
            }
            break;
        }
        case 1:
            config.style=(config.style+direction+3)%3;
            break;
        case 2:
            desknx::widgets::toggle(config,0);
            break;
        case 3:
            desknx::widgets::toggle(config,1);
            break;
        case 4: {
            static constexpr int rates[]={5,15,30,60,180};
            int index=0;
            for(int i=0;i<5;++i)if(rates[i]==config.refreshSeconds)index=i;
            index=(index+direction+5)%5;
            config.refreshSeconds=rates[index];
            break;
        }
        case 5: {
            static constexpr int counts[]={2,3,4,6};
            int index=0;
            for(int i=0;i<4;++i)if(counts[i]==config.itemCount)index=i;
            index=(index+direction+4)%4;
            config.itemCount=counts[index];
            break;
        }
        case 6:
            closeWidgetEditor();
            return;
        default:return;
    }
    tile->data=desknx::widgets::serialize(config);
    if(tile->kind==Kind::Nextendo)
        SDL_AtomicSet(&nextendoConfiguredRefreshMs,configuredWidgetRefresh(Kind::Nextendo)*1000);
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
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
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
    // Normal add flow opens on Games. The widget picker sets its own filter.
    if(!widgetPickerMode && catalogueFilter!=desknx::catalog_view::FilterMode::Games) {
        catalogueFilter=desknx::catalog_view::FilterMode::Games;
        ++catalogueViewRevision;
    }
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
    if(widgetPickerMode||gameLibraryMode) {
        catalogueFilter=widgetPickerPreviousFilter;
        catalogueSort=widgetPickerPreviousSort;
        catalogueQuery=widgetPickerPreviousQuery;
        widgetPickerMode=false;
        gameLibraryMode=false;
        ++catalogueViewRevision;
        listIndex=0;
        listOffset=0;
    }
}
void openGameLibraryCatalogue() {
    widgetPickerPreviousFilter=catalogueFilter;
    widgetPickerPreviousSort=catalogueSort;
    widgetPickerPreviousQuery=catalogueQuery;
    gameLibraryMode=true;
    catalogueFilter=desknx::catalog_view::FilterMode::Games;
    catalogueSort=desknx::catalog_view::SortMode::Name;
    catalogueQuery.clear();
    ++catalogueViewRevision;
    listIndex=0;
    listOffset=0;
    openList();
    setStatus("GAME LIBRARY",1800);
}

void renameSelectedGame() {
    const auto& available=visibleCatalogueSnapshot();
    if(available.empty()||listIndex<0||static_cast<size_t>(listIndex)>=available.size())return;
    const Candidate selected=available[static_cast<size_t>(listIndex)];
    if(selected.kind!=Kind::Game){setStatus("SELECT A GAME TO RENAME",2200);return;}
    SwkbdConfig config{};
    const Result createRc=swkbdCreate(&config,0);
    if(R_FAILED(createRc)){setStatus("KEYBOARD UNAVAILABLE",2500);return;}
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,languageRussian?"Переименовать игру":"Rename game");
    swkbdConfigSetInitialText(&config,selected.title.c_str());
    char output[128]{};
    std::snprintf(output,sizeof(output),"%s",selected.title.c_str());
    const Result rc=showKeyboard(&config,output,sizeof(output));
    swkbdClose(&config);
    if(R_FAILED(rc))return;
    std::string name=output;
    const size_t first=name.find_first_not_of(" \t\r\n");
    if(first==std::string::npos){setStatus("GAME NAME CANNOT BE EMPTY",2200);return;}
    const size_t last=name.find_last_not_of(" \t\r\n");
    name=name.substr(first,last-first+1);
    if(name.size()>124||name.find('\t')!=std::string::npos||
       name.find('\n')!=std::string::npos||name.find('\r')!=std::string::npos) {
        setStatus("GAME NAME TOO LONG (124 UTF-8 BYTES MAX)",3000);return;
    }
    const auto old=gameNameOverrides.find(selected.data);
    const bool hadOld=old!=gameNameOverrides.end();
    const std::string oldName=hadOld?old->second:std::string{};
    if(name==selected.title)return;
    gameNameOverrides[selected.data]=name;
    if(!saveGameNameOverrides()) {
        if(hadOld)gameNameOverrides[selected.data]=oldName;
        else gameNameOverrides.erase(selected.data);
        return;
    }
    for(auto& desk:desks)for(auto& tile:desk.tiles)
        if(tile.kind==Kind::Game&&tile.data==selected.data)tile.title=name;
    ++catalogueViewRevision;
    layoutUserModified=true;
    (void)saveNow();
    setStatus("GAME RENAMED",2200);
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
        case 4: desk.accentColor=desk.accentColor>=static_cast<int>(COLORS.size())-1?-1:desk.accentColor+1; break;
        case 5:
            desk.backgroundPath.clear();desk.backgroundFit=0;desk.artworkMode=0;
            desk.labelOpacity=196;desk.accentColor=-1;break;
        case 6: workspaceSettingsMenu=false;return;
        default:return;
    }
    layoutUserModified=true;
    if(saveNow())setStatus("DESKTOP APPEARANCE SAVED");
}

void activateAppSettings(int action) {
    if(action!=3&&action!=4&&action!=5&&action!=12&&
       (action<14||action>18))
        appSettingsMenu=false;
    switch(action) {
        case 0: openMediaPicker(MediaPickerType::Wallpaper); break;
        case 1: openMediaPicker(MediaPickerType::Font); break;
        case 2: openMediaPicker(MediaPickerType::IconPack); break;
        case 3: cycleTheme(1); break;
        case 4: cycleAccent(1); break;
        case 5: editSteamGridDbApiKey(); break;
        case 6:
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
        case 7:
            workspaceMenu=true;
            workspaceMenuIndex=0;
            break;
        case 8:
            editing=!editing;
            if(!editing&&dragging){dragging=false;saveNow();}
            setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            break;
        case 9: restartCatalogueScan(); break;
        case 10:
            wallpaperPath.clear();
            if(!gFontRenderer.isLoadingCustomFont()) {
                fontPath.clear();
                (void)gFontRenderer.setCustomFont("");
            }
            iconPackPath.clear();
            themeIndex=0;accentColorIndex=-1;iconStyle=0;
            headerStyle=0;dockStyle=0;surfaceOpacity=255;
            selectionStyle=0;motionEffects=false;
            (void)loadWallpaperTexture("");
            gIconCache.setCustomIconPackDir("");
            applyThemePalette();
            scheduleLauncherSettingsSave(0);
            setStatus("APPEARANCE RESET TO V0.1");
            break;
        case 11:
            languageRussian=!languageRussian;
            scheduleLauncherSettingsSave(0);
            setStatus(languageRussian?"ЯЗЫК: РУССКИЙ":"LANGUAGE: ENGLISH");
            break;
        case 12:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18: {
            switch(action) {
                case 12: iconStyle=(iconStyle+1)%3; break;
                case 14: headerStyle=(headerStyle+1)%3; break;
                case 15: dockStyle=(dockStyle+1)%3; break;
                case 16: {
                    const int levels[]={64,112,160,208,255};
                    int pos=0;while(pos<4&&levels[pos]!=surfaceOpacity)++pos;
                    surfaceOpacity=levels[(pos+1)%5];
                    break;
                }
                case 17: selectionStyle=(selectionStyle+1)%4; break;
                case 18: motionEffects=!motionEffects; break;
                default: break;
            }
            applyThemePalette();
            scheduleLauncherSettingsSave(0);
            break;
        }
        case 13:
            appSettingsMenu=false;
            diagnosticsOpen=true;
            refreshDiagnosticsSnapshot(true);
            break;
        default: break;
    }
}
void activateWidget(const Tile& t) {
    desknx::diag::Write("widgets","activate_begin",
        "kind=%s title=%s width=%d height=%d",
        typeName(t.kind).c_str(),t.title.c_str(),t.w,t.h);
    switch(t.kind) {
        case Kind::Nextendo:
            nextendoSelected=0;
            nextendoOpen=true;
            setStatus(languageRussian?"ПОПУЛЯРНЫЕ ИГРЫ NEXTENDO":
                "NEXTENDO POPULAR GAMES");
            break;
        case Kind::Battery:
            setStatus(battery>=0?"BATTERY: "+std::to_string(battery)+"%":
                "BATTERY STATUS UNAVAILABLE");
            break;
        case Kind::Storage:
            setStatus("SD STORAGE WIDGET");
            break;
        case Kind::Calendar:
            setStatus("CALENDAR WIDGET");
            break;
        case Kind::System: {
            const u32 firmware=hosversionGet();
            char detail[64]{};
            std::snprintf(detail,sizeof(detail),"SYSTEM FIRMWARE %u.%u.%u",
                HOSVER_MAJOR(firmware),HOSVER_MINOR(firmware),HOSVER_MICRO(firmware));
            setStatus(detail,3500);
            break;
        }
        case Kind::Clock:
            setStatus("CLOCK WIDGET");
            break;
        case Kind::QuickSettings:
            openQuickSettings();
            break;
        case Kind::Recent:
            if(recentLaunches.empty())setStatus("NO RECENT LAUNCHES YET");
            else {recentIndex=0;recentOpen=true;}
            break;
        default:
            setStatus("WIDGET ACTION UNAVAILABLE");
            break;
    }
    desknx::diag::Write("widgets","activate_end","kind=%s modal=%d",
        typeName(t.kind).c_str(),nextendoOpen?1:0);
}

void launch(const Tile& t) {
    desknx::diag::Write("launch", "requested", "kind=%s",
        t.kind == Kind::Game ? "game" : t.kind == Kind::Nro ? "homebrew" : "widget");
    if (t.kind == Kind::Unsupported) {
        setStatus("UNSUPPORTED TILE TYPE: " + t.unknownTypeCode, 3500);
        return;
    }
    if(t.kind!=Kind::Game && t.kind!=Kind::Nro) {
        // Widgets are UI components, never application launch targets. Keep
        // them out of the HOME/NS/chain-load path that owns game and NRO launches.
        activateWidget(t);
        return;
    }
    drainLauncherSettingsSave();
    if(t.kind==Kind::Game) {
        if(!desknx::model::isTitleId(t.data)){setStatus("INVALID TITLE ID");return;}
        const u64 id=std::strtoull(t.data.c_str(),nullptr,16);
        if(id==0){setStatus("INVALID TITLE ID");return;}
#ifdef DESKNX_SYSTEM_APPLET
        if(g_backendAppSuspended&&g_backendAppTitle==id) {
            if(!saveLayout())return;
            const Result resumeRc=sendIpc([]{
                return desknx::ipc::Send(desknx::ipc::Command::ReturnToGame);
            });
            if(R_FAILED(resumeRc)) {
                setStatus("RETURN TO GAME FAILED",3500);
                return;
            }
            appletRequestExitToSelf();
            running=false;
            return;
        }
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
        const bool appManagesPlayers=R_SUCCEEDED(launchControlRc)&&
            launchControl.nacp.startup_user_account==0;
        const bool hasChosenUser=accountUidIsValid(&uid);
        const u64 flags=appManagesPlayers
            ?desknx::ipc::LaunchFlagNoPreuser
            :(hasChosenUser?desknx::ipc::LaunchFlagHasUser:0);
        desknx::diag::Write("launch","player_policy",
            "title_id=%016llX nacp_rc=0x%08X startup_user=%u has_user=%d flags=0x%llX",
            static_cast<unsigned long long>(id),
            static_cast<unsigned int>(launchControlRc),
            R_SUCCEEDED(launchControlRc)?launchControl.nacp.startup_user_account:255,
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
    if(t.kind==Kind::Battery) {
        setStatus(battery>=0?"BATTERY: "+std::to_string(battery)+"%":"BATTERY STATUS UNAVAILABLE");
    } else if(t.kind==Kind::Storage) {
        setStatus(storageStatsAvailable?"SD STORAGE: "+std::to_string(sdFreeBytes/(1024u*1024u))+" MB FREE":"SD STORAGE UNAVAILABLE");
    } else if(t.kind==Kind::Calendar) {
        setStatus(clockText(true),3000);
    } else if(t.kind==Kind::System) {
        const u32 firmware=hosversionGet();
        char detail[128]{};
        std::snprintf(detail,sizeof(detail),"SYSTEM %.1f C  CPU %u MHz  GPU %u MHz",
            systemSocTemperatureAvailable?systemSocTemperature:0.0f,
            systemCpuClockAvailable?systemCpuClockHz/1000000u:0u,
            systemGpuClockAvailable?systemGpuClockHz/1000000u:0u);
        if(!systemSocTemperatureAvailable&& !systemCpuClockAvailable && !systemGpuClockAvailable)
            std::snprintf(detail,sizeof(detail),"SYSTEM FIRMWARE %u.%u.%u",
                HOSVER_MAJOR(firmware),HOSVER_MINOR(firmware),HOSVER_MICRO(firmware));
        setStatus(detail,3500);
    } else if(t.kind==Kind::Clock) {
        setStatus("LOCAL TIME: "+clockText(),2500);
    } else if(t.kind==Kind::Recent) {
        if(recentEntriesForModal().empty())setStatus("NO RECENT GAMES YET");
        else openRecentGames();
    } else if(t.kind==Kind::Favorites) {
        openFavorites();
    } else if(t.kind==Kind::GameLibrary) {
        openGameLibraryCatalogue();
    } else if(t.kind==Kind::Nextendo) {
        const auto snapshot=nextendoWidgetSnapshot();
        setStatus(snapshot.fresh?"NEXTENDO: "+std::to_string(snapshot.total)+
            " ONLINE / "+std::to_string(snapshot.activeTitles)+" ACTIVE GAMES":
            "NEXTENDO STATUS UNKNOWN",3000);
    } else setStatus(lastLaunched);
}
void keyInput(u64 keys, bool fromRepeat) {
    using desknx::ui::SurfaceId;
    refreshSurfaceManager();
    // A live touch gesture owns focus until its initiating finger is released.
    // This prevents controller buttons from closing the menu or mutating the
    // workspace midway through a touch selection/drag.
    if(touchPressActive||catalogueTouchActive)return;
    if(gSurfaceManager.isTop(SurfaceId::QuickSettings)) {
        if(keys&HidNpadButton_B){closeQuickSettings();return;}
        if(keys&HidNpadButton_Up)quickSettingsIndex=(quickSettingsIndex+4)%5;
        if(keys&HidNpadButton_Down)quickSettingsIndex=(quickSettingsIndex+1)%5;
        if(quickSettingsIndex==4) {
            if(keys&HidNpadButton_Left)adjustQuickBrightness(-1);
            if(keys&HidNpadButton_Right)adjustQuickBrightness(1);
            if(keys&HidNpadButton_A)adjustQuickBrightness(1);
        } else if(keys&HidNpadButton_A)toggleQuickSetting(quickSettingsIndex);
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::WidgetEditor)) {
        if(keys&HidNpadButton_B){closeWidgetEditor();return;}
        if(keys&HidNpadButton_Up)widgetEditorRow=(widgetEditorRow+6)%7;
        if(keys&HidNpadButton_Down)widgetEditorRow=(widgetEditorRow+1)%7;
        if(keys&HidNpadButton_Left)adjustWidgetEditor(-1);
        if(keys&HidNpadButton_Right)adjustWidgetEditor(1);
        if(keys&HidNpadButton_A)adjustWidgetEditor(1);
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::Favorites)) {
        if(keys&HidNpadButton_B){favoritesOpen=false;return;}
        favoritesView=favoriteTiles();
        const int count=static_cast<int>(favoritesView.size());
        if(count<=0){favoritesOpen=false;return;}
        favoritesIndex=std::max(0,std::min(favoritesIndex,count-1));
        if(keys&HidNpadButton_Up)favoritesIndex=(favoritesIndex-1+count)%count;
        if(keys&HidNpadButton_Down)favoritesIndex=(favoritesIndex+1)%count;
        if(favoritesIndex<favoritesOffset)favoritesOffset=favoritesIndex;
        if(favoritesIndex>=favoritesOffset+10)favoritesOffset=favoritesIndex-9;
        if(keys&HidNpadButton_Minus) {
            const Tile selected=favoritesView[static_cast<std::size_t>(favoritesIndex)];
            (void)toggleFavorite(selected);
            favoritesView=favoriteTiles();
            favoritesIndex=std::min(favoritesIndex,std::max(0,static_cast<int>(favoritesView.size())-1));
            if(favoritesIndex<favoritesOffset)favoritesOffset=favoritesIndex;
            if(favoritesIndex>=favoritesOffset+10)favoritesOffset=favoritesIndex-9;
            return;
        }
        if(keys&HidNpadButton_A&&favoritesIndex<count) {
            const Tile selected=favoritesView[static_cast<std::size_t>(favoritesIndex)];
            favoritesOpen=false;
            launch(selected);
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::SteamGridDb)) {
        if(keys&HidNpadButton_B){steamGridDbClosePicker();return;}
        if(steamGridDbPickerView==SteamGridDbPickerView::Matches) {
            const auto state=gSteamGridDbService.snapshot();
            const int count=static_cast<int>(state.games.size());
            if(count>0&&(keys&HidNpadButton_Up)) {
                steamGridDbMatchIndex=(steamGridDbMatchIndex-1+count)%count;
                if(steamGridDbMatchIndex<steamGridDbMatchOffset)
                    steamGridDbMatchOffset=steamGridDbMatchIndex;
            }
            if(count>0&&(keys&HidNpadButton_Down)) {
                steamGridDbMatchIndex=(steamGridDbMatchIndex+1)%count;
                if(steamGridDbMatchIndex>=steamGridDbMatchOffset+8)
                    steamGridDbMatchOffset=steamGridDbMatchIndex-7;
            }
            if(keys&HidNpadButton_A)steamGridDbChooseMatch(steamGridDbMatchIndex);
            if(keys&HidNpadButton_Y)steamGridDbEditSearchTitle();
            if(keys&HidNpadButton_X)steamGridDbSearch(steamGridDbCurrentTitle);
        } else {
            if(keys&HidNpadButton_Left)steamGridDbChangeKind(-1);
            if(keys&HidNpadButton_Right)steamGridDbChangeKind(1);
            if(steamGridDbOfflineFallback) {
                if(keys&HidNpadButton_A)steamGridDbApplyCurrentCached();
            } else {
                if(keys&HidNpadButton_Up)steamGridDbMoveAsset(-1);
                if(keys&HidNpadButton_Down)steamGridDbMoveAsset(1);
                if(keys&HidNpadButton_A)
                    steamGridDbApplyPending=gSteamGridDbService.applySelected();
            }
            if(keys&HidNpadButton_Y)steamGridDbRestoreSystem();
            if(keys&HidNpadButton_X)steamGridDbEditSearchTitle();
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::Nextendo)) {
        if(keys&HidNpadButton_B || keys&HidNpadButton_A) {
            nextendoOpen=false;
            return;
        }
        const auto popular=nextendoPopularSnapshot();
        const int count=static_cast<int>(popular.count);
        if(count>0&&keys&HidNpadButton_Up)
            nextendoSelected=(nextendoSelected-1+count)%count;
        if(count>0&&keys&HidNpadButton_Down)
            nextendoSelected=(nextendoSelected+1)%count;
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::UserPicker)) {
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
    if(gSurfaceManager.isTop(SurfaceId::MediaPicker)) {
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
    if(gSurfaceManager.isTop(SurfaceId::LauncherSettings)) {
        if(keys&HidNpadButton_B){appSettingsMenu=false;return;}
        appSettingsIndex=std::max(0,std::min(18,appSettingsIndex));
        if(keys&HidNpadButton_Up)appSettingsIndex=(appSettingsIndex+18)%19;
        if(keys&HidNpadButton_Down)appSettingsIndex=(appSettingsIndex+1)%19;
        if(appSettingsIndex<appSettingsOffset)appSettingsOffset=appSettingsIndex;
        if(appSettingsIndex>=appSettingsOffset+8)appSettingsOffset=appSettingsIndex-7;
        appSettingsOffset=std::max(0,std::min(11,appSettingsOffset));
        auto adjustSettingsRow=[&](int direction) {
            switch(appSettingsIndex) {
                case 3: cycleTheme(direction); break;
                case 4: cycleAccent(direction); break;
                case 12: iconStyle=(iconStyle+direction+3)%3; break;
                case 14: headerStyle=(headerStyle+direction+3)%3; break;
                case 15: dockStyle=(dockStyle+direction+3)%3; break;
                case 16: {
                    const int levels[]={64,112,160,208,255};
                    int pos=0;while(pos<4&&levels[pos]!=surfaceOpacity)++pos;
                    surfaceOpacity=levels[(pos+direction+5)%5];
                    break;
                }
                case 17: selectionStyle=(selectionStyle+direction+4)%4; break;
                case 18: motionEffects=!motionEffects; break;
                default: return;
            }
            applyThemePalette();
            scheduleLauncherSettingsSave();
        };
        if(keys&HidNpadButton_Left)adjustSettingsRow(-1);
        if(keys&HidNpadButton_Right)adjustSettingsRow(1);
        if(keys&HidNpadButton_A)activateAppSettings(appSettingsIndex);
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::Diagnostics)) {
        if(keys&HidNpadButton_B){diagnosticsOpen=false;return;}
        if(keys&(HidNpadButton_A|HidNpadButton_X))
            refreshDiagnosticsSnapshot(true);
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::WorkspaceSettings)) {
        if(keys&HidNpadButton_B){workspaceSettingsMenu=false;return;}
        if(keys&HidNpadButton_Up)workspaceSettingsIndex=(workspaceSettingsIndex+6)%7;
        if(keys&HidNpadButton_Down)workspaceSettingsIndex=(workspaceSettingsIndex+1)%7;
        if(keys&HidNpadButton_A)activateWorkspaceAppearance(workspaceSettingsIndex);
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::SystemMenu)) {
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
            else if(action==1)openRecentAll();
            else if(action==2){powerMenu=true;powerIndex=0;}
            else if(action==3) {
                editing=!editing;
                if(!editing&&dragging){dragging=false;saveNow();}
                setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            } else if(action==4)activateAppSettings(6);
            else if(action==5)openQuickSettings();
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::WorkspacePicker)) {
        const int count=static_cast<int>(desks.size());
        if(keys&HidNpadButton_B){workspacePicker=false;return;}
        if(count>0&&keys&HidNpadButton_Up)
            workspacePickerIndex=(workspacePickerIndex-1+count)%count;
        if(count>0&&keys&HidNpadButton_Down)
            workspacePickerIndex=(workspacePickerIndex+1)%count;
        if(keys&HidNpadButton_A)selectWorkspace(workspacePickerIndex);
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::WorkspaceMenu)) {
        if(keys&HidNpadButton_B){workspaceMenu=false;return;}
        if(keys&HidNpadButton_Up)
            workspaceMenuIndex=(workspaceMenuIndex+5)%6;
        if(keys&HidNpadButton_Down)
            workspaceMenuIndex=(workspaceMenuIndex+1)%6;
        if(keys&HidNpadButton_A) {
            const int action=workspaceMenuIndex;
            workspaceMenu=false;
            if(action==0)openWorkspacePicker(false);
            else if(action==1)openWorkspacePicker(true);
            else if(action==2){workspaceSettingsMenu=true;workspaceSettingsIndex=0;}
            else if(action==3)renameDesk();
            else if(action==4)deleteDesk();
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::RecentGames)) {
        if(keys&HidNpadButton_B){recentOpen=false;recentGamesOnly=false;return;}
        const auto entries=recentEntriesForModal();
        if(entries.empty()){recentOpen=false;recentGamesOnly=false;return;}
        recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(entries.size())-1));
        if(keys&HidNpadButton_Up)
            recentIndex=(recentIndex-1+static_cast<int>(entries.size()))%static_cast<int>(entries.size());
        if(keys&HidNpadButton_Down)
            recentIndex=(recentIndex+1)%static_cast<int>(entries.size());
        if(keys&HidNpadButton_A) {
            const HistoryEntry entry=entries[static_cast<size_t>(recentIndex)];
            recentOpen=false;
            recentGamesOnly=false;
            launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::PowerMenu)) {
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
    if(gSurfaceManager.isTop(SurfaceId::DeleteConfirmation)) {
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
    if(gSurfaceManager.isTop(SurfaceId::Library)) {
        const auto& available=visibleCatalogueSnapshot();
        if(keys&HidNpadButton_B){closeList();return;}
        if(keys&HidNpadButton_ZL){if(editing)renameSelectedGame();return;}
        if(keys&HidNpadButton_ZR){if(editing)steamGridDbOpenPicker();return;}
        if(keys&HidNpadButton_Y){searchCatalogue();return;}
        if(keys&HidNpadButton_X){cycleCatalogueSort();return;}
        if(keys&HidNpadButton_L){cycleCatalogueFilter(-1);return;}
        if(keys&HidNpadButton_R){cycleCatalogueFilter(1);return;}
        if(keys&HidNpadButton_Minus){catalogueQuery.clear();++catalogueViewRevision;listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);return;}
        if(keys&HidNpadButton_StickR){restartCatalogueScan();return;}
        if(available.empty())return;
        if(listIndex>=static_cast<int>(available.size()))listIndex=static_cast<int>(available.size())-1;
        if(keys&(HidNpadButton_Left|HidNpadButton_Right|
                 HidNpadButton_Up|HidNpadButton_Down)) {
            const int dx=(keys&HidNpadButton_Left)?-1:
                         ((keys&HidNpadButton_Right)?1:0);
            const int dy=(keys&HidNpadButton_Up)?-1:
                         ((keys&HidNpadButton_Down)?1:0);
            const int next=desknx::library_grid::move(
                listIndex,static_cast<int>(available.size()),dx,dy);
            if(next!=listIndex) {
                listIndex=next;
                listOffset=desknx::library_grid::offsetForSelection(
                    listIndex,static_cast<int>(available.size()));
                loadPlayStatsForSelection();
            }
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
    if(!editing&&!dragging&&(keys&HidNpadButton_ZR)) {
        Tile* widget=selectedWidgetTile();
        if(widget)openWidgetEditor();
        else {
            Workspace& desk=desks[static_cast<size_t>(deskIndex)];
            const int index=itemAt(desk,selectedX,selectedY);
            if(index>=0&&desk.tiles[static_cast<size_t>(index)].kind==Kind::Game)
                setStatus("ENTER EDIT MODE TO CHANGE GAME COVERS",2200);
            else setStatus("SELECT A WIDGET",1800);
        }
        return;
    }
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
            else if(i>=0){const Tile&t=d.tiles[static_cast<size_t>(i)];selectedX=t.x;selectedY=t.y;dragging=true;setStatus("D-PAD MOVE; L/R CHANGE DESKTOP; A/B DROP");}
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
void openControllerSupport() {
    const Uint64 started=SDL_GetTicks64();
    HidLaControllerSupportArg arg{};
    hidLaCreateControllerSupportArg(&arg);
    HidLaControllerSupportResultInfo result{};
    desknx::diag::Write("controller","support_applet_begin",
                        "applet_type=%d",static_cast<int>(appletGetAppletType()));
    const Result rc=hidLaShowControllerSupport(&result,&arg);
    desknx::diag::Write("controller","support_applet_end",
        "rc=0x%08X elapsed_ms=%llu",
        static_cast<unsigned int>(rc),
        static_cast<unsigned long long>(SDL_GetTicks64()-started));
    if(R_FAILED(rc))setStatus("PADS MENU UNAVAILABLE",3000);
}

void touchInput(const SDL_Event&e) {
    using desknx::ui::SurfaceId;
    refreshSurfaceManager();
    if(gSurfaceManager.isTop(SurfaceId::Nextendo)) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW);
            const int y=static_cast<int>(e.tfinger.y*SH);
            if(x>=920&&x<=1020&&y>=100&&y<=152) {
                nextendoOpen=false;
            } else if(y>=180&&y<540) {
                const int row=(y-180)/36;
                const auto popular=nextendoPopularSnapshot();
                if(row>=0&&static_cast<std::size_t>(row)<popular.count)
                    nextendoSelected=row;
            }
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::UserPicker)) {
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
    if(gSurfaceManager.isTop(SurfaceId::WidgetEditor)) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW);
            const int y=static_cast<int>(e.tfinger.y*SH);
            if(x>=900&&x<=990&&y>=108&&y<=144) {
                closeWidgetEditor();
                return;
            }
            if(y>=188&&y<552) {
                const int row=(y-188)/52;
                if(row>=0&&row<7) {
                    widgetEditorRow=row;
                    if(row==6)closeWidgetEditor();
                    else if(x>=896&&x<=934)adjustWidgetEditor(-1);
                    else if(x>=940&&x<=978)adjustWidgetEditor(1);
                }
            }
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::Favorites)) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW);
            const int y=static_cast<int>(e.tfinger.y*SH);
            favoritesView=favoriteTiles();
            if(x>=838&&x<=916&&y>=104&&y<=150) {
                favoritesOpen=false;
                return;
            }
            if(x>=924&&x<=1012&&y>=104&&y<=150) {
                if(!favoritesView.empty()) {
                    favoritesIndex=std::max(0,std::min(favoritesIndex,
                        static_cast<int>(favoritesView.size())-1));
                    const Tile selected=favoritesView[static_cast<std::size_t>(favoritesIndex)];
                    favoritesOpen=false;
                    launch(selected);
                }
                return;
            }
            if(y>=190&&y<580&&x>=278&&x<=1002) {
                const int row=(y-190)/39;
                if(row>=0&&static_cast<std::size_t>(row)<favoritesView.size()) {
                    favoritesIndex=row;
                    if(x>=940) {
                        const Tile selected=favoritesView[static_cast<std::size_t>(row)];
                        (void)toggleFavorite(selected);
                        favoritesView=favoriteTiles();
                        favoritesIndex=std::min(favoritesIndex,
                            std::max(0,static_cast<int>(favoritesView.size())-1));
                    }
                }
            }
        }
        return;
    }
    if(gSurfaceManager.isTop(SurfaceId::SteamGridDb)) {
        if(e.type==SDL_FINGERDOWN) {
            steamGridDbTouchTracking=true;
            steamGridDbTouchDragged=false;
            steamGridDbTouchStartX=static_cast<int>(e.tfinger.x*SW);
            steamGridDbTouchStartY=static_cast<int>(e.tfinger.y*SH);
            return;
        }
        if(e.type==SDL_FINGERMOTION&&steamGridDbTouchTracking) {
            const int y=static_cast<int>(e.tfinger.y*SH);
            const int delta=steamGridDbTouchStartY-y;
            if(steamGridDbPickerView==SteamGridDbPickerView::Matches&&
               steamGridDbTouchStartX>=126&&steamGridDbTouchStartX<=1154&&
               steamGridDbTouchStartY>=198&&steamGridDbTouchStartY<574) {
                if(std::abs(delta)>=18)steamGridDbTouchDragged=true;
                const int steps=delta/47;
                if(steps!=0) {
                    const int count=static_cast<int>(gSteamGridDbService.snapshot().games.size());
                    if(count>0) {
                        steamGridDbMatchIndex=((steamGridDbMatchIndex+steps)%count+count)%count;
                        if(steamGridDbMatchIndex<steamGridDbMatchOffset)
                            steamGridDbMatchOffset=steamGridDbMatchIndex;
                        if(steamGridDbMatchIndex>=steamGridDbMatchOffset+8)
                            steamGridDbMatchOffset=steamGridDbMatchIndex-7;
                    }
                    steamGridDbTouchStartY-=steps*47;
                }
            } else if(steamGridDbPickerView==SteamGridDbPickerView::Assets&&
                      !steamGridDbOfflineFallback&&
                      steamGridDbTouchStartX>=124&&steamGridDbTouchStartX<=474&&
                      steamGridDbTouchStartY>=289&&steamGridDbTouchStartY<541) {
                if(std::abs(delta)>=18)steamGridDbTouchDragged=true;
                const int steps=delta/36;
                if(steps!=0) {
                    steamGridDbMoveAsset(steps);
                    steamGridDbTouchStartY-=steps*36;
                }
            }
            return;
        }
        if(e.type==SDL_FINGERUP&&steamGridDbTouchTracking) {
            const int x=static_cast<int>(e.tfinger.x*SW);
            const int y=static_cast<int>(e.tfinger.y*SH);
            const bool tap=!steamGridDbTouchDragged;
            steamGridDbTouchTracking=false;
            steamGridDbTouchDragged=false;
            if(!tap)return;

            if(x>=1082&&x<=1178&&y>=62&&y<=102) {
                steamGridDbClosePicker();
            } else if(steamGridDbPickerView==SteamGridDbPickerView::Matches) {
                if(y>=198&&y<574&&x>=126&&x<=1154) {
                    const int row=(y-198)/47;
                    const int count=static_cast<int>(gSteamGridDbService.snapshot().games.size());
                    const int index=steamGridDbMatchOffset+row;
                    if(index>=0&&index<count) {
                        steamGridDbMatchIndex=index;
                        steamGridDbChooseMatch(index);
                    }
                } else if(x>=842&&x<=1002&&y>=574&&y<=616) {
                    steamGridDbChooseMatch(steamGridDbMatchIndex);
                } else if(x>=1016&&x<=1152&&y>=574&&y<=616) {
                    steamGridDbEditSearchTitle();
                }
            } else {
                static const int tabX[]={116,374,632,890};
                if(y>=188&&y<=226) {
                    for(int i=0;i<4;++i)if(x>=tabX[i]&&x<=tabX[i]+238) {
                        if(i!=steamGridDbKindIndex)steamGridDbChangeKind(i-steamGridDbKindIndex);
                        break;
                    }
                } else if(steamGridDbOfflineFallback&&x>=126&&x<=466&&y>=290&&y<486) {
                    const int index=std::max(0,std::min(3,(y-290)/48));
                    if(index!=steamGridDbKindIndex) {
                        steamGridDbKindIndex=index;
                        steamGridDbDestroyPreview();
                        if(gSteamGridDbService.cache().hasArtwork(steamGridDbCurrentTitleId,
                            steamGridDbCacheKind(index)))
                            steamGridDbPreviewLoadedPath=gSteamGridDbService.cache().pathFor(
                                steamGridDbCurrentTitleId,steamGridDbCacheKind(index));
                    }
                } else if(!steamGridDbOfflineFallback&&x>=124&&x<=474&&y>=289&&y<541) {
                    const int index=steamGridDbAssetOffset+(y-289)/36;
                    const auto state=gSteamGridDbService.snapshot();
                    if(index>=0&&index<static_cast<int>(state.assets.size()))
                        gSteamGridDbService.selectAsset(index);
                } else if(x>=108&&x<=344&&y>=574&&y<=616) {
                    steamGridDbRestoreSystem();
                } else if(x>=358&&x<=536&&y>=574&&y<=616) {
                    steamGridDbEditSearchTitle();
                } else if(x>=854&&x<=1172&&y>=574&&y<=616) {
                    if(steamGridDbOfflineFallback)steamGridDbApplyCurrentCached();
                    else steamGridDbApplyPending=gSteamGridDbService.applySelected();
                }
            }
            return;
        }
        if(e.type==SDL_FINGERUP) {
            steamGridDbTouchTracking=false;
            steamGridDbTouchDragged=false;
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
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::QuickSettings)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=878&&x<=962&&y>=94&&y<=130)closeQuickSettings();
        else if(x>=326&&x<=954&&y>=194&&y<524) {
            const int relativeY=y-194;
            const int row=relativeY/68;
            const int withinRow=relativeY%68;
            if(row>=0&&row<5&&withinRow<58) {
                quickSettingsIndex=row;
                if(row==4) {
                    const float ratio=std::max(0.0f,std::min(1.0f,
                        static_cast<float>(x-770)/150.0f));
                    if(quickBrightnessAvailable) {
                        const int target=static_cast<int>(std::lround(10.0f+ratio*90.0f));
                        const int current=static_cast<int>(std::lround(quickBrightness*100.0f));
                        adjustQuickBrightness(target>current?1:(target<current?-1:0));
                    }
                } else toggleQuickSetting(quickSettingsIndex);
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::Diagnostics)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=1000&&x<=1145&&y>=72&&y<=112)diagnosticsOpen=false;
        else if(x>=840&&x<=1000&&y>=72&&y<=112)refreshDiagnosticsSnapshot(true);
        return;
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::MediaPicker)) {
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
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::LauncherSettings)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=948&&x<=1040&&y>=64&&y<=104) {
            appSettingsMenu=false;
        } else if(x>=1040&&x<=1076&&y>=170&&y<214) {
            appSettingsIndex=std::max(0,appSettingsIndex-1);
            appSettingsOffset=std::max(0,appSettingsIndex-7);
        } else if(x>=1040&&x<=1076&&y>=499&&y<543) {
            appSettingsIndex=std::min(18,appSettingsIndex+1);
            appSettingsOffset=std::min(11,appSettingsIndex-7);
        } else if(x>=242&&x<=1038&&y>=170&&y<546) {
            const int row=(y-170)/47;
            const int index=appSettingsOffset+row;
            if(row>=0&&row<8&&index<19) {
                appSettingsIndex=index;
                activateAppSettings(index);
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::WorkspaceSettings)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=842&&x<=934&&y>=132&&y<=164)workspaceSettingsMenu=false;
        else if(x>=340&&x<=940&&y>=216&&y<552) {
            const int row=(y-216)/48;
            if(row>=0&&row<7){workspaceSettingsIndex=row;activateWorkspaceAppearance(row);}
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::SystemMenu)) {
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
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::WorkspacePicker)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=744&&x<=920&&y>=590&&y<=628) {
            workspacePicker=false;
        } else if(x>=360&&x<=920&&y>=168&&y<168+static_cast<int>(desks.size())*32) {
            const int target=(y-168)/32;
            selectWorkspace(target);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::WorkspaceMenu)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=390&&x<=890&&y>=238&&y<502) {
            const int row=(y-238)/44;
            if(row>=0&&row<6) {
                workspaceMenu=false;
                if(row==0)openWorkspacePicker(false);
                else if(row==1)openWorkspacePicker(true);
                else if(row==2){workspaceSettingsMenu=true;workspaceSettingsIndex=0;}
                else if(row==3)renameDesk();
                else if(row==4)deleteDesk();
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::DeleteConfirmation)) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=382&&y<=430&&x>=405&&x<615) keyInput(HidNpadButton_A,false);
        else if(y>=382&&y<=430&&x>=635&&x<=875) keyInput(HidNpadButton_B,false);
        return;
    }
    if(e.type==SDL_FINGERDOWN && !editing && !dragging && !listOpen &&
       !recentOpen && !favoritesOpen && !widgetEditorOpen && !powerMenu && !appSettingsMenu &&
       !quickSettingsOpen) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=566&&y<=614&&x>=642&&x<832) {
            Workspace& desk=desks[static_cast<std::size_t>(deskIndex)];
            const int index=itemAt(desk,selectedX,selectedY);
            if(index>=0&&isWidgetKind(desk.tiles[static_cast<std::size_t>(index)].kind))
                openWidgetEditor();
            return;
        }
    }
    if(e.type==SDL_FINGERDOWN && editing && !dragging && !listOpen && !recentOpen && !nextendoOpen && !powerMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=566&&y<=614&&itemAt(
               desks[static_cast<size_t>(deskIndex)],selectedX,selectedY)>=0) {
            if(x>=650&&x<815) resizeTile(false);
            else if(x>=826&&x<991) resizeTile(true);
            else if(x>=1002&&x<1230) removeTile();
            return;
        }
    }
    if(e.type==SDL_FINGERDOWN && gSurfaceManager.isTop(SurfaceId::PowerMenu)) {
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
    if(gSurfaceManager.isTop(SurfaceId::RecentGames)) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
            const auto entries=recentEntriesForModal();
            if(x>=838&&x<=916&&y>=104&&y<=150) {
                recentOpen=false;
                recentGamesOnly=false;
            } else if(x>=924&&x<=1012&&y>=104&&y<=150) {
                if(!entries.empty()) {
                    recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(entries.size())-1));
                    const HistoryEntry entry=entries[static_cast<size_t>(recentIndex)];
                    recentOpen=false;
                    recentGamesOnly=false;
                    launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
                }
            } else if(y>=190&&y<580&&!entries.empty()) {
                const int index=(y-190)/39;
                if(index>=0&&static_cast<size_t>(index)<entries.size())
                    recentIndex=index;
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN) {
        int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const bool emptyDeskVisible=!desks.empty()&&deskIndex>=0&&
            deskIndex<static_cast<int>(desks.size())&&
            desks[static_cast<size_t>(deskIndex)].tiles.empty()&&
            !systemMenu&&!appSettingsMenu&&!workspaceMenu&&
            !workspaceSettingsMenu&&!workspacePicker&&!powerMenu&&!diagnosticsOpen&&
            !gSurfaceManager.hasBlockingSurface();
        if(emptyDeskVisible&&y>=376&&y<=424) {
            if(x>=386&&x<592) {
                openList();
                return;
            }
            if(x>=604&&x<888) {
                newDesk();
                return;
            }
        }
        if(gSurfaceManager.isTop(SurfaceId::Library)) {
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
            if(y>=532&&y<572&&x>=900&&x<=1150) {
                addSelected();
                return;
            }
            if(y>=578&&y<=612&&!editing&&x>=974&&x<1128) {
                editing=true;
                setStatus("EDIT MODE ENABLED — COVER OPTIONS READY",2200);
                return;
            }
            if(editing&&y>=578&&y<=612&&x>=930&&x<1018){renameSelectedGame();return;}
            if(editing&&y>=578&&y<=612&&x>=1020&&x<1128){steamGridDbOpenPicker();return;}
            if(y>=180&&y<570&&x>=138&&x<858) {
                const int row=(y-180)/130,column=(x-138)/242;
                if(row>=0&&row<3&&column>=0&&column<3) {
                    const int cardX=138+column*242,cardY=180+row*130;
                    if(x<cardX+232&&y<cardY+120) {
                        const auto& available=visibleCatalogueSnapshot();
                        const int index=desknx::library_grid::indexAt(
                            listOffset,row,column,static_cast<int>(available.size()));
                        if(index>=0) {
                            const Uint32 now=SDL_GetTicks();
                            if(lastCatalogueTap==index&&now-lastCatalogueTapTick<=500) {
                                listIndex=index;addSelected();lastCatalogueTap=-1;
                                catalogueTouchActive=false;catalogueTouchFingerId=0;return;
                            }
                            listIndex=index;
                            listOffset=desknx::library_grid::offsetForSelection(
                                listIndex,static_cast<int>(available.size()));
                            lastCatalogueTap=index;lastCatalogueTapTick=now;
                            loadPlayStatsForSelection();
                            catalogueTouchActive=true;catalogueTouchFingerId=e.tfinger.fingerId;
                            catalogueTouchMoved=false;catalogueTouchStartY=y;catalogueTouchRow=row;
                        }
                    }
                }
                return;
            }
            return;
        }
        if(gSurfaceManager.hasBlockingSurface())return;
        // Three permanent header shortcuts; quick tools stay accessible via L3.
        if(y>=16&&y<=82) {
            if(x>=392&&x<436) {
                systemMenu=true;
                systemMenuIndex=0;
                return;
            }
            if(x>=448&&x<=822) {
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
                    case 1: appSettingsMenu=true;appSettingsIndex=0;appSettingsOffset=0; break;
                    case 2: openControllerSupport(); break;
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
            const int deltaRows=(catalogueTouchStartY-y)/130;
            if(deltaRows!=0) {
                const auto available=visibleCatalogueSnapshot();
                listIndex=desknx::library_grid::move(
                    listIndex,static_cast<int>(available.size()),0,deltaRows);
                listOffset=desknx::library_grid::offsetForSelection(
                    listIndex,static_cast<int>(available.size()));
                loadPlayStatsForSelection();
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
    // libnx normally mounts sdmc during __libnx_init(), before main(). Calling
    // fsdevMountSdmc() again fails because a device with the same name already
    // exists; the old code treated that expected condition as "no SD card".
    // Reuse and probe the pre-mounted device first, and only mount when absent.
    Result sdMountRc = 0;
    const bool sdWasAlreadyMounted =
        fsdevGetDeviceFileSystem("sdmc") != nullptr;
    int sdProbeErrno = 0;
    auto probeSdRoot = [&]() {
        struct statvfs probe{};
        errno = 0;
        const int rc = statvfs("sdmc:/", &probe);
        sdProbeErrno = rc == 0 ? 0 : errno;
        return rc == 0;
    };

    if (sdWasAlreadyMounted) {
        sdMounted = probeSdRoot();
        if (!sdMounted) {
            // The registered device may refer to a card removed between
            // libnx startup and main(). Detach that stale handle and retry
            // the normal mount once; never mount over an existing device.
            const int unmountRc = fsdevUnmountDevice("sdmc");
            if (unmountRc == 0) {
                sdMountRc = fsdevMountSdmc();
                if (R_SUCCEEDED(sdMountRc)) sdMounted = probeSdRoot();
            }
        }
    } else {
        sdMountRc = fsdevMountSdmc();
        if (R_SUCCEEDED(sdMountRc)) sdMounted = probeSdRoot();
        // A concurrent/automatic mount between the existence check and mount
        // is harmless: accept it after verifying the actual SD root.
        if (!sdMounted && fsdevGetDeviceFileSystem("sdmc") != nullptr)
            sdMounted = probeSdRoot();
    }

    desknx::diag::Initialize(sdMounted);
#ifdef DESKNX_SYSTEM_APPLET
    constexpr const char* buildMode = "system-applet";
#else
    constexpr const char* buildMode = "standalone-nro";
#endif
    const AppletType appletType = appletGetAppletType();
    desknx::diag::Write("session", "start",
        "mode=%s applet_type=%u application_mode=%d sd_mounted=%d sd_pre_mounted=%d sd_probe_errno=%d sd_mount_rc=0x%08X",
        buildMode, static_cast<unsigned int>(appletType),
        appletType == AppletType_Application ? 1 : 0,
        sdMounted ? 1 : 0, sdWasAlreadyMounted ? 1 : 0,
        sdProbeErrno, static_cast<unsigned int>(sdMountRc));
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){
        desknx::diag::Write("startup", "sdl_init_failed", "error=%s", SDL_GetError());
        
        return 1;
    }
    desknx::diag::Write("startup", "sdl_init_ok", "sdl_version=%u.%u.%u",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    window=SDL_CreateWindow("Transwitch Launcher",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SW,SH,SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(!window){
        desknx::diag::Write("startup", "window_create_failed", "error=%s", SDL_GetError());
        SDL_Quit();return 2;
    }
    desknx::diag::Write("startup", "window_created", "logical_width=%d logical_height=%d", SW, SH);
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){
        desknx::diag::Write("startup", "renderer_create_failed", "error=%s", SDL_GetError());
        SDL_DestroyWindow(window);SDL_Quit();return 3;
    }
    SDL_RendererInfo rendererInfo{};
    const int rendererInfoRc = SDL_GetRendererInfo(renderer, &rendererInfo);
    rendererVsync=(rendererInfoRc==0)&&(rendererInfo.flags&SDL_RENDERER_PRESENTVSYNC);
    desknx::diag::Write("startup", "renderer_ready", "name=%s info_rc=%d flags=0x%08X vsync=%d",
        rendererInfo.name ? rendererInfo.name : "unknown", rendererInfoRc,
        static_cast<unsigned int>(rendererInfo.flags),rendererVsync?1:0);
    SDL_RenderSetLogicalSize(renderer,SW,SH);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    if(!gVisualEngine.initialize(renderer,SW,SH))
        desknx::diag::Write("visual","engine_init_failed","fallback=direct_sdl_primitives");
    gUiComponents.bind(&gVisualEngine,renderer);
    gUiComponents.setThemeTokens(desknx::ui::ThemeTokens{
        SDL_Color{BLUE.r,BLUE.g,BLUE.b,BLUE.a},
        SDL_Color{WHITE.r,WHITE.g,WHITE.b,220},
        SDL_Color{255,255,255,28},
        UiCornerRadius});
    const auto& visualProfile=gVisualEngine.rendererProfile();
    desknx::diag::Write("visual","renderer_profile",
        "name=%s flags=0x%08X accelerated=%d software=%d vsync=%d target_textures=%d max_texture=%dx%d",
        visualProfile.name.c_str(),static_cast<unsigned int>(visualProfile.flags),
        visualProfile.accelerated?1:0,visualProfile.software?1:0,
        visualProfile.presentVsync?1:0,visualProfile.targetTextures?1:0,
        visualProfile.maxTextureWidth,visualProfile.maxTextureHeight);
    catalogueMutex=SDL_CreateMutex();ipcMutex=SDL_CreateMutex();
    storageCommitMutex=SDL_CreateMutex();
    playStatsMutex=SDL_CreateMutex();
    nextendoMutex=SDL_CreateMutex();
    storageStatsMutex=SDL_CreateMutex();
    nextendoFeed.setEnabled(true);

    // Match SLauncher's startup order: query the real firmware version and
    // publish it to libnx BEFORE initializing version-gated NS services.
    // Initializing NS later from the scan worker while libnx still reports an
    // unknown/default HOS version can make NS calls fail or stall on firmware
    // newer than the build SDK.
    const Result setsysRc = setsysInitialize();
    const bool setsysReady = R_SUCCEEDED(setsysRc);
    setsysServiceReady=setsysReady;
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
    loadFavorites();
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
    loadGameNameOverrides();
    applyGameNameOverridesToWorkspaces();
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
    gWorkspaceBackgroundCache.setRenderer(renderer);
    gIconCache.setRenderer(renderer);
    // The nine authored SVG controls are parsed/rasterized once at startup;
    // thereafter they are ordinary cached SDL textures with no per-frame work.
    gVectorIconCache.setRenderer(renderer);
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
    gIconCache.setGameIconOverridesDir(GAME_ICON_OVERRIDES_DIR);
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
    SDL_AtomicSet(&nextendoConfiguredRefreshMs,
        configuredWidgetRefresh(Kind::Nextendo)*1000);
    if(storageStatsMutex)
        storageStatsThread=SDL_CreateThread(storageStatsThreadMain,"DeskNX storage stats",nullptr);
    if(!storageStatsThread)
        desknx::diag::Write("storage","stats_thread_create_failed","storage widget will show unavailable values");
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
    const Uint64 performanceFrequency=SDL_GetPerformanceFrequency();
    Uint64 metricsWindowStart=SDL_GetPerformanceCounter();
    std::uint64_t metricsFrames=0,metricsSlowFrames=0;
    double metricsFrameSumMs=0.0,metricsRenderSumMs=0.0,metricsMaximumFrameMs=0.0;
    while(running&&appletMainLoop()) {
        const Uint64 frameStartCounter=SDL_GetPerformanceCounter();
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
            const bool saved=saveNow();
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
        pumpLauncherSettingsSave();
        pumpPlayStatsQuery();
        (void)gFontRenderer.pumpWarmup();
        if(gFontRenderer.consumeLoadFailure()) {
            fontPath=gFontRenderer.customFontPath();
            scheduleLauncherSettingsSave(0);
            desknx::diag::Write("font","load_rejected",
                "fallback=%s",fontPath.empty()?"system":fontPath.c_str());
            setStatus(languageRussian
                ?"ШРИФТ НЕ ПОДХОДИТ — ПРЕДЫДУЩИЙ СОХРАНЁН"
                :"FONT REJECTED — PREVIOUS FONT RESTORED",4500);
        }
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT){appletRequestExitToSelf();running=false;}else touchInput(event);}
        pumpSteamGridDbPicker();
        pumpSteamGridDbCacheCleanup();
        const Uint32 persistenceNow=SDL_GetTicks();
        if(activeWorkspaceMarkerPending &&
           static_cast<Sint32>(persistenceNow-activeWorkspaceMarkerDue)>=0)
            (void)flushActiveWorkspaceSave();
        const Uint32 renderStarted=SDL_GetTicks();
        const Uint64 renderStartCounter=SDL_GetPerformanceCounter();
        drawScreen();
        const Uint64 renderEndCounter=SDL_GetPerformanceCounter();
        const double renderMs=performanceFrequency>0
            ?static_cast<double>(renderEndCounter-renderStartCounter)*1000.0/
                static_cast<double>(performanceFrequency):0.0;
        // Present already waits for the display when VSync is active. Sleeping
        // another 16ms here capped the accelerated path at roughly 30 FPS.
        if(!rendererVsync) {
            const Uint32 elapsed=SDL_GetTicks()-renderStarted;
            if(elapsed<16)svcSleepThread(static_cast<s64>(16-elapsed)*1'000'000LL);
        }
        const Uint64 frameEndCounter=SDL_GetPerformanceCounter();
        const double frameMs=performanceFrequency>0
            ?static_cast<double>(frameEndCounter-frameStartCounter)*1000.0/
                static_cast<double>(performanceFrequency):0.0;
        ++metricsFrames;
        metricsFrameSumMs+=frameMs;
        metricsRenderSumMs+=renderMs;
        metricsMaximumFrameMs=std::max(metricsMaximumFrameMs,frameMs);
        if(frameMs>20.0)++metricsSlowFrames;
        if(performanceFrequency>0&&frameEndCounter-metricsWindowStart>=
            performanceFrequency*5) {
            const double windowMs=static_cast<double>(
                frameEndCounter-metricsWindowStart)*1000.0/
                static_cast<double>(performanceFrequency);
            runtimeFrameMetrics.fps=windowMs>0.0
                ?static_cast<double>(metricsFrames)*1000.0/windowMs:0.0;
            runtimeFrameMetrics.averageFrameMs=metricsFrames
                ?metricsFrameSumMs/static_cast<double>(metricsFrames):0.0;
            runtimeFrameMetrics.averageRenderMs=metricsFrames
                ?metricsRenderSumMs/static_cast<double>(metricsFrames):0.0;
            runtimeFrameMetrics.maximumFrameMs=metricsMaximumFrameMs;
            runtimeFrameMetrics.slowFrames=metricsSlowFrames;
            runtimeFrameMetrics.updatedAt=SDL_GetTicks();
            desknx::diag::Write("runtime","frame_metrics",
                "fps=%.2f avg_frame_ms=%.3f avg_render_ms=%.3f max_frame_ms=%.3f slow_frames=%llu",
                runtimeFrameMetrics.fps,runtimeFrameMetrics.averageFrameMs,
                runtimeFrameMetrics.averageRenderMs,runtimeFrameMetrics.maximumFrameMs,
                static_cast<unsigned long long>(runtimeFrameMetrics.slowFrames));
            const auto& visualStats=gVisualEngine.lastFrameStats();
            desknx::diag::Write("visual","frame_primitives",
                "fill_rect=%u rounded_rect=%u rounded_batches=%u outlines=%u gradient_draws=%u gradient_rebuilds=%u failures=%u",
                visualStats.filledRectCalls,visualStats.roundedRectCalls,
                visualStats.roundedFillBatches,visualStats.outlineCalls,
                visualStats.gradientDrawCalls,visualStats.gradientTextureRebuilds,
                visualStats.failedCalls);
            metricsWindowStart=frameEndCounter;
            metricsFrames=metricsSlowFrames=0;
            metricsFrameSumMs=metricsRenderSumMs=metricsMaximumFrameMs=0.0;
        }
    }
#ifdef DESKNX_SYSTEM_APPLET
    stopHeartbeatThread();
#endif
    SDL_AtomicSet(&nextendoStop,1);
    drainLauncherSettingsSave();
    gSteamGridDbService.shutdown();
    if(nextendoThread){SDL_WaitThread(nextendoThread,nullptr);nextendoThread=nullptr;}
    // The storage worker only publishes a small snapshot, but it still owns
    // an SD read path and the shared mutex. Stop/join it before destroying
    // that mutex or unmounting SDMC.
    SDL_AtomicSet(&storageStatsStop,1);
    if(storageStatsThread){SDL_WaitThread(storageStatsThread,nullptr);storageStatsThread=nullptr;}

    // Always retire async SD writers before destroying SDL, unmounting SDMC,
    // or returning to the caller. On first run layoutLoaded can still be false
    // while a user-triggered save is already in flight; skipping this drain
    // allowed the process to exit with the only current snapshot unwritten.
    const bool asyncSavePendingAtExit =
        layoutSaveThread || activeLayoutSaveJob || pendingLayoutSaveJob;
    desknx::diag::Write("layout","shutdown_flush_begin",
        "loaded=%d default_pending=%d user_modified=%d blocked=%d async_pending=%d",
        layoutLoaded?1:0,defaultLayoutPending?1:0,layoutUserModified?1:0,
        layoutSaveBlocked?1:0,asyncSavePendingAtExit?1:0);
    drainAsyncLayoutSaver();

    const bool shouldSaveAtExit =
        desknx::persistence::shouldSaveLayoutOnShutdown(
            sdMounted,layoutSaveBlocked,!desks.empty(),
            layoutLoaded,defaultLayoutPending,layoutUserModified);
    bool finalLayoutSaveOk = true;
    if(shouldSaveAtExit) finalLayoutSaveOk=saveLayout();
    desknx::diag::Write("layout","shutdown_flush_end",
        "async_was_pending=%d save_requested=%d save_ok=%d loaded=%d",
        asyncSavePendingAtExit?1:0,shouldSaveAtExit?1:0,
        finalLayoutSaveOk?1:0,layoutLoaded?1:0);
    if(roundedCornerMaskTexture){SDL_DestroyTexture(roundedCornerMaskTexture);roundedCornerMaskTexture=nullptr;}
    desknx::diag::Write("session", "shutdown_begin", "catalogue_thread=%d",
                        catalogueThread ? 1 : 0);
    playStatsPending=false;
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
    if (setsysReady) {setsysExit();setsysServiceReady=false;}
    if(tsInternalOpen){tsSessionClose(&tsInternalSession);tsInternalOpen=false;}
    if(tsSocOpen){tsSessionClose(&tsSocSession);tsSocOpen=false;}
    if(tsServiceReady){tsExit();tsServiceReady=false;}
    if(cpuClockOpen){clkrstCloseSession(&cpuClockSession);cpuClockOpen=false;}
    if(gpuClockOpen){clkrstCloseSession(&gpuClockSession);gpuClockOpen=false;}
    if(clkrstServiceReady){clkrstExit();clkrstServiceReady=false;}
    steamGridDbDestroyPreview();
    gWorkspaceBackgroundCache.shutdown();
    gModalLayer.shutdown();
    gIconCache.shutdown();gVectorIconCache.shutdown();gFontRenderer.shutdown();
    IMG_Quit();
    if(psmReady)psmExit();
    if (playStatsMutex) SDL_DestroyMutex(playStatsMutex);
    if (nextendoMutex) SDL_DestroyMutex(nextendoMutex);
    if (storageStatsMutex) SDL_DestroyMutex(storageStatsMutex);
    if (ipcMutex) SDL_DestroyMutex(ipcMutex);
    if (storageCommitMutex) SDL_DestroyMutex(storageCommitMutex);
    if (catalogueMutex) SDL_DestroyMutex(catalogueMutex);
    gUiComponents.bind(nullptr,nullptr);
    gVisualEngine.shutdown();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    desknx::diag::Write("session", "end", "normal_exit=1");
    
    return 0;
}
