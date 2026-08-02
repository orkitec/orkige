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
