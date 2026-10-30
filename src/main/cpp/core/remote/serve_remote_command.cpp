#include "serve_remote_internal.h"
#include "core/remote/serve_remote_readonly_policy.h"

#include "core/common/build_info.h"
#include "core/execution/orchestrator.h"
#include "core/registry/registry.h"
#include "core/remote/remote_profiles.h"
#include "core/security/security_bridge.h"
#include "output/command_output.h"

#include <rqp/security/security_status.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace {

void configure_logger_from_config(Logger& logger, const ReqPackConfig& config) {
    logger.setLevel(to_string(config.logging.level));
    logger.setPattern(config.logging.pattern);
    logger.setBacktrace(config.logging.enableBacktrace, config.logging.backtraceSize);
    logger.setConsoleOutput(config.logging.consoleOutput);
    logger.setEnabledCategories(config.logging.enabledCategories);
    logger.setCaptureDisplayEvents(config.logging.captureDisplayEvents);
    if (config.logging.fileOutput) {
        logger.setFileSink(config.logging.filePath);
    } else {
        logger.disableFileSink();
    }
    if (config.logging.structuredFileOutput) {
        logger.setStructuredFileSink(config.logging.structuredFilePath);
    } else {
        logger.disableStructuredFileSink();
    }
}

std::optional<RemoteResponse> try_execute_query_fast_path_command(const std::vector<std::string>& tokens,
                                                                  const ReqPackConfig& config, Logger& logger) {
    if (serve_remote_readonly_policy::is_security_status_or_doctor_command(tokens)) {
        Registry registry(config);
        (void)registry.getDatabase()->ensureReady();
        const SecuritySettings settings = security_settings_from(config);
        const SecurityStatusReport report = build_security_status(settings, &registry);
        std::ostringstream buffer;
        print_security_status_report(report, buffer);
        logger.logStdout(buffer.str());
        logger.flushSync();
        CommandOutput output;
        output.mode = DisplayMode::REMOTE;
        output.sessionItems = {"security status"};
        output.blocks.push_back(make_command_raw_text_block(buffer.str()));
        output.success = !report.hasBlockingIssues;
        output.succeeded = report.hasBlockingIssues ? 0 : 1;
        output.failed = report.hasBlockingIssues ? 1 : 0;
        return RemoteResponse {.ok = !report.hasBlockingIssues, .output = std::move(output)};
    }

    if (serve_remote_readonly_policy::is_version_print_command(tokens)) {
        const std::string line = config.applicationName + " " + reqpack_build_release_id();
        logger.logStdout(line);
        logger.flushSync();
        CommandOutput output;
        output.mode = DisplayMode::REMOTE;
        output.sessionItems = {"version"};
        output.blocks.push_back(make_command_raw_text_block(line + "\n"));
        output.success = true;
        output.succeeded = 1;
        return RemoteResponse {.ok = true, .output = std::move(output)};
    }

    return std::nullopt;
}

} // namespace

bool requests_allowed_in_readonly_mode(const std::vector<Request>& requests) {
    return serve_remote_readonly_policy::requests_allowed_in_readonly_mode(requests);
}

namespace {

class StdIoCapture {
  public:
    StdIoCapture() {
        std::fflush(stdout);
        std::fflush(stderr);
        oldStdout_ = ::dup(STDOUT_FILENO);
        oldStderr_ = ::dup(STDERR_FILENO);
        file_ = std::tmpfile();
        if (oldStdout_ == -1 || oldStderr_ == -1 || file_ == nullptr) {
            restore();
            throw std::runtime_error("failed to start stdio capture");
        }
        const int captureFd = ::fileno(file_);
        if (::dup2(captureFd, STDOUT_FILENO) == -1 || ::dup2(captureFd, STDERR_FILENO) == -1) {
            restore();
            throw std::runtime_error("failed to redirect stdio");
        }
    }

    ~StdIoCapture() {
        restore();
        if (file_ != nullptr) {
            std::fclose(file_);
        }
    }

    std::ostream& stream() {
        return buffer_;
    }

    std::string finish() {
        std::fflush(stdout);
        std::fflush(stderr);
        std::string captured;
        if (file_ != nullptr) {
            std::rewind(file_);
            std::ostringstream fileBuffer;
            char chunk[4096];
            while (std::fgets(chunk, static_cast<int>(sizeof(chunk)), file_) != nullptr) {
                fileBuffer << chunk;
            }
            captured = fileBuffer.str();
        }
        restore();
        return buffer_.str() + captured;
    }

  private:
    void restore() {
        if (oldStdout_ != -1) {
            (void)::dup2(oldStdout_, STDOUT_FILENO);
            ::close(oldStdout_);
            oldStdout_ = -1;
        }
        if (oldStderr_ != -1) {
            (void)::dup2(oldStderr_, STDERR_FILENO);
            ::close(oldStderr_);
            oldStderr_ = -1;
        }
    }

