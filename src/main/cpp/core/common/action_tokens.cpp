#include "core/common/action_tokens.h"

#include <algorithm>
#include <cctype>

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool is_install_command_token(const std::string& normalizedCommand) {
    return normalizedCommand == "install" || normalizedCommand == "i";
}

bool is_remove_command_token(const std::string& normalizedCommand) {
    return normalizedCommand == "remove" || normalizedCommand == "rm";
}

bool is_update_command_token(const std::string& normalizedCommand) {
    return normalizedCommand == "update" || normalizedCommand == "up";
}

}  // namespace

ActionType parse_action_token(const std::string& command) {
    const std::string normalizedCommand = to_lower_copy(command);

    if (is_install_command_token(normalizedCommand)) {
        return ActionType::INSTALL;
    }
    if (is_remove_command_token(normalizedCommand)) {
        return ActionType::REMOVE;
    }
    if (is_update_command_token(normalizedCommand)) {
        return ActionType::UPDATE;
    }
    if (normalizedCommand == "search") {
        return ActionType::SEARCH;
    }
    if (normalizedCommand == "list") {
        return ActionType::LIST;
    }
    if (normalizedCommand == "info") {
        return ActionType::INFO;
    }
    if (normalizedCommand == "ensure") {
        return ActionType::ENSURE;
    }
    if (normalizedCommand == "sbom") {
        return ActionType::SBOM;
    }
    if (normalizedCommand == "audit") {
        return ActionType::AUDIT;
    }
    if (normalizedCommand == "outdated") {
        return ActionType::OUTDATED;
    }
    if (normalizedCommand == "host") {
        return ActionType::HOST;
    }
    if (normalizedCommand == "snapshot") {
        return ActionType::SNAPSHOT;
    }
    if (normalizedCommand == "pack") {
        return ActionType::PACK;
    }
    if (normalizedCommand == "serve") {
        return ActionType::SERVE;
    }
    if (normalizedCommand == "remote") {
        return ActionType::REMOTE;
    }

    return ActionType::UNKNOWN;
}
