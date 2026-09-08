#include <catch2/catch.hpp>

#include <sol/sol.hpp>

#include "core/common/types.h"
#include "plugins/lua_bridge_value_mapper.h"

namespace {

sol::state make_lua_state() {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    return lua;
}

} // namespace

TEST_CASE("LuaBridgeValueMapper toLowerCopy normalizes case", "[unit][lua_bridge_value_mapper]") {
    CHECK(LuaBridgeValueMapper::toLowerCopy("Install") == "install");
    CHECK(LuaBridgeValueMapper::toLowerCopy("HTTPS") == "https");
    CHECK(LuaBridgeValueMapper::toLowerCopy("") == "");
}

TEST_CASE("LuaBridgeValueMapper valueToString formats primitive lua values", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();

    CHECK(LuaBridgeValueMapper::valueToString(sol::make_object(lua, std::string("demo"))) == "demo");
    CHECK(LuaBridgeValueMapper::valueToString(sol::make_object(lua, true)) == "true");
    CHECK(LuaBridgeValueMapper::valueToString(sol::make_object(lua, false)) == "false");
    CHECK(LuaBridgeValueMapper::valueToString(sol::make_object(lua, 42)) == "42");
    CHECK(LuaBridgeValueMapper::valueToString(sol::object()) == "null");
}

TEST_CASE("LuaBridgeValueMapper serializeLuaPayload sorts table entries", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table table = lua.create_table();
    table["zeta"] = 3;
    table["alpha"] = 1;
    table["beta"] = 2;

    CHECK(LuaBridgeValueMapper::serializeLuaPayload(sol::make_object(lua, table)) == "{alpha=1, beta=2, zeta=3}");
}

TEST_CASE("LuaBridgeValueMapper stringArrayFromObject parses lua string arrays", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table table = lua.create_table();
    table[1] = "one";
    table[2] = "two";

    const std::optional<std::vector<std::string>> parsed =
        LuaBridgeValueMapper::stringArrayFromObject(sol::make_object(lua, table));
    REQUIRE(parsed.has_value());
    CHECK(parsed.value() == std::vector<std::string> {"one", "two"});
    CHECK_FALSE(LuaBridgeValueMapper::stringArrayFromObject(sol::make_object(lua, 7)).has_value());
}

TEST_CASE("LuaBridgeValueMapper packageFromObject parses action and fields", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table table = lua.create_table();
    table["action"] = "install";
    table["system"] = "dnf";
    table["name"] = "curl";
    table["version"] = "8.0";
    table["sourcePath"] = "/tmp/curl.rpm";
    table["localTarget"] = true;
    table["flags"] = std::vector<std::string> {"force"};

    const std::optional<Package> parsed = LuaBridgeValueMapper::packageFromObject(sol::make_object(lua, table));
    REQUIRE(parsed.has_value());
    CHECK(parsed->action == ActionType::INSTALL);
    CHECK(parsed->system == "dnf");
    CHECK(parsed->name == "curl");
    CHECK(parsed->version == "8.0");
    CHECK(parsed->sourcePath == "/tmp/curl.rpm");
    CHECK(parsed->localTarget);
    CHECK(parsed->flags == std::vector<std::string> {"force"});
}

TEST_CASE("LuaBridgeValueMapper packagesFromObject parses package tables", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table packageTable = lua.create_table();
    packageTable["action"] = "update";
    packageTable["name"] = "demo";
    packageTable["version"] = "1.2.3";

    sol::table list = lua.create_table();
    list[1] = packageTable;

    const std::optional<std::vector<Package>> parsed =
        LuaBridgeValueMapper::packagesFromObject(sol::make_object(lua, list));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 1);
    CHECK(parsed->front().action == ActionType::UPDATE);
    CHECK(parsed->front().name == "demo");
    CHECK(parsed->front().version == "1.2.3");
}

TEST_CASE("LuaBridgeValueMapper inheritMissingPackageFields fills missing values", "[unit][lua_bridge_value_mapper]") {
    const std::vector<Package> sourcePackages {
        Package {
            .action = ActionType::INSTALL,
            .system = "dnf",
            .name = "curl",
            .version = "8.0",
            .sourcePath = "/tmp/curl.rpm",
            .localTarget = true,
            .flags = {"dep-flag"},
        },
    };

    std::vector<Package> packages {
        Package {
            .action = ActionType::UNKNOWN,
            .name = "curl",
        },
    };

    LuaBridgeValueMapper::inheritMissingPackageFields(sourcePackages, packages);
    CHECK(packages.front().action == ActionType::INSTALL);
    CHECK(packages.front().system == "dnf");
    CHECK(packages.front().version == "8.0");
    CHECK(packages.front().sourcePath == "/tmp/curl.rpm");
    CHECK(packages.front().localTarget);
    CHECK(packages.front().flags == std::vector<std::string> {"dep-flag"});
}

