# OrkigeSdk.cmake - install the engine as a RELOCATABLE SDK pack.
#
# The pack is what a downloaded (binary) editor hands a project whose game code
# is compiled C++: everything a native game module needs to configure, build and
# link, under ONE directory that works wherever it is unpacked. A build tree
# cannot serve that role - it spells absolute paths and its dependency closure
# lives inside it (see cmake/OrkigePackage.cmake, the build-tree form of the
# same package).
#
# Layout (the ONE thing a consumer must know is the pack root):
#
#   <pack>/include/          the engine headers, LAYER-ROOTED exactly as the
#                            tree spells them: core_util/String.h,
#                            engine_graphic/Engine.h. Both layers merge into one
#                            root because their module directories are disjoint
#                            (core_* vs engine_*), so one include path serves
#                            both and consumer include lines are unchanged.
#   <pack>/lib/              liborkige_core + liborkige_engine
#   <pack>/media/            the engine's runtime media (fonts, water plane)
#   <pack>/cmake/            OrkigeConfig + OrkigeConfigVersion (the ABI stamp),
#                            OrkigeAbiStamp.cmake, OrkigeWriteVersion.cmake,
#                            OrkigeGameModule.cmake and OrkigeSdkPack.cmake (the
#                            marker that puts the game-module helper into pack
#                            mode, and the one description of the layout)
#   <pack>/vcpkg/            the dependency closure - the build's vcpkg triplet
#                            prefix, RELEASE half only
#
# THE ABI STAMP survives into the pack with its teeth intact. A build tree
# compares the stamp of the engine sources on disk against the stamp the
# archives were built from; a pack has no .cpp files, but it has the ABI surface
# that actually matters - the installed HEADERS plus the cmake files that define
# how a module compiles and links. So the pack computes its recorded stamp over
# its own installed surface at install time (the install(CODE) tail below) with
# the SAME orkige_compute_abi_stamp function, and the game-module helper
# recomputes it over the pack it is handed. Edit an installed header and the
# EXACT find_package match fails at configure - never a skewed object layout at
# run time.
#
# RELEASE ONLY. The pack ships one configuration: a distributed engine is a
# release engine, the debug half of a vcpkg triplet is an order of magnitude
# larger than the release half, and a mixed pack would invite linking a Release
# module against Debug dependencies. On MSVC that is not a size preference but a
# correctness one - the tree pins x64-windows-static-md, so the CRT is the
# DLL runtime and /MD (release) and /MDd (debug) objects cannot be mixed in one
# image. Consumers therefore build Release against a pack; the module helper's
# Debug-first library search finds nothing debug in a pack and falls through to
# the release lib dir, which is the only correct answer there.
#
# Usage (from the root CMakeLists, after orkige_emit_package()):
#     orkige_install_sdk()
# then
#     cmake --install <build> --prefix <pack>

include_guard(GLOBAL)

# The dependency-closure sub-directories worth shipping, split the way vcpkg
# lays a triplet out. The config-NEUTRAL ones carry headers, the port cmake
# configs and the runtime pieces some ports place beside them; the per-config
# ones carry the actual binaries, release at the triplet root and debug under
# debug/. vcpkg's own bookkeeping tree (vcpkg_installed/vcpkg/) never enters
# the pack.
set(ORKIGE_SDK_VCPKG_SHARED_SUBDIRS include share plugins tools etc bin loader)
set(ORKIGE_SDK_VCPKG_RELEASE_SUBDIRS lib)
set(ORKIGE_SDK_VCPKG_DEBUG_SUBDIRS debug)

