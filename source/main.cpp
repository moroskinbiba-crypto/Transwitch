#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <desknx/Protocol.hpp>
#include <desknx/SafeReplace.hpp>
#include "../include/desknx/Model.hpp"
#include "../include/desknx/Layout.hpp"
#include "catalog.hpp"
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
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace desknx::model;
constexpr int SW = 1280, SH = 720, COLS = Columns, ROWS = Rows;
constexpr int CW = 154, CH = 112, GX = 14, GY = 14, LEFT = 141, TOP = 170;
constexpr int MAX_ITEMS = MaxItems, MAX_DESKS = MaxDesks;
constexpr const char* CFG_DIR = "sdmc:/switch/DeskNX";
constexpr const char* CFG_FILE = "sdmc:/switch/DeskNX/layout.tsv";
constexpr const char* CFG_TMP = "sdmc:/switch/DeskNX/layout.tmp";
constexpr const char* CFG_BACKUP_BASE = "sdmc:/switch/DeskNX/layout.corrupt";
constexpr const char* CFG_BAK_FILE = "sdmc:/switch/DeskNX/layout.bak";
size_t layoutRecoveredTiles = 0, layoutDroppedTiles = 0;
constexpr const char* HISTORY_FILE = "sdmc:/switch/DeskNX/recent.tsv";
constexpr const char* HISTORY_TMP = "sdmc:/switch/DeskNX/recent.tmp";
constexpr const char* HISTORY_BAK_FILE = "sdmc:/switch/DeskNX/recent.bak";

