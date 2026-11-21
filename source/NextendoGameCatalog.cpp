#include <desknx/NextendoGameCatalog.hpp>

#include <switch.h>
#include <curl/curl.h>
#include <sodium.h>
#include "NetworkRuntime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

namespace desknx::nextendo {
namespace {

constexpr std::size_t MaxEnvelopeBytes = 512u * 1024u;
constexpr std::size_t MaxGames = 4096u;
constexpr std::size_t MaxTitleIds = 8192u;
constexpr std::size_t MaxAcceptedVersions = 64u;
constexpr std::size_t MaxStringBytes = 4096u;
constexpr const char* CatalogUrl =
    "https://nextendo.network/service-catalog/v1/game-catalog";

struct JsonValue {
    enum class Type { Null, Boolean, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolean{false};
    std::string scalar;
    std::vector<JsonValue> array;
    std::map<std::string,JsonValue> object;

    const JsonValue* get(const char* key) const {
        if(type!=Type::Object)return nullptr;
        const auto it=object.find(key);
        return it==object.end()?nullptr:&it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input):input_(input){}

    bool parse(JsonValue& value) {
        position_=0;
        nodes_=0;
        if(input_.size()>MaxEnvelopeBytes)return false;
        skipSpace();
        if(!parseValue(value,0))return false;
        skipSpace();
        return position_==input_.size();
    }

private:
    static bool isDigit(char c){return c>='0'&&c<='9';}
    void skipSpace(){
        while(position_<input_.size()&&
              (input_[position_]==' '||input_[position_]=='\t'||
               input_[position_]=='\r'||input_[position_]=='\n'))++position_;
    }
    bool consume(char c){
        if(position_>=input_.size()||input_[position_]!=c)return false;
        ++position_;return true;
    }
    static void appendUtf8(std::string& out,unsigned value){
        if(value<=0x7Fu)out.push_back(static_cast<char>(value));
        else if(value<=0x7FFu){
            out.push_back(static_cast<char>(0xC0u|(value>>6)));
            out.push_back(static_cast<char>(0x80u|(value&0x3Fu)));
        } else if(value<=0xFFFFu){
            out.push_back(static_cast<char>(0xE0u|(value>>12)));
            out.push_back(static_cast<char>(0x80u|((value>>6)&0x3Fu)));
            out.push_back(static_cast<char>(0x80u|(value&0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u|(value>>18)));
            out.push_back(static_cast<char>(0x80u|((value>>12)&0x3Fu)));
            out.push_back(static_cast<char>(0x80u|((value>>6)&0x3Fu)));
            out.push_back(static_cast<char>(0x80u|(value&0x3Fu)));
        }
    }
    bool hex4(unsigned& value){
        value=0;
        for(int i=0;i<4;++i){
            if(position_>=input_.size())return false;
            const char c=input_[position_++];
            unsigned digit=0;
            if(c>='0'&&c<='9')digit=static_cast<unsigned>(c-'0');
            else if(c>='a'&&c<='f')digit=static_cast<unsigned>(c-'a'+10);
            else if(c>='A'&&c<='F')digit=static_cast<unsigned>(c-'A'+10);
            else return false;
            value=(value<<4)|digit;
        }
        return true;
    }
    bool parseString(std::string& out){
        if(!consume('"'))return false;
        out.clear();
        while(position_<input_.size()){
            const unsigned char c=static_cast<unsigned char>(input_[position_++]);
            if(c=='"')return out.size()<=MaxStringBytes;
            if(c<0x20)return false;
            if(c!='\\'){
                out.push_back(static_cast<char>(c));
                if(out.size()>MaxStringBytes)return false;
                continue;
            }
            if(position_>=input_.size())return false;
            const char escaped=input_[position_++];
            switch(escaped){
                case '"':out.push_back('"');break;
                case '\\':out.push_back('\\');break;
                case '/':out.push_back('/');break;
                case 'b':out.push_back('\b');break;
                case 'f':out.push_back('\f');break;
                case 'n':out.push_back('\n');break;
                case 'r':out.push_back('\r');break;
                case 't':out.push_back('\t');break;
                case 'u':{
                    unsigned code=0;
                    if(!hex4(code))return false;
                    if(code>=0xD800u&&code<=0xDBFFu){
                        if(position_+2>input_.size()||
                           input_[position_]!='\\'||input_[position_+1]!='u')return false;
                        position_+=2;
                        unsigned low=0;
                        if(!hex4(low)||low<0xDC00u||low>0xDFFFu)return false;
                        code=0x10000u+((code-0xD800u)<<10)+(low-0xDC00u);
                    }else if(code>=0xDC00u&&code<=0xDFFFu)return false;
                    appendUtf8(out,code);
                    break;
                }
                default:return false;
            }
            if(out.size()>MaxStringBytes)return false;
        }
        return false;
    }
    bool parseNumber(JsonValue& out){
        const std::size_t start=position_;
        consume('-');
        if(position_>=input_.size())return false;
        if(input_[position_]=='0')++position_;
        else {
            if(!isDigit(input_[position_]))return false;
            while(position_<input_.size()&&isDigit(input_[position_]))++position_;
        }
        if(consume('.')){
            if(position_>=input_.size()||!isDigit(input_[position_]))return false;
            while(position_<input_.size()&&isDigit(input_[position_]))++position_;
        }
        if(position_<input_.size()&&(input_[position_]=='e'||input_[position_]=='E')){
            ++position_;
            if(position_<input_.size()&&(input_[position_]=='+'||input_[position_]=='-'))++position_;
            if(position_>=input_.size()||!isDigit(input_[position_]))return false;
            while(position_<input_.size()&&isDigit(input_[position_]))++position_;
        }
        out.type=JsonValue::Type::Number;
        out.scalar=input_.substr(start,position_-start);
        return !out.scalar.empty()&&out.scalar.size()<=64;
    }
    bool parseValue(JsonValue& out,unsigned depth){
        if(depth>32||++nodes_>50000u)return false;
        skipSpace();
        if(position_>=input_.size())return false;
        const char c=input_[position_];
        if(c=='"'){
            out.type=JsonValue::Type::String;
            return parseString(out.scalar);
        }
        if(c=='{'){
            ++position_;out.type=JsonValue::Type::Object;skipSpace();
            if(consume('}'))return true;
            for(std::size_t count=0;count<10000u;++count){
                skipSpace();std::string key;
                if(!parseString(key))return false;
                skipSpace();if(!consume(':'))return false;
                JsonValue child;
                if(!parseValue(child,depth+1))return false;
                if(!out.object.emplace(std::move(key),std::move(child)).second)return false;
                skipSpace();
                if(consume('}'))return true;
                if(!consume(','))return false;
            }
            return false;
        }
        if(c=='['){
            ++position_;out.type=JsonValue::Type::Array;skipSpace();
            if(consume(']'))return true;
            for(std::size_t count=0;count<MaxTitleIds+MaxGames+MaxAcceptedVersions;++count){
                JsonValue child;
                if(!parseValue(child,depth+1))return false;
                out.array.push_back(std::move(child));
                skipSpace();
                if(consume(']'))return true;
                if(!consume(','))return false;
            }
            return false;
        }
        if(input_.compare(position_,4,"true")==0){
            position_+=4;out.type=JsonValue::Type::Boolean;out.boolean=true;return true;
        }
        if(input_.compare(position_,5,"false")==0){
            position_+=5;out.type=JsonValue::Type::Boolean;out.boolean=false;return true;
        }
        if(input_.compare(position_,4,"null")==0){
            position_+=4;out.type=JsonValue::Type::Null;return true;
        }
        return parseNumber(out);
    }

    const std::string& input_;
    std::size_t position_{0};
    std::size_t nodes_{0};
};

bool stringValue(const JsonValue* value,std::string& out){
    if(!value||value->type!=JsonValue::Type::String)return false;
    out=value->scalar;return true;
}
bool boolValue(const JsonValue* value,bool& out){
    if(!value||value->type!=JsonValue::Type::Boolean)return false;
    out=value->boolean;return true;
}
bool unsignedValue(const JsonValue* value,std::uint64_t& out){
    if(!value||value->type!=JsonValue::Type::Number||value->scalar.empty()||
       value->scalar.size()>20||value->scalar[0]=='-')return false;
    if(value->scalar.find_first_not_of("0123456789")!=std::string::npos)return false;
    char* end=nullptr;
    const unsigned long long parsed=std::strtoull(value->scalar.c_str(),&end,10);
    if(!end||*end!='\0')return false;
    out=static_cast<std::uint64_t>(parsed);return true;
}
bool base64Decode(const std::string& text,std::vector<unsigned char>& output){
    static const std::string alphabet=
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(text.empty()||text.size()>MaxEnvelopeBytes||text.size()%4!=0)return false;
    output.clear();output.reserve(text.size()/4*3);
    for(std::size_t i=0;i<text.size();i+=4){
        unsigned value=0;unsigned padding=0;
        for(unsigned j=0;j<4;++j){
            const char c=text[i+j];
            value<<=6;
            if(c=='='){
                ++padding;
                if(j<2||i+4!=text.size())return false;
            }else{
                if(padding)return false;
                const auto pos=alphabet.find(c);
                if(pos==std::string::npos)return false;
                value|=static_cast<unsigned>(pos);
            }
        }
        if(padding>2)return false;
        output.push_back(static_cast<unsigned char>((value>>16)&0xFFu));
        if(padding<2)output.push_back(static_cast<unsigned char>((value>>8)&0xFFu));
        if(padding<1)output.push_back(static_cast<unsigned char>(value&0xFFu));
        if(padding==1&&(value&0xFFu)!=0)return false;
        if(padding==2&&(value&0xFFFFu)!=0)return false;
    }
    return true;
}
bool parseTitleId(const std::string& text,std::uint64_t& id){
    if(text.size()!=16)return false;
    id=0;
    for(unsigned char c:text){
        unsigned digit=0;
        if(c>='0'&&c<='9')digit=c-'0';
        else if(c>='a'&&c<='f')digit=c-'a'+10;
        else if(c>='A'&&c<='F')digit=c-'A'+10;
        else return false;
        id=(id<<4)|digit;
    }
    return id!=0;
}

bool daysFromCivil(int year,unsigned month,unsigned day,std::int64_t& days){
    if(year<1970||year>9999||month<1||month>12||day<1||day>31)return false;
    year-=month<=2;
    const int era=(year>=0?year:year-399)/400;
    const unsigned yoe=static_cast<unsigned>(year-era*400);
    const unsigned doy=(153u*(month+(month>2?-3u:9u))+2u)/5u+day-1u;
    const unsigned doe=yoe*365u+yoe/4u-yoe/100u+doy;
    days=static_cast<std::int64_t>(era)*146097+static_cast<std::int64_t>(doe)-719468;
    return true;
}
bool parseTimestamp(const std::string& text,std::int64_t& result){
    if(text.size()<20||text[4]!='-'||text[7]!='-'||text[10]!='T'||
       text[13]!=':'||text[16]!=':')return false;
    auto digits=[&](std::size_t at,std::size_t count,int& value){
        value=0;
        if(at+count>text.size())return false;
        for(std::size_t i=at;i<at+count;++i){
            if(text[i]<'0'||text[i]>'9')return false;
            value=value*10+(text[i]-'0');
        }
        return true;
    };
    int year,month,day,hour,minute,second;
    if(!digits(0,4,year)||!digits(5,2,month)||!digits(8,2,day)||
       !digits(11,2,hour)||!digits(14,2,minute)||!digits(17,2,second)||
       hour>23||minute>59||second>59)return false;
    std::size_t at=19;
    if(at<text.size()&&text[at]=='.'){
        ++at;const std::size_t start=at;
        while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;
        if(at==start)return false;
    }
    int offsetSeconds=0;
    if(at<text.size()&&text[at]=='Z'){
        ++at;
    }else if(at+6==text.size()&&(text[at]=='+'||text[at]=='-')&&text[at+3]==':'){
        int oh,om;
        if(!digits(at+1,2,oh)||!digits(at+4,2,om)||oh>23||om>59)return false;
        offsetSeconds=(oh*60+om)*60;
        if(text[at]=='-')offsetSeconds=-offsetSeconds;
        at+=6;
    }else return false;
    if(at!=text.size())return false;
    std::int64_t days=0;
    if(!daysFromCivil(year,static_cast<unsigned>(month),static_cast<unsigned>(day),days))
        return false;
    // Validate the day of month after the month/year conversion.
    static constexpr unsigned monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned maximum=monthDays[month-1];
    const bool leap=(year%4==0&&year%100!=0)||year%400==0;
    if(month==2&&leap)maximum=29;
    if(static_cast<unsigned>(day)>maximum)return false;
    result=days*86400+hour*3600+minute*60+second-offsetSeconds;
    return result>=0;
}

bool parseAcceptedVersions(const JsonValue* value,
                           std::vector<std::string>& versions){
    if(!value||value->type!=JsonValue::Type::Array||
       value->array.size()>MaxAcceptedVersions)return false;
    versions.clear();
    for(const auto& entry:value->array){
        if(entry.type!=JsonValue::Type::String||entry.scalar.empty()||
           entry.scalar.size()>32)return false;
        versions.push_back(entry.scalar);
    }
    return true;
}

struct Response {
    std::string body;
    bool overflow{false};
};
size_t receive(char* data,size_t size,size_t count,void* opaque){
    auto* response=static_cast<Response*>(opaque);
    const size_t bytes=size*count;
    if(bytes>MaxEnvelopeBytes||response->body.size()>MaxEnvelopeBytes-bytes){
        response->overflow=true;return 0;
    }
    response->body.append(data,bytes);return bytes;
}
struct CancelInfo {
    CatalogCancelCheck check{nullptr};
    void* context{nullptr};
};
int transferProgress(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t){
    const auto* cancel=static_cast<const CancelInfo*>(opaque);
    return cancel&&cancel->check&&cancel->check(cancel->context)?1:0;
}

} // namespace

bool sameGameVersion(const std::string& left,const std::string& right){
    if(left==right)return true;
    auto parse=[](const std::string& value,std::vector<std::uint64_t>& parts){
        if(value.empty()||value.size()>64)return false;
        std::size_t start=0;
        while(start<=value.size()){
            const std::size_t end=value.find('.',start);
            const std::string token=value.substr(start,
                end==std::string::npos?std::string::npos:end-start);
            if(token.empty()||token.size()>10||
               token.find_first_not_of("0123456789")!=std::string::npos)return false;
            char* tail=nullptr;
            const unsigned long long number=std::strtoull(token.c_str(),&tail,10);
            if(!tail||*tail!='\0')return false;
            parts.push_back(static_cast<std::uint64_t>(number));
            if(parts.size()>12)return false;
            if(end==std::string::npos)break;
            start=end+1;
        }
        return true;
    };
    std::vector<std::uint64_t> a,b;
    if(!parse(left,a)||!parse(right,b))return false;
    const std::size_t size=std::max(a.size(),b.size());
    a.resize(size,0);b.resize(size,0);
    return a==b;
}

VersionVerdict GameCatalog::check(std::uint64_t titleId,
                                 const std::string& installedVersion) const {
    const auto found=games.find(titleId);
    if(found==games.end())return {CompatibilityState::NotListed,{}};
    const GameVersions& versions=found->second;
    if(!versions.enabled)return {CompatibilityState::Disabled,{}};
    if(installedVersion.empty()||versions.policy.empty())
        return {CompatibilityState::Unknown,{}};
    if(versions.policy=="any")
        return {CompatibilityState::Accepted,{}};
    if(versions.policy!="exact"||versions.accepted.empty())
        return {CompatibilityState::Unknown,{}};
    for(const auto& accepted:versions.accepted)
        if(sameGameVersion(installedVersion,accepted))
            return {CompatibilityState::Accepted,{}};
    return {CompatibilityState::UpdateRequired,versions.recommended};
}

bool parseSignedGameCatalog(const std::string& envelope,std::int64_t nowUnix,
                            GameCatalog& output,std::string& error){
    output=GameCatalog{};
    error.clear();
    if(envelope.empty()||envelope.size()>MaxEnvelopeBytes){
        error="Nextendo catalogue response is empty or too large";return false;
    }
    try{
        JsonValue outer;
        if(!JsonParser(envelope).parse(outer)||outer.type!=JsonValue::Type::Object){
            error="Invalid Nextendo signed catalogue envelope";return false;
        }
        std::string keyId,payload64,signature64;
        if(!stringValue(outer.get("key_id"),keyId)||
           !stringValue(outer.get("payload"),payload64)||
           !stringValue(outer.get("signature"),signature64)||
           keyId!=GameCatalogKeyId){
            error="Unknown or incomplete Nextendo catalogue envelope";return false;
        }
        std::vector<unsigned char> publicKey,payload,signature;
        if(!base64Decode(GameCatalogPublicKey,publicKey)||
           !base64Decode(payload64,payload)||
           !base64Decode(signature64,signature)||
           publicKey.size()!=crypto_sign_PUBLICKEYBYTES||
           signature.size()!=crypto_sign_BYTES||payload.empty()||
           payload.size()>MaxEnvelopeBytes){
            error="Invalid Nextendo catalogue signature encoding";return false;
        }
        if(sodium_init()<0||
           crypto_sign_verify_detached(signature.data(),payload.data(),
               static_cast<unsigned long long>(payload.size()),publicKey.data())!=0){
            error="Nextendo catalogue signature verification failed";return false;
        }
        const std::string payloadText(payload.begin(),payload.end());
        JsonValue document;
        if(!JsonParser(payloadText).parse(document)||document.type!=JsonValue::Type::Object){
            error="Invalid signed Nextendo catalogue payload";return false;
        }
        std::string documentType,expiresText;
        std::uint64_t revision=0;
        std::int64_t expires=0;
        const JsonValue* gamesValue=document.get("games");
        if(!stringValue(document.get("document_type"),documentType)||
           documentType!="game_catalog"||
           !unsignedValue(document.get("revision"),revision)||revision==0||
           !stringValue(document.get("expires_at"),expiresText)||
           !parseTimestamp(expiresText,expires)||expires<=nowUnix||
           !gamesValue||gamesValue->type!=JsonValue::Type::Array||
           gamesValue->array.size()>MaxGames){
            error="Nextendo catalogue is expired or has an unsupported schema";return false;
        }
        GameCatalog parsed;
        parsed.revision=revision;
        parsed.expiresAt=expires;
        std::size_t titleCount=0;
        for(const auto& game:gamesValue->array){
            if(game.type!=JsonValue::Type::Object){
                error="Invalid Nextendo game entry";return false;
            }
            GameVersions versions;
            if(!boolValue(game.get("enabled"),versions.enabled)){
                error="Nextendo game entry has no enabled state";return false;
            }
            if(const JsonValue* versionValue=game.get("versions")){
                if(versionValue->type!=JsonValue::Type::Object||
                   !stringValue(versionValue->get("policy"),versions.policy)||
                   (versions.policy!="exact"&&versions.policy!="any")){
                    error="Nextendo game entry has an unsupported version policy";return false;
                }
                if(const JsonValue* accepted=versionValue->get("accepted")){
                    if(!parseAcceptedVersions(accepted,versions.accepted)){
                        error="Invalid accepted version list in Nextendo catalogue";return false;
                    }
                }
                if(versions.policy=="exact"&&versions.accepted.empty()){
                    error="Exact Nextendo version policy has no accepted versions";return false;
                }
                if(!stringValue(versionValue->get("recommended"),versions.recommended)){
                    versions.recommended=versions.accepted.empty()?std::string{}:
                        versions.accepted.front();
                }
                if(versions.recommended.size()>32){
                    error="Recommended Nextendo version is too long";return false;
                }
            }
            const JsonValue* ids=game.get("title_ids");
            if(!ids||ids->type!=JsonValue::Type::Array||ids->array.empty()||
               ids->array.size()>MaxTitleIds-titleCount){
                error="Invalid title ID list in Nextendo catalogue";return false;
            }
            for(const auto& idValue:ids->array){
                std::string text;
                std::uint64_t id=0;
                if(!stringValue(&idValue,text)||!parseTitleId(text,id)||
                   !parsed.games.emplace(id,versions).second){
                    error="Invalid or duplicate title ID in Nextendo catalogue";return false;
                }
                ++titleCount;
            }
        }
        output=std::move(parsed);
        return true;
    }catch(...){
        error="Unexpected error while parsing Nextendo catalogue";
        output=GameCatalog{};
        return false;
    }
}

bool fetchGameCatalog(const std::string& clientId,std::string& envelope,
                      std::string& error,CatalogCancelCheck shouldCancel,
                      void* cancelContext){
    envelope.clear();error.clear();
    if(clientId.empty()||clientId.size()>128||
       clientId.find_first_not_of(
           "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos){
        error="A valid Nextendo public Client ID is required";return false;
    }
    if(shouldCancel&&shouldCancel(cancelContext)){
        error="Request cancelled";return false;
    }
    CURL* curl=curl_easy_init();
    if(!curl){error="Nextendo: HTTP client unavailable";return false;}
    curl_slist* headers=nullptr;
    const std::string clientHeader="X-Nextendo-Client-Id: "+clientId;
    headers=curl_slist_append(headers,"Accept: application/json");
    headers=curl_slist_append(headers,"User-Agent: Transwitch/DeskNX");
    headers=curl_slist_append(headers,clientHeader.c_str());
    Response response;
    CancelInfo cancellation{shouldCancel,cancelContext};
    curl_easy_setopt(curl,CURLOPT_URL,CatalogUrl);
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,8000L);
    curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
    if(shouldCancel){
        curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
        curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,transferProgress);
        curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&cancellation);
    }
    const CURLcode result=curl_easy_perform(curl);
    long status=0;
    if(result==CURLE_OK)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if(shouldCancel&&shouldCancel(cancelContext)){
        error="Request cancelled";return false;
    }
    if(response.overflow){error="Nextendo catalogue response is too large";return false;}
    if(result!=CURLE_OK){
        error=std::string("Nextendo: ")+curl_easy_strerror(result);return false;
    }
    if(status!=200){
        error="Nextendo catalogue HTTP "+std::to_string(status);return false;
    }
    envelope.swap(response.body);
    return true;
}

} // namespace desknx::nextendo
