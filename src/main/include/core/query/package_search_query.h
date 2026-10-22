#pragma once

#include "core/common/types.h"

#include <optional>
#include <string>
#include <vector>

std::vector<std::string> package_search_terms_from_prompt(const std::string& prompt);

int package_search_relevance_score(const PackageInfo& info, const std::vector<std::string>& terms);

void sort_package_search_results(std::vector<PackageInfo>& results, const std::string& prompt);

bool package_info_is_installed_for_display(const PackageInfo& info);

std::optional<std::string> package_search_did_you_mean_suggestion(const std::vector<PackageInfo>& catalog,
                                                                  const std::string& prompt);

std::vector<PackageInfo> package_search_ranked_catalog(const std::vector<PackageInfo>& catalog,
                                                       const std::string& prompt);
