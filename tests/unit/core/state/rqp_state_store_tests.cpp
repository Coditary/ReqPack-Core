#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/state/rqp_state_store.h"

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

void write_installed_state(const std::filesystem::path& root, const std::string& name, const std::string& version, int release, int revision) {
    const std::filesystem::path stateDir = root / name / (name + "@" + version + "-" + std::to_string(release) + "+r" + std::to_string(revision));
    write_file(stateDir / "metadata.json",
        "{\n"
        "  \"formatVersion\": 1,\n"
        "  \"name\": \"" + name + "\",\n"
        "  \"version\": \"" + version + "\",\n"
        "  \"release\": " + std::to_string(release) + ",\n"
        "  \"revision\": " + std::to_string(revision) + ",\n"
        "  \"summary\": \"summary\",\n"
        "  \"description\": \"description\",\n"
        "  \"license\": \"MIT\",\n"
        "  \"architecture\": \"noarch\",\n"
        "  \"vendor\": \"ReqPack Tests\",\n"
        "  \"maintainerEmail\": \"tests@example.org\",\n"
        "  \"tags\": [\"test\"],\n"
        "  \"url\": \"https://example.test/" + name + ".rqp\"\n"
        "}\n");
    write_file(stateDir / "reqpack.lua", R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install.lua"
  }
}
)" );
    write_file(stateDir / "scripts" / "install.lua", "return true\n");
    write_file(stateDir / "source.json",
        "{\n"
        "  \"source\": \"repository\",\n"
        "  \"path\": \"" + name + "@" + version + "\",\n"
        "  \"repository\": \"file:///tmp/index.json\",\n"
        "  \"identity\": \"" + name + "@" + version + "-" + std::to_string(release) + "+r" + std::to_string(revision) + "\"\n"
        "}\n");
    write_file(stateDir / "manifest.json", "[]\n");
}

}  // namespace

TEST_CASE("rqp state store lists installed packages in stable order", "[unit][rqp_state_store][core]") {
    TempDir tempDir{"reqpack-rqp-state-list"};
    ReqPackConfig config = default_reqpack_config();
    config.rqp.statePath = tempDir.path().string();
    write_installed_state(tempDir.path(), "zeta", "1.0.0", 1, 0);
    write_installed_state(tempDir.path(), "alpha", "2.0.0", 1, 0);

    const std::vector<RqpInstalledPackage> installed = RqpStateStore(config).listInstalled();

    REQUIRE(installed.size() == 2);
    CHECK(installed[0].metadata.name == "alpha");
    CHECK(installed[1].metadata.name == "zeta");
}

TEST_CASE("rqp state store finds exact version match", "[unit][rqp_state_store][core]") {
    TempDir tempDir{"reqpack-rqp-state-find"};
    ReqPackConfig config = default_reqpack_config();
    config.rqp.statePath = tempDir.path().string();
    write_installed_state(tempDir.path(), "tool", "1.0.0", 1, 0);
    write_installed_state(tempDir.path(), "tool", "2.0.0", 1, 0);

    const std::vector<RqpInstalledPackage> installed = RqpStateStore(config).findInstalled("tool", "2.0.0");

    REQUIRE(installed.size() == 1);
    CHECK(installed.front().metadata.version == "2.0.0");
    CHECK(installed.front().source.source == "repository");
}

TEST_CASE("rqp state store finds installed package by registry request name", "[unit][rqp_state_store][core]") {
    TempDir tempDir{"reqpack-rqp-state-alias"};
    ReqPackConfig config = default_reqpack_config();
    config.rqp.statePath = tempDir.path().string();

    const std::filesystem::path stateDir = tempDir.path() / "ycallr" / "ycallr@0.1.1-1+r0";
    write_file(stateDir / "metadata.json",
        R"({
  "formatVersion": 1,
  "name": "ycallr",
  "version": "0.1.1-1+r0",
  "release": 1,
  "revision": 0,
  "summary": "summary",
  "description": "description",
  "license": "MIT",
  "architecture": "noarch",
  "vendor": "ReqPack Tests",
  "maintainerEmail": "tests@example.org",
  "tags": ["test"],
  "url": "https://example.test/ycallr.rqp"
}
)");
    write_file(stateDir / "reqpack.lua", R"(
return {
  apiVersion = 1,
  hooks = {
    install = "scripts/install.lua"
  }
}
)");
    write_file(stateDir / "scripts" / "install.lua", "return true\n");
    write_file(stateDir / "source.json",
        R"({
  "source": "repository",
  "path": "ycallr-cli@0.1.1",
  "repository": "https://example.test/ycallr-cli-index.json",
  "identity": "ycallr@0.1.1-1+r0",
  "requestName": "ycallr-cli"
}
)");
    write_file(stateDir / "manifest.json", "[]\n");

    RqpStateStore store(config);

    const std::vector<RqpInstalledPackage> byRequestName = store.findInstalled("ycallr-cli");
    REQUIRE(byRequestName.size() == 1);
    CHECK(byRequestName.front().metadata.name == "ycallr");
    CHECK(byRequestName.front().source.requestName == "ycallr-cli");

    const std::vector<RqpInstalledPackage> byManifestName = store.findInstalled("ycallr");
    REQUIRE(byManifestName.size() == 1);

    const std::vector<RqpInstalledPackage> byMixedCase = store.findInstalled("YCallr-CLI");
    REQUIRE(byMixedCase.size() == 1);

    CHECK(store.findInstalled("ycallr-cli", "9.9.9").empty());
}

TEST_CASE("rqp state store finds installed package among cached list", "[unit][rqp_state_store][core]") {
    TempDir tempDir{"reqpack-rqp-state-among"};
    ReqPackConfig config = default_reqpack_config();
    config.rqp.statePath = tempDir.path().string();
    write_installed_state(tempDir.path(), "tool", "1.0.0", 1, 0);

    RqpStateStore store(config);
    const std::vector<RqpInstalledPackage> installed = store.listInstalled();
    const std::vector<RqpInstalledPackage> matches = store.findInstalledAmong(installed, "tool", "1.0.0");

    REQUIRE(matches.size() == 1);
    CHECK(matches.front().metadata.version == "1.0.0");
}

TEST_CASE("rqp state store removes installed state and prunes empty package directory", "[unit][rqp_state_store][core]") {
    TempDir tempDir{"reqpack-rqp-state-remove"};
    ReqPackConfig config = default_reqpack_config();
    config.rqp.statePath = tempDir.path().string();
    write_installed_state(tempDir.path(), "tool", "1.0.0", 1, 0);

    RqpStateStore store(config);
    const std::vector<RqpInstalledPackage> installed = store.findInstalled("tool", "1.0.0");
    REQUIRE(installed.size() == 1);

    REQUIRE(store.removeInstalledState(installed.front()));
    CHECK_FALSE(std::filesystem::exists(tempDir.path() / "tool"));
}
