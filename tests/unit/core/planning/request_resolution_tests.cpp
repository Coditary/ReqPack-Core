#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch.hpp>

#include "core/planning/request_resolution.h"
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

ReqPackConfig make_resolution_test_config(const std::filesystem::path& root) {
    ReqPackConfig config;
    config.registry.pluginDirectory = (root / "plugins").string();
    config.registry.databasePath = (root / "registry-db").string();
    config.registry.autoLoadPlugins = true;
    config.registry.shutDownPluginsOnExit = true;
    config.planner.enableProxyExpansion = true;
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

const char* TARGET_PLUGIN = R"(
plugin = {}

function plugin.getName() return "target" end
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

const char* PROXY_PLUGIN = R"(
plugin = {}

function plugin.getName() return "proxy" end
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
function plugin.resolveProxyRequest(context, request)
  return {
    targetSystem = "target",
    packages = request.packages,
    flags = { "--proxied" },
  }
end
function plugin.shutdown() return true end
)";

}  // namespace

TEST_CASE("request resolution returns request unchanged when proxy expansion is disabled", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-disabled"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.enableProxyExpansion = false;

    Registry registry(config);
    RequestResolutionService service(&registry, config);

    const Request request{
        .action = ActionType::INSTALL,
        .system = "proxy",
        .packages = {"alpha"},
    };

    const std::optional<Request> resolved = service.resolveRequest(request);
    REQUIRE(resolved.has_value());
    CHECK(resolved->system == "proxy");
    CHECK(resolved->packages == std::vector<std::string>{"alpha"});
}

TEST_CASE("request resolution returns request unchanged when system is empty", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-empty-system"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());

    Registry registry(config);
    RequestResolutionService service(&registry, config);

    const Request request{
        .action = ActionType::INSTALL,
        .packages = {"alpha"},
    };

    const std::optional<Request> resolved = service.resolveRequest(request);
    REQUIRE(resolved.has_value());
    CHECK(resolved->system.empty());
    CHECK(resolved->packages == std::vector<std::string>{"alpha"});
}

TEST_CASE("request resolution resolves configured system aliases through registry", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-alias"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.systemAliases["lookup"] = "target";

    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const Request request{
        .action = ActionType::INSTALL,
        .system = "lookup",
        .packages = {"alpha"},
    };

    const std::optional<Request> resolved = service.resolveRequest(request);
    REQUIRE(resolved.has_value());
    CHECK(resolved->system == "target");
    CHECK(resolved->packages == std::vector<std::string>{"alpha"});
}

TEST_CASE("request resolution expands proxy plugin into target system", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-proxy"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.proxies["proxy"].defaultTarget = "target";
    config.planner.proxies["proxy"].targets = {"target"};

    add_plugin_script(tempDir.path() / "plugins", "proxy", PROXY_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const Request request{
        .action = ActionType::SEARCH,
        .system = "proxy",
        .packages = {"alpha", "beta"},
    };

    const std::optional<Request> resolved = service.resolveRequest(request);
    REQUIRE(resolved.has_value());
    CHECK(resolved->system == "target");
    CHECK(resolved->packages == std::vector<std::string>{"alpha", "beta"});
    CHECK(resolved->flags == std::vector<std::string>{"--proxied"});
}

TEST_CASE("request resolution reports cycle when proxy resolves to itself", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-cycle"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
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
function plugin.resolveProxyRequest(context, request)
  return { targetSystem = "loop", packages = request.packages }
end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "loop", LOOP_PROXY);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const Request request{
        .action = ActionType::INSTALL,
        .system = "loop",
        .packages = {"alpha"},
    };

    std::string errorMessage;
    const std::optional<Request> resolved = service.resolveRequest(request, &errorMessage);
    CHECK_FALSE(resolved.has_value());
    CHECK(errorMessage == "proxy 'loop' resolved to itself");
}

TEST_CASE("request resolution resolves multiple requests and stops on first failure", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-batch"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
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
function plugin.resolveProxyRequest(context, request)
  return { targetSystem = "loop", packages = request.packages }
end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);
    add_plugin_script(tempDir.path() / "plugins", "loop", LOOP_PROXY);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const std::vector<Request> requests{
        Request{.action = ActionType::INSTALL, .system = "target", .packages = {"ok"}},
        Request{.action = ActionType::INSTALL, .system = "loop", .packages = {"fail"}},
    };

    std::string errorMessage;
    const std::optional<std::vector<Request>> resolved = service.resolveRequests(requests, &errorMessage);
    CHECK_FALSE(resolved.has_value());
    CHECK(errorMessage == "proxy 'loop' resolved to itself");
}

