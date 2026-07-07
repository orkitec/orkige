cmake_minimum_required(VERSION 3.28)

project(Orkige
    VERSION 2.0.0
    DESCRIPTION "Orkige - the orkitec game engine"
    HOMEPAGE_URL "https://github.com/orkitec/orkige"
    LANGUAGES CXX
)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORKIGE_BUILD_ENGINE "Build the engine modules (requires OGRE)" OFF)
# The tools (currently the editor) sit on top of the engine, so they follow
# ORKIGE_BUILD_ENGINE by default; override with -DORKIGE_BUILD_TOOLS=ON/OFF.
option(ORKIGE_BUILD_TOOLS  "Build the tool executables" ${ORKIGE_BUILD_ENGINE})
option(ORKIGE_BUILD_TESTS  "Build the unit tests and register the ctest suite" ON)

# Scripting backend: LUA (sol2-based Meta_Lua.h backend, the default) or OFF
# (Meta_None.h, no script bindings compiled).
set(ORKIGE_SCRIPTING "LUA" CACHE STRING "Scripting backend (LUA or OFF)")
set_property(CACHE ORKIGE_SCRIPTING PROPERTY STRINGS "LUA" "OFF")

# Everything is built statically during the modernization; shared-library
# packaging is revisited once the engine runs again.
if(ORKIGE_SCRIPTING STREQUAL "LUA")
    add_compile_definitions(ORKIGE_STATIC ORKIGE_LUA)
elseif(ORKIGE_SCRIPTING STREQUAL "OFF")
    add_compile_definitions(ORKIGE_STATIC ORKIGE_NOSCRIPT)
else()
    message(FATAL_ERROR "ORKIGE_SCRIPTING must be LUA or OFF (got '${ORKIGE_SCRIPTING}')")
endif()

add_subdirectory(orkige_core)

if(ORKIGE_BUILD_ENGINE)
    add_subdirectory(orkige_engine)
    add_subdirectory(samples/hello_orkige)
endif()

if(ORKIGE_BUILD_TOOLS)
    if(NOT ORKIGE_BUILD_ENGINE)
        message(FATAL_ERROR "ORKIGE_BUILD_TOOLS requires ORKIGE_BUILD_ENGINE=ON (the editor is an Orkige engine app)")
    endif()
    add_subdirectory(tools/editor)
    add_subdirectory(tools/player)
endif()

if(ORKIGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
