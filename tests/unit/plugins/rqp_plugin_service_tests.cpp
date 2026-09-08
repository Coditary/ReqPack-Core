#include <catch2/catch.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "core/common/types.h"
#include "core/host/host_info.h"
#include "core/registry/registry.h"
#include "core/state/rqp_state_store.h"
#include "plugins/rqp_plugin.h"
#include "test_helpers.h"

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

std::filesystem::path build_rqp_package(const std::filesystem::path& root, const std::string& name,
                                        const std::string& version = "1.0.0") {
    const std::filesystem::path packageRoot = root / (name + "-pkg");
    const std::filesystem::path controlRoot = packageRoot / "control";
    std::filesystem::create_directories(controlRoot / "scripts");

    write_file(controlRoot / "metadata.json", "{\n"
                                              "  \"formatVersion\": 1,\n"
                                              "  \"name\": \"" +
                                                  name +
                                                  "\",\n"
                                                  "  \"version\": \"" +
                                                  version +
                                                  "\",\n"
                                                  "  \"release\": 1,\n"
                                                  "  \"revision\": 0,\n"
                                                  "  \"summary\": \"test package\",\n"
                                                  "  \"description\": \"rqp plugin test package\",\n"
                                                  "  \"license\": \"MIT\",\n"
                                                  "  \"architecture\": \"noarch\",\n"
                                                  "  \"vendor\": \"ReqPack Tests\",\n"
                                                  "  \"maintainerEmail\": \"tests@example.org\",\n"
                                                  "  \"tags\": [\"test\"],\n"
                                                  "  \"url\": \"https://example.test/" +
                                                  name +
                                                  ".rqp\"\n"
                                                  "}\n");
    write_file(controlRoot / "reqpack.lua", R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install.lua",
    remove = "scripts/remove.lua"
  }
}
)");
    write_file(controlRoot / "scripts" / "install.lua", "return true\n");
    write_file(controlRoot / "scripts" / "remove.lua", "return true\n");

    const std::filesystem::path packagePath = root / (name + ".rqp");
    REQUIRE(std::system(("tar -C " + escape_shell_arg(controlRoot.string()) + " -cf " +
                         escape_shell_arg(packagePath.string()) + " .")
                            .c_str()) == 0);
    return packagePath;
}

std::string sha256_file_hex(const std::filesystem::path& path) {
    const std::string hashOutput = run_command_capture("openssl dgst -sha256 " + escape_shell_arg(path.string()));
    const std::size_t pos = hashOutput.rfind(' ');
    REQUIRE(pos != std::string::npos);
    return hashOutput.substr(pos + 1, 64);
}

std::filesystem::path write_repository_index(const std::filesystem::path& root, const std::string& packageName,
                                             const std::string& packageVersion,
                                             const std::filesystem::path& artifactPath,
                                             const std::optional<std::string>& packageSha256 = std::nullopt) {
    const std::filesystem::path indexPath = root / "index.json";
    write_file(
        indexPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"packages\": [\n"
        "    {\n"
        "      \"name\": \"" +
            packageName +
            "\",\n"
            "      \"version\": \"" +
            packageVersion +
            "\",\n"
            "      \"release\": 1,\n"
            "      \"revision\": 0,\n"
            "      \"architecture\": \"noarch\",\n"
            "      \"summary\": \"repo package\",\n"
            "      \"url\": \"file://" +
            artifactPath.string() + "\"" +
            (packageSha256.has_value() ? ",\n      \"packageSha256\": \"" + packageSha256.value() + "\"\n" : "\n") +
            "    }\n"
            "  ]\n"
            "}\n");
    return indexPath;
}

std::filesystem::path write_repository_index_multi(
    const std::filesystem::path& root,
    const std::vector<std::tuple<std::string, std::string, std::filesystem::path, std::optional<std::string>>>&
        packages) {
    const std::filesystem::path indexPath = root / "index.json";
    std::string entries;
    for (std::size_t index = 0; index < packages.size(); ++index) {
        const auto& [packageName, packageVersion, artifactPath, packageSha256] = packages[index];
        entries += "    {\n"
                   "      \"name\": \"" +
                   packageName +
                   "\",\n"
                   "      \"version\": \"" +
                   packageVersion +
                   "\",\n"
                   "      \"release\": 1,\n"
                   "      \"revision\": 0,\n"
                   "      \"architecture\": \"noarch\",\n"
                   "      \"summary\": \"repo package\",\n"
                   "      \"url\": \"file://" +
                   artifactPath.string() + "\"";
        if (packageSha256.has_value()) {
            entries += ",\n      \"packageSha256\": \"" + packageSha256.value() + "\"";
        }
        entries += "\n    }";
        if (index + 1 < packages.size()) {
            entries += ",\n";
        } else {
            entries += "\n";
        }
    }

    write_file(indexPath, "{\n"
                          "  \"schemaVersion\": 1,\n"
                          "  \"packages\": [\n" +
                              entries +
                              "  ]\n"
                              "}\n");
    return indexPath;
}