    FILE* file_ {nullptr};
    int oldStdout_ {-1};
    int oldStderr_ {-1};
    std::ostringstream buffer_;
};

class DisplayGuard {
  public:
    DisplayGuard(Logger& logger, IDisplay* restoreDisplay) : logger_(logger), restoreDisplay_(restoreDisplay) {
        logger_.setDisplay(nullptr);
    }

    ~DisplayGuard() {
        logger_.setDisplay(restoreDisplay_);
    }

  private:
    Logger& logger_;
    IDisplay* restoreDisplay_;
};

} // namespace

bool reload_remote_state(RemoteServerState& state, Logger& logger, std::string& error) {
    try {
        ReqPackConfig defaults = default_reqpack_config();
        defaults.registry.remoteUrl = "https://github.com/Coditary/rqp-registry.git";
        ReqPackConfig config = load_config_from_lua(state.configPath, defaults);
        config = apply_config_overrides(config, state.configOverrides);
        const std::filesystem::path workspacePluginDirectory = std::filesystem::current_path() / "plugins";
        if (!state.configOverrides.pluginDirectory.has_value() &&
            config.registry.pluginDirectory == defaults.registry.pluginDirectory &&
            std::filesystem::exists(workspacePluginDirectory)) {
            config.registry.pluginDirectory = workspacePluginDirectory.string();
        }

        ServeRuntimeOptions options;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            options = state.options;
        }
        if (!options.readonlyExplicit) {
            options.readonly = config.remote.readonly;
        }
        if (!options.maxConnectionsExplicit) {
            options.maxConnections = config.remote.maxConnections;
        }

        const std::vector<RemoteUser> users = load_remote_users(state.remoteUsersPath);

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.config = config;
            state.options = options;
            state.users = users;
        }

        configure_logger_from_config(logger, config);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool command_requires_close(const std::string& command) {
    const std::string trimmed = trim_copy(command);
    return trimmed == "quit" || trimmed == "exit";
}

