# Overlay triplet: same as vcpkg's stock arm64-android (static libs, API
# level 28), plus the hermetic-build settings this machine needs (see
# triplets/arm64-osx.cmake: a shared Intel-Homebrew lives at /usr/local and
# must never leak into port builds) and the NDK wiring - vcpkg's android
# toolchain locates the NDK exclusively through the ANDROID_NDK_HOME
# environment variable, so pin it here to the SDK-managed NDK unless the
# caller already exported one (CMakePresets.json exports the same path for
# the project configure).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 28)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DANDROID_ABI=arm64-v8a
    -DCMAKE_IGNORE_PREFIX_PATH=/usr/local)
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    set(ENV{ANDROID_NDK_HOME} "$ENV{HOME}/Library/Android/sdk/ndk/27.2.12479018")
endif()
# Note (ogre): unlike iOS (which needs -DAPPLE_IOS=ON injected here), OGRE's
# CMakeLists recognizes Android through the NDK toolchain's ANDROID variable
# and then FORCES OGRE_BUILD_RENDERSYSTEM_GLES2=ON itself (CMakeLists.txt
# "Forcing OpenGL ES 2 RenderSystem for Android"), overriding the portfile's
# -DOGRE_BUILD_RENDERSYSTEM_GLES2=OFF - no per-port injection needed.
