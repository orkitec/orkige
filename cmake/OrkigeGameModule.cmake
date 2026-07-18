# OrkigeGameModule.cmake - build a project's native (C++) game module
# against the Orkige ENGINE BUILD TREE.
#
# A "native module" is the compiled game code of a .orkproj project (see
# core_project/NativeModule.h for the manifest keys and the editor's
# compile-on-Play pipeline). It is a small standalone CMake project:
#
#     cmake_minimum_required(VERSION 3.28)
#     project(my_game LANGUAGES CXX)
#     if(APPLE)
#         enable_language(OBJCXX)   # only if the module has .mm sources
#     endif()
#     include(${ORKIGE_ROOT}/cmake/OrkigeGameModule.cmake)
#     add_executable(my_game main.cpp)
#     orkige_game_module(my_game)
#
# configured with (the editor's Play button assembles exactly this - see
# NativeModule::configureCommand):
#
#     cmake -G Ninja -S <project>/native -B <project>/native/build-<flavor> \
#         -DCMAKE_BUILD_TYPE=<Debug|Release> \
#         -DORKIGE_ROOT=<engine source root> \
#         -DORKIGE_ENGINE_BUILD_DIR=<engine build dir, e.g.
#                                    build/macos-debug (next) or
#                                    build/macos-debug-classic (classic)>
#
# The module links against EITHER render flavor's engine tree: the flavor is
# read from the engine build's CMakeCache.txt (ORKIGE_RENDER_BACKEND), and the
# module links THAT flavor's render backend closure and gets its ABI macro
# (ORKIGE_RENDER_NEXT or ORKIGE_RENDER_CLASSIC + USE_RTSHADER_SYSTEM) - the
# game code above the engine_render facade compiles the correct branch of the
# flavor-gated engine headers (engine_graphic/Engine.h, engine_render/
# RenderMath.h). Game modules are flavor-neutral by construction: they spell
# only facade types (Orkige::Vec3, engine_render/*), never Ogre::.
#
# What orkige_game_module(<target>) wires up:
#   - include dirs + ABI defines of the engine (ORKIGE_STATIC, the flavor ABI
#     macro, the scripting backend define matching ORKIGE_SCRIPTING, ...)
#   - links liborkige_engine.a / liborkige_core.a straight out of the engine
#     build tree plus their full dependency closure (the flavor's OGRE +
#     render systems + codecs, SDL3, OpenAL, Jolt, tinyxml2, Lua/sol2, ...)
#     resolved through the engine build's own vcpkg_installed/ tree - versions
#     can never diverge
#   - C++20 and the ORKIGE_MODULE_MEDIA_DIR define (the flavor's OGRE Media dir:
#     the classic RTSS shader library / the Ogre-Next Hlms shader templates -
#     the fallback resolveMediaDirectory returns for a dev run, overridden by
#     an exported .app's bundled Media/)
#
# The executable must implement the player CLI contract so the editor can run
# it as the play process:  [scene.oscene] [--project <dir>] [--debug-port N]
# - parse it with Orkige::PlayerArguments and serve the debug protocol with
# Orkige::PlayerDebugLink (engine_runtime/PlayerRuntime.h); the reference
# module is projects/jumper-native/native/.
#
# FLAVOR-BOUND BUILD TREE: a module build tree is flavor-bound like the engine
# tree it links (its CMake cache, the resolved vcpkg targets and every compiled
# object encode ONE flavor). Reconfiguring it against the other flavor's engine
# tree would poison all of that silently, so this file records the flavor and
# FATAL_ERRORs on a flip - delete the build dir, or (the editor/exporter shape)
# use a per-flavor build dir: native/build-next vs native/build-classic, and
# native/build-export-<flavor> for the exporter. All of native/build* is
# gitignored.
#
# HONEST LIMITS (v1):
#   - the ENGINE MUST BE BUILT FIRST for the same build type; the editor
#     guarantees that (it runs out of that very build tree). There is no
#     installed engine SDK yet - this file IS the interim contract, a proper
#     install/find_package(Orkige) story is future work.
#   - desktop host builds only; iOS/Android native modules are out of scope
#     until the export pipeline covers them.
#   - scripting backend defaults to LUA (the tree default); pass
#     -DORKIGE_SCRIPTING=OFF only when the engine build was configured so.

