#pragma once

#include "core/common/types.h"

#include <string>
#include <vector>

namespace serve_remote_readonly_policy {

bool is_security_status_or_doctor_command(const std::vector<std::string>& tokens);
bool is_version_print_command(const std::vector<std::string>& tokens);
bool is_self_update_command_line(const std::vector<std::string>& tokens);
bool is_plugin_info_command_line(const std::vector<std::string>& tokens);

/// Mutating CLI invocations that must be rejected before Orchestrator when server is readonly.
bool command_line_blocked_in_readonly_mode(const std::vector<std::string>& tokens);

/// True when the line is handled outside Orchestrator (security status, version, …).
bool command_line_is_query_fast_path(const std::vector<std::string>& tokens);

bool requests_allowed_in_readonly_mode(const std::vector<Request>& requests);

}  // namespace serve_remote_readonly_policy
