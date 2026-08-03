# OrkigeTargetShape.cmake - ONE definition of "what platform is this, and what
# shape does a game module take there".
#
# Two sides read it and they must agree: cmake/OrkigeSdk.cmake stamps the answer
# into a pack as it is installed, and cmake/OrkigeGameModule.cmake derives it
# again from the toolchain a consumer is configuring with. A pack that says
# "android" while the consumer's toolchain says "macos" is a mismatch the helper
# refuses - which only works while both sides compute the name the same way, so
# they compute it here.
#
# THE SHAPE is the pack's business, never the project's. A desktop module is an
# executable the editor runs; an Android module is the shared library the
# activity loads by a fixed name; an Apple mobile module is a bundle. A project
# that spelled add_executable() in its own CMakeLists would be frozen to the
# desktop shape and would have to be edited - by its author, in a file this
# engine does not own - the day it targets a phone. So the project says
# orkige_add_game_module(<name> <sources...>) and the shape follows the pack.

include_guard(GLOBAL)

# orkige_target_platform(<out_platform> <out_shape> <out_output_name>)
#   Derive all three from the toolchain in force. Pure: reads only CMake's own
#   system variables, sets no state, and answers the same for the engine build
#   and for a consumer's module build.
function(orkige_target_platform out_platform out_shape out_output_name)
    set(_platform "")
    set(_shape "executable")
    set(_output_name "")

    if(EMSCRIPTEN)
        # the browser player is one emscripten link: an "executable" whose
        # artifact is the .js loader beside its .wasm
        set(_platform "web")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        # the activity dlopen()s a library of exactly this name
        set(_platform "android")
        set(_shape "sharedlib")
        set(_output_name "main")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # device and simulator differ in sysroot and archs, never in shape
        set(_platform "ios")
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            set(_platform "ios-simulator")
        endif()
        set(_shape "appbundle")
    elseif(APPLE)
        set(_platform "macos")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_platform "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_platform "linux")
    else()
        # an unnamed system is not a refusal here - it is reported verbatim so
        # whoever sees it knows exactly what was in force
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" _platform)
    endif()

    set(${out_platform} "${_platform}" PARENT_SCOPE)
    set(${out_shape} "${_shape}" PARENT_SCOPE)
    set(${out_output_name} "${_output_name}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# THE APPLE BUNDLE RECIPE
#
# A shape is more than "which add_ command": an Apple mobile module is a bundle,
# and a bundle is only launchable once it carries an Info.plist and the shader
# media its render backend loads at boot. Those two steps are the same for the
# player and for a project's compiled game module, and a pack consumer has
# neither the engine's source tree nor its vcpkg directory to copy them from -
# so the recipe lives here, beside the shape derivation, and the pack ships this
# file plus the plist template next to it.
# ---------------------------------------------------------------------------

# orkige_apple_plist_template(<out>)
#   The ONE Info.plist template every Orkige app bundle is built with. Resolved
#   against this file's own directory, so it answers in an engine checkout and
#   in an unpacked SDK pack alike.
function(orkige_apple_plist_template out)
    set(${out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/apple/Info.plist.in"
        PARENT_SCOPE)
endfunction()

# orkige_apply_target_shape(<target> [NAME <n>] [IDENTIFIER <id>] [VERSION <v>])
#   Give an already-created target the properties its shape requires: the fixed
#   artifact name where the platform dictates one, and - for a bundle - the
#   identity keys plus the plist template above. Silent on the shapes that need
#   nothing (a desktop executable is just an executable).
function(orkige_apply_target_shape target)
    cmake_parse_arguments(_shape "" "NAME;IDENTIFIER;VERSION" "" ${ARGN})
    orkige_target_platform(_platform _kind _output_name)
    if(_output_name)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${_output_name}")
    endif()
    if(NOT _kind STREQUAL "appbundle")
        return()
    endif()
    if(NOT _shape_NAME)
        set(_shape_NAME "${target}")
    endif()
    if(NOT _shape_IDENTIFIER)
        set(_shape_IDENTIFIER "com.orkitec.${target}")
    endif()
    if(NOT _shape_VERSION)
        set(_shape_VERSION "1.0.0")
    endif()
    orkige_apple_plist_template(_plist)
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "${_shape_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${_shape_IDENTIFIER}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${_shape_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${_shape_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${_plist}")
endfunction()

# orkige_stage_bundle_backend_media(<target> <flavor> <backend media dir>
#                                   <engine media dir>)
#   Copy the render backend's shader media INTO the bundle, where the runtime
#   resolves it as <bundle>/Media. A bundle without it launches and then renders
#   nothing, so this is part of the shape rather than of the packaging: the app
#   the build produces is already runnable.
#
#   @param backend media dir  the flavor's OGRE media root (`share/ogre-next/
#          Media` or `share/ogre/Media`) - in the build's vcpkg triplet prefix,
#          or in a pack's bundled closure
#   @param engine media dir   the engine's own media root (orkige_engine/media,
#          or a pack's media/); the classic flavor's metal-rough shader library
#          rides MERGED into RTShaderLib, the ONE shader location the runtime
#          registers
function(orkige_stage_bundle_backend_media target flavor backend_media
        engine_media)
    orkige_target_platform(_platform _kind _output_name)
    if(NOT _kind STREQUAL "appbundle")
        return()
    endif()
    set(_content "$<TARGET_BUNDLE_CONTENT_DIR:${target}>")
    if(flavor STREQUAL "next")
        set(_copy COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${backend_media}/Hlms" "${_content}/Media/Hlms")
        # optional: an older port pin may not ship the sky media yet - the
        # runtime degrades that honestly (no sky, flat fog colour)
        if(EXISTS "${backend_media}/Atmosphere")
            list(APPEND _copy COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${backend_media}/Atmosphere" "${_content}/Media/Atmosphere")
        endif()
    else()
        set(_copy COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${backend_media}/Main" "${_content}/Media/Main"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${backend_media}/RTShaderLib" "${_content}/Media/RTShaderLib")
        if(EXISTS "${engine_media}/rtss")
            list(APPEND _copy COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${engine_media}/rtss" "${_content}/Media/RTShaderLib")
        endif()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD ${_copy}
        COMMENT "Bundling the ${flavor} backend shader media into ${target}")
endfunction()

# orkige_target_os_floor(<out>)
#   The OS version floor the binaries are built for, in whatever form the
#   platform states it. Empty where the platform has no such concept in the
#   build (Linux states its floor as the base image its packs are built on, not
#   as a compiler switch - see Docs/sdk-pack.md).
function(orkige_target_os_floor out)
    set(_floor "")
    if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
        set(_floor "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android" AND CMAKE_SYSTEM_VERSION)
        set(_floor "${CMAKE_SYSTEM_VERSION}")
    endif()
    set(${out} "${_floor}" PARENT_SCOPE)
endfunction()

# orkige_target_apple_sysroot(<out>)
#   The Apple SDK a cross build selects, reduced to the SHORT NAME the platform
#   resolves for itself ("iphonesimulator", "iphoneos", "macosx"). CMake expands
#   the setting to the absolute .sdk path of whichever Xcode is installed, and
#   that path is a fact about the BUILD MACHINE - a pack recording it would hand
#   a consumer a directory that does not exist there. The short name always
#   resolves. Empty off Apple.
function(orkige_target_apple_sysroot out)
    set(_name "")
    if(APPLE AND CMAKE_OSX_SYSROOT)
        get_filename_component(_name "${CMAKE_OSX_SYSROOT}" NAME)
        # an absolute sysroot ends in "<Name><version>.sdk"; strip both
        string(REGEX REPLACE "[0-9.]*\\.sdk$" "" _name "${_name}")
        string(TOLOWER "${_name}" _name)
    endif()
    set(${out} "${_name}" PARENT_SCOPE)
endfunction()
