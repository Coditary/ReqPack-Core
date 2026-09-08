#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch.hpp>

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

ReqPackConfig make_executor_update_config(const std::filesystem::path& root) {
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

const char* UPDATE_PLUGIN = R"(
plugin = {}

local function shell_quote(value)
  return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

function plugin.getName() return "update-plugin" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg", "update" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages)
  reqpack.exec.run("printf 'updated\\n' > " .. shell_quote(context.plugin.dir .. "/updated.txt"))
  return true
end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

const char* FAILING_UPDATE_PLUGIN = R"(
plugin = {}

function plugin.getName() return "failing-update" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "pkg", "update" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return false end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)";

}  // namespace

TEST_CASE("executor updateSystems skips requests with packages or local targets", "[unit][executor_update]") {
    TempDir tempDir{"reqpack-executor-update-skip"};
    ReqPackConfig config = make_executor_update_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "update", UPDATE_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    const std::vector<Request> requests{
        Request{.action = ActionType::UPDATE, .system = "update", .packages = {"git"}},
        Request{.action = ActionType::UPDATE, .system = "update", .localPath = "/tmp/pkg.rpm", .usesLocalTarget = true},
        Request{.action = ActionType::UPDATE, .system = ""},
    };

    const std::vector<bool> results = executer.updateSystems(requests);
    REQUIRE(results.size() == 3);
    CHECK_FALSE(results[0]);
    CHECK_FALSE(results[1]);
    CHECK_FALSE(results[2]);
    CHECK_FALSE(std::filesystem::exists(tempDir.path() / "plugins" / "update" / "updated.txt"));
}

TEST_CASE("executor updateSystems performs dry-run without invoking plugin update", "[unit][executor_update]") {
    TempDir tempDir{"reqpack-executor-update-dry-run"};
    ReqPackConfig config = make_executor_update_config(tempDir.path());
    config.execution.dryRun = true;

    add_plugin_script(tempDir.path() / "plugins", "update", UPDATE_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Request request;
    request.action = ActionType::UPDATE;
    request.system = "update";

    CHECK(executer.updateSystem(request));
    CHECK_FALSE(std::filesystem::exists(tempDir.path() / "plugins" / "update" / "updated.txt"));
}

TEST_CASE("executor updateSystems dispatches system-wide update to plugin", "[unit][executor_update]") {
    TempDir tempDir{"reqpack-executor-update-dispatch"};
    ReqPackConfig config = make_executor_update_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "update", UPDATE_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Request request;
    request.action = ActionType::UPDATE;
    request.system = "update";

    CHECK(executer.updateSystem(request));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "update" / "updated.txt"));
}

TEST_CASE("executor updateSystems reports failure when plugin update returns false", "[unit][executor_update]") {
    TempDir tempDir{"reqpack-executor-update-failure"};
    ReqPackConfig config = make_executor_update_config(tempDir.path());

    add_plugin_script(tempDir.path() / "plugins", "failing", FAILING_UPDATE_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    Request request;
    request.action = ActionType::UPDATE;
    request.system = "failing";

    CHECK_FALSE(executer.updateSystem(request));
}

TEST_CASE("executor updateSystems updates multiple systems independently", "[unit][executor_update]") {
    TempDir tempDir{"reqpack-executor-update-multi"};
    ReqPackConfig config = make_executor_update_config(tempDir.path());
    config.execution.jobs = 2;

    add_plugin_script(tempDir.path() / "plugins", "alpha", UPDATE_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "beta", UPDATE_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    Executer executer(&registry, config);

    const std::vector<Request> requests{
        Request{.action = ActionType::UPDATE, .system = "alpha"},
        Request{.action = ActionType::UPDATE, .system = "beta"},
    };

    const std::vector<bool> results = executer.updateSystems(requests);
    REQUIRE(results.size() == 2);
    CHECK(results[0]);
    CHECK(results[1]);
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "alpha" / "updated.txt"));
    CHECK(std::filesystem::exists(tempDir.path() / "plugins" / "beta" / "updated.txt"));
}
