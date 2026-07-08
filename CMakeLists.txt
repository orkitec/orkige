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

# iOS cross-build (preset ios-simulator-debug): ORKIGE_IPHONE is the engine's
# historical iOS define (PlatformUtil bundle paths, Engine iPhone/iPad config
# handling). Desktop-only targets (editor, desktop samples, tests) are gated
# on ORKIGE_MOBILE below.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(ORKIGE_IOS TRUE)
    add_compile_definitions(ORKIGE_IPHONE)
else()
    set(ORKIGE_IOS FALSE)
endif()
# Android cross-build (preset android-debug): the engine's historical Android
# code keys off the compiler-provided __ANDROID__, no extra define needed.
# The player builds as the SDL-loaded shared lib libmain.so packaged into an
# APK by tools/player/android/package_apk.sh.
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(ORKIGE_ANDROID TRUE)
else()
    set(ORKIGE_ANDROID FALSE)
endif()
if(ORKIGE_IOS OR ORKIGE_ANDROID)
    set(ORKIGE_MOBILE TRUE)
else()
    set(ORKIGE_MOBILE FALSE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    # the same static archive (zlib & friends) legitimately reaches a link
    # line through several vcpkg dependency chains; Xcode 15+'s ld warns
    # about the (harmless, deduplicated) repetition - silence just that
    add_link_options(LINKER:-no_warn_duplicate_libraries)
endif()

# ccache: transparent compiler cache for every language in the build. Rebuilds
# after a clean (or branch switch) come from the cache instead of the compiler.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_OBJC_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Call on every target that uses target_precompile_headers. Clang embeds a
# timestamp into the .pch by default, which makes the PCH (and thus every TU
# consuming it) uncacheable for ccache; -fno-pch-timestamp disables that
# (Ninja's dependency tracking handles PCH staleness, the timestamp is
# redundant here). The companion one-time ccache setting is
#   ccache --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
# (see "Build speed" in CLAUDE.md).
function(orkige_pch_ccache_compat target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
    endif()
endfunction()

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
    if(NOT ORKIGE_MOBILE)
        # desktop-only SDL samples (plain executables, host asset paths)
        add_subdirectory(samples/hello_orkige)
        add_subdirectory(samples/jumper)
    endif()
endif()

if(ORKIGE_BUILD_TOOLS)
    if(NOT ORKIGE_BUILD_ENGINE)
        message(FATAL_ERROR "ORKIGE_BUILD_TOOLS requires ORKIGE_BUILD_ENGINE=ON (the editor is an Orkige engine app)")
    endif()
    if(NOT ORKIGE_MOBILE)
        # the editor is desktop-only (imgui/imguizmo are not built for mobile)
        add_subdirectory(tools/editor)
    endif()
    add_subdirectory(tools/player)
endif()

if(ORKIGE_BUILD_TESTS AND NOT ORKIGE_MOBILE)
    enable_testing()
    add_subdirectory(tests)
endif()
