#include "core/remote/serve_remote_readonly_policy.h"

#include "core/common/action_tokens.h"

#include <algorithm>
#include <cctype>

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_flag_only_token(const std::string& token) {
    return token.rfind("--", 0) == 0 || (token.size() == 2 && token[0] == '-' && token[1] != '-');
}

std::vector<std::string> strip_config_like_prefix_flags(const std::vector<std::string>& tokens) {
    std::vector<std::string> filtered;
    filtered.reserve(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        if (token == "--config" || token == "-c") {
            if (i + 1 < tokens.size()) {
                ++i;
            }
            continue;
        }
        if (token.rfind("--config=", 0) == 0 || token.rfind("-c=", 0) == 0) {
            continue;
        }
        filtered.push_back(token);
    }
    return filtered;
}

}  // namespace

namespace serve_remote_readonly_policy {

bool is_security_status_or_doctor_command(const std::vector<std::string>& tokens) {
    const std::vector<std::string> filtered = strip_config_like_prefix_flags(tokens);
    if (filtered.size() != 2) {
        return false;
    }
    return to_lower_copy(filtered[0]) == "security" &&
           (to_lower_copy(filtered[1]) == "status" || to_lower_copy(filtered[1]) == "doctor");
}

bool is_version_print_command(const std::vector<std::string>& tokens) {
    const std::vector<std::string> filtered = strip_config_like_prefix_flags(tokens);
    if (filtered.size() != 1) {
        return false;
    }
    const std::string& token = filtered.front();
    return to_lower_copy(token) == "version" || token == "--version" || token == "-V";
}

bool is_self_update_command_line(const std::vector<std::string>& tokens) {
    const std::vector<std::string> filtered = strip_config_like_prefix_flags(tokens);
    if (filtered.empty()) {
        return false;
    }
    if (to_lower_copy(filtered.front()) == "self-update") {
        for (std::size_t i = 1; i < filtered.size(); ++i) {
            if (!is_flag_only_token(filtered[i])) {
                return false;
            }
        }
        return true;
    }
    if (parse_action_token(filtered.front()) != ActionType::UPDATE) {
        return false;
    }
    for (std::size_t i = 1; i < filtered.size(); ++i) {
        const std::string& argument = filtered[i];
        if (argument == "--all" || argument == "-a" || argument == "--plugins") {
            return false;
        }
        if (!is_flag_only_token(argument)) {
            return false;
        }
    }
    return true;
}

bool is_plugin_info_command_line(const std::vector<std::string>& tokens) {
    const std::vector<std::string> filtered = strip_config_like_prefix_flags(tokens);
    return filtered.size() >= 2 && to_lower_copy(filtered[0]) == "plugin" && to_lower_copy(filtered[1]) == "info";
}

bool command_line_blocked_in_readonly_mode(const std::vector<std::string>& tokens) {
    if (is_self_update_command_line(tokens)) {
        return true;
    }

    const std::vector<std::string> filtered = strip_config_like_prefix_flags(tokens);
    if (filtered.empty()) {
        return false;
    }

    const std::string head = to_lower_copy(filtered.front());
    if (head == "test-plugin") {
        return true;
    }
    if (head == "plugin" && filtered.size() >= 2) {
        const std::string sub = to_lower_copy(filtered[1]);
        return sub != "info";
    }
    if (head == "ensure") {
        return true;
    }

    const ActionType action = parse_action_token(filtered.front());
    switch (action) {
        case ActionType::INSTALL:
        case ActionType::REMOVE:
        case ActionType::UPDATE:
        case ActionType::ENSURE:
        case ActionType::PACK:
        case ActionType::SNAPSHOT:
        case ActionType::HOST:
        case ActionType::SERVE:
        case ActionType::REMOTE:
            return true;
        default:
            break;
    }
    return false;
}

bool command_line_is_query_fast_path(const std::vector<std::string>& tokens) {
    return is_security_status_or_doctor_command(tokens) || is_version_print_command(tokens);
}

bool requests_allowed_in_readonly_mode(const std::vector<Request>& requests) {
    if (requests.empty()) {
        return false;
    }
    for (const Request& request : requests) {
        if (request.action != ActionType::LIST && request.action != ActionType::SEARCH &&
            request.action != ActionType::INFO && request.action != ActionType::OUTDATED &&
            request.action != ActionType::SBOM && request.action != ActionType::AUDIT) {
            return false;
        }
        if (!request.outputPath.empty()) {
            return false;
        }
    }
    return true;
}

}  // namespace serve_remote_readonly_policy
