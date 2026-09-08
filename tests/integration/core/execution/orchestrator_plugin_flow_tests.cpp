#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <catch2/catch.hpp>

#include "core/config/configuration.h"
#include "core/execution/orchestrator.h"
#include "core/registry/registry.h"
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
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path);
    REQUIRE(output.is_open());
    output << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input.is_open());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void require_command_success(const std::string& command) {
    const int status = std::system(command.c_str());
    REQUIRE(status != -1);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

void init_git_repository(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    require_command_success("git -C " + escape_shell_arg(path.string()) + " init -b main");
    require_command_success("git -C " + escape_shell_arg(path.string()) +
                            " config user.email 'reqpack@test.local' && git -C " + escape_shell_arg(path.string()) +
                            " config user.name 'ReqPack Tests'");
}

void commit_all_git_repository(const std::filesystem::path& path, const std::string& message) {
    require_command_success("git -C " + escape_shell_arg(path.string()) + " add -A && git -C " +
                            escape_shell_arg(path.string()) + " commit -m " + escape_shell_arg(message));
}

void write_plugin_bundle(const std::filesystem::path& pluginRoot, const std::string& pluginName,
                         const std::string& version, const std::string& versionLabel) {
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
    write_file(pluginDirectory / "run.lua",
               "plugin = {}\n"
               "function plugin.getName() return REQPACK_PLUGIN_ID end\n"
               "function plugin.getVersion() return '" +
                   version +
                   "' end\n"
                   "function plugin.getSecurityMetadata() return { osvEcosystem = 'demo-osv', purlType = 'generic', "
                   "versionComparatorProfile = 'lexicographic' } end\n"
                   "function plugin.getRequirements() return {} end\n"
                   "function plugin.getCategories() return { 'pkg' } end\n"
                   "function plugin.getMissingPackages(packages) return packages end\n"
                   "function plugin.install(context, packages) return true end\n"
                   "function plugin.installLocal(context, path) return true end\n"
                   "function plugin.remove(context, packages) return true end\n"
                   "function plugin.update(context, packages) return true end\n"
                   "function plugin.list(context) return {} end\n"
                   "function plugin.search(context, prompt) return {} end\n"
                   "function plugin.info(context, package) return { name = package, version = '" +
                   versionLabel +
                   "' } end\n"
                   "function plugin.shutdown() return true end\n");
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");
}

void commit_plugin_source_version(const std::filesystem::path& repoPath, const std::string& pluginName,
                                  const std::string& tag, const std::string& version, const std::string& versionLabel) {
    write_plugin_bundle(repoPath, pluginName, version, versionLabel);
    commit_all_git_repository(repoPath, pluginName + " " + tag);
    require_command_success("git -C " + escape_shell_arg(repoPath.string()) + " tag -f " + escape_shell_arg(tag));
}

ReqPackConfig make_plugin_flow_config(const TempDir& tempDir, const std::filesystem::path& pipRepoPath) {
    const std::filesystem::path pluginDirectory = tempDir.path() / "plugins";
    ReqPackConfig config = default_reqpack_config();
    config.registry.remoteUrl.clear();
    config.registry.pluginDirectory = pluginDirectory.string();
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.registry.autoLoadPlugins = true;
    config.registry.shutDownPluginsOnExit = true;
    config.registry.sources["pip"] = RegistrySourceEntry {
        .source = "git+" + pipRepoPath.string(),
        .alias = false,
        .description = "pip plugin",
    };
    config.planner.autoDownloadMissingPlugins = false;
    config.planner.autoDownloadMissingDependencies = false;
    config.execution.useTransactionDb = false;
    config.interaction.interactive = false;
    return config;
}

} // namespace

TEST_CASE("orchestrator install plugin wrapper succeeds in-process", "[integration][orchestrator][plugin-flow]") {
    TempDir tempDir {"reqpack-orchestrator-plugin-install-in-process"};
    const std::filesystem::path pipRepoPath = tempDir.path() / "pip-origin";
    init_git_repository(pipRepoPath);
    commit_plugin_source_version(pipRepoPath, "pip", "v1", "1.0.0", "v1.0.0");
    commit_plugin_source_version(pipRepoPath, "pip", "v2", "1.2.0", "v1.2.0");

    const ReqPackConfig config = make_plugin_flow_config(tempDir, pipRepoPath);
    const std::filesystem::path pluginDirectory = config.registry.pluginDirectory;
    REQUIRE_FALSE(std::filesystem::exists(pluginDirectory / "pip" / "run.lua"));

    Orchestrator orchestrator({Request {.action = ActionType::INSTALL, .system = "pip"}}, config);
    REQUIRE(orchestrator.run() == 0);
    REQUIRE(std::filesystem::exists(pluginDirectory / "pip" / "run.lua"));
}

TEST_CASE("orchestrator remove plugin wrapper succeeds in-process", "[integration][orchestrator][plugin-flow]") {
    TempDir tempDir {"reqpack-orchestrator-plugin-remove-in-process"};
    const std::filesystem::path pipRepoPath = tempDir.path() / "pip-origin";
    init_git_repository(pipRepoPath);
    commit_plugin_source_version(pipRepoPath, "pip", "v1", "1.0.0", "v1.0.0");

    const ReqPackConfig config = make_plugin_flow_config(tempDir, pipRepoPath);
    const std::filesystem::path pluginDirectory = config.registry.pluginDirectory;

    {
        Registry registry(config);
        REQUIRE(registry.getDatabase()->ensureReady());
        REQUIRE(registry.loadPlugin("pip"));
        REQUIRE(std::filesystem::exists(pluginDirectory / "pip" / "run.lua"));
    }

    Orchestrator orchestrator({Request {.action = ActionType::REMOVE, .system = "pip"}}, config);
    REQUIRE(orchestrator.run() == 0);
    CHECK_FALSE(std::filesystem::exists(pluginDirectory / "pip" / "run.lua"));
}

