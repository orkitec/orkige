# Locally authored overlay port - see Docs/ports.md for the rationale.
#
# Coexistence with the classic 'ogre' port in ONE installed tree is a hard
# requirement (Docs/render-abstraction.md - the two backends of the
# engine_render facade). Everything this port installs is namespaced away
# from classic ogre's files:
#   headers  include/OGRE-Next/...            (classic: include/OGRE/...)
#   libs     lib/libOgreNext*Static.a,
#            lib/libRenderSystem_MetalStatic.a (classic: libOgre*.a - the
#            RenderSystem_Metal name collides only WITHIN a linked binary,
#            which the ODR rule forbids anyway; the FILE names differ:
#            classic installs libRenderSystem_Metal.a, Next appends Static)
#   cmake    share/ogre-next/OGRE-NextConfig.cmake (classic: share/ogre)
#   media    share/ogre-next/Media/Hlms       (classic: share/ogre/Media)
#   pc       lib/pkgconfig/OGRE-Next.pc       (classic: OGRE.pc)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO OGRECave/ogre-next
    REF "v${VERSION}"
    SHA512 2ef8f16517c96cc7ddb31986857e4d0002e33c2eeff845b4af0b8e5848c3e92289dc3b10ededbe66fb63ef6234cbee88ed513466182bd4e70d710d0507f98418
    HEAD_REF master
    PATCHES
        apple-ninja-objcxx-sysroot.patch          # enable OBJC/OBJCXX for the .mm sources + do not clobber CMAKE_OSX_SYSROOT with the symbolic "macosx" (Xcode-only assumptions upstream)
        apple-ninja-no-framework-postbuild.patch  # macOS: the framework-header POST_BUILD uses Xcode $(PLATFORM_NAME) vars - guard it behind OGRE_BUILD_LIBS_AS_FRAMEWORKS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        # OgreNext* libs + OGRE-Next include prefix: THE coexistence switch
        -DOGRE_USE_NEW_PROJECT_NAME=ON
        -DOGRE_STATIC=ON
        -DOGRE_BUILD_LIBS_AS_FRAMEWORKS=OFF
        # vcpkg installs ONE header tree for both configs, but with the
        # default "auto" (= embed for Ninja) OgreBuildSettings.h bakes
        # OGRE_DEBUG_MODE per build type - debug consumers would compile
        # against the release header while linking the debug lib (real ABI
        # break: v2 debug bookkeeping changes struct layouts). "never" makes
        # OgrePlatform.h derive the level from _DEBUG/DEBUG/NDEBUG at
        # compile time; the shipped OGRE-NextConfig.cmake propagates the
        # matching definitions to consumers.
        -DOGRE_EMBED_DEBUG_MODE=never
        # Metal is the first-class render system of Ogre-Next on Apple; the
        # legacy GL3+ path (OpenGL 4.1 on macOS) buys nothing here
        -DOGRE_BUILD_RENDERSYSTEM_METAL=ON
        -DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=OFF
        -DOGRE_BUILD_RENDERSYSTEM_VULKAN=OFF
        # Hlms PBS/Unlit are the material system - effectively core
        -DOGRE_BUILD_COMPONENT_HLMS_PBS=ON
        -DOGRE_BUILD_COMPONENT_HLMS_UNLIT=ON
        # everything else off until a phase needs it (B-phase skeleton scope)
        -DOGRE_BUILD_COMPONENT_ATMOSPHERE=OFF
        -DOGRE_BUILD_COMPONENT_OVERLAY=OFF
        -DOGRE_BUILD_COMPONENT_MESHLODGENERATOR=OFF
        -DOGRE_BUILD_COMPONENT_PAGING=OFF
        -DOGRE_BUILD_COMPONENT_VOLUME=OFF
        -DOGRE_BUILD_COMPONENT_PROPERTY=OFF
        -DOGRE_BUILD_COMPONENT_PLANAR_REFLECTIONS=OFF
        -DOGRE_BUILD_COMPONENT_SCENE_FORMAT=OFF
        -DOGRE_BUILD_PLUGIN_PFX=OFF
        -DOGRE_BUILD_SAMPLES2=OFF
        -DOGRE_BUILD_TESTS=OFF
        -DOGRE_BUILD_TOOLS=OFF
        -DOGRE_INSTALL_SAMPLES=OFF
        -DOGRE_INSTALL_SAMPLES_SOURCE=OFF
        -DOGRE_INSTALL_TOOLS=OFF
        -DOGRE_INSTALL_DOCS=OFF
        # image codecs: FreeImage (loads png/jpg AND encodes - the STBI codec
        # in ogre-next is decode-only, screenshots need an encoder)
        -DOGRE_CONFIG_ENABLE_FREEIMAGE=ON
        -DOGRE_CONFIG_ENABLE_STBI=OFF
        # zip archives wait for a real need (would add a zziplib dependency)
        -DOGRE_CONFIG_ENABLE_ZIP=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_FreeImage=ON
        -DCMAKE_REQUIRE_FIND_PACKAGE_ZLIB=ON
        # rapidjson is a hard OgreMain dependency in 3.0 (OgreRootLayout.cpp
        # includes it unconditionally), not just the Hlms-JSON option
        -DCMAKE_REQUIRE_FIND_PACKAGE_Rapidjson=ON
        # hermeticity: nothing may resolve outside the vcpkg tree
        -DCMAKE_DISABLE_FIND_PACKAGE_ZZip=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_RenderDoc=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Freetype=ON
    MAYBE_UNUSED_VARIABLES
        CMAKE_DISABLE_FIND_PACKAGE_SDL2
        CMAKE_DISABLE_FIND_PACKAGE_Doxygen
        OGRE_BUILD_COMPONENT_PROPERTY
)

vcpkg_cmake_install()

# The .pc files upstream templates are wrong for this configuration (they
# unconditionally Require: gl, which is neither built nor installed) and the
# CMake/ directory is the in-tree find-module toolbox, not package config.
# Consumers use the OGRE-NextConfig.cmake this port ships below.
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig"
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig"
    "${CURRENT_PACKAGES_DIR}/CMake"
    "${CURRENT_PACKAGES_DIR}/debug/CMake"
)

# the HLMS shader template library - runtime data every Ogre-Next app must
# register before loading Hlms implementations (Terra belongs to the unbuilt
# Terrain samples and stays out)
foreach(hlms_dir Common Pbs Unlit)
    file(COPY "${SOURCE_PATH}/Samples/Media/Hlms/${hlms_dir}"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/Media/Hlms")
endforeach()

# upstream installs no CMake package config (only .pc files) - ship ours
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/OGRE-NextConfig.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
