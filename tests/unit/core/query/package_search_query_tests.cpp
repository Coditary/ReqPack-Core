#include <catch2/catch.hpp>

#include "core/common/types.h"
#include "core/query/package_search_query.h"

TEST_CASE("package search relevance ranks exact name matches first", "[unit][query][search]") {
    PackageInfo exact;
    exact.name = "wuji";
    exact.summary = "Unified AI CLI";

    PackageInfo partial;
    partial.name = "wuji-ai";
    partial.summary = "Unified AI CLI alias";

    PackageInfo descriptionOnly;
    descriptionOnly.name = "other";
    descriptionOnly.description = "wrapper for wuji tools";

    std::vector<PackageInfo> results = {descriptionOnly, partial, exact};
    sort_package_search_results(results, "wuji");

    REQUIRE(results.size() == 3);
    CHECK(results[0].name == "wuji");
    CHECK(results[1].name == "wuji-ai");
    CHECK(results[2].name == "other");
}

TEST_CASE("package search suggests close typo matches", "[unit][query][search]") {
    PackageInfo candidate;
    candidate.name = "wuji";

    const std::optional<std::string> suggestion =
        package_search_did_you_mean_suggestion({candidate}, "wuii");
    REQUIRE(suggestion.has_value());
    CHECK(*suggestion == "wuji");
}

TEST_CASE("package search installed filter helper recognizes installed state", "[unit][query][search]") {
    PackageInfo installed;
    installed.installed = "true";
    CHECK(package_info_is_installed_for_display(installed));

    PackageInfo notInstalled;
    notInstalled.status = "available";
    CHECK_FALSE(package_info_is_installed_for_display(notInstalled));
}
