#include "core/remote/serve_remote_readonly_policy.h"

#include "core/common/types.h"

#include <catch2/catch.hpp>

TEST_CASE("readonly policy blocks mutating plugin and self-update commands", "[unit][remote][readonly]") {
    CHECK(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"self-update"}));
    CHECK(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"plugin", "update", "pip"}));
    CHECK(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"plugin", "update-tool", "pip"}));
    CHECK(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"plugin", "ensure", "apt"}));
    CHECK(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"plugin", "test", "--plugin", "demo"}));
    CHECK_FALSE(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"plugin", "info", "apt"}));
}

TEST_CASE("readonly policy allows security and version query commands", "[unit][remote][readonly]") {
    CHECK(serve_remote_readonly_policy::is_security_status_or_doctor_command({"security", "status"}));
    CHECK(serve_remote_readonly_policy::is_security_status_or_doctor_command({"security", "doctor"}));
    CHECK(serve_remote_readonly_policy::is_version_print_command({"version"}));
    CHECK(serve_remote_readonly_policy::is_version_print_command({"-V"}));
    CHECK_FALSE(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"security", "status"}));
    CHECK_FALSE(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"version"}));
}

TEST_CASE("readonly policy allows parsed read-only orchestrator actions", "[unit][remote][readonly]") {
    std::vector<Request> requests;
    requests.push_back(Request {.action = ActionType::LIST, .system = "apt"});
    CHECK(serve_remote_readonly_policy::requests_allowed_in_readonly_mode(requests));

    requests.front().action = ActionType::INFO;
    CHECK(serve_remote_readonly_policy::requests_allowed_in_readonly_mode(requests));

    requests.front().action = ActionType::UPDATE;
    CHECK_FALSE(serve_remote_readonly_policy::requests_allowed_in_readonly_mode(requests));

    requests.front().action = ActionType::ENSURE;
    CHECK_FALSE(serve_remote_readonly_policy::requests_allowed_in_readonly_mode(requests));
}

TEST_CASE("legacy bare update without args is treated as self-update for readonly blocking", "[unit][remote][readonly]") {
    CHECK(serve_remote_readonly_policy::is_self_update_command_line({"update"}));
    CHECK(serve_remote_readonly_policy::command_line_blocked_in_readonly_mode({"update"}));
    CHECK_FALSE(serve_remote_readonly_policy::is_self_update_command_line({"update", "apt"}));
}
