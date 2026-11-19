#include "SteamGridDbClient.hpp"
#include "../include/desknx/CurlGlobal.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace desknx::steamgriddb_api {
namespace {

constexpr std::size_t MaxJsonBytes=1024u*1024u;
constexpr std::size_t MaxImageBytes=8u*1024u*1024u;
constexpr const char* ApiRoot="https://www.steamgriddb.com/api/v2";

struct Response {
    std::vector<unsigned char> bytes;
    std::size_t limit{0};
    bool overflow{false};
};

std::size_t receive(char* data,std::size_t size,std::size_t count,void* opaque) {
    auto* response=static_cast<Response*>(opaque);
    if(size!=0&&count>std::numeric_limits<std::size_t>::max()/size) {
        response->overflow=true;
        return 0;
    }
    const std::size_t amount=size*count;
    if(amount>response->limit||response->bytes.size()>response->limit-amount) {
        response->overflow=true;
        return 0;
    }
    const auto* begin=reinterpret_cast<const unsigned char*>(data);
    response->bytes.insert(response->bytes.end(),begin,begin+amount);
    return amount;
}

bool performGet(const std::string& url,const std::string& apiKey,
                std::size_t limit,std::vector<unsigned char>& output,
                std::string& error) {
    if(url.compare(0,8,"https://")!=0) {
        error="Only HTTPS SteamGridDB URLs are accepted.";
        return false;
    }
    if(!desknx::network::acquireCurl()) {
        error="libcurl initialization failed.";
        return false;
    }
    CURL* curl=curl_easy_init();
    if(!curl) {
        desknx::network::releaseCurl();
        error="Could not create HTTP request.";
        return false;
    }
    Response response;
    response.limit=limit;
    curl_slist* headers=nullptr;
    headers=curl_slist_append(headers,"Accept: application/json, image/png, image/jpeg");
    if(!apiKey.empty()) {
        const std::string authorization="Authorization: Bearer "+apiKey;
        headers=curl_slist_append(headers,authorization.c_str());
    }
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"Transwitch/DeskNX SteamGridDB");
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,5000L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,15000L);
    curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
    const CURLcode result=curl_easy_perform(curl);
    long status=0;
    if(result==CURLE_OK)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    desknx::network::releaseCurl();

    if(result!=CURLE_OK) {
        error=response.overflow?"Response exceeded the safe download limit.":
            std::string("Network request failed: ")+curl_easy_strerror(result);
        return false;
    }
    if(status!=200) {
        if(status==401||status==403)error="SteamGridDB rejected the API key (HTTP "+std::to_string(status)+").";
        else error="SteamGridDB returned HTTP "+std::to_string(status)+".";
        return false;
    }
    if(response.overflow) {
        error="Response exceeded the safe download limit.";
        return false;
    }
    output.swap(response.bytes);
    return true;
}

void skipSpace(const std::string& text,std::size_t& pos) {
    while(pos<text.size()&&(text[pos]==' '||text[pos]=='\t'||
          text[pos]=='\r'||text[pos]=='\n'))++pos;
}

