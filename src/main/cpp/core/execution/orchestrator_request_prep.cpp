#include "orchestrator_internal.h"

#include "core/archive/archive_resolver.h"
#include "core/download/downloader.h"
#include "core/planning/request_resolution.h"
#include "core/plugins/plugin_bundle.h"
#include "core/registry/registry_database_core.h"
#include "core/security/security_bridge.h"
#include "core/security/security_gateway_service.h"
#include "executor_internal.h"
#include "output/diagnostic.h"
#include "output/logger.h"
#include "output/run_result_json.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

bool is_url(const std::string& value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0 || value.rfind("file://", 0) == 0;
}

std::string url_filename(const std::string& url) {
    const std::size_t pos = url.rfind('/');
    if (pos == std::string::npos || pos + 1 >= url.size()) {
        return "download";
    }
    const std::string raw = url.substr(pos + 1);
    const std::size_t q = raw.find('?');
    return q == std::string::npos ? raw : raw.substr(0, q);
}

std::string file_extension(const std::string& path) {
    const std::string filename = url_filename(path);
    const std::string archiveSuffix = generic_archive_suffix(filename);
    if (!archiveSuffix.empty()) {
        return archiveSuffix;
    }
    const std::size_t dot = filename.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return {};
    }
    std::string ext = filename.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

void append_cleanup_paths(std::vector<std::filesystem::path>& tempFiles,
                          const std::vector<std::filesystem::path>& cleanupPaths) {
    tempFiles.insert(tempFiles.end(), cleanupPaths.begin(), cleanupPaths.end());
}

std::string internal_rqp_repository_flag(const std::string& repositoryUrl) {
    return std::string{INTERNAL_RQP_REPOSITORY_FLAG_PREFIX} + repositoryUrl;
}

bool request_has_internal_rqp_repository_flag(const Request& request, const std::string& repositoryUrl) {
    const std::string expected = internal_rqp_repository_flag(repositoryUrl);
    return std::find(request.flags.begin(), request.flags.end(), expected) != request.flags.end();
}

std::string package_specifier_from_request(const Request& request) {
    if (request.system.empty()) {
        return {};
    }
    if (!request.packages.empty()) {
        return request.packages.front();
    }
    return request.system;
}

std::string registry_lookup_name_from_request(const Request& request) {
    const std::string packageSpecifier = package_specifier_from_request(request);
    const std::size_t versionSeparator = packageSpecifier.rfind('@');
    if (versionSeparator == std::string::npos || versionSeparator == 0) {
        return request.system;
    }
    return packageSpecifier.substr(0, versionSeparator);
}

ArchiveExtractionOptions archive_options_from_config(const ReqPackConfig& config) {
    return ArchiveExtractionOptions{
        .password = resolve_archive_password(config),
        .interactive = config.interaction.interactive,
    };
}

bool resolve_local_target(Request& request, Registry* registry, const ReqPackConfig& config,
                          std::vector<std::filesystem::path>& tempFiles, std::string* errorMessage) {
    try {
        const ArchiveResolution resolution =
            extract_archive_to_temp_directory(request.localPath, archive_options_from_config(config));
        if (resolution.changed) {
            append_cleanup_paths(tempFiles, resolution.cleanupPaths);
            request.localPath = resolution.installPath.string();
        }
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }

    if (!request.system.empty()) {
        return true;
    }

    request.system = registry->resolveSystemForLocalTarget(request.localPath);
    if (!request.system.empty()) {
        return true;
    }

    const std::string ext = file_extension(request.localPath);
    if (errorMessage != nullptr) {
        if (ext.empty()) {
            *errorMessage =
                "cannot determine system for local target: " + request.localPath + "\nUse: rqp install <system> <path>";
        } else {
            *errorMessage = "no plugin found for file extension '" + ext + "': " + request.localPath +
                            "\nUse: rqp install <system> <path>";
        }
    }
    return false;
}

} // namespace