TEST_CASE("LuaBridgeValueMapper packageInfoFromObject maps nested fields", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table info = lua.create_table();
    info["name"] = "demo";
    info["version"] = "1.0.0";
    info["summary"] = "short";
    info["description"] = "long";
    info["dependencies"] = lua.create_table_with(1, "lib-a", 2, "lib-b");
    info["extraFields"] = lua.create_table_with(1, lua.create_table_with("key", "license", "value", "MIT"));

    const PackageInfo parsed = LuaBridgeValueMapper::packageInfoFromObject(sol::make_object(lua, info));
    CHECK(parsed.name == "demo");
    CHECK(parsed.version == "1.0.0");
    CHECK(parsed.summary == "short");
    CHECK(parsed.description == "long");
    CHECK(parsed.dependencies == std::vector<std::string> {"lib-a", "lib-b"});
    REQUIRE(parsed.extraFields.size() == 1);
    CHECK(parsed.extraFields.front().first == "license");
    CHECK(parsed.extraFields.front().second == "MIT");
}

TEST_CASE("LuaBridgeValueMapper packageInfoListFromObject parses table entries", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    const sol::table entry = lua.create_table_with("name", "alpha", "version", "2.0.0");
    const sol::table list = lua.create_table_with(1, entry);

    const std::vector<PackageInfo> parsed =
        LuaBridgeValueMapper::packageInfoListFromObject(sol::make_object(lua, list));
    REQUIRE(parsed.size() == 1);
    CHECK(parsed.front().name == "alpha");
    CHECK(parsed.front().version == "2.0.0");
}

TEST_CASE("LuaBridgeValueMapper proxyResolutionFromObject parses proxy tables", "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table table = lua.create_table();
    table["targetSystem"] = "maven";
    table["packages"] = lua.create_table_with(1, "org.demo:artifact");
    table["localPath"] = "/tmp/demo";
    table["flags"] = lua.create_table_with(1, "--offline");

    const std::optional<ProxyResolution> parsed =
        LuaBridgeValueMapper::proxyResolutionFromObject(sol::make_object(lua, table));
    REQUIRE(parsed.has_value());
    CHECK(parsed->targetSystem == "maven");
    CHECK(parsed->packages == std::vector<std::string> {"org.demo:artifact"});
    CHECK(parsed->localPath == "/tmp/demo");
    CHECK(parsed->flags == std::vector<std::string> {"--offline"});
}

TEST_CASE("LuaBridgeValueMapper pluginSecurityMetadataFromObject normalizes metadata",
          "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table metadata = lua.create_table();
    metadata["role"] = "Security-Provider";
    metadata["capabilities"] = lua.create_table_with(1, "Network");
    metadata["ecosystemScopes"] = lua.create_table_with(1, "demo-osv");
    metadata["writeScopes"] = lua.create_table_with(1, lua.create_table_with("kind", "Temp"));
    metadata["networkScopes"] =
        lua.create_table_with(1, lua.create_table_with("host", "API.OSV.DEV", "scheme", "HTTPS", "pathPrefix", "/v1"));
    metadata["privilegeLevel"] = "None";
    metadata["osvEcosystem"] = "demo-osv";
    metadata["purlType"] = "generic";
    metadata["versionComparatorProfile"] = "lexicographic";
    metadata["versionTokenPattern"] = "[0-9]+";
    metadata["versionCaseInsensitive"] = true;

    const std::optional<PluginSecurityMetadata> parsed =
        LuaBridgeValueMapper::pluginSecurityMetadataFromObject(sol::make_object(lua, metadata));
    REQUIRE(parsed.has_value());
    CHECK(parsed->role == "security-provider");
    CHECK(parsed->capabilities == std::vector<std::string> {"network"});
    CHECK(parsed->ecosystemScopes == std::vector<std::string> {"demo-osv"});
    REQUIRE(parsed->writeScopes.size() == 1);
    CHECK(parsed->writeScopes.front().kind == "temp");
    REQUIRE(parsed->networkScopes.size() == 1);
    CHECK(parsed->networkScopes.front().host == "api.osv.dev");
    CHECK(parsed->networkScopes.front().scheme == "https");
    CHECK(parsed->networkScopes.front().pathPrefix == "/v1");
    CHECK(parsed->privilegeLevel == "none");
    CHECK(parsed->osvEcosystem == "demo-osv");
    CHECK(parsed->purlType == "generic");
    CHECK(parsed->versionComparator.profile == "lexicographic");
    CHECK(parsed->versionComparator.tokenPattern == "[0-9]+");
    CHECK(parsed->versionComparator.caseInsensitive);
}

TEST_CASE("LuaBridgeValueMapper fileExtensionsFromPluginTable reads extension list",
          "[unit][lua_bridge_value_mapper]") {
    sol::state lua = make_lua_state();
    sol::table pluginTable = lua.create_table();
    pluginTable["fileExtensions"] = lua.create_table_with(1, ".rqp", 2, ".rqpack");

    const std::vector<std::string> extensions = LuaBridgeValueMapper::fileExtensionsFromPluginTable(pluginTable);
    CHECK(extensions == std::vector<std::string> {".rqp", ".rqpack"});
    CHECK(LuaBridgeValueMapper::fileExtensionsFromPluginTable(lua.create_table()).empty());
}
