#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config/configuration.h"
#include "core/host/host_info.h"
#include "plugins/lua_bridge.h"

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

std::filesystem::path write_plugin_bundle(
    const std::filesystem::path& pluginDirectory,
    const std::string& pluginId,
    const std::string& runScript
) {
    write_file(pluginDirectory / "metadata.json",
        "{\n"
        "  \"formatVersion\": 1,\n"
        "  \"name\": \"" + pluginId + "\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"summary\": \"" + pluginId + " plugin\",\n"
        "  \"description\": \"" + pluginId + " plugin bundle\",\n"
        "  \"license\": \"MIT\"\n"
        "}\n");
    write_file(pluginDirectory / "reqpack.lua", "return {\n  apiVersion = 1,\n  depends = {}\n}\n");
    write_file(pluginDirectory / "run.lua", runScript);
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");
    return pluginDirectory / "run.lua";
}

PluginCallContext make_context(LuaBridge& bridge, const ReqPackConfig& config, std::vector<std::string> flags = {}) {
    return PluginCallContext{
        .pluginId = bridge.getPluginId(),
        .pluginDirectory = bridge.getPluginDirectory(),
        .scriptPath = bridge.getScriptPath(),
        .flags = std::move(flags),
        .host = bridge.getRuntimeHost(),
        .proxy = proxy_config_for_system(config, bridge.getPluginId()),
        .repositories = repositories_for_ecosystem(config, bridge.getPluginId()),
        .hostInfo = HostInfoService::currentSnapshot(),
    };
}

const char* QUERY_PLUGIN = R"(
plugin = {}

QUERY_LABEL = "booted"
QUERY_READY = "no"

function plugin.init()
  QUERY_READY = "yes"
  return true
end

function plugin.getName() return "query-bridge" end
function plugin.getVersion() return QUERY_LABEL end
function plugin.getSecurityMetadata()
  return {
    role = "Security-Provider",
    capabilities = { "Network" },
    ecosystemScopes = { "demo-osv" },
    writeScopes = { { kind = "Temp" } },
    networkScopes = { { host = "API.OSV.DEV", scheme = "HTTPS", pathPrefix = "/v1" } },
    privilegeLevel = "None",
    osvEcosystem = "demo-osv",
    purlType = "generic",
    versionComparatorProfile = "lexicographic",
  }
end
function plugin.getRequirements()
  return {
    {
      action = "install",
      system = "dnf",
      name = "curl",
      version = "8.0",
      sourcePath = "/tmp/curl.rpm",
      localTarget = true,
      flags = { "dep-flag" },
    }
  }
end
function plugin.getCategories() return { "query", QUERY_READY } end
function plugin.getMissingPackages(packages)
  local missing = {}
  local host_os = reqpack.host.platform.osFamily or "unknown"
  for _, package in ipairs(packages) do
    missing[#missing + 1] = {
      action = package.action,
      system = package.system,
      name = package.name .. "-" .. host_os .. "-missing",
      version = package.version,
      sourcePath = package.sourcePath,
      localTarget = package.localTarget,
      flags = package.flags,
    }
  end
  return missing
end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return path ~= "" end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context)
  return {
    {
      name = QUERY_LABEL,
      version = context.flags[1],
      description = context.plugin.id,
    }
  }
end
function plugin.search(context, prompt)
  return {
    {
      name = prompt,
      version = QUERY_READY,
      type = "cli",
      architecture = "noarch",
      description = context.plugin.script,
    }
  }
end
function plugin.info(context, package)
  return {
    name = package,
    version = QUERY_LABEL,
    description = context.plugin.script,
  }
end
function plugin.shutdown() return true end
)";

const char* EXTENSIONS_PLUGIN = R"(
plugin = {}

function plugin.getName() return "extensions-bridge" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "extensions" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end

plugin.fileExtensions = { ".demo", ".pkg" }
)";

}  // namespace

TEST_CASE("lua bridge initializes plugin metadata and security fields", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-init"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(tempDir.path() / "plugins" / "query", "query", QUERY_PLUGIN);

    LuaBridge bridge(scriptPath.string(), config);
    CHECK(bridge.getName() == "query-bridge");
    CHECK(bridge.getVersion() == "booted");
    REQUIRE(bridge.getSecurityMetadata().has_value());
    CHECK(bridge.getSecurityMetadata()->role == "security-provider");
    CHECK(bridge.getSecurityMetadata()->capabilities == std::vector<std::string>{"network"});
    REQUIRE(bridge.init());

    const std::vector<std::string> categories = bridge.getCategories();
    REQUIRE(categories.size() == 2);
    CHECK(categories[0] == "query");
    CHECK(categories[1] == "yes");
}

