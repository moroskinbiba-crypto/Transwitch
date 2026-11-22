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
#include "FontRenderer.hpp"
#include "IconCache.hpp"
#include "../include/desknx/ArtworkRequestPolicy.hpp"
#include "../include/desknx/SensorService.hpp"
#include "../include/desknx/RuntimeMetrics.hpp"
#include "../include/desknx/ModalStack.hpp"
#include "../include/desknx/ConfirmationDialogLayout.hpp"
#include "../include/desknx/WindowComponents.hpp"
#include "../include/desknx/ControlComponents.hpp"
#include "../include/desknx/RendererStatePolicy.hpp"
#include "../include/desknx/RendererStateRecovery.hpp"
#include "../include/desknx/BitmapFont.hpp"
#include "../include/desknx/Typography.hpp"
#include "../include/desknx/MotionController.hpp"

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

// SDL renderer state is global to the renderer. Scope clip and texture
// modulation changes so newly extracted components cannot leak them into later
// drawing paths, even when control flow gains an early return.
class ScopedRenderClip {
public:
    ScopedRenderClip(SDL_Renderer* target,const SDL_Rect& clip) noexcept
        : renderer_(target) {
        if(!renderer_)return;
        previousClipEnabled_=SDL_RenderIsClipEnabled(renderer_)==SDL_TRUE;
        if(previousClipEnabled_)
            SDL_RenderGetClipRect(renderer_,&previousClip_);
        // If SDL rejects the new clip, leave the renderer untouched and do
        // not attempt to restore a state that was never changed.
        changed_=SDL_RenderSetClipRect(renderer_,&clip)==0;
    }
    ScopedRenderClip(const ScopedRenderClip&)=delete;
    ScopedRenderClip& operator=(const ScopedRenderClip&)=delete;
    ~ScopedRenderClip() {
        if(!renderer_||!changed_)return;
        if(previousClipEnabled_)
            (void)SDL_RenderSetClipRect(renderer_,&previousClip_);
        else
            (void)SDL_RenderSetClipRect(renderer_,nullptr);
    }
private:
    SDL_Renderer* renderer_{nullptr};
    SDL_Rect previousClip_{};
    bool previousClipEnabled_{false};
    bool changed_{false};
};

void diagnoseRendererState(const char* phase) {
    if(!renderer)return;
    SDL_BlendMode blendMode=SDL_BLENDMODE_NONE;
    const int blendQuery=SDL_GetRenderDrawBlendMode(renderer,&blendMode);
    const SDL_bool clipEnabled=SDL_RenderIsClipEnabled(renderer);
    const bool renderTargetActive=SDL_GetRenderTarget(renderer)!=nullptr;
    const int actualBlend=blendQuery==0?static_cast<int>(blendMode):-1;
    const auto violations=desknx::ui::RendererStateViolations({
        renderTargetActive,clipEnabled==SDL_TRUE,actualBlend
    },static_cast<int>(SDL_BLENDMODE_BLEND));

    if(violations&desknx::ui::RendererStateRenderTargetActive) {
        desknx::diag::Write("render","render_target_state_leak",
            "phase=%s target=offscreen",phase?phase:"unknown");
    }
    if(violations&desknx::ui::RendererStateClipEnabled) {
        SDL_Rect clip{};
        SDL_RenderGetClipRect(renderer,&clip);
        desknx::diag::Write("render","clip_state_leak",
            "phase=%s x=%d y=%d w=%d h=%d",
            phase?phase:"unknown",clip.x,clip.y,clip.w,clip.h);
    }
    if(violations&desknx::ui::RendererStateBlendMismatch) {
        desknx::diag::Write("render","draw_blend_state_leak",
            "phase=%s mode=%d expected=%d query_rc=%d",
            phase?phase:"unknown",actualBlend,
            static_cast<int>(SDL_BLENDMODE_BLEND),blendQuery);
    }
}

void restoreFrameRendererBaseline(const char* phase) {
    if(!renderer)return;
    const auto recovery=desknx::ui::RestoreRendererFrameBaseline(renderer);
    if(recovery.hadActiveTarget&&recovery.renderTargetResult!=0) {
        desknx::diag::Write("render","render_target_restore_failed",
            "phase=%s rc=%d",phase?phase:"unknown",recovery.renderTargetResult);
    }
    if(recovery.clipResult!=0) {
        desknx::diag::Write("render","clip_restore_failed",
            "phase=%s rc=%d",phase?phase:"unknown",recovery.clipResult);
    }
    if(recovery.blendResult!=0) {
        desknx::diag::Write("render","draw_blend_restore_failed",
            "phase=%s rc=%d",phase?phase:"unknown",recovery.blendResult);
    }
}