TEST_CASE("orchestrator remove missing plugin wrapper fails in-process", "[integration][orchestrator][plugin-flow]") {
    TempDir tempDir {"reqpack-orchestrator-plugin-remove-missing-in-process"};
    const std::filesystem::path pipRepoPath = tempDir.path() / "pip-origin";
    init_git_repository(pipRepoPath);
    commit_plugin_source_version(pipRepoPath, "pip", "v1", "1.0.0", "v1.0.0");

    const ReqPackConfig config = make_plugin_flow_config(tempDir, pipRepoPath);
    Orchestrator orchestrator({Request {.action = ActionType::REMOVE, .system = "pip"}}, config);
    CHECK(orchestrator.run() != 0);
}

TEST_CASE("orchestrator update all expand refreshes git registry plugins in-process",
          "[integration][orchestrator][plugin-flow]") {
    TempDir tempDir {"reqpack-orchestrator-plugin-update-all-in-process"};
    const std::filesystem::path remoteRegistry = tempDir.path() / "remote-registry";
    const std::filesystem::path pipRepoPath = tempDir.path() / "pip-origin";
    init_git_repository(remoteRegistry);
    init_git_repository(pipRepoPath);
    commit_plugin_source_version(pipRepoPath, "pip", "v1", "1.0.0", "v1.0.0");
    commit_plugin_source_version(pipRepoPath, "pip", "v2", "1.2.0", "v1.2.0");

    write_file(remoteRegistry / "registry" / "p" / "pip.json", std::string {"{\n"
                                                                            "  \"schemaVersion\": 1,\n"
                                                                            "  \"name\": \"pip\",\n"
                                                                            "  \"source\": \"git+" +
                                                                            pipRepoPath.string() +
                                                                            "\",\n"
                                                                            "  \"description\": \"pip plugin\",\n"
                                                                            "  \"role\": \"package-manager\",\n"
                                                                            "  \"privilegeLevel\": \"none\"\n"
                                                                            "}\n"});
    commit_all_git_repository(remoteRegistry, "initial");

    ReqPackConfig config = default_reqpack_config();
    config.registry.pluginDirectory = (tempDir.path() / "plugins").string();
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.registry.remoteUrl = "git+" + remoteRegistry.string();
    config.registry.remoteBranch = "main";
    config.registry.remotePluginsPath = "registry";
    config.registry.autoLoadPlugins = true;
    config.registry.shutDownPluginsOnExit = true;
    config.planner.autoDownloadMissingPlugins = false;
    config.interaction.interactive = false;

    Request request;
    request.action = ActionType::UPDATE;
    request.flags = {"all", "__reqpack-internal-plugin-refresh-all", "__reqpack-internal-update-all-expand"};

    Orchestrator orchestrator({request}, config);
    REQUIRE(orchestrator.run() == 0);
    REQUIRE(std::filesystem::exists(config.registry.pluginDirectory + "/pip/run.lua"));
}

TEST_CASE("orchestrator install unknown plugin wrapper fails in-process", "[integration][orchestrator][plugin-flow]") {
    TempDir tempDir {"reqpack-orchestrator-plugin-install-missing-in-process"};
    ReqPackConfig config = default_reqpack_config();
    config.registry.remoteUrl.clear();
    config.registry.pluginDirectory = (tempDir.path() / "plugins").string();
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.registry.autoLoadPlugins = true;
    config.interaction.interactive = false;

    Orchestrator orchestrator({Request {.action = ActionType::INSTALL, .system = "missing-plugin"}}, config);
    CHECK(orchestrator.run() != 0);
}

TEST_CASE("orchestrator plugin wrapper refresh succeeds in-process", "[integration][orchestrator][plugin-flow]") {
    TempDir tempDir {"reqpack-orchestrator-plugin-refresh-in-process"};
    const std::filesystem::path pipRepoPath = tempDir.path() / "pip-origin";
    init_git_repository(pipRepoPath);
    commit_plugin_source_version(pipRepoPath, "pip", "v1", "1.0.0", "v1.0.0");
    commit_plugin_source_version(pipRepoPath, "pip", "v2", "1.2.0", "v1.2.0");

    const ReqPackConfig config = make_plugin_flow_config(tempDir, pipRepoPath);
    const std::filesystem::path pluginDirectory = config.registry.pluginDirectory;

    {
        Registry registry(config);
        REQUIRE(registry.getDatabase()->ensureReady());
        REQUIRE(registry.loadPlugin("pip"));
        REQUIRE(std::filesystem::exists(pluginDirectory / "pip" / "run.lua"));
    }

    Request request;
    request.action = ActionType::UPDATE;
    request.system = "pip";
    request.flags = {"all", "__reqpack-internal-plugin-refresh-all"};

    Orchestrator orchestrator({request}, config);
    REQUIRE(orchestrator.run() == 0);
    CHECK(read_file(pluginDirectory / "pip" / "run.lua").find("1.2.0") != std::string::npos);
}
