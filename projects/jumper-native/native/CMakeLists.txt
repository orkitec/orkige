# jumper-native - the native (C++) game module of the Jumper Native project:
# the jumper gameplay as compiled project code instead of a Lua script.
#
# Built by the editor's compile-on-Play pipeline (or by hand) against the
# engine's build tree - see cmake/OrkigeGameModule.cmake for the full
# contract and the required -DORKIGE_ROOT / -DORKIGE_ENGINE_BUILD_DIR cache
# variables. The manifest's "native.target" setting names the executable
# target below; it implements the player CLI contract
# ([scene.oscene] [--project <dir>] [--debug-port N]) so the editor runs it
# as the play process with the debug protocol attached.

cmake_minimum_required(VERSION 3.28)

project(jumper_native LANGUAGES CXX)

if(NOT DEFINED ORKIGE_ROOT)
    message(FATAL_ERROR "pass -DORKIGE_ROOT=<orkige source root> (the editor "
        "does this automatically; see cmake/OrkigeGameModule.cmake)")
endif()
include("${ORKIGE_ROOT}/cmake/OrkigeGameModule.cmake")

add_executable(jumper_native main.cpp)
orkige_game_module(jumper_native)

# the pure gameplay math is shared with the C++ jumper SAMPLE by including
# its header directly (samples/jumper/JumperLogic.h) - one source of truth
# for the feel-critical functions, verified headlessly by tests/jumper
target_include_directories(jumper_native PRIVATE
    "${ORKIGE_ROOT}/samples/jumper")
