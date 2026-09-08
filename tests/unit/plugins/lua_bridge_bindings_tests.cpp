#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <sol/sol.hpp>

#include "core/config/configuration.h"
#include "core/host/host_info.h"
#include "output/logger.h"
#include "plugins/lua_bridge.h"
#include "plugins/lua_bridge_bindings.h"
#include "plugins/lua_bridge_host_runtime.h"
#include "plugins/lua_bridge_runtime.h"

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

std::filesystem::path write_plugin_bundle(const std::filesystem::path& root, const std::string& pluginName,
                                          const std::string& runScript) {
    const std::filesystem::path pluginDirectory = root / pluginName;
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
    write_file(pluginDirectory / "reqpack.lua", "return {\n  apiVersion = 1,\n  depends = {}\n}\n");
    write_file(pluginDirectory / "run.lua", runScript);
    write_file(pluginDirectory / "scripts" / "install.lua", "return true\n");
    write_file(pluginDirectory / "scripts" / "remove.lua", "return true\n");
    return pluginDirectory / "run.lua";
}

} // namespace

TEST_CASE("lua bridge bindings expose builtin and context types to lua", "[unit][lua_bridge_bindings]") {
    TempDir tempDir {"reqpack-lua-bridge-bindings"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeScriptRuntime runtime;
    LuaBridgeHostRuntime hostRuntime(logger, config, "demo", tempDir.path().string(), &securityMetadata);
    LuaBridgeBindings bindings(runtime, hostRuntime);

    bindings.registerBuiltinTypes();
    bindings.registerContextTypes();
    bindings.registerReqpackNamespace();

    PluginCallContext context {
        .pluginId = "demo",
        .pluginDirectory = tempDir.path().string(),
        .scriptPath = (tempDir.path() / "run.lua").string(),
        .host = nullptr,
        .proxy =
            ProxyConfig {
                .defaultTarget = "dnf",
                .targets = {"dnf", "apt"},
                .options = {{"arch", "x86_64"}},
            },
        .repositories =
            {
                RepositoryEntry {
                    .id = "main",
                    .url = "https://example.test/repo",
                    .priority = 10,
                    .enabled = true,
                    .type = "rpm",
                    .auth =
                        {
                            .type = RepositoryAuthType::TOKEN,
                            .username = "user",
                            .password = "pass",
                            .token = "secret",
                            .sshKey = "/tmp/key",
                            .headerName = "Authorization",
                        },
                    .validation = {.checksum = RepositoryChecksumPolicy::WARN, .tlsVerify = true},
                    .scope = {.include = {"*"}, .exclude = {"debug"}},
                    .extras =
                        {
                            {"tags", std::vector<std::string> {"stable"}},
                            {"mirror", std::string {"primary"}},
                            {"retries", 3.0},
                        },
                },
            },
        .hostInfo = HostInfoService::currentSnapshot(),
    };

    sol::state& lua = runtime.state();
    lua["ctx"] = context;
    const sol::protected_function_result result = lua.safe_script(R"(
        local pkg = Package.new()
        pkg.action = 1
        pkg.system = "dnf"
        pkg.name = "git"
        pkg.version = "2.0"
        pkg.sourcePath = "/tmp/git.rpm"
        pkg.localTarget = true
        pkg.flags = { "dry-run" }

        local request = Request.new()
        request.action = 2
        request.system = "dnf"
        request.packages = { "git" }
        request.localPath = "/tmp/git.rpm"
        request.usesLocalTarget = true

        local info = PackageInfo.new()
        info.name = "git"
        info.version = "2.0"
        info.summary = "git summary"
        info.dependencies = { "openssl" }

        local execResult = ExecResult.new()
        execResult.success = true
        execResult.exitCode = 0
        execResult.stdout = "ok"
        execResult.stderr = ""

        local repo = ctx.repositories[1]
        local proxy = ctx.proxy
        local host = ctx.host
        local reqpackHost = reqpack.host

        return pkg.name == "git"
            and request.packages[1] == "git"
            and info.summary == "git summary"
            and execResult.stdout == "ok"
            and repo.auth.token == "secret"
            and repo.auth.username == "user"
            and repo.auth.password == "pass"
            and repo.auth.sshKey == "/tmp/key"
            and repo.auth.headerName == "Authorization"
            and repo.validation.checksum == "warn"
            and repo.scope.exclude[1] == "debug"
            and repo.tags[1] == "stable"
            and repo.mirror == "primary"
            and repo.retries == 3
            and proxy.options.arch == "x86_64"
            and host.platform ~= nil
            and reqpackHost.platform ~= nil
            and ctx.log ~= nil
            and ctx.tx ~= nil
            and ctx.events ~= nil
            and ctx.artifacts ~= nil
            and ctx.exec ~= nil
            and ctx.fs ~= nil
            and ctx.net ~= nil
    )");

    REQUIRE(result.valid());
    CHECK(result.get<bool>());
}

TEST_CASE("lua bridge bindings wire context callbacks through plugin install", "[unit][lua_bridge_bindings]") {
    TempDir tempDir {"reqpack-lua-bridge-bindings-callbacks"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(tempDir.path() / "plugins" / "demo", "demo",
                                                                 R"(
plugin = {}
function plugin.getName() return "demo" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages)
  context.log.info("hello")
  context.tx.status(201)
  context.tx.progress({ percent = 10, current = 1, total = 10 })
  context.tx.begin_step("step")
  context.tx.commit()
  context.tx.success()
  context.events.updated("pkg")
  context.artifacts.register("artifact")
  local tmp = context.fs.get_tmp_dir()
  return tmp ~= nil and tmp ~= ""
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)");

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());

    PluginCallContext context {
        .pluginId = bridge.getPluginId(),
        .pluginDirectory = bridge.getPluginDirectory(),
        .scriptPath = bridge.getScriptPath(),
        .host = bridge.getRuntimeHost(),
        .hostInfo = HostInfoService::currentSnapshot(),
    };

    Package request;
    request.name = "demo";
    CHECK(bridge.install(context, {request}));
    CHECK(bridge.takeRecentEvents().size() == 1);
    CHECK(bridge.takeRecentArtifacts().size() == 1);
    CHECK(bridge.shutdown());
}

