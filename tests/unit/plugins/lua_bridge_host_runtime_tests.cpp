#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "output/logger.h"
#include "plugins/lua_bridge_host_runtime.h"

#include <sol/sol.hpp>

namespace {

constexpr const char* kSilentRuntimeFlag = "__reqpack-internal-silent-runtime";

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

}  // namespace

TEST_CASE("lua bridge host runtime emits logs and transaction events", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-events"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    runtime.logDebug("demo", "debug");
    runtime.logInfo("demo", "info");
    runtime.logWarn("demo", "warn");
    runtime.logError("demo", "error");
    runtime.emitStatus("demo", 200);
    runtime.emitBeginStep("demo", "install");
    runtime.emitCommit("demo");
    runtime.emitSuccess("demo");
    runtime.emitFailure("demo", "boom");
    runtime.emitEvent("demo", "installed", "pkg");
    runtime.registerArtifact("demo", "artifact.json");

    DisplayProgressMetrics metrics;
    metrics.percent = 42;
    metrics.currentBytes = 16;
    metrics.totalBytes = 100;
    metrics.bytesPerSecond = 4;
    runtime.emitProgress("demo", metrics);
    runtime.emitProgress("demo", DisplayProgressMetrics{});

    const std::vector<PluginEventRecord> events = runtime.takeRecentEvents();
    REQUIRE(events.size() == 1);
    CHECK(events.front().name == "installed");
    CHECK(events.front().payload == "pkg");

    const std::vector<std::string> artifacts = runtime.takeRecentArtifacts();
    REQUIRE(artifacts.size() == 1);
    CHECK(artifacts.front() == "artifact.json");
}

TEST_CASE("lua bridge host runtime honors silent output mode", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-silent"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    runtime.setSilentRuntimeOutput(true);
    runtime.logInfo("demo", "hidden");
    runtime.emitSuccess("demo");
    runtime.registerArtifact("demo", "hidden-artifact");
    CHECK(runtime.takeRecentEvents().empty());
    CHECK(runtime.takeRecentArtifacts().size() == 1);
}

TEST_CASE("lua bridge host runtime executes commands with override", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-exec"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    runtime.setExecOverride([](const std::string& sourceId, const std::string& command) {
        return ExecResult{
            .success = true,
            .exitCode = 0,
            .stdoutText = sourceId + ":" + command,
            .stderrText = {},
        };
    });

    const ExecResult result = runtime.execute("scope:demo", "echo hi");
    CHECK(result.success);
    CHECK(result.stdoutText == "scope:demo:echo hi");
}

TEST_CASE("lua bridge host runtime downloads local file urls", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-download"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    const std::filesystem::path source = tempDir.path() / "payload.txt";
    write_file(source, "hello");
    const std::filesystem::path destination = tempDir.path() / "downloads" / "payload.txt";

    const DownloadResult result = runtime.downloadToPath("file://" + source.string(), destination.string());
    CHECK(result.success);
    CHECK(std::filesystem::exists(destination));
}

TEST_CASE("lua bridge host runtime denies execution when policy enforcement fails", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-deny"};
    ReqPackConfig config;
    config.security.requireThinLayer = true;
    config.execution.checkVirtualFileSystemWrite = true;
    Logger& logger = Logger::instance();
    PluginSecurityMetadata metadata;
    metadata.role = "package-manager";
    metadata.capabilities = {"exec"};
    metadata.writeScopes = {{.kind = "plugin-dir", .value = {}}};
    std::optional<PluginSecurityMetadata> securityMetadata = metadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    const ExecResult result = runtime.runCommand("printf blocked > /tmp/reqpack-policy-block-test.txt");
    CHECK_FALSE(result.success);
    CHECK(result.exitCode == 126);
    CHECK_FALSE(result.stderrText.empty());
}

TEST_CASE("lua bridge host runtime retains binding contexts", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-binding"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    PluginCallContext context{
        .pluginId = "demo",
        .pluginDirectory = tempDir.path().string(),
        .scriptPath = (tempDir.path() / "run.lua").string(),
        .flags = {kSilentRuntimeFlag},
    };

    const std::uint64_t contextId = runtime.retainRuntimeBindingContext(context);
    const LuaBridgeRuntimeBindingContext* binding = runtime.runtimeBindingContext(contextId);
    REQUIRE(binding != nullptr);
    CHECK(binding->pluginId == "demo");
    CHECK(binding->flags == std::vector<std::string>{kSilentRuntimeFlag});
    CHECK(runtime.hasSilentRuntimeFlag(context.flags));
    CHECK(runtime.shouldUseSilentRuntime(context.flags));
}

