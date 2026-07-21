# OGRE-NextConfig.cmake - vcpkg-port-authored package config for ogre-next.
# Upstream installs no CMake package files (only pkg-config); this config
# provides namespaced imported targets so consumers never collide with the
# classic ogre port's exported OgreMain/RenderSystem_* target names:
#
#   OgreNext::Main                core (OgreNextMainStatic)
#   OgreNext::HlmsPbs             physically based material system
#   OgreNext::HlmsUnlit           unlit material system
#   OgreNext::Atmosphere          sky dome + object fog + sun linkage
#   OgreNext::PlanarReflections   mirror-of-scene planar reflection subsystem
#   OgreNext::RenderSystem_Metal  Metal render system (Apple; static plugin)
#   OgreNext::RenderSystem_Vulkan Vulkan render system (Linux/Android; static)
#
# Variables:
#   OGRE_NEXT_INCLUDE_DIR  include/OGRE-Next
#   OGRE_NEXT_MEDIA_DIR    share/ogre-next/Media (HLMS shader templates)

# Platform detection for the per-platform link interfaces below. Apple covers
# macOS and iOS (Metal); non-Apple covers Linux and Android (Vulkan).
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_ogre_next_ios TRUE)
else()
    set(_ogre_next_ios FALSE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(_ogre_next_android TRUE)
else()
    set(_ogre_next_android FALSE)
endif()
if(_ogre_next_ios OR _ogre_next_android)
    set(_ogre_next_mobile TRUE)
else()
    set(_ogre_next_mobile FALSE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_ogre_next_windows TRUE)
else()
    set(_ogre_next_windows FALSE)
endif()

include(CMakeFindDependencyMacro)
find_dependency(ZLIB)
if(NOT _ogre_next_mobile)
    # FreeImage is the desktop image codec (decode + encode); mobile builds
    # the port with the in-tree STBI codec and no FreeImage dependency
    find_dependency(freeimage CONFIG)
endif()
if(NOT APPLE)
    # the Vulkan RS: on Linux the loader+headers come from the vcpkg vulkan-*
    # ports, on Android from the NDK sysroot (CMake's built-in FindVulkan
    # resolves either into Vulkan::Vulkan); glslang does the runtime
    # GLSL->SPIR-V compile the RS performs (the upstream static lib does not
    # carry its link interface - the consumer must)
    find_dependency(Vulkan)
    find_dependency(glslang CONFIG)
endif()

get_filename_component(_ogre_next_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(OGRE_NEXT_INCLUDE_DIR "${_ogre_next_prefix}/include/OGRE-Next")
set(OGRE_NEXT_MEDIA_DIR "${CMAKE_CURRENT_LIST_DIR}/Media")

# Install-layout differences between Apple and non-Apple (Linux/Android):
#   - RS plugins (RenderSystem_*) install under lib/OGRE-Next/ on non-Apple
#     (OGRE_PLUGIN_PATH=/OGRE-Next), but directly in lib/ on Apple
#     (OGRE_PLUGIN_PATH=/). Core libs (Main/Hlms) are in lib/ everywhere.
#   - non-Apple debug libs carry OGRE's "_d" debug postfix; Apple debug libs
#     do not (they share the release name in the separate debug tree).
if(APPLE)
    set(_ogre_next_dbg_suffix "")
    set(_ogre_next_plugin_subdir "")
else()
    set(_ogre_next_dbg_suffix "_d")
    set(_ogre_next_plugin_subdir "OGRE-Next/")
endif()
# static archive naming: lib<name>.a on Unix-family platforms, <name>.lib on
# Windows (MSVC/clang-cl layout)
if(_ogre_next_windows)
    set(_ogre_next_lib_prefix "")
    set(_ogre_next_lib_ext ".lib")
else()
    set(_ogre_next_lib_prefix "lib")
    set(_ogre_next_lib_ext ".a")
endif()

# is_plugin TRUE routes through the RS plugin subdirectory (non-Apple only)
function(_ogre_next_add_library target libname is_plugin)
    if(TARGET ${target})
        return()
    endif()
    if(is_plugin)
        set(_sub "${_ogre_next_plugin_subdir}")
    else()
        set(_sub "")
    endif()
    add_library(${target} STATIC IMPORTED)
    set_target_properties(${target} PROPERTIES
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
        IMPORTED_LOCATION_RELEASE "${_ogre_next_prefix}/lib/${_sub}${_ogre_next_lib_prefix}${libname}${_ogre_next_lib_ext}"
        IMPORTED_LOCATION_DEBUG "${_ogre_next_prefix}/debug/lib/${_sub}${_ogre_next_lib_prefix}${libname}${_ogre_next_dbg_suffix}${_ogre_next_lib_ext}"
        # single-name fallback for configurations beyond Release/Debug
        IMPORTED_LOCATION "${_ogre_next_prefix}/lib/${_sub}${_ogre_next_lib_prefix}${libname}${_ogre_next_lib_ext}"
    )
endfunction()

if(_ogre_next_ios)
    # iOS: UIKit platform sources; no Cocoa/Carbon/IOKit (those are macOS)
    set(_ogre_next_main_platform_libs "-framework Foundation;-framework UIKit;-framework QuartzCore;-framework CoreGraphics")
elseif(APPLE)
    set(_ogre_next_main_platform_libs "-framework Foundation;-framework IOKit;-framework Cocoa;-framework Carbon;-framework CoreVideo")
elseif(_ogre_next_android)
    # Android: the NDK platform libs OgreMain's logging/native-window plumbing
    # references (no X11/pthread - the NDK folds pthread into libc)
    set(_ogre_next_main_platform_libs "android;log;dl")
elseif(_ogre_next_windows)
    # Windows: OgreMain's timers/window plumbing (the Win32 platform sources)
    set(_ogre_next_main_platform_libs "winmm")
else()
    # Linux: OgreMain's threading/plugin loading + X11 window-event plumbing.
    # Xt/Xaw/Xrandr back the GLX config dialog compiled into OgreMain (system
    # packages libxt-dev/libxaw7-dev/libxrandr-dev - the same set the classic
    # OGRE build needs).
    set(_ogre_next_main_platform_libs "X11;Xt;Xaw;Xrandr;pthread;dl")
endif()

if(_ogre_next_mobile)
    set(_ogre_next_image_libs "ZLIB::ZLIB")
else()
    set(_ogre_next_image_libs "ZLIB::ZLIB;freeimage::FreeImage")
endif()

_ogre_next_add_library(OgreNext::Main OgreNextMainStatic FALSE)
set_target_properties(OgreNext::Main PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}"
    # the port builds with OGRE_EMBED_DEBUG_MODE=never: OGRE_DEBUG_MODE is
    # derived from _DEBUG/DEBUG/NDEBUG in OgrePlatform.h. The debug libs
    # were compiled with DEBUG=1 (upstream injects it into
    # CMAKE_CXX_FLAGS_DEBUG), so debug consumers MUST see the same macros -
    # OGRE_DEBUG_MODE is ABI-relevant in Ogre-Next (debug bookkeeping in
    # the v2 memory managers changes struct layouts)
    INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:DEBUG=1;_DEBUG=1>"
    INTERFACE_LINK_LIBRARIES "${_ogre_next_image_libs};${_ogre_next_main_platform_libs}"
)

_ogre_next_add_library(OgreNext::HlmsPbs OgreNextHlmsPbsStatic FALSE)
set_target_properties(OgreNext::HlmsPbs PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/Hlms/Pbs;${OGRE_NEXT_INCLUDE_DIR}/Hlms/Common"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

_ogre_next_add_library(OgreNext::HlmsUnlit OgreNextHlmsUnlitStatic FALSE)
set_target_properties(OgreNext::HlmsUnlit PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/Hlms/Unlit;${OGRE_NEXT_INCLUDE_DIR}/Hlms/Common"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

# AtmosphereNpr component (sky dome + object fog + sun linkage; integrated into
# HlmsPbs). Built when OGRE_BUILD_COMPONENT_ATMOSPHERE=ON in the portfile; its
# sky material media installs under share/ogre-next/Media/Atmosphere.
_ogre_next_add_library(OgreNext::Atmosphere OgreNextAtmosphereStatic FALSE)
set_target_properties(OgreNext::Atmosphere PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/Atmosphere"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

# PlanarReflections component (mirror-of-scene reflection: a camera reflected
# across a plane renders the scene into a reflection RTT that HlmsPbs samples).
# Built when OGRE_BUILD_COMPONENT_PLANAR_REFLECTIONS=ON in the portfile; its
# HlmsPbs shader integration pieces (Pbs/Any/PlanarReflections_piece_*.any)
# ride in the Pbs Hlms media. Depends on HlmsPbs (it drives its reflection map).
_ogre_next_add_library(OgreNext::PlanarReflections OgreNextPlanarReflectionsStatic FALSE)
set_target_properties(OgreNext::PlanarReflections PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/PlanarReflections"
    INTERFACE_LINK_LIBRARIES "OgreNext::HlmsPbs;OgreNext::Main"
)

if(APPLE)
    # Metal RS: on iOS the view layer is QuartzCore (UIKit), no AppKit
    if(_ogre_next_ios)
        set(_ogre_next_metal_libs "OgreNext::Main;-framework Metal;-framework QuartzCore")
    else()
        set(_ogre_next_metal_libs "OgreNext::Main;-framework Metal;-framework AppKit;-framework QuartzCore")
    endif()
    _ogre_next_add_library(OgreNext::RenderSystem_Metal RenderSystem_MetalStatic TRUE)
    set_target_properties(OgreNext::RenderSystem_Metal PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/RenderSystems/Metal/include"
        INTERFACE_LINK_LIBRARIES "${_ogre_next_metal_libs}"
    )
else()
    # Vulkan RS. The upstream static archive carries no link interface, so
    # everything its objects reference rides here: the Vulkan loader, glslang
    # (+SPIRV) for the runtime shader compile, and - on Linux only - the
    # xcb/Xlib bridge libs of VulkanXcbWindow (system packages: libx11-xcb-dev,
    # libxcb-randr0-dev). Android uses the ANativeWindow surface (no X11/xcb).
    if(_ogre_next_android OR _ogre_next_windows)
        # ANativeWindow / Win32 window surfaces - no X11/xcb bridge libs
        set(_ogre_next_vulkan_libs "OgreNext::Main;Vulkan::Vulkan;glslang::glslang;glslang::SPIRV")
    else()
        set(_ogre_next_vulkan_libs "OgreNext::Main;Vulkan::Vulkan;glslang::glslang;glslang::SPIRV;xcb;X11-xcb;xcb-randr")
    endif()
    _ogre_next_add_library(OgreNext::RenderSystem_Vulkan RenderSystem_VulkanStatic TRUE)
    set_target_properties(OgreNext::RenderSystem_Vulkan PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/RenderSystems/Vulkan/include"
        INTERFACE_LINK_LIBRARIES "${_ogre_next_vulkan_libs}"
    )
endif()

# headless render system (no window/GPU) - handy for future headless runs
_ogre_next_add_library(OgreNext::RenderSystem_NULL RenderSystem_NULLStatic TRUE)
set_target_properties(OgreNext::RenderSystem_NULL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/RenderSystems/NULL/include"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

set(OGRE-Next_FOUND TRUE)
