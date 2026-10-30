#include "rq_package_internal.h"

#include "core/host/host_info.h"
#include "core/plugins/plugin_bundle.h"

#include <iostream>
#include <stdexcept>

namespace rq_package_internal {

namespace {

constexpr const char* kPluginBundlePackageReqpackLua = R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install_plugin_bundle.lua",
    remove = "scripts/remove_plugin_bundle.lua"
  }
}
)";

constexpr const char* kInstallPluginBundleHook = R"(
local pluginId = context.metadata.name
local pluginRoot = context.paths.pluginDirectory .. "/" .. pluginId
local bundleRoot = context.paths.controlDir .. "/bundle"
context.fs.mkdir(pluginRoot)
context.fs.mkdir(pluginRoot .. "/scripts")
context.fs.copy(bundleRoot .. "/metadata.json", pluginRoot .. "/metadata.json")
context.fs.copy(bundleRoot .. "/reqpack.lua", pluginRoot .. "/reqpack.lua")
context.fs.copy(bundleRoot .. "/run.lua", pluginRoot .. "/run.lua")
local copyScripts = context.exec.run("cp -a " .. bundleRoot .. "/scripts/. " .. pluginRoot .. "/scripts/")
if not copyScripts.success then
  return false
end
context.artifacts.register_dir(pluginRoot)
return true
)";

constexpr const char* kRemovePluginBundleHook = R"(
local pluginId = context.metadata.name
local pluginRoot = context.paths.pluginDirectory .. "/" .. pluginId
local removeTree = context.exec.run("rm -rf " .. pluginRoot)
return removeTree.success
)";

bool is_plugin_bundle_pack_project(const std::filesystem::path& projectRoot) {
    if (!is_directory_no_error(projectRoot)) {
        return false;
    }
    if (!plugin_bundle_probe_directory(projectRoot).has_value()) {
        return false;
    }
    if (!is_regular_file_no_error(projectRoot / "run.lua")) {
        return false;
    }
    if (!is_regular_file_no_error(projectRoot / "reqpack.lua")) {
        return false;
    }
    try {
        const std::map<std::string, std::string> hooks = parse_reqpack_hooks_impl(projectRoot / "reqpack.lua");
        if (hooks.contains("install")) {
            return false;
        }
    } catch (...) {
        return true;
    }
    return true;
}

RqMetadata rq_metadata_from_plugin_bundle(const PluginBundleProbe& probe, const RqPackMetadataOverrides& overrides,
                                          const ReqPackConfig& config) {
    RqMetadata metadata;
    metadata.formatVersion = 1;
    metadata.name = to_lower_copy(probe.metadata.name);
    metadata.version = probe.metadata.version;
    metadata.release = 0;
    metadata.revision = 0;
    metadata.summary = probe.metadata.summary;
    metadata.description = probe.metadata.description.empty() ? probe.metadata.summary : probe.metadata.description;
    metadata.license = probe.metadata.license;
    metadata.architecture = rq_normalize_architecture(rq_host_architecture());
    metadata.systems = {metadata.name};
    metadata.vendor = "Coditary";
    metadata.maintainerEmail = "opensource@coditary.com";
    metadata.url = "rqp+plugin://" + metadata.name + "/" + metadata.version;
    if (!probe.metadata.homepage.empty()) {
        metadata.homepage = probe.metadata.homepage;
    }
    if (!probe.metadata.sourceUrl.empty()) {
        metadata.sourceUrl = probe.metadata.sourceUrl;
    }
    metadata.tags = probe.metadata.tags;
    rq_apply_pack_metadata_overrides(metadata, overrides);
    metadata.name = to_lower_copy(metadata.name);
    metadata.architecture = rq_normalize_architecture(metadata.architecture);
    metadata.systems = rq_normalize_systems(metadata.systems);
    (void)config;
    return metadata;
}

} // namespace

bool is_plugin_bundle_pack_input(const std::filesystem::path& projectRoot) {
    return is_plugin_bundle_pack_project(absolute_path(projectRoot));
}

RqPackageBuildResult build_plugin_bundle_package_impl(const RqPackageBuildRequest& request, const ReqPackConfig& config) {
    const std::filesystem::path projectRoot = absolute_path(request.projectRoot);
    const std::optional<PluginBundleProbe> probe = plugin_bundle_probe_directory(projectRoot);
    if (!probe.has_value()) {
        throw std::runtime_error("invalid plugin bundle layout: " + projectRoot.string());
    }

    RqMetadata metadata = rq_metadata_from_plugin_bundle(probe.value(), request.metadataOverrides, config);

    std::filesystem::path outputPath = request.outputPath;
    if (outputPath.empty()) {
        outputPath = projectRoot.parent_path() / (metadata.name + ".rqp");
    }
    if (outputPath.is_relative()) {
        outputPath = std::filesystem::current_path() / outputPath;
    }

    if (exists_no_error(outputPath) && !request.force) {
        if (!request.interactive) {
            throw std::runtime_error("output file already exists: " + outputPath.string() + "\nUse --force to overwrite.");
        }
        std::cout << outputPath.string() << " already exists. Overwrite? [y/N]\n";
        std::cout.flush();
        std::string answer;
        if (!std::getline(std::cin, answer) || (answer != "y" && answer != "Y")) {
            throw std::runtime_error("pack aborted");
        }
    }

    std::vector<TarWriteEntry> packageEntries;
    packageEntries.push_back(TarWriteEntry {
        .path = "metadata.json",
        .type = '0',
        .data = metadata_json_impl(metadata),
        .mode = 0644,
    });
    packageEntries.push_back(TarWriteEntry {
        .path = "reqpack.lua",
        .type = '0',
        .data = kPluginBundlePackageReqpackLua,
        .mode = 0644,
    });
    packageEntries.push_back(TarWriteEntry {
        .path = "scripts/install_plugin_bundle.lua",
        .type = '0',
        .data = kInstallPluginBundleHook,
        .mode = 0644,
    });
    packageEntries.push_back(TarWriteEntry {
        .path = "scripts/remove_plugin_bundle.lua",
        .type = '0',
        .data = kRemovePluginBundleHook,
        .mode = 0644,
    });
    append_control_tree_files(packageEntries, projectRoot, "bundle");

    const std::string archiveBytes = tar_bytes_from_entries(packageEntries);
    std::error_code error;
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            throw std::runtime_error("failed to create output directory: " + outputPath.parent_path().string());
        }
    }
    write_file(outputPath, archiveBytes);

    const std::filesystem::path validationRoot = std::filesystem::temp_directory_path() / "reqpack-rqp-plugin-pack-validate";
    (void)load_package_layout_impl(outputPath, validationRoot / "work", validationRoot / "state", config, false);

    return RqPackageBuildResult {
        .metadata = metadata,
        .identity = rq_package_identity(metadata),
        .outputPath = outputPath,
        .hasPayload = false,
    };
}

} // namespace rq_package_internal