namespace orchestrator_internal {

void rewrite_security_gateway_package_requests(std::vector<Request>& requests, Registry* registry,
                                               const ReqPackConfig& config) {
    if (!config.security.enabled || registry == nullptr || requests.size() < 2) {
        return;
    }

    SecurityGatewayService gateway(registry, security_settings_from(config));
    const Request& first = requests.front();
    if (first.system.empty() || !gateway.isGatewaySystem(first.system) ||
        (first.action != ActionType::INSTALL && first.action != ActionType::UPDATE &&
         first.action != ActionType::ENSURE) ||
        first.usesLocalTarget || !first.packages.empty()) {
        return;
    }

    std::vector<std::string> gatewayPackages;
    gatewayPackages.reserve(requests.size() - 1);
    for (std::size_t index = 1; index < requests.size(); ++index) {
        const Request& request = requests[index];
        if (request.action != first.action || request.system.empty() || request.usesLocalTarget ||
            !request.packages.empty()) {
            return;
        }
        if (!registry->getPluginSecurityMetadata(request.system).has_value()) {
            return;
        }
        gatewayPackages.push_back(request.system);
    }

    Request rewritten = first;
    rewritten.packages = std::move(gatewayPackages);
    requests = {std::move(rewritten)};
}

void rewrite_registry_package_requests(std::vector<Request>& requests, const RegistryDatabase* database) {
    if (database == nullptr) {
        return;
    }

    for (Request& request : requests) {
        if (request.system.empty() || request.usesLocalTarget) {
            continue;
        }

        const std::string lookupName = registry_lookup_name_from_request(request);
        const std::optional<RegistryRecord> record = database->getRecord(lookupName);
        if (!record.has_value() || !registry_record_is_package_entry(record.value())) {
            continue;
        }

        const std::string packageSpecifier = package_specifier_from_request(request);
        if (packageSpecifier.empty()) {
            continue;
        }

        request.system = registry_record_target_system(record.value());
        request.packages = {packageSpecifier};
        if (!record->source.empty() && !request_has_internal_rqp_repository_flag(request, record->source)) {
            request.flags.push_back(internal_rqp_repository_flag(record->source));
        }
    }
}

void cleanup_temp_files(const std::vector<std::filesystem::path>& tempFiles) {
    for (const std::filesystem::path& tempFile : tempFiles) {
        std::error_code ec;
        std::filesystem::remove_all(tempFile, ec);
    }
}

bool prepare_requests_for_run(std::vector<Request>& requests, Registry* registry, const ReqPackConfig& config,
                              std::vector<std::filesystem::path>& tempFiles) {
    for (Request& request : requests) {
        if (request.action != ActionType::INSTALL || !request.usesLocalTarget) {
            continue;
        }

        if (!is_url(request.localPath)) {
            std::string errorMessage;
            if (!resolve_local_target(request, registry, config, tempFiles, &errorMessage)) {
                Logger::instance().diagnostic(make_error_diagnostic(
                    "orchestrator", "Local target could not be resolved",
                    "ReqPack could not infer plugin or install target details from provided local path.",
                    "Specify system explicitly, for example `rqp install <system> <path>`.", errorMessage,
                    request.system.empty() ? "install" : request.system, "local-target",
                    {{"path", request.localPath}}));
                return false;
            }
            continue;
        }

        const std::string filename = url_filename(request.localPath);
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "reqpack";
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);
        const std::filesystem::path tempFile = tempDir / filename;

        Logger::instance().logStdout("downloading " + request.localPath, request.system, "install");

        Downloader downloader(registry->getDatabase(), config);
        if (!downloader.download(request.localPath, tempFile.string())) {
            Logger::instance().diagnostic(make_error_diagnostic(
                "network", "Download failed for " + request.localPath,
                "Remote host could not be reached or returned data ReqPack could not download successfully.",
                "Check network access, verify URL, and confirm proxy or firewall settings.", {},
                request.system.empty() ? "download" : request.system, "install",
                {{"url", request.localPath}, {"target", tempFile.string()}}));
            return false;
        }

        tempFiles.push_back(tempFile);
        request.localPath = tempFile.string();
        std::string errorMessage;
        if (!resolve_local_target(request, registry, config, tempFiles, &errorMessage)) {
            Logger::instance().diagnostic(make_error_diagnostic(
                "orchestrator", "Downloaded target could not be resolved",
                "ReqPack downloaded file successfully, but could not map it to a plugin or install target.",
                "Specify system explicitly when installing remote files.", errorMessage,
                request.system.empty() ? "install" : request.system, "local-target", {{"path", request.localPath}}));
            return false;
        }
    }

