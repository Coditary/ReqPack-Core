#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch.hpp>

#include "core/registry/registry.h"
#include "core/registry/registry_database_core.h"

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

ReqPackConfig make_registry_trust_config(const std::filesystem::path& root) {
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

const char* TRUSTED_PLUGIN = R"(
plugin = {}

function plugin.getName() return "trusted-plugin" end
function plugin.getVersion() return "1.0.0" end
function plugin.getSecurityMetadata()
  return {
    role = "package-manager",
    capabilities = { "exec" },
    ecosystemScopes = { "demo-osv" },
    writeScopes = {
      { kind = "temp" },
    },
    networkScopes = {
      { host = "api.osv.dev", scheme = "https", pathPrefix = "/v1" },
    },
    privilegeLevel = "none",
  }
end
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

}  // namespace

TEST_CASE("registry materializes database-backed plugin script on load", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-materialize"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    const std::filesystem::path materializedPath = tempDir.path() / "plugins" / "trusted" / "run.lua";
    CHECK_FALSE(std::filesystem::exists(materializedPath));

    REQUIRE(registry.loadPlugin("trusted"));
    CHECK(std::filesystem::exists(materializedPath));
    CHECK(registry.getState("trusted") == PluginState::ACTIVE);
}

TEST_CASE("registry blocks database-backed plugin when thin-layer trust metadata is missing", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-block"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
    CHECK_FALSE(std::filesystem::exists(tempDir.path() / "plugins" / "trusted" / "run.lua"));
}

TEST_CASE("registry exposes security metadata for trusted database-backed plugin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-metadata"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.enabled = true;
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    const std::optional<PluginSecurityMetadata> metadata = registry.getPluginSecurityMetadata("trusted");
    REQUIRE(metadata.has_value());
    CHECK(metadata->role == "package-manager");
    CHECK(metadata->privilegeLevel == "none");
    CHECK(metadata->capabilities == std::vector<std::string>{"exec"});
    CHECK(metadata->ecosystemScopes == std::vector<std::string>{"demo-osv"});
}

TEST_CASE("registry blocks load when thin-layer script hash mismatches", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-hash"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = (tempDir.path() / "remote-source" / "trusted.lua").string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = std::string(64, '0'),
    };

    write_file(tempDir.path() / "remote-source" / "trusted.lua", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry refreshPlugin rejects built-in rqp plugin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-refresh-builtin"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());

    Registry registry(config);
    CHECK_FALSE(registry.refreshPlugin("rqp"));
}

TEST_CASE("registry blocks load when runtime security metadata mismatches record", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-runtime-mismatch"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "security-provider",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry getPluginSecurityMetadata returns null when security is disabled", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-disabled"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.enabled = false;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());

    CHECK_FALSE(registry.getPluginSecurityMetadata("trusted").has_value());
}

TEST_CASE("registry refreshPlugin returns false for unknown plugin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-refresh-missing"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());

    Registry registry(config);
    CHECK_FALSE(registry.refreshPlugin("missing-plugin"));
}

TEST_CASE("registry refreshPlugin succeeds for trusted local plugin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-refresh-success"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    REQUIRE(registry.loadPlugin("trusted"));
    CHECK(registry.refreshPlugin("trusted"));
}

TEST_CASE("registry refreshPlugin honors preferLatestTag flag", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-refresh-tag"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    REQUIRE(registry.loadPlugin("trusted"));
    CHECK(registry.refreshPlugin("trusted", true));
}

TEST_CASE("registry passes thin-layer trust for built-in rqp plugin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-rqp-builtin"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;

    Registry registry(config);
    CHECK(registry.loadPlugin("rqp"));
    CHECK(registry.getState("rqp") == PluginState::ACTIVE);
}

TEST_CASE("registry refreshPlugin fails when refreshed script hash mismatches pin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-refresh-hash"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    REQUIRE(registry.loadPlugin("trusted"));

    write_file(cachedSource / "run.lua", std::string(TRUSTED_PLUGIN) + "-- tampered\n");
    CHECK_FALSE(registry.refreshPlugin("trusted"));
}

TEST_CASE("registry getPluginSecurityMetadata returns null when thin-layer trust fails", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-metadata-block"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.enabled = true;
    config.security.requireThinLayer = true;
    const std::filesystem::path source = tempDir.path() / "remote-source" / "weak.lua";
    config.registry.sources["weak"] = RegistrySourceEntry{
        .source = source.string(),
        .alias = false,
        .description = "weak plugin",
    };

    write_file(source, TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.getPluginSecurityMetadata("weak").has_value());
    CHECK(registry.getState("weak") == PluginState::FAILED);
}

TEST_CASE("registry getPluginSecurityMetadata returns null when script hash mismatches pin", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-metadata-hash"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.enabled = true;
    config.security.requireThinLayer = true;
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = (tempDir.path() / "remote-source" / "trusted.lua").string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = std::string(64, '0'),
    };

    write_file(tempDir.path() / "remote-source" / "trusted.lua", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.getPluginSecurityMetadata("trusted").has_value());
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry blocks load when trust record capability is missing from runtime metadata", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-capability-mismatch"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"network"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry blocks load when trust record ecosystem scope is missing from runtime metadata", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-ecosystem-mismatch"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"missing-ecosystem"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry blocks load when trust record write scope is missing from runtime metadata", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-write-scope-mismatch"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "plugin-dir", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry blocks load when trust record network scope is missing from runtime metadata", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-network-scope-mismatch"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "example.test", .scheme = "https", .pathPrefix = "/api"}},
        .privilegeLevel = "none",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry blocks load when trust record privilege level mismatches runtime metadata", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-privilege-mismatch"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path cachedSource = tempDir.path() / "remote-source" / "trusted";
    config.registry.sources["trusted"] = RegistrySourceEntry{
        .source = cachedSource.string(),
        .alias = false,
        .description = "trusted plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "elevated",
        .scriptSha256 = registry_database_sha256_hex(TRUSTED_PLUGIN),
    };

    add_plugin_script(tempDir.path() / "remote-source", "trusted", TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.loadPlugin("trusted"));
    CHECK(registry.getState("trusted") == PluginState::FAILED);
}

TEST_CASE("registry refreshPlugin fails when thin-layer trust metadata is missing", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-refresh-thin-block"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.requireThinLayer = true;
    const std::filesystem::path source = tempDir.path() / "remote-source" / "weak.lua";
    config.registry.sources["weak"] = RegistrySourceEntry{
        .source = source.string(),
        .alias = false,
        .description = "weak plugin",
    };

    write_file(source, TRUSTED_PLUGIN);

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.refreshPlugin("weak"));
}

TEST_CASE("registry getPluginSecurityMetadata returns null when database refresh fails", "[unit][registry_trust]") {
    TempDir tempDir{"reqpack-registry-trust-metadata-refresh-fail"};
    ReqPackConfig config = make_registry_trust_config(tempDir.path());
    config.security.enabled = true;
    config.security.requireThinLayer = true;
    config.registry.sources["remote"] = RegistrySourceEntry{
        .source = "git+https://invalid.example.test/plugins/remote.git",
        .alias = false,
        .description = "remote plugin",
        .role = "package-manager",
        .capabilities = {"exec"},
        .ecosystemScopes = {"demo-osv"},
        .writeScopes = {{.kind = "temp", .value = {}}},
        .networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}},
        .privilegeLevel = "none",
    };

    Registry registry(config);
    REQUIRE(registry.getDatabase()->ensureReady());
    CHECK_FALSE(registry.getPluginSecurityMetadata("remote").has_value());
    CHECK(registry.getState("remote") == PluginState::FAILED);
}
