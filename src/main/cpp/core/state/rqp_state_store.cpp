#include "core/state/rqp_state_store.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace {

bool iequals(const std::string& left, const std::string& right) {
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

bool installed_name_matches(const RqpInstalledPackage& installed, const std::string& name) {
    if (iequals(installed.metadata.name, name)) {
        return true;
    }
    if (!installed.source.requestName.empty() && iequals(installed.source.requestName, name)) {
        return true;
    }

    const std::size_t versionSeparator = installed.source.path.rfind('@');
    if (versionSeparator != std::string::npos && versionSeparator > 0) {
        return iequals(installed.source.path.substr(0, versionSeparator), name);
    }

    return iequals(installed.source.path, name);
}

bool installed_version_matches(const RqpInstalledPackage& installed, const std::string& version) {
    if (version.empty()) {
        return true;
    }
    if (installed.metadata.version == version || installed.identity == version) {
        return true;
    }

    const std::size_t versionSeparator = installed.source.path.rfind('@');
    if (versionSeparator == std::string::npos || versionSeparator + 1 >= installed.source.path.size()) {
        return false;
    }

    const std::string sourceVersion = installed.source.path.substr(versionSeparator + 1);
    if (iequals(sourceVersion, version)) {
        return true;
    }

    return installed.metadata.version.rfind(version, 0) == 0 || installed.identity.rfind(version, 0) == 0;
}

} // namespace

RqpStateStore::RqpStateStore(const ReqPackConfig& config) : config_(config) {}

std::vector<RqpInstalledPackage> RqpStateStore::listInstalled() const {
    std::vector<RqpInstalledPackage> installed;
    const std::filesystem::path root(config_.rqp.statePath);
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return installed;
    }

    for (const auto& packageEntry : std::filesystem::directory_iterator(root, error)) {
        if (error || !packageEntry.is_directory()) {
            continue;
        }
        for (const auto& identityEntry : std::filesystem::directory_iterator(packageEntry.path(), error)) {
            if (error || !identityEntry.is_directory()) {
                continue;
            }
            if (const auto loaded = loadInstalled(identityEntry.path())) {
                installed.push_back(loaded.value());
            }
        }
    }

    std::sort(installed.begin(), installed.end(),
              [](const RqpInstalledPackage& left, const RqpInstalledPackage& right) {
                  if (left.metadata.name != right.metadata.name) {
                      return left.metadata.name < right.metadata.name;
                  }
                  return left.identity < right.identity;
              });
    return installed;
}

std::vector<RqpInstalledPackage> RqpStateStore::findInstalledAmong(const std::vector<RqpInstalledPackage>& installed,
                                                                   const std::string& name,
                                                                   const std::string& version) const {
    std::vector<RqpInstalledPackage> matches;
    for (const RqpInstalledPackage& candidate : installed) {
        if (!installed_name_matches(candidate, name)) {
            continue;
        }
        if (!installed_version_matches(candidate, version)) {
            continue;
        }
        matches.push_back(candidate);
    }
    return matches;
}

std::vector<RqpInstalledPackage> RqpStateStore::findInstalled(const std::string& name,
                                                              const std::string& version) const {
    return this->findInstalledAmong(this->listInstalled(), name, version);
}

bool RqpStateStore::removeInstalledState(const RqpInstalledPackage& installed) const {
    std::error_code error;
    std::filesystem::remove_all(installed.stateDir, error);
    if (error) {
        return false;
    }

    const std::filesystem::path packageRoot = installed.stateDir.parent_path();
    if (packageRoot.empty()) {
        return true;
    }
    if (std::filesystem::is_empty(packageRoot, error) && !error) {
        std::filesystem::remove(packageRoot, error);
    }
    return !error;
}
