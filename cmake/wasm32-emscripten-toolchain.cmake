# Chainload toolchain for every wasm32-emscripten build - vcpkg ports (via
# triplets/wasm32-emscripten.cmake) AND the engine tree (via the web-release
# preset) include this one file, so the whole link closure agrees on the one
# ABI-relevant codegen mode:
#
#   -fwasm-exceptions   native WebAssembly exception handling. Emscripten
#                       disables C++ exceptions by default, but the engine and
#                       OGRE throw for recoverable errors, and a throw crossing
#                       a frame compiled without EH support aborts the module.
#                       For C objects the same flag lowers setjmp/longjmp
#                       (Lua's error path) onto wasm unwinding - mixing modes
#                       leaves emscripten_longjmp/__wasm_longjmp unresolved at
#                       link time.
#
# The flags must live HERE, not in the triplet's VCPKG_C(XX)_FLAGS: vcpkg has
# no emscripten toolchain of its own (scripts/toolchains/ has none), so for a
# chainload-only platform those triplet variables never reach the compiler.
# CMAKE_*_FLAGS_INIT is the documented toolchain-file seam; the emsdk platform
# file below does not touch it.
#
# emsdk lives user-local (~/Development/emsdk) unless the caller exported
# EMSDK. Install: see triplets/wasm32-emscripten.cmake.
set(CMAKE_C_FLAGS_INIT "-fwasm-exceptions")
set(CMAKE_CXX_FLAGS_INIT "-fwasm-exceptions")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fwasm-exceptions")

if(NOT DEFINED ENV{EMSDK})
    set(ENV{EMSDK} "$ENV{HOME}/Development/emsdk")
endif()
include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
