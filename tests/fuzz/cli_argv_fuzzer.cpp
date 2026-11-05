#include "cli_argv_helpers.h"

#include "cli/cli.h"
#include "ecosystem/plugin_host/testing/plugin_test_runner.h"
#include "main_arg_parsing.h"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > (1U << 16)) {
        return 0;
    }

    const std::vector<std::string> arguments = fuzz_split_argv_tokens(data, size);
    const ReqPackConfig config = default_reqpack_config();

    try {
        Cli cli;
        (void)cli.parse(arguments, config);
        (void)cli.parseFailed();
        (void)cli.lastParseError();

        std::vector<std::string> overrideStorage = arguments;
        std::vector<char*> overrideArgv = fuzz_argv_pointers(overrideStorage);
        (void)cli.parseConfigOverrides(static_cast<int>(overrideArgv.size()), overrideArgv.data());
    } catch (const std::exception&) {
        // Invalid fuzz input is expected.
    }

    ServeRuntimeOptions serveOptions;
    std::string error;
    (void)parse_serve_runtime_options(arguments, serveOptions, error);

    RemoteClientInvocation remoteInvocation;
    (void)parse_remote_client_invocation(arguments, remoteInvocation, error);

    (void)parse_plugin_test_invocation(arguments);

    (void)is_self_update_command(arguments);
    (void)is_legacy_self_update_via_update_command(arguments);
    (void)is_host_refresh_command(arguments);
    (void)is_security_status_command(arguments);
    (void)is_version_command(arguments);
    (void)is_info_command(arguments);
    (void)is_action_stdin_command(arguments, "install");
    (void)is_action_stdin_command(arguments, "audit");
    (void)inherited_stream_arguments(arguments);

    return 0;
}
