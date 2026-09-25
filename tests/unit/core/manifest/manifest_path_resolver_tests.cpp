#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/manifest/manifest_loader.h"
#include "core/manifest/manifest_path_resolver.h"
#include "core/plugins/plugin_bundle.h"

namespace {

class TempDir {
  public:
    explicit TempDir(const std::string& prefix)
        : path_(std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.is_open());
    output << content;
}

} // namespace

TEST_CASE("manifest path resolver accepts explicit directory and file paths", "[unit][manifest][path]") {
    TempDir tempDir {"reqpack-manifest-path-explicit"};
    write_file(tempDir.path() / "reqpack.lua", "return { packages = { { system = \"dnf\", name = \"curl\" } } }\n");

    const std::optional<std::filesystem::path> directoryPath = resolve_manifest_path(tempDir.path().string());
    REQUIRE(directoryPath.has_value());
    CHECK(directoryPath.value().filename() == MANIFEST_FILENAME);

    const std::optional<std::filesystem::path> filePath =
        resolve_manifest_path((tempDir.path() / "reqpack.lua").string());
    REQUIRE(filePath.has_value());
    CHECK(filePath.value() == directoryPath.value());
}

TEST_CASE("manifest path resolver resolves plugin-relative paths", "[unit][manifest][path]") {
    TempDir tempDir {"reqpack-manifest-path-plugin"};
    const std::filesystem::path pluginDirectory = tempDir.path() / "plugin";
    const std::filesystem::path nestedDirectory = pluginDirectory / "drivers" / "ffmpeg";
    write_file(nestedDirectory / "reqpack.lua", "return { packages = { { system = \"dnf\", name = \"ffmpeg\" } } }\n");

    const std::optional<std::filesystem::path> nestedFilePath =
        resolve_plugin_manifest_path("drivers/ffmpeg/reqpack.lua", pluginDirectory);
    REQUIRE(nestedFilePath.has_value());
    CHECK(std::filesystem::exists(nestedFilePath.value()));

    const std::optional<std::filesystem::path> nestedDirectoryPath =
        resolve_plugin_manifest_path("drivers/ffmpeg", pluginDirectory);
    REQUIRE(nestedDirectoryPath.has_value());
    CHECK(nestedDirectoryPath.value().filename() == MANIFEST_FILENAME);
}

TEST_CASE("manifest loader tryLoadPackages returns nullopt without packages table", "[unit][manifest][path]") {
    TempDir tempDir {"reqpack-manifest-path-try-load"};
    write_file(tempDir.path() / "reqpack.lua", "return { apiVersion = 1, depends = {} }\n");

    const std::optional<std::vector<ManifestEntry>> entries =
        ManifestLoader::tryLoadPackages(tempDir.path() / "reqpack.lua");
    CHECK_FALSE(entries.has_value());
}

TEST_CASE("manifest loader tryLoadPackages loads project-style packages", "[unit][manifest][path]") {
    TempDir tempDir {"reqpack-manifest-path-try-load-packages"};
    write_file(tempDir.path() / "reqpack.lua",
               "return { packages = { { system = \"npm\", name = \"typescript\" } } }\n");

    const std::optional<std::vector<ManifestEntry>> entries =
        ManifestLoader::tryLoadPackages(tempDir.path() / "reqpack.lua");
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 1);
    CHECK(entries->front().system == "npm");
    CHECK(entries->front().name == "typescript");
}

TEST_CASE("plugin bundle resolves nested project manifests from plugin reqpack.lua", "[unit][manifest][path]") {
    TempDir tempDir {"reqpack-plugin-bundle-manifests"};
    const std::filesystem::path pluginDirectory = tempDir.path() / "app";
    write_file(pluginDirectory / "drivers" / "ffmpeg" / "reqpack.lua",
               "return { packages = { { system = \"dep\", name = \"ffmpeg\" } } }\n");
    write_file(pluginDirectory / "metadata.json", "{\n"
                                                  "  \"formatVersion\": 1,\n"
                                                  "  \"name\": \"app\",\n"
                                                  "  \"version\": \"1.0.0\",\n"
                                                  "  \"summary\": \"app plugin\",\n"
                                                  "  \"description\": \"app plugin bundle\",\n"
                                                  "  \"license\": \"MIT\"\n"
                                                  "}\n");
    write_file(pluginDirectory / "reqpack.lua", R"(
return {
  apiVersion = 1,
  depends = { "dep:runtime" },
  manifests = { "drivers/ffmpeg/reqpack.lua" },
  packages = {
    { system = "dep", name = "inline" },
  },
}
)");
    write_file(pluginDirectory / "run.lua", "return {}\n");
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");

    const std::optional<PluginBundleLayout> layout = plugin_bundle_read_directory(pluginDirectory);
    REQUIRE(layout.has_value());
    REQUIRE(layout->manifest.inlinePackageEntries.size() == 1);
    CHECK(layout->manifest.inlinePackageEntries.front().name == "inline");

    const std::vector<Package> packages = plugin_bundle_project_packages(layout.value());
    REQUIRE(packages.size() == 2);
    CHECK(packages[0].name == "inline");
    CHECK(packages[1].name == "ffmpeg");
}