if(NOT DEFINED ORKIGE_ROOT)
    message(FATAL_ERROR "ORKIGE_ROOT is not set - pass the Orkige engine "
        "source root: -DORKIGE_ROOT=/path/to/orkige")
endif()

# ccache, same wiring as the engine's root CMakeLists: module builds
# (compile-on-Play, the exporter's build-export tree, the break-variant test)
# recompile the same fat OGRE-including TU - cache what is cacheable. A
# launcher the caller already chose wins.
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(ORKIGE_MODULE_CCACHE ccache)
    if(ORKIGE_MODULE_CCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER "${ORKIGE_MODULE_CCACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${ORKIGE_MODULE_CCACHE}")
        set(CMAKE_OBJC_COMPILER_LAUNCHER "${ORKIGE_MODULE_CCACHE}")
        set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${ORKIGE_MODULE_CCACHE}")
    endif()
endif()
if(NOT DEFINED ORKIGE_ENGINE_BUILD_DIR)
    message(FATAL_ERROR "ORKIGE_ENGINE_BUILD_DIR is not set - pass the engine "
        "build tree to link against: -DORKIGE_ENGINE_BUILD_DIR="
        "${ORKIGE_ROOT}/build/macos-debug")
endif()

# The render flavor the engine tree was built with - read from ITS cache. The
# module links that flavor's OGRE closure and defines its ABI macro so the
# flavor-gated engine headers compile the matching branch. Default classic when
# the marker is absent (a legacy tree from before the guard existed).
set(ORKIGE_MODULE_FLAVOR "classic")
if(EXISTS "${ORKIGE_ENGINE_BUILD_DIR}/CMakeCache.txt")
    file(STRINGS "${ORKIGE_ENGINE_BUILD_DIR}/CMakeCache.txt"
        _orkige_module_backend_line REGEX "^ORKIGE_RENDER_BACKEND:")
    if(_orkige_module_backend_line MATCHES "=next$")
        set(ORKIGE_MODULE_FLAVOR "next")
    endif()
    # ABI-relevant flags ride along like the flavor: a sanitizer-instrumented
    # engine's static libs reference runtime symbols (__asan_*/__ubsan_*) that
    # only the same -fsanitize= compile AND link provide - a module built
    # plain against such a tree dies at link with thousands of undefined
    # sanitizer symbols.
    file(STRINGS "${ORKIGE_ENGINE_BUILD_DIR}/CMakeCache.txt"
        _orkige_module_cxxflags_line REGEX "^CMAKE_CXX_FLAGS:")
    if(_orkige_module_cxxflags_line MATCHES "(-fsanitize=[^ ;]+)")
        set(_orkige_module_sanitize "${CMAKE_MATCH_1}")
        string(APPEND CMAKE_CXX_FLAGS " ${_orkige_module_sanitize} -fno-omit-frame-pointer")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " ${_orkige_module_sanitize}")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS " ${_orkige_module_sanitize}")
        message(STATUS "engine tree is sanitizer-instrumented - module inherits ${_orkige_module_sanitize}")
    endif()
endif()

# Flavor-bind guard (mirrors the engine root CMakeLists' ORKIGE_RENDER_BACKEND_
# CONFIGURED guard): a module tree configured against one flavor cannot be
# flipped to the other in place.
if(DEFINED CACHE{ORKIGE_MODULE_FLAVOR_CONFIGURED})
    if(NOT "$CACHE{ORKIGE_MODULE_FLAVOR_CONFIGURED}" STREQUAL ORKIGE_MODULE_FLAVOR)
        message(FATAL_ERROR "this native-module build tree (${CMAKE_BINARY_DIR}) "
            "was configured against the '$CACHE{ORKIGE_MODULE_FLAVOR_CONFIGURED}' "
            "render flavor and cannot be flipped to '${ORKIGE_MODULE_FLAVOR}' in "
            "place (stale CMake cache + linked backend objects). Delete this "
            "build directory, or use a per-flavor build dir - the editor and "
            "exporter do: native/build-next vs native/build-classic (and "
            "native/build-export-<flavor> for exports).")
    endif()
