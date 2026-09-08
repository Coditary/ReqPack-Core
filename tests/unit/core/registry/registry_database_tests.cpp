#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/registry/registry_database.h"
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

void write_plugin_bundle(const std::filesystem::path& root, const std::string& pluginName, const std::string& script) {
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
    write_file(pluginDirectory / "run.lua", script);
}

const char* PAYLOAD_PLUGIN_SCRIPT = R"(
plugin = {}

function plugin.getName() return "payload-demo" end
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

} // namespace

TEST_CASE("registry database validates plugin script payloads", "[unit][registry_database][payload]") {
    CHECK(registry_database_is_valid_plugin_script("return { getName = function() return 'x' end }"));
    CHECK_FALSE(registry_database_is_valid_plugin_script("   \n\t  "));
    CHECK_FALSE(registry_database_is_valid_plugin_script("<!DOCTYPE html><html><body>oops</body></html>"));
    CHECK_FALSE(registry_database_is_valid_plugin_script("<html><head></head><body>oops</body></html>"));
}

TEST_CASE("registry database identifies git and non-git sources", "[unit][registry_database][source]") {
    CHECK(registry_database_is_git_source("git+https://github.com/org/repo.git"));
    CHECK(registry_database_is_git_source("git@github.com:org/repo.git"));
    CHECK(registry_database_is_git_source("ssh://git@github.com/org/repo.git"));
    CHECK(registry_database_is_git_source("https://github.com/org/repo.git?ref=main"));
    CHECK(registry_database_is_git_source("https://github.com/org/repo"));
    CHECK(registry_database_is_git_source("https://github.com/org/repo?ref=main"));
    CHECK_FALSE(registry_database_is_git_source("https://example.test/plugin.lua"));
    CHECK_FALSE(registry_database_is_git_source("/tmp/plugin.lua"));
}

TEST_CASE("registry database normalizes git source url and strips query fragments",
          "[unit][registry_database][source]") {
    CHECK(registry_database_git_source_url("git+https://github.com/org/repo.git") == "https://github.com/org/repo.git");
    CHECK(registry_database_git_source_url("https://github.com/org/repo.git") == "https://github.com/org/repo.git");
    CHECK(registry_database_git_source_url("https://github.com/org/repo?ref=main") == "https://github.com/org/repo");
    CHECK(registry_database_git_source_url("git+https://github.com/org/repo.git@main") ==
          "https://github.com/org/repo.git");
    CHECK(registry_database_strip_query_fragment("https://github.com/org/repo.git?ref=main#frag") ==
          "https://github.com/org/repo.git");
    CHECK(registry_database_git_source_ref("git+https://github.com/org/repo.git?ref=v1.2.3") == "v1.2.3");
    CHECK(registry_database_git_source_ref("https://github.com/org/repo.git#v2.0.0") == "v2.0.0");
    CHECK(registry_database_git_source_ref("https://github.com/org/repo?ref=v1.2.3") == "v1.2.3");
    CHECK(registry_database_git_source_ref("git+https://github.com/org/repo.git@main") == "main");
    CHECK(registry_database_git_source_with_ref("git+https://github.com/org/repo.git", "v1.2.3") ==
          "git+https://github.com/org/repo.git?ref=v1.2.3");
    CHECK(registry_database_git_source_with_ref("https://github.com/org/repo", "v1.2.3") ==
          "https://github.com/org/repo?ref=v1.2.3");
}

TEST_CASE("registry database extracts git tags from ls-remote output", "[unit][registry_database][source]") {
    const std::vector<std::string> tags = registry_database_extract_git_tags("abc123\trefs/tags/v1.0.0\n"
                                                                             "def456\trefs/tags/v1.1.0\n");

    REQUIRE(tags.size() == 2);
    CHECK(tags[0] == "v1.0.0");
    CHECK(tags[1] == "v1.1.0");
}

