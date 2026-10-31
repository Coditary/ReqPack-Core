#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

#include "core/execution/orchestrator_internal.h"
#include "core/registry/registry_database.h"

namespace {

class TempDir {
  public:
    explicit TempDir(const std::string& prefix)
        : path_(std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {}

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

ReqPackConfig make_registry_config(const TempDir& tempDir) {
    ReqPackConfig config;
    config.registry.databasePath = (tempDir.path() / "registry-db").string();
    config.security.requireThinLayer = false;
    return config;
}

void add_registry_package(ReqPackConfig& config, const std::string& name, const std::string& repositoryUrl) {
    config.registry.sources[name] = RegistrySourceEntry {
        .source = repositoryUrl,
        .alias = false,
        .description = name + " package",
        .role = "package",
    };
}

} // namespace

TEST_CASE("rewrite_registry_package_requests expands multi-package install requests",
          "[unit][orchestrator][registry]") {
    TempDir tempDir {"reqpack-rewrite-registry-multi"};
    ReqPackConfig config = make_registry_config(tempDir);
    add_registry_package(config, "wuji-ai", "https://example.test/wuji-index.json");
    add_registry_package(config, "tempify", "https://example.test/tempify-index.json");

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());

    std::vector<Request> requests = {Request {
        .action = ActionType::INSTALL,
        .system = "rqp",
        .packages = {"wuji-ai", "tempify"},
    }};

    orchestrator_internal::rewrite_registry_package_requests(requests, &database);

    REQUIRE(requests.size() == 2);
    CHECK(requests[0].system == "rqp");
    CHECK(requests[0].packages == std::vector<std::string> {"wuji-ai"});
    CHECK(requests[1].system == "rqp");
    CHECK(requests[1].packages == std::vector<std::string> {"tempify"});

    const std::string repoFlagPrefix = "__reqpack-internal-rqp-repository=";
    REQUIRE(std::any_of(requests[0].flags.begin(), requests[0].flags.end(), [&](const std::string& flag) {
        return flag.rfind(repoFlagPrefix, 0) == 0;
    }));
    REQUIRE(std::any_of(requests[1].flags.begin(), requests[1].flags.end(), [&](const std::string& flag) {
        return flag.rfind(repoFlagPrefix, 0) == 0;
    }));
}

TEST_CASE("rewrite_registry_package_requests keeps non-registry package names in one request slice",
          "[unit][orchestrator][registry]") {
    TempDir tempDir {"reqpack-rewrite-registry-mixed"};
    ReqPackConfig config = make_registry_config(tempDir);
    add_registry_package(config, "wuji-ai", "https://example.test/wuji-index.json");

    RegistryDatabase database(config);
    REQUIRE(database.ensureReady());

    std::vector<Request> requests = {Request {
        .action = ActionType::INSTALL,
        .system = "rqp",
        .packages = {"wuji-ai", "custom-local-name"},
    }};

    orchestrator_internal::rewrite_registry_package_requests(requests, &database);

    REQUIRE(requests.size() == 2);
    CHECK(requests[0].packages == std::vector<std::string> {"wuji-ai"});
    CHECK(requests[1].system == "rqp");
    CHECK(requests[1].packages == std::vector<std::string> {"custom-local-name"});
}
