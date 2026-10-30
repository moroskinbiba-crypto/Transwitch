#pragma once
// Application-service adapter based on the SLaunch backend.
// Upstream: https://github.com/etonedemid/slaunch
// This file is distributed under GPL-3.0-or-later; see THIRD_PARTY.md.
#include <switch.h>
#include <string>
#include <vector>

namespace desknx::slaunch {
Result EnumerateInstalled(std::vector<NsApplicationRecord>& records);
Result QueryApplicationViews(const std::vector<u64>& titleIds,
                             std::vector<NsApplicationView>& views);
Result ReadControlData(u64 titleId, NsApplicationControlData& data, u64& bytesWritten);
std::string ResolveTitleName(const NacpStruct& nacp);
}