void appendUtf8(std::string& out,unsigned cp) {
    if(cp<=0x7F)out.push_back(static_cast<char>(cp));
    else if(cp<=0x7FF) {
        out.push_back(static_cast<char>(0xC0|(cp>>6)));
        out.push_back(static_cast<char>(0x80|(cp&0x3F)));
    } else if(cp<=0xFFFF) {
        out.push_back(static_cast<char>(0xE0|(cp>>12)));
        out.push_back(static_cast<char>(0x80|((cp>>6)&0x3F)));
        out.push_back(static_cast<char>(0x80|(cp&0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0|(cp>>18)));
        out.push_back(static_cast<char>(0x80|((cp>>12)&0x3F)));
        out.push_back(static_cast<char>(0x80|((cp>>6)&0x3F)));
        out.push_back(static_cast<char>(0x80|(cp&0x3F)));
    }
}

bool readJsonString(const std::string& text,std::size_t& pos,std::string& out) {
    if(pos>=text.size()||text[pos]!='"')return false;
    ++pos;out.clear();
    while(pos<text.size()) {
        const unsigned char ch=static_cast<unsigned char>(text[pos++]);
        if(ch=='"')return true;
        if(ch<0x20)return false;
        if(ch!='\\') {out.push_back(static_cast<char>(ch));continue;}
        if(pos>=text.size())return false;
        const char escaped=text[pos++];
        switch(escaped) {
            case '"':case '\\':case '/':out.push_back(escaped);break;
            case 'b':out.push_back('\b');break;
            case 'f':out.push_back('\f');break;
            case 'n':out.push_back('\n');break;
            case 'r':out.push_back('\r');break;
            case 't':out.push_back('\t');break;
            case 'u': {
                if(pos+4>text.size())return false;
                unsigned cp=0;
                for(int i=0;i<4;++i) {
                    const char h=text[pos++];
                    cp<<=4;
                    if(h>='0'&&h<='9')cp|=static_cast<unsigned>(h-'0');
                    else if(h>='a'&&h<='f')cp|=static_cast<unsigned>(h-'a'+10);
                    else if(h>='A'&&h<='F')cp|=static_cast<unsigned>(h-'A'+10);
                    else return false;
                }
                appendUtf8(out,cp);
                break;
            }
            default:return false;
        }
    }
    return false;
}

bool fieldString(const std::string& object,const char* key,std::string& out) {
    const std::string quoted=std::string("\"")+key+"\"";
    std::size_t pos=object.find(quoted);
    if(pos==std::string::npos)return false;
    pos=object.find(':',pos+quoted.size());
    if(pos==std::string::npos)return false;
    ++pos;skipSpace(object,pos);
    return readJsonString(object,pos,out);
}

bool fieldUnsigned(const std::string& object,const char* key,std::uint64_t& out) {
    const std::string quoted=std::string("\"")+key+"\"";
    std::size_t pos=object.find(quoted);
    if(pos==std::string::npos)return false;
    pos=object.find(':',pos+quoted.size());
    if(pos==std::string::npos)return false;
    ++pos;skipSpace(object,pos);
    const char* start=object.c_str()+pos;
    char* end=nullptr;
    const unsigned long long parsed=std::strtoull(start,&end,10);
    if(end==start||(*end!=','&&*end!='}'&&*end!=' '&&*end!='\t'&&*end!='\r'&&*end!='\n'))
        return false;
    out=static_cast<std::uint64_t>(parsed);
    return true;
}

std::vector<std::string> dataObjects(const std::string& json) {
    std::vector<std::string> objects;
    const std::size_t dataKey=json.find("\"data\"");
    if(dataKey==std::string::npos)return objects;
    const std::size_t open=json.find('[',dataKey);
    if(open==std::string::npos)return objects;
    std::size_t pos=open+1;
    while(pos<json.size()&&objects.size()<80) {
        while(pos<json.size()&&(json[pos]==' '||json[pos]=='\t'||
              json[pos]=='\r'||json[pos]=='\n'||json[pos]==','))++pos;
        if(pos>=json.size()||json[pos]==']')break;
        if(json[pos]!='{') {++pos;continue;}
        const std::size_t start=pos;
        int depth=0;bool quoted=false,escape=false;
        for(;pos<json.size();++pos) {
            const char ch=json[pos];
            if(quoted) {
                if(escape)escape=false;
                else if(ch=='\\')escape=true;
                else if(ch=='"')quoted=false;
                continue;
            }
            if(ch=='"')quoted=true;
            else if(ch=='{')++depth;
            else if(ch=='}'&&--depth==0) {
                ++pos;
                objects.emplace_back(json.substr(start,pos-start));
                break;
            }
        }
        if(depth!=0)break;
    }
    return objects;
}

std::string jsonString(const std::vector<unsigned char>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}

} // namespace

