#pragma once

#include "core/common/types.h"

#include <optional>
#include <string>

struct SearchInteractiveSelection {
    std::string installSpecifier;
    std::string label;
};

std::optional<SearchInteractiveSelection> pick_search_result_interactive(const std::vector<PackageInfo>& results,
                                                                         const std::string& system,
                                                                         bool stdinIsInteractive);
