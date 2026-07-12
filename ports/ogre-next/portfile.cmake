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
        apple-ninja-objcxx-sysroot.patch          # enable OBJC/OBJCXX for the .mm sources + do not clobber CMAKE_OSX_SYSROOT (macOS "macosx" and iOS "iphoneos") with a symbolic name under single-config generators (Xcode-only assumptions upstream)
        apple-ninja-no-framework-postbuild.patch  # macOS: the framework-header POST_BUILD uses Xcode $(PLATFORM_NAME) vars - guard it behind OGRE_BUILD_LIBS_AS_FRAMEWORKS
        vulkan-no-shaderc-probe.patch             # the Vulkan RS compiles GLSL via glslang only - stop the find-probe from requiring shaderc_combined (absent from vcpkg and the NDK), which silently disabled the whole RS
        lib-install-path.patch                    # iOS + Windows: keep the standard vcpkg bin/lib layout (upstream installs into per-config lib/Release-style subdirs there, plugins under /opt on Windows)
)

# Render system per platform: Metal is Ogre-Next's first-class RS on Apple
# (macOS and iOS); on Linux and Android it is Vulkan. The legacy GL3+/GLES
# paths stay off everywhere.
#   - Linux: Vulkan with XCB windowing (needs the system X11/xcb dev packages:
#     libx11-xcb-dev, libxcb-randr0-dev, plus libxt-dev/libxaw7-dev for the
#     required X11 dependency probe). Vulkan headers/loader come from vcpkg
#     (see vcpkg.json); glslang headers resolve through the same installed
#     include tree via FindVulkan's Vulkan_INCLUDE_DIRS.
#   - Android: Vulkan with the ANativeWindow surface. The loader and headers
#     come from the NDK sysroot (API 28 >= Vulkan 1.1), treated as driver-tier
#     the same way MoltenVK is on macOS; glslang is a vcpkg dependency.
#   - macOS / iOS: Metal only, no Vulkan find-probe.
if(VCPKG_TARGET_IS_OSX)
    set(RENDERSYSTEM_OPTIONS
        # Metal is the first-class render system of Ogre-Next on Apple; the
        # legacy GL3+ path (OpenGL 4.1 on macOS) buys nothing here
        -DOGRE_BUILD_RENDERSYSTEM_METAL=ON
        -DOGRE_BUILD_RENDERSYSTEM_VULKAN=OFF
        # hermeticity: no Vulkan RS on Apple -> nothing may probe for a
        # system Vulkan SDK
        -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON
    )
elseif(VCPKG_TARGET_IS_IOS)
    set(RENDERSYSTEM_OPTIONS
        # OGRE_BUILD_PLATFORM_APPLE_IOS is Ogre-Next's own iOS switch (a plain
        # option upstream never sets from the toolchain): it selects the UIKit
        # platform sources, the iOS view path and codec set. Setting it here
        # keeps the iOS-simulator triplet untouched.
        -DOGRE_BUILD_PLATFORM_APPLE_IOS=ON
        -DOGRE_BUILD_RENDERSYSTEM_METAL=ON
        -DOGRE_BUILD_RENDERSYSTEM_VULKAN=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON
    )
elseif(VCPKG_TARGET_IS_ANDROID)
    set(RENDERSYSTEM_OPTIONS
        -DOGRE_BUILD_RENDERSYSTEM_METAL=OFF
        -DOGRE_BUILD_RENDERSYSTEM_VULKAN=ON
        # no XCB on Android: the Vulkan ANativeWindow surface auto-selects via
        # the dependent option's ANDROID condition
    )
elseif(VCPKG_TARGET_IS_WINDOWS)
    set(RENDERSYSTEM_OPTIONS
        # Vulkan with the Win32 window surface (auto-selected); Direct3D stays
        # off - the engine's render facade drives Vulkan on every non-Apple
        # platform
        -DOGRE_BUILD_RENDERSYSTEM_METAL=OFF
        -DOGRE_BUILD_RENDERSYSTEM_VULKAN=ON
        -DOGRE_BUILD_RENDERSYSTEM_D3D11=OFF
    )
else()
    set(RENDERSYSTEM_OPTIONS
        -DOGRE_BUILD_RENDERSYSTEM_METAL=OFF
        -DOGRE_BUILD_RENDERSYSTEM_VULKAN=ON
        -DOGRE_VULKAN_WINDOW_XCB=ON
    )
endif()

