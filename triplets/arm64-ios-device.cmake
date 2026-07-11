# Overlay triplet: arm64 iPhoneOS (physical-device) builds, the device
# counterpart of triplets/arm64-ios-simulator.cmake. Same hermetic-build
# settings this machine needs (a shared Intel-Homebrew lives at /usr/local and
# must never leak into port builds - see triplets/arm64-osx.cmake) and the same
# pinned deployment target, but the sysroot is the on-device SDK (iphoneos)
# rather than the simulator (iphonesimulator).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_SYSROOT iphoneos)
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
# Explicit arch: OGRE's CMakeLists defaults CMAKE_OSX_ARCHITECTURES to x86_64
# before project() (i.e. before vcpkg's iOS chainload toolchain runs), so the
# arch must arrive as a -D cache definition, which VCPKG_OSX_ARCHITECTURES does.
set(VCPKG_OSX_ARCHITECTURES arm64)
# Ports are static libs; nothing here needs a signing identity. Turn Xcode-style
# codesigning off so a port that happens to build a bundle/executable never asks
# for a certificate we deliberately do not carry on this machine - the real app
# signing happens later, at export time, through orkige_export.py's seam.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DCMAKE_IGNORE_PREFIX_PATH=/usr/local
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO)
if(PORT STREQUAL "ogre")
    # OGRE only recognizes an iOS build through APPLE_IOS, which upstream sets
    # exclusively in its own CMake/toolchain/ios.toolchain.xcode.cmake - vcpkg's
    # iOS toolchain never defines it, leaving OGRE half-configured as macOS
    # (OSX platform sources, GLES2 off). Injecting it here scopes the fix to
    # the ogre port without changing the overlay port (and thus the desktop
    # package hash).
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DAPPLE_IOS=ON)
endif()
