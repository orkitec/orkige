# CI emulator triplet: mirrors the production arm64-android settings for the
# x86_64 system image used by GitHub's hardware-accelerated Android emulator.
# Shipping builds remain arm64; this triplet exists only to execute the real
# Android player in CI.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 28)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=x86_64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DANDROID_ABI=x86_64
    -DCMAKE_IGNORE_PREFIX_PATH=/usr/local)
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    set(ENV{ANDROID_NDK_HOME} "$ENV{ANDROID_HOME}/ndk/27.2.12479018")
endif()
