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
#     cmake -G Ninja -S <project>/native -B <project>/native/build \
#         -DCMAKE_BUILD_TYPE=<Debug|Release> \
#         -DORKIGE_ROOT=<engine source root> \
#         -DORKIGE_ENGINE_BUILD_DIR=<engine build dir, e.g.
#                                    build/macos-debug-classic - native
#                                    modules are classic-flavor-only for now>
#
# What orkige_game_module(<target>) wires up:
#   - include dirs + ABI defines of the engine (ORKIGE_STATIC, the scripting
#     backend define matching ORKIGE_SCRIPTING, USE_RTSHADER_SYSTEM, ...)
#   - links liborkige_engine.a / liborkige_core.a straight out of the engine
#     build tree plus their full dependency closure (OGRE + render systems +
#     codecs, SDL3, OpenAL, Jolt, tinyxml2, Lua/sol2) resolved through the
#     engine build's own vcpkg_installed/ tree - versions can never diverge
#   - C++20 and the ORKIGE_MODULE_MEDIA_DIR define (the vcpkg OGRE Media dir
#     carrying the RTSS shader library the runtime must register)
#
# The executable must implement the player CLI contract so the editor can run
# it as the play process:  [scene.oscene] [--project <dir>] [--debug-port N]
# - parse it with Orkige::PlayerArguments and serve the debug protocol with
# Orkige::PlayerDebugLink (engine_runtime/PlayerRuntime.h); the reference
# module is projects/jumper-native/native/.
#
# HONEST LIMITS (v1, revisit with the export milestone):
#   - the ENGINE MUST BE BUILT FIRST for the same build type; the editor
#     guarantees that (it runs out of that very build tree). There is no
#     installed engine SDK yet - this file IS the interim contract, a proper
#     install/find_package(Orkige) story is future work.
#   - desktop host builds only; iOS/Android native modules are out of scope
#     until the export pipeline exists.
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
        "${ORKIGE_ROOT}/build/macos-debug-classic")
endif()

# Native modules are CLASSIC-flavor-only for now: this file links the classic
# OGRE closure (OgreOverlay/RTSS/GL3Plus) and defines the classic ABI set - a
# next-flavor engine tree would link but misbehave. Refuse honestly.
# TODO(next-modules): per-flavor link/define sets once a next-flavor module
# story (and export) exists.
if(EXISTS "${ORKIGE_ENGINE_BUILD_DIR}/CMakeCache.txt")
    file(STRINGS "${ORKIGE_ENGINE_BUILD_DIR}/CMakeCache.txt"
        _orkige_module_backend_line REGEX "^ORKIGE_RENDER_BACKEND:")
    if(_orkige_module_backend_line MATCHES "=next$")
        message(FATAL_ERROR "engine build tree '${ORKIGE_ENGINE_BUILD_DIR}' "
            "is the Ogre-Next render flavor - native game modules are "
            "classic-only for now; point ORKIGE_ENGINE_BUILD_DIR at a "
            "classic tree (preset macos-debug-classic/-release-classic)")
    endif()
endif()
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

# the exact package set orkige_core + orkige_engine link (see their
# CMakeLists.txt); the imported targets carry the full transitive closure
find_package(OGRE CONFIG REQUIRED COMPONENTS Overlay RTShaderSystem)
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # the lua wrapper redirects to the multi-config-correct unofficial-lua
    # package and fills LUA_INCLUDE_DIR/LUA_LIBRARIES
    find_package(Lua REQUIRED)
    find_package(sol2 CONFIG REQUIRED)
endif()

# the OGRE media dir (RTSS shader library) the module's runtime must register
# as a resource location, exported both as a variable and (in
# orkige_game_module) as the ORKIGE_MODULE_MEDIA_DIR compile define
if(NOT DEFINED OGRE_MEDIA_DIR)
    set(OGRE_MEDIA_DIR "${ORKIGE_VCPKG_PREFIX}/share/ogre/Media")
endif()
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
        USE_RTSHADER_SYSTEM
        ORKIGE_OPENAL_SOUND
        ORKIGE_ENGINE_HAS_GOCOMPONENT
        ORKIGE_MODULE_MEDIA_DIR="${ORKIGE_GAME_MODULE_MEDIA_DIR}"
    )
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        target_compile_definitions(${target} PRIVATE ORKIGE_LUA)
        target_include_directories(${target} PRIVATE ${LUA_INCLUDE_DIR})
        target_link_libraries(${target} PRIVATE sol2::sol2 ${LUA_LIBRARIES})
    else()
        target_compile_definitions(${target} PRIVATE ORKIGE_NOSCRIPT)
    endif()
    # the engine build-tree archives first, then their dependency closure
    # (mirrors the PUBLIC/PRIVATE link set of orkige_core + orkige_engine)
    target_link_libraries(${target} PRIVATE
        "${_orkige_engine_lib}"
        "${_orkige_core_lib}"
        tinyxml2::tinyxml2
        OgreOverlay
        OgreRTShaderSystem
        Codec_STBI
        Codec_Assimp
        OgreMain
        SDL3::SDL3
        OpenAL::OpenAL
        Jolt::Jolt
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        message(FATAL_ERROR "native game modules are desktop-only for now "
            "(mobile builds go through the export pipeline once it exists)")
    else()
        target_link_libraries(${target} PRIVATE RenderSystem_GL3Plus)
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
            RenderSystem_Metal
        )
    endif()
    if(TARGET RenderSystem_Vulkan)
        target_compile_definitions(${target} PRIVATE ORKIGE_HAVE_VULKAN)
        target_link_libraries(${target} PRIVATE
            RenderSystem_Vulkan
            Plugin_GLSLangProgramManager
        )
        if(APPLE)
            # same loader arrangement as the engine build: vcpkg's Vulkan
            # loader on the link line + rpath so volk's dlopen finds it
            find_library(ORKIGE_VULKAN_LOADER_LIBRARY NAMES vulkan
                PATHS "${ORKIGE_VCPKG_PREFIX}/lib" REQUIRED)
            target_link_libraries(${target} PRIVATE
                ${ORKIGE_VULKAN_LOADER_LIBRARY})
        endif()
    endif()
endfunction()
