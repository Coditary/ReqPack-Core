#pragma once

#include "core/manifest/manifest_loader.h"

#include <filesystem>
#include <optional>
#include <string>

struct ManifestPathResolverOptions {
    /// When set, non-explicit manifest arguments are resolved relative to this directory.
    std::filesystem::path baseDirectory;
    /// Accept a bare `reqpack.lua` argument resolved from the current working directory.
    bool allowBareFilename {true};
};

/// Resolves a user-provided manifest argument to a concrete reqpack.lua path.
///
/// Explicit paths start with `.` or `/`, or are exactly `reqpack.lua` when allowed.
/// When `baseDirectory` is set, plugin-relative paths such as `drivers/ffmpeg/reqpack.lua`
/// or `drivers/ffmpeg` are resolved against that directory.
std::optional<std::filesystem::path> resolve_manifest_path(const std::string& argument,
                                                           const ManifestPathResolverOptions& options = {});

/// Resolves a manifest path relative to a plugin directory.
std::optional<std::filesystem::path> resolve_plugin_manifest_path(const std::string& argument,
                                                                  const std::filesystem::path& pluginDirectory);