TEST_CASE("lua bridge bindings cover exec rules proxy and event surfaces", "[unit][lua_bridge_bindings]") {
    TempDir tempDir {"reqpack-lua-bridge-bindings-surfaces"};
    ReqPackConfig config;
    const std::filesystem::path scriptPath = write_plugin_bundle(tempDir.path() / "plugins" / "surfaces", "surfaces",
                                                                 R"(
plugin = {}
function plugin.getName() return "surfaces" end
function plugin.getVersion() return "1.0.0" end
function plugin.getRequirements() return {} end
function plugin.getCategories() return { "test" } end
function plugin.getMissingPackages(packages) return packages end
function plugin.install(context, packages)
  local execResult = context.exec.run("printf ok", {
    rules = {
      {
        source = "line",
        regex = "^ok$",
        actions = { { type = "success" } },
      },
    },
  })
  local downloaded = context.net.download("file://" .. context.plugin.script, context.fs.get_tmp_dir() .. "/copy.txt")
  local reqpackExec = reqpack.exec.run("printf reqpack")
  context.events.installed("pkg")
  context.events.deleted("pkg")
  context.events.updated("pkg")
  context.events.listed("pkg")
  context.events.searched("pkg")
  context.events.informed("pkg")
  context.events.outdated("pkg")
  context.events.unavailable("pkg")
  context.tx.failed("boom")
  return execResult.success and downloaded and reqpackExec.success
end
function plugin.installLocal(context, path) return true end
function plugin.remove(context, packages) return true end
function plugin.update(context, packages) return true end
function plugin.list(context) return {} end
function plugin.search(context, prompt) return {} end
function plugin.info(context, package) return { name = package, version = "1.0.0" } end
function plugin.shutdown() return true end
)");

    LuaBridge bridge(scriptPath.string(), config);
    REQUIRE(bridge.init());

    PluginCallContext context {
        .pluginId = bridge.getPluginId(),
        .pluginDirectory = bridge.getPluginDirectory(),
        .scriptPath = bridge.getScriptPath(),
        .host = bridge.getRuntimeHost(),
        .hostInfo = HostInfoService::currentSnapshot(),
    };

    Package request;
    request.name = "surfaces";
    CHECK(bridge.install(context, {request}));
    const std::vector<PluginEventRecord> events = bridge.takeRecentEvents();
    CHECK(events.size() >= 7);
    CHECK(bridge.shutdown());
}

