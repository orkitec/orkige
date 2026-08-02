# OrkigeGameModule.cmake - build a project's native (C++) game module
# against the Orkige engine.
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
# configured against an engine SOURCE + BUILD TREE with (the editor's Play
# button assembles exactly this - see NativeModule::configureCommand):
#
#     cmake -G Ninja -S <project>/native -B <project>/native/build-<flavor> \
#         -DCMAKE_BUILD_TYPE=<Debug|Release> \
#         -DORKIGE_ROOT=<engine source root> \
#         -DORKIGE_ENGINE_BUILD_DIR=<engine build dir, e.g.
#                                    build/macos-debug (next) or
#                                    build/macos-debug-classic (classic)>
#
# or against an installed SDK PACK (Docs/sdk-pack.md) with neither, by including
# the copy of this file the pack carries:
#
#     cmake -G Ninja -S <project>/native -B <project>/native/build-<flavor> \
#         -DCMAKE_BUILD_TYPE=Release -DORKIGE_ROOT=<pack>
#
# The module links against EITHER render flavor's engine: the flavor comes from
# the package (which records what its archives were built with), and the
# module links THAT flavor's render backend closure and gets its ABI macro
# (ORKIGE_RENDER_NEXT or ORKIGE_RENDER_CLASSIC + USE_RTSHADER_SYSTEM) - the
# game code above the engine_render facade compiles the correct branch of the
# flavor-gated engine headers (engine_graphic/Engine.h, engine_render/
# RenderMath.h). Game modules are flavor-neutral by construction: they spell
# only facade types (Orkige::Vec3, engine_render/*), never Ogre::.
#
# The engine is resolved as ONE find_package(Orkige) package - emitted into the
# engine build tree by cmake/OrkigePackage.cmake, or installed as part of a
# relocatable SDK pack by cmake/OrkigeSdk.cmake - that exports TWO imported
# targets sharing ONE ABI stamp: Orkige::Core (the OGRE-free core) and
# Orkige::Engine (which pulls Orkige::Core transitively). find_package requires
# an EXACT ABI-stamp match, so a module compiled against newer engine headers
# than the archive it links fails HERE at configure with a clear message - never
# as a runtime crash from a skewed object layout. The stamp is a content
# fingerprint of the engine's ABI surface (cmake/OrkigeAbiStamp.cmake). The
# editor/player/tests build in the engine graph itself and never drift, so they
# are deliberately NOT migrated onto find_package.
#
# What orkige_game_module(<target>) wires up:
#   - links Orkige::Engine (the find_package package's imported target), which
#     carries the engine include roots + ABI defines (ORKIGE_STATIC, the flavor
#     ABI macro, the scripting backend define) and pulls Orkige::Core
#   - plus the full dependency closure (the flavor's OGRE + render systems +
#     codecs, SDL3, OpenAL, Jolt, tinyxml2, Lua/sol2, ...) resolved through the
#     prefix the package points at - the build's own vcpkg_installed/<triplet>
#     tree, or the pack's bundled vcpkg/. Either way these are the exact
#     binaries the engine archives were linked against, so versions can never
#     diverge
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
# HONEST LIMITS:
#   - against a BUILD TREE the engine must be built first for the same build
#     type; the editor guarantees that (it runs out of that very build tree).
#     An SDK pack carries one configuration - Release - so a module built
#     against a pack builds Release (Docs/sdk-pack.md).
#   - desktop host packs only; iOS/Android/wasm SDK packs are not built yet, and
#     iOS/Android native modules go through the export pipeline.
#   - the scripting backend follows the package; pass -DORKIGE_SCRIPTING=OFF
#     only to override a package too old to record it.
#   - this file owns the vcpkg dependency-closure resolution the package
#     declares; the package declares WHICH packages and WHERE, never how they
#     link.

# --- where the engine comes from --------------------------------------------
# TWO forms of the SAME find_package(Orkige) package (cmake/OrkigeConfig.cmake.in):
#
#   BUILD TREE  the engine source root plus the tree it was built in. Pass
#               -DORKIGE_ROOT=<engine source> -DORKIGE_ENGINE_BUILD_DIR=<build>.
#               The developer loop, and what the editor's compile-on-Play and
#               the exporter use when they run out of an engine checkout.
#   SDK PACK    a self-contained, relocatable installed pack (headers,
#               archives, the dependency closure and the cmake surface under
#               ONE root - cmake/OrkigeSdk.cmake). A module includes
#               <pack>/cmake/OrkigeGameModule.cmake and passes NOTHING: the
#               pack answers every question about itself. This is how compiled
#               game code builds against a distributed engine, where no source
#               tree and no build tree exist.
#
# A pack carries OrkigeSdkPack.cmake beside this file. Its presence IS the
# marker, and it is the only file that knows the pack layout - everything below
# is the same code for both forms.
set(ORKIGE_SDK_MODE OFF)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/OrkigeSdkPack.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/OrkigeSdkPack.cmake")
    set(ORKIGE_SDK_MODE ON)
    set(ORKIGE_PACKAGE_DIR "${ORKIGE_SDK_PACKAGE_DIR}")
