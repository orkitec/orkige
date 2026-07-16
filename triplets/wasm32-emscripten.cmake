# Overlay triplet: same as vcpkg's community wasm32-emscripten (static wasm
# libs via the Emscripten chainload toolchain), plus the hermetic-build
# settings this machine needs (see triplets/arm64-osx.cmake: a shared
# Intel-Homebrew lives at /usr/local and must never leak into port builds)
# and the emsdk wiring - the toolchain is located exclusively through the
# EMSDK environment variable, so pin it here to the user-local install
# (~/Development/emsdk, never system-wide) unless the caller already
# exported one (CMakePresets.json exports the same path for the project
# configure). Reproduce the install with:
#   git clone https://github.com/emscripten-core/emsdk.git ~/Development/emsdk
#   ~/Development/emsdk/emsdk install latest && ~/Development/emsdk/emsdk activate latest
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)

if(NOT DEFINED ENV{EMSDK})
    set(ENV{EMSDK} "$ENV{HOME}/Development/emsdk")
endif()
if(DEFINED ENV{EMSCRIPTEN_ROOT})
    set(EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN_ROOT}")
else()
    set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
endif()

if(NOT EXISTS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
    message(FATAL_ERROR "Emscripten.cmake toolchain file not found under "
        "'${EMSCRIPTEN_ROOT}' - install the user-local emsdk (see the comment "
        "at the top of triplets/wasm32-emscripten.cmake) or export EMSDK")
endif()

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
# the chainload is the repo's own wrapper around the emsdk platform file: it
# seeds the ABI-relevant codegen flags (native wasm exception handling) every
# wasm object in the closure must share - vcpkg has no emscripten toolchain,
# so the VCPKG_C(XX)_FLAGS triplet variables would never reach the compiler
# here. See cmake/wasm32-emscripten-toolchain.cmake for the full rationale.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/wasm32-emscripten-toolchain.cmake")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_IGNORE_PREFIX_PATH=/usr/local)
