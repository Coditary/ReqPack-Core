#include "core/packages/rq_package.h"

#include <cstdlib>
#include <sstream>

namespace {

std::optional<std::string> environment_value(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        return std::string {value};
    }
    return std::nullopt;
}

void merge_string_override(std::optional<std::string>& target, const std::optional<std::string>& fallback) {
    if (!target.has_value() && fallback.has_value() && !fallback->empty()) {
        target = fallback;
    }
}

std::optional<int> parse_int_override(const std::string& raw) {
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(raw, &consumed);
        if (consumed != raw.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

void merge_int_override(std::optional<int>& target, const std::optional<std::string>& envValue) {
    if (target.has_value() || !envValue.has_value()) {
        return;
    }
    if (const std::optional<int> parsed = parse_int_override(*envValue)) {
        target = parsed;
    }
}

std::vector<std::string> split_csv_systems(const std::string& raw) {
    std::vector<std::string> systems;
    std::string current;
    for (char ch : raw) {
        if (ch == ',' || ch == ';') {
            if (!current.empty()) {
                systems.push_back(current);
                current.clear();
            }
            continue;
        }
        if (ch != ' ' && ch != '\t') {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        systems.push_back(current);
    }
    return systems;
}

} // namespace

void rq_pack_metadata_overrides_from_environment(RqPackMetadataOverrides& overrides) {
    merge_string_override(overrides.version, environment_value("REQPACK_PACK_VERSION"));
    merge_int_override(overrides.release, environment_value("REQPACK_PACK_RELEASE"));
    merge_int_override(overrides.revision, environment_value("REQPACK_PACK_REVISION"));
    merge_string_override(overrides.vendor, environment_value("REQPACK_PACK_VENDOR"));
    merge_string_override(overrides.maintainerEmail, environment_value("REQPACK_PACK_MAINTAINER_EMAIL"));
    merge_string_override(overrides.url, environment_value("REQPACK_PACK_URL"));
    merge_string_override(overrides.homepage, environment_value("REQPACK_PACK_HOMEPAGE"));
    merge_string_override(overrides.sourceUrl, environment_value("REQPACK_PACK_SOURCE_URL"));
    merge_string_override(overrides.architecture, environment_value("REQPACK_PACK_ARCHITECTURE"));

    if (overrides.systems.empty()) {
        if (const auto envSystems = environment_value("REQPACK_PACK_SYSTEM")) {
            overrides.systems = split_csv_systems(*envSystems);
        }
    }
}

void rq_apply_pack_metadata_overrides(RqMetadata& metadata, const RqPackMetadataOverrides& overrides) {
    if (overrides.version.has_value()) {
        metadata.version = overrides.version.value();
    }
    if (overrides.release.has_value()) {
        metadata.release = overrides.release.value();
    }
    if (overrides.revision.has_value()) {
        metadata.revision = overrides.revision.value();
    }
    if (overrides.vendor.has_value()) {
        metadata.vendor = overrides.vendor.value();
    }
    if (overrides.maintainerEmail.has_value()) {
        metadata.maintainerEmail = overrides.maintainerEmail.value();
    }
    if (overrides.url.has_value()) {
        metadata.url = overrides.url.value();
    }
    if (overrides.homepage.has_value()) {
        metadata.homepage = overrides.homepage.value();
    }
    if (overrides.sourceUrl.has_value()) {
        metadata.sourceUrl = overrides.sourceUrl.value();
    }
    if (overrides.architecture.has_value()) {
        metadata.architecture = overrides.architecture.value();
    }
    if (!overrides.systems.empty()) {
        metadata.systems = overrides.systems;
    }
}