# orkige_install_sdk()
#   Adds the install() rules that produce the pack. Called once from the root
#   CMakeLists for desktop host builds (native modules are desktop-only).
function(orkige_install_sdk)
    set(_module_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(_src "${Orkige_SOURCE_DIR}")

    # --- headers ------------------------------------------------------------
    # The COMPLETE header set of both layers, layer-rooted. "Public" is not a
    # distinction this tree draws: includes are layer-rooted with no public/
    # private split, the umbrella/Meta/template headers cross-reference each
    # other freely, and a carve today would be a guess that fails at a
    # consumer's first unusual include. The whole set is a few megabytes against
    # a pack dominated by the dependency closure, so the honest v1 answer is to
    # ship it all and let the surface probe (the sdk_pack test compiles a
    # translation unit that includes every one of them against the INSTALLED
    # pack) keep the install set complete. Backend-internal headers are
    # reachable in the pack; reachable is not sanctioned - game code above the
    # engine_render facade spells only facade types, exactly as in-tree code
    # does.
    foreach(_layer orkige_core orkige_engine)
        install(DIRECTORY "${_src}/${_layer}/"
            DESTINATION include
            COMPONENT sdk
            FILES_MATCHING
                PATTERN "*.h"
                PATTERN "*.inc"
                PATTERN "media" EXCLUDE)
    endforeach()

    # --- the two archives ---------------------------------------------------
    # by TARGET_FILE, so the generator answers where it actually put them
    install(FILES
        "$<TARGET_FILE:orkige_core>"
        "$<TARGET_FILE:orkige_engine>"
        DESTINATION lib
        COMPONENT sdk)

    # --- engine runtime media ------------------------------------------------
    # the fonts and the shared water plane the engine registers at boot; a
    # module that runs from the pack resolves them here
    if(IS_DIRECTORY "${_src}/orkige_engine/media")
        install(DIRECTORY "${_src}/orkige_engine/media/"
            DESTINATION media
            COMPONENT sdk)
    endif()

    # --- the dependency closure ---------------------------------------------
    # The build's own vcpkg triplet prefix. Copied rather than re-resolved:
    # these are the exact binaries the engine archives were compiled and linked
    # against, so versions can never diverge.
    #
    # ONE CONFIGURATION, and it is the one the ENGINE ARCHIVES were built in.
    # This is not a size preference, it is correctness: a dependency's headers
    # compile differently per configuration (Jolt turns its asserts on when
    # NDEBUG is absent, and its interface definitions say so per config), so a
    # Debug engine archive contains calls to symbols only the debug build of
    # that dependency defines. Ship the other half and the mismatch surfaces as
    # an undefined symbol at a consumer's link - or, for a difference that is
    # layout rather than linkage, as a binary that links and then misbehaves.
    # Mixing halves is therefore never offered.
    #
    # The per-config imported-target files vcpkg ports install come in release
    # and debug pairs, and a port's main *Targets.cmake globs its siblings and
    # includes them all. The pair for the half this pack does NOT carry would
    # name archives that are not there and turn every consumer configure into a
    # hard IMPORTED_LOCATION check failure, so they are pruned with the binaries
    # they describe.
    set(_debug_pack OFF)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_debug_pack ON)
    endif()
    set(_triplet "")
    file(GLOB _triplets LIST_DIRECTORIES true "${CMAKE_BINARY_DIR}/vcpkg_installed/*")
    foreach(_candidate IN LISTS _triplets)
        if(IS_DIRECTORY "${_candidate}/include")
            set(_triplet "${_candidate}")
        endif()
    endforeach()
    if(NOT _triplet)
        message(FATAL_ERROR "orkige_install_sdk: no vcpkg_installed/<triplet> "
            "under '${CMAKE_BINARY_DIR}' - the SDK pack carries the dependency "
            "closure the engine was built against and cannot be assembled "
            "without it.")
    endif()
    set(_closure_subdirs ${ORKIGE_SDK_VCPKG_SHARED_SUBDIRS})
    if(_debug_pack)
        list(APPEND _closure_subdirs ${ORKIGE_SDK_VCPKG_DEBUG_SUBDIRS})
        set(_pruned_config "release")
    else()
        list(APPEND _closure_subdirs ${ORKIGE_SDK_VCPKG_RELEASE_SUBDIRS})
        set(_pruned_config "debug")
    endif()
    foreach(_sub IN LISTS _closure_subdirs)
        if(IS_DIRECTORY "${_triplet}/${_sub}")
            install(DIRECTORY "${_triplet}/${_sub}"
                DESTINATION vcpkg
                COMPONENT sdk)
        endif()
    endforeach()
    install(CODE "
        file(GLOB_RECURSE _orkige_other_cfg
            \"\${CMAKE_INSTALL_PREFIX}/vcpkg/share/*-${_pruned_config}.cmake\")
        if(_orkige_other_cfg)
            file(REMOVE \${_orkige_other_cfg})
        endif()
        "
        COMPONENT sdk)

    # --- the cmake surface ---------------------------------------------------
    # OrkigeGameModule.cmake, OrkigeAbiStamp.cmake and OrkigeWriteVersion.cmake
    # ship VERBATIM: one definition of how a module compiles, links and
    # version-checks, whether it is handed a build tree or a pack.
    # OrkigeSdkPack.cmake beside them is the MARKER that flips the helper into
    # pack mode - and the only file that knows the pack layout.
    #
    # OrkigeSdkPack.cmake is realized from its template HERE and nowhere else:
    # the helper detects a pack by finding that file beside itself, so a name
    # the engine source tree also carried would put every build-tree consumer
    # into pack mode.
    configure_file("${_module_dir}/OrkigeSdkPack.cmake.in"
        "${CMAKE_BINARY_DIR}/sdk/OrkigeSdkPack.cmake" COPYONLY)
    install(FILES
        "${_module_dir}/OrkigeGameModule.cmake"
        "${_module_dir}/OrkigeAbiStamp.cmake"
        "${_module_dir}/OrkigeWriteVersion.cmake"
        "${CMAKE_BINARY_DIR}/sdk/OrkigeSdkPack.cmake"
        DESTINATION cmake
        COMPONENT sdk)

    # OrkigeConfig for the pack: the same template the build-tree form uses,
    # with every path spelled against the config's own directory so the pack
    # relocates. The literal ${_orkige_pkg_root} below survives configure_file
    # (@ONLY substitutes @VAR@ and leaves ${VAR} alone) and is expanded when the
    # generated config is included.
    set(ORKIGE_PACKAGE_KIND "sdk")
    set(ORKIGE_PACKAGE_ROOT_REL "..")
    # the configuration of BOTH halves of the pack (archives and closure), which
    # a consumer must match - recorded, never assumed
    set(ORKIGE_PACKAGE_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    set(ORKIGE_PACKAGE_SOURCE_ROOT "")
    set(ORKIGE_PACKAGE_CORE_LIB
        "\${_orkige_pkg_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}orkige_core${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(ORKIGE_PACKAGE_ENGINE_LIB
        "\${_orkige_pkg_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}orkige_engine${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(ORKIGE_PACKAGE_CORE_INCLUDES "\${_orkige_pkg_root}/include")
    set(ORKIGE_PACKAGE_ENGINE_INCLUDES "\${_orkige_pkg_root}/include")
    set(ORKIGE_PACKAGE_VCPKG_PREFIX "\${_orkige_pkg_root}/vcpkg")
    set(ORKIGE_PACKAGE_MEDIA_DIR "\${_orkige_pkg_root}/media")
    orkige_package_transitive_list(ORKIGE_PACKAGE_TRANSITIVE)
    orkige_package_compile_definitions(
        ORKIGE_PACKAGE_CORE_DEFS ORKIGE_PACKAGE_ENGINE_DEFS)
    configure_file("${_module_dir}/OrkigeConfig.cmake.in"
        "${CMAKE_BINARY_DIR}/sdk/OrkigeConfig.cmake" @ONLY)
    install(FILES "${CMAKE_BINARY_DIR}/sdk/OrkigeConfig.cmake"
        DESTINATION cmake
        COMPONENT sdk)

    # --- the recorded ABI stamp (LAST) --------------------------------------
    # Computed over the pack's OWN installed surface (the surface the
    # OrkigeSdkPack.cmake marker names, so both sides read ONE definition of
    # what a pack's ABI surface is), through the SAME writer the build-tree
    # package uses. It must run after every file above is in place, which
    # install(CODE) at the end of the rule list guarantees: CMake runs install
    # rules in declaration order.
    install(CODE "
        include(\"\${CMAKE_INSTALL_PREFIX}/cmake/OrkigeSdkPack.cmake\")
        execute_process(COMMAND \"${CMAKE_COMMAND}\"
            \"-DORKIGE_ROOT=\${CMAKE_INSTALL_PREFIX}\"
            \"-DORKIGE_ABI_OUT_DIR=\${CMAKE_INSTALL_PREFIX}/cmake\"
            \"-DORKIGE_ABI_TAG=sdk\"
            \"-DORKIGE_ABI_SOURCE_DIRS=\${ORKIGE_SDK_ABI_SOURCE_DIRS}\"
            \"-DORKIGE_ABI_EXTRA_FILES=\${ORKIGE_SDK_ABI_EXTRA_FILES}\"
            -P \"\${CMAKE_INSTALL_PREFIX}/cmake/OrkigeWriteVersion.cmake\"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"orkige_install_sdk: recording the pack ABI stamp failed (\${_rc})\")
        endif()
        file(READ \"\${CMAKE_INSTALL_PREFIX}/cmake/OrkigeAbiStamp.txt\" _s)
        message(STATUS \"Orkige SDK pack ABI stamp: \${_s}\")
        "
        COMPONENT sdk)
endfunction()
