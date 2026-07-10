# orkige_core - platform- and renderer-independent engine core.
# core_python is excluded until scripting bindings are rebuilt (Lua-first).

set(ORKIGE_CORE_MODULES
    core_base
    core_debug
    core_debugnet
    core_event
    core_game
    core_module
    core_project
    core_script
    core_serialization
    core_tween
    core_util
)

set(ORKIGE_CORE_SOURCES "")
foreach(module IN LISTS ORKIGE_CORE_MODULES)
    file(GLOB module_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.h"
    )
    list(APPEND ORKIGE_CORE_SOURCES ${module_sources})
endforeach()

if(NOT ORKIGE_SCRIPTING STREQUAL "LUA")
    # the neutral core_script/ScriptRuntime.h seam is ALWAYS compiled (it is
    # what keeps call sites free of ORKIGE_LUA guards); only the sol2-backed
    # Lua backend behind it drops out of no-scripting builds
    list(REMOVE_ITEM ORKIGE_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/core_script/ScriptManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core_script/ScriptManager.h"
    )
endif()

if(APPLE)
    file(GLOB core_objcxx_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/core_util/*.mm"
    )
    list(APPEND ORKIGE_CORE_SOURCES ${core_objcxx_sources})
endif()

add_library(orkige_core STATIC ${ORKIGE_CORE_SOURCES})
add_library(Orkige::Core ALIAS orkige_core)

target_include_directories(orkige_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

find_package(tinyxml2 CONFIG REQUIRED)
target_link_libraries(orkige_core PRIVATE tinyxml2::tinyxml2)
if(WIN32)
    # the debug-net socket layer's winsock backend (WSAStartup, closesocket)
    target_link_libraries(orkige_core PUBLIC ws2_32)
endif()

if(APPLE)
    # core_util/PlatformUtil.mm uses Foundation (NSFileManager & co) and
    # CoreServices (FSFindFolder); PUBLIC because static-lib consumers must
    # link the frameworks themselves
    target_link_libraries(orkige_core PUBLIC
        "-framework Foundation"
        "-framework CoreServices"
    )
endif()

if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # PUBLIC: the Meta_Lua.h macros (and thus sol2/lua headers) surface in
    # the headers of every dependent target
    find_package(Lua REQUIRED)
    find_package(sol2 CONFIG REQUIRED)
    target_include_directories(orkige_core PUBLIC ${LUA_INCLUDE_DIR})
    target_link_libraries(orkige_core PUBLIC sol2::sol2 ${LUA_LIBRARIES})

    # sol2 reaches nearly every TU through the Meta_Lua.h backend and is by
    # far the heaviest include in the layer - precompile it. CXX-only guard:
    # the target has ObjC++ sources on Apple platforms. Adding the heavy std
    # headers next to it was measured as a no-op (sol.hpp already includes
    # them, so they ride along in this PCH).
    target_precompile_headers(orkige_core PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<sol/sol.hpp$<ANGLE-R>>"
    )
    orkige_pch_ccache_compat(orkige_core)
endif()
