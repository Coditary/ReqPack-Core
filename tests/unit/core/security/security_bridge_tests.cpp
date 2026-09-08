#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/config/configuration.h"
#include "core/security/security_bridge.h"
#include "test_helpers.h"

#include <rqp/security/security_log.h>

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

TEST_CASE("security bridge maps reqpack config into security settings", "[unit][security][bridge]") {
    ReqPackConfig config;
    config.security.enabled = true;
    config.security.osvDatabasePath = "/tmp/osv";
    config.downloader.connectTimeoutSeconds = 11;
    config.downloader.requestTimeoutSeconds = 22;
    config.downloader.followRedirects = false;
    config.downloader.userAgent = "test-agent";
    config.interaction.interactive = false;
    config.reports.enabled = true;

    const SecuritySettings settings = security_settings_from(config);

    CHECK(settings.security.enabled);
    CHECK(settings.security.osvDatabasePath == "/tmp/osv");
    CHECK(settings.network.connectTimeoutSeconds == 11);
    CHECK(settings.network.requestTimeoutSeconds == 22);
    CHECK(settings.network.followRedirects == false);
    CHECK(settings.network.userAgent == "test-agent");
    CHECK(settings.interaction.interactive == false);
    CHECK(settings.reports.enabled);
    CHECK(static_cast<bool>(settings.archive.extractToTemp));
}

TEST_CASE("security bridge wires runtime callbacks", "[unit][security][bridge]") {
    wire_reqpack_security_runtime();
    security_log(SecurityLogLevel::Info, "security", "coverage probe");
    security_log(SecurityLogLevel::Debug, "security", "debug probe");
    security_log(SecurityLogLevel::Warn, "security", "warn probe");
    security_log(SecurityLogLevel::Error, "security", "error probe");
    security_log_stdout("stdout probe");
    security_log_diagnostic("security", "summary", "hint");
    security_flush();
    SUCCEED();
}

TEST_CASE("security bridge archive extract callback is configured", "[unit][security][bridge]") {
    ReqPackConfig config;
    const SecuritySettings settings = security_settings_from(config);
    REQUIRE(static_cast<bool>(settings.archive.extractToTemp));

    TempDir tempDir {"reqpack-security-bridge-archive"};
    const std::filesystem::path archivePath = tempDir.path() / "demo.tar";
    write_file(tempDir.path() / "payload.txt", "hello");
    REQUIRE(std::system(("tar -C " + escape_shell_arg(tempDir.path().string()) + " -cf " +
                         escape_shell_arg(archivePath.string()) + " payload.txt")
                            .c_str()) == 0);

    const SecurityArchiveResolution resolution = settings.archive.extractToTemp(archivePath);
    CHECK(std::filesystem::exists(resolution.installPath / "payload.txt"));
}
