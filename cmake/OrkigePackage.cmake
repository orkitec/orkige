# OrkigePackage.cmake - emit the Orkige engine as a build-tree find_package()
# package (OrkigeConfig.cmake + OrkigeConfigVersion.cmake) so native game
# modules resolve the engine + an EXACT ABI-stamp guard instead of hand-globbing
# liborkige_engine.a. Included from the root CMakeLists after the two engine
# archives (orkige_core, orkige_engine) are defined; call orkige_emit_package().
#
# The config resolves against the build tree with NO install step (fast dev
# loop + CI). The version file records the ABI stamp of the sources the archives
# were built from (cmake/OrkigeAbiStamp.cmake) and is refreshed as a POST_BUILD
# step of the archives, so a stale library is caught the moment its sources
# drift from a module's headers - the find_package(... EXACT) mismatch is a hard
# configure error (see cmake/OrkigeGameModule.cmake).

include_guard(GLOBAL)

# orkige_package_transitive_list(<out>)
#   The vcpkg dependency closure the two archives link (mirrors the find set in
#   cmake/OrkigeGameModule.cmake); DECLARED for consumers, realized there. Both
#   package forms - the build tree here and the SDK pack in cmake/OrkigeSdk.cmake
#   - declare the SAME closure, so it is written once.
function(orkige_package_transitive_list out)
    set(_packages "SDL3" "Jolt" "tinyxml2" "NanoSVG" "ZLIB")
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        list(APPEND _packages "OGRE-Next" "assimp")
    else()
        list(APPEND _packages "OGRE")
    endif()
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        list(APPEND _packages "Lua" "sol2")
    endif()
    # the HTTP transport's own dependency, where it has one a consumer must
    # resolve: curl is a vcpkg package, WinHTTP an SDK import library, and the
    # Apple/Android/wasm transports need nothing beyond what the layer already
    # links (the frameworks are PUBLIC, the JNI entry points come with the NDK)
    if(ORKIGE_HTTP_BACKEND STREQUAL "curl")
        list(APPEND _packages "CURL")
    endif()
    set(${out} "${_packages}" PARENT_SCOPE)
endfunction()

# orkige_package_compile_definitions(<out_core> <out_engine>)
#   THE COMPILE CONTRACT, read off the engine itself rather than restated.
#
#   A consumer's translation units must be compiled with the same ABI-relevant
#   definitions the engine archives were, or its objects disagree with the
#   archive about layout and inline behaviour - the failure is a link error at
#   best and a miscompiled binary at worst. Restating that set in a package
#   config and again in a link helper is a drift trap: the engine grows a
#   define, the two copies do not, and nothing says so.
#
#   So it is CAPTURED, from the two places CMake actually records it:
#     - the root directory's COMPILE_DEFINITIONS, which is where the engine's
#       global ABI switches live (ORKIGE_STATIC, the scripting backend define,
#       and on Windows NOMINMAX/WIN32_LEAN_AND_MEAN)
#     - each archive's INTERFACE_COMPILE_DEFINITIONS, i.e. exactly what it
#       declares PUBLIC (ORKIGE_ENGINE_HAS_GOCOMPONENT, the render flavor
#       macro, USE_RTSHADER_SYSTEM, ORKIGE_HAVE_VULKAN, ...)
#   The engine's own build-tree-absolute paths are declared PRIVATE and are
#   therefore correctly absent - they are implementation, not contract, and a
#   pack must not carry them.
#
#   Third-party contract (Jolt's JPH_OBJECT_STREAM and its per-configuration
#   NDEBUG, OGRE's, SDL's) is NOT copied here: it rides the imported targets a
#   consumer links, which carry it themselves. What that requires instead is
#   that the pack's dependency closure be the SAME CONFIGURATION as the engine
#   archives - see cmake/OrkigeSdk.cmake.
function(orkige_package_compile_definitions out_core out_engine)
    get_directory_property(_global COMPILE_DEFINITIONS)
    set(_core "${_global}")
    set(_engine "${_global}")
    get_target_property(_core_public orkige_core INTERFACE_COMPILE_DEFINITIONS)
    if(_core_public)
        list(APPEND _core ${_core_public})
        list(APPEND _engine ${_core_public})
    endif()
    if(TARGET orkige_engine)
        get_target_property(_engine_public orkige_engine INTERFACE_COMPILE_DEFINITIONS)
        if(_engine_public)
            list(APPEND _engine ${_engine_public})
        endif()
    endif()
    list(REMOVE_DUPLICATES _core)
    list(REMOVE_DUPLICATES _engine)
    set(${out_core} "${_core}" PARENT_SCOPE)
    # Orkige::Engine pulls Orkige::Core, so the engine contract is the union -
    # a consumer that links only the engine still compiles core headers
    set(${out_engine} "${_engine}" PARENT_SCOPE)