# Image codecs per platform: FreeImage on desktop (loads png/jpg AND encodes -
# screenshots need an encoder). Mobile drops the FreeImage dependency entirely
# and uses the in-tree STBI codec (decode-only, which is all device asset
# loading needs) - matching the classic mobile flavor.
if(VCPKG_TARGET_IS_IOS OR VCPKG_TARGET_IS_ANDROID)
    set(CODEC_OPTIONS
        -DOGRE_CONFIG_ENABLE_FREEIMAGE=OFF
        -DOGRE_CONFIG_ENABLE_STBI=ON
    )
else()
    set(CODEC_OPTIONS
        -DOGRE_CONFIG_ENABLE_FREEIMAGE=ON
        -DOGRE_CONFIG_ENABLE_STBI=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_FreeImage=ON
    )
endif()

# ogre-next resolves zlib through its own FindPkgMacros-based finder, which
# searches Unix library names and misses vcpkg's Windows archives
# (zlib.lib / zlibd.lib) - seed the variables per configuration there
if(VCPKG_TARGET_IS_WINDOWS)
    set(ZLIB_SEED_OPTIONS
        "-DZLIB_INCLUDE_DIR=${CURRENT_INSTALLED_DIR}/include"
        # ogre-next's in-tree FindVulkan searches the Unix library name
        # ('vulkan'); the Windows import library is vulkan-1.lib - seed the
        # probe the same way as zlib so the Vulkan RS is not silently dropped
        "-DVulkan_INCLUDE_DIR=${CURRENT_INSTALLED_DIR}/include")
    set(ZLIB_SEED_OPTIONS_RELEASE
        "-DZLIB_LIBRARY_REL=${CURRENT_INSTALLED_DIR}/lib/zlib.lib"
        "-DZLIB_LIBRARY=${CURRENT_INSTALLED_DIR}/lib/zlib.lib"
        "-DVulkan_LIBRARY=${CURRENT_INSTALLED_DIR}/lib/vulkan-1.lib")
    set(ZLIB_SEED_OPTIONS_DEBUG
        "-DZLIB_LIBRARY_DBG=${CURRENT_INSTALLED_DIR}/debug/lib/zlibd.lib"
        "-DZLIB_LIBRARY=${CURRENT_INSTALLED_DIR}/debug/lib/zlibd.lib"
        "-DVulkan_LIBRARY=${CURRENT_INSTALLED_DIR}/debug/lib/vulkan-1.lib")
else()
    set(ZLIB_SEED_OPTIONS "")
    set(ZLIB_SEED_OPTIONS_RELEASE "")
    set(ZLIB_SEED_OPTIONS_DEBUG "")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS_RELEASE
        ${ZLIB_SEED_OPTIONS_RELEASE}
    OPTIONS_DEBUG
        ${ZLIB_SEED_OPTIONS_DEBUG}
    OPTIONS
        ${ZLIB_SEED_OPTIONS}
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
        # per-platform render system selection (see RENDERSYSTEM_OPTIONS)
        ${RENDERSYSTEM_OPTIONS}
        -DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=OFF
        # Hlms PBS/Unlit are the material system - effectively core
        -DOGRE_BUILD_COMPONENT_HLMS_PBS=ON
        -DOGRE_BUILD_COMPONENT_HLMS_UNLIT=ON
        # AtmosphereNpr: the sky + object-fog + sun-linkage component the
        # engine_render environment surface wires (see Docs/ports.md); its sky
        # material media is installed below alongside the Hlms templates
        -DOGRE_BUILD_COMPONENT_ATMOSPHERE=ON
        # everything else off until a phase needs it
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
        # image codecs (per-platform, see CODEC_OPTIONS above)
        ${CODEC_OPTIONS}
        # zip archives wait for a real need (would add a zziplib dependency)
        -DOGRE_CONFIG_ENABLE_ZIP=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_ZLIB=ON
        # rapidjson is a hard OgreMain dependency in 3.0 (OgreRootLayout.cpp
        # includes it unconditionally), not just the Hlms-JSON option
        -DCMAKE_REQUIRE_FIND_PACKAGE_Rapidjson=ON
        # hermeticity: nothing may resolve outside the vcpkg tree (the
        # Vulkan disable moved into RENDERSYSTEM_OPTIONS - on Linux the
        # Vulkan RS is on and FindVulkan resolves to the vcpkg
        # vulkan-headers/vulkan-loader ports)
        -DCMAKE_DISABLE_FIND_PACKAGE_ZZip=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_RenderDoc=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Freetype=ON
    MAYBE_UNUSED_VARIABLES
        CMAKE_DISABLE_FIND_PACKAGE_SDL2
        CMAKE_DISABLE_FIND_PACKAGE_Doxygen
        OGRE_BUILD_COMPONENT_PROPERTY
        OGRE_VULKAN_WINDOW_XCB
)

