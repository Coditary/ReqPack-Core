#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

#include "core/execution/executor.h"

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

ReqPackConfig make_executor_graph_config(const std::filesystem::path& root) {
    ReqPackConfig config;
    config.registry.pluginDirectory = (root / "plugins").string();
    config.registry.databasePath = (root / "registry-db").string();
    config.registry.autoLoadPlugins = true;
    config.registry.shutDownPluginsOnExit = true;
    config.execution.transactionDatabasePath = (root / "transactions").string();
    config.execution.checkVirtualFileSystemWrite = false;
    config.history.historyPath = (root / "history").string();
    config.rqp.statePath = (root / "rqp-state").string();
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

Graph make_linear_graph(const std::vector<Package>& packages) {
    Graph graph;
    std::vector<Graph::vertex_descriptor> vertices;
    vertices.reserve(packages.size());
    for (const Package& package : packages) {
        vertices.push_back(boost::add_vertex(package, graph));
    }
    for (std::size_t index = 1; index < vertices.size(); ++index) {
        boost::add_edge(vertices[index - 1], vertices[index], graph);
    }
    return graph;
}

const char* FILTER_PLUGIN = R"(
plugin = {}

function plugin.getName() return "filter" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages)
  local missing = {}
  for _, package in ipairs(packages) do
    if package.name ~= "already" then
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

const char* GROUP_PLUGIN = R"(
plugin = {}

local function shell_quote(value)
  return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

function plugin.getName() return REQPACK_PLUGIN_ID end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages)
  local names = {}
  for _, package in ipairs(packages) do
    names[#names + 1] = package.name
  end
  reqpack.exec.run("printf '%s' " .. shell_quote(table.concat(names, ",")) .. " > " .. shell_quote(context.plugin.dir .. "/installed.txt"))
  return true
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

}  // namespace

TEST_CASE("executor groups packages by action and system", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-group"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "alpha", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "beta", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);
    executer.setRequestedItemCount(1, true);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "alpha", .name = "one"},
        Package{.action = ActionType::INSTALL, .system = "alpha", .name = "two"},
        Package{.action = ActionType::REMOVE, .system = "beta", .name = "three"},
    });

    CHECK(executer.execute(&graph));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "alpha" / "installed.txt"));
}

TEST_CASE("executor filters already satisfied install packages", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-filter"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "filter", FILTER_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);
    executer.setRequestedItemCount(1, true);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "filter", .name = "already"},
        Package{.action = ActionType::INSTALL, .system = "filter", .name = "needed"},
    });

    CHECK(executer.execute(&graph));
}

TEST_CASE("executor dispatches local install targets through installLocal", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-local"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    const char* LOCAL_PLUGIN = R"(
plugin = {}

function plugin.getName() return "localer" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return false end
function plugin.installLocal(context, path)
  return path ~= "" and string.match(path, "artifact%.rpm$") ~= nil
end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "localer", LOCAL_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    const std::filesystem::path localArtifact = tempDir.path() / "artifact.rpm";
    write_file(localArtifact, "rpm");

    Graph graph = make_linear_graph({
        Package{
            .action = ActionType::INSTALL,
            .system = "localer",
            .name = "artifact.rpm",
            .sourcePath = localArtifact.string(),
            .localTarget = true,
        },
    });

    CHECK(executer.execute(&graph));
}

TEST_CASE("executor schedules dependency edges between task groups", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-deps"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());
    config.execution.useTransactionDb = true;
    config.execution.deleteCommittedTransactions = false;

    add_plugin_script(tempDir.path() / "plugins", "first", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "second", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Graph graph;
    const Graph::vertex_descriptor first = boost::add_vertex(
        Package{.action = ActionType::INSTALL, .system = "first", .name = "alpha"},
        graph
    );
    const Graph::vertex_descriptor second = boost::add_vertex(
        Package{.action = ActionType::INSTALL, .system = "second", .name = "beta"},
        graph
    );
    boost::add_edge(first, second, graph);

    CHECK(executer.execute(&graph));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "first" / "installed.txt"));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "second" / "installed.txt"));
}

TEST_CASE("executor handles empty graph without failure", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-empty"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    Registry registry(config);
    Executer executer(&registry, config);

    Graph graph;
    CHECK(executer.execute(&graph));
}

TEST_CASE("executor groups ensure actions separately from install", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-ensure"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "alpha", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "beta", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);
    executer.setRequestedItemCount(1, true);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::ENSURE, .system = "alpha", .name = "ensure-me"},
        Package{.action = ActionType::INSTALL, .system = "beta", .name = "install-me"},
    });

    CHECK(executer.execute(&graph));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "alpha" / "installed.txt"));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "beta" / "installed.txt"));
}

