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
option(ORKIGE_BUILD_TOOLS  "Build the tool executables" OFF)

# Everything is built statically during the modernization; shared-library
# packaging is revisited once the engine runs again.
# ORKIGE_NOSCRIPT: the old Lua meta backend depends on the abandoned luabind
# library; script bindings return on sol2 in a later phase.
add_compile_definitions(ORKIGE_STATIC ORKIGE_NOSCRIPT)

add_subdirectory(orkige_core)

if(ORKIGE_BUILD_ENGINE)
    add_subdirectory(orkige_engine)
    add_subdirectory(samples/hello_orkige)
endif()