struct Color { Uint8 r, g, b, a; };
constexpr Color BG{17,18,22,255}, PANEL{34,36,42,255}, EDGE{66,69,77,255};
constexpr Color WHITE{250,250,252,255}, MUTED{164,168,178,255}, BLUE{10,132,255,255};
constexpr Color GREEN{48,209,88,255}, RED{255,69,58,255};
constexpr std::array<Color,8> COLORS{{
    {78,117,188,255},{45,139,123,255},{151,91,185,255},{187,116,66,255},
    {177,77,99,255},{67,135,177,255},{115,128,74,255},{89,99,119,255}
}};

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
std::vector<Workspace> desks;
std::vector<Candidate> catalogue;
SDL_mutex* catalogueMutex = nullptr;
SDL_mutex* ipcMutex = nullptr;
SDL_Thread* catalogueThread = nullptr;
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
FontRenderer gFontRenderer;
IconCache gIconCache;
struct HistoryEntry { Kind kind{Kind::Game}; std::string title, data; };
std::vector<HistoryEntry> recentLaunches;
std::string catalogueQuery;
desknx::catalog_view::FilterMode catalogueFilter=desknx::catalog_view::FilterMode::All;
desknx::catalog_view::SortMode catalogueSort=desknx::catalog_view::SortMode::Name;
int deskIndex=0, selectedX=0, selectedY=0, listIndex=0, listOffset=0, battery=-1;
bool catalogueTouchActive=false, catalogueTouchMoved=false;
int catalogueTouchStartY=0, catalogueTouchRow=0, lastCatalogueTap=-1;
Uint32 lastCatalogueTapTick=0;
bool editing=false, dragging=false, listOpen=false, recentOpen=false, deletePrompt=false, running=true;
int recentIndex=0;
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
void roundRect(int x,int y,int w,int h,int radius,Color c) {
    if(w<=0||h<=0) return;
    const int r=std::max(0,std::min(radius,std::min(w,h)/2));
    color(c);
    for(int row=0;row<h;++row) {
        int inset=0;
        if(row<r) {
            const double dy=static_cast<double>(r-row);
            inset=static_cast<int>(std::ceil(r-std::sqrt(std::max(0.0,static_cast<double>(r*r)-dy*dy))));
        } else if(row>=h-r) {
            const double dy=static_cast<double>(row-(h-r-1));
            inset=static_cast<int>(std::ceil(r-std::sqrt(std::max(0.0,static_cast<double>(r*r)-dy*dy))));
        }
        SDL_RenderDrawLine(renderer,x+inset,y+row,x+w-inset-1,y+row);
    }
}
void softCard(int x,int y,int w,int h,Color fill,Color edge=Color{255,255,255,18}) {
    roundRect(x,y+4,w,h,18,Color{0,0,0,45});
    roundRect(x,y,w,h,18,fill);
    roundRect(x+1,y+1,w-2,h-2,17,edge);
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
void readCatalogueState(bool& ready,bool& complete,bool& truncated) {
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    ready=catalogueReady; complete=catalogueScanComplete; truncated=catalogueTruncated;
    if(catalogueMutex) SDL_UnlockMutex(catalogueMutex);
}
void catalogueProgress(const desknx::catalog::ScanResult& snapshot, void*) {
    publishCatalogue(snapshot);
}
int catalogueThreadMain(void*) {
    (void)desknx::catalog::Scan(catalogueProgress,nullptr);
    return 0;
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
    listIndex=0;
    listOffset=0;
    catalogueThread=SDL_CreateThread(catalogueThreadMain,"DeskNX catalogue refresh",nullptr);
    if(!catalogueThread) {
        setStatus("BACKGROUND RESCAN UNAVAILABLE",3000);
        publishCatalogue(desknx::catalog::Scan());
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

void saveLayout() {
    if(!sdMounted)return;
    if (layoutSaveBlocked) {
        setStatus("SAVE BLOCKED: PRESERVE LAYOUT BACKUP", 4500);
        return;
    }
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    const std::string serialized=desknx::layout::serialize(desks,deskIndex);
    if(serialized.empty()){setStatus("LAYOUT INVALID; SAVE REFUSED",3500);return;}
    std::ofstream f(CFG_TMP,std::ios::binary|std::ios::trunc);
    if(!f){setStatus("SAVE FAILED: SD CARD NOT WRITABLE",3500);return;}
    f.write(serialized.data(),static_cast<std::streamsize>(serialized.size()));
    f.flush();const bool good=f.good();f.close();
    if(!good){std::remove(CFG_TMP);setStatus("SAVE FAILED: WRITE ERROR",3500);return;}
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
        return;
    }
    // Keep layout.bak as the last known-good version after successful replacement.
    layoutLoaded=true;
    setStatus("LAYOUT SAVED",1600);
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
}
void refreshBattery() {
    Uint32 now=SDL_GetTicks();if(now-lastBatteryTick<3000)return;lastBatteryTick=now;
    if(!psmReady&&R_SUCCEEDED(psmInitialize()))psmReady=true;
    if(psmReady){u32 pct=0;if(R_SUCCEEDED(psmGetBatteryChargePercentage(&pct)))battery=static_cast<int>(pct);}
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
void drawTileIcon(const Tile&t,int x,int y,int size) {
    Color c=COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    if(SDL_Texture* texture=gIconCache.get(t)) {
        int width=0,height=0;
        if(SDL_QueryTexture(texture,nullptr,nullptr,&width,&height)==0) {
            const int inset=std::max(2,size/24),inner=std::max(1,size-inset*2);
            const float factor=std::min(static_cast<float>(inner)/std::max(1,width),static_cast<float>(inner)/std::max(1,height));
            const int dw=std::max(1,static_cast<int>(width*factor)),dh=std::max(1,static_cast<int>(height*factor));
            SDL_Rect dst{x+(size-dw)/2,y+(size-dh)/2,dw,dh};SDL_RenderCopy(renderer,texture,nullptr,&dst);
            outline(x,y,size,size,Color{255,255,255,55});return;
        }
    }
    rect(x,y,size,size,c);outline(x,y,size,size,Color{255,255,255,55});
    std::string mark=initials(t.title);int s=size>=70?3:2;int w=static_cast<int>(mark.size())*6*s-s;
    text(mark,x+(size-w)/2,y+(size-7*s)/2,s,WHITE,x+size-2);
}
void drawTile(const Tile&t,bool selected) {
    SDL_Rect r=tileRect(t);Color accent=COLORS[static_cast<size_t>(t.color)%COLORS.size()];
    softCard(r.x,r.y,r.w,r.h,PANEL,selected?Color{10,132,255,180}:Color{255,255,255,22});
    roundRect(r.x+10,r.y+7,r.w-20,3,2,accent);
    if(t.kind==Kind::Clock) {
        text(clockText(),r.x+16,r.y+18,5,WHITE,r.x+r.w-10);
        text(clockText(true).substr(6),r.x+18,r.y+74,2,MUTED,r.x+r.w-10);
        text("CLOCK",r.x+18,r.y+r.h-23,1,accent,r.x+r.w-10);
    } else if(t.kind==Kind::Battery) {
        text("BATTERY",r.x+18,r.y+14,2,MUTED,r.x+r.w-10);
        text(battery>=0?std::to_string(battery)+"%":"N/A",r.x+18,r.y+41,4,WHITE,r.x+r.w-10);
        int maxW=std::max(1,r.w-42),value=battery>=0?maxW*battery/100:4;
        rect(r.x+20,r.y+r.h-24,std::max(4,value),7,battery>=20?GREEN:RED);outline(r.x+18,r.y+r.h-26,maxW+4,11,EDGE);
    } else if(t.kind==Kind::System) {
        text("SYSTEM",r.x+18,r.y+14,2,MUTED,r.x+r.w-10);text("DESKNX",r.x+18,r.y+39,3,WHITE,r.x+r.w-10);
        text("HOME REPLACEMENT INACTIVE",r.x+18,r.y+78,1,accent,r.x+r.w-10);
    } else if(t.kind==Kind::Unsupported) {
        rect(r.x+3,r.y+3,r.w-6,r.h-6,Color{52,55,61,255});
        text("UNSUPPORTED TILE",r.x+12,r.y+18,2,WHITE,r.x+r.w-10);
        text(t.unknownTypeCode.empty()?"?":t.unknownTypeCode,r.x+12,r.y+52,2,MUTED,r.x+r.w-10);
    } else if(t.kind==Kind::Recent) {
        text("RECENT LAUNCHES",r.x+18,r.y+10,2,MUTED,r.x+r.w-10);
        if(recentLaunches.empty())text("NOTHING LAUNCHED YET",r.x+18,r.y+44,2,WHITE,r.x+r.w-10);
        else {
            const size_t visible=std::min<size_t>(2,recentLaunches.size());
            for(size_t i=0;i<visible;++i) {
                const auto& e=recentLaunches[i];Tile iconTile{e.kind,e.title,e.data,0,0,1,1,static_cast<int>(i)};
                const int iy=r.y+37+static_cast<int>(i)*28;drawTileIcon(iconTile,r.x+18,iy,22);
                text(e.title,r.x+48,iy+5,1,WHITE,r.x+r.w-12);
            }
        }
        text("HISTORY STORED ON SD",r.x+18,r.y+r.h-18,1,accent,r.x+r.w-10);
    } else if(t.w==1&&t.h==1) {
        drawTileIcon(t,r.x+(r.w-56)/2,r.y+10,56);text(t.title,r.x+9,r.y+r.h-22,1,WHITE,r.x+r.w-6);
        if(t.kind==Kind::Nro)rect(r.x+7,r.y+7,5,5,GREEN);
    } else {
        int is=t.h>1?86:64;drawTileIcon(t,r.x+16,r.y+(r.h-is)/2,is);
        text(t.title,r.x+is+30,r.y+r.h/2-12,2,WHITE,r.x+r.w-9);
        text(t.kind==Kind::Nro?"HOMEBREW":"GAME",r.x+is+30,r.y+r.h/2+12,1,MUTED,r.x+r.w-9);
    }
    if(selected)rect(r.x+r.w-16,r.y+9,7,7,BLUE);
}
void drawDotted(int x,int y,int w,int h) {
    for(int xx=x;xx<x+w;xx+=12){rect(xx,y,6,2,Color{67,79,98,180});rect(xx,y+h-2,6,2,Color{67,79,98,180});}
    for(int yy=y;yy<y+h;yy+=12){rect(x,yy,2,6,Color{67,79,98,180});rect(x+w-2,yy,2,6,Color{67,79,98,180});}
}
int visibleStart() {
    return std::max(0,std::min(deskIndex-3,static_cast<int>(desks.size())-7));
}
void drawScreen() {
    const std::vector<Candidate>& available = listOpen ? visibleCatalogueSnapshot() : visibleCatalogueCache;
    bool catalogReadyNow=false,catalogCompleteNow=false,catalogTruncatedNow=false;
    readCatalogueState(catalogReadyNow,catalogCompleteNow,catalogTruncatedNow);
    refreshBattery();rect(0,0,SW,SH,BG);
    for(int y=0;y<SH;y+=28)rect(0,y,SW,1,Color{255,255,255,static_cast<Uint8>(y%3==0?7:3)});
    rect(0,0,SW,76,Color{24,25,30,245});rect(0,74,SW,1,Color{255,255,255,18});
    text("DESKNX",30,21,4,WHITE,260);text("PERSONAL HOME WORKSPACE",34,50,1,MUTED,420);
    text("DESKTOP "+std::to_string(deskIndex+1)+" OF "+std::to_string(desks.size()),470,28,1,MUTED,690);
    text(clockText(),1124,20,3,WHITE,1260);text(battery>=0?std::to_string(battery)+"%":"BAT --",1130,50,1,MUTED,1260);
    int start=visibleStart(),tabW=156,x=28;
    for(int i=0;i<7&&start+i<static_cast<int>(desks.size());++i) {
        int idx=start+i;bool active=idx==deskIndex;
        roundRect(x,94,tabW,42,14,active?Color{54,58,68,255}:Color{32,34,40,210});
        if(active) roundRect(x+10,132,tabW-20,2,1,BLUE);
        text(desks[static_cast<size_t>(idx)].name,x+12,109,1,active?WHITE:MUTED,x+tabW-8);
        x+=166;
    }
    text("L / R SWITCH DESK",930,110,1,MUTED,1105);
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(d.tiles.empty()) {
        softCard(390,292,500,132,Color{27,30,37,238},Color{255,255,255,24});
        text("EMPTY WORKSPACE",430,318,3,WHITE,850);
        text("PRESS Y TO ADD GAMES, HOMEBREW AND WIDGETS",432,365,1,BLUE,850);
        text("PRESS + TO CREATE ANOTHER DESKTOP",432,390,1,MUTED,850);
    }
    if(editing)for(int y=0;y<ROWS;++y)for(int x=0;x<COLS;++x)if(itemAt(d,x,y)<0) {
        int px=LEFT+x*(CW+GX),py=TOP+y*(CH+GY);drawDotted(px,py,CW,CH);
        if(x==selectedX&&y==selectedY){outline(px+2,py+2,CW-4,CH-4,BLUE);text("+",px+CW/2-4,py+CH/2-10,2,MUTED,px+CW-3);}
    }
    for(const auto&t:d.tiles)drawTile(t,selectedX>=t.x&&selectedX<t.x+t.w&&selectedY>=t.y&&selectedY<t.y+t.h);
    if(editing&&dragging)text("MOVING",1040,146,1,BLUE,1260);
    // Floating Dock: quick access to the library, edit mode and adjacent desktops.
    rect(0,628,SW,92,Color{23,24,29,250});rect(0,628,SW,1,Color{255,255,255,18});
    softCard(380,636,520,54,Color{48,50,58,245},Color{255,255,255,28});
    struct DockItem { int x; const char* glyph; const char* label; bool active; };
    const DockItem dock[]={{388,"APP","LIBRARY",listOpen},{490,"EDIT","LAYOUT",editing},
        {592,"LEFT","PREV",false},{694,"RIGHT","NEXT",false},
        {796,"HIST","RECENT",recentOpen}};
    for(const auto& item:dock) {
        const Color tileFill=item.active?Color{10,132,255,255}:Color{67,70,79,255};
        roundRect(item.x,642,44,40,12,tileFill);
        text(item.glyph,item.x+5,650,1,WHITE,item.x+40);
        text(item.label,item.x+50,657,1,item.active?BLUE:WHITE,item.x+104);
    }
    struct Hint { const char* key; const char* action; };
    const Hint leftHints[]={{"A",editing?(dragging?"DROP":"MOVE"):"OPEN"},{"X",editing?"DONE":"EDIT"},
                            {"Y","ADD"},{"ZL/ZR",editing?"SIZE":""}};
    const Hint rightHints[]={{"-",editing?"REMOVE":"POWER"},{"L/R","DESK"},{"+","NEW DESK"},{"RSTICK",editing?"DEL DESK":""}};
    for(int i=0;i<4;++i) {
        const int lx=18+i*92,rx=900+i*92;
        text(leftHints[i].key,lx,650,1,BLUE,lx+48);
        text(leftHints[i].action,lx,670,1,MUTED,lx+86);
        text(rightHints[i].key,rx,650,1,BLUE,rx+48);
        text(rightHints[i].action,rx,670,1,MUTED,rx+86);
    }
    bool show=statusUntil==0||SDL_GetTicks()<statusUntil;
    text(show?statusText:(editing?"EDIT MODE - EMPTY CELLS STAY EMPTY":"DESKNX WORKSPACE"),24,699,1,show?BLUE:MUTED,1230);
    if(listOpen) {
        rect(0,0,SW,SH,Color{0,0,0,175});int mx=210,my=70,mw=860,mh=570;softCard(mx,my,mw,mh,Color{31,33,39,252},Color{255,255,255,30});
        text("ADD TO DESKTOP",mx+28,my+22,3,WHITE,mx+mw-20);
        struct DrawerChip { int x; int w; const char* label; };
        const DrawerChip chips[]={{mx+28,96,"Y SEARCH"},{mx+132,82,"X SORT"},
            {mx+222,112,"L/R FILTER"},{mx+342,82,"- CLEAR"},
            {mx+432,126,"RSTICK RESCAN"},{mx+566,72,"B BACK"},
            {mx+646,72,"A ADD"}};
        for(const auto& chip:chips) {
            roundRect(chip.x,my+52,chip.w,25,8,Color{49,52,61,255});
            text(chip.label,chip.x+6,my+61,1,WHITE,chip.x+chip.w-4);
        }
        std::string viewLabel=std::string("FILTER: ")+desknx::catalog_view::FilterName(catalogueFilter)+
            "  SORT: "+desknx::catalog_view::SortName(catalogueSort);
        if(!catalogueQuery.empty()) viewLabel+="  QUERY: "+catalogueQuery;
        if(!catalogCompleteNow) viewLabel+="  / SCANNING";
        if(catalogueNsError!=0) {
            char nsError[40]{};
            std::snprintf(nsError,sizeof(nsError),"  / NS ERROR 0X%08X",static_cast<unsigned int>(catalogueNsError));
            viewLabel+=nsError;
        }
        text(viewLabel,mx+28,my+82,1,BLUE,mx+mw-20);
        text("UP / DOWN SELECT     A ADD     B CLOSE",mx+28,my+96,1,MUTED,mx+mw-20);
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
        if(!catalogCompleteNow && available.empty()) {
            text("SCANNING GAME AND HOMEBREW LIBRARY...",mx+32,my+128,2,BLUE,mx+mw-20);
            text("PLEASE WAIT - FIRST SCAN MAY TAKE A MOMENT",mx+32,my+160,1,MUTED,mx+mw-20);
        } else if(catalogCompleteNow && available.empty()) {
            if(!catalogueNsAvailable) {
                char errorText[96]{};
                std::snprintf(errorText,sizeof(errorText),"GAME SERVICE UNAVAILABLE  (0X%08X)",
                              static_cast<unsigned int>(catalogueNsError));
                text(errorText,mx+32,my+128,2,RED,mx+mw-20);
                text("INSTALLED GAMES COULD NOT BE READ",mx+32,my+162,1,MUTED,mx+mw-20);
            } else {
                text("NO MATCHING APPS",mx+32,my+128,2,WHITE,mx+mw-20);
                text("TRY ANOTHER FILTER OR CLEAR SEARCH",mx+32,my+162,1,MUTED,mx+mw-20);
            }
        } else {
            // The list occupies the left side; the persistent inspector keeps
            // the selected item's metadata visible without another modal.
            for(int row=0;row<vis;++row) {
                int i=listOffset+row;if(i>=static_cast<int>(available.size()))break;int yy=my+110+row*40;
                if(i==listIndex)roundRect(mx+22,yy,mx+604,36,9,Color{45,61,82,255});
                const Candidate& candidate=available[static_cast<size_t>(i)];
                Tile iconTile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
                drawTileIcon(iconTile,mx+30,yy+5,26);
                text(candidate.title,mx+66,yy+7,2,i==listIndex?WHITE:MUTED,mx+475);
                std::string appBadge = typeName(candidate.kind);
                if (candidate.kind == Kind::Game) {
                    if (candidate.viewFlags & BIT(4)) appBadge = "UPDATE";
                    else if (candidate.viewFlags & BIT(2)) appBadge = "GAMECARD";
                }
                text(appBadge,mx+486,yy+11,1,
                     (candidate.viewFlags & BIT(4)) ? Color{255,159,10,255} :
                     (i==listIndex?BLUE:MUTED),mx+620);
            }
            roundRect(mx+640,my+110,192,410,15,Color{25,27,33,255});
            outline(mx+640,my+110,192,410,Color{255,255,255,20});
            const Candidate& selected = available[static_cast<size_t>(
                std::max(0,std::min(listIndex,static_cast<int>(available.size())-1)))];
            Tile preview{selected.kind,selected.title,selected.data,0,0,1,1,selected.color};
            drawTileIcon(preview,mx+694,my+128,84);
            text(selected.title,mx+654,my+224,2,WHITE,mx+818);
            std::string detailType = typeName(selected.kind);
            if(selected.kind==Kind::Game && (selected.viewFlags&BIT(4))) detailType="UPDATE AVAILABLE";
            else if(selected.kind==Kind::Game && (selected.viewFlags&BIT(2))) detailType="GAME CARD";
            text(detailType,mx+654,my+266,1,BLUE,mx+818);
            if(selected.kind==Kind::Game) {
                text("TITLE ID",mx+654,my+296,1,MUTED,mx+818);
                text(selected.data,mx+654,my+314,1,WHITE,mx+818);
            } else if(selected.kind==Kind::Nro) {
                std::string fileName=selected.data;
                const size_t slash=fileName.find_last_of('/');
                const std::string folder=slash==std::string::npos?std::string{}:fileName.substr(0,slash);
                if(slash!=std::string::npos)fileName=fileName.substr(slash+1);
                text("FILE",mx+654,my+296,1,MUTED,mx+818);
                text(fileName,mx+654,my+314,1,WHITE,mx+818);
                text("FOLDER",mx+654,my+338,1,MUTED,mx+818);
                text(folder,mx+654,my+356,1,WHITE,mx+818);
                struct stat appStat{};
                if(stat(selected.data.c_str(),&appStat)==0 && S_ISREG(appStat.st_mode) && appStat.st_size>=0) {
                    const unsigned long long bytes=static_cast<unsigned long long>(appStat.st_size);
                    char sizeLabel[48]{};
                    if(bytes>=1024ULL*1024ULL)
                        std::snprintf(sizeLabel,sizeof(sizeLabel),"SIZE  %llu MB",bytes/(1024ULL*1024ULL));
                    else
                        std::snprintf(sizeLabel,sizeof(sizeLabel),"SIZE  %llu KB",bytes/1024ULL);
                    text(sizeLabel,mx+654,my+382,1,MUTED,mx+818);
                }
            } else {
                text("DESKTOP WIDGET",mx+654,my+296,1,MUTED,mx+818);
                text("ADDS LIVE INFORMATION",mx+654,my+314,1,WHITE,mx+818);
            }
            text(std::to_string(listIndex+1)+" / "+std::to_string(available.size()),
                 mx+654,my+472,1,MUTED,mx+818);
            text("A  ADD TO WORKSPACE",mx+654,my+490,1,BLUE,mx+818);
        }
        const std::string counts=std::to_string(catalogueGameCount)+" GAMES  /  "+
            std::to_string(catalogueNroCount)+" HOMEBREW  /  "+
            std::to_string(available.size())+" SHOWN";
        text(counts,mx+28,my+mh-26,1,MUTED,mx+620);
    }
    if(deletePrompt) {
        rect(0,0,SW,SH,Color{0,0,0,190});
        softCard(360,250,560,200,Color{31,33,39,255},Color{255,69,58,100});
        text("DELETE WORKSPACE?",405,278,3,WHITE,890);
        text(desks[static_cast<size_t>(deskIndex)].name,405,320,2,MUTED,890);
        roundRect(405,366,210,48,12,Color{112,38,42,255});
        outline(405,366,210,48,Color{255,69,58,130});
        text("DELETE",466,383,2,WHITE,600);
        roundRect(635,366,240,48,12,Color{54,57,66,255});
        text("CANCEL",706,383,2,WHITE,865);
        text("A CONFIRM     B CANCEL",405,425,1,MUTED,890);
    }
    // PowerMenu modal (ТЗ 6.1/6.2: "-" opens the power menu; PWR-03).
    if(recentOpen) {
        rect(0,0,SW,SH,Color{0,0,0,185});
        softCard(250,88,780,544,Color{31,33,39,252},Color{255,255,255,30});
        text("RECENT LAUNCHES",282,112,3,WHITE,850);
        roundRect(918,108,84,30,9,Color{49,52,61,255});
        text("B BACK",933,118,1,WHITE,995);
        text("UP / DOWN SELECT     A LAUNCH",282,153,1,MUTED,850);
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
    if(powerMenu) {
        static const char* kPowerItems[]={"SLEEP","REBOOT","SHUTDOWN"};
        rect(0,0,SW,SH,Color{0,0,0,190});
        softCard(420,220,440,280,Color{31,33,39,255},Color{255,255,255,28});
        text("POWER",460,242,3,WHITE,810);
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
void saveNow(){saveLayout();}
void updateCursor(int dx,int dy){selectedX=std::max(0,std::min(COLS-1,selectedX+dx));selectedY=std::max(0,std::min(ROWS-1,selectedY+dy));}
bool moveTile(int dx,int dy) {
    Workspace& d=desks[static_cast<size_t>(deskIndex)];int i=itemAt(d,selectedX,selectedY);if(i<0)return false;
    Tile&t=d.tiles[static_cast<size_t>(i)];int nx=t.x+dx,ny=t.y+dy;
    if(!placeable(d,t,nx,ny,i)){setStatus("MOVE BLOCKED: CELL OCCUPIED");return false;}
    t.x=nx;t.y=ny;selectedX=nx;selectedY=ny;return true;
}
bool moveAcrossDesk(int step) {
    if(desks.size()<2) return false;
    Workspace& src=desks[static_cast<size_t>(deskIndex)];
    int i=itemAt(src,selectedX,selectedY);
    if(i<0) return false;
    int next=(deskIndex+step+static_cast<int>(desks.size()))%static_cast<int>(desks.size());Tile t=src.tiles[static_cast<size_t>(i)];
    int nx=t.x,ny=t.y;Workspace& dst=desks[static_cast<size_t>(next)];
    if(!placeable(dst,t,nx,ny)&&!firstFree(dst,t,nx,ny)){setStatus("TARGET WORKSPACE IS FULL");return false;}
    src.tiles.erase(src.tiles.begin()+i);t.x=nx;t.y=ny;dst.tiles.push_back(t);deskIndex=next;selectedX=nx;selectedY=ny;
    setStatus("MOVED TO "+dst.name);return true;
}
void resizeTile(bool bigger) {
    Workspace& d=desks[static_cast<size_t>(deskIndex)];int i=itemAt(d,selectedX,selectedY);if(i<0){setStatus("SELECT A TILE FIRST");return;}
    const std::array<std::pair<int,int>,4> sizes{{{1,1},{2,1},{2,2},{3,2}}};Tile&t=d.tiles[static_cast<size_t>(i)];int at=0;
    for(size_t n=0;n<sizes.size();++n)if(sizes[n].first==t.w&&sizes[n].second==t.h){at=static_cast<int>(n);break;}
    int next=std::max(0,std::min(3,at+(bigger?1:-1)));Tile p=t;p.w=sizes[static_cast<size_t>(next)].first;p.h=sizes[static_cast<size_t>(next)].second;
    if(!placeable(d,p,t.x,t.y,i)){setStatus("RESIZE BLOCKED: NOT ENOUGH SPACE");return;}
    t.w=p.w;t.h=p.h;saveNow();
}
void removeTile() {
    Workspace& d=desks[static_cast<size_t>(deskIndex)];int i=itemAt(d,selectedX,selectedY);if(i<0){setStatus("EMPTY CELL");return;}
    std::string name=d.tiles[static_cast<size_t>(i)].title;d.tiles.erase(d.tiles.begin()+i);dragging=false;setStatus("REMOVED: "+name);saveNow();
}
void addSelected() {
    const auto& available=visibleCatalogueSnapshot();bool ready=false,complete=false,truncated=false;
    readCatalogueState(ready,complete,truncated);
    if(available.empty()){setStatus(complete?"NO APPS FOUND":"CATALOGUE IS STILL SCANNING",2200);return;}
    if(listIndex<0||static_cast<size_t>(listIndex)>=available.size())listIndex=0;
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    const Candidate& c=available[static_cast<size_t>(listIndex)];
    for(const auto& existing:d.tiles) {
        if(existing.kind==c.kind && existing.data==c.data) {
            Tile placeholder{Kind::Game,"","",0,0,1,1,0};
            int freeX=0,freeY=0;
            if(firstFree(d,placeholder,freeX,freeY)){selectedX=freeX;selectedY=freeY;}
            setStatus("ALREADY ON THIS WORKSPACE: "+c.title,2200);
            return;
        }
    }
    Tile t{c.kind,c.title,c.data,selectedX,selectedY,c.w,c.h,c.color};
    if(!placeable(d,t,t.x,t.y) && !firstFree(d,t,t.x,t.y)) {
        listOpen=false;
        setStatus("WORKSPACE FULL - REMOVE A TILE BEFORE ADDING APPS",3500);
        return;
    }
    d.tiles.push_back(t);editing=true;
    Tile placeholder{Kind::Game,"","",0,0,1,1,0};
    int freeX=0,freeY=0;
    if(firstFree(d,placeholder,freeX,freeY)) {
        selectedX=freeX;selectedY=freeY;
        setStatus("ADDED: "+t.title+"  -  SELECT ANOTHER OR PRESS B",2600);
    } else {
        selectedX=t.x;selectedY=t.y;listOpen=false;
        setStatus("DESKTOP FULL - ITEM ADDED",2600);
    }
    saveNow();
}
void openList(){
    Workspace& desk=desks[static_cast<size_t>(deskIndex)];
    if(itemAt(desk,selectedX,selectedY)>=0) {
        Tile placeholder{Kind::Game,"","",0,0,1,1,0};
        int freeX=0,freeY=0;
        if(!firstFree(desk,placeholder,freeX,freeY)) {
            setStatus("WORKSPACE FULL - REMOVE A TILE BEFORE ADDING APPS",3500);
            return;
        }
        selectedX=freeX;
        selectedY=freeY;
    }
    listOpen=true;
    listIndex=0;
    listOffset=0;
    lastCatalogueTap=-1;
}
void newDesk() {
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
    selectedX=selectedY=0;editing=true;saveNow();
    setStatus("NEW WORKSPACE CREATED - PRESS Y TO ADD ITEMS");
}
void renameDesk() {
    SwkbdConfig kbd{};if(R_FAILED(swkbdCreate(&kbd,0))){setStatus("KEYBOARD UNAVAILABLE");return;}
    swkbdConfigMakePresetDefault(&kbd);swkbdConfigSetHeaderText(&kbd,"Rename workspace");swkbdConfigSetInitialText(&kbd,desks[static_cast<size_t>(deskIndex)].name.c_str());
    char result[64]{};Result rc=showKeyboard(&kbd,result,sizeof(result));swkbdClose(&kbd);
    if(R_SUCCEEDED(rc)&&result[0]){desks[static_cast<size_t>(deskIndex)].name=result;saveNow();setStatus("WORKSPACE RENAMED");}
}
void deleteDesk() {
    if(desks.size()<=1){setStatus("AT LEAST ONE WORKSPACE IS REQUIRED");return;}
    deletePrompt=true;
}
void launch(const Tile& t) {
    if (t.kind == Kind::Unsupported) {
        setStatus("UNSUPPORTED TILE TYPE: " + t.unknownTypeCode, 3500);
        return;
    }
    if(t.kind==Kind::Game) {
        if(!desknx::model::isTitleId(t.data)){setStatus("INVALID TITLE ID");return;}
        const u64 id=std::strtoull(t.data.c_str(),nullptr,16);
        if(id==0){setStatus("INVALID TITLE ID");return;}
#ifdef DESKNX_SYSTEM_APPLET
        // LNC-02/LNC-03: pass the selected user only when we actually have a
        // valid uid; zero uid means "backend decides" (kept v1-compatible).
        // HasUser forces the preselection even if NACP would skip it.
        const AccountUid uid=g_backendHasUser?g_backendUser:AccountUid{};
        const u64 flags=g_backendHasUser?desknx::ipc::LaunchFlagHasUser:0;
        const Result rc=sendIpc([&]{return desknx::ipc::SendLaunchTitle(id,flags,uid);});
        if(R_FAILED(rc)) {
            char b[72]{};
            std::snprintf(b,sizeof(b),"SYSTEM BACKEND REQUEST FAILED: 0X%08X",static_cast<unsigned int>(rc));
            setStatus(b,4000);
            return;
        }
        recordRecentLaunch(t);
        saveLayout();
        appletRequestExitToSelf();
        running=false;
#else
        if(R_FAILED(nsInitialize())){setStatus("NS SERVICE UNAVAILABLE IN THIS CONTEXT",4000);return;}
        const Result rc=appletRequestLaunchApplication(id,nullptr);
        nsExit();
        if(R_FAILED(rc)){char b[64]{};std::snprintf(b,sizeof(b),"GAME LAUNCH FAILED: 0X%08X",static_cast<unsigned int>(rc));setStatus(b,4000);}
        else {recordRecentLaunch(t);setStatus("LAUNCH REQUEST SENT");}
#endif
        return;
    }
    if(t.kind==Kind::Nro) {
        if(!isSafeNroPath(t.data)){setStatus("INVALID OR MISSING NRO PATH",3500);return;}
#ifdef DESKNX_SYSTEM_APPLET
        const Result rc=sendIpc([&]{return desknx::ipc::SendLaunchHomebrew(t.data.c_str(),"");});
        if(R_FAILED(rc)) {
            char b[72]{};
            std::snprintf(b,sizeof(b),"HOMEBREW REQUEST FAILED: 0X%08X",static_cast<unsigned int>(rc));
            setStatus(b,4000);
            return;
        }
        recordRecentLaunch(t);
        saveLayout();
        appletRequestExitToSelf();
        running=false;
#else
        if(!envHasNextLoad()){setStatus("NRO CHAIN-LOAD UNAVAILABLE; USE HBMENU TAKEOVER",4500);return;}
        if(R_FAILED(envSetNextLoad(t.data.c_str(),""))){setStatus("CANNOT PREPARE NEXT NRO");return;}
        recordRecentLaunch(t);
        appletRequestExitToSelf();
#endif
        return;
    }
    if(t.kind==Kind::Battery)setStatus(battery>=0?"BATTERY: "+std::to_string(battery)+"%":"BATTERY STATUS UNAVAILABLE");
    else if(t.kind==Kind::System)setStatus("PROTOTYPE ONLY - SYSTEM HOME REPLACEMENT IS INACTIVE",3500);
    else if(t.kind==Kind::Clock)setStatus("CLOCK WIDGET");
    else if(t.kind==Kind::Recent) {
        if(recentLaunches.empty())setStatus("NO RECENT LAUNCHES YET");
        else {recentIndex=0;recentOpen=true;}
    } else setStatus(lastLaunched);
}
void keyInput(u64 keys, bool fromRepeat) {
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
        if(keys&HidNpadButton_A){deletePrompt=false;desks.erase(desks.begin()+deskIndex);deskIndex=std::min(deskIndex,static_cast<int>(desks.size())-1);selectedX=selectedY=0;saveNow();setStatus("WORKSPACE DELETED");}
        return;
    }
    if(listOpen) {
        const auto& available=visibleCatalogueSnapshot();
        if(keys&HidNpadButton_B){listOpen=false;return;}
        if(keys&HidNpadButton_Y){searchCatalogue();return;}
        if(keys&HidNpadButton_X){cycleCatalogueSort();return;}
        if(keys&HidNpadButton_L){cycleCatalogueFilter(-1);return;}
        if(keys&HidNpadButton_R){cycleCatalogueFilter(1);return;}
        if(keys&HidNpadButton_Minus){catalogueQuery.clear();++catalogueViewRevision;listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);return;}
        if(keys&HidNpadButton_StickR){restartCatalogueScan();return;}
        if(available.empty())return;
        if(listIndex>=static_cast<int>(available.size()))listIndex=static_cast<int>(available.size())-1;
        if(keys&HidNpadButton_Up)listIndex=(listIndex-1+static_cast<int>(available.size()))%static_cast<int>(available.size());
        if(keys&HidNpadButton_Down)listIndex=(listIndex+1)%static_cast<int>(available.size());
        if(keys&HidNpadButton_A)addSelected();
        return;
    }
    if(keys&HidNpadButton_B) {
        if(dragging){dragging=false;saveNow();setStatus("POSITION SAVED");return;}
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
    if(keys&HidNpadButton_StickR&&editing)deleteDesk();
    if(keys&HidNpadButton_X){editing=!editing;if(!editing&&dragging){dragging=false;saveNow();}setStatus(editing?"EDIT MODE - EMPTY CELLS STAY EMPTY":"EDIT MODE CLOSED");}
    if(keys&HidNpadButton_Y){editing=true;openList();}
    if(keys&HidNpadButton_L) {
        if(dragging) moveAcrossDesk(-1);
        else {
            deskIndex=(deskIndex-1+static_cast<int>(desks.size()))%static_cast<int>(desks.size());
            saveNow();
            setStatus(desks[static_cast<size_t>(deskIndex)].name);
        }
    } else if(keys&HidNpadButton_R) {
        if(dragging) moveAcrossDesk(1);
        else {
            deskIndex=(deskIndex+1)%static_cast<int>(desks.size());
            saveNow();
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
void touchSelect(int x,int y) {
    if(x<LEFT||x>=LEFT+COLS*(CW+GX)||y<TOP||y>=TOP+ROWS*(CH+GY))return;
    int cx=std::max(0,std::min(COLS-1,(x-LEFT)/(CW+GX))),cy=std::max(0,std::min(ROWS-1,(y-TOP)/(CH+GY)));
    if(dragging){int dx=cx-selectedX,dy=cy-selectedY;if(dx&&dy){moveTile(dx,0);moveTile(0,dy);}else if(dx||dy)moveTile(dx,dy);return;}
    selectedX=cx;selectedY=cy;
    if(editing){int i=itemAt(desks[static_cast<size_t>(deskIndex)],cx,cy);if(i>=0){const Tile&t=desks[static_cast<size_t>(deskIndex)].tiles[static_cast<size_t>(i)];selectedX=t.x;selectedY=t.y;}}
}
int tabStart(){return std::max(0,std::min(deskIndex-3,static_cast<int>(desks.size())-7));}
void touchInput(const SDL_Event&e) {
    if(e.type==SDL_FINGERDOWN && deletePrompt) {
        const int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(y>=366&&y<=414&&x>=405&&x<615) keyInput(HidNpadButton_A,false);
        else if(y>=366&&y<=414&&x>=635&&x<=875) keyInput(HidNpadButton_B,false);
        return;
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
            if(x>=910&&x<=1010&&y>=104&&y<=150) {
                recentOpen=false;
            } else if(y>=190&&y<580&&!recentLaunches.empty()) {
                const int index=(y-190)/39;
                if(index>=0&&static_cast<size_t>(index)<recentLaunches.size()) {
                    const HistoryEntry entry=recentLaunches[static_cast<size_t>(index)];
                    recentOpen=false;
                    launch(Tile{entry.kind,entry.title,entry.data,0,0,1,1,0});
                }
            }
        }
        return;
    }
    if(e.type==SDL_FINGERDOWN) {
        int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);
        if(listOpen) {
            if(y>=122&&y<=151) {
                if(x>=238&&x<334) searchCatalogue();
                else if(x>=342&&x<424) cycleCatalogueSort();
                else if(x>=432&&x<544) cycleCatalogueFilter(1);
                else if(x>=552&&x<634){catalogueQuery.clear();++catalogueViewRevision;listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);}
                else if(x>=642&&x<768) restartCatalogueScan();
                else if(x>=776&&x<848) listOpen=false;
                else if(x>=856&&x<928) addSelected();
                return;
            }
            if(y>=550&&y<=584&&x>=850&&x<=1042) {
                addSelected();
                return;
            }
            if(y>=180&&y<580&&x<830) {
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
                        return;
                    } else {
                        listIndex=index;
                        lastCatalogueTap=index;
                        lastCatalogueTapTick=now;
                    }
                    catalogueTouchActive=true;
                    catalogueTouchMoved=false;
                    catalogueTouchStartY=y;
                    catalogueTouchRow=row;
                }
                return;
            }
            return;
        }
        if(deletePrompt)return;
        if(y>=92&&y<=140){
            int idx=tabStart()+(x-28)/166;
            const int tabX=28+(idx-tabStart())*166;
            if(x>=tabX&&x<tabX+156&&idx>=0&&idx<static_cast<int>(desks.size())&&idx<tabStart()+7&&idx!=deskIndex){
                deskIndex=idx;
                saveNow();
                setStatus(desks[static_cast<size_t>(deskIndex)].name);
            }
            return;
        }
        if(y>=TOP&&y<TOP+ROWS*(CH+GY)) {
            int oldX=selectedX,oldY=selectedY;touchSelect(x,y);Workspace&d=desks[static_cast<size_t>(deskIndex)];int i=itemAt(d,selectedX,selectedY);
            if(editing){if(i>=0&&oldX==selectedX&&oldY==selectedY){dragging=true;touchDragging=true;}else if(i<0)openList();}
            else if(i>=0)launch(d.tiles[static_cast<size_t>(i)]);
            else openList();
            return;
        }
        if(y>=632&&y<=700) {
            if(x>=380&&x<490) {openList();return;}
            if(x>=490&&x<590) {editing=!editing;if(!editing&&dragging){dragging=false;saveNow();}setStatus(editing?"EDIT MODE":"EDIT MODE CLOSED");return;}
            if(x>=590&&x<690) {deskIndex=(deskIndex-1+static_cast<int>(desks.size()))%static_cast<int>(desks.size());saveNow();setStatus(desks[static_cast<size_t>(deskIndex)].name);return;}
            if(x>=690&&x<790) {deskIndex=(deskIndex+1)%static_cast<int>(desks.size());saveNow();setStatus(desks[static_cast<size_t>(deskIndex)].name);return;}
            if(x>=790&&x<900) {recentIndex=0;recentOpen=true;return;}
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
    } else if(e.type==SDL_FINGERUP&&listOpen&&catalogueTouchActive) {
        if(catalogueTouchMoved) lastCatalogueTap=-1;
        catalogueTouchActive=false;
        catalogueTouchMoved=false;
    } else if(e.type==SDL_FINGERMOTION&&touchDragging) {
        touchSelect(static_cast<int>(e.tfinger.x*SW),static_cast<int>(e.tfinger.y*SH));
    } else if(e.type==SDL_FINGERUP&&touchDragging) {
        touchDragging=false;dragging=false;saveNow();
    }
}
} // namespace

int main(int,char**) {
    if(R_SUCCEEDED(fsdevMountSdmc()))sdMounted=true;
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){if(sdMounted)fsdevUnmountDevice("sdmc");return 1;}
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    window=SDL_CreateWindow("DeskNX",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SW,SH,SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(!window){SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 2;}
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){SDL_DestroyWindow(window);SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 3;}
    SDL_RenderSetLogicalSize(renderer,SW,SH);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    catalogueMutex=SDL_CreateMutex();ipcMutex=SDL_CreateMutex();

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
    } else if (!layoutLoaded) {
        defaultLayout();
    }
    if (layoutRecoveredTiles > 0 || layoutDroppedTiles > 0) {
        char message[96]{};
        std::snprintf(message, sizeof(message), "LAYOUT RECOVERED: %zu MOVED, %zu OMITTED",
                      layoutRecoveredTiles, layoutDroppedTiles);
        setStatus(message, 6000);
    } else if (restoredFromBackup) {
        setStatus("LAYOUT RESTORED FROM BACKUP", 5000);
    }

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
    IMG_Init(IMG_INIT_JPG);
    gIconCache.setRenderer(renderer);
    gFontRenderer.setRenderer(renderer);
    gFontRenderer.initialize(); // slow pl initialization cannot delay MenuReady
    if(layoutNeedsMigration&&layoutLoaded){saveLayout();layoutNeedsMigration=false;}

    catalogueThread=SDL_CreateThread(catalogueThreadMain,"DeskNX catalogue",nullptr);
    if(!catalogueThread){setStatus("BACKGROUND SCAN UNAVAILABLE",3000);publishCatalogue(desknx::catalog::Scan());}
    while(running&&appletMainLoop()) {
        bool ready=false,complete=false,truncated=false;readCatalogueState(ready,complete,truncated);
        if(complete&&!layoutLoaded){defaultLayout();saveLayout();layoutLoaded=true;setStatus("DESKNX READY - Y ADD  X EDIT",4000);}
        if(complete&&truncated&&!catalogueWarningShown){catalogueWarningShown=true;setStatus("CATALOGUE LIMIT REACHED - SOME ITEMS HIDDEN",5000);}
#ifdef DESKNX_SYSTEM_APPLET
        if(SDL_AtomicGet(&heartbeatError)){SDL_AtomicSet(&heartbeatError,0);setStatus("SYSTEM BACKEND IPC UNAVAILABLE",2500);}
        consumeInitialState(); // IPC-06: queued at backend launch, read once
        pollBackendEvents();   // §4.4: AppExited / AppLaunchFailed / SdRemoved / LowBattery
#endif
        padUpdate(&pad);const u64 keys=padGetButtonsDown(&pad);if(keys)keyInput(keys, false);
        // UI-14 (P1): holding a direction repeats the cursor move - 350 ms
        // initial delay, then every 80 ms. Only when no modal owns input.
        {
            static Uint32 holdStart = 0, lastRepeat = 0;
            static u64 lastHeldDirs = 0;
            const u64 held = padGetButtons(&pad);
            const u64 dirs = HidNpadButton_Left|HidNpadButton_Right|HidNpadButton_Up|HidNpadButton_Down;
            // Navigation modals handle directional input themselves; keep key
            // repeat active in the library, recent list and power menu. The
            // destructive workspace-confirmation prompt remains single-shot.
            const bool modalBlocksRepeat = deletePrompt;
            if(!modalBlocksRepeat && (held&dirs) && !dragging) {
                const Uint32 now = SDL_GetTicks();
                if((held&dirs) != lastHeldDirs) { holdStart=now; lastRepeat=now; lastHeldDirs=held&dirs; }
                else if(now-holdStart >= 350 && now-lastRepeat >= 80) {
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
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT){appletRequestExitToSelf();running=false;}else touchInput(event);}
        drawScreen();svcSleepThread(16'000'000);
    }
#ifdef DESKNX_SYSTEM_APPLET
    stopHeartbeatThread();
#endif
    if(catalogueThread){SDL_WaitThread(catalogueThread,nullptr);catalogueThread=nullptr;}
    gIconCache.shutdown();gFontRenderer.shutdown();IMG_Quit();
    if(psmReady)psmExit();
    if (ipcMutex) SDL_DestroyMutex(ipcMutex);
    if (catalogueMutex) SDL_DestroyMutex(catalogueMutex);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");
    return 0;
}
