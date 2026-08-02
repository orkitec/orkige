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
# THE macOS FLOOR, and it belongs here as much as in the presets. Without a
# deployment target every object records the SDK's own minimum, so a build on a
# current machine produces binaries that refuse to launch on anything older -
# a shipped editor that only runs on the newest macOS. The engine pins the same
# 14.0 (CMakePresets.json macos-base); pinning only one side would be worse
# than pinning neither, because a link of newer-minimum dependency objects into
# an older-minimum binary is a build that WARNS and a binary that claims a floor
# it cannot honour. Changing this number rebuilds the whole arm64-osx closure -
# it is part of every port's ABI hash - so it is a deliberate, coupled edit.
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_IGNORE_PREFIX_PATH=/usr/local)