void write_plugin_bundle(const std::filesystem::path& pluginRoot, const std::string& pluginName,
                         const std::string& version) {
    const std::filesystem::path pluginDirectory = pluginRoot / pluginName;
    write_file(pluginDirectory / "metadata.json", "{\n"
                                                  "  \"formatVersion\": 1,\n"
                                                  "  \"name\": \"" +
                                                      pluginName +
                                                      "\",\n"
                                                      "  \"version\": \"" +
                                                      version +
                                                      "\",\n"
                                                      "  \"summary\": \"" +
                                                      pluginName +
                                                      " plugin\",\n"
                                                      "  \"description\": \"" +
                                                      pluginName +
                                                      " plugin bundle\",\n"
                                                      "  \"license\": \"MIT\"\n"
                                                      "}\n");
    write_file(pluginDirectory / "reqpack.lua", "return {\n  apiVersion = 1,\n  depends = {}\n}\n");
    write_file(pluginDirectory / "run.lua", R"(
plugin = {}

function plugin.getName() return REQPACK_PLUGIN_ID end
function plugin.getVersion() return ")" + version +
                                                R"(" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages or {} end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.outdated(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, packageName) return { name = packageName or "pkg", version = "1.0.0" } end
function plugin.shutdown() return true end
)");
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");
}

ReqPackConfig make_plugin_config(const TempDir& tempDir) {
    ReqPackConfig config = default_reqpack_config();
    config.rqp.statePath = (tempDir.path() / "rqp-state").string();
    config.registry.pluginDirectory = (tempDir.path() / "plugins").string();
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.registry.autoLoadPlugins = false;
    return config;
}

PluginCallContext make_plugin_context(RqpPlugin& plugin, std::vector<std::string> flags = {}) {
    return PluginCallContext {
        .pluginId = plugin.getPluginId(),
        .pluginDirectory = plugin.getPluginDirectory(),
        .scriptPath = plugin.getScriptPath(),
        .flags = std::move(flags),
        .host = plugin.getRuntimeHost(),
        .hostInfo = HostInfoService::currentSnapshot(),
    };
}

} // namespace

TEST_CASE("rqp plugin exposes built-in metadata and capabilities", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-metadata"};
    RqpPlugin plugin(make_plugin_config(tempDir));

    CHECK(plugin.init());
    CHECK(plugin.getPluginId() == "rqp");
    CHECK(plugin.getName() == "ReqPack Native Package Manager");
    CHECK_FALSE(plugin.getVersion().empty());
    CHECK(plugin.supportsPack());
    CHECK(plugin.supportsResolvePackage());

    const std::vector<std::string> categories = plugin.getCategories();
    REQUIRE(categories.size() == 3);
    CHECK(std::find(categories.begin(), categories.end(), "ReqPack") != categories.end());

    const std::vector<std::string> extensions = plugin.getFileExtensions();
    REQUIRE(extensions.size() == 1);
    CHECK(extensions.front() == ".rqp");
    CHECK(plugin.shutdown());
}

TEST_CASE("rqp plugin installLocal persists installed package state", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-local"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "local-demo");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    CHECK(plugin.installLocal(context, packagePath.string()));

    const std::vector<RqpInstalledPackage> installed = RqpStateStore(config).findInstalled("local-demo", "1.0.0");
    REQUIRE(installed.size() == 1);
    CHECK(installed.front().metadata.name == "local-demo");
}

TEST_CASE("rqp plugin list and info report installed rqp packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-list-info"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "listed-tool", "2.1.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    const std::vector<PackageInfo> listed = plugin.list(context);
    const auto listedIt =
        std::find_if(listed.begin(), listed.end(), [](const PackageInfo& info) { return info.name == "listed-tool"; });
    REQUIRE(listedIt != listed.end());
    CHECK(listedIt->status == "installed");

    const PackageInfo info = plugin.info(context, "listed-tool@2.1.0");
    CHECK(info.name == "listed-tool");
    CHECK(info.version.find("2.1.0") != std::string::npos);
}

TEST_CASE("rqp plugin install resolves repository packages from file index", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "repo-demo", "3.0.0");
    const std::filesystem::path indexPath = write_repository_index(tempDir.path(), "repo-demo", "3.0.0", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "repo-demo";
    request.version = "3.0.0";
    CHECK(plugin.install(context, {request}));

    const std::vector<RqpInstalledPackage> installed = RqpStateStore(config).findInstalled("repo-demo", "3.0.0");
    REQUIRE(installed.size() == 1);
}

