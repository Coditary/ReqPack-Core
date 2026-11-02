#include "ecosystem/plugin_host/testing/plugin_test_runner.h"

#include "plugin_test_runner_internal.h"

PluginTestRunReport run_plugin_test_cases(const ReqPackConfig& config, const PluginTestInvocation& invocation) {
    return plugin_test_runner_internal::run_plugin_test_cases_impl(config, invocation);
}