TEST_CASE("lua bridge bindings expose nil proxy and populated host snapshot", "[unit][lua_bridge_bindings]") {
    TempDir tempDir {"reqpack-lua-bridge-bindings-proxy-nil"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeScriptRuntime runtime;
    LuaBridgeHostRuntime hostRuntime(logger, config, "demo", tempDir.path().string(), &securityMetadata);
    LuaBridgeBindings bindings(runtime, hostRuntime);
    bindings.registerBuiltinTypes();
    bindings.registerContextTypes();

    HostInfoSnapshot snapshot;
    snapshot.platform.osFamily = "linux";
    snapshot.platform.arch = "x86_64";
    snapshot.os.id = "fedora";
    snapshot.os.prettyName = "Fedora";
    snapshot.cpu.arch = "x86_64";
    snapshot.cpu.logicalCores = 8;
    snapshot.memory.totalBytes = 16ULL * 1024 * 1024 * 1024;
    snapshot.gpus.push_back(HostGpuInfo {.vendor = "Demo", .model = "GPU"});
    snapshot.storage.mounts.push_back(HostMountInfo {.mountPoint = "/", .totalBytes = 1024});
    snapshot.cache.schemaVersion = 1;
    snapshot.cache.collectedAtEpoch = 1;
    snapshot.cache.expiresAtEpoch = 2;
    snapshot.cache.refreshReason = "test";
    snapshot.cache.source = "unit";

    PluginCallContext context {
        .pluginId = "demo",
        .pluginDirectory = tempDir.path().string(),
        .scriptPath = (tempDir.path() / "run.lua").string(),
        .hostInfo = std::make_shared<HostInfoSnapshot>(snapshot),
    };

    sol::state& lua = runtime.state();
    lua["ctx"] = context;
    const sol::protected_function_result result = lua.safe_script(R"(
        return ctx.proxy == nil
            and ctx.host.platform.osFamily == "linux"
            and ctx.host.os.prettyName == "Fedora"
            and ctx.host.cpu.logicalCores == 8
            and ctx.host.memory.totalBytes ~= nil
            and ctx.host.gpus[1].vendor == "Demo"
            and ctx.host.storage.mounts[1].mountPoint == "/"
            and ctx.host.cache.source == "unit"
    )");
    REQUIRE(result.valid());
    CHECK(result.get<bool>());
}

TEST_CASE("lua bridge bindings expose detailed host kernel and storage fields", "[unit][lua_bridge_bindings]") {
    TempDir tempDir {"reqpack-lua-bridge-bindings-host-detail"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeScriptRuntime runtime;
    LuaBridgeHostRuntime hostRuntime(logger, config, "demo", tempDir.path().string(), &securityMetadata);
    LuaBridgeBindings bindings(runtime, hostRuntime);
    bindings.registerBuiltinTypes();
    bindings.registerContextTypes();

    HostInfoSnapshot snapshot;
    snapshot.platform.osFamily = "linux";
    snapshot.platform.arch = "x86_64";
    snapshot.platform.target = "linux-gnu";
    snapshot.platform.supportLevel = "supported";
    snapshot.platform.supportReason = "unit test";
    snapshot.os.family = "unix";
    snapshot.os.id = "fedora";
    snapshot.os.version = "40";
    snapshot.os.versionId = "40";
    snapshot.os.prettyName = "Fedora Linux 40";
    snapshot.os.distroId = "fedora";
    snapshot.os.distroName = "Fedora";
    snapshot.kernel.name = "Linux";
    snapshot.kernel.release = "6.1.0";
    snapshot.kernel.version = "#1 SMP";
    snapshot.cpu.arch = "x86_64";
    snapshot.cpu.vendor = "Intel";
    snapshot.cpu.model = "Core";
    snapshot.cpu.logicalCores = 8;
    snapshot.cpu.physicalCores = 4;
    snapshot.memory.totalBytes = 16ULL * 1024 * 1024 * 1024;
    snapshot.memory.availableBytes = 8ULL * 1024 * 1024 * 1024;
    snapshot.gpus.push_back(
        HostGpuInfo {.vendor = "Demo", .model = "GPU", .driverVersion = "1.2.3", .backend = "vulkan"});
    snapshot.storage.mounts.push_back(
        HostMountInfo {.device = "/dev/sda1", .mountPoint = "/", .totalBytes = 1024, .availableBytes = 512});

    PluginCallContext context {
        .pluginId = "demo",
        .pluginDirectory = tempDir.path().string(),
        .scriptPath = (tempDir.path() / "run.lua").string(),
        .hostInfo = std::make_shared<HostInfoSnapshot>(snapshot),
    };

    sol::state& lua = runtime.state();
    lua["ctx"] = context;
    const sol::protected_function_result result = lua.safe_script(R"(
        return ctx.host.platform.target == "linux-gnu"
            and ctx.host.platform.supportReason == "unit test"
            and ctx.host.os.version == "40"
            and ctx.host.kernel.release == "6.1.0"
            and ctx.host.cpu.vendor == "Intel"
            and ctx.host.memory.availableBytes ~= nil
            and ctx.host.gpus[1].driverVersion == "1.2.3"
            and ctx.host.storage.mounts[1].device == "/dev/sda1"
    )");
    REQUIRE(result.valid());
    CHECK(result.get<bool>());
}
