#include "rqp_plugin_internal.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using boost::property_tree::ptree;

std::optional<ptree> parse_json_tree(const std::string& json) {
    if (json.empty()) {
        return std::nullopt;
    }

    std::istringstream input(json);
    ptree tree;
    try {
        boost::property_tree::read_json(input, tree);
        return tree;
    } catch (...) {
        return std::nullopt;
    }
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string normalize_manifest_path_string(const std::string& path) {
    std::error_code error;
    const std::filesystem::path normalized =
        std::filesystem::weakly_canonical(std::filesystem::path(path), error);
    if (!error) {
        return normalized.string();
    }
    return std::filesystem::path(path).lexically_normal().string();
}

bool is_ignorable_remove_error(const std::error_code& error) {
    return !error || error == std::errc::no_such_file_or_directory;
}

} // namespace

bool RqpPlugin::persistInstalledState(const RqPackageLayout& layout, const std::string& sourceType,
                                      const std::string& sourceValue, const std::string& repository,
                                      const std::string& requestName) const {
    std::filesystem::create_directories(layout.stateDir / "scripts");
    std::ofstream metadataFile(layout.stateDir / "metadata.json", std::ios::binary | std::ios::trunc);
    if (!metadataFile.is_open()) {
        return false;
    }
    metadataFile << rq_metadata_json(layout.metadata);
    std::filesystem::copy_file(layout.controlDir / "reqpack.lua", layout.stateDir / "reqpack.lua",
                               std::filesystem::copy_options::overwrite_existing);

    const std::filesystem::path scriptsDir = layout.controlDir / "scripts";
    if (std::filesystem::exists(scriptsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(scriptsDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::filesystem::copy_file(entry.path(), layout.stateDir / "scripts" / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
        }
    }

    std::ofstream sourceFile(layout.stateDir / "source.json", std::ios::binary | std::ios::trunc);
    if (!sourceFile.is_open()) {
        return false;
    }
    sourceFile << "{\n"
               << "  \"source\": \"" << json_escape(sourceType) << "\",\n"
               << "  \"path\": \"" << json_escape(sourceValue) << "\",\n"
               << "  \"repository\": \"" << json_escape(repository) << "\",\n"
               << "  \"identity\": \"" << json_escape(layout.identity) << "\"";
    if (!requestName.empty()) {
        sourceFile << ",\n"
                   << "  \"requestName\": \"" << json_escape(requestName) << "\"";
    }
    sourceFile << "\n}\n";
    std::ofstream manifestFile(layout.stateDir / "manifest.json", std::ios::binary | std::ios::trunc);
    if (!manifestFile.is_open()) {
        return false;
    }
    manifestFile << manifestJson(pendingManifest_);
    return true;
}

bool RqpPlugin::removeManifestArtifacts(const RqpInstalledPackage& installed) const {
    std::vector<ManifestEntry> manifest = parseManifestJson(readTextFile(installed.manifestPath));
    std::reverse(manifest.begin(), manifest.end());

    const auto pathStillNeededElsewhere = [&](const std::string& path) {
        const std::string normalizedPath = normalize_manifest_path_string(path);
        for (const RqpInstalledPackage& other : RqpStateStore(config_).listInstalled()) {
            if (other.stateDir == installed.stateDir) {
                continue;
            }
            for (const ManifestEntry& otherEntry : parseManifestJson(readTextFile(other.manifestPath))) {
                if (normalize_manifest_path_string(otherEntry.path) == normalizedPath) {
                    return true;
                }
            }
        }
        return false;
    };

    for (const ManifestEntry& entry : manifest) {
        if (entry.path.empty() || pathStillNeededElsewhere(entry.path)) {
            continue;
        }

        std::error_code error;
        const std::filesystem::path path(entry.path);
        if (entry.type == "dir") {
            if (std::filesystem::is_directory(path, error) && std::filesystem::is_empty(path, error)) {
                std::filesystem::remove(path, error);
            }
        } else if (std::filesystem::exists(path, error)) {
            std::filesystem::remove(path, error);
        }
        if (!is_ignorable_remove_error(error)) {
            return false;
        }
    }
    return true;
}

std::vector<RqpPlugin::ManifestEntry> RqpPlugin::parseManifestJson(const std::string& content) {
    std::vector<ManifestEntry> manifest;
    const auto tree = parse_json_tree(content);
    if (!tree.has_value()) {
        return manifest;
    }
    for (const auto& [_, child] : tree.value()) {
        manifest.push_back(ManifestEntry {
            .type = child.get<std::string>("type", {}),
            .path = child.get<std::string>("path", {}),
        });
    }
    return manifest;
}

std::string RqpPlugin::manifestJson(const std::vector<ManifestEntry>& manifest) {
    std::ostringstream stream;
    stream << "[\n";
    for (std::size_t index = 0; index < manifest.size(); ++index) {
        const ManifestEntry& entry = manifest[index];
        stream << "  {\"type\": \"" << json_escape(entry.type) << "\", \"path\": \"" << json_escape(entry.path)
               << "\"}";
        if (index + 1 < manifest.size()) {
            stream << ',';
        }
        stream << "\n";
    }
    stream << "]\n";
    return stream.str();
}

PackageInfo RqpPlugin::packageInfoMultipleInstalled(const std::string& name,
                                                    std::vector<RqpInstalledPackage> matches) {
    std::sort(matches.begin(), matches.end(),
              [](const RqpInstalledPackage& left, const RqpInstalledPackage& right) {
                  return compareInstalledVersions(left, right) < 0;
              });

    PackageInfo info;
    info.system = BUILTIN_RQP_PLUGIN_ID;
    info.name = name;
    info.status = "installed";
    info.installed = "true";

    std::ostringstream versions;
    bool firstVersion = true;
    for (const RqpInstalledPackage& installed : matches) {
        const std::string version = installedVersionString(installed.metadata);
        if (!firstVersion) {
            versions << ", ";
        }
        versions << version;
        firstVersion = false;
    }
    info.version = versions.str();
    info.summary = "multiple installed versions (" + info.version + ")";
    info.description = multipleInstalledVersionsDetail(name, std::move(matches));
    return info;
}

PackageInfo RqpPlugin::packageInfoFromInstalled(const RqpInstalledPackage& installed) {
    return PackageInfo {
        .system = "rqp",
        .name = installed.metadata.name,
        .packageId = installed.identity,
        .version = installedVersionString(installed.metadata),
        .status = "installed",
        .installed = "true",
        .summary = installed.metadata.summary,
        .description = installed.metadata.summary.empty() ? installed.metadata.description : installed.metadata.summary,
        .homepage = installed.metadata.url,
        .sourceUrl = installed.metadata.sourceUrl,
        .repository = installed.source.repository,
        .packageType = "package",
        .architecture = installed.metadata.architecture,
        .targetSystems = rq_join_systems(installed.metadata.systems),
        .license = installed.metadata.license,
        .author = installed.metadata.vendor,
        .maintainer = installed.metadata.packager,
        .email = installed.metadata.maintainerEmail,
        .publishedAt = installed.metadata.buildDate,
        .dependencies = installed.metadata.depends,
        .provides = installed.metadata.provides,
        .conflicts = installed.metadata.conflicts,
        .replaces = installed.metadata.replaces,
        .tags = installed.metadata.tags,
    };
}

std::string RqpPlugin::installedVersionString(const RqMetadata& metadata) {
    return metadata.version + "-" + std::to_string(metadata.release) + "+r" + std::to_string(metadata.revision);
}

int RqpPlugin::compareInstalledVersions(const RqpInstalledPackage& left, const RqpInstalledPackage& right) {
    const int versionComparison = version_compare_values(left.metadata.version, right.metadata.version);
    if (versionComparison != 0) {
        return versionComparison;
    }
    if (left.metadata.release != right.metadata.release) {
        return left.metadata.release < right.metadata.release ? -1 : 1;
    }
    if (left.metadata.revision != right.metadata.revision) {
        return left.metadata.revision < right.metadata.revision ? -1 : 1;
    }
    return 0;
}

std::string RqpPlugin::multipleInstalledVersionsFailure(const std::string& requestName,
                                                      std::vector<RqpInstalledPackage> matches) {
    std::sort(matches.begin(), matches.end(),
              [](const RqpInstalledPackage& left, const RqpInstalledPackage& right) {
                  return compareInstalledVersions(left, right) < 0;
              });

    std::ostringstream message;
    message << "multiple installed versions for \"" << requestName
            << "\"; specify one version, for example:";
    bool first = true;
    for (const RqpInstalledPackage& installed : matches) {
        const std::string version = installedVersionString(installed.metadata);
        if (!first) {
            message << " or";
        }
        first = false;
        message << " " << requestName << "@" << version;
    }
    return message.str();
}

std::string RqpPlugin::multipleInstalledVersionsDetail(const std::string& requestName,
                                                     std::vector<RqpInstalledPackage> matches) {
    std::sort(matches.begin(), matches.end(),
              [](const RqpInstalledPackage& left, const RqpInstalledPackage& right) {
                  return compareInstalledVersions(left, right) < 0;
              });

    std::ostringstream description;
    description << "Multiple versions are installed.\n"
                << "Installed identities:\n";
    for (const RqpInstalledPackage& installed : matches) {
        const std::string version = installedVersionString(installed.metadata);
        description << "  - " << installed.identity << " (" << version << ")\n";
    }
    description << "Inspect one version:\n";
    for (const RqpInstalledPackage& installed : matches) {
        const std::string version = installedVersionString(installed.metadata);
        description << "  rqp info rqp " << requestName << "@" << version << '\n';
    }
    description << "Remove one version:\n";
    for (const RqpInstalledPackage& installed : matches) {
        const std::string version = installedVersionString(installed.metadata);
        description << "  rqp remove rqp " << requestName << "@" << version << '\n';
    }
    return description.str();
}

bool RqpPlugin::repositoryCandidateIsNewer(const RqpInstalledPackage& installed, const RqRepositoryPackage& candidate) {
    const int versionComparison = version_compare_values(candidate.version, installed.metadata.version);
    if (versionComparison != 0) {
        return versionComparison > 0;
    }
    if (candidate.release != installed.metadata.release) {
        return candidate.release > installed.metadata.release;
    }
    return candidate.revision > installed.metadata.revision;
}