TEST_CASE("registry database cache path derivation is stable for source and plugin name",
          "[unit][registry_database][source]") {
    ReqPackConfig config;
    config.registry.databasePath = "/tmp/reqpack-registry";

    const std::filesystem::path first =
        registry_database_git_repository_cache_path(config, "git+https://github.com/org/repo.git", "dnf");
    const std::filesystem::path second =
        registry_database_git_repository_cache_path(config, "git+https://github.com/org/repo.git", "dnf");
    const std::filesystem::path differentPlugin =
        registry_database_git_repository_cache_path(config, "git+https://github.com/org/repo.git", "maven");
    const std::filesystem::path differentSource =
        registry_database_git_repository_cache_path(config, "git+https://github.com/org/other.git", "dnf");

    CHECK(first == second);
    CHECK(first.parent_path() == default_reqpack_repo_cache_path());
    CHECK(first.filename() != differentPlugin.filename());
    CHECK(first.filename() != differentSource.filename());
}

TEST_CASE("registry database thin-layer trust requires metadata and pinned git refs",
          "[unit][registry_database][trust]") {
    ReqPackConfig config;
    config.security.requireThinLayer = true;

    RegistryRecord record;
    record.name = "dnf";
    record.source = "git+https://github.com/org/repo.git";
    record.role = "package-manager";
    record.privilegeLevel = "none";

    CHECK_FALSE(registry_record_passes_thin_layer_trust(config, record));

    record.source = "git+https://github.com/org/repo.git?ref=v1.2.3";
    record.scriptSha256 = std::string(64, 'a');
    CHECK(registry_record_passes_thin_layer_trust(config, record));

    record.role.clear();
    CHECK_FALSE(registry_record_passes_thin_layer_trust(config, record));

    config.security.requireThinLayer = false;
    CHECK(registry_record_passes_thin_layer_trust(config, record));
}

TEST_CASE("registry database verifies expected script and bootstrap hashes", "[unit][registry_database][trust]") {
    RegistryRecord record;
    record.name = "dnf";
    record.script = "return { getName = function() return 'dnf' end }\n";
    record.bootstrapScript = "print('boot')\n";
    record.scriptSha256 = registry_database_sha256_hex(record.script);
    record.bootstrapSha256 = registry_database_sha256_hex(record.bootstrapScript);

    CHECK(registry_record_matches_expected_hashes(record));

    record.scriptSha256 = std::string(64, '0');
    CHECK_FALSE(registry_record_matches_expected_hashes(record));

    record.scriptSha256 = registry_database_sha256_hex(record.script);
    record.bootstrapSha256 = std::string(64, '1');
    CHECK_FALSE(registry_record_matches_expected_hashes(record));
}

