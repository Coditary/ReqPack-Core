#include "core/query/package_search_query.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::size_t levenshtein_distance(const std::string& left, const std::string& right) {
    const std::size_t leftSize = left.size();
    const std::size_t rightSize = right.size();
    if (leftSize == 0) {
        return rightSize;
    }
    if (rightSize == 0) {
        return leftSize;
    }

    std::vector<std::size_t> previous(rightSize + 1);
    std::vector<std::size_t> current(rightSize + 1);
    for (std::size_t index = 0; index <= rightSize; ++index) {
        previous[index] = index;
    }

    for (std::size_t leftIndex = 1; leftIndex <= leftSize; ++leftIndex) {
        current[0] = leftIndex;
        for (std::size_t rightIndex = 1; rightIndex <= rightSize; ++rightIndex) {
            const std::size_t substitutionCost =
                left[leftIndex - 1] == right[rightIndex - 1] ? 0 : 1;
            current[rightIndex] = std::min({previous[rightIndex] + 1, current[rightIndex - 1] + 1,
                                            previous[rightIndex - 1] + substitutionCost});
        }
        previous.swap(current);
    }
    return previous[rightSize];
}

bool string_iequals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::string display_name_for_score(const PackageInfo& info) {
    if (!info.name.empty()) {
        return info.name;
    }
    return info.packageId;
}

}  // namespace

std::vector<std::string> package_search_terms_from_prompt(const std::string& prompt) {
    std::vector<std::string> terms;
    std::string current;
    for (const char character : prompt) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (!current.empty()) {
                terms.push_back(to_lower_copy(current));
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }
    if (!current.empty()) {
        terms.push_back(to_lower_copy(current));
    }
    return terms;
}

int package_search_relevance_score(const PackageInfo& info, const std::vector<std::string>& terms) {
    if (terms.empty()) {
        return 0;
    }

    const std::string name = to_lower_copy(display_name_for_score(info));
    const std::string packageId = to_lower_copy(info.packageId);
    const std::string summary = to_lower_copy(info.summary);
    const std::string description = to_lower_copy(info.description);
    const std::string packageType = to_lower_copy(info.packageType);

    int score = 0;
    for (const std::string& term : terms) {
        if (name == term) {
            score += 1000;
            continue;
        }
        if (!packageId.empty() && packageId == term) {
            score += 950;
            continue;
        }
        if (!name.empty() && name.rfind(term, 0) == 0) {
            score += 500;
            continue;
        }
        if (name.find(term) != std::string::npos) {
            score += 250;
            continue;
        }
        if (packageId.find(term) != std::string::npos) {
            score += 200;
            continue;
        }
        if (summary.find(term) != std::string::npos || description.find(term) != std::string::npos) {
            score += 100;
            continue;
        }
        if (packageType.find(term) != std::string::npos) {
            score += 50;
        }
    }
    return score;
}

void sort_package_search_results(std::vector<PackageInfo>& results, const std::string& prompt) {
    const std::vector<std::string> terms = package_search_terms_from_prompt(prompt);
    std::stable_sort(results.begin(), results.end(), [&](const PackageInfo& left, const PackageInfo& right) {
        const int leftScore = package_search_relevance_score(left, terms);
        const int rightScore = package_search_relevance_score(right, terms);
        if (leftScore != rightScore) {
            return leftScore > rightScore;
        }
        const std::string leftName = display_name_for_score(left);
        const std::string rightName = display_name_for_score(right);
        if (leftName != rightName) {
            return leftName < rightName;
        }
        return left.packageType < right.packageType;
    });
}

bool package_info_is_installed_for_display(const PackageInfo& info) {
    if (string_iequals(info.installed, "true") || string_iequals(info.installed, "yes")) {
        return true;
    }
    if (string_iequals(info.status, "installed")) {
        return true;
    }
    return false;
}

std::vector<PackageInfo> package_search_ranked_catalog(const std::vector<PackageInfo>& catalog,
                                                       const std::string& prompt) {
    std::vector<PackageInfo> ranked = catalog;
    sort_package_search_results(ranked, prompt);
    return ranked;
}

std::optional<std::string> package_search_did_you_mean_suggestion(const std::vector<PackageInfo>& catalog,
                                                                  const std::string& prompt) {
    const std::vector<std::string> terms = package_search_terms_from_prompt(prompt);
    if (terms.empty() || catalog.empty()) {
        return std::nullopt;
    }

    const std::string& primary = terms.front();
    if (primary.size() < 2) {
        return std::nullopt;
    }

    std::unordered_map<std::string, int> bestDistance;
    for (const PackageInfo& info : catalog) {
        const std::string candidate = to_lower_copy(display_name_for_score(info));
        if (candidate.empty() || candidate == primary) {
            continue;
        }
        const int distance = static_cast<int>(levenshtein_distance(primary, candidate));
        const int maxDistance = primary.size() <= 4 ? 1 : 2;
        if (distance > maxDistance) {
            continue;
        }
        const auto existing = bestDistance.find(candidate);
        if (existing == bestDistance.end() || distance < existing->second) {
            bestDistance[candidate] = distance;
        }
    }

    if (bestDistance.empty()) {
        return std::nullopt;
    }

    std::string suggestion;
    int best = -1;
    for (const auto& [name, distance] : bestDistance) {
        if (best < 0 || distance < best || (distance == best && name < suggestion)) {
            best = distance;
            suggestion = name;
        }
    }
    return suggestion;
}