endfunction()

# orkige_package_private_definitions(<out>)
#   The definitions the two archives declare PRIVATE - implementation, never
#   contract, and therefore deliberately absent from the captured set above.
#
#   Recorded anyway, because capture is only as complete as the engine's
#   discipline of declaring an ABI-relevant define PUBLIC: a define added
#   PRIVATE that some HEADER then reads would change what that header means for
#   a consumer while escaping the contract silently. Recording the private set
#   turns that into something checkable - the sdk_pack test asserts no installed
#   header mentions any of these names, so the day one of them leaks into a
#   header the suite says so and the fix is to declare it PUBLIC.
function(orkige_package_private_definitions out)
    # a PUBLIC definition lands in BOTH properties, so the private set is the
    # difference: what a target compiles itself with, minus what it hands on
    set(_private "")
    foreach(_target orkige_core orkige_engine)
        if(TARGET ${_target})
            get_target_property(_all ${_target} COMPILE_DEFINITIONS)
            get_target_property(_public ${_target} INTERFACE_COMPILE_DEFINITIONS)
            if(_all)
                if(_public)
                    list(REMOVE_ITEM _all ${_public})
                endif()
                list(APPEND _private ${_all})
            endif()
        endif()
    endforeach()
    # only the NAMES matter for the audit; a value is implementation detail
    # (the two media-dir path macros carry build-tree paths as their value)
    set(_names "")
    foreach(_entry IN LISTS _private)
        string(REGEX REPLACE "=.*$" "" _name "${_entry}")
        if(_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            list(APPEND _names "${_name}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _names)
    set(${out} "${_names}" PARENT_SCOPE)
endfunction()

# orkige_package_compile_options(<out_core> <out_engine>)
# orkige_package_link_options(<out>)
#   THE SAME CAPTURE, for compile and link OPTIONS.
#
#   Definitions are not the whole contract. An option can be as ABI-relevant as
#   a define - the exception model a wasm build compiles with decides how every
#   translation unit unwinds, so a consumer's own TUs must carry it or the
#   objects cannot be linked into one working image - and a capacity flag like
#   MSVC's /bigobj is needed by the consumer for the same reason the engine
#   needs it: it compiles the same fat, heavily-templated headers.
#
#   So options ride the identical channel: the root directory's COMPILE_OPTIONS
#   and LINK_OPTIONS (where the engine's global toolchain switches live - the
#   sanitizer instrumentation, /bigobj) plus each archive's PUBLIC ones. Nothing
#   is restated in the game-module helper, which is what a hand-mirrored copy of
#   the sanitizer flag set used to be.
function(orkige_package_compile_options out_core out_engine)
    get_directory_property(_global COMPILE_OPTIONS)
    set(_core "${_global}")
    set(_engine "${_global}")
    get_target_property(_core_public orkige_core INTERFACE_COMPILE_OPTIONS)
    if(_core_public)
        list(APPEND _core ${_core_public})
        list(APPEND _engine ${_core_public})
    endif()
    if(TARGET orkige_engine)
        get_target_property(_engine_public orkige_engine INTERFACE_COMPILE_OPTIONS)
        if(_engine_public)
            list(APPEND _engine ${_engine_public})
        endif()
    endif()
    list(REMOVE_DUPLICATES _core)
    list(REMOVE_DUPLICATES _engine)
    set(${out_core} "${_core}" PARENT_SCOPE)
    set(${out_engine} "${_engine}" PARENT_SCOPE)
endfunction()

function(orkige_package_link_options out)
    get_directory_property(_options LINK_OPTIONS)
    foreach(_target orkige_core orkige_engine)
        if(TARGET ${_target})
            get_target_property(_public ${_target} INTERFACE_LINK_OPTIONS)
            if(_public)
                list(APPEND _options ${_public})
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _options)
    set(${out} "${_options}" PARENT_SCOPE)
endfunction()

function(orkige_emit_package)
    set(_out "${CMAKE_BINARY_DIR}")
    set(_module_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")

    # the engine archives, at the predictable per-target output paths the module
    # helper and exporter already trust
    set(ORKIGE_PACKAGE_CORE_LIB
        "${_out}/orkige_core/${CMAKE_STATIC_LIBRARY_PREFIX}orkige_core${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(ORKIGE_PACKAGE_ENGINE_LIB
        "${_out}/orkige_engine/${CMAKE_STATIC_LIBRARY_PREFIX}orkige_engine${CMAKE_STATIC_LIBRARY_SUFFIX}")

    set(ORKIGE_PACKAGE_SOURCE_ROOT "${Orkige_SOURCE_DIR}")

    # the build-tree form: the config sits at the build root and spells every
    # path absolutely (a build tree is bound to its location anyway); the SDK
    # pack form in cmake/OrkigeSdk.cmake spells the same slots relative to the
    # pack root, which is what makes a pack relocatable
    set(ORKIGE_PACKAGE_KIND "buildtree")
    set(ORKIGE_PACKAGE_ROOT_REL ".")
    set(ORKIGE_PACKAGE_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    set(ORKIGE_PACKAGE_CORE_INCLUDES "${Orkige_SOURCE_DIR}/orkige_core")
    set(ORKIGE_PACKAGE_ENGINE_INCLUDES "${Orkige_SOURCE_DIR}/orkige_engine")

    # the vcpkg triplet prefix in this build tree (the dir with include/) - a
    # hint the module helper uses to resolve the closure below without the vcpkg
    # toolchain
    set(ORKIGE_PACKAGE_VCPKG_PREFIX "")
    file(GLOB _triplets LIST_DIRECTORIES true
        "${_out}/vcpkg_installed/*")
    foreach(_triplet IN LISTS _triplets)
        if(IS_DIRECTORY "${_triplet}/include")
            set(ORKIGE_PACKAGE_VCPKG_PREFIX "${_triplet}")
        endif()
    endforeach()
    # the engine's own runtime media (fonts, the shared water plane); a build
    # tree reads it from the source layer, a pack from its bundled copy
    set(ORKIGE_PACKAGE_MEDIA_DIR "${Orkige_SOURCE_DIR}/orkige_engine/media")

    orkige_package_transitive_list(ORKIGE_PACKAGE_TRANSITIVE)
    orkige_package_compile_definitions(
        ORKIGE_PACKAGE_CORE_DEFS ORKIGE_PACKAGE_ENGINE_DEFS)
    orkige_package_private_definitions(ORKIGE_PACKAGE_PRIVATE_DEFS)
    orkige_package_compile_options(
        ORKIGE_PACKAGE_CORE_OPTIONS ORKIGE_PACKAGE_ENGINE_OPTIONS)
    orkige_package_link_options(ORKIGE_PACKAGE_LINK_OPTIONS)

    configure_file("${_module_dir}/OrkigeConfig.cmake.in"
        "${_out}/OrkigeConfig.cmake" @ONLY)

    # Write the version file at configure time (so a module configuring right
    # after the engine builds finds it). This alone is not enough: editing a
    # header and rebuilding does NOT re-run configure, which would leave the
    # stamp behind the freshly-built archives and reject a VALID module build.
    # So an always-out-of-date target re-derives the stamp on every engine build
    # (it runs after the archives, part of ALL), keeping the recorded stamp in
    # lock-step with the sources the libraries were last built from. The stamp
    # is source-derived, so ordering versus the archive link is immaterial.
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DORKIGE_ROOT=${Orkige_SOURCE_DIR}"
        "-DORKIGE_ABI_OUT_DIR=${_out}"
        "-DORKIGE_ABI_TAG=configure"
        -P "${_module_dir}/OrkigeWriteVersion.cmake")
    add_custom_target(orkige_abi_stamp ALL
        COMMAND "${CMAKE_COMMAND}"
            "-DORKIGE_ROOT=${Orkige_SOURCE_DIR}"
            "-DORKIGE_ABI_OUT_DIR=${_out}"
            "-DORKIGE_ABI_TAG=build"
            -P "${_module_dir}/OrkigeWriteVersion.cmake"
        COMMENT "Refreshing Orkige package ABI stamp"
        VERBATIM)
    add_dependencies(orkige_abi_stamp orkige_core orkige_engine)
endfunction()