elseif(NOT DEFINED ORKIGE_ROOT)
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
if(NOT ORKIGE_SDK_MODE)
    if(NOT DEFINED ORKIGE_ENGINE_BUILD_DIR)
        message(FATAL_ERROR "ORKIGE_ENGINE_BUILD_DIR is not set - pass the engine "
            "build tree to link against: -DORKIGE_ENGINE_BUILD_DIR="
            "${ORKIGE_ROOT}/build/macos-debug")
    endif()
    set(ORKIGE_PACKAGE_DIR "${ORKIGE_ENGINE_BUILD_DIR}")
endif()

# --- engine package + ABI-stamp guard --------------------------------------
# Resolve the engine as ONE find_package(Orkige) package (two imported targets,
# Orkige::Core + Orkige::Engine, sharing ONE ABI stamp) instead of
# hand-globbing liborkige_engine.a - and REQUIRE the package's stamp match the
# stamp of the engine ABI surface this module compiles against. A
# stale/mismatched engine archive (headers grew a struct member but the library
# was not rebuilt) is then a HARD CONFIGURE ERROR here, never a runtime crash
# there (the JumperNative setWindowBackgroundColour null-deref class). The stamp
# is derived from the engine sources at ORKIGE_ROOT (cmake/OrkigeAbiStamp.cmake:
# committed HEAD + the tracked working diff), so it moves the instant a header
# changes; the engine records the stamp of the sources ITS archives were built
# from. Both the editor's compile-on-Play and the exporter flow through
# this same path, so a stale engine tree refuses at configure rather than
# shipping a crashing app.
#
# In an SDK PACK the guard keeps exactly this shape and exactly these teeth. A
# pack has no .cpp files, but it carries the surface that decides object layout
# for a consumer - the installed headers plus the cmake files defining the
# compile and link - and OrkigeSdkPack.cmake names that surface for both sides.
# The pack recorded its stamp over it at install time; the recomputation below
# reads the pack as it stands NOW. An edited (or truncated, or half-unpacked)
# installed header is therefore a hard configure error here, never a skewed
# object layout there.
if(DEFINED ORKIGE_EXPECTED_ABI_VERSION)
    # an explicit override of the expected ABI version; when unset it is
    # computed from the engine's ABI surface
    set(_orkige_expected "${ORKIGE_EXPECTED_ABI_VERSION}")
elseif(ORKIGE_SDK_MODE)
    include("${CMAKE_CURRENT_LIST_DIR}/OrkigeAbiStamp.cmake")
    set(ORKIGE_ABI_SOURCE_DIRS ${ORKIGE_SDK_ABI_SOURCE_DIRS})
    set(ORKIGE_ABI_EXTRA_FILES ${ORKIGE_SDK_ABI_EXTRA_FILES})
    orkige_compute_abi_stamp("${ORKIGE_SDK_ROOT}"
        _orkige_expected _orkige_expected_stamp)
else()
    include("${ORKIGE_ROOT}/cmake/OrkigeAbiStamp.cmake")
    orkige_compute_abi_stamp("${ORKIGE_ROOT}"
        _orkige_expected _orkige_expected_stamp)
endif()
find_package(Orkige "${_orkige_expected}" EXACT QUIET CONFIG
    PATHS "${ORKIGE_PACKAGE_DIR}" NO_DEFAULT_PATH)
