#include <sol/sol.hpp>

#include <stdexcept>
#include <string>

#include <catch2/catch.hpp>

#include "plugins/exec_rules_core.h"

namespace {

sol::object eval_lua(sol::state& lua, const std::string& source) {
    sol::load_result loaded = lua.load(source);
    REQUIRE(loaded.valid());
    sol::protected_function_result result = loaded();
    REQUIRE(result.valid());
    return result.get<sol::object>();
}

sol::state make_lua() {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
    return lua;
}

}  // namespace

TEST_CASE("exec rules parser accepts valid unified rules", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const sol::object rulesObject = eval_lua(lua, R"(
        return {
            initial = "confirm",
            rules = {
                {
                    state = "confirm",
                    source = "screen",
                    regex = "Continue\\?",
                    ["repeat"] = false,
                    stop = true,
                    actions = {
                        { type = "send", value = "y\n" },
                        { type = "state", value = "running" },
                    },
                },
                {
                    state = "running",
                    source = "line",
                    regex = "^Progress:\\s+(\\d+)%$",
                    actions = {
                        { type = "progress", percent = "${1}", current = "16.4", currentUnit = "MiB", total = "40.0", totalUnit = "MiB", speed = "2.5", speedUnit = "MiB/s" },
                    },
                },
            },
        }
    )");

    const ExecRuleset ruleset = parse_exec_rules(rulesObject);
    REQUIRE(ruleset.initialState == "confirm");
    REQUIRE(ruleset.rules.size() == 2);
    REQUIRE(ruleset.requiresPty);
    CHECK(ruleset.rules[0].source == ExecRuleSource::Screen);
    CHECK_FALSE(ruleset.rules[0].repeat);
    CHECK(ruleset.rules[0].stop);
    REQUIRE(ruleset.rules[0].actions.size() == 2);
    CHECK(ruleset.rules[0].actions[0].type == ExecRuleActionType::Send);
    CHECK(ruleset.rules[1].source == ExecRuleSource::Line);
}

TEST_CASE("exec rules parser rejects invalid source", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const sol::object rulesObject = eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "broken",
                    regex = "x",
                    actions = {
                        { type = "log", message = "x" },
                    },
                },
            },
        }
    )");

    REQUIRE_THROWS_WITH(
        parse_exec_rules(rulesObject),
        Catch::Contains("source must be 'line' or 'screen'")
    );
}

TEST_CASE("exec rules parser rejects missing action fields", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const sol::object rulesObject = eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "x",
                    actions = {
                        { type = "progress" },
                    },
                },
            },
        }
    )");

    REQUIRE_THROWS_WITH(
        parse_exec_rules(rulesObject),
        Catch::Contains("progress action requires field 'percent', 'current', 'total', or 'speed'")
    );
}

TEST_CASE("exec rules parser accepts rich progress fields without percent", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const sol::object rulesObject = eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "^Loaded (.+)$",
                    actions = {
                        { type = "progress", current = "16.4", currentUnit = "MiB", total = "40.0", totalUnit = "MiB", speed = "2.5", speedUnit = "MiB/s" },
                    },
                },
            },
        }
    )");

    const ExecRuleset ruleset = parse_exec_rules(rulesObject);
    REQUIRE(ruleset.rules.size() == 1);
    CHECK(ruleset.rules[0].actions[0].fields.at("currentUnit") == "MiB");
    CHECK(ruleset.rules[0].actions[0].fields.at("speedUnit") == "MiB/s");
}

TEST_CASE("exec rules parser rejects unknown action type", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const sol::object rulesObject = eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "x",
                    actions = {
                        { type = "explode", message = "x" },
                    },
                },
            },
        }
    )");

    REQUIRE_THROWS_WITH(
        parse_exec_rules(rulesObject),
        Catch::Contains("action type 'explode' is unknown")
    );
}

TEST_CASE("exec rules parser rejects malformed top-level rules shape", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, "return 'not-a-table'")),
        Catch::Contains("rules must be a table")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, "return { rules = 'broken' }")),
        Catch::Contains("rules.rules must be an array-style table")
    );
}

TEST_CASE("exec rules parser rejects invalid regex patterns", "[unit][exec_rules][parse]" ) {
    sol::state lua = make_lua();
    const sol::object rulesObject = eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "(",
                    actions = {
                        { type = "log", message = "x" },
                    },
                },
            },
        }
    )");

    REQUIRE_THROWS_WITH(
        parse_exec_rules(rulesObject),
        Catch::Contains("regex failed to compile")
    );
}

