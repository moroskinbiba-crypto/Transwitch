#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <desknx/Protocol.hpp>
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

struct Color { Uint8 r, g, b, a; };
constexpr Color BG{14,17,25,255}, PANEL{24,29,40,255}, EDGE{55,66,83,255};
constexpr Color WHITE{242,246,252,255}, MUTED{147,160,181,255}, BLUE{92,166,255,255};
constexpr Color GREEN{97,214,174,255}, RED{246,105,119,255};
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
bool layoutLoaded = false, layoutNeedsMigration = false, catalogueWarningShown = false;
FontRenderer gFontRenderer;
IconCache gIconCache;
struct HistoryEntry { Kind kind{Kind::Game}; std::string title, data; };
std::vector<HistoryEntry> recentLaunches;
std::string catalogueQuery;
desknx::catalog_view::FilterMode catalogueFilter=desknx::catalog_view::FilterMode::All;
desknx::catalog_view::SortMode catalogueSort=desknx::catalog_view::SortMode::Name;
int deskIndex=0, selectedX=0, selectedY=0, listIndex=0, listOffset=0, battery=-1;
bool editing=false, dragging=false, listOpen=false, deletePrompt=false, running=true;
bool sdMounted=false, psmReady=false, touchDragging=false;
std::string statusText="Y ADD   X EDIT   A OPEN", lastLaunched="Nothing launched yet";
Uint32 statusUntil=0, lastBatteryTick=0;

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
std::string ascii(const std::string& input) {
    std::string out;
    for(size_t i=0;i<input.size();) {
        unsigned char b=static_cast<unsigned char>(input[i]);
        if(b<128) { out.push_back(static_cast<char>(b)); ++i; continue; }
        uint32_t cp=0; size_t n=1;
        if((b&0xE0)==0xC0&&i+1<input.size()) { cp=((b&31)<<6)|(static_cast<unsigned char>(input[i+1])&63); n=2; }
        else if((b&0xF0)==0xE0&&i+2<input.size()) { cp=((b&15)<<12)|((static_cast<unsigned char>(input[i+1])&63)<<6)|(static_cast<unsigned char>(input[i+2])&63); n=3; }
        else { ++i; out.push_back(' '); continue; }
        i+=n; if(cp>=0x430&&cp<=0x44f) cp-=0x20; if(cp==0x401) cp=0x415;
        switch(cp) {
            case 0x410:out+="A";break; case 0x411:out+="B";break; case 0x412:out+="V";break; case 0x413:out+="G";break;
            case 0x414:out+="D";break; case 0x415:out+="E";break; case 0x416:out+="ZH";break; case 0x417:out+="Z";break;
            case 0x418:case 0x419:out+="I";break; case 0x41a:out+="K";break; case 0x41b:out+="L";break;
            case 0x41c:out+="M";break; case 0x41d:out+="N";break; case 0x41e:out+="O";break; case 0x41f:out+="P";break;
            case 0x420:out+="R";break; case 0x421:out+="S";break; case 0x422:out+="T";break; case 0x423:out+="U";break;
            case 0x424:out+="F";break; case 0x425:out+="H";break; case 0x426:out+="TS";break; case 0x427:out+="CH";break;
            case 0x428:out+="SH";break; case 0x429:out+="SCH";break; case 0x42b:out+="Y";break; case 0x42d:out+="E";break;
            case 0x42e:out+="YU";break; case 0x42f:out+="YA";break; default:out+=" ";break;
        }
    }
    for(char& c:out) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
void text(const std::string& value,int x,int y,int scale,Color c,int right=1260) {
    if(gFontRenderer.draw(value,x,y,scale,SDL_Color{c.r,c.g,c.b,c.a},right))return;
    const std::string s=ascii(value); color(c); int px=x;
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
std::vector<Candidate> visibleCatalogueSnapshot() {
    const auto all=catalogueSnapshot();
    std::vector<desknx::catalog_view::RecentKey> recent;
    recent.reserve(recentLaunches.size());
    for(const auto& item:recentLaunches) recent.push_back({item.kind,item.data});
    return desknx::catalog_view::FilterAndSort(all,catalogueQuery,catalogueFilter,catalogueSort,recent);
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
        listIndex=0;listOffset=0;
        setStatus(catalogueQuery.empty()?"SEARCH CLEARED":"SEARCH: "+catalogueQuery,2500);
    }
}
void cycleCatalogueFilter(int direction) {
    constexpr int count=4;
    int current=static_cast<int>(catalogueFilter);
    current=(current+direction+count)%count;
    catalogueFilter=static_cast<desknx::catalog_view::FilterMode>(current);
    listIndex=0;listOffset=0;
    setStatus(std::string("FILTER: ")+desknx::catalog_view::FilterName(catalogueFilter),1800);
}
void cycleCatalogueSort() {
    constexpr int count=3;
    int current=(static_cast<int>(catalogueSort)+1)%count;
    catalogueSort=static_cast<desknx::catalog_view::SortMode>(current);
    listIndex=0;listOffset=0;
    setStatus(std::string("SORT: ")+desknx::catalog_view::SortName(catalogueSort),1800);
}
void publishCatalogue(const desknx::catalog::ScanResult& result) {
    if(catalogueMutex) SDL_LockMutex(catalogueMutex);
    catalogue=result.items;
    catalogueTruncated=result.truncated;
    catalogueReady=!catalogue.empty()||result.complete;
    catalogueScanComplete=result.complete;
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
void startHeartbeatThread() {
    SDL_AtomicSet(&heartbeatError,0); SDL_AtomicSet(&heartbeatRunning,1);
    heartbeatThread=SDL_CreateThread(heartbeatThreadMain,"DeskNX heartbeat",nullptr);
    if(!heartbeatThread){SDL_AtomicSet(&heartbeatRunning,0);setStatus("HEARTBEAT THREAD FAILED",4000);}
}
void stopHeartbeatThread() {
    SDL_AtomicSet(&heartbeatRunning,0);
    if(heartbeatThread){SDL_WaitThread(heartbeatThread,nullptr);heartbeatThread=nullptr;}
}
#endif
void loadHistory() {
    if(!sdMounted)return;
    std::ifstream input(HISTORY_FILE,std::ios::binary);
    std::string line;
    if(!input||!std::getline(input,line)||line!="DESKNXH1")return;
    while(std::getline(input,line)&&recentLaunches.size()<10) {
        auto fields=splitTabs(line);
        if(fields.size()<3)continue;
        Kind kind=parseKind(fields[0]);
        if((kind!=Kind::Game&&kind!=Kind::Nro)||fields[1].empty()||fields[2].empty())continue;
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
    std::remove(HISTORY_FILE);
    if(std::rename(HISTORY_TMP,HISTORY_FILE)!=0){std::remove(HISTORY_TMP);setStatus("RECENT HISTORY RENAME FAILED",3000);}
}
void recordRecentLaunch(const Tile& tile) {
    if(tile.kind!=Kind::Game&&tile.kind!=Kind::Nro)return;
    recentLaunches.erase(std::remove_if(recentLaunches.begin(),recentLaunches.end(),
        [&](const HistoryEntry& e){return e.kind==tile.kind&&e.data==tile.data;}),recentLaunches.end());
    recentLaunches.insert(recentLaunches.begin(),{tile.kind,tile.title,tile.data});
    if(recentLaunches.size()>10)recentLaunches.resize(10);
    lastLaunched=tile.title;saveHistory();
}
void backupBrokenLayout() {
    struct stat st{};
    if (stat(CFG_FILE, &st) != 0) return;
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
            return;
        }
        break;
    }
    setStatus("LAYOUT INVALID; BACKUP FAILED", 4500);
}

void saveLayout() {
    if(!sdMounted)return;
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    const std::string serialized=desknx::layout::serialize(desks,deskIndex);
    if(serialized.empty()){setStatus("LAYOUT INVALID; SAVE REFUSED",3500);return;}
    std::ofstream f(CFG_TMP,std::ios::binary|std::ios::trunc);
    if(!f){setStatus("SAVE FAILED: SD CARD NOT WRITABLE",3500);return;}
    f.write(serialized.data(),static_cast<std::streamsize>(serialized.size()));
    f.flush();const bool good=f.good();f.close();
    if(!good){std::remove(CFG_TMP);setStatus("SAVE FAILED: WRITE ERROR",3500);return;}
    std::remove(CFG_BAK_FILE);
    const bool hadPrevious=std::rename(CFG_FILE,CFG_BAK_FILE)==0;
    if(std::rename(CFG_TMP,CFG_FILE)!=0) {
        if(hadPrevious)std::rename(CFG_BAK_FILE,CFG_FILE);
        std::remove(CFG_TMP);setStatus("SAVE FAILED: RENAME ERROR",3500);return;
    }
    // Keep layout.bak as the last known-good version until the next successful save.
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
    std::string s=ascii(name),out;bool begin=true;
    for(char c:s){if(c==' '||c=='-'||c=='_'||c=='.'){begin=true;continue;}if(begin){out.push_back(c);begin=false;if(out.size()==2)break;}}
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
    rect(r.x,r.y,r.w,r.h,PANEL);rect(r.x,r.y,r.w,3,accent);outline(r.x,r.y,r.w,r.h,selected?BLUE:EDGE);
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
    const auto available=visibleCatalogueSnapshot();
    bool catalogReadyNow=false,catalogCompleteNow=false,catalogTruncatedNow=false;
    readCatalogueState(catalogReadyNow,catalogCompleteNow,catalogTruncatedNow);
    refreshBattery();rect(0,0,SW,SH,BG);
    for(int y=0;y<SH;y+=28)rect(0,y,SW,1,Color{255,255,255,static_cast<Uint8>(y%3==0?7:3)});
    rect(0,0,SW,76,Color{19,23,33,255});rect(0,74,SW,2,Color{43,53,69,255});
    text("DESKNX",30,21,4,WHITE,260);text("PERSONAL HOME WORKSPACE",34,50,1,MUTED,420);
    text(clockText(),1124,20,3,WHITE,1260);text(battery>=0?std::to_string(battery)+"%":"BAT --",1130,50,1,MUTED,1260);
    int start=visibleStart(),tabW=156,x=28;
    for(int i=0;i<7&&start+i<static_cast<int>(desks.size());++i) {
        int idx=start+i;bool active=idx==deskIndex;rect(x,94,tabW,42,active?Color{42,61,86,255}:Color{24,29,40,255});
        outline(x,94,tabW,42,active?BLUE:EDGE);text(desks[static_cast<size_t>(idx)].name,x+12,109,1,active?WHITE:MUTED,x+tabW-8);
        if(active) rect(x+10,132,tabW-20,2,BLUE);
        x+=166;
    }
    text("L / R SWITCH DESK",930,110,1,MUTED,1105);
    Workspace& d=desks[static_cast<size_t>(deskIndex)];
    if(editing)for(int y=0;y<ROWS;++y)for(int x=0;x<COLS;++x)if(itemAt(d,x,y)<0) {
        int px=LEFT+x*(CW+GX),py=TOP+y*(CH+GY);drawDotted(px,py,CW,CH);
        if(x==selectedX&&y==selectedY){outline(px+2,py+2,CW-4,CH-4,BLUE);text("+",px+CW/2-4,py+CH/2-10,2,MUTED,px+CW-3);}
    }
    for(const auto&t:d.tiles)drawTile(t,selectedX>=t.x&&selectedX<t.x+t.w&&selectedY>=t.y&&selectedY<t.y+t.h);
    if(editing&&dragging)text("MOVING",1040,146,1,BLUE,1260);
    rect(0,628,SW,92,Color{18,22,31,255});rect(0,628,SW,2,EDGE);
    struct Hint{const char*k;const char*v;};
    const Hint hints[]={{"A",editing?(dragging?"DROP":"MOVE"):"OPEN"},{"X",editing?"DONE":"EDIT"},{"Y","ADD"},{"ZL/ZR","SIZE"},{"-","REMOVE"},{"L/R","DESK"},{"+","NEW DESK"}};
    int hx=18;for(const auto& h:hints){rect(hx,650,50,28,Color{37,45,59,255});outline(hx,650,50,28,EDGE);text(h.k,hx+5,659,1,WHITE,hx+47);text(h.v,hx+56,659,1,MUTED,hx+150);hx+=178;}
    bool show=statusUntil==0||SDL_GetTicks()<statusUntil;
    text(show?statusText:(editing?"EDIT MODE - EMPTY CELLS STAY EMPTY":"DESKNX WORKSPACE"),24,694,1,show?BLUE:MUTED,850);
    if(listOpen) {
        rect(0,0,SW,SH,Color{0,0,0,175});int mx=210,my=70,mw=860,mh=570;rect(mx,my,mw,mh,Color{22,27,38,250});outline(mx,my,mw,mh,BLUE);
        text("ADD TO DESKTOP",mx+28,my+22,3,WHITE,mx+mw-20);
        text("A ADD  B BACK  Y SEARCH  X SORT  L/R FILTER  - CLEAR",mx+28,my+59,1,MUTED,mx+mw-20);
        std::string viewLabel=std::string("FILTER: ")+desknx::catalog_view::FilterName(catalogueFilter)+
            "  SORT: "+desknx::catalog_view::SortName(catalogueSort);
        if(!catalogueQuery.empty()) viewLabel+="  QUERY: "+catalogueQuery;
        if(!catalogCompleteNow) viewLabel+="  / SCANNING";
        text(viewLabel,mx+28,my+77,1,BLUE,mx+mw-20);
        int vis=10;if(listIndex<listOffset)listOffset=listIndex;if(listIndex>=listOffset+vis)listOffset=listIndex-vis+1;
        if(!catalogCompleteNow && available.empty())
            text("SCANNING GAME AND HOMEBREW LIBRARY...",mx+32,my+118,2,BLUE,mx+mw-20);
        else {
            for(int row=0;row<vis;++row) {
                int i=listOffset+row;if(i>=static_cast<int>(available.size()))break;int yy=my+92+row*43;
                if(i==listIndex)rect(mx+22,yy,mw-44,38,Color{45,61,82,255});
                const Candidate& candidate=available[static_cast<size_t>(i)];
                Tile iconTile{candidate.kind,candidate.title,candidate.data,0,0,1,1,candidate.color};
                drawTileIcon(iconTile,mx+30,yy+6,26);
                text(candidate.title,mx+66,yy+8,2,i==listIndex?WHITE:MUTED,mx+575);
                text(typeName(candidate.kind),mx+595,yy+12,1,i==listIndex?BLUE:MUTED,mx+mw-18);
            }
        }
        const size_t totalItems=catalogueSnapshot().size();
        text(std::to_string(available.size())+" SHOWN / "+std::to_string(totalItems)+" TOTAL ITEMS",
             mx+28,my+mh-26,1,MUTED,mx+mw-18);
    }
    if(deletePrompt) {
        rect(0,0,SW,SH,Color{0,0,0,185});rect(360,250,560,200,Color{25,30,42,255});outline(360,250,560,200,RED);
        text("DELETE WORKSPACE?",405,292,3,WHITE,890);text(desks[static_cast<size_t>(deskIndex)].name,405,334,2,MUTED,890);
        text("A CONFIRM   /   B CANCEL",405,394,2,BLUE,890);
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
    const auto available=visibleCatalogueSnapshot();bool ready=false,complete=false,truncated=false;
    readCatalogueState(ready,complete,truncated);
    if(available.empty()){setStatus(complete?"NO APPS FOUND":"CATALOGUE IS STILL SCANNING",2200);return;}
    if(listIndex<0||static_cast<size_t>(listIndex)>=available.size())listIndex=0;
    Workspace& d=desks[static_cast<size_t>(deskIndex)];if(itemAt(d,selectedX,selectedY)>=0){listOpen=false;setStatus("CHOOSE AN EMPTY CELL FIRST");return;}
    const Candidate& c=available[static_cast<size_t>(listIndex)];Tile t{c.kind,c.title,c.data,selectedX,selectedY,c.w,c.h,c.color};
    if(!placeable(d,t,t.x,t.y)){listOpen=false;setStatus("ITEM NEEDS MORE SPACE");return;}
    d.tiles.push_back(t);selectedX=t.x;selectedY=t.y;listOpen=false;editing=true;setStatus("ADDED: "+t.title);saveNow();
}
void openList(){listOpen=true;listIndex=0;listOffset=0;}
void newDesk() {
    if(desks.size()>=MAX_DESKS){setStatus("MAXIMUM OF 12 WORKSPACES");return;}
    desks.push_back(Workspace{"Workspace "+std::to_string(desks.size()+1),{}});deskIndex=static_cast<int>(desks.size())-1;selectedX=selectedY=0;editing=true;saveNow();
    setStatus("NEW WORKSPACE CREATED");
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
        const u64 id=std::strtoull(t.data.c_str(),nullptr,16);
        if(id==0){setStatus("INVALID TITLE ID");return;}
#ifdef DESKNX_SYSTEM_APPLET
        const Result rc=sendIpc([&]{return desknx::ipc::SendLaunchTitle(id);});
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
    else setStatus(lastLaunched);
}
void keyInput(u64 keys) {
    if(deletePrompt) {
        if(keys&HidNpadButton_B){deletePrompt=false;return;}
        if(keys&HidNpadButton_A){deletePrompt=false;desks.erase(desks.begin()+deskIndex);deskIndex=std::min(deskIndex,static_cast<int>(desks.size())-1);selectedX=selectedY=0;saveNow();setStatus("WORKSPACE DELETED");}
        return;
    }
    if(listOpen) {
        const auto available=visibleCatalogueSnapshot();
        if(keys&HidNpadButton_B){listOpen=false;return;}
        if(keys&HidNpadButton_Y){searchCatalogue();return;}
        if(keys&HidNpadButton_X){cycleCatalogueSort();return;}
        if(keys&HidNpadButton_L){cycleCatalogueFilter(-1);return;}
        if(keys&HidNpadButton_R){cycleCatalogueFilter(1);return;}
        if(keys&HidNpadButton_Minus){catalogueQuery.clear();listIndex=0;listOffset=0;setStatus("SEARCH CLEARED",1800);return;}
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
    if(e.type==SDL_FINGERDOWN) {
        int x=static_cast<int>(e.tfinger.x*SW),y=static_cast<int>(e.tfinger.y*SH);if(listOpen||deletePrompt)return;
        if(y>=92&&y<=140){
            int idx=tabStart()+(x-28)/166;
            if(x>=28&&idx>=0&&idx<static_cast<int>(desks.size())&&idx<tabStart()+7&&idx!=deskIndex){
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
            return;
        }
        if(y>=628){if(x<210){editing=true;openList();}else if(x<385){editing=!editing;setStatus(editing?"EDIT MODE":"EDIT MODE CLOSED");}}
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
    if (layoutExists && !layoutLoaded) {
        backupBrokenLayout();
        if (backupExists && loadLayoutFrom(CFG_BAK_FILE)) {
            layoutLoaded = true;
            restoredFromBackup = true;
            setStatus("LAYOUT RESTORED FROM BACKUP", 5000);
            saveLayout();
        } else {
            defaultLayout();
        }
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
#endif
        padUpdate(&pad);const u64 keys=padGetButtonsDown(&pad);if(keys)keyInput(keys);
        SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT){appletRequestExitToSelf();running=false;}else touchInput(event);}
        drawScreen();svcSleepThread(16'000'000);
    }
#ifdef DESKNX_SYSTEM_APPLET
    stopHeartbeatThread();
#endif
    if(catalogueThread){SDL_WaitThread(catalogueThread,nullptr);catalogueThread=nullptr;}
    gIconCache.shutdown();gFontRenderer.shutdown();IMG_Quit();
    if(psmReady)psmExit();
    if(ipcMutex)SDL_DestroyMutex(ipcMutex);if(catalogueMutex)SDL_DestroyMutex(catalogueMutex);
    if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);
    SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");
    return 0;
}