if(NOT Orkige_FOUND)
    # discover what the package DOES carry so the message names both versions
    find_package(Orkige QUIET CONFIG
        PATHS "${ORKIGE_PACKAGE_DIR}" NO_DEFAULT_PATH)
    if(NOT Orkige_FOUND)
        if(ORKIGE_SDK_MODE)
            message(FATAL_ERROR "no Orkige engine package under "
                "'${ORKIGE_PACKAGE_DIR}' - this SDK pack is incomplete "
                "(OrkigeConfig.cmake or an engine archive is missing). "
                "Re-install or re-download the pack.")
        endif()
        message(FATAL_ERROR "no Orkige engine package under "
            "'${ORKIGE_PACKAGE_DIR}' - build the engine tree first "
            "(cmake --build --preset <preset>), which emits OrkigeConfig.cmake, "
            "or fix ORKIGE_ENGINE_BUILD_DIR.")
    endif()
    if(ORKIGE_SDK_MODE)
        message(FATAL_ERROR "Orkige engine ABI mismatch: the SDK pack at "
            "'${ORKIGE_SDK_ROOT}' records version ${Orkige_VERSION} (ABI stamp "
            "${ORKIGE_ABI_STAMP}), but its installed surface now fingerprints "
            "as ${_orkige_expected}. The pack's headers no longer match the "
            "archives shipped with them - restore or re-download the pack "
            "rather than building a skewed object layout against it.")
    endif()
    message(FATAL_ERROR "Orkige engine ABI mismatch: the package at "
        "'${ORKIGE_PACKAGE_DIR}' is version ${Orkige_VERSION} (ABI stamp "
        "${ORKIGE_ABI_STAMP}), but this module's engine headers expect version "
        "${_orkige_expected}. The engine library is stale relative to the "
        "sources at '${ORKIGE_ROOT}' - rebuild the engine tree (cmake --build "
        "...) so its archives match the current headers, then reconfigure this "
        "module.")
endif()

# --- configuration match ----------------------------------------------------
# The package carries ONE configuration - its engine archives and its
# dependency closure were built in it together (cmake/OrkigeSdk.cmake ships
# exactly the matching half). A module compiled in a different one disagrees
# with both about what its shared headers mean: a dependency's per-config
# interface definitions change inline behaviour and struct layout (Jolt turns
# its asserts on where NDEBUG is absent, growing the code that calls into it),
# and on MSVC the C runtime itself differs - the tree pins
# x64-windows-static-md, so /MD and /MDd objects cannot live in one image at
# all. The failures range from an undefined symbol at link to a binary that
# links and then corrupts its heap, so the mismatch is refused HERE, by name.
#
# Only an SDK pack is checked: a build tree keeps both halves of its closure,
# so a module may legitimately be built in either configuration against it -
# which is exactly what the editor's compile-on-Play does.
if(ORKIGE_SDK_MODE AND ORKIGE_PACKAGE_BUILD_TYPE
        AND NOT CMAKE_BUILD_TYPE STREQUAL ORKIGE_PACKAGE_BUILD_TYPE)
    message(FATAL_ERROR "Orkige SDK pack configuration mismatch: the pack at "
        "'${ORKIGE_SDK_ROOT}' carries ${ORKIGE_PACKAGE_BUILD_TYPE} binaries "
        "(engine archives AND dependency closure), but this module is being "
        "configured as '${CMAKE_BUILD_TYPE}'. Mixing them produces undefined "
        "symbols at link or a miscompiled binary - configure the module with "
        "-DCMAKE_BUILD_TYPE=${ORKIGE_PACKAGE_BUILD_TYPE}, or use a pack built "
        "from an engine tree of the configuration you want.")
endif()

# The render flavor the engine was built with. The module links that flavor's
# OGRE closure and defines its ABI macro so the flavor-gated engine headers
# compile the matching branch. THE PACKAGE is the answer - it records what the
# archives were actually built with, which is the one fact that matters and the
# only one an SDK pack (which has no CMakeCache) can carry. The build tree's
# cache is the fallback for a package too old to say.
#
# Sanitizer instrumentation rides along like the flavor: an instrumented
# engine's static libs reference runtime symbols (__asan_*/__ubsan_*) that only
# the same -fsanitize= compile AND link provide - a module built plain against
# such an engine dies at link with thousands of undefined sanitizer symbols. The
# root CMakeLists carries the option as ORKIGE_ENABLE_SANITIZERS (the flags come
# from there, not the flags cache), the package records it, and
# orkige_game_module() mirrors the root's exact option set onto the module
# target - target options survive project() ordering, directory-scope flag edits
# do not.
set(ORKIGE_MODULE_FLAVOR "classic")
if(DEFINED ORKIGE_PACKAGE_RENDER_BACKEND)
    if(ORKIGE_PACKAGE_RENDER_BACKEND STREQUAL "next")
        set(ORKIGE_MODULE_FLAVOR "next")
    endif()
    if(ORKIGE_PACKAGE_SANITIZERS)
        set(ORKIGE_MODULE_SANITIZERS ON)
    endif()