TEST_CASE("rqp plugin search and resolvePackage use configured repositories", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-search-resolve"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "searchable", "4.2.0");
    const std::filesystem::path indexPath = write_repository_index(tempDir.path(), "searchable", "4.2.0", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    const std::vector<PackageInfo> searchResults = plugin.search(context, "searchable");
    REQUIRE_FALSE(searchResults.empty());
    CHECK(searchResults.front().name == "searchable");

    Package unresolved;
    unresolved.name = "searchable";
    unresolved.version = "4.2.0";
    const std::optional<Package> resolved = plugin.resolvePackage(context, unresolved);
    REQUIRE(resolved.has_value());
    CHECK(resolved->name == "searchable");
    CHECK(resolved->version.find("4.2.0") != std::string::npos);
}

TEST_CASE("rqp plugin remove uninstalls previously installed packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-remove"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "removable", "1.4.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    Package request;
    request.name = "removable";
    request.version = "1.4.0";
    CHECK(plugin.remove(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("removable", "1.4.0").empty());
}

TEST_CASE("rqp plugin getMissingPackages skips installed packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-missing"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "present", "1.0.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    Package installed;
    installed.name = "present";
    installed.version = "1.0.0";
    Package missing;
    missing.name = "absent";
    missing.version = "9.9.9";

    const std::vector<Package> missingPackages = plugin.getMissingPackages({installed, missing});
    REQUIRE(missingPackages.size() == 1);
    CHECK(missingPackages.front().name == "absent");
}

TEST_CASE("rqp plugin pack builds output archive from project directory", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-pack"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path projectRoot = tempDir.path() / "project";
    write_file(projectRoot / "metadata.json", "{\n"
                                              "  \"formatVersion\": 1,\n"
                                              "  \"name\": \"packed-demo\",\n"
                                              "  \"version\": \"1.0.0\",\n"
                                              "  \"release\": 1,\n"
                                              "  \"revision\": 0,\n"
                                              "  \"summary\": \"packed\",\n"
                                              "  \"description\": \"packed\",\n"
                                              "  \"license\": \"MIT\",\n"
                                              "  \"architecture\": \"noarch\",\n"
                                              "  \"vendor\": \"ReqPack Tests\",\n"
                                              "  \"maintainerEmail\": \"tests@example.org\",\n"
                                              "  \"url\": \"https://example.test/packed-demo.rqp\"\n"
                                              "}\n");
    write_file(projectRoot / "reqpack.lua", R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install.lua"
  }
}
)");
    write_file(projectRoot / "scripts" / "install.lua", "return true\n");

    const std::filesystem::path outputPath = tempDir.path() / "dist" / "packed-demo.rqp";
    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    CHECK(plugin.pack(context, projectRoot.string(), outputPath.string(), {"force"}));
    CHECK(std::filesystem::exists(outputPath));

    const std::vector<std::string> artifacts = plugin.takeRecentArtifacts();
    REQUIRE_FALSE(artifacts.empty());
    CHECK(artifacts.front() == outputPath.string());
}

TEST_CASE("rqp plugin install fails when repositories are not configured", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-no-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    config.rqp.repositories.clear();

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "missing";
    request.version = "1.0.0";
    CHECK_FALSE(plugin.install(context, {request}));
}

TEST_CASE("rqp plugin installLocal fails when directory has no installable rqp file", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-local-empty"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path emptyDir = tempDir.path() / "empty";
    std::filesystem::create_directories(emptyDir);

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    CHECK_FALSE(plugin.installLocal(context, emptyDir.string()));
}

TEST_CASE("rqp plugin update installs newer repository version", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-update-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path artifactV1 = build_rqp_package(tempDir.path() / "v1", "updatable-demo", "1.0.0");
    const std::filesystem::path artifactV2 = build_rqp_package(tempDir.path() / "v2", "updatable-demo", "1.1.0");
    const std::filesystem::path indexPath = write_repository_index_multi(
        tempDir.path(), {
                            {"updatable-demo", "1.0.0", artifactV1, sha256_file_hex(artifactV1)},
                            {"updatable-demo", "1.1.0", artifactV2, sha256_file_hex(artifactV2)},
                        });
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "updatable-demo";
    request.version = "1.0.0";
    REQUIRE(plugin.install(context, {request}));
    REQUIRE(RqpStateStore(config).findInstalled("updatable-demo", "1.0.0").size() == 1);

    Package updateRequest;
    updateRequest.name = "updatable-demo";
    CHECK(plugin.update(context, {updateRequest}));

    CHECK(RqpStateStore(config).findInstalled("updatable-demo", "1.1.0").size() == 1);
    CHECK(RqpStateStore(config).findInstalled("updatable-demo", "1.0.0").empty());
}

TEST_CASE("rqp plugin install short-circuits when package is already installed", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-short-circuit"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "already-there", "2.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "already-there", "2.0.0", packagePath, sha256_file_hex(packagePath));
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "already-there";
    request.version = "2.0.0";
    REQUIRE(plugin.install(context, {request}));
    CHECK(plugin.install(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("already-there", "2.0.0").size() == 1);
}

TEST_CASE("rqp plugin install fails on repository artifact sha256 mismatch", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-bad-hash"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "bad-hash", "1.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "bad-hash", "1.0.0", packagePath, std::string(64, 'a'));
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "bad-hash";
    request.version = "1.0.0";
    CHECK_FALSE(plugin.install(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("bad-hash", "1.0.0").empty());
}

