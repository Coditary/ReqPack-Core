cmake_minimum_required(VERSION 3.15)
project(ReqPackCore LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(REQPACK_CORE_STANDALONE TRUE)
else()
    set(REQPACK_CORE_STANDALONE FALSE)
endif()

option(REQPACK_CORE_BUILD_TESTS
    "Build and register ReqPack-Core unit/integration tests with CTest"
    ${REQPACK_CORE_STANDALONE}
)

option(REQPACK_CORE_ENABLE_COVERAGE
    "Enable gcov coverage instrumentation for library and tests"
    OFF
)

option(REQPACK_CORE_BUILD_FUZZERS
    "Build libFuzzer targets (requires Clang + -fsanitize=fuzzer)"
    OFF
)

set(REQPACK_CLI_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../reqpack"
    CACHE PATH "ReqPack CLI repository providing output (and optional CLI) sources for tests"
)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Lua REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_library(REQPACK_ZSTD_LIBRARY NAMES zstd REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(REQPACK_CORE_BUILD_TESTS OR NOT REQPACK_CORE_STANDALONE)
    find_package(CLI11 REQUIRED)
endif()

set(REQPACK_FMT_TARGET fmt::fmt)
if(NOT WIN32 AND TARGET fmt::fmt-header-only)
    set(REQPACK_FMT_TARGET fmt::fmt-header-only)
endif()

set(REQPACK_SPDLOG_TARGET spdlog::spdlog)
if(NOT WIN32 AND TARGET spdlog::spdlog_header_only)
    set(REQPACK_SPDLOG_TARGET spdlog::spdlog_header_only)
endif()

set(REQPACK_PLATFORM_LIBS)
if(WIN32)
    list(APPEND REQPACK_PLATFORM_LIBS ws2_32)
endif()
if(UNIX AND NOT APPLE)
    list(APPEND REQPACK_PLATFORM_LIBS util)
endif()

set(REQPACK_SECURITY_CORE_REPO
    "https://github.com/Coditary/reqpack-security-core.git"
    CACHE STRING "Git repository for reqpack-security-core"
)
set(REQPACK_SECURITY_CORE_TAG
    "main"
    CACHE STRING "Git tag, branch, or commit for reqpack-security-core"
)
set(REQPACK_SECURITY_CORE_DIR
    ""
    CACHE PATH "Optional local checkout of reqpack-security-core"
)

set(_reqpack_security_core_local "${CMAKE_CURRENT_SOURCE_DIR}/../reqpack-security-core")
if(REQPACK_SECURITY_CORE_DIR AND EXISTS "${REQPACK_SECURITY_CORE_DIR}/CMakeLists.txt")
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${REQPACK_SECURITY_CORE_DIR}")
elseif(EXISTS "${_reqpack_security_core_local}/CMakeLists.txt")
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${_reqpack_security_core_local}")
else()
    FetchContent_Declare(
        reqpack_security_core
        GIT_REPOSITORY ${REQPACK_SECURITY_CORE_REPO}
        GIT_TAG ${REQPACK_SECURITY_CORE_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(reqpack_security_core)
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${reqpack_security_core_SOURCE_DIR}")
endif()

if(NOT TARGET reqpack_security_core)
    add_subdirectory(
        ${REQPACK_SECURITY_CORE_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/reqpack-security-core-build
    )
endif()

if(NOT TARGET lmdb)
    FetchContent_Declare(
        lmdb
        GIT_REPOSITORY https://github.com/LMDB/lmdb.git
        GIT_TAG LMDB_0.9.33
    )
    FetchContent_GetProperties(lmdb)
    if(NOT lmdb_POPULATED)
        FetchContent_Populate(lmdb)
    endif()
    add_library(lmdb STATIC
        ${lmdb_SOURCE_DIR}/libraries/liblmdb/mdb.c
        ${lmdb_SOURCE_DIR}/libraries/liblmdb/midl.c
    )
    set_target_properties(lmdb PROPERTIES LINKER_LANGUAGE C)
    target_include_directories(lmdb PUBLIC ${lmdb_SOURCE_DIR}/libraries/liblmdb)
endif()

if(NOT TARGET lua_cffi)
    find_path(REQPACK_FFI_INCLUDE_DIR NAMES ffi.h
        HINTS /opt/homebrew/opt/libffi/include /usr/local/opt/libffi/include)
    find_library(REQPACK_FFI_LIBRARY NAMES ffi libffi
        HINTS /opt/homebrew/opt/libffi/lib /usr/local/opt/libffi/lib)
    if(NOT REQPACK_FFI_INCLUDE_DIR OR NOT REQPACK_FFI_LIBRARY)
        message(FATAL_ERROR "libffi not found (required for lua_cffi when building ReqPack-Core standalone)")
    endif()

    FetchContent_Declare(
        cffi_lua
        GIT_REPOSITORY https://github.com/q66/cffi-lua.git
        GIT_TAG v0.2.4
    )
    FetchContent_MakeAvailable(cffi_lua)

    add_library(lua_cffi STATIC
        ${cffi_lua_SOURCE_DIR}/src/util.cc
        ${cffi_lua_SOURCE_DIR}/src/ffilib.cc
        ${cffi_lua_SOURCE_DIR}/src/parser.cc
        ${cffi_lua_SOURCE_DIR}/src/ast.cc
        ${cffi_lua_SOURCE_DIR}/src/lib.cc
        ${cffi_lua_SOURCE_DIR}/src/ffi.cc
        ${cffi_lua_SOURCE_DIR}/src/main.cc
    )
    set_target_properties(lua_cffi PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(lua_cffi PRIVATE
        ${cffi_lua_SOURCE_DIR}/src
        ${REQPACK_FFI_INCLUDE_DIR}
        ${LUA_INCLUDE_DIR}
    )
    if(DEFINED CMAKE_CXX_BYTE_ORDER)
        if(CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
            target_compile_definitions(lua_cffi PRIVATE FFI_BIG_ENDIAN)
        else()
            target_compile_definitions(lua_cffi PRIVATE FFI_LITTLE_ENDIAN)
        endif()
    elseif(WIN32)
        target_compile_definitions(lua_cffi PRIVATE FFI_LITTLE_ENDIAN)
    else()
        include(TestBigEndian)
        test_big_endian(REQPACK_CFFI_BIG_ENDIAN)
        if(REQPACK_CFFI_BIG_ENDIAN)
            target_compile_definitions(lua_cffi PRIVATE FFI_BIG_ENDIAN)
        else()
            target_compile_definitions(lua_cffi PRIVATE FFI_LITTLE_ENDIAN)
        endif()
    endif()
    target_link_libraries(lua_cffi PUBLIC ${CMAKE_DL_LIBS} ${REQPACK_FFI_LIBRARY})
endif()

find_path(SOL2_INCLUDE_DIR NAMES sol/sol.hpp PATHS /usr/include /usr/local/include)
if(SOL2_INCLUDE_DIR STREQUAL "/usr/include" OR SOL2_INCLUDE_DIR STREQUAL "/usr/local/include")
    set(SOL2_INCLUDE_DIR "")
endif()
if(NOT SOL2_INCLUDE_DIR)
    if(SOL2_INCLUDE_DIR_OVERRIDE)
        set(SOL2_INCLUDE_DIR "${SOL2_INCLUDE_DIR_OVERRIDE}")
    elseif(NOT EXISTS "/usr/include/sol/sol.hpp" AND NOT EXISTS "/usr/local/include/sol/sol.hpp")
        message(FATAL_ERROR "Sol2 headers not found. Pass -DSOL2_INCLUDE_DIR_OVERRIDE=/path/to/sol2/include")
    endif()
endif()

file(GLOB_RECURSE REQPACK_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/plugins/*.cpp"
)

add_library(reqpack_core STATIC ${REQPACK_CORE_SOURCES})
add_library(ReqPack::Core ALIAS reqpack_core)

target_include_directories(reqpack_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/main/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${SOL2_INCLUDE_DIR}
        ${SOL2_INCLUDE_DIR_OVERRIDE}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(reqpack_core PUBLIC SOL_ALL_SAFETIES_ON=1)

target_link_libraries(reqpack_core
    PUBLIC
        reqpack_security_core
        CURL::libcurl
        Boost::boost
        OpenSSL::Crypto
        ${REQPACK_ZSTD_LIBRARY}
        lmdb
        lua_cffi
        ${LUA_LIBRARIES}
        ${REQPACK_SPDLOG_TARGET}
        ${REQPACK_FMT_TARGET}
        ${REQPACK_PLATFORM_LIBS}
)

function(reqpack_core_ensure_output_target)
    if(TARGET reqpack_output)
        return()
    endif()

    set(_cli_dir "${REQPACK_CLI_DIR}")
    if(NOT EXISTS "${_cli_dir}/src/main/cpp/output/logger.cpp")
        message(FATAL_ERROR
            "reqpack_output sources not found under ${REQPACK_CLI_DIR}. "
            "Set REQPACK_CLI_DIR to the ReqPack CLI repository path."
        )
    endif()

    file(GLOB REQPACK_OUTPUT_SOURCES CONFIGURE_DEPENDS
        "${_cli_dir}/src/main/cpp/output/*.cpp"
    )
    add_library(reqpack_output STATIC ${REQPACK_OUTPUT_SOURCES})
    target_include_directories(reqpack_output
        PUBLIC
            "${_cli_dir}/src/main/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/main/include"
    )
    target_link_libraries(reqpack_output
        PUBLIC
            reqpack_security_core
            ${REQPACK_SPDLOG_TARGET}
            ${REQPACK_FMT_TARGET}
    )
    target_link_libraries(reqpack_core PUBLIC reqpack_output)
endfunction()

function(reqpack_core_ensure_cli_target)
    if(TARGET reqpack_cli)
        return()
    endif()

    set(_cli_dir "${REQPACK_CLI_DIR}")
    file(GLOB REQPACK_CLI_SOURCES CONFIGURE_DEPENDS
        "${_cli_dir}/src/main/cpp/cli/*.cpp"
    )
    add_library(reqpack_cli STATIC ${REQPACK_CLI_SOURCES})
    target_include_directories(reqpack_cli
        PUBLIC "${_cli_dir}/src/main/include"
        PRIVATE "${_cli_dir}/src/main/cpp/cli"
    )
    target_link_libraries(reqpack_cli
        PUBLIC
            reqpack_core
            reqpack_output
            CLI11::CLI11
    )
endfunction()

if(NOT TARGET reqpack_output)
    if(REQPACK_CORE_STANDALONE)
        reqpack_core_ensure_output_target()
    endif()
elseif(NOT TARGET reqpack_core)
    # Parent builds reqpack_output after adding this subdirectory.
else()
    target_link_libraries(reqpack_core PUBLIC reqpack_output)
endif()

if(REQPACK_CORE_ENABLE_COVERAGE OR REQPACK_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(reqpack_core PRIVATE -O0 -g --coverage)
        target_link_options(reqpack_core PRIVATE --coverage)
    endif()
endif()

if(REQPACK_CORE_BUILD_TESTS)
    enable_testing()

    reqpack_core_ensure_cli_target()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v2.13.10
    )
    FetchContent_MakeAvailable(Catch2)

    set(REQPACK_CORE_UNIT_TEST_SOURCES
        tests/unit/core/common/action_tokens_tests.cpp
        tests/unit/core/common/version_compare_tests.cpp
        tests/unit/core/common/network_environment_tests.cpp
        tests/unit/core/config/configuration_cli_tests.cpp
        tests/unit/core/config/configuration_parse_tests.cpp
        tests/unit/core/download/downloader_tests.cpp
        tests/unit/core/export/audit_exporter_tests.cpp
        tests/unit/core/export/sbom_exporter_tests.cpp
        tests/unit/core/history/history_manager_tests.cpp
        tests/unit/core/luaenv/lua_env_ffi_tests.cpp
        tests/unit/core/host/host_info_tests.cpp
        tests/unit/core/manifest/manifest_loader_tests.cpp
        tests/unit/core/packages/rq_package_tests.cpp
        tests/unit/core/packages/rq_repository_tests.cpp
        tests/unit/core/planning/planner_core_tests.cpp
        tests/unit/core/planning/request_resolution_tests.cpp
        tests/unit/core/planning/planner_dependency_policy_tests.cpp
        tests/unit/core/execution/executor_update_tests.cpp
        tests/unit/core/execution/executor_task_graph_tests.cpp
        tests/unit/core/registry/registry_database_tests.cpp
        tests/unit/core/registry/registry_json_parser_tests.cpp
        tests/unit/core/registry/registry_query_tests.cpp
        tests/unit/core/registry/registry_trust_tests.cpp
        tests/unit/core/remote/remote_profiles_tests.cpp
        tests/unit/core/security/security_bridge_tests.cpp
        tests/unit/core/state/rqp_state_store_tests.cpp
        tests/unit/core/state/transaction_database_serialization_tests.cpp
        tests/unit/core/state/transaction_database_storage_tests.cpp
        tests/unit/plugins/exec_rules/parse_tests.cpp
        tests/unit/plugins/exec_rules/normalize_tests.cpp
        tests/unit/plugins/exec_rules/evaluation_tests.cpp
        tests/unit/plugins/rqp_plugin_hooks_tests.cpp
        tests/unit/plugins/rqp_plugin_service_tests.cpp
        tests/unit/plugins/lua_bridge_value_mapper_tests.cpp
        tests/unit/plugins/lua_bridge_core_tests.cpp
        tests/unit/plugins/lua_bridge_host_runtime_tests.cpp
        tests/unit/plugins/lua_bridge_bindings_tests.cpp
    )

    add_executable(reqpack_core_unit_tests
        tests/cpp/test_main.cpp
        ${REQPACK_CORE_UNIT_TEST_SOURCES}
    )

    target_include_directories(reqpack_core_unit_tests PRIVATE
        tests/cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/plugins
    )

    target_link_libraries(reqpack_core_unit_tests PRIVATE
        Catch2::Catch2
        reqpack_cli
        reqpack_core
        reqpack_output
        CLI11::CLI11
    )

    set(REQPACK_ORCHESTRATOR_INTEGRATION_TEST_SOURCES)
    if(NOT WIN32)
        list(APPEND REQPACK_ORCHESTRATOR_INTEGRATION_TEST_SOURCES
            tests/integration/core/execution/orchestrator_service_tests.cpp
            tests/integration/core/execution/orchestrator_plugin_flow_tests.cpp
        )
    endif()

    add_executable(reqpack_core_integration_tests
        tests/cpp/test_main.cpp
        tests/integration/core/execution/executor_service_tests.cpp
        ${REQPACK_ORCHESTRATOR_INTEGRATION_TEST_SOURCES}
        tests/integration/core/plugins/plugin_test_runner_service_tests.cpp
        tests/integration/core/planning/planner_service_tests.cpp
        tests/integration/core/registry/registry_service_tests.cpp
        tests/integration/plugins/lua_bridge/service_tests.cpp
        tests/integration/plugins/exec_rules/smoke_tests.cpp
    )

    target_include_directories(reqpack_core_integration_tests PRIVATE tests/cpp)

    target_link_libraries(reqpack_core_integration_tests PRIVATE
        Catch2::Catch2
        reqpack_cli
        reqpack_core
        reqpack_output
        CLI11::CLI11
    )

    if(TARGET ReqPack)
        add_dependencies(reqpack_core_integration_tests ReqPack)
    endif()

    if(REQPACK_TRIVY_HELPER_PATH)
        set(_reqpack_trivy_helper_path "${REQPACK_TRIVY_HELPER_PATH}")
    else()
        set(_reqpack_trivy_helper_path "${CMAKE_BINARY_DIR}/tools/trivydb-exporter/trivydb-exporter")
    endif()

    if(REQPACK_CORE_TEST_BUILD_DIR)
        set(_reqpack_test_build_dir "${REQPACK_CORE_TEST_BUILD_DIR}")
    else()
        set(_reqpack_test_build_dir "${CMAKE_BINARY_DIR}")
    endif()

    foreach(_test_target IN ITEMS reqpack_core_unit_tests reqpack_core_integration_tests)
        target_compile_definitions(${_test_target} PRIVATE
            SOL_ALL_SAFETIES_ON=1
            REQPACK_TEST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
            REQPACK_TEST_BUILD_DIR="${_reqpack_test_build_dir}"
            REQPACK_TRIVY_HELPER_DEFAULT_PATH="${_reqpack_trivy_helper_path}"
        )
        if(REQPACK_RELEASE_ID)
            target_compile_definitions(${_test_target} PRIVATE REQPACK_RELEASE_ID="${REQPACK_RELEASE_ID}")
        endif()
        if(REQPACK_CORE_ENABLE_COVERAGE OR REQPACK_ENABLE_COVERAGE)
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${_test_target} PRIVATE -O0 -g --coverage)
                target_link_options(${_test_target} PRIVATE --coverage)
            endif()
        endif()
    endforeach()

    add_custom_target(reqpack_core_test_targets
        DEPENDS reqpack_core_unit_tests reqpack_core_integration_tests
    )

    include(${catch2_SOURCE_DIR}/contrib/Catch.cmake)
    catch_discover_tests(reqpack_core_unit_tests TEST_PREFIX "core::unit::")
    catch_discover_tests(reqpack_core_integration_tests TEST_PREFIX "core::integration::")
endif()

if(REQPACK_CORE_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "REQPACK_CORE_BUILD_FUZZERS requires Clang (libFuzzer)")
    endif()

    set(REQPACK_CORE_FUZZ_TARGETS
        action_tokens_fuzzer
        registry_json_fuzzer
        manifest_loader_fuzzer
    )
    set(REQPACK_CORE_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g -O1)

    foreach(fuzzer IN LISTS REQPACK_CORE_FUZZ_TARGETS)
        add_executable(${fuzzer} tests/fuzz/${fuzzer}.cpp)
        target_link_libraries(${fuzzer} PRIVATE reqpack_core reqpack_output)
        target_compile_options(${fuzzer} PRIVATE ${REQPACK_CORE_FUZZ_FLAGS})
        target_link_options(${fuzzer} PRIVATE ${REQPACK_CORE_FUZZ_FLAGS})

        set(corpus_dir ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/${fuzzer%_fuzzer})
        if(EXISTS ${corpus_dir})
            add_test(
                NAME core_fuzz_${fuzzer}
                COMMAND ${fuzzer} -runs=4096 -max_total_time=20 -timeout=5 -rss_limit_mb=512 ${corpus_dir}
            )
        else()
            add_test(
                NAME core_fuzz_${fuzzer}
                COMMAND ${fuzzer} -runs=4096 -max_total_time=20 -timeout=5 -rss_limit_mb=512
            )
        endif()
    endforeach()
endif()
