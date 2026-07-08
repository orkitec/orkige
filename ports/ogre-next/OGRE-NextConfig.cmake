# OGRE-NextConfig.cmake - vcpkg-port-authored package config for ogre-next.
# Upstream installs no CMake package files (only pkg-config); this config
# provides namespaced imported targets so consumers never collide with the
# classic ogre port's exported OgreMain/RenderSystem_* target names:
#
#   OgreNext::Main                core (OgreNextMainStatic)
#   OgreNext::HlmsPbs             physically based material system
#   OgreNext::HlmsUnlit           unlit material system
#   OgreNext::RenderSystem_Metal  Metal render system (Apple; static plugin)
#   OgreNext::RenderSystem_Vulkan Vulkan render system (Linux; static plugin)
#
# Variables:
#   OGRE_NEXT_INCLUDE_DIR  include/OGRE-Next
#   OGRE_NEXT_MEDIA_DIR    share/ogre-next/Media (HLMS shader templates)

include(CMakeFindDependencyMacro)
find_dependency(ZLIB)
find_dependency(freeimage CONFIG)
if(NOT APPLE)
    # the Vulkan RS: loader+headers from the vcpkg vulkan-* ports (built-in
    # FindVulkan resolves inside the installed tree -> Vulkan::Vulkan), and
    # glslang for the runtime GLSL->SPIR-V compile the RS does (the upstream
    # static lib does not carry its link interface - the consumer must)
    find_dependency(Vulkan)
    find_dependency(glslang CONFIG)
endif()

get_filename_component(_ogre_next_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(OGRE_NEXT_INCLUDE_DIR "${_ogre_next_prefix}/include/OGRE-Next")
set(OGRE_NEXT_MEDIA_DIR "${CMAKE_CURRENT_LIST_DIR}/Media")

function(_ogre_next_add_library target libname)
    if(TARGET ${target})
        return()
    endif()
    add_library(${target} STATIC IMPORTED)
    set_target_properties(${target} PROPERTIES
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
        IMPORTED_LOCATION_RELEASE "${_ogre_next_prefix}/lib/lib${libname}.a"
        IMPORTED_LOCATION_DEBUG "${_ogre_next_prefix}/debug/lib/lib${libname}.a"
        # single-name fallback for configurations beyond Release/Debug
        IMPORTED_LOCATION "${_ogre_next_prefix}/lib/lib${libname}.a"
    )
endfunction()

if(APPLE)
    set(_ogre_next_main_platform_libs "-framework Foundation;-framework IOKit;-framework Cocoa;-framework Carbon;-framework CoreVideo")
else()
    # Linux: OgreMain's threading/plugin loading + X11 window-event plumbing
    set(_ogre_next_main_platform_libs "X11;pthread;dl")
endif()

_ogre_next_add_library(OgreNext::Main OgreNextMainStatic)
set_target_properties(OgreNext::Main PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}"
    # the port builds with OGRE_EMBED_DEBUG_MODE=never: OGRE_DEBUG_MODE is
    # derived from _DEBUG/DEBUG/NDEBUG in OgrePlatform.h. The debug libs
    # were compiled with DEBUG=1 (upstream injects it into
    # CMAKE_CXX_FLAGS_DEBUG), so debug consumers MUST see the same macros -
    # OGRE_DEBUG_MODE is ABI-relevant in Ogre-Next (debug bookkeeping in
    # the v2 memory managers changes struct layouts)
    INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:DEBUG=1;_DEBUG=1>"
    INTERFACE_LINK_LIBRARIES "ZLIB::ZLIB;freeimage::FreeImage;${_ogre_next_main_platform_libs}"
)

_ogre_next_add_library(OgreNext::HlmsPbs OgreNextHlmsPbsStatic)
set_target_properties(OgreNext::HlmsPbs PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/Hlms/Pbs;${OGRE_NEXT_INCLUDE_DIR}/Hlms/Common"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

_ogre_next_add_library(OgreNext::HlmsUnlit OgreNextHlmsUnlitStatic)
set_target_properties(OgreNext::HlmsUnlit PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/Hlms/Unlit;${OGRE_NEXT_INCLUDE_DIR}/Hlms/Common"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

if(APPLE)
    _ogre_next_add_library(OgreNext::RenderSystem_Metal RenderSystem_MetalStatic)
    set_target_properties(OgreNext::RenderSystem_Metal PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/RenderSystems/Metal/include"
        INTERFACE_LINK_LIBRARIES "OgreNext::Main;-framework Metal;-framework AppKit;-framework QuartzCore"
    )
else()
    # Linux: the Vulkan RS with XCB windowing. The upstream static archive
    # carries no link interface, so everything its objects reference rides
    # here: the Vulkan loader, glslang(+SPIRV) for the runtime shader
    # compile, and the xcb/Xlib bridge libs of VulkanXcbWindow (system
    # packages: libx11-xcb-dev, libxcb-randr0-dev).
    _ogre_next_add_library(OgreNext::RenderSystem_Vulkan RenderSystem_VulkanStatic)
    set_target_properties(OgreNext::RenderSystem_Vulkan PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/RenderSystems/Vulkan/include"
        INTERFACE_LINK_LIBRARIES "OgreNext::Main;Vulkan::Vulkan;glslang::glslang;glslang::SPIRV;xcb;X11-xcb;xcb-randr"
    )
endif()

# headless render system (no window/GPU) - handy for future headless runs
_ogre_next_add_library(OgreNext::RenderSystem_NULL RenderSystem_NULLStatic)
set_target_properties(OgreNext::RenderSystem_NULL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OGRE_NEXT_INCLUDE_DIR}/RenderSystems/NULL/include"
    INTERFACE_LINK_LIBRARIES "OgreNext::Main"
)

set(OGRE-Next_FOUND TRUE)
