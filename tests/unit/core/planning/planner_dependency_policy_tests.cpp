#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/planning/planner.h"
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

ReqPackConfig make_planner_config(const std::filesystem::path& root) {
    ReqPackConfig config;
    config.registry.pluginDirectory = (root / "plugins").string();
    config.registry.databasePath = (root / "registry-db").string();
    config.registry.autoLoadPlugins = true;
    config.registry.shutDownPluginsOnExit = true;
    config.planner.autoDownloadMissingPlugins = false;
    config.planner.autoDownloadMissingDependencies = false;
    return config;
}

std::filesystem::path add_plugin_script(const std::filesystem::path& pluginRoot, const std::string& pluginName,
                                        const std::string& content,
                                        const std::vector<std::string>& dependencySpecs = {}) {
    const std::filesystem::path pluginDirectory = pluginRoot / pluginName;
    std::string manifest = "return {\n  apiVersion = 1,\n  depends = {";
    if (!dependencySpecs.empty()) {
        manifest += "\n";
        for (const std::string& dependencySpec : dependencySpecs) {
            manifest += "    \"" + dependencySpec + "\",\n";
        }
        manifest += "  ";
    }
    manifest += "}\n}\n";

    write_file(pluginDirectory / "metadata.json", "{\n"
                                                  "  \"formatVersion\": 1,\n"
                                                  "  \"name\": \"" +
                                                      pluginName +
                                                      "\",\n"
                                                      "  \"version\": \"1.0.0\",\n"
                                                      "  \"summary\": \"" +
                                                      pluginName +
                                                      " plugin\",\n"
                                                      "  \"description\": \"" +
                                                      pluginName +
                                                      " plugin bundle\",\n"
                                                      "  \"license\": \"MIT\"\n"
                                                      "}\n");
    write_file(pluginDirectory / "reqpack.lua", manifest);
    write_file(pluginDirectory / "run.lua", content);
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");
    return pluginDirectory / "run.lua";
}

bool graph_contains_package(const Graph& graph, const std::string& system, const std::string& name) {
    auto [vertex, vertexEnd] = boost::vertices(graph);
    for (; vertex != vertexEnd; ++vertex) {
        const Package& package = graph[*vertex];
        if (package.system == system && package.name == name) {
            return true;
        }
    }
    return false;
}

const char* APP_PLUGIN = R"(
plugin = {}

function plugin.getName() return "app" end
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
)";

const char* DEP_PLUGIN = R"(
plugin = {}

function plugin.getName() return "dep" end
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
)";

const char* FILTER_PLUGIN = R"(
plugin = {}

function plugin.getName() return "filter" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages)
  local missing = {}
  for _, package in ipairs(packages) do
    if package.name ~= "present" then
      missing[#missing + 1] = package
    end
  end
  return missing
end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

} // namespace

TEST_CASE("planner expands reqpack.lua depends into ensure dependency packages", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-depends"};
    ReqPackConfig config = make_planner_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "app", APP_PLUGIN, {"dep:runtime"});
    add_plugin_script(tempDir.path() / "plugins", "dep", DEP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(
        planner.plan({Request{.action = ActionType::INSTALL, .system = "app", .packages = {"demo"}}}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "app", "demo"));
    CHECK(graph_contains_package(*graph, "dep", "runtime"));
}

TEST_CASE("planner filters install requests to missing packages only", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-filter-missing"};
    ReqPackConfig config = make_planner_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "filter", FILTER_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(planner.plan({Request{
        .action = ActionType::INSTALL,
        .system = "filter",
        .packages = {"present", "missing"},
    }}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "filter", "missing"));
    CHECK_FALSE(graph_contains_package(*graph, "filter", "present"));
}

TEST_CASE("planner preserves local install targets during filtering", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-local-filter"};
    ReqPackConfig config = make_planner_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "filter", FILTER_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(planner.plan({Request{
        .action = ActionType::INSTALL,
        .system = "filter",
        .localPath = "/tmp/demo.rpm",
        .usesLocalTarget = true,
    }}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "filter", "demo.rpm"));
}

TEST_CASE("planner passes through non-install requests unchanged", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-non-install"};
    ReqPackConfig config = make_planner_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "filter", FILTER_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(planner.plan({Request{
        .action = ActionType::SEARCH,
        .system = "filter",
        .packages = {"present", "missing"},
    }}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "filter", "present"));
    CHECK(graph_contains_package(*graph, "filter", "missing"));
}

TEST_CASE("planner returns null graph when request resolution fails", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-resolution-fail"};
    ReqPackConfig config = make_planner_config(tempDir.path());
    config.planner.proxies["loop"].defaultTarget = "loop";
    config.planner.proxies["loop"].targets = {"loop"};

    const char* LOOP_PROXY = R"(
plugin = {}
function plugin.getName() return "loop" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "proxy" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.resolveProxyRequest(context, request) return { targetSystem = "loop", packages = request.packages } end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "loop", LOOP_PROXY);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    CHECK(planner.plan({Request{.action = ActionType::INSTALL, .system = "loop", .packages = {"alpha"}}}) == nullptr);
}

TEST_CASE("planner builds ensure-only dependency graphs", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-ensure-only"};
    ReqPackConfig config = make_planner_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "app", APP_PLUGIN, {"dep:runtime"});
    add_plugin_script(tempDir.path() / "plugins", "dep", DEP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(planner.plan({Request{.action = ActionType::ENSURE, .system = "app"}}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "dep", "runtime"));
}

TEST_CASE("planner auto-downloads missing dependency plugins when enabled", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-auto-download"};
    ReqPackConfig config = make_planner_config(tempDir.path());
    config.planner.autoDownloadMissingDependencies = true;

    add_plugin_script(tempDir.path() / "plugins", "app", APP_PLUGIN, {"dep:runtime"});
    const std::filesystem::path depBundle = add_plugin_script(tempDir.path() / "remote", "dep", DEP_PLUGIN);
    config.registry.sources["dep"] = RegistrySourceEntry{
        .source = depBundle.parent_path().string(),
        .alias = false,
        .description = "dep plugin source",
    };

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    REQUIRE(registry.getDatabase()->ensureReady());
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(
        planner.plan({Request{.action = ActionType::INSTALL, .system = "app", .packages = {"demo"}}}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "dep", "runtime"));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "dep" / "run.lua"));
}

TEST_CASE("planner topologically sorts graph when configured", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-topo-sort"};
    ReqPackConfig config = make_planner_config(tempDir.path());
    config.planner.topologicallySortGraph = true;

    add_plugin_script(tempDir.path() / "plugins", "app", APP_PLUGIN, {"dep:runtime"});
    add_plugin_script(tempDir.path() / "plugins", "dep", DEP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(
        planner.plan({Request{.action = ActionType::INSTALL, .system = "app", .packages = {"demo"}}}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "app", "demo"));
    CHECK(graph_contains_package(*graph, "dep", "runtime"));
}

TEST_CASE("planner passes security gateway systems through install filtering", "[unit][planner_dependency]") {
    TempDir tempDir{"reqpack-planner-gateway"};
    ReqPackConfig config = make_planner_config(tempDir.path());
    config.security.gateways["snyk"] = {"osv"};

    Registry registry(config);
    Planner planner(&registry, registry.getDatabase(), config);

    std::unique_ptr<Graph> graph(
        planner.plan({Request{.action = ActionType::INSTALL, .system = "snyk", .packages = {"pkg"}}}));
    REQUIRE(graph != nullptr);
    CHECK(graph_contains_package(*graph, "snyk", "pkg"));
}
