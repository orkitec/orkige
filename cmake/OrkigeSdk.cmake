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
#                            OrkigeGameModule.cmake, OrkigeTargetShape.cmake +
#                            what its recipes need (OrkigeModuleEntry.cpp, the
#                            apple/ plist template), a CROSS pack's
#                            OrkigeSdkToolchain.cmake, and OrkigeSdkPack.cmake
#                            (the marker that puts the game-module helper into
#                            pack mode, and the one description of the layout)
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
#
# tools/ and bin/ are deliberately NOT here. They hold HOST EXECUTABLES a port
# installs beside its library (compressors, validators, uninstall scripts), and
# a game-module build invokes none of them - it configures with find_package and
# compiles. Shipping them is pure size and license surface, and on macOS it is
# worse than that: executables inside a downloaded archive carry the quarantine
# attribute, so they would meet Gatekeeper on a user's machine in a way our own
# machines never see. A pack that carries no host executables cannot have that
# problem. If a future target genuinely needs a tool at module-build time, ship
# THAT tool by name rather than the whole directory.
set(ORKIGE_SDK_VCPKG_SHARED_SUBDIRS include share plugins etc loader)
set(ORKIGE_SDK_VCPKG_RELEASE_SUBDIRS lib)
set(ORKIGE_SDK_VCPKG_DEBUG_SUBDIRS debug/lib debug/plugins debug/etc debug/loader)

# Ports in the engine build's closure that a GAME MODULE never links.
#
# The closure is the engine BUILD's closure, and the engine build is more than
# the game runtime: it also builds the editor and the test suite. Their
# dependencies are installed beside the runtime's and would otherwise ride into
# every pack a user downloads - size, and a license surface nothing in a shipped
# game consumes.
#
# Pruning is exact rather than pattern-matched: vcpkg records every file each
# port installed, and those manifests are read at install time to delete
# precisely that port's files. A port whose files are wrongly removed does not
# fail quietly - the sdk_pack test compiles a translation unit over every engine
# header against the pack and then builds and RUNS a real module from it, on
# both flavors, so an over-eager entry here is a test failure.
set(ORKIGE_SDK_PRUNE_PORTS
    # the unit-test framework: tests/ only, never the engine or a game
    catch2
    # the editor's gizmo widget, its code editor and its terminal. imgui
    # ITSELF is not here: classic OGRE's overlay is built against it, so its
    # package config requires it and a classic pack that dropped it would fail
    # a consumer's configure. It is pruned in the next-flavor list below, where
    # nothing in the closure asks for it.
    imguizmo imgui-color-text-edit libvterm
    # the texture-cook encoder, which runs in the host CLI tools/texcook; the
    # runtime reads cooked containers through its render backend's own codecs
    ktx)