TEST_CASE("exec rule runner mode is derived from rules", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();

    const ExecRuleset lineRules = parse_exec_rules(eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "x",
                    actions = { { type = "log", message = "x" } },
                },
            },
        }
    )"));
    CHECK(determine_exec_rule_runner_mode(lineRules) == ExecRuleRunnerMode::Line);

    const ExecRuleset ptyRules = parse_exec_rules(eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "x",
                    actions = { { type = "send", value = "y\n" } },
                },
            },
        }
    )"));
    CHECK(determine_exec_rule_runner_mode(ptyRules) == ExecRuleRunnerMode::Pty);
}

TEST_CASE("exec rules parser accepts diagnostic action types", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const ExecRuleset ruleset = parse_exec_rules(eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "^done$",
                    actions = {
                        { type = "log", message = "hello" },
                        { type = "status", code = "200" },
                        { type = "begin_step", label = "install" },
                        { type = "success" },
                        { type = "failed", message = "boom" },
                        { type = "artifact", payload = "file.txt" },
                    },
                },
            },
        }
    )"));

    REQUIRE(ruleset.rules.size() == 1);
    const std::vector<ExecRuleAction>& actions = ruleset.rules[0].actions;
    REQUIRE(actions.size() == 6);
    CHECK(actions[0].type == ExecRuleActionType::Log);
    CHECK(actions[1].type == ExecRuleActionType::Status);
    CHECK(actions[2].type == ExecRuleActionType::BeginStep);
    CHECK(actions[3].type == ExecRuleActionType::Success);
    CHECK(actions[4].type == ExecRuleActionType::Failed);
    CHECK(actions[5].type == ExecRuleActionType::Artifact);
}

TEST_CASE("exec rules parser rejects missing fields for diagnostic actions", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "log" } } } } }
        )")),
        Catch::Contains("log action requires field 'message'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "status" } } } } }
        )")),
        Catch::Contains("status action requires field 'code'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "begin_step" } } } } }
        )")),
        Catch::Contains("begin_step action requires field 'label'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "failed" } } } } }
        )")),
        Catch::Contains("failed action requires field 'message'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "artifact" } } } } }
        )")),
        Catch::Contains("artifact action requires field 'payload'")
    );
}

TEST_CASE("exec rules parser rejects malformed rule tables", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = {} } } }
        )")),
        Catch::Contains("actions must not be empty")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "send" } } } } }
        )")),
        Catch::Contains("action requires field 'value'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", ["repeat"] = "yes", actions = { { type = "log", message = "x" } } } } }
        )")),
        Catch::Contains("repeat must be a boolean")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", unknown = true, actions = { { type = "log", message = "x" } } } } }
        )")),
        Catch::Contains("contains unknown key 'unknown'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { [1] = { source = "line", regex = "x", actions = { { type = "log", message = "x" } } }, [3] = { source = "line", regex = "y", actions = { { type = "log", message = "y" } } } } }
        )")),
        Catch::Contains("must be contiguous and start at index 1")
    );
}

TEST_CASE("exec rules parser rejects invalid top-level and action shapes", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, "return { initial = 1, rules = {} }")),
        Catch::Contains("rules.initial must be a string")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, "return { extra = true, rules = {} }")),
        Catch::Contains("contains unknown top-level key 'extra'")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { type = "log", message = { nested = true } } } } } }
        )")),
        Catch::Contains("contains unsupported nested value")
    );

    REQUIRE_THROWS_WITH(
        parse_exec_rules(eval_lua(lua, R"(
            return { rules = { { source = "line", regex = "x", actions = { { message = "x" } } } } }
        )")),
        Catch::Contains("is missing field 'type'")
    );
}

TEST_CASE("exec rules parser coerces scalar action field types", "[unit][exec_rules][parse]") {
    sol::state lua = make_lua();
    const ExecRuleset ruleset = parse_exec_rules(eval_lua(lua, R"(
        return {
            rules = {
                {
                    source = "line",
                    regex = "^x$",
                    actions = {
                        { type = "status", code = 404, enabled = true, count = 3, ratio = 1.5 },
                    },
                },
            },
        }
    )"));

    const ExecRuleAction& action = ruleset.rules[0].actions[0];
    CHECK(action.fields.at("code") == "404");
    CHECK(action.fields.at("enabled") == "true");
    CHECK(action.fields.at("count") == "3");
    CHECK(action.fields.at("ratio").rfind("1.5") == 0);
}