class ScopedTextureModulation {
public:
    ScopedTextureModulation(SDL_Texture* texture,Uint8 red,Uint8 green,
                            Uint8 blue,Uint8 alpha) noexcept
        : texture_(texture) {
        if(!texture_)return;
        (void)SDL_GetTextureColorMod(texture_,&previousRed_,&previousGreen_,&previousBlue_);
        (void)SDL_GetTextureAlphaMod(texture_,&previousAlpha_);
        (void)SDL_SetTextureColorMod(texture_,red,green,blue);
        (void)SDL_SetTextureAlphaMod(texture_,alpha);
    }
    ScopedTextureModulation(const ScopedTextureModulation&)=delete;
    ScopedTextureModulation& operator=(const ScopedTextureModulation&)=delete;
    ~ScopedTextureModulation() {
        if(!texture_)return;
        (void)SDL_SetTextureColorMod(texture_,previousRed_,previousGreen_,previousBlue_);
        (void)SDL_SetTextureAlphaMod(texture_,previousAlpha_);
    }
private:
    SDL_Texture* texture_{nullptr};
    Uint8 previousRed_{255},previousGreen_{255},previousBlue_{255},previousAlpha_{255};
};
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
desknx::runtime::SensorService gSensorService;
desknx::runtime::RollingMetrics gRuntimeMetrics;
desknx::ui::MotionController gMotionController;
desknx::ui::MotionTrack gTileFocusTransition{};
int gMotionWorkspace=-1,gMotionSelectedX=-1,gMotionSelectedY=-1;
float gTileFocusProgress=1.0f;
Uint64 gInputStartedCounter=0;
Uint64 gWorkspaceSwitchStartedCounter=0;
Uint64 gFrameRenderStartedCounter=0;
Uint64 gPerformanceFrequency=0;
Uint32 gLastPerformanceReportTick=0;
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
bool sdMounted=false, touchDragging=false;
std::string statusText="Y ADD   X EDIT   L3 TOOLS", lastLaunched="Nothing launched yet";
Uint32 statusUntil=0;
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
        } else if(key=="motion") {
            desknx::ui::MotionMode parsedMode{};
            if(desknx::ui::ParseMotionModeSetting(value.c_str(),parsedMode))
                gMotionController.setMode(parsedMode);
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
       <<"motion="<<desknx::ui::MotionModeSettingName(gMotionController.mode())<<"\n";
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
    {
        ScopedRenderClip clipScope(renderer,viewport);
        SDL_RenderCopy(renderer,wallpaperTexture,nullptr,&dst);
    }
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
    {
        ScopedRenderClip clipScope(renderer,viewport);
        SDL_RenderCopy(renderer,texture,nullptr,&dst);
    }
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

// Shared outer-window primitives. Keep all modal dimming and shell surfaces
// here so later Dialog/SidePanel components share one visual and compositing base.
void drawModalBackdrop(Uint8 alpha) {
    rect(0,0,SW,SH,Color{3,7,14,alpha});
}
void drawModalSurface(int x,int y,int w,int h,Color fill,Color edge) {
    softCard(x,y,w,h,fill,edge);
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
// Deterministic renderer-thread bitmap drawing. Kept separate from the TTF
// cache so a multiline fallback never mixes fonts when the texture budget runs out.
void drawBitmapText(const std::string& localized,int x,int y,int scale,
                    Color c,int right) {
    if(scale<=0||right<=x)return;
    color(c);
    int px=x;
    int py=y;
    for(size_t offset=0;offset<localized.size();) {
        const auto decoded=desknx::ui::DecodeUtf8Codepoint(localized,offset);
        offset+=decoded.length>0?decoded.length:1;
        const std::uint32_t codepoint=decoded.codepoint;
        if(codepoint=='\r'||codepoint=='\n') {
            if(codepoint=='\r'&&offset<localized.size()) {
                const auto next=desknx::ui::DecodeUtf8Codepoint(localized,offset);
                if(next.valid&&next.codepoint=='\n')offset+=next.length;
            }
            px=x;
            py+=8*scale;
            continue;
        }
        if(codepoint=='\t') {
            px+=24*scale;
            continue;
        }
        if(px+5*scale>right)break;
        const auto rows=codepoint<0x80
            ?glyph(static_cast<char>(codepoint))
            :desknx::ui::RussianBitmapGlyph(codepoint);
        for(int ry=0;ry<7;++ry) for(int rx=0;rx<5;++rx) if(rows[ry]&(1u<<(4-rx))) {
            SDL_Rect pixel{px+rx*scale,py+ry*scale,scale,scale};
            SDL_RenderFillRect(renderer,&pixel);
        }
        px+=6*scale;
    }
}

// Bitmap fallback remains fully readable for Russian labels when both the
// custom font and Horizon shared-font service are unavailable.
void text(const std::string& value,int x,int y,int scale,Color c,int right=1260) {
    const std::string localized=desknx::i18n::translate(value,languageRussian);
    if(gFontRenderer.draw(localized,x,y,scale,SDL_Color{c.r,c.g,c.b,c.a},right))return;
    drawBitmapText(localized,x,y,scale,c,right);
}
void textRole(const std::string& value,int x,int y,desknx::ui::TextRole role,
              Color c,int right=1260) {
    const auto style=desknx::ui::TypographyFor(role);
    text(value,x,y,style.fontScale,c,right);
}

void textBlock(const std::string& value,int x,int y,desknx::ui::TextRole role,
               Color c,int right=1260) {
    const auto style=desknx::ui::TypographyFor(role);
    const std::string localized=desknx::i18n::translate(value,languageRussian);
    if(gFontRenderer.drawWrapped(localized,x,y,style.fontScale,
        SDL_Color{c.r,c.g,c.b,c.a},right,style.maxLines,style.lineHeight))return;

    // Keep fallback output consistent for the entire block. Do not route each
    // line back through FontRenderer: a nearly exhausted texture budget could
    // otherwise render early lines with TTF and later lines with bitmap glyphs.
    const int maxWidth=std::max(0,right-x);
    if(maxWidth==0||style.maxLines<=0)return;
    const int scale=style.fontScale;
    const auto measure=[scale](const std::string& candidate) {
        return desknx::ui::MeasureBitmapTextWidth(candidate,scale);
    };
    const auto lines=desknx::ui::WrapUtf8Text(
        localized,maxWidth,style.maxLines,measure);
    const int lineHeight=style.lineHeight>0?style.lineHeight:8*scale;
    for(std::size_t index=0;index<lines.size();++index)
        drawBitmapText(lines[index],x,y+static_cast<int>(index)*lineHeight,
            scale,c,right);
}

Color controlColor(desknx::ui::ControlColor c) {
    return Color{c.r,c.g,c.b,c.a};
}

void drawButton(const desknx::ui::ButtonControl& control,
                const std::string& label,int labelX,int labelY,
                int textRight,
                desknx::ui::TextRole role=desknx::ui::TextRole::Body,
                int cornerRadius=desknx::ui::ButtonCornerRadius) {
    const auto& bounds=control.bounds;
    if(bounds.width<=0||bounds.height<=0)return;
    const auto style=desknx::ui::ResolveButtonStyle(control.variant,control.state);
    roundRect(bounds.x,bounds.y,bounds.width,bounds.height,
        cornerRadius,controlColor(style.background));
    if(style.drawBorder)
        outline(bounds.x,bounds.y,bounds.width,bounds.height,controlColor(style.border));
    if(style.drawFocusRing)
        outline(bounds.x-2,bounds.y-2,bounds.width+4,bounds.height+4,
            controlColor(style.focusRing));
    textRole(label,labelX,labelY,role,controlColor(style.foreground),
        std::min(textRight,bounds.x+bounds.width-10));
}

void drawSelectableRowBackground(const desknx::ui::ModalRect& bounds,
                                 desknx::ui::SelectableRowState state,
                                 int radius=10) {
    const auto style=desknx::ui::ResolveSelectableRowStyle(state);
    if(style.background.a==0)return;
    roundRect(bounds.x,bounds.y,bounds.width,bounds.height,radius,
        controlColor(style.background));
}


void drawBadge(const desknx::ui::ModalRect& bounds,const std::string& label,
               desknx::ui::BadgeVariant variant) {
    if(bounds.width<=0||bounds.height<=0)return;
    const auto style=desknx::ui::ResolveBadgeStyle(variant);
    roundRect(bounds.x,bounds.y,bounds.width,bounds.height,
        std::min(10,bounds.height/2),controlColor(style.background));
    outline(bounds.x,bounds.y,bounds.width,bounds.height,
        controlColor(style.border));
    textRole(label,bounds.x+8,bounds.y+std::max(1,(bounds.height-10)/2),
        desknx::ui::TextRole::Metadata,controlColor(style.foreground),
        bounds.x+bounds.width-6);
}

void drawToggle(const desknx::ui::ToggleControl& toggle) {
    const auto& bounds=toggle.bounds;
    if(bounds.width<=0||bounds.height<=0)return;
    const auto style=desknx::ui::ResolveToggleStyle(toggle.value,toggle.state);
    roundRect(bounds.x,bounds.y,bounds.width,bounds.height,
        bounds.height/2,controlColor(style.track));
    outline(bounds.x,bounds.y,bounds.width,bounds.height,
        controlColor(style.border));
    const int knobSize=std::max(4,bounds.height-6);
    const int knobX=toggle.value?bounds.x+bounds.width-knobSize-3:bounds.x+3;
    roundRect(knobX,bounds.y+3,knobSize,knobSize,knobSize/2,
        controlColor(style.knob));
    if(style.drawFocusRing)
        outline(bounds.x-2,bounds.y-2,bounds.width+4,bounds.height+4,
            controlColor(style.focusRing));
}

void drawValueField(const desknx::ui::ValueFieldControl& field,
                    const std::string& value) {
    const auto& bounds=field.bounds;
    if(bounds.width<=0||bounds.height<=0)return;
    const auto style=desknx::ui::ResolveValueFieldStyle(field.visualState);
    roundRect(bounds.x,bounds.y,bounds.width,bounds.height,
        std::min(8,bounds.height/2),controlColor(style.background));
    outline(bounds.x,bounds.y,bounds.width,bounds.height,
        controlColor(style.border));
    if(style.drawFocusRing)
        outline(bounds.x-2,bounds.y-2,bounds.width+4,bounds.height+4,
            controlColor(style.focusRing));
    textRole(value,bounds.x+8,bounds.y+std::max(1,(bounds.height-10)/2),
        desknx::ui::TextRole::Metadata,controlColor(style.foreground),
        bounds.x+bounds.width-6);
}

void drawCard(const desknx::ui::ModalRect& bounds,
              desknx::ui::CardVisualState state) {
    const auto style=desknx::ui::ResolveCardStyle(state);
    const Color fill=controlColor(style.background);
    const Color border=controlColor(style.border);
    if(style.shadow)softCard(bounds.x,bounds.y,bounds.width,bounds.height,fill,border);
    else {
        roundRect(bounds.x,bounds.y,bounds.width,bounds.height,UiCornerRadius,fill);
        if(style.border.a>0)
            outline(bounds.x,bounds.y,bounds.width,bounds.height,border);
    }
}

void drawStatusIndicator(const desknx::ui::ModalRect& bounds,
                         desknx::ui::IndicatorTone tone) {
    if(bounds.width<=0||bounds.height<=0)return;
    const auto style=desknx::ui::ResolveIndicatorStyle(tone);
    const int diameter=std::min(bounds.width,bounds.height);
    if(style.drawHalo) {
        roundRect(bounds.x-2,bounds.y-2,diameter+4,diameter+4,
            (diameter+4)/2,controlColor(style.halo));
    }
    roundRect(bounds.x,bounds.y,diameter,diameter,diameter/2,
        controlColor(style.dot));
}


void drawConfirmationDialog(
    const desknx::ui::ConfirmationDialogLayout& layout,
    const std::string& title,const std::string& subtitle,
    const std::string& description,const std::string& primaryLabel,
    const std::string& secondaryLabel,bool destructive) {
    drawModalBackdrop(194);
    drawModalSurface(layout.surface.x,layout.surface.y,
        layout.surface.width,layout.surface.height,
        Color{24,27,38,255},
        destructive?Color{255,99,112,100}:Color{255,255,255,24});
    textRole(title,layout.titleX,layout.titleY,desknx::ui::TextRole::Heading,
        WHITE,layout.surface.x+layout.surface.width-30);
    textRole(subtitle,layout.subtitleX,layout.subtitleY,desknx::ui::TextRole::Subheading,
        MUTED,layout.surface.x+layout.surface.width-30);
    textBlock(description,layout.descriptionX,layout.descriptionY,
        desknx::ui::TextRole::Caption,MUTED,
        layout.surface.x+layout.surface.width-30);
    const desknx::ui::ButtonVariant primaryVariant=destructive
        ?desknx::ui::ButtonVariant::Destructive
        :desknx::ui::ButtonVariant::Primary;
    drawButton({layout.primaryButton,primaryVariant,{}},primaryLabel,
        layout.primaryLabelX,layout.primaryLabelY,
        layout.primaryButton.x+layout.primaryButton.width);
    drawButton({layout.secondaryButton,desknx::ui::ButtonVariant::Secondary,{}},
        secondaryLabel,layout.secondaryLabelX,layout.secondaryLabelY,
        layout.secondaryButton.x+layout.secondaryButton.width);
    textRole("A CONFIRM     B CANCEL",layout.footer.x,layout.footer.y,
        desknx::ui::TextRole::Metadata,MUTED,
        layout.footer.x+layout.footer.width);
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
    desks[2].tiles.push_back(Tile{Kind::Clock,"Clock","",0,0,2,1,1});
    desks[2].tiles.push_back(Tile{Kind::Battery,"Battery","",2,0,2,1,2});
    desks[2].tiles.push_back(Tile{Kind::System,"System info","",4,0,2,1,5});
    desks[2].tiles.push_back(Tile{Kind::Recent,"Recent launches","",0,1,3,1,6});
    desks[2].tiles.push_back(Tile{Kind::Storage,"SD storage","",0,2,3,1,3});
    desks[2].tiles.push_back(Tile{Kind::Calendar,"Calendar","",3,2,3,1,4});
}
double elapsedPerformanceMs(Uint64 start, Uint64 end) {
    if(gPerformanceFrequency==0 || end<start)return 0.0;
    return static_cast<double>(end-start)*1000.0/static_cast<double>(gPerformanceFrequency);
}
void markInputActivity() {
    if(gInputStartedCounter==0)gInputStartedCounter=SDL_GetPerformanceCounter();
}
void pumpSensorSnapshot() {
    static uint64_t lastGeneration=0;
    desknx::runtime::SystemSnapshot snapshot{};
    if(!gSensorService.read(snapshot)||snapshot.generation==lastGeneration)return;
    lastGeneration=snapshot.generation;
    battery=snapshot.batteryAvailable?static_cast<int>(snapshot.batteryPercent):-1;
    storageStatsAvailable=snapshot.storageAvailable;
    if(storageStatsAvailable) {
        sdFreeBytes=snapshot.sdFreeBytes;
        sdTotalBytes=snapshot.sdTotalBytes;
    } else {
        sdFreeBytes=0;
        sdTotalBytes=0;
    }
    desknx::diag::Write("sensors","snapshot",
        "generation=%llu age_ms=%llu battery_available=%d battery_fresh=%d battery_pct=%d psm_init_rc=0x%08X battery_rc=0x%08X storage_available=%d storage_fresh=%d storage_errno=%d sd_free=%llu sd_total=%llu",
        static_cast<unsigned long long>(snapshot.generation),
        static_cast<unsigned long long>(SDL_GetTicks64()-snapshot.capturedAtMs),
        snapshot.batteryAvailable?1:0,snapshot.batteryFresh?1:0,battery,
        static_cast<unsigned int>(snapshot.psmInitResult),
        static_cast<unsigned int>(snapshot.batteryResult),
        snapshot.storageAvailable?1:0,snapshot.storageFresh?1:0,snapshot.storageError,
        static_cast<unsigned long long>(snapshot.sdFreeBytes),
        static_cast<unsigned long long>(snapshot.sdTotalBytes));
}
void reportRuntimeMetrics() {
    const Uint32 now=SDL_GetTicks();
    if(gLastPerformanceReportTick!=0&&now-gLastPerformanceReportTick<10000)return;
    gLastPerformanceReportTick=now;
    const auto frame=gRuntimeMetrics.summarize(desknx::runtime::Metric::FrameTime);
    const auto render=gRuntimeMetrics.summarize(desknx::runtime::Metric::RenderTime);
    const auto present=gRuntimeMetrics.summarize(desknx::runtime::Metric::PresentWait);
    const auto input=gRuntimeMetrics.summarize(desknx::runtime::Metric::InputLatency);
    const auto workspace=gRuntimeMetrics.summarize(desknx::runtime::Metric::WorkspaceSwitch);
    desknx::diag::Write("performance","rolling_window",
        "frame_avg_ms=%.3f frame_p95_ms=%.3f frame_max_ms=%.3f frame_n=%u render_avg_ms=%.3f render_p95_ms=%.3f render_max_ms=%.3f render_n=%u present_avg_ms=%.3f present_p95_ms=%.3f present_max_ms=%.3f present_n=%u input_avg_ms=%.3f input_p95_ms=%.3f input_max_ms=%.3f input_n=%u workspace_avg_ms=%.3f workspace_p95_ms=%.3f workspace_max_ms=%.3f workspace_n=%u",
        frame.averageMs,frame.p95Ms,frame.maximumMs,static_cast<unsigned int>(frame.samples),
        render.averageMs,render.p95Ms,render.maximumMs,static_cast<unsigned int>(render.samples),
        present.averageMs,present.p95Ms,present.maximumMs,static_cast<unsigned int>(present.samples),
        input.averageMs,input.p95Ms,input.maximumMs,static_cast<unsigned int>(input.samples),
        workspace.averageMs,workspace.p95Ms,workspace.maximumMs,static_cast<unsigned int>(workspace.samples));
    const auto fontStats=gFontRenderer.stats();
    const double textBuildMs=gPerformanceFrequency
        ?1000.0*static_cast<double>(fontStats.textureBuildTicks)/
            static_cast<double>(gPerformanceFrequency):0.0;
    desknx::diag::Write("performance","font_cache",
        "misses_total=%llu deferred_total=%llu build_attempts=%llu build_ms_total=%.3f max_builds_frame=%u resident=%u budget=%u",
        static_cast<unsigned long long>(fontStats.cacheMisses),
        static_cast<unsigned long long>(fontStats.deferredMisses),
        static_cast<unsigned long long>(fontStats.textureBuildAttempts),
        textBuildMs,fontStats.maxBuildAttemptsPerFrame,
        fontStats.residentTextures,
        static_cast<unsigned int>(desknx::ui::MaxTextTextureBuildsPerFrame));
}
std::string initials(const std::string& name) {
    std::string out;
    bool begin=true;
    std::size_t offset=0;
    int count=0;
    while(offset<name.size()) {
        const auto decoded=desknx::ui::DecodeUtf8Codepoint(name,offset);
        offset+=decoded.length>0?decoded.length:1;
        const std::uint32_t codepoint=decoded.codepoint;
        if(codepoint==' '||codepoint=='-'||codepoint=='_'||codepoint=='.'||
           codepoint==0x09||codepoint==0x0D||codepoint==0x0A) {
            begin=true;
            continue;
        }
        if(begin) {
            desknx::ui::AppendUtf8Codepoint(out,
                desknx::ui::UppercaseUnicodeInitial(codepoint));
            begin=false;
            if(++count==2)break;
        }
    }
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
    if(SDL_Texture* texture=gIconCache.get(t,desknx::artwork::LibraryIconPriority)) {
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
    ScopedTextureModulation modulation(mask,fill.r,fill.g,fill.b,fill.a);
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
}

void drawTileCover(const Tile& tile,const Workspace& workspace,
                   const SDL_Rect& bounds,Color cornerFill,bool focused) {
    const Color fallback=workspace.accentColor>=0
        ?COLORS[static_cast<size_t>(workspace.accentColor)%COLORS.size()]
        :COLORS[static_cast<size_t>(tile.color)%COLORS.size()];
    const SDL_Rect viewport{bounds.x+1,bounds.y+1,
        std::max(1,bounds.w-2),std::max(1,bounds.h-2)};
    roundRect(viewport.x,viewport.y,viewport.w,viewport.h,UiCornerRadius,fallback);

    SDL_Texture* texture=gIconCache.get(tile,
        desknx::artwork::tileRenderPriority(focused));
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
            {
                ScopedRenderClip clipScope(renderer,clip);
                SDL_RenderCopy(renderer,texture,nullptr,&target);
            }
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
    if(titleLines.size()==1) {
        text(titleLines[0],bounds.x+10,bounds.y+bounds.h-stripHeight+8,
             2,WHITE,bounds.x+bounds.w-10);
    } else {
        text(titleLines[0],bounds.x+10,bounds.y+bounds.h-stripHeight+3,
             2,WHITE,bounds.x+bounds.w-10);
        text(titleLines[1],bounds.x+10,bounds.y+bounds.h-stripHeight+22,
             2,WHITE,bounds.x+bounds.w-10);
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
    const Color accent=workspace.accentColor>=0
        ?COLORS[static_cast<size_t>(workspace.accentColor)%COLORS.size()]
        :COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    const float emphasis=selected?std::max(0.0f,std::min(1.0f,gTileFocusProgress)):1.0f;
    const Uint8 focusAlpha=static_cast<Uint8>(155.0f+100.0f*emphasis);
    const Uint8 innerFocusAlpha=static_cast<Uint8>(35.0f+55.0f*emphasis);
    const Color selectedEdge{92,164,255,focusAlpha};
    const Color fill=selected?Color{29,48,73,255}:Color{22,29,42,255};
    softCard(r.x,r.y,r.w,r.h,fill,
             selected?selectedEdge:Color{255,255,255,18});

    if(t.kind==Kind::Game||t.kind==Kind::Nro) {
        drawTileCover(t,workspace,r,fill,selected);
        if(selected) {
            outline(r.x,r.y,r.w,r.h,Color{139,190,255,focusAlpha});
            outline(r.x+1,r.y+1,r.w-2,r.h-2,
                Color{255,255,255,innerFocusAlpha});
        }
    } else {
        const Color focusAccent{BLUE.r,BLUE.g,BLUE.b,focusAlpha};
        roundRect(r.x+14,r.y+10,std::min(42,r.w-28),3,2,selected?focusAccent:accent);
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
        auto recentLine=[&](size_t index,int y,int iconSize) {
            if(index>=recentLaunches.size())return;
            const auto& entry=recentLaunches[index];
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
                if(recentLaunches.empty())
                    text(languageRussian?"ПУСТО":"EMPTY",left,r.y+50,1,WHITE,right);
                else recentLine(0,r.y+42,26);
            } else if(wide) {
                label("RECENTLY OPENED",r.y+12);
                if(recentLaunches.empty())
                    text(languageRussian?"ПОКА НИЧЕГО НЕТ":"NOTHING HERE YET",
                         left,r.y+48,2,WHITE,right);
                else {
                    recentLine(0,r.y+34,26);
                    if(t.w>=3)recentLine(1,r.y+67,26);
                }
            } else {
                label(languageRussian?"НЕДАВНИЕ ЗАПУСКИ":"RECENTLY OPENED",r.y+17);
                if(recentLaunches.empty()) {
                    text(languageRussian?"ПОКА НИЧЕГО НЕТ":"NOTHING HERE YET",
                         left,r.y+86,2,WHITE,right);
                } else {
                    const size_t count=std::min<size_t>(4,recentLaunches.size());
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
                     left,r.y+34,t.w>=3?4:3,WHITE,right-78);
                if(percent>=0)text(std::to_string(percent)+"% USED",
                    right-70,r.y+49,1,MUTED,right);
                progress(r.y+83,percent);
            } else {
                label("SD CARD STORAGE",r.y+18);
                if(storageStatsAvailable) {
                    text(freeSpaceText()+" GB FREE",left,r.y+50,5,WHITE,right);
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
    for(int xx=x;xx<x+w;xx+=12){rect(xx,y,6,2,Color{67,79,98,180});rect(xx,y+h-2,6,2,Color{67,79,98,180});}
    for(int yy=y;yy<y+h;yy+=12){rect(x,yy,2,6,Color{67,79,98,180});rect(x+w-2,yy,2,6,Color{67,79,98,180});}
}
int visibleStart() {
    if(touchDragging&&touchDragTabStart>=0)return touchDragTabStart;
    return std::max(0,std::min(deskIndex-2,static_cast<int>(desks.size())-desknx::ui::WorkspaceDockLayout::VisibleTabCount));
}
desknx::ui::ModalStack collectModalStack() {
    desknx::ui::ModalFlags flags{};
    flags.appLibrary=listOpen;
    flags.workspaceMenu=workspaceMenu;
    flags.workspaceSettings=workspaceSettingsMenu;
    flags.workspacePicker=workspacePicker;
    flags.deleteConfirmation=deletePrompt;
    flags.recent=recentOpen;
    flags.systemTools=systemMenu;
    flags.launcherSettings=appSettingsMenu;
    flags.mediaPicker=mediaPickerOpen;
    flags.power=powerMenu;
    flags.userPicker=userPickerOpen;
    return desknx::ui::ModalStack::build(flags);
}

void drawScreen() {
    const desknx::ui::ModalStack modalStack=collectModalStack();
    const Uint32 motionNow=SDL_GetTicks();
    if(deskIndex!=gMotionWorkspace||selectedX!=gMotionSelectedX||
       selectedY!=gMotionSelectedY) {
        gMotionWorkspace=deskIndex;
        gMotionSelectedX=selectedX;
        gMotionSelectedY=selectedY;
        gTileFocusTransition=gMotionController.begin(
            0.0f,1.0f,motionNow,160,desknx::ui::MotionCurve::EaseOutCubic);
    }
    gTileFocusProgress=gMotionController.sample(gTileFocusTransition,motionNow);
    gFontRenderer.beginFrame();
    gIconCache.beginFrame();
    // Report state left behind by the previous frame before establishing
    // the known full-frame baseline.
    diagnoseRendererState("frame_begin");
    restoreFrameRendererBaseline("frame_begin");
    const std::vector<Candidate>& available = listOpen ? visibleCatalogueSnapshot() : visibleCatalogueCache;
    bool catalogReadyNow=false,catalogCompleteNow=false,catalogTruncatedNow=false;
    bool catalogueNsAvailableNow=false;
    std::uint32_t catalogueNsErrorNow=0;
    size_t catalogueGameCountNow=0,catalogueNroCountNow=0;
    readCatalogueState(catalogReadyNow,catalogCompleteNow,catalogTruncatedNow,
        &catalogueNsAvailableNow,&catalogueNsErrorNow,
        &catalogueGameCountNow,&catalogueNroCountNow);
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

    // Main toolbar and profile/status panels use one shared chrome layout.
    const auto& toolbarLayout=desknx::ui::LauncherToolbarControls;
    softCard(toolbarLayout.surface.x,toolbarLayout.surface.y,
        toolbarLayout.surface.width,toolbarLayout.surface.height,
        Color{24,32,45,224},Color{255,255,255,28});
    struct HeaderTool { int icon; bool active; const char* label; };
    const HeaderTool headerTools[]={
        {2,false,"GALLERY"},{0,appSettingsMenu,"SETTINGS"},
        {1,false,"PADS"},{8,systemMenu,"TOOLS"}
    };
    for(int i=0;i<desknx::ui::LauncherToolbarLayout::ButtonCount;++i) {
        const auto& item=headerTools[i];
        const auto bounds=toolbarLayout.buttonRect(i);
        const auto variant=item.active?desknx::ui::ButtonVariant::HeaderToolbarSelected:
            desknx::ui::ButtonVariant::HeaderToolbar;
        const desknx::ui::ButtonControl button{bounds,variant,{}};
        const int labelWidth=static_cast<int>(std::string(item.label).size())*6;
        drawButton(button,item.label,
            bounds.x+std::max(5,(bounds.width-labelWidth)/2),56,
            bounds.x+bounds.width-4,desknx::ui::TextRole::Metadata,13);
        drawSystemIcon(item.icon,bounds.x+12,bounds.y+7,24,
            item.active?WHITE:Color{204,214,228,255});
    }
    const auto profile=toolbarLayout.profileSurface;
    softCard(profile.x,profile.y,profile.width,profile.height,
        Color{32,43,59,235},Color{255,255,255,24});
    drawSystemIcon(7,profile.x+16,profile.y+10,27,Color{215,228,246,255});
    text(selectedUserLabel,profile.x+56,profile.y+17,2,WHITE,profile.x+290);
    text("CHANGE",profile.x+310,profile.y+19,1,BLUE,profile.x+380);
    const auto nextendo=toolbarLayout.nextendoSurface;
    roundRect(nextendo.x,nextendo.y,nextendo.width,nextendo.height,18,Color{32,43,59,235});
    text(nextendoTotalLabel(),nextendo.x+12,nextendo.y+19,1,GREEN,nextendo.x+nextendo.width-9);
    const auto clockBattery=toolbarLayout.clockBatterySurface;
    roundRect(clockBattery.x,clockBattery.y,clockBattery.width,clockBattery.height,
        18,Color{32,43,59,235});
    text(clockText(),clockBattery.x+18,clockBattery.y+13,3,WHITE,clockBattery.x+114);
    rect(clockBattery.x+126,clockBattery.y+10,1,26,Color{255,255,255,30});
    text(battery>=0?std::to_string(battery)+"%":"BAT --",clockBattery.x+136,
        clockBattery.y+19,1,battery>=20?GREEN:RED,clockBattery.x+176);

    // Single desktop surface under the tiles and widgets.
    softCard(56,96,1168,454,Color{25,35,49,186},Color{255,255,255,30});
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(d.tiles.empty()) {
        softCard(360,278,560,154,Color{20,29,43,245},Color{255,255,255,24});
        roundRect(390,302,46,46,14,Color{38,76,119,255});
        drawSystemIcon(4,402,314,22,WHITE);
        text("THIS DESKTOP IS EMPTY",454,302,2,WHITE,880);
        text("Add games, homebrew and widgets to get started.",454,337,1,MUTED,880);
        roundRect(454,370,176,38,12,Color{43,111,190,255});
        text("Y  ADD APPS",493,383,1,WHITE,620);
        text("+  CREATE ANOTHER DESKTOP",650,384,1,MUTED,890);
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
    const auto& footer=desknx::ui::WorkspaceFooterControls;
    softCard(footer.surface.x,footer.surface.y,footer.surface.width,
        footer.surface.height,Color{18,26,39,238},Color{255,255,255,16});
    const bool show=statusUntil==0||SDL_GetTicks()<statusUntil;
    roundRect(footer.statusIndicator.x,footer.statusIndicator.y,
        footer.statusIndicator.width,footer.statusIndicator.height,4,
        show?BLUE:GREEN);
    textRole(show?statusText:(editing?"ARRANGE MODE  /  EMPTY CELLS STAY EMPTY":"READY"),
        footer.statusLabel.x,footer.statusLabel.y,
        desknx::ui::TextRole::Metadata,show?WHITE:MUTED,
        footer.statusLabel.x+footer.statusLabel.width);
    struct Hint { const char* key; const char* action; };
    const Hint normalHints[]={{"A","OPEN"},{"Y","ADD"},{"X","EDIT"},{"L/R","SPACES"}};
    const Hint editHints[]={{"A","MOVE / ADD"},{"D-PAD","POSITION"},{"ZL/ZR","SIZE"},{"-","REMOVE"}};
    const Hint dragHints[]={{"D-PAD","MOVE"},{"L/R","CHANGE DESK"},{"A / B","DROP"},{"-","REMOVE"}};
    const int selectedTileIndex=itemAt(d,selectedX,selectedY);
    if(editing&&!dragging&&selectedTileIndex>=0) {
        const desknx::ui::ButtonControl sizeDecrease{
            footer.tileSizeDecreaseButton,desknx::ui::ButtonVariant::Secondary,{}};
        const desknx::ui::ButtonControl sizeIncrease{
            footer.tileSizeIncreaseButton,desknx::ui::ButtonVariant::Secondary,{}};
        const desknx::ui::ButtonControl removeTileButton{
            footer.removeTileButton,desknx::ui::ButtonVariant::Destructive,{}};
        drawButton(sizeDecrease,"SIZE -  /  ZL",
            sizeDecrease.bounds.x+15,sizeDecrease.bounds.y+10,
            sizeDecrease.bounds.x+sizeDecrease.bounds.width-8,
            desknx::ui::TextRole::Metadata);
        drawButton(sizeIncrease,"SIZE +  /  ZR",
            sizeIncrease.bounds.x+15,sizeIncrease.bounds.y+10,
            sizeIncrease.bounds.x+sizeIncrease.bounds.width-8,
            desknx::ui::TextRole::Metadata);
        drawButton(removeTileButton,"REMOVE TILE  /  MINUS",
            removeTileButton.bounds.x+16,removeTileButton.bounds.y+10,
            removeTileButton.bounds.x+removeTileButton.bounds.width-8,
            desknx::ui::TextRole::Metadata);
    } else {
        const Hint* hints=dragging?dragHints:(editing?editHints:normalHints);
        for(int i=0;i<desknx::ui::WorkspaceFooterLayout::HintCount;++i) {
            const auto hint=footer.hint(i);
            drawBadge(hint.keyBadge,hints[i].key,desknx::ui::BadgeVariant::Info);
            textRole(hints[i].action,hint.actionLabel.x,hint.actionLabel.y,
                desknx::ui::TextRole::Metadata,MUTED,
                hint.actionLabel.x+hint.actionLabel.width);
        }
    }
    // Workspace switcher anchors the lower edge, matching a familiar
    // console desktop rather than duplicating navigation in a second dock.
    const auto& dock=desknx::ui::WorkspaceDockControls;
    softCard(dock.surface.x,dock.surface.y,dock.surface.width,dock.surface.height,
        Color{18,26,39,242},Color{255,255,255,24});
    const int deskStart=visibleStart();
    for(int slot=0;slot<desknx::ui::WorkspaceDockLayout::VisibleTabCount&&
        deskStart+slot<static_cast<int>(desks.size());++slot) {
        const int index=deskStart+slot;
        const auto bounds=dock.tabRect(slot);
        const bool active=index==deskIndex;
        const desknx::ui::ButtonControl tab{
            bounds,
            active?desknx::ui::ButtonVariant::HeaderToolbarSelected
                  :desknx::ui::ButtonVariant::HeaderToolbar,
            {}};
        drawButton(tab,desks[static_cast<size_t>(index)].name,
            bounds.x+12,bounds.y+17,bounds.x+bounds.width-8,
            desknx::ui::TextRole::Metadata,14);
    }

    const auto addBounds=dock.addButton;
    const bool canAddWorkspace=desks.size()<static_cast<size_t>(MAX_DESKS);
    const desknx::ui::ButtonControl addWorkspace{
        addBounds,desknx::ui::ButtonVariant::Primary,
        {false,false,!canAddWorkspace}};
    drawButton(addWorkspace,"+",addBounds.x+17,addBounds.y+10,
        addBounds.x+addBounds.width-10,desknx::ui::TextRole::Heading,14);

    const auto menuBounds=dock.menuButton;
    const desknx::ui::ButtonControl openWorkspaceMenu{
        menuBounds,desknx::ui::ButtonVariant::Secondary,{}};
    drawButton(openWorkspaceMenu,"...",menuBounds.x+10,menuBounds.y+19,
        menuBounds.x+menuBounds.width-8,desknx::ui::TextRole::Subheading,14);

    const auto pageBounds=dock.pageIndicator;
    const desknx::ui::ButtonControl openWorkspacePicker{
        pageBounds,desknx::ui::ButtonVariant::Quiet,{}};
    drawButton(openWorkspacePicker,
        std::to_string(deskIndex+1)+" / "+std::to_string(desks.size()),
        pageBounds.x+16,pageBounds.y+17,pageBounds.x+pageBounds.width-10,
        desknx::ui::TextRole::Metadata,10);

    const auto editBounds=dock.editButton;
    const desknx::ui::ButtonControl toggleEditMode{
        editBounds,
        editing?desknx::ui::ButtonVariant::HeaderToolbarSelected
               :desknx::ui::ButtonVariant::HeaderToolbar,
        {}};
    drawButton(toggleEditMode,editing?"DONE":"EDIT",
        editBounds.x+52,editBounds.y+19,editBounds.x+editBounds.width-8,
        desknx::ui::TextRole::Subheading,16);
    drawSystemIcon(5,editBounds.x+12,editBounds.y+12,30,WHITE);
    if(modalStack.contains(desknx::ui::ModalLayer::AppLibrary)) {
        drawModalBackdrop(194);
        const int mx=110,my=70,mw=1060,mh=570;
        drawModalSurface(mx,my,mw,mh,Color{17,24,36,255},Color{255,255,255,25});
        textRole("APP LIBRARY",mx+28,my+17,desknx::ui::TextRole::Heading,
            WHITE,mx+mw-24);
        textRole("Installed games, homebrew and system widgets",
            mx+30,my+43,desknx::ui::TextRole::Caption,MUTED,mx+mw-24);
        const char* toolbarLabels[]={"Y  SEARCH","X  SORT","L/R  FILTER",
            "-  CLEAR","R STICK RESCAN","B  CLOSE","A  ADD"};
        const auto& toolbar=desknx::ui::AppLibraryToolbarControls;
        for(int buttonIndex=0;buttonIndex<desknx::ui::AppLibraryToolbarLayout::ButtonCount;
            ++buttonIndex) {
            const auto bounds=toolbar.buttonRect(buttonIndex);
            const desknx::ui::ButtonControl button{
                bounds,desknx::ui::ButtonVariant::Toolbar,{}};
            drawButton(button,toolbarLabels[buttonIndex],
                bounds.x+7,bounds.y+10,bounds.x+bounds.width-5,
                desknx::ui::TextRole::Metadata);
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
        const auto& libraryRows=desknx::ui::AppLibraryListRows;
        const int vis=libraryRows.itemCount;
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
            for(int row=0;row<libraryRows.itemCount;++row) {
                const int i=listOffset+row;
                if(i>=static_cast<int>(available.size()))break;
                const auto rowBounds=libraryRows.itemRect(row);
                const Candidate& candidate=available[static_cast<size_t>(i)];
                if(i==listIndex)
                    drawSelectableRowBackground(rowBounds,
                        desknx::ui::SelectableRowState::Focused,10);
                Tile iconTile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
                drawTileIcon(iconTile,rowBounds.x+11,rowBounds.y+4,27);
                text(candidate.title,rowBounds.x+51,rowBounds.y+8,2,
                     i==listIndex?WHITE:MUTED,rowBounds.x+565);
                std::string appBadge=typeName(candidate.kind);
                if(candidate.kind==Kind::Game) {
                    if(candidate.viewFlags&NsViewFlagNeedsUpdate)appBadge="UPDATE";
                    else if(candidate.viewFlags&NsViewFlagIsGameCard)appBadge="GAME CARD";
                }
                const auto badgeVariant=(candidate.viewFlags&NsViewFlagNeedsUpdate)
                    ?desknx::ui::BadgeVariant::Warning
                    :candidate.kind==Kind::Game?desknx::ui::BadgeVariant::Info
                    :candidate.kind==Kind::Nro?desknx::ui::BadgeVariant::Success
                    :desknx::ui::BadgeVariant::Neutral;
                drawBadge({rowBounds.x+583,rowBounds.y+5,136,25},
                    appBadge,badgeVariant);
            }
        }
        const auto& detailsPanel=desknx::ui::AppLibraryDetailsPanelLayout;
        drawCard(detailsPanel.surface,desknx::ui::CardVisualState::Panel);
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
                if(!selected.playStatsQueried) {
                    text("PLAY STATS NOT LOADED",mx+800,my+332,1,MUTED,mx+mw-30);
                } else if(!selected.playStatsAvailable) {
                    text("PLAY STATS UNAVAILABLE",mx+800,my+332,1,RED,mx+mw-30);
                } else {
                    char playLabel[48]{};
                    const unsigned long long minutes=
                        static_cast<unsigned long long>(selected.playtimeSeconds/60);
                    if(minutes<60)
                        std::snprintf(playLabel,sizeof(playLabel),"PLAY TIME  %llu MIN",minutes);
                    else
                        std::snprintf(playLabel,sizeof(playLabel),"PLAY TIME  %lluH %02lluM",
                                      minutes/60,minutes%60);
                    text(playLabel,mx+800,my+332,1,MUTED,mx+mw-30);
                    char launchLabel[48]{};
                    std::snprintf(launchLabel,sizeof(launchLabel),"LAUNCHES  %u",
                                  selected.launchCount);
                    text(launchLabel,mx+800,my+354,1,MUTED,mx+mw-30);
                    if(selected.lastPlayed!=0) {
                        const std::time_t stamp=static_cast<std::time_t>(selected.lastPlayed);
                        std::tm local{};
                        char dateLabel[48]{};
                        if(localtime_r(&stamp,&local)&&
                           std::strftime(dateLabel,sizeof(dateLabel),"LAST PLAYED  %d.%m.%Y",&local))
                            text(dateLabel,mx+800,my+376,1,MUTED,mx+mw-30);
                    } else {
                        text("NOT PLAYED YET",mx+800,my+376,1,MUTED,mx+mw-30);
                    }
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
            const desknx::ui::ButtonControl addAction{
                detailsPanel.primaryAction,desknx::ui::ButtonVariant::Primary,{}};
            drawButton(addAction,"A  ADD TO WORKSPACE",
                detailsPanel.primaryAction.x+31,
                detailsPanel.primaryAction.y+15,
                detailsPanel.primaryAction.x+detailsPanel.primaryAction.width-10,
                desknx::ui::TextRole::Metadata);
        }
        const std::string counts=std::to_string(catalogueGameCountNow)+" GAMES  /  "+
            std::to_string(catalogueNroCountNow)+" HOMEBREW  /  "+
            std::to_string(available.size())+" SHOWN";
        text(counts,mx+28,my+mh-26,1,MUTED,mx+mw-30);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::WorkspaceMenu)) {
        const auto& layout=desknx::ui::WorkspaceOptionsContextMenuLayout;
        drawModalBackdrop(190);
        drawModalSurface(layout.surface.x,layout.surface.y,
            layout.surface.width,layout.surface.height,
            Color{18,25,37,255},Color{255,255,255,26});
        textRole("WORKSPACE OPTIONS",layout.title.x,layout.title.y,
            desknx::ui::TextRole::Heading,WHITE,
            layout.title.x+layout.title.width);
        const Workspace& current=desks[static_cast<size_t>(deskIndex)];
        const bool canMoveTile=editing&&itemAt(current,selectedX,selectedY)>=0;
        const char* actions[]={"OPEN WORKSPACE","MOVE SELECTED TILE","CUSTOMIZE DESKTOP",
                               "RENAME WORKSPACE","DELETE WORKSPACE","CANCEL"};
        for(int i=0;i<layout.items.itemCount;++i) {
            const auto row=layout.items.itemRect(i);
            const bool disabled=(i==1&&!canMoveTile);
            const auto rowState=disabled
                ?desknx::ui::SelectableRowState::Disabled
                :(i==workspaceMenuIndex
                    ?desknx::ui::SelectableRowState::Focused
                    :desknx::ui::SelectableRowState::Normal);
            drawSelectableRowBackground(row,rowState,10);
            const Color labelColor=controlColor(
                desknx::ui::ResolveSelectableRowStyle(rowState).foreground);
            textRole(actions[i],row.x+22,row.y+12,desknx::ui::TextRole::Body,labelColor,row.x+row.width-15);
        }
        textRole("EACH DESKTOP KEEPS ITS OWN VISUAL SETTINGS",
            layout.footer.x,layout.footer.y,desknx::ui::TextRole::Metadata,MUTED,
            layout.footer.x+layout.footer.width);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::WorkspaceSettings)) {
        drawModalBackdrop(198);
        const auto& workspaceSettings=desknx::ui::WorkspaceSettingsDialogControls;
        drawModalSurface(workspaceSettings.surface.x,workspaceSettings.surface.y,
            workspaceSettings.surface.width,workspaceSettings.surface.height,
            Color{17,24,36,255},Color{255,255,255,28});
        const Workspace& desk=desks[static_cast<size_t>(deskIndex)];
        textRole("CUSTOMIZE DESKTOP",344,130,desknx::ui::TextRole::Heading,WHITE,930);
        textRole(desk.name+"  /  PERSONAL APPEARANCE",346,168,
            desknx::ui::TextRole::Caption,MUTED,930);
        const desknx::ui::ButtonControl workspaceBack{
            workspaceSettings.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(workspaceBack,"B  BACK",856,143,926,
            desknx::ui::TextRole::Metadata);
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
        for(int i=0;i<workspaceSettings.rows.itemCount;++i) {
            const auto row=workspaceSettings.rows.itemRect(i);
            if(i==workspaceSettingsIndex)
                drawSelectableRowBackground(row,
                    desknx::ui::SelectableRowState::Focused,10);
            textRole(labels[i],row.x+20,row.y+8,desknx::ui::TextRole::Body,
                i==workspaceSettingsIndex?WHITE:MUTED,row.x+360);
            textRole(values[i],row.x+370,row.y+8,desknx::ui::TextRole::Metadata,
                i==workspaceSettingsIndex?BLUE:MUTED,row.x+row.width-20);
        }
        textRole("COVER FILLS THE TILE; CONTAIN SHOWS THE WHOLE IMAGE",346,568,
            desknx::ui::TextRole::Caption,MUTED,930);
        textRole("UP / DOWN SELECT     A CHANGE     B CLOSE",346,606,
            desknx::ui::TextRole::Metadata,BLUE,930);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::WorkspacePicker)) {
        const auto& layout=desknx::ui::WorkspacePickerDialogLayout;
        drawModalBackdrop(194);
        drawModalSurface(layout.surface.x,layout.surface.y,
            layout.surface.width,layout.surface.height,
            Color{18,25,37,255},Color{255,255,255,26});
        textRole(workspacePickerMoveTile?"MOVE TILE TO WORKSPACE":"OPEN WORKSPACE",
             layout.title.x,layout.title.y,desknx::ui::TextRole::Heading,WHITE,
             layout.title.x+layout.title.width);
        textRole("SELECT ANY OF YOUR "+std::to_string(desks.size())+" WORKSPACES",
             layout.description.x,layout.description.y,desknx::ui::TextRole::Caption,MUTED,
             layout.description.x+layout.description.width);
        for(size_t i=0;i<desks.size()&&i<static_cast<size_t>(layout.items.itemCount);++i) {
            const auto row=layout.items.itemRect(static_cast<int>(i));
            const bool selected=static_cast<int>(i)==workspacePickerIndex;
            const bool current=static_cast<int>(i)==deskIndex;
            const auto rowState=selected
                ?desknx::ui::SelectableRowState::Focused
                :(current?desknx::ui::SelectableRowState::Current:
                    desknx::ui::SelectableRowState::Normal);
            drawSelectableRowBackground(row,rowState,8);
            const Color rowText=controlColor(
                desknx::ui::ResolveSelectableRowStyle(rowState).foreground);
            textRole(desks[i].name,row.x+22,row.y+9,desknx::ui::TextRole::Body,rowText,row.x+row.width-160);
            const std::string detail=current?"CURRENT":
                std::to_string(desks[i].tiles.size())+" ITEMS";
            textRole(detail,row.x+444,row.y+9,desknx::ui::TextRole::Metadata,
                 current?BLUE:(selected?WHITE:MUTED),row.x+row.width-18);
        }
        const desknx::ui::ButtonControl cancelButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(cancelButton,"CANCEL",layout.closeButton.x+51,
            layout.closeButton.y+13,
            layout.closeButton.x+layout.closeButton.width-45);
        text("UP / DOWN SELECT     A OPEN     B BACK",
             layout.footer.x,layout.footer.y+18,1,MUTED,
             layout.footer.x+layout.footer.width);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::DeleteConfirmation)) {
        const size_t tileCount=desks[static_cast<size_t>(deskIndex)].tiles.size();
        drawConfirmationDialog(
            desknx::ui::WorkspaceDeleteConfirmationLayout,
            "DELETE WORKSPACE?",
            desks[static_cast<size_t>(deskIndex)].name,
            std::to_string(tileCount)+
                " SHORTCUTS WILL BE REMOVED; INSTALLED APPS STAY INSTALLED",
            "DELETE","CANCEL",true);
    }
    // PowerMenu modal (ТЗ 6.1/6.2: "-" opens the power menu; PWR-03).
    if(modalStack.contains(desknx::ui::ModalLayer::Recent)) {
        const auto& recentLayout=desknx::ui::RecentWindowControls;
        drawModalBackdrop(190);
        drawModalSurface(recentLayout.surface.x,recentLayout.surface.y,
            recentLayout.surface.width,recentLayout.surface.height,
            Color{17,24,36,255},Color{255,255,255,25});
        textRole("RECENTLY OPENED",266,104,desknx::ui::TextRole::Heading,WHITE,850);
        const desknx::ui::ButtonControl backButton{
            recentLayout.backButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(backButton,"B BACK",recentLayout.backButton.x+12,
            recentLayout.backButton.y+10,
            recentLayout.backButton.x+recentLayout.backButton.width-4,
            desknx::ui::TextRole::Metadata);
        const desknx::ui::ButtonControl launchButton{
            recentLayout.launchButton,desknx::ui::ButtonVariant::Primary,{}};
        drawButton(launchButton,"LAUNCH",recentLayout.launchButton.x+12,
            recentLayout.launchButton.y+10,
            recentLayout.launchButton.x+recentLayout.launchButton.width-6,
            desknx::ui::TextRole::Metadata);
        textRole("TAP TO SELECT     A / LAUNCH BUTTON TO START",282,153,desknx::ui::TextRole::Metadata,MUTED,850);
        if(recentLaunches.empty()) {
            textRole("NO RECENT LAUNCHES YET",310,245,desknx::ui::TextRole::Subheading,WHITE,970);
            textBlock("LAUNCH A GAME OR HOMEBREW APP TO SEE IT HERE",310,280,desknx::ui::TextRole::Caption,MUTED,970);
        } else {
            recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(recentLaunches.size())-1));
            const int visible=std::min(recentLayout.entries.itemCount,
                static_cast<int>(recentLaunches.size()));
            for(int row=0;row<visible;++row) {
                const auto& entry=recentLaunches[static_cast<size_t>(row)];
                const auto rowBounds=recentLayout.entries.itemRect(row);
                if(row==recentIndex)
                    drawSelectableRowBackground(rowBounds,
                        desknx::ui::SelectableRowState::Focused,9);
                Tile iconTile{entry.kind,entry.title,entry.data,0,0,1,1,row%12};
                drawTileIcon(iconTile,rowBounds.x+12,rowBounds.y+4,27);
                textRole(entry.title,rowBounds.x+52,rowBounds.y+8,desknx::ui::TextRole::Subheading,
                    row==recentIndex?WHITE:MUTED,rowBounds.x+572);
                textRole(entry.kind==Kind::Game?"GAME":"HOMEBREW",
                    rowBounds.x+592,rowBounds.y+11,desknx::ui::TextRole::Metadata,
                    row==recentIndex?BLUE:MUTED,rowBounds.x+rowBounds.width-12);
            }
        }
        textRole("A  LAUNCH SELECTED     B  CLOSE",282,594,desknx::ui::TextRole::Metadata,BLUE,850);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::SystemTools)) {
        drawModalBackdrop(190);
        const auto& systemToolsLayout=desknx::ui::SystemToolsWindowControls;
        drawModalSurface(systemToolsLayout.surface.x,systemToolsLayout.surface.y,
            systemToolsLayout.surface.width,systemToolsLayout.surface.height,
            Color{17,24,36,255},Color{255,255,255,25});
        textRole("TOOLS",146,130,desknx::ui::TextRole::Heading,WHITE,1020);
        textBlock("Library, recent apps, widgets and system actions.",
             148,168,desknx::ui::TextRole::Caption,MUTED,1020);
        struct SystemTool { int icon; const char* title; const char* subtitle; };
        const SystemTool tools[]={
            {4,"APP LIBRARY","INSTALLED GAMES AND HOMEBREW"},
            {6,"RECENT","RECENTLY LAUNCHED APPS"},
            {3,"POWER","SLEEP, REBOOT OR SHUT DOWN"},
            {5,"EDIT MODE","MOVE AND RESIZE DESKTOP TILES"},
            {0,"WIDGETS","ADD CLOCK, STORAGE AND SYSTEM INFO"},
            {4,"RESCAN LIBRARY","REFRESH INSTALLED APPLICATIONS"}
        };
        for(size_t toolIndex=0;toolIndex<sizeof(tools)/sizeof(tools[0]);++toolIndex) {
            const auto& item=tools[toolIndex];
            const auto bounds=systemToolsLayout.cardRect(static_cast<int>(toolIndex));
            drawCard(bounds,systemMenuIndex==static_cast<int>(toolIndex)
                ?desknx::ui::CardVisualState::Focused
                :desknx::ui::CardVisualState::Normal);
            roundRect(bounds.x+14,bounds.y+14,54,54,16,
                systemMenuIndex==static_cast<int>(toolIndex)?
                Color{54,108,171,255}:Color{38,51,71,255});
            drawSystemIcon(item.icon,bounds.x+25,bounds.y+25,32,WHITE);
            textRole(item.title,bounds.x+82,bounds.y+23,desknx::ui::TextRole::Subheading,WHITE,bounds.x+bounds.width-10);
            textBlock(item.subtitle,bounds.x+16,bounds.y+91,desknx::ui::TextRole::Caption,MUTED,
                bounds.x+bounds.width-10);
        }
        const desknx::ui::ButtonControl backButton{
            systemToolsLayout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(backButton,"B BACK",systemToolsLayout.closeButton.x+16,
            systemToolsLayout.closeButton.y+11,
            systemToolsLayout.closeButton.x+systemToolsLayout.closeButton.width-10,
            desknx::ui::TextRole::Metadata);
        textRole("L3 OPEN TOOLS     A SELECT     TOUCH ANY CARD",148,568,desknx::ui::TextRole::Metadata,BLUE,1120);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::LauncherSettings)) {
        drawModalBackdrop(198);
        const auto& launcherSettings=desknx::ui::LauncherSettingsDialogControls;
        drawModalSurface(launcherSettings.surface.x,launcherSettings.surface.y,
            launcherSettings.surface.width,launcherSettings.surface.height,
            Color{17,24,36,255},Color{255,255,255,28});
        textRole("LAUNCHER SETTINGS",344,130,desknx::ui::TextRole::Heading,WHITE,930);
        textBlock("Appearance, widgets and workspace behaviour",346,168,desknx::ui::TextRole::Caption,MUTED,930);
        const desknx::ui::ButtonControl launcherBack{
            launcherSettings.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(launcherBack,"B  BACK",856,143,926,
            desknx::ui::TextRole::Metadata);
        textRole("WALLPAPER  "+(wallpaperPath.empty()?std::string("DEFAULT"):std::string("CUSTOM")),
             346,190,desknx::ui::TextRole::Metadata,wallpaperPath.empty()?MUTED:BLUE,630);
        const size_t fontSlash=fontPath.find_last_of('/');
        const std::string fontLabel=fontPath.empty()?std::string("SYSTEM"):
            (fontSlash==std::string::npos?fontPath:fontPath.substr(fontSlash+1));
        textRole("FONT  "+fontLabel,646,190,desknx::ui::TextRole::Metadata,fontPath.empty()?MUTED:BLUE,930);
        textRole("ICONS  "+(iconPackPath.empty()?std::string("DEFAULT"):std::string("CUSTOM")),
             346,202,desknx::ui::TextRole::Metadata,iconPackPath.empty()?MUTED:BLUE,930);
        const char* actions[]={"CHOOSE BACKGROUND IMAGE","CHOOSE CUSTOM FONT","CHOOSE ICON PACK",
            "CHANGE COLOR THEME","ADD WIDGET TO WORKSPACE","RESCAN GAME LIBRARY",
            "WORKSPACE MANAGEMENT","TOGGLE EDIT / ARRANGE MODE","RESET APPEARANCE",
            languageRussian?"LANGUAGE: RUSSIAN":"LANGUAGE: ENGLISH","ICON STYLE",
            languageRussian?"РЕЖИМ АНИМАЦИИ":"ANIMATION MODE"};
        const char* iconStyles[]={"Classic","Tabler outline","Material filled"};
        const char* motionModes[]={"FULL","REDUCED","LOW POWER"};
        const auto& settingsRows=launcherSettings.rows;
        for(int i=0;i<settingsRows.itemCount;++i) {
            const auto row=settingsRows.itemRect(i);
            if(i==appSettingsIndex)
                roundRect(row.x,row.y,row.width,row.height,8,Color{35,61,93,255});
            textRole(actions[i],row.x+22,row.y+7,desknx::ui::TextRole::Body,
                i==appSettingsIndex?WHITE:MUTED,row.x+390);
            if(i==3||i==10||i==11) {
                std::string value;
                int fieldOffset=410;
                int fieldWidth=172;
                if(i==3) {
                    value="THEME "+std::to_string(themeIndex+1)+"/8";
                    fieldOffset=424;
                    fieldWidth=158;
                } else if(i==10) {
                    value=iconStyles[static_cast<size_t>(iconStyle)];
                    fieldOffset=405;
                } else {
                    const int mode=static_cast<int>(gMotionController.mode());
                    const int boundedMode=std::max(0,std::min(2,mode));
                    value=motionModes[boundedMode];
                    fieldOffset=430;
                    fieldWidth=144;
                }
                const auto fieldState=i==appSettingsIndex
                    ?desknx::ui::FieldVisualState::Focused
                    :desknx::ui::FieldVisualState::Normal;
                drawValueField({{row.x+fieldOffset,row.y+2,fieldWidth,22},fieldState},value);
            }
            if(i==7) {
                drawToggle({{row.x+536,row.y+2,44,22},editing,
                    desknx::ui::ControlState{i==appSettingsIndex,false,false}});
            }
        }
        char nsInfo[80]{};
        if(!catalogReadyNow&&!catalogCompleteNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY INITIALIZING");
        else if(!catalogueNsAvailableNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"GAME SERVICE OFFLINE  0X%08X",
                          static_cast<unsigned int>(catalogueNsErrorNow));
        else std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY READY");
        const auto libraryTone=(!catalogReadyNow&&!catalogCompleteNow)
            ?desknx::ui::IndicatorTone::Loading
            :catalogueNsAvailableNow?desknx::ui::IndicatorTone::Ready
            :desknx::ui::IndicatorTone::Offline;
        drawStatusIndicator({330,576,8,8},libraryTone);
        textRole(nsInfo,346,574,desknx::ui::TextRole::Metadata,catalogueNsAvailableNow?GREEN:RED,650);
        textRole("MEDIA LIBRARY  •  WALLPAPERS / FONTS / ICON PACKS",
             346,592,desknx::ui::TextRole::Caption,MUTED,930);
        textRole("UP / DOWN SELECT     A APPLY     B CLOSE",346,610,desknx::ui::TextRole::Metadata,BLUE,930);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::MediaPicker)) {
        drawModalBackdrop(205);
        const auto& mediaLayout=desknx::ui::MediaPickerWindowControls;
        drawModalSurface(mediaLayout.surface.x,mediaLayout.surface.y,
            mediaLayout.surface.width,mediaLayout.surface.height,
            Color{17,24,36,255},Color{255,255,255,25});
        textRole(mediaPickerType==MediaPickerType::Wallpaper?"CHOOSE WALLPAPER":
             mediaPickerType==MediaPickerType::WorkspaceBackground?"CHOOSE DESKTOP BACKGROUND":
             mediaPickerType==MediaPickerType::Font?"CHOOSE FONT":"CHOOSE ICON PACK",
             264,88,desknx::ui::TextRole::Heading,WHITE,1000);
        const bool browsingAlbum=mediaPickerDirectory.compare(
            0,std::string(ALBUM_ROOT).size(),ALBUM_ROOT)==0;
        textRole(browsingAlbum?"NINTENDO ALBUM":"MEDIA LIBRARY",266,128,desknx::ui::TextRole::Caption,MUTED,1000);
        if(mediaPickerType==MediaPickerType::WorkspaceBackground) {
            const desknx::ui::ButtonControl directoryButton{
                mediaLayout.directoryButton,desknx::ui::ButtonVariant::ToolbarSelected,{}};
            drawButton(directoryButton,browsingAlbum?"MEDIA":"ALBUM",
                mediaLayout.directoryButton.x+26,mediaLayout.directoryButton.y+12,
                mediaLayout.directoryButton.x+mediaLayout.directoryButton.width-6,
                desknx::ui::TextRole::Metadata);
        }
        const desknx::ui::ButtonControl backButton{
            mediaLayout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(backButton,"B  BACK",mediaLayout.closeButton.x+14,
            mediaLayout.closeButton.y+12,
            mediaLayout.closeButton.x+mediaLayout.closeButton.width-10,
            desknx::ui::TextRole::Metadata);
        if(mediaPickerEntries.empty()) {
            textRole("NO SUPPORTED FILES FOUND",286,220,desknx::ui::TextRole::Subheading,WHITE,980);
            textBlock(mediaPickerType==MediaPickerType::IconPack
                 ?"CREATE A FOLDER HERE FOR YOUR ICON PACK"
                 :"COPY JPG / PNG OR TTF / OTF FILES INTO THIS FOLDER",
                 286,256,desknx::ui::TextRole::Caption,MUTED,1000);
        }
        const auto& mediaRows=mediaLayout.entries;
        for(int row=0;row<mediaRows.itemCount;++row) {
            const int index=mediaPickerOffset+row;
            if(index>=static_cast<int>(mediaPickerEntries.size()))break;
            const auto& entry=mediaPickerEntries[static_cast<size_t>(index)];
            const auto rowBounds=mediaRows.itemRect(row);
            if(index==mediaPickerIndex)
                drawSelectableRowBackground(rowBounds,
                    desknx::ui::SelectableRowState::Focused,8);
            const char* entryType=entry.directory?"FOLDER":
                (mediaPickerType==MediaPickerType::Font?"FONT":"IMAGE");
            textRole(entryType,rowBounds.x+18,rowBounds.y+10,desknx::ui::TextRole::Metadata,
                entry.directory?BLUE:MUTED,rowBounds.x+110);
            textRole(entry.name,rowBounds.x+128,rowBounds.y+10,desknx::ui::TextRole::Body,
                index==mediaPickerIndex?WHITE:MUTED,rowBounds.x+rowBounds.width-16);
        }
        textRole(std::to_string(mediaPickerIndex+1)+" / "+std::to_string(mediaPickerEntries.size())+
             "    A OPEN / APPLY",266,590,desknx::ui::TextRole::Metadata,MUTED,650);
        textRole(mediaPickerType==MediaPickerType::WorkspaceBackground
             ?"UP/DOWN SELECT   A OPEN/APPLY   X ALBUM   B BACK"
             :"UP / DOWN SELECT     B BACK",
             682,590,desknx::ui::TextRole::Metadata,BLUE,1018);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::Power)) {
        static const char* kPowerItems[]={"SLEEP","REBOOT","SHUTDOWN"};
        drawModalBackdrop(194);
        const auto& powerLayout=desknx::ui::PowerDialogControls;
        drawModalSurface(powerLayout.surface.x,powerLayout.surface.y,
            powerLayout.surface.width,powerLayout.surface.height,
            Color{18,25,37,255},Color{255,255,255,24});
        textRole("POWER",454,238,desknx::ui::TextRole::Heading,WHITE,820);
        const desknx::ui::ButtonControl backButton{
            powerLayout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(backButton,"B",powerLayout.closeButton.x+18,
            powerLayout.closeButton.y+9,
            powerLayout.closeButton.x+powerLayout.closeButton.width-8,
            desknx::ui::TextRole::Metadata);
        for(int i=0;i<powerLayout.actions.itemCount;++i) {
            const auto row=powerLayout.actions.itemRect(i);
            if(i==powerIndex)
                drawSelectableRowBackground(row,
                    desknx::ui::SelectableRowState::Focused,9);
            textRole(kPowerItems[i],row.x+18,row.y+7,desknx::ui::TextRole::Subheading,
                i==powerIndex?WHITE:MUTED,row.x+row.width-20);
        }
        const desknx::ui::ButtonControl executeButton{
            powerLayout.executeButton,desknx::ui::ButtonVariant::Primary,{}};
        drawButton(executeButton,"EXECUTE",
            powerLayout.executeButton.x+84,powerLayout.executeButton.y+13,
            powerLayout.executeButton.x+powerLayout.executeButton.width-14);
        textRole("SELECT AN ACTION FIRST",440,478,desknx::ui::TextRole::Caption,MUTED,820);
    }
    if(modalStack.contains(desknx::ui::ModalLayer::UserPicker)) {
        drawModalBackdrop(210);
        const auto& userLayout=desknx::ui::UserPickerDialogControls;
        drawModalSurface(userLayout.surface.x,userLayout.surface.y,
            userLayout.surface.width,userLayout.surface.height,
            Color{17,24,36,255},Color{255,255,255,32});
        textRole("SELECT PLAYER",374,142,desknx::ui::TextRole::Heading,WHITE,900);
        textBlock("The selected profile is used for games that request a player account.",
             376,180,desknx::ui::TextRole::Caption,MUTED,914);
        const desknx::ui::ButtonControl backButton{
            userLayout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        drawButton(backButton,"B BACK",userLayout.closeButton.x+12,
            userLayout.closeButton.y+11,
            userLayout.closeButton.x+userLayout.closeButton.width-8,
            desknx::ui::TextRole::Metadata);
        if(consoleUsers.empty()) {
            textRole("NO NINTENDO ACCOUNT PROFILES FOUND",382,268,desknx::ui::TextRole::Subheading,WHITE,906);
            textBlock("Create a user in System Settings, then reopen this menu.",
                 382,306,desknx::ui::TextRole::Body,MUTED,906);
        } else {
            for(int i=0;i<userLayout.users.itemCount&&
                i<static_cast<int>(consoleUsers.size());++i) {
                const auto row=userLayout.users.itemRect(i);
                const bool active=uiHasSelectedUser&&
                    sameAccountUid(consoleUsers[static_cast<size_t>(i)].uid,uiSelectedUser);
                const auto state=i==userPickerIndex
                    ?desknx::ui::SelectableRowState::Focused
                    :active?desknx::ui::SelectableRowState::Current
                    :desknx::ui::SelectableRowState::Normal;
                drawSelectableRowBackground(row,state,10);
                drawSystemIcon(7,row.x+16,row.y+8,24,active?BLUE:MUTED);
                textRole(consoleUsers[static_cast<size_t>(i)].nickname,
                    row.x+54,row.y+13,desknx::ui::TextRole::Subheading,
                    i==userPickerIndex?WHITE:MUTED,row.x+420);
                if(active)textRole("ACTIVE",row.x+446,row.y+14,
                    desknx::ui::TextRole::Metadata,GREEN,row.x+row.width-12);
            }
        }
        textRole("UP / DOWN SELECT     A USE PROFILE     B CLOSE",374,588,
            desknx::ui::TextRole::Metadata,BLUE,914);
    }
    // Diagnose state left by this frame before restoring the baseline for Present.
    diagnoseRendererState("frame_end");
    restoreFrameRendererBaseline("frame_end");
    if(gFrameRenderStartedCounter!=0) {
        const Uint64 beforePresent=SDL_GetPerformanceCounter();
        gRuntimeMetrics.record(desknx::runtime::Metric::RenderTime,
            elapsedPerformanceMs(gFrameRenderStartedCounter,beforePresent));
        const Uint64 presentStarted=SDL_GetPerformanceCounter();
        SDL_RenderPresent(renderer);
        const Uint64 presentFinished=SDL_GetPerformanceCounter();
        gRuntimeMetrics.record(desknx::runtime::Metric::PresentWait,
            elapsedPerformanceMs(presentStarted,presentFinished));
        gFrameRenderStartedCounter=0;
    } else {
        SDL_RenderPresent(renderer);
    }
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
    appSettingsMenu=false;
    switch(action) {
        case 0: openMediaPicker(MediaPickerType::Wallpaper); break;
        case 1: openMediaPicker(MediaPickerType::Font); break;
        case 2: openMediaPicker(MediaPickerType::IconPack); break;
        case 3:
            themeIndex=(themeIndex+1)%8;
            applyThemePalette();
            if(saveLauncherSettings())
                setStatus("THEME "+std::to_string(themeIndex+1)+" APPLIED");
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
            wallpaperPath.clear();fontPath.clear();iconPackPath.clear();themeIndex=1;iconStyle=0;
            gMotionController.setMode(desknx::ui::MotionMode::Full);
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
        case 11: {
            using desknx::ui::MotionMode;
            const MotionMode current=gMotionController.mode();
            const MotionMode next=current==MotionMode::Full?MotionMode::Reduced:
                current==MotionMode::Reduced?MotionMode::LowPerformance:MotionMode::Full;
            gMotionController.setMode(next);
            if(saveLauncherSettings())
                setStatus("MOTION MODE: "+
                    std::string(desknx::ui::MotionModeSettingName(next)));
            else
                setStatus("MOTION MODE CHANGED; SETTINGS NOT SAVED",3000);
            break;
        }
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
    const desknx::ui::ModalLayer activeModal=collectModalStack().top();
    // A live touch gesture owns focus until its initiating finger is released.
    // This prevents controller buttons from closing the menu or mutating the
    // workspace midway through a touch selection/drag.
    if(touchPressActive||catalogueTouchActive)return;
    if(activeModal==desknx::ui::ModalLayer::UserPicker) {
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
    if(activeModal==desknx::ui::ModalLayer::MediaPicker) {
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
    if(activeModal==desknx::ui::ModalLayer::LauncherSettings) {
        if(keys&HidNpadButton_B){appSettingsMenu=false;return;}
        const int itemCount=desknx::ui::LauncherSettingsDialogControls.rows.itemCount;
        if(keys&HidNpadButton_Up)appSettingsIndex=(appSettingsIndex+itemCount-1)%itemCount;
        if(keys&HidNpadButton_Down)appSettingsIndex=(appSettingsIndex+1)%itemCount;
        if(keys&HidNpadButton_A)activateAppSettings(appSettingsIndex);
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::WorkspaceSettings) {
        if(keys&HidNpadButton_B){workspaceSettingsMenu=false;return;}
        if(keys&HidNpadButton_Up)workspaceSettingsIndex=(workspaceSettingsIndex+6)%7;
        if(keys&HidNpadButton_Down)workspaceSettingsIndex=(workspaceSettingsIndex+1)%7;
        if(keys&HidNpadButton_A)activateWorkspaceAppearance(workspaceSettingsIndex);
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::SystemTools) {
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
            else if(action==1){recentIndex=0;recentOpen=true;}
            else if(action==2){powerMenu=true;powerIndex=0;}
            else if(action==3) {
                editing=!editing;
                if(!editing&&dragging){dragging=false;saveNow();}
                setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            } else if(action==4)activateAppSettings(4);
            else if(action==5)restartCatalogueScan();
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::WorkspacePicker) {
        const int count=static_cast<int>(desks.size());
        if(keys&HidNpadButton_B){workspacePicker=false;return;}
        if(count>0&&keys&HidNpadButton_Up)
            workspacePickerIndex=(workspacePickerIndex-1+count)%count;
        if(count>0&&keys&HidNpadButton_Down)
            workspacePickerIndex=(workspacePickerIndex+1)%count;
        if(keys&HidNpadButton_A)selectWorkspace(workspacePickerIndex);
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::WorkspaceMenu) {
        if(keys&HidNpadButton_B){workspaceMenu=false;return;}
        const int itemCount=desknx::ui::WorkspaceOptionsContextMenuLayout.items.itemCount;
        if(itemCount<=0)return;
        if(keys&HidNpadButton_Up)
            workspaceMenuIndex=(workspaceMenuIndex+itemCount-1)%itemCount;
        if(keys&HidNpadButton_Down)
            workspaceMenuIndex=(workspaceMenuIndex+1)%itemCount;
        if(keys&HidNpadButton_A) {
            const int action=workspaceMenuIndex;
            const bool canMoveTile=editing&&deskIndex>=0&&
                deskIndex<static_cast<int>(desks.size())&&
                itemAt(desks[static_cast<size_t>(deskIndex)],selectedX,selectedY)>=0;
            const auto rowState=(action==1&&!canMoveTile)
                ?desknx::ui::SelectableRowState::Disabled
                :desknx::ui::SelectableRowState::Normal;
            if(!desknx::ui::IsSelectableRowInteractive(rowState)) {
                setStatus("SELECT A TILE TO MOVE FIRST",1800);
                return;
            }
            workspaceMenu=false;
            if(action==0)openWorkspacePicker(false);
            else if(action==1)openWorkspacePicker(true);
            else if(action==2){workspaceSettingsMenu=true;workspaceSettingsIndex=0;}
            else if(action==3)renameDesk();
            else if(action==4)deleteDesk();
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::Recent) {
        if(keys&HidNpadButton_B){recentOpen=false;return;}
        if(recentLaunches.empty())return;
        recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(recentLaunches.size())-1));
        if(keys&HidNpadButton_Up)
            recentIndex=(recentIndex-1+static_cast<int>(recentLaunches.size()))%static_cast<int>(recentLaunches.size());
        if(keys&HidNpadButton_Down)
            recentIndex=(recentIndex+1)%static_cast<int>(recentLaunches.size());
        if(keys&HidNpadButton_A) {
            const HistoryEntry entry=recentLaunches[static_cast<size_t>(recentIndex)];
            recentOpen=false;
            launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::Power) {
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
    if(activeModal==desknx::ui::ModalLayer::DeleteConfirmation) {
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
    if(activeModal==desknx::ui::ModalLayer::AppLibrary) {
        const auto& available=visibleCatalogueSnapshot();
        if(keys&HidNpadButton_B){closeList();return;}
        if(keys&HidNpadButton_Y){searchCatalogue();return;}
        if(keys&HidNpadButton_X){cycleCatalogueSort();return;}
        if(keys&HidNpadButton_L){cycleCatalogueFilter(-1);return;}
        if(keys&HidNpadButton_R){cycleCatalogueFilter(1);return;}
        if(keys&HidNpadButton_Minus){catalogueQuery.clear();++catalogueViewRevision;listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);return;}
        if(keys&HidNpadButton_StickR){restartCatalogueScan();return;}
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
        static_cast<int>(desks.size())-desknx::ui::WorkspaceDockLayout::VisibleTabCount));
}
void touchInput(const SDL_Event&e) {
    const desknx::ui::ModalLayer activeModal=collectModalStack().top();
    if(activeModal==desknx::ui::ModalLayer::UserPicker) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
            const auto& layout=desknx::ui::UserPickerDialogControls;
            const desknx::ui::ButtonControl backButton{
                layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
            if(backButton.hitTest(x,y)) {
                userPickerOpen=false;
            } else {
                const int row=layout.users.itemAt(x,y);
                if(row>=0&&row<static_cast<int>(consoleUsers.size())) {
                    userPickerIndex=row;
                    selectConsoleUser(row);
                }
            }
        }
        return;
    }
    // A modal owns the complete pointer stream. Non-tap events must not leak
    // through to an underlying workspace drag or tile interaction.
    if(activeModal!=desknx::ui::ModalLayer::None&&e.type!=SDL_FINGERDOWN)return;
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
    if(activeModal==desknx::ui::ModalLayer::MediaPicker ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::MediaPickerWindowControls;
        const desknx::ui::ButtonControl backButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        if(backButton.hitTest(x,y)) {
            mediaPickerBack();
        } else if(mediaPickerType==MediaPickerType::WorkspaceBackground) {
            const desknx::ui::ButtonControl directoryButton{
                layout.directoryButton,desknx::ui::ButtonVariant::ToolbarSelected,{}};
            if(directoryButton.hitTest(x,y)) {
                mediaPickerDirectory=mediaPickerDirectory.compare(
                    0,std::string(ALBUM_ROOT).size(),ALBUM_ROOT)==0
                    ?std::string(MEDIA_ROOT):std::string(ALBUM_ROOT);
                mediaPickerIndex=mediaPickerOffset=0;
                refreshMediaPicker();
                return;
            }
            const int row=layout.entries.itemAt(x,y);
            if(row>=0)
                applyMediaPickerSelection(mediaPickerOffset+row);
        } else {
            const int row=layout.entries.itemAt(x,y);
            if(row>=0)
                applyMediaPickerSelection(mediaPickerOffset+row);
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::LauncherSettings ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::LauncherSettingsDialogControls;
        const desknx::ui::ButtonControl backButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        if(backButton.hitTest(x,y)) {
            appSettingsMenu=false;
        } else {
            const int row=layout.rows.itemAt(x,y);
            if(row>=0)activateAppSettings(row);
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::WorkspaceSettings ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::WorkspaceSettingsDialogControls;
        const desknx::ui::ButtonControl backButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        if(backButton.hitTest(x,y)) {
            workspaceSettingsMenu=false;
        } else {
            const int row=layout.rows.itemAt(x,y);
            if(row>=0) {
                workspaceSettingsIndex=row;
                activateWorkspaceAppearance(row);
            }
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::SystemTools ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::SystemToolsWindowControls;
        const desknx::ui::ButtonControl backButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        if(backButton.hitTest(x,y)){systemMenu=false;return;}
        const int action=layout.itemAt(x,y);
        if(action<0)return;
        systemMenuIndex=action;
        keyInput(HidNpadButton_A,false);
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::WorkspacePicker ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::WorkspacePickerDialogLayout;
        const desknx::ui::ButtonControl cancelButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        if(cancelButton.hitTest(x,y)) {
            workspacePicker=false;
        } else {
            const int target=layout.items.itemAt(x,y);
            if(target>=0&&target<static_cast<int>(desks.size()))
                selectWorkspace(target);
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::WorkspaceMenu ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::WorkspaceOptionsContextMenuLayout;
        const int row=layout.items.itemAt(x,y);
        if(row>=0) {
            const bool canMoveTile=editing&&deskIndex>=0&&
                deskIndex<static_cast<int>(desks.size())&&
                itemAt(desks[static_cast<size_t>(deskIndex)],selectedX,selectedY)>=0;
            const auto rowState=(row==1&&!canMoveTile)
                ?desknx::ui::SelectableRowState::Disabled
                :desknx::ui::SelectableRowState::Normal;
            if(!desknx::ui::IsSelectableRowInteractive(rowState)) {
                setStatus("SELECT A TILE TO MOVE FIRST",1800);
                return;
            }
            workspaceMenu=false;
            if(row==0)openWorkspacePicker(false);
            else if(row==1)openWorkspacePicker(true);
            else if(row==2){workspaceSettingsMenu=true;workspaceSettingsIndex=0;}
            else if(row==3)renameDesk();
            else if(row==4)deleteDesk();
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::DeleteConfirmation) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::WorkspaceDeleteConfirmationLayout;
        const desknx::ui::ButtonControl primaryButton{
            layout.primaryButton,desknx::ui::ButtonVariant::Destructive,{}};
        const desknx::ui::ButtonControl secondaryButton{
            layout.secondaryButton,desknx::ui::ButtonVariant::Secondary,{}};
        if(primaryButton.hitTest(x,y))keyInput(HidNpadButton_A,false);
        else if(secondaryButton.hitTest(x,y))keyInput(HidNpadButton_B,false);
        return;
    }
    if(e.type==SDL_FINGERDOWN &&
       activeModal==desknx::ui::ModalLayer::None) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& footer=desknx::ui::WorkspaceFooterControls;
        if(footer.surface.contains(x,y)) {
            if(editing&&!dragging&&deskIndex>=0&&
               deskIndex<static_cast<int>(desks.size())&&
               itemAt(desks[static_cast<size_t>(deskIndex)],selectedX,selectedY)>=0) {
                const desknx::ui::ButtonControl sizeDecrease{
                    footer.tileSizeDecreaseButton,desknx::ui::ButtonVariant::Secondary,{}};
                const desknx::ui::ButtonControl sizeIncrease{
                    footer.tileSizeIncreaseButton,desknx::ui::ButtonVariant::Secondary,{}};
                const desknx::ui::ButtonControl removeTileButton{
                    footer.removeTileButton,desknx::ui::ButtonVariant::Destructive,{}};
                if(sizeDecrease.hitTest(x,y))resizeTile(false);
                else if(sizeIncrease.hitTest(x,y))resizeTile(true);
                else if(removeTileButton.hitTest(x,y))removeTile();
            }
            // Status text and keyboard hints are intentionally inert. A tap
            // in their area must not fall through to the workspace grid.
            return;
        }
    }
    if(activeModal==desknx::ui::ModalLayer::Power ) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& layout=desknx::ui::PowerDialogControls;
        const desknx::ui::ButtonControl backButton{
            layout.closeButton,desknx::ui::ButtonVariant::Secondary,{}};
        const desknx::ui::ButtonControl executeButton{
            layout.executeButton,desknx::ui::ButtonVariant::Primary,{}};
        if(backButton.hitTest(x,y)) {
            keyInput(HidNpadButton_B,false);
        } else if(executeButton.hitTest(x,y)) {
            keyInput(HidNpadButton_A,false);
        } else {
            const int row=layout.actions.itemAt(x,y);
            if(row>=0)powerIndex=row;
        }
        return;
    }
    if(activeModal==desknx::ui::ModalLayer::Recent) {
        if(e.type==SDL_FINGERDOWN) {
            const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
            const auto& layout=desknx::ui::RecentWindowControls;
            const desknx::ui::ButtonControl backButton{
                layout.backButton,desknx::ui::ButtonVariant::Secondary,{}};
            const desknx::ui::ButtonControl launchButton{
                layout.launchButton,desknx::ui::ButtonVariant::Primary,{}};
            if(backButton.hitTest(x,y)) {
                recentOpen=false;
            } else if(launchButton.hitTest(x,y)) {
                if(!recentLaunches.empty()) {
                    recentIndex=std::max(0,std::min(recentIndex,
                        static_cast<int>(recentLaunches.size())-1));
                    const HistoryEntry entry=recentLaunches[static_cast<size_t>(recentIndex)];
                    recentOpen=false;
                    launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
                }
            } else {
                const int row=layout.entries.itemAt(x,y);
                if(row>=0&&row<static_cast<int>(recentLaunches.size()))
                    recentIndex=row;
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN) {
        int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(activeModal==desknx::ui::ModalLayer::AppLibrary) {
            const int toolbarAction=
                desknx::ui::AppLibraryToolbarControls.itemAt(x,y);
            if(toolbarAction>=0) {
                switch(toolbarAction) {
                    case 0: searchCatalogue(); break;
                    case 1: cycleCatalogueSort(); break;
                    case 2: cycleCatalogueFilter(1); break;
                    case 3:
                        catalogueQuery.clear();
                        ++catalogueViewRevision;
                        listIndex=0;
                        listOffset=0;
                        setStatus("SEARCH CLEARED",1800);
                        break;
                    case 4: restartCatalogueScan(); break;
                    case 5: closeList(); break;
                    case 6: addSelected(); break;
                    default: break;
                }
                return;
            }
            // Preserve the toolbar's empty gutters as inert space; they must
            // not fall through into catalogue rows or the details panel.
            if(y>=122&&y<152)return;
            const auto& details=desknx::ui::AppLibraryDetailsPanelLayout;
            const desknx::ui::ButtonControl addAction{
                details.primaryAction,desknx::ui::ButtonVariant::Primary,{}};
            if(addAction.hitTest(x,y)) {
                addSelected();
                return;
            }
            const int row=desknx::ui::AppLibraryListRows.itemAt(x,y);
            if(row>=0) {
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
        const auto& toolbarLayout=desknx::ui::LauncherToolbarControls;
        if(toolbarLayout.touchBand.contains(x,y)) {
            if(toolbarLayout.profileHitBounds.contains(x,y)) {
                openUserProfile();
                return;
            }
            const int index=toolbarLayout.buttonAt(x,y);
            if(index>=0) {
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
        const auto& dock=desknx::ui::WorkspaceDockControls;
        if(dock.interactionBounds.contains(x,y)) {
            if(dock.tabStripBounds.contains(x,y)) {
                const int target=dock.workspaceAt(x,y,tabStart(),static_cast<int>(desks.size()));
                if(target>=0) {
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
            switch(dock.actionAt(x,y)) {
                case desknx::ui::WorkspaceDockAction::AddWorkspace:
                    if(desks.size()<static_cast<size_t>(MAX_DESKS))newDesk();
                    return;
                case desknx::ui::WorkspaceDockAction::OpenMenu:
                    workspaceMenu=true;workspaceMenuIndex=0;return;
                case desknx::ui::WorkspaceDockAction::OpenPicker:openWorkspacePicker(false);return;
                case desknx::ui::WorkspaceDockAction::ToggleEdit:
                    editing=!editing;
                    if(!editing&&dragging){dragging=false;saveNow();}
                    setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
                    return;
                case desknx::ui::WorkspaceDockAction::None:default:break;
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
            const auto& dock=desknx::ui::WorkspaceDockControls;
            if(dock.dragActivationBounds.contains(x,y)) {
                const int target=dock.workspaceAt(x,y,tabStart(),static_cast<int>(desks.size()));
                if(target>=0)moveToDesk(target);
            } else {
                (void)touchSelect(x,y);
            }
        }
    } else if(e.type==SDL_FINGERMOTION&&touchDragging) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        const auto& dock=desknx::ui::WorkspaceDockControls;
        if(dock.dragActivationBounds.contains(x,y)) {
            const int target=dock.workspaceAt(x,y,tabStart(),static_cast<int>(desks.size()));
            if(target>=0)moveToDesk(target);
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
    gPerformanceFrequency=SDL_GetPerformanceFrequency();
    gFrameRenderStartedCounter=SDL_GetPerformanceCounter();
    drawScreen(); // first frame precedes application enumeration, SD recursion and font service init
#ifdef DESKNX_SYSTEM_APPLET
    const Result readyRc=sendIpc([]{return desknx::ipc::SendMenuReady();});
    if(R_FAILED(readyRc))setStatus("SYSTEM BACKEND NOT CONNECTED",5000);
    else setStatus("TRANSWITCH READY - CATALOGUE LOADING",4000);
    startHeartbeatThread();
#else
    setStatus("CATALOGUE LOADING - Y ADD  X EDIT",5000);
#endif
    if(!gSensorService.start(sdMounted))
        desknx::diag::Write("sensors","worker_start_failed","battery and SD widgets remain unavailable");
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
    int lastPrefetchedWorkspace=-1;
    while(running&&appletMainLoop()) {
        const Uint64 frameStartedCounter=SDL_GetPerformanceCounter();
        const int workspaceAtFrameStart=deskIndex;
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
        if(keys){markInputActivity();keyInput(keys,false);}
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
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT){appletRequestExitToSelf();running=false;}else {markInputActivity();touchInput(event);}}
        if(deskIndex!=workspaceAtFrameStart)gWorkspaceSwitchStartedCounter=SDL_GetPerformanceCounter();
        if(layoutLoaded&&!defaultLayoutPending&&
           deskIndex>=0&&deskIndex<static_cast<int>(desks.size())&&
           lastPrefetchedWorkspace!=deskIndex) {
            gIconCache.cancelQueuedPrefetches();
            gIconCache.prefetchWorkspace(
                desks[static_cast<size_t>(deskIndex)],selectedX,selectedY);
            lastPrefetchedWorkspace=deskIndex;
        }
        pumpSensorSnapshot();
        const Uint32 persistenceNow=SDL_GetTicks();
        if(activeWorkspaceMarkerPending &&
           static_cast<Sint32>(persistenceNow-activeWorkspaceMarkerDue)>=0)
            (void)flushActiveWorkspaceSave();
        const Uint32 renderStarted=SDL_GetTicks();
        gFrameRenderStartedCounter=SDL_GetPerformanceCounter();
        drawScreen();
        const Uint64 presentedCounter=SDL_GetPerformanceCounter();
        if(gInputStartedCounter!=0) {
            gRuntimeMetrics.record(desknx::runtime::Metric::InputLatency,
                elapsedPerformanceMs(gInputStartedCounter,presentedCounter));
            gInputStartedCounter=0;
        }
        if(gWorkspaceSwitchStartedCounter!=0) {
            gRuntimeMetrics.record(desknx::runtime::Metric::WorkspaceSwitch,
                elapsedPerformanceMs(gWorkspaceSwitchStartedCounter,presentedCounter));
            gWorkspaceSwitchStartedCounter=0;
        }
        // Present already waits for the display when VSync is active. Sleeping
        // another 16ms here capped the accelerated path at roughly 30 FPS.
        if(!rendererVsync) {
            const Uint32 elapsed=SDL_GetTicks()-renderStarted;
            if(elapsed<16)svcSleepThread(static_cast<s64>(16-elapsed)*1'000'000LL);
        }
        gRuntimeMetrics.record(desknx::runtime::Metric::FrameTime,
            elapsedPerformanceMs(frameStartedCounter,SDL_GetPerformanceCounter()));
        reportRuntimeMetrics();
    }
#ifdef DESKNX_SYSTEM_APPLET
    stopHeartbeatThread();
#endif
    gSensorService.stop();
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
    gIconCache.shutdown();gFontRenderer.shutdown();
    if(wallpaperTexture){SDL_DestroyTexture(wallpaperTexture);wallpaperTexture=nullptr;}
    IMG_Quit();
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