TEST_CASE("request resolution resolves successful request batches", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-batch-success"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.systemAliases["lookup"] = "target";

    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const std::vector<Request> requests{
        Request{.action = ActionType::INSTALL, .system = "lookup", .packages = {"alpha"}},
        Request{.action = ActionType::SEARCH, .system = "target", .packages = {"beta"}},
    };

    const std::optional<std::vector<Request>> resolved = service.resolveRequests(requests);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 2);
    CHECK(resolved->at(0).system == "target");
    CHECK(resolved->at(0).packages == std::vector<std::string>{"alpha"});
    CHECK(resolved->at(1).system == "target");
}

TEST_CASE("request resolution rejects proxy resolving to unknown target", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-unknown-target"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.proxies["proxy"].defaultTarget = "target";
    config.planner.proxies["proxy"].targets = {"target"};

    const char* UNKNOWN_TARGET_PROXY = R"(
plugin = {}

function plugin.getName() return "proxy" end
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
function plugin.resolveProxyRequest(context, request)
  return { targetSystem = "missing-target", packages = request.packages }
end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "proxy", UNKNOWN_TARGET_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const Request request{.action = ActionType::INSTALL, .system = "proxy", .packages = {"alpha"}};
    std::string errorMessage;
    const std::optional<Request> resolved = service.resolveRequest(request, &errorMessage);
    CHECK_FALSE(resolved.has_value());
    CHECK(errorMessage == "proxy 'proxy' resolved to unknown target 'missing-target'");
}

TEST_CASE("request resolution rejects proxy returning packages and localPath", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-conflict"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.proxies["proxy"].defaultTarget = "target";
    config.planner.proxies["proxy"].targets = {"target"};

    const char* CONFLICT_PROXY = R"(
plugin = {}

function plugin.getName() return "proxy" end
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
function plugin.resolveProxyRequest(context, request)
  return { targetSystem = "target", packages = request.packages, localPath = "/tmp/demo.rpm" }
end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "proxy", CONFLICT_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const Request request{.action = ActionType::INSTALL, .system = "proxy", .packages = {"alpha"}};
    std::string errorMessage;
    const std::optional<Request> resolved = service.resolveRequest(request, &errorMessage);
    CHECK_FALSE(resolved.has_value());
    CHECK(errorMessage == "proxy 'proxy' returned both packages and localPath");
}

TEST_CASE("request resolution rejects excessive proxy depth", "[unit][request_resolution]") {
    TempDir tempDir{"reqpack-request-resolution-depth"};
    ReqPackConfig config = make_resolution_test_config(tempDir.path());
    config.planner.proxies["hop1"].defaultTarget = "hop2";
    config.planner.proxies["hop1"].targets = {"hop2"};
    config.planner.proxies["hop2"].defaultTarget = "hop3";
    config.planner.proxies["hop2"].targets = {"hop3"};
    config.planner.proxies["hop3"].defaultTarget = "hop4";
    config.planner.proxies["hop3"].targets = {"hop4"};
    config.planner.proxies["hop4"].defaultTarget = "hop5";
    config.planner.proxies["hop4"].targets = {"hop5"};
    config.planner.proxies["hop5"].defaultTarget = "target";
    config.planner.proxies["hop5"].targets = {"target"};

    const char* CHAIN_PROXY = R"(
plugin = {}

function plugin.getName() return REQPACK_PLUGIN_ID end
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
function plugin.resolveProxyRequest(context, request)
  local next = ({ hop1 = "hop2", hop2 = "hop3", hop3 = "hop4", hop4 = "hop5", hop5 = "target" })[REQPACK_PLUGIN_ID]
  return { targetSystem = next, packages = request.packages }
end
function plugin.shutdown() return true end
)";

    add_plugin_script(tempDir.path() / "plugins", "hop1", CHAIN_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "hop2", CHAIN_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "hop3", CHAIN_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "hop4", CHAIN_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "hop5", CHAIN_PROXY);
    add_plugin_script(tempDir.path() / "plugins", "target", TARGET_PLUGIN);

    Registry registry(config);
    registry.scanDirectory(config.registry.pluginDirectory);
    RequestResolutionService service(&registry, config);

    const Request request{.action = ActionType::INSTALL, .system = "hop1", .packages = {"alpha"}};
    std::string errorMessage;
    const std::optional<Request> resolved = service.resolveRequest(request, &errorMessage);
    CHECK_FALSE(resolved.has_value());
    CHECK(errorMessage.find("proxy resolution depth exceeded") != std::string::npos);
}