elseif(EXISTS "${ORKIGE_PACKAGE_DIR}/CMakeCache.txt")
    file(STRINGS "${ORKIGE_PACKAGE_DIR}/CMakeCache.txt"
        _orkige_module_backend_line REGEX "^ORKIGE_RENDER_BACKEND:")
    if(_orkige_module_backend_line MATCHES "=next$")
        set(ORKIGE_MODULE_FLAVOR "next")
    endif()
    file(STRINGS "${ORKIGE_PACKAGE_DIR}/CMakeCache.txt"
        _orkige_module_sanitize_line REGEX "^ORKIGE_ENABLE_SANITIZERS:")
    if(_orkige_module_sanitize_line MATCHES "=ON$")
        set(ORKIGE_MODULE_SANITIZERS ON)
    endif()
endif()
if(ORKIGE_MODULE_SANITIZERS)
    message(STATUS "the engine is sanitizer-instrumented - the module target "
        "inherits ASan + UBSan")
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

# the scripting backend the engine was built with; the package says so, and the
# cache entry stays as the manual override for a package too old to answer
set(_orkige_scripting_default "LUA")
if(DEFINED ORKIGE_PACKAGE_SCRIPTING AND ORKIGE_PACKAGE_SCRIPTING)
    set(_orkige_scripting_default "${ORKIGE_PACKAGE_SCRIPTING}")
endif()
set(ORKIGE_SCRIPTING "${_orkige_scripting_default}" CACHE STRING
    "Scripting backend the engine build was configured with (LUA or OFF)")

# The engine archives are the Orkige::Core + Orkige::Engine imported targets the
# find_package(Orkige) above resolved (their existence is checked by the package
# config); the game module links Orkige::Engine, which pulls Orkige::Core.

# The engine's dependencies come from the prefix the PACKAGE points at - the
# build's own vcpkg_installed/<triplet> tree, or the pack's bundled vcpkg/ - so
# the plain find_packages below resolve the exact binaries the engine libraries
# were compiled and linked against, without requiring the vcpkg toolchain here
# and without a second opinion about versions.
set(ORKIGE_VCPKG_PREFIX "")
if(ORKIGE_VCPKG_PREFIX_HINT AND IS_DIRECTORY "${ORKIGE_VCPKG_PREFIX_HINT}/include")
    set(ORKIGE_VCPKG_PREFIX "${ORKIGE_VCPKG_PREFIX_HINT}")
else()
    # a build tree that has been moved since it was configured: re-derive the
    # triplet prefix from the build dir it was actually found in
    file(GLOB _orkige_vcpkg_triplets
        LIST_DIRECTORIES true "${ORKIGE_PACKAGE_DIR}/vcpkg_installed/*")
    foreach(_orkige_triplet_dir IN LISTS _orkige_vcpkg_triplets)
        # skip vcpkg's own bookkeeping dir; a real triplet dir has include/
        if(IS_DIRECTORY "${_orkige_triplet_dir}/include")
            set(ORKIGE_VCPKG_PREFIX "${_orkige_triplet_dir}")
        endif()
    endforeach()
endif()
if(NOT ORKIGE_VCPKG_PREFIX)
    message(FATAL_ERROR "no dependency closure beside the Orkige package at "
        "'${ORKIGE_PACKAGE_DIR}' - a build tree carries it as "
        "vcpkg_installed/<triplet>, an SDK pack as vcpkg/.")
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
# zlib is the one MODULE-mode dependency in the closure (OGRE-Next's config
# requires it), and vcpkg's zlib find wrapper steers FindZLIB toward vcpkg
# directories the TOOLCHAIN would provide - without the toolchain those are
# empty and the guided search finds nothing, whatever the search paths say
# (the Windows job's evidence: header found, library not). Pre-seeding the
# result cache variables short-circuits the search entirely; find_library
# no-ops on an already-set entry.
# vcpkg names the static Windows zlib "zs.lib"/"zsd.lib" (the CI diag
# artifact's listing settled it) - no generic zlib*/libz* pattern matches,
# which is the whole reason the toolchain-guided wrapper exists upstream
file(GLOB _orkige_zlib_release
    "${ORKIGE_VCPKG_PREFIX}/lib/zlib*" "${ORKIGE_VCPKG_PREFIX}/lib/libz.*"
    "${ORKIGE_VCPKG_PREFIX}/lib/zs.lib")
