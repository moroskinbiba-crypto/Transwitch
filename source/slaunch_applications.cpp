// Application-service adapter based on the SLaunch backend.
// Upstream: https://github.com/etonedemid/slaunch
// GPL-3.0-or-later; see THIRD_PARTY.md and projects/sSystem/COPYING.sLaunch.
#include "slaunch_applications.hpp"
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace desknx::slaunch {
namespace {

template <typename T, typename = void>
struct HasLangData : std::false_type {};

template <typename T>
struct HasLangData<T, std::void_t<decltype(std::declval<const T&>().lang_data.lang)>>
    : std::true_type {};

template <typename T>
const NacpLanguageEntry* LanguageEntries(const T& nacp) {
    if constexpr (HasLangData<T>::value) {
        return nacp.lang_data.lang;
    } else {
        return nacp.lang;
    }
}

} // namespace

Result EnumerateInstalled(std::vector<NsApplicationRecord>& records) {
    records.clear();
    constexpr s32 pageCapacity = 30;
    s32 cursor = 0;
    for (;;) {
        NsApplicationRecord page[pageCapacity]{};
        s32 returned = 0;
        const Result rc = nsListApplicationRecord(page, pageCapacity, cursor, &returned);
        if (R_FAILED(rc)) return rc;
        if (returned < 0 || returned > pageCapacity)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        if (returned == 0) break;
        records.insert(records.end(), page, page + returned);
        cursor += returned;
        if (returned != pageCapacity) break;
    }
    return 0;
}

Result QueryApplicationViews(const std::vector<u64>& titleIds,
                             std::vector<NsApplicationView>& views) {
    views.clear();
    if (titleIds.empty()) return 0;
    if (titleIds.size() > static_cast<size_t>(std::numeric_limits<s32>::max()))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    views.resize(titleIds.size());
    return nsGetApplicationView(views.data(), titleIds.data(),
                                static_cast<s32>(titleIds.size()));
}

Result ReadControlData(u64 titleId, NsApplicationControlData& data,
                       u64& bytesWritten) {
    bytesWritten = 0;
    std::memset(&data, 0, sizeof(data));
    return nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                       titleId, &data, sizeof(data), &bytesWritten);
}

std::string ResolveTitleName(const NacpStruct& nacp) {
    // libnx renamed NacpStruct::lang to lang_data.lang. Detect the layout at
    // compile time so this adapter builds with both packaged and upstream SDKs.
    const NacpLanguageEntry* locales = LanguageEntries(nacp);
    // SLaunch's title policy: English first; otherwise use the first locale
    // that actually contains a title. Do not reject records lacking a title.
    const NacpLanguageEntry* chosen = nullptr;
    if (locales[0].name[0] != '\0') chosen = &locales[0];
    for (int index = 1; !chosen && index < 16; ++index) {
        if (locales[index].name[0] != '\0') chosen = &locales[index];
    }
    if (!chosen) return {};
    char result[129]{};
    std::strncpy(result, chosen->name, sizeof(result) - 1);
    return std::string(result);
}

}