    RequestResolutionService requestResolver(registry, config);
    std::string resolutionError;
    const std::optional<std::vector<Request>> resolvedRequests =
        requestResolver.resolveRequests(requests, &resolutionError);
    if (!resolvedRequests.has_value()) {
        if (!resolutionError.empty()) {
            Logger::instance().diagnostic(make_error_diagnostic(
                "orchestrator", "Request resolution failed",
                "ReqPack could not normalize one or more user requests into executable operations.",
                "Check package specifiers, system names, and command flags, then retry.", resolutionError, "resolver",
                "request"));
        }
        return false;
    }

    requests = resolvedRequests.value();
    return true;
}

std::vector<std::string> collect_request_item_ids(const std::vector<Request>& requests, const Registry& registry) {
    std::vector<std::string> itemIds;
    for (const Request& request : requests) {
        if (request.action != ActionType::INSTALL && request.action != ActionType::ENSURE) {
            continue;
        }
        if (request.usesLocalTarget) {
            itemIds.push_back(registry.resolvePluginName(request.system) + ":local");
            continue;
        }

        const std::string system = registry.resolvePluginName(request.system);
        for (const std::string& specifier : request.packages) {
            const std::size_t versionSeparator = specifier.rfind('@');
            const std::string name = (versionSeparator == std::string::npos || versionSeparator == 0)
                                         ? specifier
                                         : specifier.substr(0, versionSeparator);
            const std::string version =
                (versionSeparator == std::string::npos || versionSeparator + 1 >= specifier.size())
                    ? std::string{}
                    : specifier.substr(versionSeparator + 1);
            if (version.empty()) {
                itemIds.push_back(system + ":" + name);
            } else {
                itemIds.push_back(system + ":" + name + "@" + version);
            }
        }
    }
    return itemIds;
}

void display_already_satisfied_session(const std::vector<Request>& requests, const Registry& registry,
                                       ActionType action, int skippedCount) {
    const std::vector<std::string> itemIds = collect_request_item_ids(requests, registry);
    Logger::instance().displaySessionBegin(displayModeFromAction(action), itemIds);
    for (const std::string& itemId : itemIds) {
        Logger::instance().displayItemSkipped(itemId);
    }
    Logger::instance().displaySessionEnd(true, 0, skippedCount, 0);
    Logger::instance().flush();
}

RunResultJsonDocument build_already_satisfied_json(const std::vector<Request>& requests, const Registry& registry,
                                                   ActionType action, bool dryRun) {
    RunResultJsonDocument document;
    document.ok = true;
    document.dryRun = dryRun;
    for (const Request& request : requests) {
        if (request.action != ActionType::INSTALL && request.action != ActionType::ENSURE) {
            continue;
        }
        if (request.usesLocalTarget) {
            RunResultJsonItem item;
            item.system = registry.resolvePluginName(request.system);
            item.name = "local";
            item.status = run_result_status_skipped(action);
            document.items.push_back(std::move(item));
            continue;
        }

        const std::string system = registry.resolvePluginName(request.system);
        for (const std::string& specifier : request.packages) {
            const std::size_t versionSeparator = specifier.rfind('@');
            const std::string name = (versionSeparator == std::string::npos || versionSeparator == 0)
                                         ? specifier
                                         : specifier.substr(0, versionSeparator);
            const std::string version =
                (versionSeparator == std::string::npos || versionSeparator + 1 >= specifier.size())
                    ? std::string{}
                    : specifier.substr(versionSeparator + 1);
            RunResultJsonItem item;
            item.system = system;
            item.name = name;
            item.version = version;
            item.status = run_result_status_skipped(action);
            document.items.push_back(std::move(item));
        }
    }
    return document;
}