endif()
set(ORKIGE_MODULE_FLAVOR_CONFIGURED "${ORKIGE_MODULE_FLAVOR}" CACHE INTERNAL
    "render flavor this module tree was configured against (guard, do not edit)")

set(ORKIGE_SCRIPTING "LUA" CACHE STRING
    "Scripting backend the engine build was configured with (LUA or OFF)")

# the engine libraries this module links; their absence means the engine was
# never built (or a wrong build dir) - fail with the fix, not a linker error
set(_orkige_engine_lib
    "${ORKIGE_ENGINE_BUILD_DIR}/orkige_engine/${CMAKE_STATIC_LIBRARY_PREFIX}orkige_engine${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_orkige_core_lib
    "${ORKIGE_ENGINE_BUILD_DIR}/orkige_core/${CMAKE_STATIC_LIBRARY_PREFIX}orkige_core${CMAKE_STATIC_LIBRARY_SUFFIX}")
foreach(_orkige_lib IN ITEMS "${_orkige_engine_lib}" "${_orkige_core_lib}")
    if(NOT EXISTS "${_orkige_lib}")
        message(FATAL_ERROR "engine library '${_orkige_lib}' does not exist - "
            "build the engine first (cmake --build --preset <preset>) or fix "
            "ORKIGE_ENGINE_BUILD_DIR")
    endif()
endforeach()

# The engine's dependencies come from ITS build's vcpkg_installed tree (vcpkg
# manifest mode installs into the build dir) - pointing CMAKE_PREFIX_PATH at
# the triplet root lets the plain find_packages below resolve the exact
# packages the engine libraries were compiled against, without requiring the
# vcpkg toolchain here.
file(GLOB _orkige_vcpkg_triplets
    LIST_DIRECTORIES true "${ORKIGE_ENGINE_BUILD_DIR}/vcpkg_installed/*")
set(ORKIGE_VCPKG_PREFIX "")
foreach(_orkige_triplet_dir IN LISTS _orkige_vcpkg_triplets)
    # skip vcpkg's own bookkeeping dir; a real triplet dir has include/
    if(IS_DIRECTORY "${_orkige_triplet_dir}/include")
        set(ORKIGE_VCPKG_PREFIX "${_orkige_triplet_dir}")
    endif()
endforeach()
if(NOT ORKIGE_VCPKG_PREFIX)
    message(FATAL_ERROR "no vcpkg_installed/<triplet> under "
        "'${ORKIGE_ENGINE_BUILD_DIR}' - is that really an Orkige build tree?")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${ORKIGE_VCPKG_PREFIX}")
# MODULE-mode finds reached through the config packages (OGRE-Next's
# find_dependency(ZLIB) -> FindZLIB) locate libraries via find_library, which
# does not search a prefix's per-config lib layout on its own - without the
# vcpkg toolchain, replicate its per-config dispatch: Debug prefers the
# triplet's debug/lib (zlibd & friends), everything falls back to lib. On
# Apple/Linux hosts a system zlib masks this; Windows has none.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND EXISTS "${ORKIGE_VCPKG_PREFIX}/debug/lib")
    list(PREPEND CMAKE_LIBRARY_PATH "${ORKIGE_VCPKG_PREFIX}/debug/lib")
endif()
list(APPEND CMAKE_LIBRARY_PATH "${ORKIGE_VCPKG_PREFIX}/lib")

# vcpkg packages resolve transitive dependencies (freetype's bzip2/brotli,
# assimp's FindStb, lua's unofficial-lua redirection, ...) through per-port
# find_package wrappers the vcpkg toolchain normally dispatches to. Without
# the toolchain, replicate exactly that dispatch: redefining find_package
# makes the original available as _find_package (which is precisely the name
# the wrapper files call), and each wrapper receives its arguments in ARGS.
macro(find_package name)
    string(TOLOWER "${name}" _orkige_find_package_lower)
    if(EXISTS "${ORKIGE_VCPKG_PREFIX}/share/${_orkige_find_package_lower}/vcpkg-cmake-wrapper.cmake")
        set(ARGS "${ARGV}")
        include("${ORKIGE_VCPKG_PREFIX}/share/${_orkige_find_package_lower}/vcpkg-cmake-wrapper.cmake")
    else()
        _find_package(${ARGV})
    endif()
