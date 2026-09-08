#include "core/security/security_bridge.h"

#include "core/archive/archive_resolver.h"
#include "output/logger.h"

#include <rqp/security/security_log.h>

SecuritySettings security_settings_from(const ReqPackConfig& config) {
    SecuritySettings settings;
    settings.security = config.security;
    settings.network.connectTimeoutSeconds = config.downloader.connectTimeoutSeconds;
    settings.network.requestTimeoutSeconds = config.downloader.requestTimeoutSeconds;
    settings.network.followRedirects = config.downloader.followRedirects;
    settings.network.userAgent = config.downloader.userAgent;
    settings.interaction.interactive = config.interaction.interactive;
    settings.reports.enabled = config.reports.enabled;
    settings.archive.extractToTemp = [](const std::filesystem::path& path) {
        const ArchiveResolution resolution = extract_archive_to_temp_directory(path);
        return SecurityArchiveResolution{resolution.installPath, resolution.cleanupPaths};
    };
    return settings;
}

void wire_reqpack_security_runtime() {
    set_security_log_callback([](SecurityLogLevel level, const std::string& category, const std::string& message) {
        spdlog::level::level_enum spdLevel = spdlog::level::info;
        switch (level) {
        case SecurityLogLevel::Debug:
            spdLevel = spdlog::level::debug;
            break;
        case SecurityLogLevel::Warn:
            spdLevel = spdlog::level::warn;
            break;
        case SecurityLogLevel::Error:
            spdLevel = spdlog::level::err;
            break;
        case SecurityLogLevel::Info:
        default:
            spdLevel = spdlog::level::info;
            break;
        }

        Logger::instance().emit(OutputAction::LOG, OutputContext{
                                                       .level = spdLevel,
                                                       .message = message,
                                                       .category = category,
                                                   });
    });

    set_security_stdout_callback([](const std::string& message) { Logger::instance().logStdout(message); });

    set_security_diagnostic_callback(
        [](const std::string& category, const std::string& message, const std::string& hint) {
            Logger::instance().emitDiagnostic(make_error_diagnostic(category, message, message, hint));
        });

    set_security_flush_callback([]() { Logger::instance().flushSync(); });
}
