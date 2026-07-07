# orkige_core - platform- and renderer-independent engine core.
# core_python is excluded until scripting bindings are rebuilt (Lua-first).

set(ORKIGE_CORE_MODULES
    core_base
    core_debug
    core_event
    core_game
    core_module
    core_serialization
    core_util
)

if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # core_script owns the sol2 Lua state used by the Meta_Lua.h backend
    list(APPEND ORKIGE_CORE_MODULES core_script)
endif()

set(ORKIGE_CORE_SOURCES "")
foreach(module IN LISTS ORKIGE_CORE_MODULES)
    file(GLOB module_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.h"
    )
    list(APPEND ORKIGE_CORE_SOURCES ${module_sources})
endforeach()

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

if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # PUBLIC: the Meta_Lua.h macros (and thus sol2/lua headers) surface in
    # the headers of every dependent target
    find_package(Lua REQUIRED)
    find_package(sol2 CONFIG REQUIRED)
    target_include_directories(orkige_core PUBLIC ${LUA_INCLUDE_DIR})
    target_link_libraries(orkige_core PUBLIC sol2::sol2 ${LUA_LIBRARIES})
endif()