TEST_CASE("lua bridge parses requirements and missing package names", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-requirements"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(tempDir.path() / "plugins" / "query", "query", QUERY_PLUGIN);
    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());

    const std::vector<Package> requirements = bridge.getRequirements();
    REQUIRE(requirements.size() == 1);
    CHECK(requirements[0].action == ActionType::INSTALL);
    CHECK(requirements[0].system == "dnf");
    CHECK(requirements[0].name == "curl");
    CHECK(requirements[0].version == "8.0");
    CHECK(requirements[0].sourcePath == "/tmp/curl.rpm");
    CHECK(requirements[0].localTarget);
    CHECK(requirements[0].flags == std::vector<std::string>{"dep-flag"});

    const Package requested{
        .action = ActionType::INSTALL,
        .system = "query",
        .name = "demo",
        .version = "1.2.3",
        .sourcePath = "/tmp/demo.pkg",
        .localTarget = true,
        .flags = {"flag-a"},
    };
    const std::vector<Package> missing = bridge.getMissingPackages({requested});
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].name == "demo-" + HostInfoService::currentSnapshot()->platform.osFamily + "-missing");
    CHECK(missing[0].version == "1.2.3");
}

TEST_CASE("lua bridge list search and info parse query results", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-query"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(tempDir.path() / "plugins" / "query", "query", QUERY_PLUGIN);
    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());

    const PluginCallContext context = make_context(bridge, config, {"--query-flag"});
    const std::vector<PackageInfo> listed = bridge.list(context);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].name == "booted");
    CHECK(listed[0].version == "--query-flag");
    CHECK(listed[0].description == "query");

    const std::vector<PackageInfo> searched = bridge.search(context, "alpha beta");
    REQUIRE(searched.size() == 1);
    CHECK(searched[0].name == "alpha beta");
    CHECK(searched[0].version == "yes");
    CHECK(searched[0].packageType == "cli");
    CHECK(searched[0].architecture == "noarch");
    CHECK(searched[0].description == scriptPath.string());

    const PackageInfo info = bridge.info(context, "artifact");
    CHECK(info.name == "artifact");
    CHECK(info.version == "booted");
    CHECK(info.description == scriptPath.string());
}

TEST_CASE("lua bridge installLocal accepts non-empty paths", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-install-local"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(tempDir.path() / "plugins" / "query", "query", QUERY_PLUGIN);
    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());

    const PluginCallContext context = make_context(bridge, config);
    CHECK(bridge.installLocal(context, "/tmp/demo.pkg"));
    CHECK_FALSE(bridge.installLocal(context, ""));
}

TEST_CASE("lua bridge reads fileExtensions from plugin table", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-extensions"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath =
        write_plugin_bundle(tempDir.path() / "plugins" / "extensions", "extensions", EXTENSIONS_PLUGIN);

    LuaBridge bridge(scriptPath.string(), config);
    CHECK(bridge.getFileExtensions() == std::vector<std::string>{".demo", ".pkg"});
}

TEST_CASE("lua bridge init fails for invalid plugin contract", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-bad-init"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "broken",
        "broken",
        R"(
plugin = {}
function plugin.getName() return "broken" end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    CHECK_FALSE(bridge.init());
}

TEST_CASE("lua bridge shutdown succeeds for valid plugin", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-shutdown"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "shutdown",
        "shutdown",
        R"(
plugin = {}
function plugin.getName() return "shutdown" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());
    CHECK(bridge.shutdown());
}

