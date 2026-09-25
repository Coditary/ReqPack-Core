#include "core/manifest/manifest_path_resolver.h"

#include <system_error>

namespace {

bool is_explicit_manifest_argument(const std::string& argument, const bool allowBareFilename) {
    if (argument.empty()) {
        return false;
    }

    if (argument[0] == '.' || argument[0] == '/') {
        return true;
    }

    return allowBareFilename && std::filesystem::path(argument).filename() == MANIFEST_FILENAME;
}

std::optional<std::filesystem::path> normalize_manifest_candidate(const std::filesystem::path& candidatePath) {
    std::error_code fsError;
    if (std::filesystem::is_directory(candidatePath, fsError) && !fsError) {
        return candidatePath / MANIFEST_FILENAME;
    }

    fsError.clear();
    if (std::filesystem::is_regular_file(candidatePath, fsError) && !fsError &&
        candidatePath.filename() == MANIFEST_FILENAME) {
        return candidatePath;
    }

    if (candidatePath.filename() == MANIFEST_FILENAME) {
        return candidatePath;
    }

    return std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> resolve_manifest_path(const std::string& argument,
                                                           const ManifestPathResolverOptions& options) {
    if (argument.empty()) {
        return std::nullopt;
    }

    const bool explicitPath = is_explicit_manifest_argument(argument, options.allowBareFilename);
    if (!explicitPath && options.baseDirectory.empty()) {
        return std::nullopt;
    }

    std::error_code fsError;
    const std::filesystem::path candidatePath =
        explicitPath ? std::filesystem::absolute(std::filesystem::path(argument), fsError)
                     : std::filesystem::absolute(options.baseDirectory / argument, fsError).lexically_normal();
    if (fsError) {
        return std::nullopt;
    }

    return normalize_manifest_candidate(candidatePath);
}

std::optional<std::filesystem::path> resolve_plugin_manifest_path(const std::string& argument,
                                                                  const std::filesystem::path& pluginDirectory) {
    if (pluginDirectory.empty()) {
        return resolve_manifest_path(argument);
    }

    ManifestPathResolverOptions options;
    options.baseDirectory = pluginDirectory;
    options.allowBareFilename = false;
    return resolve_manifest_path(argument, options);
}