TEST_CASE("lua bridge host runtime executes commands with exec rules", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-rules"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);
    const sol::object rules = lua.safe_script(R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "^ok$",
                    actions = { { type = "success" } },
                },
            },
        }
    )");

    runtime.setExecOverride([](const std::string&, const std::string&) {
        return ExecResult{.success = true, .exitCode = 0, .stdoutText = "ok", .stderrText = {}};
    });

    const ExecResult result = runtime.executeCommandWithPolicy("demo", "ignored", rules, false);
    CHECK(result.success);
}

TEST_CASE("lua bridge host runtime executes exec rules without policy enforcement", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-rules-open"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);
    const sol::object rules = lua.safe_script(R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "^ok$",
                    actions = { { type = "success" } },
                },
            },
        }
    )");

    const std::filesystem::path marker = tempDir.path() / "rules-open.txt";
    const ExecResult allowed = runtime.executeCommandWithPolicy(
        "demo",
        "printf ok > " + marker.string(),
        rules,
        true
    );
    CHECK(allowed.success);
    CHECK(std::filesystem::exists(marker));
}

TEST_CASE("lua bridge host runtime denies exec rules when policy enforcement fails", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-rules-deny"};
    ReqPackConfig config;
    config.security.requireThinLayer = true;
    config.execution.checkVirtualFileSystemWrite = true;
    Logger& logger = Logger::instance();
    PluginSecurityMetadata metadata;
    metadata.role = "package-manager";
    metadata.capabilities = {"exec"};
    metadata.writeScopes = {{.kind = "plugin-dir", .value = {}}};
    std::optional<PluginSecurityMetadata> securityMetadata = metadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);
    const sol::object rules = lua.safe_script(R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "^blocked$",
                    actions = { { type = "success" } },
                },
            },
        }
    )");

    const ExecResult denied = runtime.executeCommandWithPolicy(
        "demo",
        "printf blocked > /tmp/reqpack-rules-deny-test.txt",
        rules,
        false
    );
    CHECK_FALSE(denied.success);
    CHECK(denied.exitCode == 126);
    CHECK_FALSE(denied.stderrText.empty());
}

TEST_CASE("lua bridge host runtime download appends archive suffix to destination", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-download-suffix"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    const std::filesystem::path source = tempDir.path() / "archive.tar.zst";
    write_file(source, "not-a-real-archive");
    const std::filesystem::path destination = tempDir.path() / "downloads" / "artifact";

    const DownloadResult result = runtime.downloadToPath(source.string(), destination.string());
    CHECK_FALSE(result.success);
}

TEST_CASE("lua bridge host runtime creates temp directories", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-temp"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    const std::string tempDirectory = runtime.createTempDirectory("demo");
    CHECK_FALSE(tempDirectory.empty());
    CHECK(std::filesystem::exists(tempDirectory));
    runtime.cleanupAfterShutdown();
}

TEST_CASE("lua bridge host runtime execute delegates to policy-aware command runner", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-execute-delegate"};
    ReqPackConfig config;
    Logger& logger = Logger::instance();
    std::optional<PluginSecurityMetadata> securityMetadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);

    runtime.setExecOverride([](const std::string& sourceId, const std::string& command) {
        return ExecResult{.success = true, .exitCode = 0, .stdoutText = sourceId + ":" + command, .stderrText = {}};
    });

    const ExecResult result = runtime.execute("scope:demo", "echo delegated");
    CHECK(result.success);
    CHECK(result.stdoutText == "scope:demo:echo delegated");
}

TEST_CASE("lua bridge host runtime denies execution silently when policy blocks writes", "[unit][lua_bridge_host_runtime]") {
    TempDir tempDir{"reqpack-lua-host-runtime-silent-deny"};
    ReqPackConfig config;
    config.security.requireThinLayer = true;
    config.execution.checkVirtualFileSystemWrite = true;
    Logger& logger = Logger::instance();
    PluginSecurityMetadata metadata;
    metadata.role = "package-manager";
    metadata.capabilities = {"exec"};
    metadata.writeScopes = {{.kind = "plugin-dir", .value = {}}};
    std::optional<PluginSecurityMetadata> securityMetadata = metadata;
    LuaBridgeHostRuntime runtime(logger, config, "demo", tempDir.path().string(), &securityMetadata);
    runtime.setSilentRuntimeOutput(true);

    const ExecResult result = runtime.runCommand("printf blocked > /tmp/reqpack-silent-deny-test.txt");
    CHECK_FALSE(result.success);
    CHECK(result.exitCode == 126);
    CHECK_FALSE(result.stderrText.empty());
}