TEST_CASE("rqp plugin info reports repository-only packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-info-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "repo-only", "5.0.0");
    const std::filesystem::path indexPath = write_repository_index(tempDir.path(), "repo-only", "5.0.0", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    const PackageInfo info = plugin.info(context, "repo-only@5.0.0");
    CHECK(info.name == "repo-only");
    CHECK(info.version.find("5.0.0") != std::string::npos);
    CHECK(info.status == "available");
    CHECK(info.installed == "false");
    CHECK(info.packageType == "package");
}

TEST_CASE("rqp plugin list includes scanned local plugin directory", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-list-plugins"};
    ReqPackConfig config = make_plugin_config(tempDir);
    write_plugin_bundle(std::filesystem::path(config.registry.pluginDirectory), "scanned-demo", "2.3.4");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    const std::vector<PackageInfo> listed = plugin.list(context);
    const auto pluginIt =
        std::find_if(listed.begin(), listed.end(), [](const PackageInfo& info) { return info.name == "scanned-demo"; });
    REQUIRE(pluginIt != listed.end());
    CHECK(pluginIt->status == "installed");
    CHECK(pluginIt->packageType == "plugin");
    CHECK(pluginIt->version == "2.3.4");
}

TEST_CASE("rqp plugin outdated returns empty results", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-outdated"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "current", "1.0.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    CHECK(plugin.outdated(context).empty());
}

TEST_CASE("rqp plugin install uses internal repository flag override", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-internal-repo-flag"};
    ReqPackConfig config = make_plugin_config(tempDir);
    config.rqp.repositories.clear();
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "flagged-repo", "1.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "flagged-repo", "1.0.0", packagePath);

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    PluginCallContext context = make_plugin_context(plugin);
    context.flags = {std::string(INTERNAL_RQP_REPOSITORY_FLAG_PREFIX) + "file://" + indexPath.string()};

    Package request;
    request.name = "flagged-repo";
    request.version = "1.0.0";
    CHECK(plugin.install(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("flagged-repo", "1.0.0").size() == 1);
}

TEST_CASE("rqp plugin installLocal finds nested rqp in extracted directory", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-nested-local"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "nested-local", "1.0.0");
    const std::filesystem::path extractedDir = tempDir.path() / "extracted";
    std::filesystem::create_directories(extractedDir / "nested");
    std::filesystem::copy_file(packagePath, extractedDir / "nested" / "nested-local.rqp");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    CHECK(plugin.installLocal(context, extractedDir.string()));
    CHECK(RqpStateStore(config).findInstalled("nested-local", "1.0.0").size() == 1);
}

TEST_CASE("rqp plugin search finds repository packages by term", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-search-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "searchable-repo", "6.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "searchable-repo", "6.0.0", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    const std::vector<PackageInfo> results = plugin.search(context, "searchable repo");
    const auto match = std::find_if(results.begin(), results.end(),
                                    [](const PackageInfo& info) { return info.name == "searchable-repo"; });
    REQUIRE(match != results.end());
    CHECK(match->status == "available");
}

TEST_CASE("rqp plugin resolvePackage prefers installed versions", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-resolve-installed"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "resolved-local", "3.3.3");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    Package request;
    request.name = "resolved-local";
    request.version = "9.9.9";
    const std::optional<Package> resolved = plugin.resolvePackage(context, request);
    REQUIRE(resolved.has_value());
    CHECK(resolved->version.find("3.3.3") != std::string::npos);
}

TEST_CASE("rqp plugin info reports multiple installed versions message", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-info-multi"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packageV1 = build_rqp_package(tempDir.path() / "v1", "multi-info", "1.0.0");
    const std::filesystem::path packageV2 = build_rqp_package(tempDir.path() / "v2", "multi-info", "2.0.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packageV1.string()));
    REQUIRE(plugin.installLocal(context, packageV2.string()));

    const PackageInfo info = plugin.info(context, "multi-info");
    CHECK(info.name == "multi-info");
    CHECK(info.summary == "multiple installed versions");
}

TEST_CASE("rqp plugin remove uninstalls repository-installed package", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-remove-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "removable-repo", "1.5.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "removable-repo", "1.5.0", packagePath, sha256_file_hex(packagePath));
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "removable-repo";
    request.version = "1.5.0";
    REQUIRE(plugin.install(context, {request}));
    CHECK(plugin.remove(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("removable-repo", "1.5.0").empty());
}

void add_registry_plugin_source(ReqPackConfig& config, const TempDir& tempDir, const std::string& pluginName,
                                const std::string& script) {
    const std::filesystem::path sourceRoot = tempDir.path() / "remote-source";
    write_plugin_bundle(sourceRoot, pluginName, "1.0.0");
    write_file(sourceRoot / pluginName / "run.lua", script);
    config.registry.sources[pluginName] = RegistrySourceEntry {
        .source = (sourceRoot / pluginName).string(),
        .alias = false,
        .description = pluginName + " registry plugin",
        .role = "plugin",
    };
}

TEST_CASE("rqp plugin info reads registry-backed plugin records", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-info-registry"};
    ReqPackConfig config = make_plugin_config(tempDir);
    add_registry_plugin_source(config, tempDir, "registry-demo", R"(
plugin = {}
function plugin.getName() return "registry-demo" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    const PackageInfo info = plugin.info(context, "registry-demo");
    CHECK(info.name == "registry-demo");
    CHECK(info.summary.find("registry-demo") != std::string::npos);
}

TEST_CASE("rqp plugin search includes registry plugin records", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-search-registry"};
    ReqPackConfig config = make_plugin_config(tempDir);
    add_registry_plugin_source(config, tempDir, "searchable-plugin", R"(
plugin = {}
function plugin.getName() return "searchable-plugin" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    const std::vector<PackageInfo> results = plugin.search(context, "searchable plugin");
    const auto match = std::find_if(results.begin(), results.end(),
                                    [](const PackageInfo& info) { return info.name == "searchable-plugin"; });
    REQUIRE(match != results.end());
}

TEST_CASE("rqp plugin install records unavailable events for missing packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-install-missing"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "available-only", "1.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "available-only", "1.0.0", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package missing;
    missing.name = "ghost-package";
    missing.version = "9.9.9";
    CHECK_FALSE(plugin.install(context, {missing}));

    const std::vector<PluginEventRecord> events = plugin.takeRecentEvents();
    REQUIRE(events.size() == 1);
    CHECK(events.front().name == "unavailable");
    CHECK(events.front().payload == "ghost-package@9.9.9");
}

TEST_CASE("rqp plugin remove reports multiple installed versions", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-remove-multi"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packageV1 = build_rqp_package(tempDir.path() / "v1", "multi-remove", "1.0.0");
    const std::filesystem::path packageV2 = build_rqp_package(tempDir.path() / "v2", "multi-remove", "2.0.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packageV1.string()));
    REQUIRE(plugin.installLocal(context, packageV2.string()));

    Package request;
    request.name = "multi-remove";
    CHECK_FALSE(plugin.remove(context, {request}));
}

TEST_CASE("rqp plugin installLocal rejects multiple nested rqp files", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-nested-ambiguous"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packageA = build_rqp_package(tempDir.path() / "a", "nested-a", "1.0.0");
    const std::filesystem::path packageB = build_rqp_package(tempDir.path() / "b", "nested-b", "1.0.0");
    const std::filesystem::path extractedDir = tempDir.path() / "extracted";
    std::filesystem::create_directories(extractedDir / "nested");
    std::filesystem::copy_file(packageA, extractedDir / "nested" / "a.rqp");
    std::filesystem::copy_file(packageB, extractedDir / "nested" / "b.rqp");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    REQUIRE_THROWS_WITH(plugin.installLocal(context, extractedDir.string()),
                        Catch::Matchers::Contains("multiple installable rqp files found in extracted archive"));
}

TEST_CASE("rqp plugin update upgrades repository-installed package", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-update-repo"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packageV1 = build_rqp_package(tempDir.path() / "v1", "update-repo", "1.0.0");
    const std::filesystem::path packageV2 = build_rqp_package(tempDir.path() / "v2", "update-repo", "2.0.0");
    const std::filesystem::path indexPath = write_repository_index_multi(
        tempDir.path(), {
                            {"update-repo", "1.0.0", packageV1, sha256_file_hex(packageV1)},
                            {"update-repo", "2.0.0", packageV2, sha256_file_hex(packageV2)},
                        });
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "update-repo";
    request.version = "1.0.0";
    REQUIRE(plugin.install(context, {request}));

    Package updateRequest;
    updateRequest.name = "update-repo";
    updateRequest.version = "1.0.0";
    CHECK(plugin.update(context, {updateRequest}));
    CHECK(RqpStateStore(config).findInstalled("update-repo", "2.0.0").size() == 1);
}

class RecordingDownloadHost final : public IPluginRuntimeHost {
  public:
    std::function<DownloadResult(const std::string&, const std::string&)> onDownload;
    std::filesystem::path tempRoot;

    void logDebug(const std::string&, const std::string&) override {}
    void logInfo(const std::string&, const std::string&) override {}
    void logWarn(const std::string&, const std::string&) override {}
    void logError(const std::string&, const std::string&) override {}
    void emitStatus(const std::string&, int) override {}
    void emitProgress(const std::string&, const DisplayProgressMetrics&) override {}
    void emitBeginStep(const std::string&, const std::string&) override {}
    void emitCommit(const std::string&) override {}
    void emitSuccess(const std::string&) override {}
    void emitFailure(const std::string&, const std::string&) override {}
    void emitEvent(const std::string&, const std::string&, const std::string&) override {}
    void registerArtifact(const std::string&, const std::string&) override {}
    ExecResult execute(const std::string&, const std::string&) override {
        return ExecResult {.success = true, .exitCode = 0};
    }

    std::string createTempDirectory(const std::string&) override {
        const std::filesystem::path directory = tempRoot / ("tmp-" + std::to_string(++tempCounter));
        std::filesystem::create_directories(directory);
        return directory.string();
    }

    DownloadResult download(const std::string&, const std::string& url, const std::string& destinationPath) override {
        if (onDownload) {
            return onDownload(url, destinationPath);
        }
        return {};
    }

  private:
    int tempCounter {0};
};

std::filesystem::path build_rqp_package_with_manifest_install(const std::filesystem::path& root,
                                                              const std::string& name,
                                                              const std::string& version = "1.0.0") {
    const std::filesystem::path packageRoot = root / (name + "-pkg");
    const std::filesystem::path controlRoot = packageRoot / "control";
    std::filesystem::create_directories(controlRoot / "scripts");

    write_file(controlRoot / "metadata.json", "{\n"
                                              "  \"formatVersion\": 1,\n"
                                              "  \"name\": \"" +
                                                  name +
                                                  "\",\n"
                                                  "  \"version\": \"" +
                                                  version +
                                                  "\",\n"
                                                  "  \"release\": 1,\n"
                                                  "  \"revision\": 0,\n"
                                                  "  \"summary\": \"manifest package\",\n"
                                                  "  \"description\": \"manifest package\",\n"
                                                  "  \"license\": \"MIT\",\n"
                                                  "  \"architecture\": \"noarch\",\n"
                                                  "  \"vendor\": \"ReqPack Tests\",\n"
                                                  "  \"maintainerEmail\": \"tests@example.org\",\n"
                                                  "  \"tags\": [\"test\"],\n"
                                                  "  \"url\": \"https://example.test/" +
                                                  name +
                                                  ".rqp\"\n"
                                                  "}\n");
    write_file(controlRoot / "reqpack.lua", R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install.lua",
    remove = "scripts/remove.lua"
  }
}
)");
    write_file(controlRoot / "scripts" / "install.lua", R"(
local target = context.paths.stateDir .. "/installed.txt"
context.fs.copy(context.paths.controlDir .. "/metadata.json", target)
context.artifacts.register_file(target)
context.artifacts.register_dir(context.paths.stateDir .. "/scripts")
return true
)");
    write_file(controlRoot / "scripts" / "remove.lua", "return true\n");

    const std::filesystem::path packagePath = root / (name + ".rqp");
    REQUIRE(std::system(("tar -C " + escape_shell_arg(controlRoot.string()) + " -cf " +
                         escape_shell_arg(packagePath.string()) + " .")
                            .c_str()) == 0);
    return packagePath;
}

TEST_CASE("rqp plugin getMissingPackages filters already installed packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-missing-packages"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "missing-filter", "1.0.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    const std::vector<Package> requests {
        Package {.action = ActionType::INSTALL, .system = "rqp", .name = "missing-filter", .version = "1.0.0"},
        Package {.action = ActionType::INSTALL, .system = "rqp", .name = "other-package"},
    };
    const std::vector<Package> missing = plugin.getMissingPackages(requests);
    REQUIRE(missing.size() == 1);
    CHECK(missing.front().name == "other-package");
}

TEST_CASE("rqp plugin list includes installed packages and registry aliases", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-list-aliases"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "listed-alias", "2.0.0");
    write_plugin_bundle(tempDir.path() / "plugins", "target-plugin", "1.0.0");
    config.registry.sources["alias-plugin"] = RegistrySourceEntry {
        .source = "target-plugin",
        .alias = true,
        .description = "Alias for target-plugin",
    };

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    const std::vector<PackageInfo> packages = plugin.list(context);
    const auto installed = std::find_if(packages.begin(), packages.end(),
                                        [](const PackageInfo& info) { return info.name == "listed-alias"; });
    const auto builtin =
        std::find_if(packages.begin(), packages.end(), [](const PackageInfo& info) { return info.name == "rqp"; });
    const auto aliasEntry = std::find_if(packages.begin(), packages.end(),
                                         [](const PackageInfo& info) { return info.name == "alias-plugin"; });
    REQUIRE(installed != packages.end());
    CHECK(installed->status == "installed");
    CHECK(installed->packageType == "package");
    REQUIRE(builtin != packages.end());
    CHECK(builtin->packageType == "builtin");
    REQUIRE(aliasEntry != packages.end());
    CHECK(aliasEntry->packageType == "alias");
}

TEST_CASE("rqp plugin search skips installed packages but finds repository matches", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-search-filter"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path installedPackage =
        build_rqp_package(tempDir.path() / "installed", "installed-search", "1.0.0");
    const std::filesystem::path repoPackage = build_rqp_package(tempDir.path() / "repo", "repo-searchable", "3.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "repo-searchable", "3.0.0", repoPackage);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, installedPackage.string()));

    const std::vector<PackageInfo> results = plugin.search(context, "search");
    const auto installedMatch = std::find_if(results.begin(), results.end(),
                                             [](const PackageInfo& info) { return info.name == "installed-search"; });
    if (installedMatch != results.end()) {
        CHECK(installedMatch->status == "installed");
    }
    const auto repoMatch = std::find_if(results.begin(), results.end(),
                                        [](const PackageInfo& info) { return info.name == "repo-searchable"; });
    REQUIRE(repoMatch != results.end());
    CHECK(repoMatch->status == "available");
}

TEST_CASE("rqp plugin info resolves versioned repository package names", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-info-versioned"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "versioned-info", "5.5.5");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "versioned-info", "5.5.5", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PackageInfo info = plugin.info(make_plugin_context(plugin), "versioned-info@5.5.5");
    CHECK(info.name == "versioned-info");
    CHECK(info.version.find("5.5.5") != std::string::npos);
    CHECK(info.status == "available");
}

TEST_CASE("rqp plugin install and remove persist and clean manifest artifacts", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-manifest"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath =
        build_rqp_package_with_manifest_install(tempDir.path(), "manifest-demo", "1.0.0");

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    const std::vector<RqpInstalledPackage> installed = RqpStateStore(config).findInstalled("manifest-demo", "1.0.0");
    REQUIRE(installed.size() == 1);
    CHECK(std::filesystem::exists(installed.front().manifestPath));
    CHECK(std::filesystem::exists(installed.front().stateDir / "installed.txt"));

    Package request;
    request.name = "manifest-demo";
    request.version = "1.0.0";
    CHECK(plugin.remove(context, {request}));
    CHECK_FALSE(std::filesystem::exists(installed.front().stateDir / "installed.txt"));
    CHECK(RqpStateStore(config).findInstalled("manifest-demo", "1.0.0").empty());
}

TEST_CASE("rqp plugin remove cleans empty manifest directories", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-manifest-dir"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::string name = "manifest-dir-demo";
    const std::filesystem::path packageRoot = tempDir.path() / (name + "-pkg");
    const std::filesystem::path controlRoot = packageRoot / "control";
    std::filesystem::create_directories(controlRoot / "scripts");
    write_file(controlRoot / "metadata.json", "{\n"
                                              "  \"formatVersion\": 1,\n"
                                              "  \"name\": \"" +
                                                  name +
                                                  "\",\n"
                                                  "  \"version\": \"1.0.0\",\n"
                                                  "  \"release\": 1,\n"
                                                  "  \"revision\": 0,\n"
                                                  "  \"summary\": \"manifest dir demo\",\n"
                                                  "  \"description\": \"manifest dir demo\",\n"
                                                  "  \"license\": \"MIT\",\n"
                                                  "  \"architecture\": \"noarch\",\n"
                                                  "  \"vendor\": \"ReqPack Tests\",\n"
                                                  "  \"maintainerEmail\": \"tests@example.org\",\n"
                                                  "  \"tags\": [\"test\"],\n"
                                                  "  \"url\": \"https://example.test/" +
                                                  name +
                                                  ".rqp\"\n"
                                                  "}\n");
    write_file(controlRoot / "reqpack.lua", R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install.lua",
    remove = "scripts/remove.lua"
  }
}
)");
    write_file(controlRoot / "scripts" / "install.lua", R"(
local emptyDir = context.paths.stateDir .. "/empty-dir"
context.fs.mkdir(emptyDir)
context.artifacts.register_dir(emptyDir)
return true
)");
    write_file(controlRoot / "scripts" / "remove.lua", "return true\n");
    const std::filesystem::path packagePath = tempDir.path() / (name + ".rqp");
    REQUIRE(std::system(("tar -C " + escape_shell_arg(controlRoot.string()) + " -cf " +
                         escape_shell_arg(packagePath.string()) + " .")
                            .c_str()) == 0);

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);
    REQUIRE(plugin.installLocal(context, packagePath.string()));

    const std::vector<RqpInstalledPackage> installed = RqpStateStore(config).findInstalled(name, "1.0.0");
    REQUIRE(installed.size() == 1);
    const std::filesystem::path emptyDir = installed.front().stateDir / "empty-dir";
    REQUIRE(std::filesystem::is_directory(emptyDir));

    Package request;
    request.name = name;
    request.version = "1.0.0";
    plugin.remove(context, {request});
    CHECK_FALSE(std::filesystem::exists(emptyDir));
}

TEST_CASE("rqp plugin install fails when repository index cannot be loaded", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-repo-load-fail"};
    ReqPackConfig config = make_plugin_config(tempDir);
    config.rqp.repositories = {"https://repo.test/missing-index.json"};

    RecordingDownloadHost host;
    host.tempRoot = tempDir.path();
    host.onDownload = [](const std::string&, const std::string&) { return DownloadResult {}; };

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    PluginCallContext context = make_plugin_context(plugin);
    context.host = &host;

    Package request;
    request.name = "ghost";
    REQUIRE_THROWS_WITH(plugin.install(context, {request}),
                        Catch::Matchers::Contains("failed to load rqp repository index"));
}

TEST_CASE("rqp plugin install resolves downloaded repository directory artifacts", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-repo-directory"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "directory-repo", "1.0.0");
    const std::filesystem::path bundleDirectory = tempDir.path() / "bundle";
    std::filesystem::create_directories(bundleDirectory);
    std::filesystem::copy_file(packagePath, bundleDirectory / "download.rqp");

    const std::filesystem::path indexPath = tempDir.path() / "index.json";
    write_file(indexPath, "{\n"
                          "  \"schemaVersion\": 1,\n"
                          "  \"packages\": [\n"
                          "    {\n"
                          "      \"name\": \"directory-repo\",\n"
                          "      \"version\": \"1.0.0\",\n"
                          "      \"release\": 1,\n"
                          "      \"revision\": 0,\n"
                          "      \"architecture\": \"noarch\",\n"
                          "      \"summary\": \"repo package\",\n"
                          "      \"url\": \"https://repo.test/directory-repo.pkg\"\n"
                          "    }\n"
                          "  ]\n"
                          "}\n");
    config.rqp.repositories = {"file://" + indexPath.string()};

    RecordingDownloadHost host;
    host.tempRoot = tempDir.path();
    host.onDownload = [bundleDirectory](const std::string& url, const std::string& destinationPath) {
        DownloadResult result;
        if (url == "https://repo.test/directory-repo.pkg") {
            result.success = true;
            result.resolvedPath = bundleDirectory.string();
            return result;
        }
        std::filesystem::copy_file(url.rfind("file://") == 0 ? std::filesystem::path(url.substr(7))
                                                             : std::filesystem::path(url),
                                   destinationPath, std::filesystem::copy_options::overwrite_existing);
        result.success = true;
        result.resolvedPath = destinationPath;
        return result;
    };

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    PluginCallContext context = make_plugin_context(plugin);
    context.host = &host;

    Package request;
    request.name = "directory-repo";
    request.version = "1.0.0";
    CHECK(plugin.install(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("directory-repo", "1.0.0").size() == 1);
}

TEST_CASE("rqp plugin outdated returns empty stub list", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-outdated-stub"};
    RqpPlugin plugin(make_plugin_config(tempDir));
    REQUIRE(plugin.init());
    CHECK(plugin.outdated(make_plugin_context(plugin)).empty());
}

TEST_CASE("rqp plugin list exposes request name aliases for installed packages", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-request-alias"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "actual-name", "1.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "friendly-name", "1.0.0", packagePath, sha256_file_hex(packagePath));
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PluginCallContext context = make_plugin_context(plugin);

    Package request;
    request.name = "friendly-name";
    request.version = "1.0.0";
    REQUIRE(plugin.install(context, {request}));

    const std::vector<PackageInfo> packages = plugin.list(context);
    const auto canonical = std::find_if(packages.begin(), packages.end(),
                                        [](const PackageInfo& info) { return info.name == "actual-name"; });
    const auto alias = std::find_if(packages.begin(), packages.end(),
                                    [](const PackageInfo& info) { return info.name == "friendly-name"; });
    REQUIRE(canonical != packages.end());
    CHECK(canonical->packageType == "package");
    REQUIRE(alias != packages.end());
    CHECK(alias->packageType == "alias");
    CHECK(alias->name == "friendly-name");
}

TEST_CASE("rqp plugin info returns empty package for unknown names", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-info-missing"};
    ReqPackConfig config = make_plugin_config(tempDir);

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    const PackageInfo info = plugin.info(make_plugin_context(plugin), "definitely-missing-package");
    CHECK(info.name.empty());
    CHECK(info.version.empty());
}

TEST_CASE("rqp plugin ignores empty internal repository flag overrides", "[unit][rqp_plugin]") {
    TempDir tempDir {"reqpack-rqp-plugin-empty-repo-flag"};
    ReqPackConfig config = make_plugin_config(tempDir);
    const std::filesystem::path packagePath = build_rqp_package(tempDir.path(), "flagged-empty", "1.0.0");
    const std::filesystem::path indexPath =
        write_repository_index(tempDir.path(), "flagged-empty", "1.0.0", packagePath);
    config.rqp.repositories = {"file://" + indexPath.string()};

    RqpPlugin plugin(config);
    REQUIRE(plugin.init());
    PluginCallContext context = make_plugin_context(plugin);
    context.flags = {std::string(INTERNAL_RQP_REPOSITORY_FLAG_PREFIX)};

    Package request;
    request.name = "flagged-empty";
    request.version = "1.0.0";
    CHECK(plugin.install(context, {request}));
    CHECK(RqpStateStore(config).findInstalled("flagged-empty", "1.0.0").size() == 1);
}