TEST_CASE("executor skips missing package filter for remove actions", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-remove"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "filter", FILTER_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);
    executer.setRequestedItemCount(1, true);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::REMOVE, .system = "filter", .name = "present"},
    });

    CHECK(executer.execute(&graph));
}

TEST_CASE("executor initializes history manager when tracking is enabled", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-history"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());
    config.history.enabled = true;
    config.history.trackInstalled = true;

    add_plugin_script(tempDir.path() / "plugins", "filter", FILTER_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);
    executer.setRequestedItemCount(1, true);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "filter", .name = "needed"},
    });

    CHECK(executer.execute(&graph));
}

TEST_CASE("executor schedules parallel dependency branches before shared target", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-parallel"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "first", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "second", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "third", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Graph graph;
    const Graph::vertex_descriptor first = boost::add_vertex(
        Package{.action = ActionType::INSTALL, .system = "first", .name = "alpha"},
        graph
    );
    const Graph::vertex_descriptor second = boost::add_vertex(
        Package{.action = ActionType::INSTALL, .system = "second", .name = "beta"},
        graph
    );
    const Graph::vertex_descriptor third = boost::add_vertex(
        Package{.action = ActionType::INSTALL, .system = "third", .name = "gamma"},
        graph
    );
    boost::add_edge(first, third, graph);
    boost::add_edge(second, third, graph);

    CHECK(executer.execute(&graph));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "third" / "installed.txt"));
}

TEST_CASE("executor honors internal ensure order when grouping tasks", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-ensure-order"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "alpha", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "beta", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Package first;
    first.action = ActionType::ENSURE;
    first.system = "beta";
    first.name = "later";
    first.flags = {"__reqpack-internal-ensure-order=1"};

    Package second;
    second.action = ActionType::ENSURE;
    second.system = "alpha";
    second.name = "earlier";
    second.flags = {"__reqpack-internal-ensure-order=0"};

    Graph graph = make_linear_graph({second, first});
    CHECK(executer.execute(&graph));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "alpha" / "installed.txt"));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "beta" / "installed.txt"));
}

TEST_CASE("executor keeps gateway systems during missing-package filtering", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-gateway-filter"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());
    config.security.gateways["snyk"] = SecurityGatewayConfig{.enabled = true, .backends = {"osv"}};

    const char* gatewayPlugin = R"(
plugin = {}

local function shell_quote(value)
  return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

function plugin.getName() return REQPACK_PLUGIN_ID end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages) return {} end
function plugin.install(context, packages)
  reqpack.exec.run("printf gateway > " .. shell_quote(context.plugin.dir .. "/gateway.txt"))
  return true
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";
    add_plugin_script(tempDir.path() / "plugins", "snyk", gatewayPlugin);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "snyk", .name = "pkg"},
    });

    CHECK(executer.execute(&graph));
}

TEST_CASE("executor propagates plugin load failures for unknown systems", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-missing-plugin"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    Registry registry(config);
    Executer executer(&registry, config);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "missing-system", .name = "pkg"},
    });

    CHECK_FALSE(executer.execute(&graph));
}

TEST_CASE("executor groups install and update actions into separate task groups", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-update-group"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "alpha", GROUP_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "beta", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "alpha", .name = "install-me"},
        Package{.action = ActionType::UPDATE, .system = "beta", .name = "update-me"},
    });

    CHECK(executer.execute(&graph));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "alpha" / "installed.txt"));
}

TEST_CASE("executor skips install groups when all packages are already present", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-all-installed"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    static constexpr const char* FILTER_ALL_PLUGIN = R"(
plugin = {}
function plugin.getName() return REQPACK_PLUGIN_ID end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg" } end
function plugin.getMissingPackages(packages) return {} end
function plugin.install(context, packages)
  reqpack.exec.run("printf unexpected > " .. context.plugin.dir .. "/installed.txt")
  return true
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "filtered", FILTER_ALL_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::INSTALL, .system = "filtered", .name = "already-there"},
    });

    CHECK(executer.execute(&graph));
    CHECK_FALSE(std::filesystem::exists(tempDir.path() / "plugins" / "filtered" / "installed.txt"));
}

TEST_CASE("executor succeeds on empty task graph", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-empty"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    Registry registry(config);
    Executer executer(&registry, config);

    Graph graph;
    CHECK(executer.execute(&graph));
}

TEST_CASE("executor executes remove actions without missing-package filtering", "[unit][executor_task_graph]") {
    TempDir tempDir{"reqpack-executor-graph-remove"};
    ReqPackConfig config = make_executor_graph_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "alpha", GROUP_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Graph graph = make_linear_graph({
        Package{.action = ActionType::REMOVE, .system = "alpha", .name = "gone"},
    });

    CHECK(executer.execute(&graph));
}
