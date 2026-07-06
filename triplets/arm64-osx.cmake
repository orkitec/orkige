# Overlay triplet: same as vcpkg's community arm64-osx, plus hermetic-build
# settings. This machine has a shared Intel-Homebrew at /usr/local whose
# headers otherwise leak into port builds (clang searches /usr/local/include
# by default unless an explicit SDK sysroot is set).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_SYSROOT macosx)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_IGNORE_PREFIX_PATH=/usr/local)
