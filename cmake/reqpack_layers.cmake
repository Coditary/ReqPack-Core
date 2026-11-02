# ReqPack-Core layer source assignment and library targets.
# Dependency order: workflows → engine → ecosystem → platform

function(reqpack_collect_layer_sources reqpack_core_root)
    file(GLOB_RECURSE REQPACK_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${reqpack_core_root}/src/platform/*.cpp"
    )
    file(GLOB_RECURSE REQPACK_ECOSYSTEM_SOURCES CONFIGURE_DEPENDS
        "${reqpack_core_root}/src/ecosystem/*.cpp"
    )

    file(GLOB_RECURSE REQPACK_ENGINE_SOURCES CONFIGURE_DEPENDS
        "${reqpack_core_root}/src/engine/*.cpp"
    )
    file(GLOB_RECURSE REQPACK_WORKFLOW_SOURCES CONFIGURE_DEPENDS
        "${reqpack_core_root}/src/workflows/*.cpp"
    )

    set(REQPACK_PLATFORM_SOURCES ${REQPACK_PLATFORM_SOURCES} PARENT_SCOPE)
    set(REQPACK_ECOSYSTEM_SOURCES ${REQPACK_ECOSYSTEM_SOURCES} PARENT_SCOPE)
    set(REQPACK_ENGINE_SOURCES ${REQPACK_ENGINE_SOURCES} PARENT_SCOPE)
    set(REQPACK_WORKFLOW_SOURCES ${REQPACK_WORKFLOW_SOURCES} PARENT_SCOPE)
endfunction()

function(reqpack_add_layer_targets reqpack_core_root)
    reqpack_collect_layer_sources("${reqpack_core_root}")

    add_library(reqpack_platform_obj OBJECT ${REQPACK_PLATFORM_SOURCES})
    add_library(reqpack_ecosystem_obj OBJECT ${REQPACK_ECOSYSTEM_SOURCES})
    add_library(reqpack_engine_obj OBJECT ${REQPACK_ENGINE_SOURCES})
    add_library(reqpack_workflows_obj OBJECT ${REQPACK_WORKFLOW_SOURCES})

    add_library(reqpack_platform STATIC $<TARGET_OBJECTS:reqpack_platform_obj>)
    add_library(reqpack_ecosystem STATIC $<TARGET_OBJECTS:reqpack_ecosystem_obj>)
    add_library(reqpack_engine STATIC $<TARGET_OBJECTS:reqpack_engine_obj>)
    add_library(reqpack_workflows STATIC $<TARGET_OBJECTS:reqpack_workflows_obj>)

    add_library(ReqPack::Platform ALIAS reqpack_platform)
    add_library(ReqPack::Ecosystem ALIAS reqpack_ecosystem)
    add_library(ReqPack::Engine ALIAS reqpack_engine)
    add_library(ReqPack::Workflows ALIAS reqpack_workflows)

    foreach(_layer_obj IN ITEMS reqpack_platform_obj reqpack_ecosystem_obj reqpack_engine_obj reqpack_workflows_obj)
        target_include_directories(${_layer_obj}
            PUBLIC
                $<BUILD_INTERFACE:${reqpack_core_root}/include>
                $<BUILD_INTERFACE:${reqpack_core_root}/src>
        )
    endforeach()

    target_include_directories(reqpack_ecosystem_obj PUBLIC
        $<BUILD_INTERFACE:${reqpack_core_root}/src/ecosystem/plugin_host/native_rqp>
        $<BUILD_INTERFACE:${reqpack_core_root}/src/ecosystem/plugin_host/testing>
    )
    target_include_directories(reqpack_engine_obj PUBLIC
        $<BUILD_INTERFACE:${reqpack_core_root}/src/engine/scheduler>
        $<BUILD_INTERFACE:${reqpack_core_root}/src/workflows/orchestrator>
    )
    target_include_directories(reqpack_workflows_obj PUBLIC
        $<BUILD_INTERFACE:${reqpack_core_root}/src/workflows/orchestrator>
    )

    target_link_libraries(reqpack_ecosystem_obj PUBLIC reqpack_platform_obj)
    target_link_libraries(reqpack_engine_obj PUBLIC reqpack_ecosystem_obj reqpack_security_core)
    target_link_libraries(reqpack_workflows_obj PUBLIC reqpack_engine_obj reqpack_security_core)
endfunction()

function(reqpack_layer_object_libs)
    set(REQPACK_LAYER_OBJECT_LIBS
        reqpack_platform_obj
        reqpack_ecosystem_obj
        reqpack_engine_obj
        reqpack_workflows_obj
        PARENT_SCOPE
    )
endfunction()
