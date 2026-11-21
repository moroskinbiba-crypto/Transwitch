#include <desknx/SteamGridDbClient.hpp>
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace desknx::steamgriddb {
namespace {
constexpr std::size_t MaxApiBytes=2u*1024u*1024u,MaxImageBytes=16u*1024u*1024u;
constexpr const char* Api="https://www.steamgriddb.com/api/v2";
struct Body{std::string value;bool overflow{false};};
size_t receive(char* p,size_t s,size_t n,void* u){
 auto* b=static_cast<Body*>(u);if(!b||(n&&s>static_cast<std::size_t>(-1)/n))return 0;
 const size_t z=s*n;if(z>MaxApiBytes||b->value.size()>MaxApiBytes-z){b->overflow=true;return 0;}
 b->value.append(p,z);return z;
}
struct FileSink{FILE* f{nullptr};size_t bytes{0};bool overflow{false};};
size_t receiveFile(char* p,size_t s,size_t n,void* u){
 auto* b=static_cast<FileSink*>(u);if(!b||!b->f||(n&&s>static_cast<std::size_t>(-1)/n))return 0;
 const size_t z=s*n;if(z>MaxImageBytes||b->bytes>MaxImageBytes-z){b->overflow=true;return 0;}
 const size_t w=std::fwrite(p,1,z,b->f);b->bytes+=w;return w;
}
std::string encode(const std::string& in){
 static const char h[]="0123456789ABCDEF";std::string out;
 for(unsigned char c:in){
  if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out.push_back(c);
  else{out.push_back('%');out.push_back(h[c>>4]);out.push_back(h[c&15]);}
 }return out;
}
bool request(const std::string& key,const std::string& url,std::string& body,std::string& error){
 CURL* c=curl_easy_init();if(!c){error="SteamGridDB: HTTP client unavailable";return false;}
 Body b;curl_slist* headers=nullptr;const std::string auth="Authorization: Bearer "+key;
 headers=curl_slist_append(headers,auth.c_str());headers=curl_slist_append(headers,"Accept: application/json");
 curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
 curl_easy_setopt(c,CURLOPT_USERAGENT,"Transwitch/DeskNX");curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
 curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,6000L);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,15000L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
 curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
 const CURLcode rc=curl_easy_perform(c);long status=0;if(rc==CURLE_OK)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
 curl_slist_free_all(headers);curl_easy_cleanup(c);
 if(rc!=CURLE_OK||status!=200||b.overflow){
  if(status==401||status==403)error="SteamGridDB: API key rejected";
  else if(b.overflow)error="SteamGridDB: response too large";
  else if(rc!=CURLE_OK)error=std::string("SteamGridDB: ")+curl_easy_strerror(rc);
  else error="SteamGridDB: HTTP "+std::to_string(status);return false;
 }
 body.swap(b.value);return true;
}
void utf8(std::string& o,unsigned cp){
 if(cp<=0x7f)o.push_back(static_cast<char>(cp));
 else if(cp<=0x7ff){o.push_back(static_cast<char>(0xc0|(cp>>6)));o.push_back(static_cast<char>(0x80|(cp&63)));}
 else{o.push_back(static_cast<char>(0xe0|(cp>>12)));o.push_back(static_cast<char>(0x80|((cp>>6)&63)));o.push_back(static_cast<char>(0x80|(cp&63)));}
}
bool readString(const std::string& s,size_t& p,std::string& out){
 if(p>=s.size()||s[p++]!='"')return false;out.clear();
 while(p<s.size()){unsigned char c=static_cast<unsigned char>(s[p++]);if(c=='"')return true;if(c<32)return false;
  if(c!='\\'){out.push_back(static_cast<char>(c));continue;}if(p>=s.size())return false;char e=s[p++];
  switch(e){case '"':case '\\':case '/':out.push_back(e);break;case 'b':out.push_back('\b');break;
   case 'f':out.push_back('\f');break;case 'n':out.push_back(' ');break;case 'r':case 't':out.push_back(' ');break;
   case 'u':{if(p+4>s.size())return false;unsigned cp=0;for(int i=0;i<4;i++){char d=s[p++];cp<<=4;
    if(d>='0'&&d<='9')cp|=d-'0';else if(d>='a'&&d<='f')cp|=d-'a'+10;else if(d>='A'&&d<='F')cp|=d-'A'+10;else return false;}utf8(out,cp);break;}
   default:return false;
  }
 }return false;
}
bool locate(const std::string& s,const char* key,size_t& p){
 const std::string t=std::string("\"")+key+"\"";p=s.find(t);if(p==std::string::npos)return false;
 p=s.find(':',p+t.size());if(p==std::string::npos)return false;++p;
 while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'))++p;return p<s.size();
}
bool stringField(const std::string& s,const char* key,std::string& out){size_t p=0;return locate(s,key,p)&&readString(s,p,out);}
std::uint64_t uintField(const std::string& s,const char* key){
 size_t p=0;if(!locate(s,key,p)||s[p]<'0'||s[p]>'9')return 0;char* e=nullptr;
 auto v=std::strtoull(s.c_str()+p,&e,10);return e==s.c_str()+p?0:static_cast<std::uint64_t>(v);
}
int intField(const std::string& s,const char* k){auto v=uintField(s,k);return v>1000000?0:static_cast<int>(v);}
bool success(const std::string& s){
 size_t p=0;if(!locate(s,"success",p))return false;return s.compare(p,4,"true")==0;
}
bool dataObjects(const std::string& s,std::vector<std::string>& out,size_t max){
 out.clear();size_t p=0;if(!locate(s,"data",p)||s[p++]!='[')return false;
 while(p<s.size()&&out.size()<max){
  while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'||s[p]==','))++p;
  if(p>=s.size()||s[p]==']')break;if(s[p]!='{')return false;size_t start=p;int depth=0;bool quote=false,escape=false;
  for(;p<s.size();++p){char c=s[p];if(quote){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quote=false;continue;}
   if(c=='"')quote=true;else if(c=='{')++depth;else if(c=='}'&&--depth==0){++p;out.push_back(s.substr(start,p-start));break;}}
  if(depth!=0)return false;
 }return true;
}
std::string normalize(const std::string& s){
 std::string out;bool space=true;for(unsigned char c:s){if(std::isalnum(c)){out.push_back(static_cast<char>(std::tolower(c)));space=false;}
 else if(!space){out.push_back(' ');space=true;}}if(!out.empty()&&out.back()==' ')out.pop_back();return out;
}
bool safeUrl(const std::string& u){
 if(u.compare(0,8,"https://")!=0)return false;size_t a=8,b=u.find('/',a);
 std::string host=u.substr(a,b==std::string::npos?std::string::npos:b-a),suffix=".steamgriddb.com";
 return host=="steamgriddb.com"||(host.size()>suffix.size()&&host.compare(host.size()-suffix.size(),suffix.size(),suffix)==0);
}
bool supported(const std::string& mime,const std::string& url){
 std::string m=mime;for(char& c:m)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
 if(m=="image/png"||m=="image/jpeg"||m=="image/jpg")return true;if(!m.empty())return false;
 std::string u=url;size_t q=u.find('?');if(q!=std::string::npos)u.resize(q);
 for(char& c:u)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
 return u.size()>=4&&(u.compare(u.size()-4,4,".png")==0||u.compare(u.size()-4,4,".jpg")==0||
  (u.size()>=5&&u.compare(u.size()-5,5,".jpeg")==0));
}
} // namespace
const char* artworkKindName(ArtworkKind k){switch(k){case ArtworkKind::Hero:return "Hero";case ArtworkKind::Grid:return "Grid";case ArtworkKind::Logo:return "Logo";case ArtworkKind::Icon:return "Icon";}return "Grid";}
float titleMatchScore(const std::string& q,const std::string& c){
 const std::string a=normalize(q),b=normalize(c);if(a.empty()||b.empty())return 0.0f;if(a==b)return 1.0f;
 if(a.size()>=b.size()&&a.compare(0,b.size(),b)==0)return 0.94f;
 if(b.size()>=a.size()&&b.compare(0,a.size(),a)==0)return 0.92f;
 size_t n=std::max(a.size(),b.size());if(n>128)return 0.0f;
 std::vector<unsigned> prev(b.size()+1),cur(b.size()+1);for(size_t j=0;j<=b.size();j++)prev[j]=static_cast<unsigned>(j);
 for(size_t i=1;i<=a.size();i++){cur[0]=static_cast<unsigned>(i);for(size_t j=1;j<=b.size();j++)
  cur[j]=std::min({prev[j]+1,cur[j-1]+1,prev[j-1]+(a[i-1]==b[j-1]?0u:1u)});prev.swap(cur);}
 return std::max(0.0f,1.0f-static_cast<float>(prev[b.size()])/static_cast<float>(n));
}
bool searchGames(const std::string& key,const std::string& title,std::vector<GameResult>& out,std::string& error){
 out.clear();if(key.empty()||title.empty()){error="Enter an API key and game title";return false;}
 std::string body;if(!request(key,std::string(Api)+"/search/autocomplete/"+encode(title),body,error))return false;
 if(!success(body)){error="SteamGridDB search response invalid";return false;}
 std::vector<std::string> objects;if(!dataObjects(body,objects,24)){error="Cannot parse SteamGridDB game list";return false;}
 for(const auto& o:objects){GameResult g;g.id=uintField(o,"id");if(g.id&&stringField(o,"name",g.name)){g.matchScore=titleMatchScore(title,g.name);out.push_back(std::move(g));}}
 std::stable_sort(out.begin(),out.end(),[](const GameResult& a,const GameResult& b){return a.matchScore>b.matchScore;});return true;
}
bool fetchAssets(const std::string& key,std::uint64_t gameId,ArtworkKind kind,std::vector<ArtworkAsset>& out,std::string& error){
 out.clear();if(key.empty()||gameId==0){error="Missing API key or SteamGridDB game mapping";return false;}
 const char* ep=kind==ArtworkKind::Hero?"heroes":kind==ArtworkKind::Logo?"logos":kind==ArtworkKind::Icon?"icons":"grids";
 std::string body;if(!request(key,std::string(Api)+"/"+ep+"/game/"+std::to_string(gameId)+"?types=static",body,error))return false;
 if(!success(body)){error="SteamGridDB artwork response invalid";return false;}
 std::vector<std::string> objects;if(!dataObjects(body,objects,96)){error="Cannot parse SteamGridDB artwork list";return false;}
 for(const auto& o:objects){ArtworkAsset a;a.id=uintField(o,"id");stringField(o,"url",a.url);stringField(o,"thumb",a.thumb);
  stringField(o,"mime",a.mime);a.width=intField(o,"width");a.height=intField(o,"height");a.score=intField(o,"score");
  if(!a.id||!safeUrl(a.url)||!supported(a.mime,a.url))continue;if(!safeUrl(a.thumb))a.thumb=a.url;out.push_back(std::move(a));}
 return true;
}
bool downloadImage(const std::string& url,const std::string& destination,std::string& error){
 if(!safeUrl(url)||destination.empty()){error="Unsupported SteamGridDB image URL";return false;}
 FILE* f=std::fopen(destination.c_str(),"wb");if(!f){error="Cannot create temporary image";return false;}
 CURL* c=curl_easy_init();if(!c){std::fclose(f);std::remove(destination.c_str());error="HTTP client unavailable";return false;}
 FileSink sink;sink.f=f;curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_USERAGENT,"Transwitch/DeskNX");
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,6000L);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,30000L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receiveFile);curl_easy_setopt(c,CURLOPT_WRITEDATA,&sink);
 CURLcode rc=curl_easy_perform(c);long status=0;if(rc==CURLE_OK)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);curl_easy_cleanup(c);
 bool closed=std::fclose(f)==0;if(rc!=CURLE_OK||status!=200||sink.overflow||sink.bytes==0||!closed){
  std::remove(destination.c_str());if(sink.overflow)error="Image exceeds 16 MiB";
  else if(rc!=CURLE_OK)error=std::string("Download failed: ")+curl_easy_strerror(rc);else error="Image server returned HTTP "+std::to_string(status);return false;
 }return true;
}
} // namespace desknx::steamgriddb
