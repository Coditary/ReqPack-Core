#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch.hpp>

#include "core/registry/registry.h"

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

ReqPackConfig make_registry_query_config(const std::filesystem::path& root) {
    ReqPackConfig config;
    config.registry.pluginDirectory = (root / "plugins").string();
    config.registry.databasePath = (root / "registry-db").string();
    config.registry.autoLoadPlugins = true;
    config.registry.shutDownPluginsOnExit = true;
    return config;
}

std::filesystem::path add_plugin_script(
    const std::filesystem::path& pluginRoot,
    const std::string& pluginName,
    const std::string& content
) {
    const std::filesystem::path pluginDirectory = pluginRoot / pluginName;
    write_file(pluginDirectory / "metadata.json",
        "{\n"
        "  \"formatVersion\": 1,\n"
        "  \"name\": \"" + pluginName + "\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"summary\": \"" + pluginName + " plugin\",\n"
        "  \"description\": \"" + pluginName + " plugin bundle\",\n"
        "  \"license\": \"MIT\"\n"
        "}\n");
    write_file(pluginDirectory / "reqpack.lua", "return {\n  apiVersion = 1,\n  depends = {}\n}\n");
    write_file(pluginDirectory / "run.lua", content);
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");
    return pluginDirectory / "run.lua";
}

const char* CATEGORY_PLUGIN = R"(
plugin = {}

function plugin.getName() return "category-plugin" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg", "query-helper" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

const char* EXTENSION_PLUGIN = R"(
plugin = {}

function plugin.getName() return "rpm-plugin" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
plugin.fileExtensions = { ".rpm" }
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

}  // namespace

TEST_CASE("registry getAvailableNames includes built-in and scanned plugins", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-names"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "category", CATEGORY_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);

    const std::vector<std::string> names = registry.getAvailableNames();
    REQUIRE(names.size() == 2);
    CHECK(std::find(names.begin(), names.end(), "rqp") != names.end());
    CHECK(std::find(names.begin(), names.end(), "category") != names.end());
}

TEST_CASE("registry findByCategory returns plugins declaring the category", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-category"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "category", CATEGORY_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    REQUIRE(registry.loadPlugin("category"));

    const std::vector<std::string> found = registry.findByCategory("query-helper");
    REQUIRE(found.size() == 1);
    CHECK(found[0] == "category");

    CHECK(registry.findByCategory("missing-category").empty());
}

TEST_CASE("registry resolveSystemForExtension maps built-in rqp extension", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-rqp-ext"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    Registry registry(config);
    CHECK(registry.resolveSystemForExtension(".rqp") == "rqp");
}

TEST_CASE("registry resolveSystemForExtension maps plugin-declared extensions", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-ext"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "rpm", EXTENSION_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    REQUIRE(registry.loadPlugin("rpm"));

    CHECK(registry.resolveSystemForExtension(".rpm") == "rpm");
    CHECK(registry.resolveSystemForExtension(".deb").empty());
}

TEST_CASE("registry resolveSystemForLocalTarget detects reqpack manifest directories", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-local-manifest"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    const std::filesystem::path projectDir = tempDir.path() / "project";
    write_file(projectDir / "reqpack.lua", "return { targets = {} }\n");

    Registry registry(config);
    CHECK(registry.resolveSystemForLocalTarget(projectDir) == "rqp");
}

TEST_CASE("registry resolveSystemForLocalTarget resolves file extension for regular files", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-local-file"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "rpm", EXTENSION_PLUGIN);

    const std::filesystem::path rpmPath = tempDir.path() / "artifacts" / "sample.rpm";
    write_file(rpmPath, "rpm");

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    REQUIRE(registry.loadPlugin("rpm"));

    CHECK(registry.resolveSystemForLocalTarget(rpmPath) == "rpm");
}

TEST_CASE("registry resolveSystemForLocalTarget returns empty for ambiguous directory contents", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-ambiguous"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "rpm", EXTENSION_PLUGIN);

    const std::filesystem::path projectDir = tempDir.path() / "mixed";
    write_file(projectDir / "alpha.rpm", "rpm");
    write_file(projectDir / "beta.rqp", "rqp");

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    REQUIRE(registry.loadPlugin("rpm"));

    CHECK(registry.resolveSystemForLocalTarget(projectDir).empty());
}

TEST_CASE("registry resolvePluginName follows database alias and planner aliases", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-resolve"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());
    config.planner.systemAliases["brew"] = "apt";

    const std::filesystem::path pluginDirectory = add_plugin_script(tempDir.path() / "plugins", "target", CATEGORY_PLUGIN);
    config.registry.sources["target"] = RegistrySourceEntry{
        .source = pluginDirectory.parent_path().string(),
        .alias = false,
        .description = "target plugin",
    };
    config.registry.sources["alias-demo"] = RegistrySourceEntry{
        .source = "target",
        .alias = true,
        .description = "alias to target",
    };

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    CHECK(registry.resolvePluginName("alias-demo") == "target");
    CHECK(registry.resolvePluginName("brew") == "apt");
    CHECK(registry.resolvePluginName("rqp") == "rqp");
}

TEST_CASE("registry getKnownPluginNames includes planner aliases", "[unit][registry_query]") {
    TempDir tempDir{"reqpack-registry-query-known"};
    ReqPackConfig config = make_registry_query_config(tempDir.path());
    config.planner.systemAliases["brew"] = "apt";

    add_plugin_script(tempDir.path() / "plugins", "category", CATEGORY_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);

    const std::vector<std::string> names = registry.getKnownPluginNames();
    CHECK(std::find(names.begin(), names.end(), "brew") != names.end());
    CHECK(std::find(names.begin(), names.end(), "category") != names.end());
    CHECK(std::find(names.begin(), names.end(), "rqp") != names.end());
}