file(GLOB _orkige_zlib_debug
    "${ORKIGE_VCPKG_PREFIX}/debug/lib/zlib*"
    "${ORKIGE_VCPKG_PREFIX}/debug/lib/libz.*"
    "${ORKIGE_VCPKG_PREFIX}/debug/lib/zsd.lib")
message(STATUS "vcpkg zlib seed: release='${_orkige_zlib_release}' "
    "debug='${_orkige_zlib_debug}'")
if(NOT _orkige_zlib_release)
    # the seed found nothing - name the reality so the failure diagnoses
    # itself: which prefix was picked and what its lib dir actually holds
    file(GLOB _orkige_lib_listing "${ORKIGE_VCPKG_PREFIX}/lib/*")
    message(STATUS "vcpkg zlib seed found no library - prefix "
        "'${ORKIGE_VCPKG_PREFIX}', lib dir holds: ${_orkige_lib_listing}")
endif()
# the diagnosis must survive log-tail truncation: a CI job's error tail only
# carries the last few KB of build output, which the find-failure chain fills
# - persist the picked prefix + both lib listings to a file the job artifact
# collects
file(GLOB _orkige_debug_lib_listing "${ORKIGE_VCPKG_PREFIX}/debug/lib/*")
file(WRITE "${CMAKE_BINARY_DIR}/orkige_module_diag.txt"
    "package: ${ORKIGE_PACKAGE_DIR} (sdk mode: ${ORKIGE_SDK_MODE})\n"
    "prefix: ${ORKIGE_VCPKG_PREFIX}\n"
    "zlib release glob: ${_orkige_zlib_release}\n"
    "zlib debug glob: ${_orkige_zlib_debug}\n"
    "lib dir: ${_orkige_lib_listing}\n"
    "debug/lib dir: ${_orkige_debug_lib_listing}\n")
if(_orkige_zlib_release AND NOT DEFINED CACHE{ZLIB_LIBRARY})
    list(GET _orkige_zlib_release 0 _orkige_zlib_release_lib)
    set(ZLIB_LIBRARY_RELEASE "${_orkige_zlib_release_lib}" CACHE FILEPATH
        "zlib release library from the engine tree's vcpkg triplet")
    # seed the COMBINED result variable FindZLIB's found-check requires (the
    # config-specific seeds alone did not satisfy it on the Windows job) plus
    # the include dir, so the module never searches at all
    if(_orkige_zlib_debug)
        list(GET _orkige_zlib_debug 0 _orkige_zlib_debug_lib)
        set(ZLIB_LIBRARY_DEBUG "${_orkige_zlib_debug_lib}" CACHE FILEPATH
            "zlib debug library from the engine tree's vcpkg triplet")
        set(ZLIB_LIBRARY
            "optimized;${_orkige_zlib_release_lib};debug;${_orkige_zlib_debug_lib}"
            CACHE STRING "zlib per-config libraries (vcpkg triplet)")
    else()
        set(ZLIB_LIBRARY "${_orkige_zlib_release_lib}" CACHE FILEPATH
            "zlib library (vcpkg triplet)")
    endif()
    if(EXISTS "${ORKIGE_VCPKG_PREFIX}/include/zlib.h" AND
        NOT DEFINED CACHE{ZLIB_INCLUDE_DIR})
        set(ZLIB_INCLUDE_DIR "${ORKIGE_VCPKG_PREFIX}/include" CACHE PATH
            "zlib headers (vcpkg triplet)")
    endif()
