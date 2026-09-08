#include <catch2/catch.hpp>

#include "core/common/action_tokens.h"

TEST_CASE("parse_action_token maps install commands and aliases", "[unit][action_tokens]") {
    CHECK(parse_action_token("install") == ActionType::INSTALL);
    CHECK(parse_action_token("INSTALL") == ActionType::INSTALL);
    CHECK(parse_action_token("i") == ActionType::INSTALL);
}

TEST_CASE("parse_action_token maps remove commands and aliases", "[unit][action_tokens]") {
    CHECK(parse_action_token("remove") == ActionType::REMOVE);
    CHECK(parse_action_token("REMOVE") == ActionType::REMOVE);
    CHECK(parse_action_token("rm") == ActionType::REMOVE);
}

TEST_CASE("parse_action_token maps update commands and aliases", "[unit][action_tokens]") {
    CHECK(parse_action_token("update") == ActionType::UPDATE);
    CHECK(parse_action_token("UPDATE") == ActionType::UPDATE);
    CHECK(parse_action_token("up") == ActionType::UPDATE);
}

TEST_CASE("parse_action_token maps search and other known commands", "[unit][action_tokens]") {
    CHECK(parse_action_token("search") == ActionType::SEARCH);
    CHECK(parse_action_token("list") == ActionType::LIST);
    CHECK(parse_action_token("info") == ActionType::INFO);
    CHECK(parse_action_token("ensure") == ActionType::ENSURE);
    CHECK(parse_action_token("sbom") == ActionType::SBOM);
    CHECK(parse_action_token("audit") == ActionType::AUDIT);
    CHECK(parse_action_token("outdated") == ActionType::OUTDATED);
    CHECK(parse_action_token("host") == ActionType::HOST);
    CHECK(parse_action_token("snapshot") == ActionType::SNAPSHOT);
    CHECK(parse_action_token("pack") == ActionType::PACK);
    CHECK(parse_action_token("serve") == ActionType::SERVE);
    CHECK(parse_action_token("remote") == ActionType::REMOTE);
}

TEST_CASE("parse_action_token returns unknown for unrecognized tokens", "[unit][action_tokens]") {
    CHECK(parse_action_token("unknown") == ActionType::UNKNOWN);
    CHECK(parse_action_token("") == ActionType::UNKNOWN);
    CHECK(parse_action_token("installx") == ActionType::UNKNOWN);
}
