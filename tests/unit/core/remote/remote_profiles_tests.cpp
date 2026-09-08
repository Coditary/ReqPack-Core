#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch.hpp>

#include "core/remote/remote_profiles.h"

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

} // namespace

TEST_CASE("load_remote_profiles parses host port protocol and credentials", "[unit][remote_profiles]") {
    TempDir tempDir {"reqpack-remote-profiles"};
    const std::filesystem::path remotePath = tempDir.path() / "remote.lua";

    write_file(remotePath, R"(
        return {
            profiles = {
                staging = {
                    host = "https://staging.example.test:8443/api",
                    protocol = "json",
                    token = "secret-token",
                    username = "deploy",
                    password = "deploy-pass",
                },
            },
        }
    )");

    const std::vector<RemoteProfile> profiles = load_remote_profiles(remotePath);
    REQUIRE(profiles.size() == 1);

    const RemoteProfile& profile = profiles.front();
    CHECK(profile.name == "staging");
    CHECK(profile.host == "staging.example.test");
    CHECK(profile.port == 8443);
    CHECK(profile.protocol == RemoteProfileProtocol::JSON);
    REQUIRE(profile.token.has_value());
    CHECK(profile.token.value() == "secret-token");
    REQUIRE(profile.username.has_value());
    CHECK(profile.username.value() == "deploy");
    REQUIRE(profile.password.has_value());
    CHECK(profile.password.value() == "deploy-pass");
}

TEST_CASE("load_remote_profiles parses ipv6 endpoints and url aliases", "[unit][remote_profiles]") {
    TempDir tempDir {"reqpack-remote-profiles-ipv6"};
    const std::filesystem::path remotePath = tempDir.path() / "remote.lua";

    write_file(remotePath, R"(
        return {
            profiles = {
                ipv6 = {
                    url = "tcp://[2001:db8::1]:4545",
                },
                loopback = {
                    host = "127.0.0.1",
                    port = 4545,
                    protocol = "text",
                },
            },
        }
    )");

    const std::vector<RemoteProfile> profiles = load_remote_profiles(remotePath);
    REQUIRE(profiles.size() == 2);

    const auto ipv6 = std::find_if(profiles.begin(), profiles.end(),
                                   [](const RemoteProfile& profile) { return profile.name == "ipv6"; });
    REQUIRE(ipv6 != profiles.end());
    CHECK(ipv6->host == "2001:db8::1");
    CHECK(ipv6->port == 4545);
    CHECK(ipv6->protocol == RemoteProfileProtocol::TEXT);

    const auto loopback = std::find_if(profiles.begin(), profiles.end(),
                                       [](const RemoteProfile& profile) { return profile.name == "loopback"; });
    REQUIRE(loopback != profiles.end());
    CHECK(loopback->host == "127.0.0.1");
    CHECK(loopback->port == 4545);
    CHECK(loopback->protocol == RemoteProfileProtocol::TEXT);
}

TEST_CASE("load_remote_profiles skips invalid profile entries", "[unit][remote_profiles]") {
    TempDir tempDir {"reqpack-remote-profiles-invalid"};
    const std::filesystem::path remotePath = tempDir.path() / "remote.lua";

    write_file(remotePath, R"(
        return {
            profiles = {
                broken = {
                    host = "",
                    port = 0,
                },
                valid = {
                    host = "127.0.0.1",
                    port = 4545,
                },
            },
        }
    )");

    const std::vector<RemoteProfile> profiles = load_remote_profiles(remotePath);
    REQUIRE(profiles.size() == 1);
    CHECK(profiles.front().name == "valid");
}

TEST_CASE("find_remote_profile matches profile names case-insensitively", "[unit][remote_profiles]") {
    TempDir tempDir {"reqpack-remote-profiles-find"};
    const std::filesystem::path remotePath = tempDir.path() / "remote.lua";

    write_file(remotePath, R"(
        return {
            profiles = {
                prod = {
                    host = "prod.example.test",
                    port = 4545,
                    protocol = "https",
                },
            },
        }
    )");

    const std::optional<RemoteProfile> profile = find_remote_profile(remotePath, "PROD");
    REQUIRE(profile.has_value());
    CHECK(profile->name == "prod");
    CHECK(profile->host == "prod.example.test");
    CHECK(profile->protocol == RemoteProfileProtocol::HTTPS);
    CHECK_FALSE(find_remote_profile(remotePath, "missing").has_value());
}

TEST_CASE("load_remote_profiles returns empty vector for missing file", "[unit][remote_profiles]") {
    TempDir tempDir {"reqpack-remote-profiles-missing"};
    const std::filesystem::path remotePath = tempDir.path() / "missing-remote.lua";
    CHECK(load_remote_profiles(remotePath).empty());
}