TEST_CASE("lua bridge exposes context bindings to plugin scripts", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-context"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "context",
        "context",
        R"(
plugin = {}
function plugin.getName() return "context" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages)
  context.log.info("installing")
  context.tx.status(200)
  context.tx.progress({ percent = 50, current = 1, total = 2 })
  context.tx.begin_step("install")
  context.tx.commit()
  context.tx.success()
  context.events.installed("pkg")
  context.artifacts.register("artifact")
  local result = context.exec.run("printf ok")
  local tmp = context.fs.get_tmp_dir()
  local downloaded = context.net.download("file://" .. context.plugin.script, tmp .. "/copy.txt")
  local hostOs = context.host.os.id
  local repoCount = #context.repositories
  return result.success and downloaded and hostOs ~= nil and repoCount >= 0
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());

    PluginCallContext context{
        .pluginId = bridge.getPluginId(),
        .pluginDirectory = bridge.getPluginDirectory(),
        .scriptPath = bridge.getScriptPath(),
        .host = bridge.getRuntimeHost(),
        .proxy = ProxyConfig{
            .defaultTarget = "dnf",
            .targets = {"dnf", "apt"},
            .options = {{"arch", "x86_64"}},
        },
        .repositories = {
            RepositoryEntry{
                .id = "main",
                .url = "https://example.test/repo",
                .priority = 1,
                .enabled = true,
                .type = "rpm",
                .auth = {},
                .validation = {.checksum = RepositoryChecksumPolicy::WARN, .tlsVerify = true},
                .scope = {.include = {"*"}, .exclude = {}},
                .extras = {{"tags", std::vector<std::string>{"stable"}}},
            },
        },
        .hostInfo = HostInfoService::currentSnapshot(),
    };

    Package request;
    request.name = "demo";
    CHECK(bridge.install(context, {request}));

    const std::vector<PluginEventRecord> events = bridge.takeRecentEvents();
    const auto installed = std::find_if(events.begin(), events.end(), [](const PluginEventRecord& event) {
        return event.name == "installed";
    });
    REQUIRE(installed != events.end());
    CHECK(bridge.takeRecentArtifacts().size() == 1);
    CHECK(bridge.shutdown());
}

TEST_CASE("lua bridge reads security metadata from plugin scripts", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-security"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "secured",
        "secured",
        R"(
plugin = {}
function plugin.getName() return "secured" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getSecurityMetadata()
  return {
    role = "package-manager",
    capabilities = { "exec" },
    writeScopes = { { kind = "plugin-dir" } },
    networkScopes = { { host = "api.example.test", scheme = "https", pathPrefix = "/v1" } },
    privilegeLevel = "none",
  }
end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    CHECK(bridge.getName() == "secured");
    CHECK(bridge.getVersion() == "1.0.0");
    REQUIRE(bridge.init());
    CHECK(bridge.shutdown());
}

TEST_CASE("lua bridge init returns false when init hook fails", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-init-fail"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "init-fail",
        "init-fail",
        R"(
plugin = {}
function plugin.getName() return "init-fail" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context, prompt) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.init() return false end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    CHECK_FALSE(bridge.init());
}

TEST_CASE("lua bridge tolerates missing script files during construction", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-missing-script"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = tempDir.path() / "plugins" / "ghost" / "run.lua";

    LuaBridge bridge(scriptPath.string(), config);
    CHECK(bridge.getPluginId() == "ghost");
    CHECK_FALSE(bridge.init());
}

TEST_CASE("lua bridge routes print output through logger", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-print"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "printer",
        "printer",
        R"(
plugin = {}
function plugin.getName() return "printer" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages)
  print("one", "two", "three")
  return true
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());
    CHECK(bridge.install(make_context(bridge, config), {Package{.name = "demo"}}));
    CHECK(bridge.shutdown());
}

TEST_CASE("lua bridge shutdown propagates plugin failure", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-shutdown-fail"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "shutdown-fail",
        "shutdown-fail",
        R"(
plugin = {}
function plugin.getName() return "shutdown-fail" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return false end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());
    CHECK_FALSE(bridge.shutdown());
}

TEST_CASE("lua bridge init reports lua errors from init hook", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-init-lua-error"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "init-error",
        "init-error",
        R"(
plugin = {}
function plugin.getName() return "init-error" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.init() error("init failed") end
function plugin.shutdown() return true end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    CHECK_FALSE(bridge.init());
}

TEST_CASE("lua bridge shutdown reports lua errors from shutdown hook", "[unit][lua_bridge_core]") {
    TempDir tempDir{"reqpack-lua-bridge-core-shutdown-lua-error"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(
        tempDir.path() / "plugins" / "shutdown-error",
        "shutdown-error",
        R"(
plugin = {}
function plugin.getName() return "shutdown-error" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages) return true end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() error("shutdown failed") end
)"
    );

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());
    CHECK_FALSE(bridge.shutdown());
}
