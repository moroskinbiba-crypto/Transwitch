#pragma once

#include "../include/desknx/Model.hpp"
#include <string>
#include <vector>

namespace desknx::catalog_view {

enum class FilterMode { All, Games, Homebrew, Widgets };
enum class SortMode { Name, Recent, Size };

struct RecentKey {
    model::Kind kind{model::Kind::Game};
    std::string data;
};

std::vector<model::Candidate> FilterAndSort(
    const std::vector<model::Candidate>& candidates,
    const std::string& query,
    FilterMode filter,
    SortMode sort,
    const std::vector<RecentKey>& recent);

const char* FilterName(FilterMode filter);
const char* SortName(SortMode sort);

} // namespace desknx::catalog_view