bool request_has_internal_flag(const Request& request, const std::string& flag) {
    return std::find(request.flags.begin(), request.flags.end(), flag) != request.flags.end();
}

void expand_update_all_requests(std::vector<Request>& requests, Registry* registry, const ReqPackConfig& config) {
    if (registry == nullptr) {
        return;
    }

    const auto expandIt = std::find_if(requests.begin(), requests.end(), [](const Request& request) {
        return request.action == ActionType::UPDATE && request.system.empty() &&
               request_has_internal_flag(request, "__reqpack-internal-update-all-expand");
    });
    if (expandIt == requests.end()) {
        return;
    }

    const std::vector<std::string> flags = expandIt->flags;
    requests.erase(expandIt);

    std::set<std::string> pluginNames;
    if (std::filesystem::exists(config.registry.pluginDirectory)) {
        for (const auto& entry : std::filesystem::directory_iterator(config.registry.pluginDirectory)) {
            if (!entry.is_directory()) {
                continue;
            }
            const std::optional<PluginBundleLayout> layout = plugin_bundle_read_directory(entry.path());
            if (!layout.has_value()) {
                continue;
            }
            const std::string& name = layout->metadata.name;
            if (name.empty() || name == "rqp" || name == "sys") {
                continue;
            }
            pluginNames.insert(name);
        }
    }

    if (registry_database_is_git_source(config.registry.remoteUrl) && !config.registry.remotePluginsPath.empty()) {
        if (RegistryDatabase* database = registry->getDatabase()) {
            for (const RegistryRecord& record : database->getAllRecords()) {
                if (record.alias || !registry_record_can_materialize_plugin(record)) {
                    continue;
                }
                pluginNames.insert(record.name);
            }
        }
    }

    if (pluginNames.empty()) {
        return;
    }

    std::vector<std::string> ordered(pluginNames.begin(), pluginNames.end());
    std::sort(ordered.begin(), ordered.end());
    for (const std::string& plugin : ordered) {
        Request request{.action = ActionType::UPDATE, .system = plugin, .flags = flags};
        request.flags.erase(
            std::remove(request.flags.begin(), request.flags.end(), "__reqpack-internal-update-all-expand"),
            request.flags.end());
        requests.push_back(std::move(request));
    }
}

void ensure_request_plugins_discovered(const std::vector<Request>& requests, Registry* registry,
                                       const ReqPackConfig& config) {
    if (registry == nullptr) {
        return;
    }

    std::vector<std::string> pluginIds;
    for (const Request& request : requests) {
        if (request.system.empty()) {
            continue;
        }
        pluginIds.push_back(request.system);
    }

    if (std::filesystem::exists(config.registry.pluginDirectory)) {
        registry->scanDirectory(config.registry.pluginDirectory);
    }
    std::error_code error;
    const std::filesystem::path workspacePluginDirectory = std::filesystem::current_path(error) / "plugins";
    const std::filesystem::path configuredPluginDirectory = std::filesystem::path(config.registry.pluginDirectory);
    if (!error && workspacePluginDirectory != configuredPluginDirectory &&
        std::filesystem::exists(workspacePluginDirectory)) {
        registry->scanDirectory(workspacePluginDirectory.string());
    }

    if (!pluginIds.empty()) {
        registry->ensurePluginsDiscovered(pluginIds);
    }
}

} // namespace orchestrator_internal