# orkige_install_sdk()
#   Adds the install() rules that produce the pack. Called once from the root
#   CMakeLists, for every target whose game-module link closure is derived
#   (@see cmake/OrkigeGameModule.cmake) - a pack is per target, and one for a
#   target whose modules cannot link would be a promise the pack cannot keep.
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

    # --- third-party license notices -----------------------------------------
    # a pack carries the closure a module links, so it carries that closure's
    # notices too; it is also an engine source the exporter packages a game
    # from, and the notices file is looked for at the pack root
    if(EXISTS "${_src}/THIRD-PARTY-NOTICES.md")
        install(FILES "${_src}/THIRD-PARTY-NOTICES.md"
            DESTINATION .
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
            # install(DIRECTORY a/b DESTINATION d) lands at d/b, so a nested
            # entry keeps its parent explicitly
            get_filename_component(_sub_parent "${_sub}" DIRECTORY)
            install(DIRECTORY "${_triplet}/${_sub}"
                DESTINATION "vcpkg/${_sub_parent}"
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

    # --- prune the ports a game module never links --------------------------
    # The set is the editor/test dependencies above plus, in a pack of one
    # flavor, the OTHER flavor's render backend. A tree that builds the Ogre-Next
    # backend still installs classic OGRE (it is a base dependency, so both are
    # present side by side and file-disjoint) together with the windowing library
    # only classic uses - which is roughly thirty megabytes of a pack that can
    # never load either.
    #
    # vcpkg's own installed-file manifests are the authority for WHICH files a
    # port owns, so a shared dependency is never touched: pruning classic OGRE
    # leaves the image and mesh codecs both backends use exactly where they are.
    # The manifests live in the build tree's bookkeeping directory, which is read
    # here and never copied into the pack.
    set(_prune_ports ${ORKIGE_SDK_PRUNE_PORTS})
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        # classic OGRE, the windowing library only it uses, and the immediate-
        # mode UI its overlay is built against - none of which a pack that
        # cannot load classic will ever open
        list(APPEND _prune_ports ogre sdl2 imgui)
    else()
        list(APPEND _prune_ports ogre-next)
    endif()
    get_filename_component(_vcpkg_installed "${_triplet}" DIRECTORY)
    get_filename_component(_triplet_name "${_triplet}" NAME)
    set(_prune_lists "")
    foreach(_port IN LISTS _prune_ports)
        file(GLOB _port_lists
            "${_vcpkg_installed}/vcpkg/info/${_port}_*_${_triplet_name}.list")
        list(APPEND _prune_lists ${_port_lists})
    endforeach()
    if(_prune_lists)
        install(CODE "
            set(_orkige_pruned 0)
            set(_orkige_empty_dirs \"\")
            foreach(_orkige_list ${_prune_lists})
                file(STRINGS \"\${_orkige_list}\" _orkige_owned)
                foreach(_orkige_entry IN LISTS _orkige_owned)
                    # entries are '<triplet>/<path>'; directories end in '/'.
                    # Drop the FIRST segment by hand - a regex replace would
                    # strip every segment, since it replaces all matches
                    string(FIND \"\${_orkige_entry}\" \"/\" _orkige_cut)
                    if(_orkige_cut LESS 0)
                        continue()
                    endif()
                    math(EXPR _orkige_cut \"\${_orkige_cut} + 1\")
                    string(SUBSTRING \"\${_orkige_entry}\" \${_orkige_cut} -1
                        _orkige_rel)
                    if(_orkige_rel STREQUAL \"\" OR _orkige_rel MATCHES \"/$\")
                        continue()
                    endif()
                    set(_orkige_victim
                        \"\${CMAKE_INSTALL_PREFIX}/vcpkg/\${_orkige_rel}\")
                    if(EXISTS \"\${_orkige_victim}\")
                        file(REMOVE \"\${_orkige_victim}\")
                        math(EXPR _orkige_pruned \"\${_orkige_pruned} + 1\")
                        get_filename_component(_orkige_victim_dir
                            \"\${_orkige_victim}\" DIRECTORY)
                        list(APPEND _orkige_empty_dirs \"\${_orkige_victim_dir}\")
                    endif()
                endforeach()
            endforeach()
            # the directories those files sat in are the ports' own; walk each
            # one upwards while it is empty, so the pack shows only what it
            # carries. Bounded to the directories actually touched - a
            # recursive sweep of a closure this size would cost more than the
            # whole install
            if(_orkige_empty_dirs)
                list(REMOVE_DUPLICATES _orkige_empty_dirs)
                foreach(_orkige_dir IN LISTS _orkige_empty_dirs)
                    set(_orkige_walk \"\${_orkige_dir}\")
                    foreach(_orkige_up RANGE 5)
                        if(NOT IS_DIRECTORY \"\${_orkige_walk}\")
                            break()
                        endif()
                        file(GLOB _orkige_kids LIST_DIRECTORIES true
                            \"\${_orkige_walk}/*\")
                        if(_orkige_kids)
                            break()
                        endif()
                        file(REMOVE_RECURSE \"\${_orkige_walk}\")
                        get_filename_component(_orkige_walk
                            \"\${_orkige_walk}\" DIRECTORY)
                        if(NOT _orkige_walk MATCHES \"/vcpkg/\")
                            break()
                        endif()
                    endforeach()
                endforeach()
            endif()
            message(STATUS
                \"Orkige SDK pack: pruned \${_orkige_pruned} file(s) of ports a \"
                \"game module never links (${_prune_ports})\")
            "
            COMPONENT sdk)
    endif()

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
    #
    # The marker also carries THE TARGET CONTRACT - which platform this pack
    # builds for, what shape a module takes there, and what a consumer's
    # toolchain must be. Packs are built per target, and a project's own files
    # are written against that vocabulary, so the fields are all declared and
    # filled from what the engine build actually knows; a slot this target has
    # no answer for stays empty rather than absent.
    include("${_module_dir}/OrkigeTargetShape.cmake")
    orkige_target_platform(ORKIGE_SDK_TARGET_PLATFORM ORKIGE_SDK_MODULE_SHAPE
        ORKIGE_SDK_MODULE_OUTPUT_NAME)
    orkige_target_os_floor(ORKIGE_SDK_OS_DEPLOYMENT_TARGET)
    orkige_target_apple_sysroot(ORKIGE_SDK_TARGET_SYSROOT)
    set(ORKIGE_SDK_TARGET_TRIPLET "${_triplet_name}")
    set(ORKIGE_SDK_TARGET_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT ORKIGE_SDK_TARGET_ARCHS)
        set(ORKIGE_SDK_TARGET_ARCHS "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    # THE TOOLCHAIN CONTRACT. A host pack is built by the platform's own
    # toolchain and needs no toolchain file - the machine's default compiler
    # already produces objects that link with these archives. A CROSS pack does
    # not have that luxury: the same machine builds for its own platform by
    # default, so the system, the SDK, the architectures and the OS floor have
    # to be stated, and a cmake toolchain file is the only place CMake reads
    # them early enough. It is realized below and travels as a PACK-RELATIVE
    # path so the pack relocates.
    set(ORKIGE_SDK_TOOLCHAIN_KIND "host")
    set(ORKIGE_SDK_TOOLCHAIN_VERSION "")
    set(ORKIGE_SDK_TOOLCHAIN_FILE "")
    set(ORKIGE_SDK_TOOLCHAIN_OPTIONS "")
    set(ORKIGE_SDK_TOOLCHAIN_SYSTEM "${CMAKE_SYSTEM_NAME}")
    set(_sdk_abi_toolchain "")
    if(CMAKE_CROSSCOMPILING AND APPLE AND ORKIGE_SDK_TARGET_SYSROOT)
        set(ORKIGE_SDK_TOOLCHAIN_KIND "apple-cross")
        # the platform SDK this closure was compiled against, asked of the
        # machine's own Xcode rather than parsed out of a path
        execute_process(COMMAND xcrun --sdk "${ORKIGE_SDK_TARGET_SYSROOT}"
                --show-sdk-version
            OUTPUT_VARIABLE ORKIGE_SDK_TOOLCHAIN_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        set(ORKIGE_SDK_TOOLCHAIN_FILE "cmake/OrkigeSdkToolchain.cmake")
    endif()
    if(ORKIGE_SDK_TOOLCHAIN_FILE)
        configure_file("${_module_dir}/OrkigeSdkToolchain.cmake.in"
            "${CMAKE_BINARY_DIR}/sdk/OrkigeSdkToolchain.cmake" @ONLY)
        install(FILES "${CMAKE_BINARY_DIR}/sdk/OrkigeSdkToolchain.cmake"
            DESTINATION cmake
            COMPONENT sdk)
        # it decides how every object in a consumer's module is compiled, which
        # makes it ABI surface exactly like the game-module helper beside it
        set(_sdk_abi_toolchain "${ORKIGE_SDK_TOOLCHAIN_FILE}")
    endif()
    set(ORKIGE_SDK_ABI_TOOLCHAIN_FILE "${_sdk_abi_toolchain}")
    # the Apple bundle recipe's plist template, which the shape helper resolves
    # beside itself - a pack whose modules are bundles must carry it
    if(ORKIGE_SDK_MODULE_SHAPE STREQUAL "appbundle")
        orkige_apple_plist_template(_sdk_plist_template)
        install(FILES "${_sdk_plist_template}"
            DESTINATION cmake/apple
            COMPONENT sdk)
    endif()
    set(ORKIGE_SDK_CXX_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
    set(ORKIGE_SDK_CXX_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
    set(ORKIGE_SDK_CXX_STDLIB "libstdc++")
    if(ORKIGE_STDLIB_LIBCXX OR APPLE)
        set(ORKIGE_SDK_CXX_STDLIB "libc++")
    endif()
    set(ORKIGE_SDK_BUILD_HOST
        "${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_VERSION}")
    configure_file("${_module_dir}/OrkigeSdkPack.cmake.in"
        "${CMAKE_BINARY_DIR}/sdk/OrkigeSdkPack.cmake" @ONLY)
    install(FILES
        "${_module_dir}/OrkigeGameModule.cmake"
        "${_module_dir}/OrkigeAbiStamp.cmake"
        "${_module_dir}/OrkigeWriteVersion.cmake"
        "${_module_dir}/OrkigeTargetShape.cmake"
        "${_module_dir}/OrkigeModuleEntry.cpp"
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
    orkige_package_private_definitions(ORKIGE_PACKAGE_PRIVATE_DEFS)
    orkige_package_compile_options(
        ORKIGE_PACKAGE_CORE_OPTIONS ORKIGE_PACKAGE_ENGINE_OPTIONS)
    orkige_package_link_options(ORKIGE_PACKAGE_LINK_OPTIONS)
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
        # A prefix the caller spelled natively (`--prefix C:\\\\foo`) arrives
        # with backslashes, and these paths are handed to scripts that RE-PARSE
        # them as CMake code - where a backslash reads as an escape and
        # `\\\\installed` is the invalid escape `\\\\i`. Normalise once, here,
        # rather than at each use.
        file(TO_CMAKE_PATH \"\${CMAKE_INSTALL_PREFIX}\" CMAKE_INSTALL_PREFIX)
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