bool searchGames(const std::string& apiKey,const std::string& query,
                 std::vector<GameMatch>& results,std::string& error) {
    results.clear();error.clear();
    if(apiKey.empty()){error="Add a SteamGridDB API key in Launcher Settings first.";return false;}
    if(query.empty()){error="Enter a game name to search.";return false;}
    CURL* escapeCurl=curl_easy_init();
    if(!desknx::network::acquireCurl()) {
        error="libcurl initialization failed.";return false;
    }
    if(!escapeCurl) {
        desknx::network::releaseCurl();
        error="Could not prepare the search query.";return false;
    }
    char* escaped=curl_easy_escape(escapeCurl,query.c_str(),static_cast<int>(query.size()));
    const std::string encoded=escaped?escaped:"";
    if(escaped)curl_free(escaped);
    curl_easy_cleanup(escapeCurl);
    desknx::network::releaseCurl();
    if(encoded.empty()){error="Could not encode the search query.";return false;}

    std::vector<unsigned char> bytes;
    const std::string url=std::string(ApiRoot)+"/search/autocomplete/"+encoded;
    if(!performGet(url,apiKey,MaxJsonBytes,bytes,error))return false;
    const std::string json=jsonString(bytes);
    for(const auto& object:dataObjects(json)) {
        GameMatch match{};
        if(fieldUnsigned(object,"id",match.id)&&fieldString(object,"name",match.name)&&
           match.id!=0&&!match.name.empty())
            results.push_back(std::move(match));
        if(results.size()>=40)break;
    }
    if(results.empty())error="No SteamGridDB matches found. Try a shorter or English title.";
    return true;
}

bool listIcons(const std::string& apiKey,std::uint64_t gameId,
               std::vector<IconAsset>& results,std::string& error) {
    results.clear();error.clear();
    if(apiKey.empty()||gameId==0){error="SteamGridDB API key or game match is missing.";return false;}
    const std::string url=std::string(ApiRoot)+"/icons/game/"+
        std::to_string(gameId)+"?types=static&mimes=image%2Fpng%2Cimage%2Fjpeg&nsfw=false&humor=false";
    std::vector<unsigned char> bytes;
    if(!performGet(url,apiKey,MaxJsonBytes,bytes,error))return false;
    const std::string json=jsonString(bytes);
    for(const auto& object:dataObjects(json)) {
        IconAsset asset{};
        if(!fieldUnsigned(object,"id",asset.id)||asset.id==0||
           !fieldString(object,"url",asset.url))continue;
        (void)fieldString(object,"thumb",asset.thumb);
        std::uint64_t width=0,height=0;
        (void)fieldUnsigned(object,"width",width);
        (void)fieldUnsigned(object,"height",height);
        asset.width=static_cast<unsigned>(std::min<std::uint64_t>(width,8192));
        asset.height=static_cast<unsigned>(std::min<std::uint64_t>(height,8192));
        if(asset.url.compare(0,8,"https://")==0)
            results.push_back(std::move(asset));
        if(results.size()>=60)break;
    }
    if(results.empty())error="No icon artwork found for this SteamGridDB match.";
    return true;
}

bool downloadImage(const std::string& url,std::vector<unsigned char>& bytes,
                   std::string& error) {
    bytes.clear();error.clear();
    if(!performGet(url,"",MaxImageBytes,bytes,error))return false;
    if(!isSupportedImage(bytes)) {
        bytes.clear();
        error="Downloaded file is not a supported PNG or JPEG image.";
        return false;
    }
    return true;
}

bool isSupportedImage(const std::vector<unsigned char>& bytes) {
    static const unsigned char png[]={137,80,78,71,13,10,26,10};
    if(bytes.size()>=sizeof(png)&&std::equal(std::begin(png),std::end(png),bytes.begin()))
        return true;
    return bytes.size()>=4&&bytes[0]==0xFF&&bytes[1]==0xD8&&
           bytes[bytes.size()-2]==0xFF&&bytes.back()==0xD9;
}

} // namespace desknx::steamgriddb_api
