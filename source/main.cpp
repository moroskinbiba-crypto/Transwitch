#include <switch.h>
#include <SDL2/SDL.h>
#include <desknx/Protocol.hpp>

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
constexpr int SW = 1280, SH = 720, COLS = 6, ROWS = 3;
constexpr int CW = 154, CH = 112, GX = 14, GY = 14, LEFT = 141, TOP = 170;
constexpr int MAX_ITEMS = 600, MAX_DESKS = 12;
constexpr const char* CFG_DIR = "sdmc:/switch/DeskNX";
constexpr const char* CFG_FILE = "sdmc:/switch/DeskNX/layout.tsv";
constexpr const char* CFG_TMP = "sdmc:/switch/DeskNX/layout.tmp";

struct Color { Uint8 r, g, b, a; };
constexpr Color BG{14,17,25,255}, PANEL{24,29,40,255}, EDGE{55,66,83,255};
constexpr Color WHITE{242,246,252,255}, MUTED{147,160,181,255}, BLUE{92,166,255,255};
constexpr Color GREEN{97,214,174,255}, RED{246,105,119,255};
constexpr std::array<Color,8> COLORS{{
    {78,117,188,255},{45,139,123,255},{151,91,185,255},{187,116,66,255},
    {177,77,99,255},{67,135,177,255},{115,128,74,255},{89,99,119,255}
}};

