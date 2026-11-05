# Resolves reqpack-security-core for standalone clones, CI checkouts, monorepo siblings, or FetchContent.

set(REQPACK_SECURITY_CORE_REPO
    "https://github.com/Coditary/reqpack-security-core.git"
    CACHE STRING "Git repository for reqpack-security-core"
)

set(_reqpack_security_core_default_tag "main")
set(_reqpack_security_core_ref_file "${CMAKE_CURRENT_LIST_DIR}/../.github/reqpack-security-core.ref")
if(EXISTS "${_reqpack_security_core_ref_file}")
    file(READ "${_reqpack_security_core_ref_file}" _reqpack_security_core_ref_content)
    string(STRIP "${_reqpack_security_core_ref_content}" _reqpack_security_core_ref_content)
    if(_reqpack_security_core_ref_content)
        set(_reqpack_security_core_default_tag "${_reqpack_security_core_ref_content}")
    endif()
endif()

set(REQPACK_SECURITY_CORE_TAG
    "${_reqpack_security_core_default_tag}"
    CACHE STRING "Git tag, branch, or commit for reqpack-security-core (FetchContent fallback)"
)

set(REQPACK_SECURITY_CORE_DIR
    ""
    CACHE PATH "Optional local checkout of reqpack-security-core"
)

set(_reqpack_security_core_ci "${CMAKE_CURRENT_LIST_DIR}/../reqpack-security-core")
set(_reqpack_security_core_local "${CMAKE_CURRENT_LIST_DIR}/../../reqpack-security-core")

if(REQPACK_SECURITY_CORE_DIR AND EXISTS "${REQPACK_SECURITY_CORE_DIR}/CMakeLists.txt")
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${REQPACK_SECURITY_CORE_DIR}")
    set(_reqpack_security_core_origin "REQPACK_SECURITY_CORE_DIR")
elseif(EXISTS "${_reqpack_security_core_ci}/CMakeLists.txt")
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${_reqpack_security_core_ci}")
    set(_reqpack_security_core_origin "CI/workspace checkout")
elseif(EXISTS "${_reqpack_security_core_local}/CMakeLists.txt")
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${_reqpack_security_core_local}")
    set(_reqpack_security_core_origin "monorepo sibling")
else()
    set(_reqpack_security_core_shallow TRUE)
    if(REQPACK_SECURITY_CORE_TAG MATCHES "^[0-9a-fA-F]{7,40}$")
        set(_reqpack_security_core_shallow FALSE)
    endif()
    message(STATUS "Fetching reqpack-security-core from ${REQPACK_SECURITY_CORE_REPO} @ ${REQPACK_SECURITY_CORE_TAG}")
    FetchContent_Declare(
        reqpack_security_core
        GIT_REPOSITORY ${REQPACK_SECURITY_CORE_REPO}
        GIT_TAG ${REQPACK_SECURITY_CORE_TAG}
        GIT_SHALLOW ${_reqpack_security_core_shallow}
    )
    FetchContent_MakeAvailable(reqpack_security_core)
    set(REQPACK_SECURITY_CORE_SOURCE_DIR "${reqpack_security_core_SOURCE_DIR}")
    set(_reqpack_security_core_origin "FetchContent")
endif()

message(STATUS "Using reqpack-security-core (${_reqpack_security_core_origin}): ${REQPACK_SECURITY_CORE_SOURCE_DIR}")

find_program(REQPACK_RUSTC rustc)
if(NOT REQPACK_RUSTC)
    message(FATAL_ERROR
        "rustc is required to build reqpack-security-core (CycloneDX via rust/cyclonedx_ffi). "
        "Install Rust from https://rustup.rs/ or set PATH so `rustc` is available.")
endif()

if(NOT TARGET reqpack_security_core)
    set(_rqp_sec_build_tests "${RQP_SECURITY_CORE_BUILD_TESTS}")
    set(RQP_SECURITY_CORE_BUILD_TESTS OFF CACHE BOOL "Build security-core tests when pulled in by ReqPack-Core" FORCE)
    add_subdirectory(
        ${REQPACK_SECURITY_CORE_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/reqpack-security-core-build
    )
    set(RQP_SECURITY_CORE_BUILD_TESTS "${_rqp_sec_build_tests}" CACHE BOOL
        "Build and register security-core unit tests with CTest" FORCE)
endif()