vcpkg_cmake_install()

# Guard against a silent render-system disable. OGRE_BUILD_RENDERSYSTEM_VULKAN
# is a cmake_dependent_option gated on Vulkan_FOUND, so a broken find-probe
# would drop the whole Vulkan RS and STILL complete the build "successfully"
# (only a feature-summary log line). On the Vulkan platforms (Linux, Android)
# assert the RS actually landed - fail here, in the port build, not later in
# the consumer's generate step where the missing interface include dir is the
# only symptom.
if(VCPKG_TARGET_IS_LINUX OR VCPKG_TARGET_IS_ANDROID OR VCPKG_TARGET_IS_WINDOWS)
    # the RS plugins install under lib/OGRE-Next/ on non-Apple
    # (OGRE_PLUGIN_PATH=/OGRE-Next), unlike lib/ directly on Apple
    if(VCPKG_TARGET_IS_WINDOWS)
        set(_vulkan_rs_lib "${CURRENT_PACKAGES_DIR}/lib/OGRE-Next/RenderSystem_VulkanStatic.lib")
    else()
        set(_vulkan_rs_lib "${CURRENT_PACKAGES_DIR}/lib/OGRE-Next/libRenderSystem_VulkanStatic.a")
    endif()
    if(NOT EXISTS "${_vulkan_rs_lib}")
        message(FATAL_ERROR "Vulkan render system static library missing - the Vulkan find-probe or build was silently disabled")
    endif()
    if(NOT IS_DIRECTORY "${CURRENT_PACKAGES_DIR}/include/OGRE-Next/RenderSystems/Vulkan/include")
        message(FATAL_ERROR "Vulkan render system headers missing - the Vulkan render system was silently disabled")
    endif()
endif()

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

# AtmosphereNpr sky material media (the runtime registers this alongside the
# Hlms templates - see Docs/ports.md). ONLY the atmosphere sky programs +
# material are shipped, NOT the whole Samples Common material set (which carries
# unrelated sample effects + heavyweight LUT .dds files). The object-fog HlmsPbs
# integration pieces (Pbs/Any/Atmosphere/*.any) already ride in the Pbs copy
# above; these are the standalone sky-dome material + its quad vertex program.
set(_atmo_src "${SOURCE_PATH}/Samples/Media/2.0/scripts/materials/Common")
set(_atmo_dst "${CURRENT_PACKAGES_DIR}/share/${PORT}/Media/Atmosphere")
file(COPY "${_atmo_src}/Atmosphere.material" DESTINATION "${_atmo_dst}")
# a trimmed quad vertex program (only what Atmosphere.material references) in
# place of the samples' full Quad.program, so no unrelated quad shaders are
# needed to parse the media set cleanly
file(COPY "${CMAKE_CURRENT_LIST_DIR}/atmosphere-media/AtmosphereQuad.program"
    DESTINATION "${_atmo_dst}")
file(COPY "${_atmo_src}/Any/AtmosphereNprSky_ps.any" DESTINATION "${_atmo_dst}/Any")
# the sky fragment shader + its quad vertex shader, per shading language
# (Metal drives Apple; glsl covers the Vulkan RS on Android/Linux via the
# glslvk delegate; hlsl for a future D3D path). The material resolves the
# right delegate at load time from the running render system.
file(COPY "${_atmo_src}/Metal/AtmosphereNprSky_ps.metal"    DESTINATION "${_atmo_dst}/Metal")
file(COPY "${_atmo_src}/Metal/QuadCameraDirNoUV_vs.metal"   DESTINATION "${_atmo_dst}/Metal")
file(COPY "${_atmo_src}/GLSL/AtmosphereNprSky_ps.glsl"      DESTINATION "${_atmo_dst}/GLSL")
file(COPY "${_atmo_src}/GLSL/QuadCameraDirNoUV_vs.glsl"     DESTINATION "${_atmo_dst}/GLSL")
file(COPY "${_atmo_src}/HLSL/AtmosphereNprSky_ps.hlsl"      DESTINATION "${_atmo_dst}/HLSL")
file(COPY "${_atmo_src}/HLSL/QuadCameraDirNoUV_vs.hlsl"     DESTINATION "${_atmo_dst}/HLSL")

# upstream installs no CMake package config (only .pc files) - ship ours
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/OGRE-NextConfig.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