enum class Kind { Game, Nro, Clock, Battery, System, Recent };
struct Tile { Kind kind{Kind::Game}; std::string title, data; int x{0}, y{0}, w{1}, h{1}, color{0}; };
struct Workspace { std::string name; std::vector<Tile> tiles; };
struct Candidate { Kind kind; std::string title, data; int w, h, color; };

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
std::vector<Workspace> desks;
std::vector<Candidate> catalogue;
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
    if(w<=0||h<=0) return; color(c); SDL_Rect r{x,y,w,h}; SDL_RenderFillRect(renderer,&r);
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
std::string typeCode(Kind k) {
    switch(k) { case Kind::Game:return "G"; case Kind::Nro:return "N"; case Kind::Clock:return "C";
        case Kind::Battery:return "B"; case Kind::System:return "S"; case Kind::Recent:return "R"; }
    return "G";
}
Kind parseKind(const std::string& s) {
    if(s=="N")return Kind::Nro; if(s=="C")return Kind::Clock; if(s=="B")return Kind::Battery;
    if(s=="S")return Kind::System; if(s=="R")return Kind::Recent; return Kind::Game;
}
std::string typeName(Kind k) {
    switch(k) { case Kind::Game:return "GAME"; case Kind::Nro:return "HOMEBREW"; case Kind::Clock:return "WIDGET / CLOCK";
        case Kind::Battery:return "WIDGET / BATTERY"; case Kind::System:return "WIDGET / SYSTEM"; case Kind::Recent:return "WIDGET / RECENT"; }
    return "ITEM";
}
std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out; std::stringstream ss(line); std::string part;
    while(std::getline(ss,part,'\t')) out.push_back(part); return out;
}
std::string safeField(std::string s) {
    for(char& c:s) if(c=='\t'||c=='\n'||c=='\r') c=' '; return s;
}
std::string fileTitle(const std::string& p) {
    size_t s=p.find_last_of('/'); std::string n=s==std::string::npos?p:p.substr(s+1);
    size_t d=n.find_last_of('.'); if(d!=std::string::npos)n.resize(d);
    for(char& c:n) if(c=='_'||c=='-')c=' '; return n;
}
bool isWidget(Kind k) { return k==Kind::Clock||k==Kind::Battery||k==Kind::System||k==Kind::Recent; }
bool overlap(const Tile& a,const Tile& b) {
    return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
}
bool placeable(const Workspace& d,const Tile& t,int x,int y,int ignore=-1) {
    if(x<0||y<0||t.w<1||t.h<1||x+t.w>COLS||y+t.h>ROWS)return false;
    Tile p=t;p.x=x;p.y=y;
    for(size_t i=0;i<d.tiles.size();++i)if(static_cast<int>(i)!=ignore&&overlap(p,d.tiles[i]))return false;
    return true;
}
int itemAt(const Workspace& d,int x,int y) {
    for(size_t i=0;i<d.tiles.size();++i) { const Tile&t=d.tiles[i];
        if(x>=t.x&&x<t.x+t.w&&y>=t.y&&y<t.y+t.h)return static_cast<int>(i); }
    return -1;
}
bool firstFree(const Workspace& d,const Tile& t,int& x,int& y) {
    for(int yy=0;yy<ROWS;++yy)for(int xx=0;xx<COLS;++xx)if(placeable(d,t,xx,yy)){x=xx;y=yy;return true;}
    return false;
}
void addCandidate(Kind k,const std::string& title,const std::string& data,int w=1,int h=1,int c=0) {
    if(catalogue.size()<MAX_ITEMS) catalogue.push_back(Candidate{k,title,data,w,h,c%static_cast<int>(COLORS.size())});
}
void scanNros(const std::string& path,int depth,std::vector<Candidate>& found) {
    if(depth>3||found.size()>=MAX_ITEMS)return;
    DIR* d=opendir(path.c_str());if(!d)return;dirent* e=nullptr;
    while((e=readdir(d))&&found.size()<MAX_ITEMS) {
        std::string n=e->d_name;if(n=="."||n==".."||(!n.empty()&&n[0]=='.'))continue;
        std::string p=path+"/"+n;struct stat st{};if(stat(p.c_str(),&st)!=0)continue;
        if(S_ISDIR(st.st_mode)) scanNros(p,depth+1,found);
        else if(S_ISREG(st.st_mode)&&n.size()>4) {
            std::string ext=n.substr(n.size()-4);for(char& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(ext==".nro"&&p.find("/DeskNX.nro")==std::string::npos)
                found.push_back(Candidate{Kind::Nro,fileTitle(p),p,1,1,static_cast<int>(found.size()%COLORS.size())});
        }
    }
    closedir(d);
}
void scanApps() {
    catalogue.clear();
    if(R_SUCCEEDED(nsInitialize())) {
        std::array<NsApplicationRecord,256> records{};s32 count=0;
        if(R_SUCCEEDED(nsListApplicationRecord(records.data(),static_cast<s32>(records.size()),0,&count))) {
            const int n=std::max(0,std::min(count,static_cast<s32>(records.size())));
            static NsApplicationControlData ctl{};
            for(int i=0;i<n;++i) {
                const u64 id=records[static_cast<size_t>(i)].application_id;u64 actual=0;
                if(R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage,id,&ctl,sizeof(ctl),&actual)))continue;
                NacpLanguageEntry* entry=nullptr;std::string title;
                if(R_SUCCEEDED(nacpGetLanguageEntry(&ctl.nacp,&entry))&&entry&&entry->name[0]) title=entry->name;
                else { char b[32]{};std::snprintf(b,sizeof(b),"Game %08X",static_cast<unsigned int>(id&0xffffffffu));title=b; }
                char hex[32]{};std::snprintf(hex,sizeof(hex),"%016llX",static_cast<unsigned long long>(id));
                addCandidate(Kind::Game,title,hex,1,1,i);
            }
        }
        nsExit();
    }
    std::vector<Candidate> homebrew;scanNros("sdmc:/switch",0,homebrew);
    std::sort(homebrew.begin(),homebrew.end(),[](const Candidate&a,const Candidate&b){return ascii(a.title)<ascii(b.title);});
    for(const auto& c:homebrew)addCandidate(c.kind,c.title,c.data,c.w,c.h,c.color);
    addCandidate(Kind::Clock,"Clock widget","",2,1,1);
    addCandidate(Kind::Battery,"Battery widget","",2,1,2);
    addCandidate(Kind::System,"System info widget","",2,1,5);
    addCandidate(Kind::Recent,"Recent launch widget","",3,1,6);
}
void saveLayout() {
    if(!sdMounted)return;
    mkdir("sdmc:/switch",0777);mkdir(CFG_DIR,0777);
    std::ofstream f(CFG_TMP,std::ios::binary|std::ios::trunc);
    if(!f){setStatus("SAVE FAILED: SD CARD NOT WRITABLE",3500);return;}
    f<<"DESKNX1\n";
    for(const auto& d:desks)f<<"D\t"<<safeField(d.name)<<"\n";
    for(size_t di=0;di<desks.size();++di)for(const auto&t:desks[di].tiles)
        f<<"I\t"<<di<<'\t'<<typeCode(t.kind)<<'\t'<<safeField(t.title)<<'\t'<<safeField(t.data)<<'\t'
         <<t.x<<'\t'<<t.y<<'\t'<<t.w<<'\t'<<t.h<<'\t'<<t.color<<"\n";
    f.flush();bool good=f.good();f.close();if(!good){setStatus("SAVE FAILED: WRITE ERROR",3500);return;}
    std::remove(CFG_FILE);
    if(std::rename(CFG_TMP,CFG_FILE)!=0){setStatus("SAVE FAILED: RENAME ERROR",3500);return;}
    setStatus("LAYOUT SAVED",1600);
}
bool loadLayout() {
    std::ifstream f(CFG_FILE,std::ios::binary);if(!f)return false;
    std::string line;if(!std::getline(f,line)||line!="DESKNX1")return false;
    std::vector<Workspace> loaded;std::vector<std::vector<Tile>> items;
    while(std::getline(f,line)) {
        auto p=splitTabs(line);if(p.empty())continue;
        if(p[0]=="D"&&p.size()>=2&&loaded.size()<MAX_DESKS) {loaded.push_back(Workspace{p[1],{}});items.emplace_back();}
        else if(p[0]=="I"&&p.size()>=10) {
            int d=std::atoi(p[1].c_str());if(d<0||static_cast<size_t>(d)>=items.size())continue;
            Tile t{};t.kind=parseKind(p[2]);t.title=p[3];t.data=p[4];t.x=std::atoi(p[5].c_str());t.y=std::atoi(p[6].c_str());
            t.w=std::atoi(p[7].c_str());t.h=std::atoi(p[8].c_str());t.color=std::atoi(p[9].c_str())%static_cast<int>(COLORS.size());
            if(t.color<0)t.color=0;items[static_cast<size_t>(d)].push_back(t);
        }
    }
    if(loaded.empty())return false;
    for(size_t d=0;d<loaded.size();++d)for(const Tile&t:items[d])if(placeable(loaded[d],t,t.x,t.y))loaded[d].tiles.push_back(t);
    desks=std::move(loaded);deskIndex=0;selectedX=selectedY=0;return true;
}
void defaultLayout() {
    desks={{"Games",{}},{"Homebrew",{}},{"Widgets",{}}};int gi=0,hi=0;
    for(const auto& c:catalogue) {
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
    Color c=COLORS[static_cast<size_t>(t.color)%COLORS.size()];rect(x,y,size,size,c);outline(x,y,size,size,Color{255,255,255,55});
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
    } else if(t.kind==Kind::Recent) {
        text("RECENT LAUNCH",r.x+18,r.y+14,2,MUTED,r.x+r.w-10);text(lastLaunched,r.x+18,r.y+43,2,WHITE,r.x+r.w-10);
        text("EMPTY CELLS ARE KEPT",r.x+18,r.y+r.h-23,1,accent,r.x+r.w-10);
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
    refreshBattery();rect(0,0,SW,SH,BG);
    for(int y=0;y<SH;y+=28)rect(0,y,SW,1,Color{255,255,255,static_cast<Uint8>(y%3==0?7:3)});
    rect(0,0,SW,76,Color{19,23,33,255});rect(0,74,SW,2,Color{43,53,69,255});
    text("DESKNX",30,21,4,WHITE,260);text("PERSONAL HOME WORKSPACE",34,50,1,MUTED,420);
    text(clockText(),1124,20,3,WHITE,1260);text(battery>=0?std::to_string(battery)+"%":"BAT --",1130,50,1,MUTED,1260);
    int start=visibleStart(),tabW=156,x=28;
    for(int i=0;i<7&&start+i<static_cast<int>(desks.size());++i) {
        int idx=start+i;bool active=idx==deskIndex;rect(x,94,tabW,42,active?Color{42,61,86,255}:Color{24,29,40,255});
        outline(x,94,tabW,42,active?BLUE:EDGE);text(desks[static_cast<size_t>(idx)].name,x+12,109,1,active?WHITE:MUTED,x+tabW-8);
        if(active)rect(x+10,132,tabW-20,2,BLUE);x+=166;
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
        text("ADD TO DESKTOP",mx+28,my+22,3,WHITE,mx+mw-20);text("A ADD  /  B CANCEL",mx+28,my+59,1,MUTED,mx+mw-20);
        int vis=10;if(listIndex<listOffset)listOffset=listIndex;if(listIndex>=listOffset+vis)listOffset=listIndex-vis+1;
        for(int row=0;row<vis;++row){int i=listOffset+row;if(i>=static_cast<int>(catalogue.size()))break;int yy=my+92+row*43;
            if(i==listIndex)rect(mx+22,yy,mw-44,38,Color{45,61,82,255});
            text(catalogue[static_cast<size_t>(i)].title,mx+34,yy+8,2,i==listIndex?WHITE:MUTED,mx+575);
            text(typeName(catalogue[static_cast<size_t>(i)].kind),mx+595,yy+12,1,i==listIndex?BLUE:MUTED,mx+mw-18);
        }
        text(std::to_string(catalogue.size())+" AVAILABLE ITEMS",mx+28,my+mh-26,1,MUTED,mx+mw-18);
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
    if(desks.size()<2)return false;Workspace& src=desks[static_cast<size_t>(deskIndex)];int i=itemAt(src,selectedX,selectedY);if(i<0)return false;
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
    if(catalogue.empty()){listOpen=false;setStatus("NO APPS FOUND");return;}
    Workspace& d=desks[static_cast<size_t>(deskIndex)];if(itemAt(d,selectedX,selectedY)>=0){listOpen=false;setStatus("CHOOSE AN EMPTY CELL FIRST");return;}
    const Candidate& c=catalogue[static_cast<size_t>(listIndex)];Tile t{c.kind,c.title,c.data,selectedX,selectedY,c.w,c.h,c.color};
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
    char result[64]{};Result rc=swkbdShow(&kbd,result,sizeof(result));swkbdClose(&kbd);
    if(R_SUCCEEDED(rc)&&result[0]){desks[static_cast<size_t>(deskIndex)].name=result;saveNow();setStatus("WORKSPACE RENAMED");}
}
void deleteDesk() {
    if(desks.size()<=1){setStatus("AT LEAST ONE WORKSPACE IS REQUIRED");return;}
    deletePrompt=true;
}
void launch(const Tile& t) {
    lastLaunched=t.title;
    if(t.kind==Kind::Game) {
        const u64 id=std::strtoull(t.data.c_str(),nullptr,16);
        if(id==0){setStatus("INVALID TITLE ID");return;}
#ifdef DESKNX_SYSTEM_APPLET
        const Result rc=desknx::ipc::SendLaunchTitle(id);
        if(R_FAILED(rc)) {
            char b[72]{};
            std::snprintf(b,sizeof(b),"SYSTEM BACKEND REQUEST FAILED: 0X%08X",static_cast<unsigned int>(rc));
            setStatus(b,4000);
            return;
        }
        saveLayout();
        appletRequestExitToSelf();
        running=false;
#else
        if(R_FAILED(nsInitialize())){setStatus("NS SERVICE UNAVAILABLE IN THIS CONTEXT",4000);return;}
        const Result rc=appletRequestLaunchApplication(id,nullptr);
        nsExit();
        if(R_FAILED(rc)){char b[64]{};std::snprintf(b,sizeof(b),"GAME LAUNCH FAILED: 0X%08X",static_cast<unsigned int>(rc));setStatus(b,4000);}
        else setStatus("LAUNCH REQUEST SENT");
#endif
        return;
    }
    if(t.kind==Kind::Nro) {
        if(!envHasNextLoad()){setStatus("NRO CHAIN-LOAD UNAVAILABLE; USE HBMENU TAKEOVER",4500);return;}
        if(R_FAILED(envSetNextLoad(t.data.c_str(),""))){setStatus("CANNOT PREPARE NEXT NRO");return;}
        appletRequestExitToSelf();return;
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
        if(keys&HidNpadButton_B){listOpen=false;return;}
        if((keys&HidNpadButton_Up)&&!catalogue.empty())listIndex=(listIndex-1+static_cast<int>(catalogue.size()))%static_cast<int>(catalogue.size());
        if((keys&HidNpadButton_Down)&&!catalogue.empty())listIndex=(listIndex+1)%static_cast<int>(catalogue.size());
        if((keys&HidNpadButton_A)&&!catalogue.empty())addSelected();return;
    }
    if(keys&HidNpadButton_B) {
        if(dragging){dragging=false;saveNow();setStatus("POSITION SAVED");return;}
#ifdef DESKNX_SYSTEM_APPLET
        setStatus("DESKNX IS THE HOME MENU; SELECT AN APP TO RETURN TO IT",3000);return;
#else
        appletRequestExitToSelf();running=false;return;
#endif
    }
    if(keys&HidNpadButton_Plus)newDesk();
    if(keys&HidNpadButton_StickL)renameDesk();
    if(keys&HidNpadButton_StickR&&editing)deleteDesk();
    if(keys&HidNpadButton_X){editing=!editing;if(!editing&&dragging){dragging=false;saveNow();}setStatus(editing?"EDIT MODE - EMPTY CELLS STAY EMPTY":"EDIT MODE CLOSED");}
    if(keys&HidNpadButton_Y){editing=true;openList();}
    if(keys&HidNpadButton_L) {if(dragging)moveAcrossDesk(-1);else deskIndex=(deskIndex-1+static_cast<int>(desks.size()))%static_cast<int>(desks.size());}
    else if(keys&HidNpadButton_R) {if(dragging)moveAcrossDesk(1);else deskIndex=(deskIndex+1)%static_cast<int>(desks.size());}
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
        if(y>=92&&y<=140){int idx=tabStart()+(x-28)/166;if(x>=28&&idx>=0&&idx<static_cast<int>(desks.size())&&idx<tabStart()+7)deskIndex=idx;return;}
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
    scanApps();
    if(!loadLayout()){defaultLayout();saveLayout();}
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){if(sdMounted)fsdevUnmountDevice("sdmc");return 1;}
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    window=SDL_CreateWindow("DeskNX",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SW,SH,SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(!window){SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 2;}
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){SDL_DestroyWindow(window);SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 3;}
    SDL_RenderSetLogicalSize(renderer,SW,SH);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    PadState pad{};
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    // Tell the system backend the menu is alive. It uses this handshake to
    // distinguish a visible menu from an applet that crashed during startup.
#ifdef DESKNX_SYSTEM_APPLET
    const Result readyRc=desknx::ipc::SendMenuReady();
    if(R_FAILED(readyRc)) setStatus("SYSTEM BACKEND NOT CONNECTED",5000);
    else setStatus("DESKNX READY  -  Y ADD  X EDIT  + NEW DESK",4000);
#else
    setStatus("Y ADD  X EDIT  ZL/ZR RESIZE  - REMOVE  + NEW DESK",5000);
#endif
    while(running&&appletMainLoop()) {
        padUpdate(&pad);
        u64 keys=padGetButtonsDown(&pad);
        if(keys)keyInput(keys);
        SDL_Event e;while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){appletRequestExitToSelf();running=false;}
            else touchInput(e);
        }
        drawScreen();svcSleepThread(16'000'000);
    }
    if(psmReady)psmExit();if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);
    SDL_Quit();if(sdMounted)fsdevUnmountDevice("sdmc");return 0;
}