endmacro()

# the package set orkige_core + orkige_engine link, per flavor (see their
# CMakeLists.txt); the imported targets carry the full transitive closure. The
# backend-agnostic packages are shared; OGRE differs per flavor.
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
# the gui runtime atlas rasterises SVG UI sprites through nanosvg's
# precompiled static libs; the engine archive references their symbols
find_package(NanoSVG CONFIG REQUIRED)
if(ORKIGE_MODULE_FLAVOR STREQUAL "next")
    # the Ogre-Next backend (namespaced OgreNext::*). assimp backs the skinned-
    # rig extraction AND the next backend's own mesh import path (Ogre-Next has
    # no assimp codec of its own).
    find_package(OGRE-Next CONFIG REQUIRED)
    find_package(assimp CONFIG REQUIRED)
    if(NOT DEFINED OGRE_MEDIA_DIR)
        set(OGRE_MEDIA_DIR "${ORKIGE_VCPKG_PREFIX}/share/ogre-next/Media")
    endif()
else()
    find_package(OGRE CONFIG REQUIRED COMPONENTS Overlay RTShaderSystem)
    if(NOT DEFINED OGRE_MEDIA_DIR)
        set(OGRE_MEDIA_DIR "${ORKIGE_VCPKG_PREFIX}/share/ogre/Media")
    endif()
endif()
if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # the lua wrapper redirects to the multi-config-correct unofficial-lua
    # package and fills LUA_INCLUDE_DIR/LUA_LIBRARIES
    find_package(Lua REQUIRED)
    find_package(sol2 CONFIG REQUIRED)
endif()

# the OGRE Media dir the module's runtime resolves as the dev-run fallback
# (classic RTSS shader library / Ogre-Next Hlms shader templates), exported
# both as a variable and (in orkige_game_module) as the ORKIGE_MODULE_MEDIA_DIR
# compile define
set(ORKIGE_GAME_MODULE_MEDIA_DIR "${OGRE_MEDIA_DIR}")

