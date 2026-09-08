#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch.hpp>

#include "plugins/iplugin.h"
#include "rqp_plugin_internal.h"

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

}  // namespace

TEST_CASE("rqp plugin runtime host exposes singleton and artifact helpers", "[unit][rqp_plugin_hooks]") {
    IPluginRuntimeHost* host = rqp_plugin_runtime_host();
    REQUIRE(host != nullptr);

    rqp_plugin_clear_runtime_host_artifacts();
    host->registerArtifact("rqp", "{\"type\": \"file\", \"path\": \"/tmp/demo\"}");
    host->registerArtifact("rqp", "{\"type\": \"dir\", \"path\": \"/tmp/demo-dir\"}");

    const std::vector<std::string> artifacts = rqp_plugin_take_runtime_host_artifacts();
    REQUIRE(artifacts.size() == 2);
    CHECK(artifacts[0].find("/tmp/demo") != std::string::npos);
    CHECK(artifacts[1].find("/tmp/demo-dir") != std::string::npos);
    CHECK(rqp_plugin_take_runtime_host_artifacts().empty());
}

TEST_CASE("rqp plugin runtime host createTempDirectory returns non-empty path", "[unit][rqp_plugin_hooks]") {
    IPluginRuntimeHost* host = rqp_plugin_runtime_host();
    REQUIRE(host != nullptr);

    const std::string created = host->createTempDirectory("unit-test");
    CHECK_FALSE(created.empty());
    CHECK(std::filesystem::exists(created));

    std::error_code error;
    std::filesystem::remove_all(created, error);
}

TEST_CASE("rqp_plugin_unique_nested_file_with_extension finds single nested match", "[unit][rqp_plugin_hooks]") {
    TempDir tempDir{"reqpack-rqp-nested-file"};
    const std::filesystem::path nested = tempDir.path() / "nested" / "pkg" / "demo.rqp";
    write_file(nested, "rqp");

    const std::optional<std::filesystem::path> match =
        rqp_plugin_unique_nested_file_with_extension(tempDir.path(), ".rqp");
    REQUIRE(match.has_value());
    CHECK(match.value() == nested);
}

TEST_CASE("rqp_plugin_unique_nested_file_with_extension returns nullopt when no match exists", "[unit][rqp_plugin_hooks]") {
    TempDir tempDir{"reqpack-rqp-nested-missing"};
    write_file(tempDir.path() / "readme.txt", "no package here");

    const std::optional<std::filesystem::path> match =
        rqp_plugin_unique_nested_file_with_extension(tempDir.path(), ".rqp");
    CHECK_FALSE(match.has_value());
}

TEST_CASE("rqp_plugin_unique_nested_file_with_extension throws on multiple matches", "[unit][rqp_plugin_hooks]") {
    TempDir tempDir{"reqpack-rqp-nested-ambiguous"};
    write_file(tempDir.path() / "alpha.rqp", "one");
    write_file(tempDir.path() / "nested" / "beta.rqp", "two");

    CHECK_THROWS_AS(
        rqp_plugin_unique_nested_file_with_extension(tempDir.path(), ".rqp"),
        std::runtime_error
    );
}