endif()

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
# the gui runtime atlas rasterises SVG UI sprites (and the asset-import cook
# flattens drawings) through nanosvg's precompiled static libs; the engine
# archive references their symbols
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
# The engine's HTTP transport, which the module links because the core archive
# is linked by PATH (@see the link block below) and a raw path carries none of
# its own dependencies. WHICH transport is the package's answer, never a second
# platform decision here: the layer that chose it says so.
if(ORKIGE_PACKAGE_HTTP_BACKEND STREQUAL "curl")
    find_package(CURL CONFIG REQUIRED)
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
    if(ORKIGE_MODULE_SANITIZERS)
        # mirror the engine root CMakeLists' sanitizer option set exactly
        # (vptr excluded there for RTTI-less static dependencies - same
        # constraint applies to the module's link of those libs)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-sanitize=vptr>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-sanitize-recover=undefined>"
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-sanitize=vptr)
    endif()
    # the engine include roots the PACKAGE declares - the source tree's two
    # layer dirs from a build tree, the pack's merged include/ from a pack.
    # Spelled explicitly because the archives are linked by raw path below (see
    # that block), which carries no interface include directories with it.
    target_include_directories(${target} PRIVATE ${ORKIGE_INCLUDE_DIRS})
    # THE COMPILE CONTRACT, straight from the package. These are the
    # ABI-relevant definitions the engine archives were ACTUALLY compiled with,
    # captured off the engine targets when the package was written
    # (cmake/OrkigePackage.cmake) - never a list restated here, which would
    # drift the moment the engine grew a define and disagree silently. It
    # covers ORKIGE_STATIC, the render flavor macro (engine_graphic/Engine.h
    # and engine_render/RenderMath.h gate on it, so the module MUST compile the
    # same branch the archives were built with), USE_RTSHADER_SYSTEM,
    # ORKIGE_OPENAL_SOUND, ORKIGE_ENGINE_HAS_GOCOMPONENT, the scripting-backend
    # define, ORKIGE_HAVE_VULKAN and the platform lean-header switches.
    #
    # The dependencies' own contract (Jolt's, OGRE's, SDL's) is NOT here: it
    # rides their imported targets, linked below. That only holds while the
    # dependency binaries are the SAME CONFIGURATION as the engine archives,
    # which is why the package records its build type and this file checks it.
    target_compile_definitions(${target} PRIVATE
        ${ORKIGE_ENGINE_COMPILE_DEFINITIONS}
        # where this module's engine media lives - a property of the package
        # the module was built against, not of the engine build
        ORKIGE_MODULE_MEDIA_DIR="${ORKIGE_GAME_MODULE_MEDIA_DIR}"
    )
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        target_include_directories(${target} PRIVATE ${LUA_INCLUDE_DIR})
        # sol2 is header-only; the Lua archive itself is linked AFTER the
        # engine archives below - GNU ld resolves archives left to right, so
        # a provider listed before its consumers drops the symbols the
        # engine archives need (luaL_error etc.); ld64 does not care, which
        # is why the wrong order never surfaced on macOS
        target_link_libraries(${target} PRIVATE sol2::sol2)
    endif()
    # Link the Orkige archives as RAW .a PATHS (not the imported targets), then
    # their dependency closure. GNU ld resolves archives left-to-right in a
    # single pass, so a provider (tinyxml2, the OGRE archives + their transitive
    # deps) must follow its consumer (orkige_core / orkige_engine). Passing the
    # imported targets Orkige::Engine / Orkige::Core let CMake's interface
    # expansion reorder orkige_core relative to its providers, which dropped
    # orkige_core's tinyxml2 symbols on GNU ld while ld64 (macOS) tolerated the
    # flat list - the platform gap that broke every Linux CI job. This raw-path
    # order is the pre-package helper's proven layout, which linked on both
    # flavors and all platforms; the imported targets remain purely the ABI-guard
    # vehicle (find_package version check above). The dependency imported targets
    # carry their own transitive closure, so a plain flat list resolves them (a
    # RESCAN LINK GROUP was tried and REGRESSED classic: grouping the OGRE targets
    # displaced their transitive deps - OgreGLSupport etc. - out of the order the
    # flat link gets right).
    get_target_property(_orkige_engine_lib Orkige::Engine IMPORTED_LOCATION)
    get_target_property(_orkige_core_lib Orkige::Core IMPORTED_LOCATION)
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
    # the HTTP transport's dependency, after the archives that reference it
    # (GNU ld resolves left to right - the same rule the comment above states)
    if(ORKIGE_PACKAGE_HTTP_BACKEND STREQUAL "curl")
        target_link_libraries(${target} PRIVATE CURL::libcurl)
    elseif(ORKIGE_PACKAGE_HTTP_BACKEND STREQUAL "win")
        # winhttp.dll is part of Windows; this is the SDK's import library
        target_link_libraries(${target} PRIVATE winhttp)
    endif()
    if(ORKIGE_MODULE_FLAVOR STREQUAL "next")
        # the Ogre-Next backend closure (see orkige_engine/CMakeLists.txt); one
        # render system per platform - Metal on Apple, Vulkan elsewhere
        target_link_libraries(${target} PRIVATE
            OgreNext::Main
            OgreNext::HlmsPbs
            OgreNext::HlmsUnlit
            OgreNext::Atmosphere
            # mirror-of-scene planar water reflection (engine_render_next
            # references Ogre::PlanarReflections; @see orkige_engine/CMakeLists.txt)
            OgreNext::PlanarReflections
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
        # after the engine archives (a provider for orkige_core/sol2's luaL_*)
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
