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
    set(ORKIGE_PACKAGE_MEDIA_DIR "")

    # the vcpkg dependency closure the two archives link (mirrors the find set in
    # cmake/OrkigeGameModule.cmake); DECLARED for consumers, realized there
    set(ORKIGE_PACKAGE_TRANSITIVE
        "SDL3" "OpenAL" "Jolt" "tinyxml2" "NanoSVG")
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        list(APPEND ORKIGE_PACKAGE_TRANSITIVE "OGRE-Next" "assimp")
    else()
        list(APPEND ORKIGE_PACKAGE_TRANSITIVE "OGRE")
    endif()
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        list(APPEND ORKIGE_PACKAGE_TRANSITIVE "Lua" "sol2")
    endif()

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