# IDE support: export the module's compilation database so clangd can serve
# correct diagnostics for module sources (see the .clangd next to the module -
# the engine's database does not cover standalone module projects).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(orkige_game_module target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${ORKIGE_ROOT}/orkige_core"
        "${ORKIGE_ROOT}/orkige_engine"
    )
    # ABI-relevant defines matching the engine build (see the root
    # CMakeLists.txt and orkige_engine/CMakeLists.txt)
    target_compile_definitions(${target} PRIVATE
        ORKIGE_STATIC
        ORKIGE_OPENAL_SOUND
        ORKIGE_ENGINE_HAS_GOCOMPONENT
        ORKIGE_MODULE_MEDIA_DIR="${ORKIGE_GAME_MODULE_MEDIA_DIR}"
    )
    # the render flavor ABI macro: engine_graphic/Engine.h and engine_render/
    # RenderMath.h gate on ORKIGE_RENDER_NEXT (else the classic <Ogre.h>+RTSS
    # path); the module MUST match the engine tree's flavor so the game code
    # compiles the SAME branch the engine libraries were built with.
    if(ORKIGE_MODULE_FLAVOR STREQUAL "next")
        target_compile_definitions(${target} PRIVATE ORKIGE_RENDER_NEXT)
    else()
        target_compile_definitions(${target} PRIVATE
            ORKIGE_RENDER_CLASSIC
            USE_RTSHADER_SYSTEM)
    endif()
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        target_compile_definitions(${target} PRIVATE ORKIGE_LUA)
        target_include_directories(${target} PRIVATE ${LUA_INCLUDE_DIR})
        # sol2 is header-only; the Lua archive itself is linked AFTER the
        # engine archives below - GNU ld resolves archives left to right, so
        # a provider listed before its consumers drops the symbols the
        # engine archives need (luaL_error etc.); ld64 does not care, which
        # is why the wrong order never surfaced on macOS
        target_link_libraries(${target} PRIVATE sol2::sol2)
    else()
        target_compile_definitions(${target} PRIVATE ORKIGE_NOSCRIPT)
    endif()
    # the engine build-tree archives first, then their dependency closure
    # (mirrors the PUBLIC/PRIVATE link set of orkige_core + orkige_engine); the
    # backend-agnostic deps are shared, the OGRE closure is per flavor.
    target_link_libraries(${target} PRIVATE
        "${_orkige_engine_lib}"
        "${_orkige_core_lib}"
        tinyxml2::tinyxml2
        SDL3::SDL3
        OpenAL::OpenAL
        Jolt::Jolt
        NanoSVG::nanosvg
        NanoSVG::nanosvgrast
    )
    if(ORKIGE_MODULE_FLAVOR STREQUAL "next")
        # the Ogre-Next backend closure (see orkige_engine/CMakeLists.txt); one
        # render system per platform - Metal on Apple, Vulkan elsewhere
        target_link_libraries(${target} PRIVATE
            OgreNext::Main
            OgreNext::HlmsPbs
            OgreNext::HlmsUnlit
            OgreNext::Atmosphere
            assimp::assimp
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
            message(FATAL_ERROR "native game modules are desktop-only for now "
                "(mobile builds go through the export pipeline once it exists)")
        elseif(APPLE)
            target_link_libraries(${target} PRIVATE OgreNext::RenderSystem_Metal)
        else()
            target_link_libraries(${target} PRIVATE OgreNext::RenderSystem_Vulkan)
        endif()
    else()
        target_link_libraries(${target} PRIVATE
            OgreOverlay
            OgreRTShaderSystem
            Codec_STBI
            Codec_Assimp
            OgreMain
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
            message(FATAL_ERROR "native game modules are desktop-only for now "
                "(mobile builds go through the export pipeline once it exists)")
        else()
            target_link_libraries(${target} PRIVATE RenderSystem_GL3Plus)
        endif()
        if(APPLE)
            target_link_libraries(${target} PRIVATE RenderSystem_Metal)
        endif()
        # classic Vulkan render system (present when OGRE's 'vulkan' feature is
        # on); runtime-selectable via ORKIGE_RENDERSYSTEM=Vulkan
        if(TARGET RenderSystem_Vulkan)
            target_compile_definitions(${target} PRIVATE ORKIGE_HAVE_VULKAN)
            target_link_libraries(${target} PRIVATE
                RenderSystem_Vulkan
                Plugin_GLSLangProgramManager)
            if(APPLE)
                # same loader arrangement as the engine build: vcpkg's Vulkan
                # loader on the link line + rpath so volk's dlopen finds it
                find_library(ORKIGE_VULKAN_LOADER_LIBRARY NAMES vulkan
                    PATHS "${ORKIGE_VCPKG_PREFIX}/lib" REQUIRED)
                target_link_libraries(${target} PRIVATE
                    ${ORKIGE_VULKAN_LOADER_LIBRARY})
            endif()
        endif()
    endif()
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        # after the engine archives (see the ordering note above)
        target_link_libraries(${target} PRIVATE ${LUA_LIBRARIES})
    endif()
    if(APPLE)
        # same benign-duplicate silencing as the engine's root CMakeLists:
        # the static closure legitimately repeats archives on the link line
        target_link_options(${target} PRIVATE
            LINKER:-no_warn_duplicate_libraries)
        # frameworks orkige_core/orkige_engine list on their PUBLIC link
        # interface (PlatformUtil.mm, LoadCafData.mm); the OGRE/SDL imported
        # targets carry their own framework closure themselves
        target_link_libraries(${target} PRIVATE
            "-framework Foundation"
            "-framework CoreServices"
            "-framework AudioToolbox"
            "-framework CoreFoundation"
        )
    endif()
endfunction()
