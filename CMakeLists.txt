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

# Render backend: which renderer implements the engine_render facade
# (Docs/render-abstraction.md). BUILD-time selection only - classic OGRE and
# Ogre-Next share the Ogre:: symbol names, so linking both into one binary
# violates the ODR; there is deliberately NO runtime switch. Distinct from
# the ORKIGE_RENDERSYSTEM env var, which stays the runtime graphics-API pick
# (GL3Plus/Metal/Vulkan/GLES2) WITHIN the classic backend.
#
# DEFAULT: next (owner decision 2026-07-08 - Ogre-Next is the engine's
# default backend; the default presets macos-debug/macos-release build it and
# need the render-next vcpkg manifest feature). classic stays the fully
# supported COMPATIBILITY flavor (presets macos-debug-classic /
# macos-release-classic) - it still owns the mobile GLES2 path, the export
# pipeline, BigZip and the Vulkan/GL runtime RS pick. See the flavor
# capability matrix in Docs/render-abstraction.md.
set(ORKIGE_RENDER_BACKEND "next" CACHE STRING "Render backend implementing the engine_render facade (next or classic)")
set_property(CACHE ORKIGE_RENDER_BACKEND PROPERTY STRINGS "next" "classic")
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    # consumed by orkige_engine (compile definition ORKIGE_RENDER_NEXT)
    message(STATUS "Orkige render backend: next (the default; classic-only "
        "excluded: jumper sample, BigZip, export pipeline, mobile GLES2)")
elseif(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    message(STATUS "Orkige render backend: classic (the compatibility flavor; "
        "owns mobile GLES2, exports, BigZip, Vulkan/GL RS pick)")
else()
    message(FATAL_ERROR "ORKIGE_RENDER_BACKEND must be next or classic (got '${ORKIGE_RENDER_BACKEND}')")
endif()

# Backend flip guard: a build tree is flavor-bound - its CMake cache, vcpkg
# manifest installs and every compiled object encode ONE backend, and
# reconfiguring it with the other one poisons all of that silently. Refuse
# honestly instead. Legacy trees (configured before this guard existed) are
# fingerprinted by the find_package result only their flavor caches.
if(DEFINED CACHE{ORKIGE_RENDER_BACKEND_CONFIGURED})
    set(_orkige_backend_was "$CACHE{ORKIGE_RENDER_BACKEND_CONFIGURED}")
elseif(ORKIGE_RENDER_BACKEND STREQUAL "next" AND DEFINED CACHE{OGRE_DIR})
    set(_orkige_backend_was "classic")
elseif(ORKIGE_RENDER_BACKEND STREQUAL "classic" AND DEFINED CACHE{OGRE-Next_DIR})
    set(_orkige_backend_was "next")
else()
    set(_orkige_backend_was "${ORKIGE_RENDER_BACKEND}")
endif()
if(NOT _orkige_backend_was STREQUAL ORKIGE_RENDER_BACKEND)
    message(FATAL_ERROR "This build tree (${CMAKE_BINARY_DIR}) was configured "
        "with the '${_orkige_backend_was}' render backend and cannot be "
        "flipped to '${ORKIGE_RENDER_BACKEND}' in place (stale CMake/vcpkg "
        "caches). Delete the build directory, or use the preset matching the "
        "backend you want: macos-debug / macos-release build next (the "
        "default), macos-debug-classic / macos-release-classic build classic.")
endif()
set(ORKIGE_RENDER_BACKEND_CONFIGURED "${ORKIGE_RENDER_BACKEND}" CACHE INTERNAL
    "render backend this build tree was configured with (guard, do not edit)")

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
        # desktop-only SDL samples (plain executables, host asset paths).
        # hello_orkige builds on both render flavors (per-flavor boot
        # block); jumper stays classic-only - its HUD is fastgui, which
        # migrates to the facade HUD later.
        add_subdirectory(samples/hello_orkige)
        if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
            add_subdirectory(samples/jumper)
        endif()
    endif()
endif()

if(ORKIGE_BUILD_TOOLS)
    if(NOT ORKIGE_BUILD_ENGINE)
        message(FATAL_ERROR "ORKIGE_BUILD_TOOLS requires ORKIGE_BUILD_ENGINE=ON (the editor is an Orkige engine app)")
    endif()
    if(NOT ORKIGE_MOBILE)
        # the editor is desktop-only (imgui/imguizmo are not built for
        # mobile) and builds on BOTH render flavors via the ImGui-on-
        # DrawLayer2D port (Docs/render-abstraction.md)
        add_subdirectory(tools/editor)
    endif()
    # the player builds on both render flavors (per-flavor boot
    # block; games choose their backend)
    add_subdirectory(tools/player)
endif()

if(ORKIGE_BUILD_TESTS AND NOT ORKIGE_MOBILE)
    enable_testing()
    add_subdirectory(tests)
endif()
