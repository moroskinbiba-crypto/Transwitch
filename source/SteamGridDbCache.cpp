#include "SteamGridDbCache.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <unordered_set>
#include <vector>
namespace desknx::steamgriddb {
namespace {
std::recursive_mutex gMetadataMutex;
struct Meta{
 std::uint64_t titleId{0},gameId{0},heroId{0},gridId{0},logoId{0},iconId{0},lastAccess{0};
 bool hero{false},grid{false},logo{false},icon{false};
 std::string active,gameName;
};
void space(const std::string& s,size_t& p){while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'))++p;}
bool locate(const std::string& s,const char* key,size_t& p){
 std::string t=std::string("\"")+key+"\"";p=s.find(t);if(p==std::string::npos)return false;
 p=s.find(':',p+t.size());if(p==std::string::npos)return false;++p;space(s,p);return p<s.size();
}
std::uint64_t number(const std::string& s,const char* k){
 size_t p=0;if(!locate(s,k,p)||s[p]<'0'||s[p]>'9')return 0;char* e=nullptr;
 auto v=std::strtoull(s.c_str()+p,&e,10);return e==s.c_str()+p?0:static_cast<std::uint64_t>(v);
}
bool boolean(const std::string& s,const char* k){
 size_t p=0;if(!locate(s,k,p))return false;return s.compare(p,4,"true")==0;
}
std::string str(const std::string& s,const char* k){
 size_t p=0;if(!locate(s,k,p)||s[p++]!='"')return {};std::string o;
 while(p<s.size()){char c=s[p++];if(c=='"')break;if(c=='\\'&&p<s.size()){char e=s[p++];if(e=='"'||e=='\\'||e=='/')o.push_back(e);else o.push_back(' ');}
  else if(static_cast<unsigned char>(c)>=32)o.push_back(c);if(o.size()>512)break;}return o;
}
std::string escape(const std::string& s){std::string o;for(unsigned char c:s){if(c=='"'||c=='\\'){o.push_back('\\');o.push_back(static_cast<char>(c));}
 else if(c>=32&&c!='\r'&&c!='\n'&&c!='\t')o.push_back(static_cast<char>(c));if(o.size()>=512)break;}return o;}
bool readMeta(const std::string& path,Meta& m){
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 struct stat st{};
 if(stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode)||
    st.st_size<=0||st.st_size>16384)return false;
 std::ifstream f(path,std::ios::binary);if(!f)return false;
 std::string s(static_cast<std::size_t>(st.st_size),'\\0');
 f.read(&s[0],static_cast<std::streamsize>(s.size()));
 if(!f||f.gcount()!=static_cast<std::streamsize>(s.size()))return false;
 m.titleId=number(s,"titleId");m.gameId=number(s,"gameId");m.heroId=number(s,"heroId");m.gridId=number(s,"gridId");
 m.logoId=number(s,"logoId");m.iconId=number(s,"iconId");m.lastAccess=number(s,"lastAccess");
 m.hero=boolean(s,"hero");m.grid=boolean(s,"grid");m.logo=boolean(s,"logo");m.icon=boolean(s,"icon");
 m.active=str(s,"active");m.gameName=str(s,"gameName");return true;
}
bool writeMeta(const std::string& path,const Meta& m){
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 const std::string tmp=path+".tmp";std::ofstream f(tmp,std::ios::binary|std::ios::trunc);if(!f)return false;
 f<<"{\n  \"titleId\": "<<m.titleId<<",\n  \"gameId\": "<<m.gameId
  <<",\n  \"gameName\": \""<<escape(m.gameName)<<"\",\n  \"active\": \""<<m.active<<"\",\n"
  <<"  \"hero\": "<<(m.hero?"true":"false")<<",\n  \"grid\": "<<(m.grid?"true":"false")
  <<",\n  \"logo\": "<<(m.logo?"true":"false")<<",\n  \"icon\": "<<(m.icon?"true":"false")<<",\n"
  <<"  \"heroId\": "<<m.heroId<<",\n  \"gridId\": "<<m.gridId<<",\n  \"logoId\": "<<m.logoId
  <<",\n  \"iconId\": "<<m.iconId<<",\n  \"lastAccess\": "<<m.lastAccess<<"\n}\n";
 f.flush();bool ok=f.good();f.close();if(!ok){std::remove(tmp.c_str());return false;}
 if(std::rename(tmp.c_str(),path.c_str())!=0){std::remove(tmp.c_str());return false;}return true;
}
bool copyToTemp(const std::string& source,const std::string& dest){
 struct stat st{};
 if(stat(source.c_str(),&st)!=0||!S_ISREG(st.st_mode)||
    st.st_size<=0||st.st_size>16LL*1024LL*1024LL)return false;
 std::ifstream in(source,std::ios::binary);if(!in)return false;
 std::ofstream out(dest,std::ios::binary|std::ios::trunc);if(!out)return false;
 char buf[64*1024];while(in){in.read(buf,sizeof(buf));auto n=in.gcount();if(n>0)out.write(buf,n);if(!out.good())break;}
 bool ok=!in.bad()&&out.good();out.close();if(!ok)std::remove(dest.c_str());return ok&&out.good();
}
bool regular(const std::string& p){struct stat s{};return stat(p.c_str(),&s)==0&&S_ISREG(s.st_mode)&&s.st_size>0;}
std::uint64_t bytesIn(const std::filesystem::path& root){
 std::uint64_t n=0;std::error_code ec;
 for(std::filesystem::recursive_directory_iterator it(root,ec),end;!ec&&it!=end;it.increment(ec)){
  const std::string name=it->path().filename().string();
  // Preview/download staging files are transient and excluded from the persistent cache budget.
  if(!name.empty()&&name[0]=='.')continue;
  std::error_code e;if(it->is_regular_file(e)&&!e){auto s=it->file_size(e);if(!e&&s<=UINT64_MAX-n)n+=s;}
 }return n;
}
bool titleFolder(const std::string& s,std::uint64_t& id){
 if(s.size()!=16)return false;id=0;for(unsigned char c:s){unsigned d;
  if(c>='0'&&c<='9')d=c-'0';else if(c>='a'&&c<='f')d=c-'a'+10;else if(c>='A'&&c<='F')d=c-'A'+10;else return false;
  id=(id<<4)|d;}return id!=0;
}
std::uint64_t stampOf(const std::filesystem::path& p){
 std::error_code ec;auto t=std::filesystem::last_write_time(p,ec);if(ec)return 0;
 auto sys=std::chrono::time_point_cast<std::chrono::seconds>(t-decltype(t)::clock::now()+std::chrono::system_clock::now());
 auto v=std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();return v>0?static_cast<std::uint64_t>(v):0;
}
}
const char* SteamGridDbCache::kindKey(ArtworkKind k){switch(k){case ArtworkKind::Hero:return "hero";case ArtworkKind::Grid:return "grid";case ArtworkKind::Logo:return "logo";case ArtworkKind::Icon:return "icon";}return "grid";}
std::string SteamGridDbCache::titleDirectory(std::uint64_t id){char b[17]{};std::snprintf(b,sizeof(b),"%016llX",static_cast<unsigned long long>(id));return std::string(kCacheRoot)+"/"+b;}
std::string SteamGridDbCache::heroPath(std::uint64_t id){return titleDirectory(id)+"/hero.img";}
std::string SteamGridDbCache::gridPath(std::uint64_t id){return titleDirectory(id)+"/grid.img";}
std::string SteamGridDbCache::logoPath(std::uint64_t id){return titleDirectory(id)+"/logo.img";}
std::string SteamGridDbCache::iconPath(std::uint64_t id){return titleDirectory(id)+"/icon.img";}
std::string SteamGridDbCache::metadataPath(std::uint64_t id){return titleDirectory(id)+"/metadata.json";}
std::string SteamGridDbCache::pathFor(std::uint64_t id,ArtworkKind k){switch(k){case ArtworkKind::Hero:return heroPath(id);case ArtworkKind::Grid:return gridPath(id);case ArtworkKind::Logo:return logoPath(id);case ArtworkKind::Icon:return iconPath(id);}return gridPath(id);}
bool SteamGridDbCache::ensureDirectories(){std::error_code ec;std::filesystem::create_directories(kCacheRoot,ec);return !ec;}
bool SteamGridDbCache::hasArtwork(std::uint64_t id,ArtworkKind k)const{return regular(pathFor(id,k));}
bool SteamGridDbCache::hasHero(std::uint64_t id)const{return hasArtwork(id,ArtworkKind::Hero);}
bool SteamGridDbCache::hasGrid(std::uint64_t id)const{return hasArtwork(id,ArtworkKind::Grid);}
bool SteamGridDbCache::hasLogo(std::uint64_t id)const{return hasArtwork(id,ArtworkKind::Logo);}
bool SteamGridDbCache::hasIcon(std::uint64_t id)const{return hasArtwork(id,ArtworkKind::Icon);}
SteamGridDbCache::ArtworkKind SteamGridDbCache::selectedKind(std::uint64_t id)const{
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 Meta m;readMeta(metadataPath(id),m);
 if(m.active=="hero"&&hasHero(id))return ArtworkKind::Hero;if(m.active=="grid"&&hasGrid(id))return ArtworkKind::Grid;
 if(m.active=="logo"&&hasLogo(id))return ArtworkKind::Logo;if(m.active=="icon"&&hasIcon(id))return ArtworkKind::Icon;
 if(hasGrid(id))return ArtworkKind::Grid;if(hasHero(id))return ArtworkKind::Hero;if(hasIcon(id))return ArtworkKind::Icon;return ArtworkKind::Logo;
}
std::string SteamGridDbCache::selectedPath(std::uint64_t id)const{
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 if(!id)return {};
 Meta metadata;
 const bool haveMetadata=readMeta(metadataPath(id),metadata);
 // "system" is an explicit choice: cached alternatives stay on disk, but the
 // launcher returns to the original Nintendo/system icon until another type is activated.
 if(haveMetadata&&metadata.active=="system")return {};
 const auto p=pathFor(id,selectedKind(id));
 if(!regular(p))return {};
 // This lookup is called by the icon-decoder worker while cache maintenance
 // and downloads may run on another worker. Keep it read-only: mutating the
 // metadata file from a render-triggered lookup races atomic cache commits.
 return p;
}
std::uint64_t SteamGridDbCache::mappedGameId(std::uint64_t id)const{
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);Meta m;return readMeta(metadataPath(id),m)?m.gameId:0;}
bool SteamGridDbCache::saveGameMapping(std::uint64_t id,std::uint64_t gameId,const std::string& name){
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 if(!id||!gameId||!ensureDirectories())return false;std::error_code ec;std::filesystem::create_directories(titleDirectory(id),ec);if(ec)return false;
 Meta m;readMeta(metadataPath(id),m);m.titleId=id;m.gameId=gameId;m.gameName=name;m.lastAccess=static_cast<std::uint64_t>(std::time(nullptr));return writeMeta(metadataPath(id),m);
}
bool SteamGridDbCache::activateCachedArtwork(std::uint64_t id,ArtworkKind kind){
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 if(!id||!hasArtwork(id,kind))return false;
 Meta m;readMeta(metadataPath(id),m);m.titleId=id;m.active=kindKey(kind);
 m.lastAccess=static_cast<std::uint64_t>(std::time(nullptr));
 return writeMeta(metadataPath(id),m);
}
bool SteamGridDbCache::saveArtworkFromFile(std::uint64_t id,ArtworkKind kind,const std::string& source,
 std::uint64_t gameId,std::uint64_t assetId,const std::string& name,bool makeActive){
 if(!id||source.empty()||!ensureDirectories())return false;std::error_code ec;std::filesystem::create_directories(titleDirectory(id),ec);if(ec)return false;
 const std::string dest=pathFor(id,kind),tmp=dest+".tmp";if(!copyToTemp(source,tmp))return false;
 if(std::rename(tmp.c_str(),dest.c_str())!=0){std::remove(tmp.c_str());return false;}
 Meta m;readMeta(metadataPath(id),m);m.titleId=id;if(gameId)m.gameId=gameId;if(!name.empty())m.gameName=name;
 if(kind==ArtworkKind::Hero){m.hero=true;m.heroId=assetId;}else if(kind==ArtworkKind::Grid){m.grid=true;m.gridId=assetId;}
 else if(kind==ArtworkKind::Logo){m.logo=true;m.logoId=assetId;}else{m.icon=true;m.iconId=assetId;}
 if(makeActive)m.active=kindKey(kind);m.lastAccess=static_cast<std::uint64_t>(std::time(nullptr));
 return writeMeta(metadataPath(id),m);
}
bool SteamGridDbCache::clearArtwork(std::uint64_t id,ArtworkKind kind){
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 if(!id)return false;Meta m;readMeta(metadataPath(id),m);bool removed=std::remove(pathFor(id,kind).c_str())==0;
 if(kind==ArtworkKind::Hero){m.hero=false;m.heroId=0;}else if(kind==ArtworkKind::Grid){m.grid=false;m.gridId=0;}
 else if(kind==ArtworkKind::Logo){m.logo=false;m.logoId=0;}else{m.icon=false;m.iconId=0;}
 if(m.active==kindKey(kind)){m.active.clear();if(m.grid)m.active="grid";else if(m.hero)m.active="hero";else if(m.icon)m.active="icon";else if(m.logo)m.active="logo";}
 if(m.hero||m.grid||m.logo||m.icon)writeMeta(metadataPath(id),m);else std::remove(metadataPath(id).c_str());return removed;
}
bool SteamGridDbCache::restoreSystemArtwork(std::uint64_t id){
 std::lock_guard<std::recursive_mutex> guard(gMetadataMutex);
 if(!id||!ensureDirectories())return false;
 std::error_code ec;
 std::filesystem::create_directories(titleDirectory(id),ec);
 if(ec)return false;
 Meta metadata;
 readMeta(metadataPath(id),metadata);
 metadata.titleId=id;
 metadata.active="system";
 metadata.lastAccess=static_cast<std::uint64_t>(std::time(nullptr));
 return writeMeta(metadataPath(id),metadata);
}
std::uint64_t SteamGridDbCache::cacheBytes()const{std::error_code ec;if(!std::filesystem::exists(kCacheRoot,ec)||ec)return 0;return bytesIn(kCacheRoot);}
std::uint64_t SteamGridDbCache::prune(std::uint64_t max,const std::vector<std::uint64_t>& installed){
 if(!ensureDirectories())return cacheBytes();std::unordered_set<std::uint64_t> keep(installed.begin(),installed.end());
 struct Entry{std::filesystem::path path;std::uint64_t bytes,stamp;};std::vector<Entry> entries;std::error_code ec;
 for(std::filesystem::directory_iterator it(kCacheRoot,ec),end;!ec&&it!=end;it.increment(ec)){
  std::error_code e;if(!it->is_directory(e)||e)continue;std::uint64_t id=0;if(!titleFolder(it->path().filename().string(),id))continue;
  const auto size=bytesIn(it->path());if(keep.find(id)==keep.end()){std::error_code rm;std::filesystem::remove_all(it->path(),rm);continue;}
  Meta metadata;
  const std::uint64_t lastUsed=readMeta(metadataPath(id),metadata)&&metadata.lastAccess?
      metadata.lastAccess:stampOf(it->path());
  entries.push_back({it->path(),size,lastUsed});
 }
 auto total=cacheBytes();if(total<=max)return total;
 std::sort(entries.begin(),entries.end(),[](const Entry&a,const Entry&b){return a.stamp<b.stamp;});
 for(const auto& e:entries){if(total<=max)break;std::error_code rm;std::filesystem::remove_all(e.path,rm);if(!rm)total-=std::min(total,e.bytes);}
 return cacheBytes();
}
} // namespace desknx::steamgriddb