TEST_CASE("registry database record serialization round-trips escaped fields",
          "[unit][registry_database][serialization]") {
    RegistryRecord record;
    record.name = "dnf";
    record.source = "https://example.test/plugin.lua";
    record.alias = false;
    record.originPath = "registry/d/dnf.json";
    record.description = "line1\nline2";
    record.role = "package-manager";
    record.capabilities = {"exec", "network"};
    record.ecosystemScopes = {"demo-osv", "rubygems"};
    record.writeScopes = {{.kind = "temp", .value = {}}, {.kind = "user-home-subpath", .value = ".cache/demo"}};
    record.networkScopes = {{.host = "api.osv.dev", .scheme = "https", .pathPrefix = "/v1"}};
    record.privilegeLevel = "sudo";
    record.scriptSha256 = std::string(64, 'a');
    record.bootstrapSha256 = std::string(64, 'b');
    record.script = "return {}\n";
    record.bootstrapScript = "print('boot\\strap')\n";
    record.bundleSource = true;
    record.bundlePath = "/tmp/plugins\\dnf";

    const std::string payload = registry_database_serialize_record(record);
    const std::optional<RegistryRecord> parsed = registry_database_deserialize_record("dnf", payload);

    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "dnf");
    CHECK(parsed->source == record.source);
    CHECK_FALSE(parsed->alias);
    CHECK(parsed->originPath == record.originPath);
    CHECK(parsed->description == record.description);
    CHECK(parsed->role == record.role);
    CHECK(parsed->capabilities == record.capabilities);
    CHECK(parsed->ecosystemScopes == record.ecosystemScopes);
    CHECK(parsed->writeScopes.size() == record.writeScopes.size());
    CHECK(parsed->writeScopes[0].kind == record.writeScopes[0].kind);
    CHECK(parsed->writeScopes[0].value == record.writeScopes[0].value);
    CHECK(parsed->writeScopes[1].kind == record.writeScopes[1].kind);
    CHECK(parsed->writeScopes[1].value == record.writeScopes[1].value);
    CHECK(parsed->networkScopes.size() == record.networkScopes.size());
    CHECK(parsed->networkScopes[0].host == record.networkScopes[0].host);
    CHECK(parsed->networkScopes[0].scheme == record.networkScopes[0].scheme);
    CHECK(parsed->networkScopes[0].pathPrefix == record.networkScopes[0].pathPrefix);
    CHECK(parsed->privilegeLevel == record.privilegeLevel);
    CHECK(parsed->scriptSha256 == record.scriptSha256);
    CHECK(parsed->bootstrapSha256 == record.bootstrapSha256);
    CHECK(parsed->script == record.script);
    CHECK(parsed->bootstrapScript == record.bootstrapScript);
    CHECK(parsed->bundleSource);
    CHECK(parsed->bundlePath == record.bundlePath);
}

TEST_CASE("registry database deserializer rejects bad payload shapes", "[unit][registry_database][serialization]") {
    CHECK_FALSE(registry_database_deserialize_record(
                    "dnf", "source=https://example.test/"
                           "plugin.lua\nalias=0\ndescription=x\nbundleSource=0\nbundlePath=\nbootstrap=\nreturn {}")
                    .has_value());

    CHECK_FALSE(
        registry_database_deserialize_record(
            "dnf", "source=https://example.test/"
                   "plugin.lua\nalias=maybe\ndescription=x\nbundleSource=0\nbundlePath=\nbootstrap=\n---\nreturn {}")
            .has_value());

    CHECK_FALSE(registry_database_deserialize_record(
                    "dnf", "source=\nalias=0\ndescription=x\nbundleSource=0\nbundlePath=\nbootstrap=\n---\nreturn {}")
                    .has_value());

    CHECK_FALSE(
        registry_database_deserialize_record("dnf", "source=https://example.test/"
                                                    "plugin.lua\nalias=0\ndescription=x\nbundleSource=0\nbundlePath="
                                                    "\nbootstrap=\n---\n<!DOCTYPE html><html></html>")
            .has_value());
}

TEST_CASE("registry database alias records may omit script payload", "[unit][registry_database][serialization]") {
    const std::string payload = "source=apt\n"
                                "alias=1\n"
                                "description=Alias\n"
                                "bundleSource=0\n"
                                "bundlePath=\n"
                                "bootstrap=\n"
                                "---\n";

    const std::optional<RegistryRecord> parsed = registry_database_deserialize_record("yum", payload);
    REQUIRE(parsed.has_value());
    CHECK(parsed->alias);
    CHECK(parsed->source == "apt");
    CHECK(parsed->script.empty());
}

