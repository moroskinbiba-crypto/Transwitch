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
#include "../include/desknx/Diagnostics.hpp"
#include "catalog_view.hpp"
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

namespace {
using namespace desknx::model;
constexpr int SW = 1280, SH = 720, COLS = Columns, ROWS = Rows;
constexpr int CW = 154, CH = 112, GX = 14, GY = 14, LEFT = 141, TOP = 150;
constexpr int DeskTabX=50, DeskTabWidth=132, DeskTabStep=142, VisibleDeskTabs=5;
constexpr int MAX_ITEMS = MaxItems, MAX_DESKS = MaxDesks;
// NsApplicationView flag bits follow SLaunch's documented mapping.
constexpr u32 NsViewFlagIsGameCard = BIT(6);
constexpr u32 NsViewFlagNeedsUpdate = BIT(9);
constexpr const char* CFG_DIR = "sdmc:/switch/DeskNX";
constexpr const char* CFG_FILE = "sdmc:/switch/DeskNX/layout.tsv";
constexpr const char* CFG_TMP = "sdmc:/switch/DeskNX/layout.tmp";
constexpr const char* CFG_BACKUP_BASE = "sdmc:/switch/DeskNX/layout.corrupt";
constexpr const char* CFG_BAK_FILE = "sdmc:/switch/DeskNX/layout.bak";
size_t layoutRecoveredTiles = 0, layoutDroppedTiles = 0;
constexpr const char* HISTORY_FILE = "sdmc:/switch/DeskNX/recent.tsv";
constexpr const char* HISTORY_TMP = "sdmc:/switch/DeskNX/recent.tmp";
constexpr const char* HISTORY_BAK_FILE = "sdmc:/switch/DeskNX/recent.bak";
constexpr const char* SETTINGS_FILE = "sdmc:/switch/DeskNX/settings.ini";
constexpr const char* SETTINGS_TMP = "sdmc:/switch/DeskNX/settings.tmp";
constexpr const char* SETTINGS_BAK = "sdmc:/switch/DeskNX/settings.bak";
constexpr const char* MEDIA_ROOT = "sdmc:/switch/DeskNX/media";

struct Color { Uint8 r, g, b, a; };
Color BG{10,14,22,255};
constexpr Color WHITE{246,248,252,255};
Color MUTED{145,158,180,255}, BLUE{83,157,255,255};
Color GREEN{52,211,153,255}, RED{255,99,112,255};
Color THEME_TOP{16,27,44,255}, THEME_BOTTOM{9,17,31,255};
std::array<Color,8> COLORS{{
    {91,145,226,255},{53,169,151,255},{155,116,226,255},{222,151,83,255},
    {218,105,133,255},{79,169,206,255},{147,171,94,255},{113,130,158,255}
}};
int themeIndex=1;
std::string wallpaperPath, fontPath, iconPackPath;
SDL_Texture* wallpaperTexture=nullptr;
int wallpaperWidth=0, wallpaperHeight=0;

enum class MediaPickerType { Wallpaper, Font, IconPack };
struct MediaEntry { std::string name, path; bool directory{false}; };
bool mediaPickerOpen=false;
MediaPickerType mediaPickerType=MediaPickerType::Wallpaper;
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
SDL_Thread* catalogueThread = nullptr;
int catalogueForceRefreshToken = 1;
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
bool editing=false, dragging=false, listOpen=false, recentOpen=false, systemMenu=false, appSettingsMenu=false, workspaceMenu=false, workspacePicker=false, workspacePickerMoveTile=false, deletePrompt=false, running=true;
int recentIndex=0, systemMenuIndex=0, appSettingsIndex=0, workspaceMenuIndex=0, workspacePickerIndex=0, lastWorkspaceTabTap=-1;
int touchDragTabStart=-1;
Uint32 lastWorkspaceTabTapTick=0;
// PowerMenu modal (ТЗ 6.1: "- opens the power menu"). In system mode the
// action is delegated to the backend (PWR-03); in standalone we honour it
// locally through the applet power APIs, which are available to NROs too.
bool powerMenu=false;
s32 powerIndex=0;
bool sdMounted=false, psmReady=false, touchDragging=false;
std::string statusText="Y ADD   X EDIT   A OPEN", lastLaunched="Nothing launched yet";
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
}
void color(Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
void rect(int x,int y,int w,int h,Color c) {
    if(w<=0||h<=0) return;
    color(c);
    SDL_Rect r{x,y,w,h};
    SDL_RenderFillRect(renderer,&r);
}
void outline(int x,int y,int w,int h,Color c) {
    color(c); SDL_Rect r{x,y,w,h}; SDL_RenderDrawRect(renderer,&r);
}

void applyThemePalette() {
    switch(themeIndex) {
        case 1: // Frosted glass
            BG={63,69,78,255}; THEME_TOP={74,79,89,255}; THEME_BOTTOM={45,91,137,255};
            MUTED={215,222,231,255}; BLUE={193,147,255,255};
            GREEN={73,173,12,255}; RED={255,115,123,255};
            COLORS={{{110,151,207,255},{99,172,180,255},{163,138,206,255},{220,158,126,255},
                     {205,123,150,255},{104,162,199,255},{142,176,116,255},{135,143,159,255}}};
            break;
        case 2: // Graphite
            BG={15,17,21,255}; THEME_TOP={34,37,43,255}; THEME_BOTTOM={12,14,18,255};
            MUTED={164,169,180,255}; BLUE={187,151,255,255};
            GREEN={120,209,148,255}; RED={255,118,128,255};
            COLORS={{{142,151,166,255},{105,160,173,255},{153,132,194,255},{187,148,112,255},
                     {177,121,143,255},{115,160,180,255},{148,169,126,255},{118,126,142,255}}};
            break;
        case 3: // Violet
            BG={17,12,27,255}; THEME_TOP={48,27,68,255}; THEME_BOTTOM={12,17,39,255};
            MUTED={174,160,193,255}; BLUE={200,153,255,255};
            GREEN={127,222,181,255}; RED={255,116,147,255};
            COLORS={{{148,103,212,255},{80,155,169,255},{187,114,221,255},{209,140,111,255},
                     {209,102,153,255},{83,154,205,255},{133,175,127,255},{127,118,160,255}}};
            break;
        case 4: // Sunset
            BG={25,17,20,255}; THEME_TOP={70,37,44,255}; THEME_BOTTOM={29,36,61,255};
            MUTED={198,172,168,255}; BLUE={255,172,133,255};
            GREEN={153,210,119,255}; RED={255,105,105,255};
            COLORS={{{218,128,104,255},{104,168,145,255},{177,129,190,255},{235,162,95,255},
                     {217,110,126,255},{113,171,196,255},{170,180,101,255},{145,130,136,255}}};
            break;
        default: // Ocean
            themeIndex=0;
            BG={10,14,22,255}; THEME_TOP={16,27,44,255}; THEME_BOTTOM={9,17,31,255};
            MUTED={145,158,180,255}; BLUE={83,157,255,255};
            GREEN={52,211,153,255}; RED={255,99,112,255};
            COLORS={{{91,145,226,255},{53,169,151,255},{155,116,226,255},{222,151,83,255},
                     {218,105,133,255},{79,169,206,255},{147,171,94,255},{113,130,158,255}}};
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
    const std::string prefix=std::string(MEDIA_ROOT)+"/";
    if(path.compare(0,prefix.size(),prefix)!=0||path.size()>240||
       path.find("..")!=std::string::npos||path.find('\0')!=std::string::npos)
        return false;
    static const char* imageExts[]={".jpg",".jpeg",".png"};
    static const char* fontExts[]={".ttf",".otf",".ttc"};
    return type==MediaPickerType::Font
        ?hasExtension(path,fontExts,3):hasExtension(path,imageExts,3);
}

void loadLauncherSettings() {
    if(!sdMounted){applyThemePalette();return;}
    struct stat st{};
    if(stat(SETTINGS_FILE,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>4096) {
        applyThemePalette();
        return;
    }
    std::ifstream in(SETTINGS_FILE,std::ios::binary);
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
            if(end&&*end=='\0'&&valueLong>=0&&valueLong<=4)
                themeIndex=static_cast<int>(valueLong);
        } else if(key=="wallpaper"&&isAllowedMediaPath(value,MediaPickerType::Wallpaper)) {
            wallpaperPath=value;
        } else if(key=="font"&&isAllowedMediaPath(value,MediaPickerType::Font)) {
            fontPath=value;
        } else if(key=="iconpack") {
            struct stat packStat{};
            if(value.compare(0,(std::string(MEDIA_ROOT)+"/iconpacks/").size(),
                    std::string(MEDIA_ROOT)+"/iconpacks/")==0 &&
               value.find("..")==std::string::npos && value.size()<240 &&
               stat(value.c_str(),&packStat)==0 && S_ISDIR(packStat.st_mode))
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
       <<"iconpack="<<safeField(iconPackPath)<<"\n";
    out.flush();const bool good=out.good();out.close();
    if(!good){std::remove(SETTINGS_TMP);setStatus("SETTINGS WRITE FAILED",3000);return false;}
    const auto rc=desknx::safeio::replaceFilePreservingOnFailure(
        SETTINGS_TMP,SETTINGS_FILE,SETTINGS_BAK,true);
    if(rc!=desknx::safeio::ReplaceStatus::Success) {
        std::remove(SETTINGS_TMP);
        setStatus("SETTINGS PUBLISH FAILED",3000);
        return false;
    }
    return true;
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
    SDL_Rect src{0,0,wallpaperWidth,wallpaperHeight};
    const double sourceAspect=static_cast<double>(wallpaperWidth)/wallpaperHeight;
    const double targetAspect=static_cast<double>(SW)/SH;
    if(sourceAspect>targetAspect) {
        src.w=std::max(1,std::min(wallpaperWidth,static_cast<int>(wallpaperHeight*targetAspect)));
        src.x=(wallpaperWidth-src.w)/2;
    } else if(sourceAspect<targetAspect) {
        src.h=std::max(1,std::min(wallpaperHeight,static_cast<int>(wallpaperWidth/targetAspect)));
        src.y=(wallpaperHeight-src.h)/2;
    }
    SDL_Rect dst{0,0,SW,SH};
    SDL_RenderCopy(renderer,wallpaperTexture,&src,&dst);
    SDL_SetRenderDrawColor(renderer,3,7,14,92);
    SDL_RenderFillRect(renderer,&dst);
}

void refreshMediaPicker() {
    mediaPickerEntries.clear();
    DIR* dir=opendir(mediaPickerDirectory.c_str());
    if(!dir){setStatus("CANNOT OPEN MEDIA FOLDER",2500);return;}
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
    const size_t slash=mediaPickerDirectory.find_last_of('/');
    mediaPickerDirectory=(slash==std::string::npos)?std::string(MEDIA_ROOT):
        mediaPickerDirectory.substr(0,slash);
    mediaPickerIndex=mediaPickerOffset=0;
    refreshMediaPicker();
}
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
    const int r=std::max(0,std::min(radius,std::min(w,h)/2));
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
    roundRect(x,y+5,w,h,20,Color{0,0,0,54});
    roundRect(x,y,w,h,20,fill);
    roundRect(x+1,y+1,w-2,h-2,19,edge);
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
    if(gFontRenderer.draw(value,x,y,scale,SDL_Color{c.r,c.g,c.b,c.a},right))return;
    const std::string s=fallbackAscii(value); color(c); int px=x;
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
    SwkbdConfig config{};
    if(R_FAILED(swkbdCreate(&config,0))){setStatus("SEARCH KEYBOARD UNAVAILABLE",2500);return;}
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,"Search games and Homebrew");
    swkbdConfigSetInitialText(&config,catalogueQuery.c_str());
    char value[128]{};
    std::snprintf(value,sizeof(value),"%s",catalogueQuery.c_str());
    const Result rc=showKeyboard(&config,value,sizeof(value));
    swkbdClose(&config);
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
int catalogueThreadMain(void* context) {
    const bool forceRefresh = context != nullptr;
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
    // A manual retry must reopen NS/account after an earlier transient
    // initialization or IPC failure; otherwise every rescan repeats the
    // same dead session and can never recover without relaunching DeskNX.
    desknx::catalog::ShutdownServices();
    std::uint32_t retryServiceError=0;
    const bool retryServicesReady =
        desknx::catalog::InitializeServices(retryServiceError);
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    catalogue.clear();
    catalogueReady=false;
    catalogueScanComplete=false;
    catalogueTruncated=false;
    catalogueNsAvailable=false;
    catalogueNsError=0;
    catalogueGameCount=0;
    catalogueNroCount=0;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
    catalogueWarningShown=false;
    nroSizeCache.clear();
    tileTitleWrapCache.clear();
    listIndex=0;
    listOffset=0;
    catalogueThread=SDL_CreateThread(catalogueThreadMain,"DeskNX catalogue refresh",
                                     &catalogueForceRefreshToken);
    if(!catalogueThread) {
        setStatus("BACKGROUND RESCAN UNAVAILABLE",3000);
        publishCatalogue(desknx::catalog::Scan(nullptr,nullptr,true));
    } else if(!retryServicesReady) {
        char message[72]{};
        std::snprintf(message,sizeof(message),"NS RETRY FAILED  0X%08X",
                      static_cast<unsigned int>(retryServiceError));
        setStatus(message,5000);
    } else {
        setStatus("RESCANNING INSTALLED GAMES AND HOMEBREW",3500);
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
    const Result busyRc=sendIpc([]{return desknx::ipc::SendBusy(30);});
    const Result dialogRc=swkbdShow(config,output,outputSize);
    if(R_SUCCEEDED(busyRc))
        (void)sendIpc([]{return desknx::ipc::SendBusyEnd();});
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
    return swkbdShow(config,output,outputSize);
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

void loadHistory() {
    if(!sdMounted)return;
    struct stat historyStat{};
    constexpr size_t MaxHistoryBytes=64*1024;
    if(stat(HISTORY_FILE,&historyStat)!=0||!S_ISREG(historyStat.st_mode)||
       historyStat.st_size<=0||static_cast<uint64_t>(historyStat.st_size)>MaxHistoryBytes)return;
    std::ifstream input(HISTORY_FILE,std::ios::binary);
    std::string line;
    if(!input||!std::getline(input,line)||line.size()>32||line!="DESKNXH1")return;
    while(std::getline(input,line)&&recentLaunches.size()<10) {
        if(line.size()>2048)continue;
        auto fields=splitTabs(line);
        if(fields.size()!=3||fields[0].size()>16||fields[1].empty()||
           fields[1].size()>512||fields[2].empty()||fields[2].size()>512)continue;
        Kind kind=parseKind(fields[0]);
        if(kind!=Kind::Game&&kind!=Kind::Nro)continue;
        if(kind==Kind::Game&&!desknx::model::isTitleId(fields[2]))continue;
        if(kind==Kind::Nro&&!isSafeNroPath(fields[2]))continue;
        recentLaunches.push_back({kind,fields[1],fields[2]});
    }
    if(!recentLaunches.empty())lastLaunched=recentLaunches.front().title;
}
void saveHistory() {
    if(!sdMounted)return;
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    std::ofstream out(HISTORY_TMP,std::ios::binary|std::ios::trunc);
    if(!out){setStatus("RECENT HISTORY SAVE FAILED",3000);return;}
    out<<"DESKNXH1\n";
    for(const auto& e:recentLaunches)out<<typeCode(e.kind)<<'\t'<<safeField(e.title)<<'\t'<<safeField(e.data)<<'\n';
    out.flush();bool good=out.good();out.close();
    if(!good){std::remove(HISTORY_TMP);setStatus("RECENT HISTORY WRITE FAILED",3000);return;}
    const auto replaceStatus = desknx::safeio::replaceFilePreservingOnFailure(
        HISTORY_TMP, HISTORY_FILE, HISTORY_BAK_FILE);
    if (replaceStatus != desknx::safeio::ReplaceStatus::Success) {
        std::remove(HISTORY_TMP);
        if (replaceStatus == desknx::safeio::ReplaceStatus::RollbackFailed)
            setStatus("RECENT HISTORY RECOVERY NEEDED: KEEP recent.bak", 5000);
        else
            setStatus("RECENT HISTORY RENAME FAILED", 3000);
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
            setStatus("INVALID LAYOUT PRESERVED", 4500);
            return true;
        }
        break;
    }
    setStatus("LAYOUT INVALID; BACKUP FAILED", 4500);
    return false;
}

bool saveLayout() {
    if(!sdMounted) {
        setStatus("SAVE FAILED: SD CARD NOT MOUNTED",3500);
        return false;
    }
    if (layoutSaveBlocked) {
        setStatus("SAVE BLOCKED: PRESERVE LAYOUT BACKUP", 4500);
        return false;
    }
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    const std::string serialized=desknx::layout::serialize(desks,deskIndex);
    if(serialized.empty()){setStatus("LAYOUT INVALID; SAVE REFUSED",3500);return false;}
    std::ofstream f(CFG_TMP,std::ios::binary|std::ios::trunc);
    if(!f){setStatus("SAVE FAILED: SD CARD NOT WRITABLE",3500);return false;}
    f.write(serialized.data(),static_cast<std::streamsize>(serialized.size()));
    f.flush();const bool good=f.good();f.close();
    if(!good){std::remove(CFG_TMP);setStatus("SAVE FAILED: WRITE ERROR",3500);return false;}
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
    // Keep layout.bak as the last known-good version after successful replacement.
    layoutLoaded=true;
    setStatus("LAYOUT SAVED",1600);
    return true;
}
bool loadLayoutFrom(const char* path) {
    struct stat fileStat{};
    if (stat(path, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) ||
        fileStat.st_size <= 0 || fileStat.st_size > 1024 * 1024) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) return false;
    desknx::layout::LoadResult loaded{};
    if (!desknx::layout::parse(contents.str(), loaded)) return false;
    desks = std::move(loaded.workspaces);
    deskIndex = loaded.activeWorkspace;
    layoutNeedsMigration = loaded.needsMigration;
    layoutRecoveredTiles = loaded.recoveredTiles;
    layoutDroppedTiles = loaded.droppedTiles;
    selectedX = selectedY = 0;
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
    color(c);
    const int cx=x+size/2,cy=y+size/2;
    auto line=[&](int x1,int y1,int x2,int y2){SDL_RenderDrawLine(renderer,x1,y1,x2,y2);};
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
void drawTileCover(const Tile& tile,const SDL_Rect& bounds) {
    SDL_Texture* texture=gIconCache.get(tile);
    if(texture) {
        int sourceWidth=0,sourceHeight=0;
        if(SDL_QueryTexture(texture,nullptr,nullptr,&sourceWidth,&sourceHeight)==0 &&
           sourceWidth>0 && sourceHeight>0) {
            // Fill the entire tile while preserving the artwork's aspect ratio.
            // Crop the longer source dimension around its centre (cover, not contain).
            SDL_Rect source{0,0,sourceWidth,sourceHeight};
            const double sourceAspect=static_cast<double>(sourceWidth)/sourceHeight;
            const double targetAspect=static_cast<double>(bounds.w)/std::max(1,bounds.h);
            if(sourceAspect>targetAspect) {
                source.w=std::max(1,std::min(sourceWidth,
                    static_cast<int>(sourceHeight*targetAspect)));
                source.x=(sourceWidth-source.w)/2;
            } else if(sourceAspect<targetAspect) {
                source.h=std::max(1,std::min(sourceHeight,
                    static_cast<int>(sourceWidth/targetAspect)));
                source.y=(sourceHeight-source.h)/2;
            }
            SDL_Rect target{bounds.x+1,bounds.y+1,
                            std::max(1,bounds.w-2),std::max(1,bounds.h-2)};
            SDL_RenderCopy(renderer,texture,&source,&target);
        } else {
            roundRect(bounds.x+1,bounds.y+1,bounds.w-2,bounds.h-2,
                      12,COLORS[static_cast<size_t>(tile.color)%COLORS.size()]);
        }
    } else {
        const Color base=COLORS[static_cast<size_t>(tile.color)%COLORS.size()];
        roundRect(bounds.x+1,bounds.y+1,bounds.w-2,bounds.h-2,12,base);
        const std::string mark=initials(tile.title);
        const int scale=std::max(2,std::min(5,bounds.h/24));
        const int width=static_cast<int>(mark.size())*6*scale-scale;
        text(mark,bounds.x+(bounds.w-width)/2,
             bounds.y+(bounds.h-7*scale)/2,scale,WHITE,bounds.x+bounds.w-8);
    }

    // Large, readable labels stay on the artwork. Long names wrap to two
    // lines instead of being silently clipped at the right edge.
    const size_t maxChars=static_cast<size_t>(std::max(8,(bounds.w-20)/12));
    const auto& titleLines=cachedTileTitleLines(tile.title,maxChars);
    const int stripHeight=titleLines.size()>1?44:32;
    rect(bounds.x+1,bounds.y+bounds.h-stripHeight,
         bounds.w-2,stripHeight,Color{5,9,16,196});
    if(titleLines.size()==1) {
        text(titleLines[0],bounds.x+10,
             bounds.y+bounds.h-stripHeight+8,2,WHITE,bounds.x+bounds.w-10);
    } else {
        text(titleLines[0],bounds.x+10,bounds.y+bounds.h-stripHeight+3,
             2,WHITE,bounds.x+bounds.w-10);
        text(titleLines[1],bounds.x+10,bounds.y+bounds.h-stripHeight+22,
             2,WHITE,bounds.x+bounds.w-10);
    }
}
void drawTile(const Tile&t,bool selected) {
    SDL_Rect r=tileRect(t);
    const Color accent=COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    const Color fill=selected?Color{29,48,73,255}:Color{22,29,42,255};
    softCard(r.x,r.y,r.w,r.h,fill,
             selected?Color{92,164,255,210}:Color{255,255,255,18});
    if(t.kind==Kind::Game||t.kind==Kind::Nro) {
        drawTileCover(t,r);
        if(selected) {
            outline(r.x,r.y,r.w,r.h,Color{139,190,255,255});
            outline(r.x+1,r.y+1,r.w-2,r.h-2,Color{255,255,255,90});
        }
    } else {
        roundRect(r.x+14,r.y+10,std::min(42,r.w-28),3,2,selected?BLUE:accent);
        if(t.kind==Kind::Clock) {
        text(clockText(),r.x+18,r.y+18,5,WHITE,r.x+r.w-12);
        text(clockText(true).substr(6),r.x+20,r.y+74,2,MUTED,r.x+r.w-12);
        text("CLOCK",r.x+20,r.y+r.h-23,1,BLUE,r.x+r.w-12);
        } else if(t.kind==Kind::Battery) {
        text("BATTERY",r.x+20,r.y+14,1,MUTED,r.x+r.w-12);
        text(battery>=0?std::to_string(battery)+"%":"N/A",r.x+20,r.y+38,4,WHITE,r.x+r.w-12);
        int maxW=std::max(1,r.w-48),value=battery>=0?maxW*battery/100:4;
        roundRect(r.x+20,r.y+r.h-25,maxW,7,4,Color{54,66,84,255});
        roundRect(r.x+20,r.y+r.h-25,std::max(4,value),7,4,battery>=20?GREEN:RED);
    } else if(t.kind==Kind::System) {
        roundRect(r.x+18,r.y+18,70,70,20,Color{37,61,91,255});
        drawSystemIcon(0,r.x+34,r.y+34,38,WHITE);
        text("SYSTEM TOOLS",r.x+104,r.y+27,2,WHITE,r.x+r.w-12);
        text("SETTINGS  /  CONTROLLERS",r.x+104,r.y+54,1,MUTED,r.x+r.w-12);
        text("ALBUM  /  PROFILE  /  POWER",r.x+104,r.y+73,1,BLUE,r.x+r.w-12);
    } else if(t.kind==Kind::Unsupported) {
        roundRect(r.x+3,r.y+3,r.w-6,r.h-6,14,Color{39,46,58,255});
        text("UNSUPPORTED ITEM",r.x+14,r.y+18,2,WHITE,r.x+r.w-12);
        text(t.unknownTypeCode.empty()?"?":t.unknownTypeCode,r.x+14,r.y+52,2,MUTED,r.x+r.w-12);
    } else if(t.kind==Kind::Recent) {
        text("RECENTLY OPENED",r.x+18,r.y+12,1,MUTED,r.x+r.w-12);
        if(recentLaunches.empty())text("NOTHING HERE YET",r.x+18,r.y+44,2,WHITE,r.x+r.w-12);
        else {
            const size_t visible=std::min<size_t>(2,recentLaunches.size());
            for(size_t i=0;i<visible;++i) {
                const auto& e=recentLaunches[i];Tile iconTile{e.kind,e.title,e.data,0,0,1,1,static_cast<int>(i)};
                const int iy=r.y+37+static_cast<int>(i)*28;drawTileIcon(iconTile,r.x+18,iy,22);
                text(e.title,r.x+48,iy+5,1,WHITE,r.x+r.w-12);
            }
        }
        text("HISTORY ON THIS CONSOLE",r.x+18,r.y+r.h-18,1,BLUE,r.x+r.w-12);
    } else if(t.kind==Kind::Storage) {
        text("SD CARD STORAGE",r.x+18,r.y+12,1,MUTED,r.x+r.w-14);
        if(storageStatsAvailable) {
            const uint64_t used=sdTotalBytes>sdFreeBytes?sdTotalBytes-sdFreeBytes:0;
            const int percent=static_cast<int>(std::min<uint64_t>(
                100,(used*100)/std::max<uint64_t>(1,sdTotalBytes)));
            char sizeText[64]{};
            std::snprintf(sizeText,sizeof(sizeText),"%.1f GB FREE",
                static_cast<double>(sdFreeBytes)/(1024.0*1024.0*1024.0));
            text(sizeText,r.x+18,r.y+37,3,WHITE,r.x+r.w-14);
            roundRect(r.x+18,r.y+r.h-29,r.w-36,9,5,Color{48,62,79,255});
            roundRect(r.x+18,r.y+r.h-29,std::max(3,(r.w-36)*percent/100),9,5,GREEN);
            text(std::to_string(percent)+"% USED",r.x+18,r.y+r.h-16,1,MUTED,r.x+r.w-14);
        } else {
            text("SD STATUS UNAVAILABLE",r.x+18,r.y+45,1,RED,r.x+r.w-14);
        }
        } else if(t.kind==Kind::Calendar) {
        std::time_t now=std::time(nullptr);
        std::tm* tmv=std::localtime(&now);
        char date[48]="--";
        char day[48]="CALENDAR";
        if(tmv) {
            std::strftime(date,sizeof(date),"%d %b %Y",tmv);
            std::strftime(day,sizeof(day),"%A",tmv);
        }
        text("TODAY",r.x+18,r.y+12,1,MUTED,r.x+r.w-14);
        text(date,r.x+18,r.y+35,3,WHITE,r.x+r.w-14);
        text(day,r.x+18,r.y+r.h-24,1,BLUE,r.x+r.w-14);
        } else if(t.w==1&&t.h==1) {
        drawTileIcon(t,r.x+(r.w-60)/2,r.y+9,60);
        text(t.title,r.x+9,r.y+r.h-21,1,WHITE,r.x+r.w-8);
        if(t.kind==Kind::Nro)roundRect(r.x+8,r.y+8,6,6,3,GREEN);
    } else {
        const int is=t.h>1?86:66;
        drawTileIcon(t,r.x+17,r.y+(r.h-is)/2,is);
        text(t.title,r.x+is+31,r.y+r.h/2-13,2,WHITE,r.x+r.w-12);
        text(t.kind==Kind::Nro?"HOMEBREW":"GAME",r.x+is+31,r.y+r.h/2+13,1,MUTED,r.x+r.w-12);
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
    return std::max(0,std::min(deskIndex-2,static_cast<int>(desks.size())-VisibleDeskTabs));
}
void drawScreen() {
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
    drawWallpaper();

    // Compact system bar: common actions remain one touch away, while the
    // desktop canvas no longer has a second row of competing workspace tabs.
    softCard(56,16,1168,64,Color{24,32,45,224},Color{255,255,255,28});
    struct HeaderTool { int x,icon; bool active; };
    const HeaderTool headerTools[]={
        {72,4,listOpen},{132,0,appSettingsMenu},{192,1,false},
        {252,2,false},{312,3,powerMenu},{372,6,recentOpen}
    };
    for(const auto& item:headerTools) {
        roundRect(item.x,24,50,48,14,item.active?Color{53,94,145,255}:Color{39,48,62,230});
        drawSystemIcon(item.icon,item.x+13,36,24,item.active?WHITE:MUTED);
    }
    text("DESKNX",470,32,3,WHITE,760);
    text("PERSONAL WORKSPACE",472,58,1,MUTED,760);
    roundRect(1022,24,180,48,18,Color{32,43,59,235});
    text(clockText(),1040,37,3,WHITE,1136);
    rect(1148,34,1,26,Color{255,255,255,30});
    text(battery>=0?std::to_string(battery)+"%":"BAT --",1158,43,1,
         battery>=20?GREEN:RED,1198);

    // Single desktop surface under the tiles and widgets.
    softCard(56,96,1168,454,Color{25,35,49,186},Color{255,255,255,30});
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(d.tiles.empty()) {
        softCard(360,278,560,154,Color{20,29,43,245},Color{255,255,255,24});
        roundRect(390,302,46,46,14,Color{38,76,119,255});
        drawSystemIcon(4,402,314,22,WHITE);
        text("A FRESH WORKSPACE",454,302,2,WHITE,880);
        text("Your apps and tools will live here.",454,337,1,MUTED,880);
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
    for(const auto&t:d.tiles)drawTile(t,selectedX>=t.x&&selectedX<t.x+t.w&&selectedY>=t.y&&selectedY<t.y+t.h);
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
            active?Color{53,91,139,255}:Color{34,43,58,235});
        if(active)outline(x,641,DeskTabWidth,48,Color{144,189,244,110});
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
        editing?Color{53,100,156,255}:Color{34,46,64,255});
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
                text(candidate.title,mx+78,yy+8,2,
                     i==listIndex?WHITE:MUTED,mx+592);
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
            roundRect(mx+792,my+462,228,42,12,Color{49,119,199,255});
            text("A  ADD TO WORKSPACE",mx+823,my+477,1,WHITE,mx+mw-30);
        }
        const std::string counts=std::to_string(catalogueGameCountNow)+" GAMES  /  "+
            std::to_string(catalogueNroCountNow)+" HOMEBREW  /  "+
            std::to_string(available.size())+" SHOWN";
        text(counts,mx+28,my+mh-26,1,MUTED,mx+mw-30);
    }
    if(workspaceMenu) {
        rect(0,0,SW,SH,Color{3,7,14,190});
        softCard(340,176,600,370,Color{18,25,37,255},Color{255,255,255,26});
        text("WORKSPACE OPTIONS",390,204,3,WHITE,900);
        const Workspace& current=desks[static_cast<size_t>(deskIndex)];
        const bool canMoveTile=editing&&itemAt(current,selectedX,selectedY)>=0;
        const char* actions[]={"OPEN WORKSPACE","MOVE SELECTED TILE",
                               "RENAME WORKSPACE","DELETE WORKSPACE","CANCEL"};
        for(int i=0;i<5;++i) {
            const int yy=252+i*50;
            if(i==workspaceMenuIndex)roundRect(390,yy,500,42,10,Color{45,61,82,255});
            const Color labelColor=(i==1&&!canMoveTile)?Color{91,101,118,255}:
                                   (i==workspaceMenuIndex?WHITE:MUTED);
            text(actions[i],412,yy+13,1,labelColor,875);
        }
        text("ALL WORKSPACES ARE AVAILABLE HERE",390,512,1,MUTED,900);
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
        text("RECENTLY OPENED",266,104,3,WHITE,850);
        roundRect(838,108,78,30,9,Color{49,52,61,255});
        text("B BACK",850,118,1,WHITE,912);
        roundRect(924,108,88,30,9,Color{10,132,255,255});
        text("LAUNCH",936,118,1,WHITE,1006);
        text("TAP TO SELECT     A / LAUNCH BUTTON TO START",282,153,1,MUTED,850);
        if(recentLaunches.empty()) {
            text("NO RECENT LAUNCHES YET",310,245,2,WHITE,970);
            text("LAUNCH A GAME OR HOMEBREW APP TO SEE IT HERE",310,280,1,MUTED,970);
        } else {
            recentIndex=std::max(0,std::min(recentIndex,static_cast<int>(recentLaunches.size())-1));
            const int visible=std::min(10,static_cast<int>(recentLaunches.size()));
            for(int row=0;row<visible;++row) {
                const auto& entry=recentLaunches[static_cast<size_t>(row)];
                const int yy=190+row*39;
                if(row==recentIndex)roundRect(278,yy,724,35,9,Color{45,61,82,255});
                Tile iconTile{entry.kind,entry.title,entry.data,0,0,1,1,row%8};
                drawTileIcon(iconTile,290,yy+4,27);
                text(entry.title,330,yy+8,2,row==recentIndex?WHITE:MUTED,850);
                text(entry.kind==Kind::Game?"GAME":"HOMEBREW",870,yy+11,1,
                     row==recentIndex?BLUE:MUTED,990);
            }
        }
        text("A  LAUNCH SELECTED     B  CLOSE",282,594,1,BLUE,850);
    }
    if(systemMenu) {
        rect(0,0,SW,SH,Color{3,7,14,190});
        softCard(110,104,1060,512,Color{17,24,36,255},Color{255,255,255,25});
        text("SYSTEM TOOLS",146,130,3,WHITE,1020);
        text("Your console shortcuts, collected in one place.",
             148,168,1,MUTED,1020);
        struct SystemTool { int x,y,icon; const char* title; const char* subtitle; };
        const SystemTool tools[]={
            {154,218,0,"SETTINGS","DESKNX OPTIONS"},
            {398,218,1,"CONTROLLERS","PAIR / CHANGE CONTROLLERS"},
            {642,218,2,"ALBUM","SCREENSHOTS AND VIDEOS"},
            {886,218,7,"PROFILE","NINTENDO ACCOUNT"},
            {154,382,3,"POWER","SLEEP, REBOOT OR SHUT DOWN"},
            {398,382,5,"EDIT MODE","MOVE AND RESIZE TILES"},
            {642,382,6,"RECENT","RECENTLY LAUNCHED APPS"}
        };
        for(size_t toolIndex=0;toolIndex<sizeof(tools)/sizeof(tools[0]);++toolIndex) {
            const auto& item=tools[toolIndex];
            softCard(item.x,item.y,210,136,systemMenuIndex==static_cast<int>(toolIndex)?
                Color{34,63,98,255}:Color{23,32,46,255},
                systemMenuIndex==static_cast<int>(toolIndex)?
                Color{91,157,238,115}:Color{255,255,255,18});
            roundRect(item.x+14,item.y+14,54,54,16,
                systemMenuIndex==static_cast<int>(toolIndex)?
                Color{54,108,171,255}:Color{38,51,71,255});
            drawSystemIcon(item.icon,item.x+25,item.y+25,32,WHITE);
            text(item.title,item.x+80,item.y+23,2,WHITE,item.x+202);
            text(item.subtitle,item.x+16,item.y+91,1,MUTED,item.x+202);
        }
        roundRect(1058,136,82,34,10,Color{54,57,66,255});
        text("B BACK",1074,147,1,WHITE,1130);
        text("A SELECT     TOUCH ANY CARD",148,568,1,BLUE,1120);
    }
    if(appSettingsMenu) {
        rect(0,0,SW,SH,Color{3,7,14,198});
        softCard(300,104,680,520,Color{17,24,36,255},Color{255,255,255,28});
        text("DESKNX SETTINGS",344,130,3,WHITE,930);
        text("Appearance, widgets and workspace behaviour",346,168,1,MUTED,930);
        roundRect(842,132,92,32,10,Color{54,57,66,255});
        text("B  BACK",856,143,1,WHITE,926);
        text("WALLPAPER  "+(wallpaperPath.empty()?std::string("DEFAULT"):std::string("CUSTOM")),
             346,190,1,wallpaperPath.empty()?MUTED:BLUE,630);
        text("FONT  "+(fontPath.empty()?std::string("SYSTEM"):std::string("CUSTOM")),
             646,190,1,fontPath.empty()?MUTED:BLUE,930);
        text("ICONS  "+(iconPackPath.empty()?std::string("DEFAULT"):std::string("CUSTOM")),
             346,202,1,iconPackPath.empty()?MUTED:BLUE,930);
        const char* actions[]={"CHOOSE BACKGROUND IMAGE","CHOOSE CUSTOM FONT","CHOOSE ICON PACK",
            "CHANGE COLOR THEME","ADD WIDGET TO WORKSPACE","RESCAN GAME LIBRARY",
            "WORKSPACE MANAGEMENT","TOGGLE EDIT / ARRANGE MODE","RESET APPEARANCE"};
        for(int i=0;i<9;++i) {
            const int yy=216+i*38;
            if(i==appSettingsIndex)roundRect(340,yy,600,33,9,Color{35,61,93,255});
            text(actions[i],362,yy+9,2,i==appSettingsIndex?WHITE:MUTED,918);
            if(i==3)text("THEME "+std::to_string(themeIndex+1)+"/5",790,yy+11,1,BLUE,920);
        }
        char nsInfo[80]{};
        if(!catalogReadyNow&&!catalogCompleteNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY INITIALIZING");
        else if(!catalogueNsAvailableNow)
            std::snprintf(nsInfo,sizeof(nsInfo),"GAME SERVICE OFFLINE  0X%08X",
                          static_cast<unsigned int>(catalogueNsErrorNow));
        else std::snprintf(nsInfo,sizeof(nsInfo),"LIBRARY READY");
        text(nsInfo,346,574,1,catalogueNsAvailableNow?GREEN:RED,650);
        text("MEDIA: /switch/DeskNX/media  •  ICONS: iconpacks/<folder>/<TITLEID>.jpg/png",
             346,592,1,MUTED,930);
        text("UP / DOWN SELECT     A APPLY     B CLOSE",346,610,1,BLUE,930);
    }
    if(mediaPickerOpen) {
        rect(0,0,SW,SH,Color{3,7,14,205});
        softCard(220,58,840,604,Color{17,24,36,255},Color{255,255,255,25});
        text(mediaPickerType==MediaPickerType::Wallpaper?"CHOOSE WALLPAPER":
             mediaPickerType==MediaPickerType::Font?"CHOOSE FONT":"CHOOSE ICON PACK",
             264,88,3,WHITE,1000);
        text(mediaPickerDirectory,266,128,1,MUTED,1000);
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
        text("UP / DOWN SELECT     B BACK",730,590,1,BLUE,1018);
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
    SDL_RenderPresent(renderer);
}
bool saveNow(){return saveLayout();}
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
    SwkbdConfig kbd{};
    if(R_FAILED(swkbdCreate(&kbd,0))){setStatus("KEYBOARD UNAVAILABLE");return;}
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd,"Rename workspace");
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
    if(saveNow())setStatus(targetName);
}
void openUserProfile() {
    AccountUid uid{};
    bool haveUser=false;
#ifdef DESKNX_SYSTEM_APPLET
    if(g_backendHasUser&&accountUidIsValid(&g_backendUser)) {
        uid=g_backendUser;
        haveUser=true;
    }
#endif
    if(!haveUser) {
        const Result selectedRc=accountGetPreselectedUser(&uid);
        haveUser=R_SUCCEEDED(selectedRc)&&accountUidIsValid(&uid);
    }
    if(!haveUser) {
        AccountUid users[8]{};
        s32 actual=0;
        const Result listRc=accountListAllUsers(users,8,&actual);
        if(R_SUCCEEDED(listRc)) {
            for(s32 i=0;i<std::min<s32>(actual,8);++i) {
                if(accountUidIsValid(&users[i])) {
                    uid=users[i];
                    haveUser=true;
                    break;
                }
            }
        }
    }
    if(!haveUser) {
        setStatus("NO NINTENDO ACCOUNT PROFILE",3000);
        return;
    }
    if(R_FAILED(friendsLaShowMyProfile(uid)))
        setStatus("PROFILE APPLET UNAVAILABLE",3000);
}
void activateAppSettings(int action) {
    appSettingsMenu=false;
    switch(action) {
        case 0: openMediaPicker(MediaPickerType::Wallpaper); break;
        case 1: openMediaPicker(MediaPickerType::Font); break;
        case 2: openMediaPicker(MediaPickerType::IconPack); break;
        case 3:
            themeIndex=(themeIndex+1)%5;
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
            wallpaperPath.clear();fontPath.clear();iconPackPath.clear();themeIndex=1;
            (void)loadWallpaperTexture("");
            (void)gFontRenderer.setCustomFont("");
            gIconCache.setCustomIconPackDir("");
            applyThemePalette();
            if(saveLauncherSettings())setStatus("APPEARANCE RESET");
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
        // LNC-02/LNC-03: pass the selected user only when we actually have a
        // valid uid; zero uid means "backend decides" (kept v1-compatible).
        // HasUser forces the preselection even if NACP would skip it.
        const AccountUid uid=g_backendHasUser?g_backendUser:AccountUid{};
        const u64 flags=g_backendHasUser?desknx::ipc::LaunchFlagHasUser:0;
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
        const Result initRc = nsInitialize();
        desknx::diag::Write("launch", "standalone_ns_init", "rc=0x%08X",
                            static_cast<unsigned int>(initRc));
        if(R_FAILED(initRc)){setStatus("NS SERVICE UNAVAILABLE IN THIS CONTEXT",4000);return;}
        desknx::diag::Write("launch", "game_request_begin", "title_id=%016llX",
                            static_cast<unsigned long long>(id));
        const Result rc=appletRequestLaunchApplication(id,nullptr);
        desknx::diag::Write("launch", "game_request_end", "rc=0x%08X",
                            static_cast<unsigned int>(rc));
        nsExit();
        if(R_FAILED(rc)){char b[64]{};std::snprintf(b,sizeof(b),"GAME LAUNCH FAILED: 0X%08X",static_cast<unsigned int>(rc));setStatus(b,4000);}
        else {recordRecentLaunch(t);setStatus("LAUNCH REQUEST SENT");}
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
    else if(t.kind==Kind::System){systemMenu=true;systemMenuIndex=0;}
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
    if(mediaPickerOpen) {
        if(keys&HidNpadButton_B){mediaPickerBack();return;}
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
    if(appSettingsMenu) {
        if(keys&HidNpadButton_B){appSettingsMenu=false;return;}
        if(keys&HidNpadButton_Up)appSettingsIndex=(appSettingsIndex+8)%9;
        if(keys&HidNpadButton_Down)appSettingsIndex=(appSettingsIndex+1)%9;
        if(keys&HidNpadButton_A)activateAppSettings(appSettingsIndex);
        return;
    }
    if(systemMenu) {
        if(keys&HidNpadButton_B){systemMenu=false;return;}
        if(systemMenuIndex<0||systemMenuIndex>6)systemMenuIndex=0;
        if(keys&HidNpadButton_Left) {
            if(systemMenuIndex==0)systemMenuIndex=3;
            else if(systemMenuIndex==4)systemMenuIndex=6;
            else --systemMenuIndex;
        }
        if(keys&HidNpadButton_Right) {
            if(systemMenuIndex==3)systemMenuIndex=0;
            else if(systemMenuIndex==6)systemMenuIndex=4;
            else ++systemMenuIndex;
        }
        if(keys&HidNpadButton_Up) {
            if(systemMenuIndex>=4)systemMenuIndex-=4;
            else systemMenuIndex=(systemMenuIndex==3)?6:systemMenuIndex+4;
        }
        if(keys&HidNpadButton_Down) {
            if(systemMenuIndex<4)systemMenuIndex+=(systemMenuIndex==3)?3:4;
            else systemMenuIndex-=4;
        }
        if(keys&HidNpadButton_A) {
            const int action=systemMenuIndex;
            systemMenu=false;
            if(action==0) {
                // Retail System Settings are a scene inside qlaunch, not a
                // launchable title. Requesting qlaunch here would relaunch HOME.
                appSettingsMenu=true;
                appSettingsIndex=0;
            } else if(action==1) {
                HidLaControllerSupportArg arg{};hidLaCreateControllerSupportArg(&arg);
                HidLaControllerSupportResultInfo result{};
                if(R_FAILED(hidLaShowControllerSupport(&result,&arg)))
                    setStatus("CONTROLLER MENU UNAVAILABLE",3000);
            } else if(action==2) {
                if(R_FAILED(albumLaShowAllAlbumFiles()))setStatus("ALBUM UNAVAILABLE",3000);
            } else if(action==3) {
                openUserProfile();
            } else if(action==4) {
                powerMenu=true;powerIndex=0;
            } else if(action==5) {
                editing=!editing;
                if(!editing&&dragging){dragging=false;saveNow();}
                setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            } else if(action==6) {
                recentIndex=0;recentOpen=true;
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
            workspaceMenuIndex=(workspaceMenuIndex+4)%5;
        if(keys&HidNpadButton_Down)
            workspaceMenuIndex=(workspaceMenuIndex+1)%5;
        if(keys&HidNpadButton_A) {
            const int action=workspaceMenuIndex;
            workspaceMenu=false;
            if(action==0)openWorkspacePicker(false);
            else if(action==1)openWorkspacePicker(true);
            else if(action==2)renameDesk();
            else if(action==3)deleteDesk();
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
    if(keys&HidNpadButton_StickL)renameDesk();
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
            if(saveNow())setStatus(desks[static_cast<size_t>(deskIndex)].name);
        }
    } else if(keys&HidNpadButton_R) {
        if(dragging) moveAcrossDesk(1);
        else {
            deskIndex=(deskIndex+1)%static_cast<int>(desks.size());
            if(saveNow())setStatus(desks[static_cast<size_t>(deskIndex)].name);
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
        } else if(x>=260&&x<=1020&&y>=166&&y<574) {
            const int row=(y-166)/34;
            applyMediaPickerSelection(mediaPickerOffset+row);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && appSettingsMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=842&&x<=934&&y>=132&&y<=164) {
            appSettingsMenu=false;
        } else if(x>=340&&x<=940&&y>=216&&y<558) {
            const int row=(y-216)/38;
            if(row>=0&&row<9)activateAppSettings(row);
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN && systemMenu) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(x>=1058&&x<=1140&&y>=136&&y<=170){systemMenu=false;return;}
        bool cardHit=false;
        if(y>=218&&y<354) {
            if(x>=154&&x<364){systemMenuIndex=0;cardHit=true;}
            else if(x>=398&&x<608){systemMenuIndex=1;cardHit=true;}
            else if(x>=642&&x<852){systemMenuIndex=2;cardHit=true;}
            else if(x>=886&&x<1096){systemMenuIndex=3;cardHit=true;}
        } else if(y>=382&&y<518) {
            if(x>=154&&x<364){systemMenuIndex=4;cardHit=true;}
            else if(x>=398&&x<608){systemMenuIndex=5;cardHit=true;}
            else if(x>=642&&x<852){systemMenuIndex=6;cardHit=true;}
        }
        if(cardHit) {
            systemMenu=false;
            if(systemMenuIndex==0) {
                // System Settings are a qlaunch scene on retail consoles.
                // Open the real DeskNX settings panel instead of relaunching HOME.
                appSettingsMenu=true;
                appSettingsIndex=0;
            } else if(systemMenuIndex==1) {
                HidLaControllerSupportArg arg{};hidLaCreateControllerSupportArg(&arg);
                HidLaControllerSupportResultInfo result{};
                if(R_FAILED(hidLaShowControllerSupport(&result,&arg)))setStatus("CONTROLLER MENU UNAVAILABLE",3000);
            } else if(systemMenuIndex==2) {
                if(R_FAILED(albumLaShowAllAlbumFiles()))setStatus("ALBUM UNAVAILABLE",3000);
            } else if(systemMenuIndex==3) {
                openUserProfile();
            } else if(systemMenuIndex==4) {
                powerMenu=true;powerIndex=0;
            } else if(systemMenuIndex==5) {
                editing=!editing;
                if(!editing&&dragging){dragging=false;saveNow();}
                setStatus(editing?"EDIT MODE ENABLED":"EDIT MODE CLOSED");
            } else if(systemMenuIndex==6) {
                recentIndex=0;recentOpen=true;
            }
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
        if(x>=390&&x<=890) {
            if(y>=252&&y<294) {
                workspaceMenu=false;
                openWorkspacePicker(false);
            } else if(y>=302&&y<344) {
                workspaceMenu=false;
                openWorkspacePicker(true);
            } else if(y>=352&&y<394) {
                workspaceMenu=false;
                renameDesk();
            } else if(y>=402&&y<444) {
                workspaceMenu=false;
                deleteDesk();
            } else if(y>=452&&y<494) {
                workspaceMenu=false;
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
                const int index=(y-190)/39;
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
                addSelected();
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
        // System shortcuts in the top bar.
        if(y>=16&&y<=82) {
            if(x>=72&&x<122) {openList();return;}
            if(x>=132&&x<182) {appSettingsMenu=true;appSettingsIndex=0;return;}
            if(x>=192&&x<242) {
                HidLaControllerSupportArg arg{};hidLaCreateControllerSupportArg(&arg);
                HidLaControllerSupportResultInfo result{};
                if(R_FAILED(hidLaShowControllerSupport(&result,&arg)))
                    setStatus("CONTROLLER MENU UNAVAILABLE",3000);
                return;
            }
            if(x>=252&&x<302) {
                if(R_FAILED(albumLaShowAllAlbumFiles()))setStatus("ALBUM UNAVAILABLE",3000);
                return;
            }
            if(x>=312&&x<362) {powerMenu=true;powerIndex=0;return;}
            if(x>=372&&x<422) {recentIndex=0;recentOpen=true;return;}
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
                            if(saveNow())setStatus(desks[static_cast<size_t>(deskIndex)].name);
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
    desknx::diag::Write("session", "start", "mode=%s sd_mounted=%d sd_mount_rc=0x%08X",
        buildMode, sdMounted ? 1 : 0, static_cast<unsigned int>(sdMountRc));
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){
        desknx::diag::Write("startup", "sdl_init_failed", "error=%s", SDL_GetError());
        if(sdMounted) fsdevUnmountDevice("sdmc");
        return 1;
    }
    desknx::diag::Write("startup", "sdl_init_ok", "sdl_version=%u.%u.%u",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    window=SDL_CreateWindow("DeskNX",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SW,SH,SDL_WINDOW_FULLSCREEN_DESKTOP);
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
    playStatsMutex=SDL_CreateMutex();

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

    loadHistory();
    struct stat layoutStat{}, backupStat{};
    const bool layoutExists = stat(CFG_FILE, &layoutStat) == 0;
    const bool backupExists = stat(CFG_BAK_FILE, &backupStat) == 0;
    layoutLoaded = loadLayout();
    bool restoredFromBackup = false;
    if (!layoutLoaded && backupExists && loadLayoutFrom(CFG_BAK_FILE)) {
        // The primary may be missing as well as corrupt. Never discard the only
        // valid backup until the old primary has been preserved or is absent.
        if (layoutExists && !backupBrokenLayout())
            layoutSaveBlocked = true;
        layoutLoaded = true;
        restoredFromBackup = true;
        setStatus("LAYOUT RESTORED FROM BACKUP", 5000);
        if (!layoutSaveBlocked)
            saveLayout();
    } else if (layoutExists && !layoutLoaded) {
        if (!backupBrokenLayout())
            layoutSaveBlocked = true;
        defaultLayout();
        defaultLayoutPending=true;
    } else if (!layoutLoaded) {
        defaultLayout();
        defaultLayoutPending=true;
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
    setStatus("STARTING DESKNX...",5000);
    drawScreen(); // first frame precedes application enumeration, SD recursion and font service init
#ifdef DESKNX_SYSTEM_APPLET
    const Result readyRc=sendIpc([]{return desknx::ipc::SendMenuReady();});
    if(R_FAILED(readyRc))setStatus("SYSTEM BACKEND NOT CONNECTED",5000);
    else setStatus("DESKNX READY - CATALOGUE LOADING",4000);
    startHeartbeatThread();
#else
    setStatus("CATALOGUE LOADING - Y ADD  X EDIT",5000);
#endif
    IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);
    gIconCache.setRenderer(renderer);
    gFontRenderer.setRenderer(renderer);
    gFontRenderer.initialize(); // slow pl initialization cannot delay MenuReady
    gIconCache.setCustomIconPackDir(iconPackPath);
    if(!fontPath.empty()&&!gFontRenderer.setCustomFont(fontPath)) {
        fontPath.clear();
        setStatus("CUSTOM FONT UNAVAILABLE - USING SYSTEM FONT",3000);
        (void)saveLauncherSettings();
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
    if(!catalogueThread){
        desknx::diag::Write("catalogue", "thread_create_failed", "fallback= synchronous_scan");
        setStatus("BACKGROUND SCAN UNAVAILABLE",3000);
        publishCatalogue(desknx::catalog::Scan());
    }
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
            if(saved)setStatus("DESKNX READY - Y ADD  X EDIT",4000);
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
        pumpPlayStatsQuery();
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT){appletRequestExitToSelf();running=false;}else touchInput(event);}
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
    desknx::diag::Write("session", "shutdown_begin", "catalogue_thread=%d",
                        catalogueThread ? 1 : 0);
    playStatsPending=false;
    if(playStatsThread) {
        SDL_WaitThread(playStatsThread,nullptr);
        playStatsThread=nullptr;
    }
    if(catalogueThread){SDL_WaitThread(catalogueThread,nullptr);catalogueThread=nullptr;}
    desknx::catalog::ShutdownServices();
    if (setsysReady) setsysExit();
    gIconCache.shutdown();gFontRenderer.shutdown();
    if(wallpaperTexture){SDL_DestroyTexture(wallpaperTexture);wallpaperTexture=nullptr;}
    IMG_Quit();
    if(psmReady)psmExit();
    if (playStatsMutex) SDL_DestroyMutex(playStatsMutex);
    if (ipcMutex) SDL_DestroyMutex(ipcMutex);
    if (catalogueMutex) SDL_DestroyMutex(catalogueMutex);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    desknx::diag::Write("session", "end", "normal_exit=1");
    if(sdMounted)fsdevUnmountDevice("sdmc");
    return 0;
}