RemoteResponse execute_command(Cli& cli, RemoteServerState& state, Logger& logger, IDisplay* display,
                               const SessionIdentity& identity, const std::string& commandLine,
                               std::mutex& commandMutex) {
    const std::string trimmed = trim_copy(commandLine);
    if (trimmed.empty()) {
        return RemoteResponse {.ok = true, .output = command_output_message(DisplayMode::REMOTE, {})};
    }
    if (command_requires_close(trimmed)) {
        return RemoteResponse {
            .ok = true, .output = command_output_message(DisplayMode::REMOTE, {}), .closeConnection = true};
    }

    const std::vector<std::string> commandTokens = tokenize_command_line(trimmed);
    if (commandTokens.empty()) {
        return RemoteResponse {.ok = false,
                               .output = command_output_message(DisplayMode::REMOTE, "invalid command syntax", false)};
    }

    if (commandTokens[0] == "shutdown") {
        if (!identity.isAdmin) {
            return RemoteResponse {
                .ok = false, .output = command_output_message(DisplayMode::SERVE, "admin privileges required", false)};
        }
        state.shutdownRequested.store(true);
        return RemoteResponse {.ok = true,
                               .output = command_output_message(DisplayMode::SERVE, "server shutting down"),
                               .closeConnection = true};
    }
    if (commandTokens.size() == 2 && commandTokens[0] == "connections" && commandTokens[1] == "count") {
        if (!identity.isAdmin) {
            return RemoteResponse {
                .ok = false, .output = command_output_message(DisplayMode::SERVE, "admin privileges required", false)};
        }
        return RemoteResponse {.ok = true, .output = active_connection_count_output(state)};
    }
    if (commandTokens.size() == 2 && commandTokens[0] == "connections" && commandTokens[1] == "list") {
        if (!identity.isAdmin) {
            return RemoteResponse {
                .ok = false, .output = command_output_message(DisplayMode::SERVE, "admin privileges required", false)};
        }
        return RemoteResponse {.ok = true, .output = active_connection_list_output(state)};
    }
    if (commandTokens[0] == "reload-config") {
        if (!identity.isAdmin) {
            return RemoteResponse {
                .ok = false, .output = command_output_message(DisplayMode::SERVE, "admin privileges required", false)};
        }
        std::string error;
        if (!reload_remote_state(state, logger, error)) {
            return RemoteResponse {
                .ok = false, .output = command_output_message(DisplayMode::SERVE, "reload failed: " + error, false)};
        }
        return RemoteResponse {.ok = true, .output = command_output_message(DisplayMode::SERVE, "config reloaded")};
    }

    const RemoteStateSnapshot snapshot = snapshot_remote_state(state);
    const std::vector<std::string> mergedTokens =
        merged_command_arguments(commandTokens, snapshot.options.inheritedArguments);
    const ReqPackConfigOverrides overrides = extract_cli_config_overrides(mergedTokens);
    if (overrides.errorMessage.has_value()) {
        return RemoteResponse {
            .ok = false, .output = command_output_message(DisplayMode::REMOTE, overrides.errorMessage.value(), false)};
    }
    const ReqPackConfig effectiveConfig = apply_config_overrides(snapshot.config, overrides);

    if (snapshot.options.readonly &&
        serve_remote_readonly_policy::command_line_blocked_in_readonly_mode(mergedTokens)) {
        return RemoteResponse {
            .ok = false, .output = command_output_message(DisplayMode::REMOTE, "remote server is readonly", false)};
    }

    const std::vector<Request> requests = cli.parse(mergedTokens, effectiveConfig);
    if (requests.empty()) {
        if (!cli.lastParseError().empty()) {
            return RemoteResponse {.ok = false,
                                   .output = command_output_message(DisplayMode::REMOTE, cli.lastParseError(), false)};
        }
        if (std::optional<RemoteResponse> fastPath = try_execute_query_fast_path_command(mergedTokens, effectiveConfig, logger)) {
            return fastPath.value();
        }
        if (snapshot.options.readonly &&
            serve_remote_readonly_policy::command_line_blocked_in_readonly_mode(mergedTokens)) {
            return RemoteResponse {
                .ok = false, .output = command_output_message(DisplayMode::REMOTE, "remote server is readonly", false)};
        }
        return RemoteResponse {
            .ok = false,
            .output = command_output_message(DisplayMode::REMOTE, "failed to parse '" + trimmed + "'", false)};
    }

    for (const Request& request : requests) {
        if (request.action == ActionType::SERVE) {
            return RemoteResponse {
                .ok = false,
                .output = command_output_message(DisplayMode::REMOTE, "nested serve commands are not allowed", false)};
        }
    }

    if (snapshot.options.readonly && !requests_allowed_in_readonly_mode(requests)) {
        return RemoteResponse {
            .ok = false, .output = command_output_message(DisplayMode::REMOTE, "remote server is readonly", false)};
    }

    std::lock_guard<std::mutex> lock(commandMutex);
    logger.flushSync();
    DisplayGuard displayGuard(logger, display);
    StdIoCapture capture;
    std::unique_ptr<IDisplay> captureDisplay = create_plain_stream_display(capture.stream());
    logger.setDisplay(captureDisplay.get());
    Orchestrator orchestrator(requests, effectiveConfig);
    const int result = orchestrator.run();
    logger.flushSync();
    CommandOutput output;
    output.mode = requests.empty() ? DisplayMode::REMOTE
                                   : (requests.front().action == ActionType::LIST       ? DisplayMode::LIST
                                      : requests.front().action == ActionType::SEARCH   ? DisplayMode::SEARCH
                                      : requests.front().action == ActionType::INFO     ? DisplayMode::INFO
                                      : requests.front().action == ActionType::OUTDATED ? DisplayMode::OUTDATED
                                      : requests.front().action == ActionType::SNAPSHOT ? DisplayMode::SNAPSHOT
                                                                                        : DisplayMode::REMOTE);
    output.sessionItems = {trimmed};
    output.blocks.push_back(make_command_raw_text_block(capture.finish()));
    output.success = result == 0;
    output.succeeded = result == 0 ? 1 : 0;
    output.failed = result == 0 ? 0 : 1;
    return RemoteResponse {.ok = result == 0, .output = std::move(output)};
}

RemoteResponse execute_upload_install_command(ReqpackSocket clientFd, Cli& cli, RemoteServerState& state,
                                              Logger& logger, IDisplay* display, const SessionIdentity& identity,
                                              const std::vector<std::string>& commandTokens, std::mutex& commandMutex) {
    const std::optional<UploadInstallEnvelope> envelope = parse_upload_install_envelope(commandTokens);
    if (!envelope.has_value()) {
        return RemoteResponse {.ok = false,
                               .output = command_output_message(DisplayMode::REMOTE, "invalid upload request", false),
                               .closeConnection = true};
    }

    if (snapshot_remote_state(state).options.readonly) {
        if (!discard_bytes(clientFd, envelope->size)) {
            return RemoteResponse {
                .ok = false,
                .output = command_output_message(DisplayMode::REMOTE, "failed to read upload payload", false),
                .closeConnection = true};
        }
        return RemoteResponse {
            .ok = false, .output = command_output_message(DisplayMode::REMOTE, "remote server is readonly", false)};
    }

    try {
        const ScopedPathCleanup uploadedFile = write_uploaded_file_to_temp(clientFd, envelope.value());
        const std::string commandLine = substitute_upload_path(envelope->commandTemplate, uploadedFile.path());
        return execute_command(cli, state, logger, display, identity, commandLine, commandMutex);
    } catch (const std::exception& e) {
        return RemoteResponse {.ok = false, .output = command_output_message(DisplayMode::REMOTE, e.what(), false)};
    }
}