TEST_CASE("registry database stores sync metadata in dedicated meta db", "[unit][registry_database][meta]") {
    TempDir tempDir{"reqpack-registry-meta"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    config.registry.sources["yum"] = RegistrySourceEntry{
        .source = "dnf",
        .alias = true,
        .description = "Alias",
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.getMetaValue("lastCommit").has_value());
    REQUIRE(database.putMetaValue("lastCommit", "abc123"));
    REQUIRE(database.putMetaValue("branch", "main"));

    REQUIRE(database.getRecord("yum").has_value());
    CHECK(database.getRecord("yum")->source == "dnf");
    REQUIRE(database.getMetaValue("lastCommit").has_value());
    CHECK(database.getMetaValue("lastCommit").value() == "abc123");
    REQUIRE(database.getMetaValue("branch").has_value());
    CHECK(database.getMetaValue("branch").value() == "main");

    RegistryDatabase reopened(config);
    REQUIRE(reopened.ensureReady());
    REQUIRE(reopened.getRecord("yum").has_value());
    REQUIRE(reopened.getMetaValue("lastCommit").has_value());
    CHECK(reopened.getMetaValue("lastCommit").value() == "abc123");
    REQUIRE(reopened.getMetaValue("branch").has_value());
    CHECK(reopened.getMetaValue("branch").value() == "main");
}

TEST_CASE("registry database stores last successful sync timestamp meta key", "[unit][registry_database][meta]") {
    TempDir tempDir{"reqpack-registry-sync-meta"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    REQUIRE(database.putMetaValue("lastSuccessfulSyncAt", "1700000000"));
    REQUIRE(database.getMetaValue("lastSuccessfulSyncAt").has_value());
    CHECK(database.getMetaValue("lastSuccessfulSyncAt").value() == "1700000000");
}

TEST_CASE("registry database refreshRecord loads script files with bootstrap", "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-script"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    const std::filesystem::path scriptPath = tempDir.path() / "standalone.lua";
    const std::string script = "return { getName = function() return 'standalone' end }\n";
    const std::string bootstrap = "print('bootstrap')\n";
    write_file(scriptPath, script);
    write_file(scriptPath.parent_path() / "bootstrap.lua", bootstrap);
    config.registry.sources["standalone"] = RegistrySourceEntry{
        .source = scriptPath.string(),
        .alias = false,
        .description = "standalone script",
        .role = "package-manager",
        .scriptSha256 = registry_database_sha256_hex(script),
        .bootstrapSha256 = registry_database_sha256_hex(bootstrap),
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    const std::optional<RegistryRecord> refreshed = database.refreshRecord("standalone");
    REQUIRE(refreshed.has_value());
    CHECK(refreshed->script == script);
    CHECK(refreshed->bootstrapScript == bootstrap);
    const std::optional<RegistryRecord> latest = database.refreshRecord("standalone", true);
    REQUIRE(latest.has_value());
    CHECK(latest->script == script);
}

TEST_CASE("registry database refreshRecord rejects invalid script sources", "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-invalid"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    const std::filesystem::path scriptPath = tempDir.path() / "invalid.lua";
    write_file(scriptPath, "<html><body>not a plugin</body></html>");
    config.registry.sources["invalid"] = RegistrySourceEntry{
        .source = scriptPath.string(),
        .alias = false,
        .description = "invalid script",
        .role = "package-manager",
        .scriptSha256 = std::string(64, 'a'),
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.refreshRecord("invalid").has_value());
}

TEST_CASE("registry database refreshRecord preserves alias and package entries", "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-alias"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    config.registry.sources["alias-demo"] = RegistrySourceEntry{
        .source = "target",
        .alias = true,
        .description = "Alias plugin",
    };
    config.registry.sources["pkg-demo"] = RegistrySourceEntry{
        .source = "https://example.test/pkg.rqp",
        .alias = false,
        .description = "Registry package",
        .role = "package",
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());

    const std::optional<RegistryRecord> aliasRecord = database.getRecord("alias-demo");
    REQUIRE(aliasRecord.has_value());
    CHECK(aliasRecord->alias);
    CHECK(aliasRecord->source == "target");

    const std::optional<RegistryRecord> refreshedPackage = database.refreshRecord("pkg-demo");
    REQUIRE(refreshedPackage.has_value());
    CHECK(refreshedPackage->script.empty());
    CHECK(refreshedPackage->bootstrapScript.empty());
    CHECK_FALSE(refreshedPackage->bundleSource);
    CHECK(refreshedPackage->bundlePath.empty());
}

TEST_CASE("registry database refreshRecord rejects hash mismatches", "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-hash"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    const std::filesystem::path sourceRoot = tempDir.path() / "sources";
    write_plugin_bundle(sourceRoot, "hash-demo", PAYLOAD_PLUGIN_SCRIPT);
    config.registry.sources["hash-demo"] = RegistrySourceEntry{
        .source = sourceRoot.string(),
        .alias = false,
        .description = "hash demo",
        .role = "package-manager",
        .scriptSha256 = std::string(64, '0'),
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.refreshRecord("hash-demo").has_value());
}

TEST_CASE("registry database refreshRecord loads file url plugin sources", "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-file-url"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    const std::filesystem::path scriptPath = tempDir.path() / "remote.lua";
    const std::string script = "return { getName = function() return 'remote' end }\n";
    write_file(scriptPath, script);
    config.registry.sources["remote"] = RegistrySourceEntry{
        .source = "file://" + scriptPath.string(),
        .alias = false,
        .description = "remote file plugin",
        .role = "package-manager",
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    const std::optional<RegistryRecord> refreshed = database.refreshRecord("remote");
    REQUIRE(refreshed.has_value());
    CHECK(refreshed->script == script);
}

TEST_CASE("registry database refreshRecord fails for unreachable git plugin sources",
          "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-git-fail"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    config.registry.sources["git-plugin"] = RegistrySourceEntry{
        .source = "git+https://github.com/example/nonexistent-plugin-repo.git?ref=main",
        .alias = false,
        .description = "git plugin",
        .role = "package-manager",
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.refreshRecord("git-plugin").has_value());
}

TEST_CASE("registry database refreshRecord rejects empty sources", "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-empty"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    config.registry.sources["empty"] = RegistrySourceEntry{
        .source = "",
        .alias = false,
        .description = "empty source",
        .role = "package-manager",
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.refreshRecord("empty").has_value());
}

TEST_CASE("registry database refreshRecord loads sibling bootstrap for single-file sources",
          "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-sibling-bootstrap"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    const std::filesystem::path sourceDirectory = tempDir.path() / "sources";
    const std::filesystem::path scriptPath = sourceDirectory / "bootstrap-demo.lua";
    const std::string bootstrap = "print('bootstrap')\n";
    write_file(scriptPath, PAYLOAD_PLUGIN_SCRIPT);
    write_file(sourceDirectory / "bootstrap.lua", bootstrap);
    config.registry.sources["bootstrap-demo"] = RegistrySourceEntry{
        .source = scriptPath.string(),
        .alias = false,
        .description = "bootstrap demo",
        .role = "package-manager",
        .bootstrapSha256 = registry_database_sha256_hex(bootstrap),
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    const std::optional<RegistryRecord> refreshed = database.refreshRecord("bootstrap-demo");
    REQUIRE(refreshed.has_value());
    CHECK(refreshed->bootstrapScript == bootstrap);
}

TEST_CASE("registry database refreshRecord loads plugin lua from directory source",
          "[unit][registry_database][payload]") {
    TempDir tempDir{"reqpack-registry-payload-directory-lua"};
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    const std::filesystem::path sourceRoot = tempDir.path() / "sources";
    write_file(sourceRoot / "lua-demo.lua", PAYLOAD_PLUGIN_SCRIPT);
    config.registry.sources["lua-demo"] = RegistrySourceEntry{
        .source = sourceRoot.string(),
        .alias = false,
        .description = "lua demo",
        .role = "package-manager",
    };

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());
    const std::optional<RegistryRecord> refreshed = database.refreshRecord("lua-demo");
    REQUIRE(refreshed.has_value());
    CHECK(refreshed->script.find("getName") != std::string::npos);
    CHECK(refreshed->bootstrapScript.empty());
}
