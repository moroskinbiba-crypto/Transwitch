#include "catalog_view.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace desknx::catalog_view {
namespace {

std::string searchKey(const std::string& value) {
    std::string key;
    key.reserve(value.size());
    for (size_t i=0; i<value.size();) {
        const unsigned char c=static_cast<unsigned char>(value[i]);
        if (c<0x80) {
            key.push_back(static_cast<char>(std::tolower(c)));
            ++i;
            continue;
        }
        uint32_t cp=0;
        size_t count=1;
        if ((c&0xE0)==0xC0 && i+1<value.size()) {
            cp=((c&0x1F)<<6)|(static_cast<unsigned char>(value[i+1])&0x3F);count=2;
        } else if ((c&0xF0)==0xE0 && i+2<value.size()) {
            cp=((c&0x0F)<<12)|((static_cast<unsigned char>(value[i+1])&0x3F)<<6)|
                (static_cast<unsigned char>(value[i+2])&0x3F);count=3;
        } else if ((c&0xF8)==0xF0 && i+3<value.size()) {
            key.append(value,i,4);i+=4;continue;
        } else {
            key.push_back(static_cast<char>(c));++i;continue;
        }
        i+=count;
        if (cp==0x401 || cp==0x451) cp=0x415;
        if (cp>=0x430 && cp<=0x44F) cp-=0x20;
        if (cp>=0x410 && cp<=0x42F) {
            static const char* const map[]={
                "a","b","v","g","d","e","zh","z","i","i","k","l","m","n","o","p",
                "r","s","t","u","f","h","ts","ch","sh","sch","","y","","e","yu","ya"
            };
            const size_t index=static_cast<size_t>(cp-0x410);
            if (index<sizeof(map)/sizeof(map[0]) && map[index][0]) key+=map[index];
            continue;
        }
        if(count==2) {
            key.push_back(static_cast<char>(0xC0|(cp>>6)));
            key.push_back(static_cast<char>(0x80|(cp&0x3F)));
        } else {
            key.push_back(static_cast<char>(0xE0|(cp>>12)));
            key.push_back(static_cast<char>(0x80|((cp>>6)&0x3F)));
            key.push_back(static_cast<char>(0x80|(cp&0x3F)));
        }
    }
    return key;
}

bool isWidget(model::Kind kind) {
    return kind==model::Kind::Clock || kind==model::Kind::Battery ||
           kind==model::Kind::System || kind==model::Kind::Recent ||
           kind==model::Kind::Storage || kind==model::Kind::Calendar;
}
bool matchesFilter(model::Kind kind, FilterMode filter) {
    switch(filter) {
        case FilterMode::All: return true;
        case FilterMode::Games: return kind==model::Kind::Game;
        case FilterMode::Homebrew: return kind==model::Kind::Nro;
        case FilterMode::Widgets: return isWidget(kind);
    }
    return true;
}
uint64_t fileSize(const model::Candidate& item) {
    if(item.kind!=model::Kind::Nro) return 0;
    struct stat st{};
    if(stat(item.data.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0) return 0;
    return static_cast<uint64_t>(st.st_size);
}
size_t recentRank(const model::Candidate& item,const std::vector<RecentKey>& recent) {
    for(size_t i=0;i<recent.size();++i)
        if(recent[i].kind==item.kind&&recent[i].data==item.data) return i;
    return recent.size()+1;
}

} // namespace

std::vector<model::Candidate> FilterAndSort(
    const std::vector<model::Candidate>& candidates,
    const std::string& query,
    FilterMode filter,
    SortMode sort,
    const std::vector<RecentKey>& recent) {
    struct SortEntry {
        model::Candidate item;
        std::string titleKey;
        uint64_t sizeBytes{0};
        size_t recentOrder{0};
    };

    const std::string queryKey=searchKey(query);
    std::vector<SortEntry> entries;
    entries.reserve(candidates.size());
    for(const auto& item:candidates) {
        if(!matchesFilter(item.kind,filter))continue;
        std::string titleKey=searchKey(item.title);
        if(!queryKey.empty() &&
           titleKey.find(queryKey)==std::string::npos &&
           searchKey(item.data).find(queryKey)==std::string::npos)
            continue;

        SortEntry entry{};
        entry.item=item;
        entry.titleKey=std::move(titleKey);
        // Filesystem metadata and transliterated sort keys are prepared once,
        // never recomputed from inside std::sort's O(n log n) comparator.
        if(sort==SortMode::Size)entry.sizeBytes=fileSize(item);
        if(sort==SortMode::Recent)entry.recentOrder=recentRank(item,recent);
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(),entries.end(),[&](const SortEntry& a,const SortEntry& b) {
        if(sort==SortMode::Recent) {
            if(a.recentOrder!=b.recentOrder)return a.recentOrder<b.recentOrder;
        } else if(sort==SortMode::Size) {
            if(a.sizeBytes!=b.sizeBytes)return a.sizeBytes>b.sizeBytes;
        }
        if(a.titleKey!=b.titleKey)return a.titleKey<b.titleKey;
        if(a.item.kind!=b.item.kind)
            return static_cast<int>(a.item.kind)<static_cast<int>(b.item.kind);
        return a.item.data<b.item.data;
    });

    std::vector<model::Candidate> result;
    result.reserve(entries.size());
    for(auto& entry:entries)result.push_back(std::move(entry.item));
    return result;
}
const char* FilterName(FilterMode filter) {
    switch(filter) {
        case FilterMode::All: return "ALL";
        case FilterMode::Games: return "GAMES";
        case FilterMode::Homebrew: return "HOMEBREW";
        case FilterMode::Widgets: return "WIDGETS";
    }
    return "ALL";
}
const char* SortName(SortMode sort) {
    switch(sort) {
        case SortMode::Name: return "NAME";
        case SortMode::Recent: return "RECENT";
        case SortMode::Size: return "SIZE";
    }
    return "NAME";
}
} // namespace desknx::catalog_view
