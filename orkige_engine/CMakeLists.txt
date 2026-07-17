# orkige_engine - the OGRE-facing layer.
# Ported module-by-module from OGRE 1.7 to 14.x; only ported modules/files are
# listed.
#
# ---------------------------------------------------------------------------
# Render flavors (ORKIGE_RENDER_BACKEND, Docs/render-abstraction.md):
# the SHARED source list below is backend-neutral - the modules
# talk to the renderer through engine_render/ and include the neutral
# engine_module/EnginePrerequisites.h umbrella. gui is in the shared
# list too: its UiRenderer draws through the facade's
# 2D layer and the widgets were always renderer-blind math. Per flavor this
# target adds:
#   classic: the classic Engine bootstrapper + backend
#            (engine_render_classic/), the BigZip Ogre::Archive plumbing,
#            the overlay console and debug renderables -
#            everything on the classic umbrella
#            (engine_module/EnginePrerequisitesClassic.h).
#   next:    the Ogre-Next backend (engine_render_next/) + the flavor's
#            Engine sibling (engine_graphic/EngineNext.*). No BigZip (the
#            facade's LT_ZIP/LT_BIGZIP stay honest notImplementedOnce
#            gaps).
# ---------------------------------------------------------------------------

# --- dependencies shared by both flavors ------------------------------------
find_package(SDL3 CONFIG REQUIRED)
if(EMSCRIPTEN)
    # the browser runtime provides OpenAL itself: the AL/ALC headers live in
    # the Emscripten sysroot and the implementation is a JavaScript library
    # the linker resolves (WebAudio-backed) - openal-soft is deliberately
    # absent from the wasm dependency set (vcpkg.json platform qualifier).
    # An empty imported target keeps the engine's link line uniform.
    if(NOT TARGET OpenAL::OpenAL)
        add_library(OpenAL::OpenAL INTERFACE IMPORTED)
    endif()
else()
    find_package(OpenAL CONFIG REQUIRED)
endif()
# stb: header/single-file only - ships stb_vorbis.c (the OGG Vorbis decoder
# engine_sound/StbVorbisImpl.cpp compiles) + stb_truetype.h (the glyph
# rasteriser engine_gui/FontBakeImpl.cpp compiles). Provides Stb_INCLUDE_DIR.
find_package(Stb REQUIRED)
# nanosvg: the vector-sprite rasteriser the gui runtime atlas bakes .svg
# sprites with. The vcpkg port precompiles the implementation into static libs
# (NanoSVG::nanosvg / NanoSVG::nanosvgrast); engine_gui/SvgRasterImpl.cpp is
# the only TU that includes its headers (Docs/ports.md).
find_package(NanoSVG CONFIG REQUIRED)
# vcpkg joltphysics installs a plain "Jolt" CMake package (target Jolt::Jolt)
find_package(Jolt CONFIG REQUIRED)

if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    # the overlay port ships this config (share/ogre-next); targets are
    # namespaced OgreNext::* so nothing collides with classic's OgreMain
    find_package(OGRE-Next CONFIG REQUIRED)
else()
    find_package(OGRE CONFIG REQUIRED COMPONENTS Bites RTShaderSystem Overlay)
endif()
# assimp backs the backend-neutral skinned-rig extraction
# (engine_render/SkinnedRigExtract.cpp) on BOTH flavors, and Ogre-Next's own
# mesh import path (engine_render_next/MeshLoaderNext.cpp - Ogre-Next has no
# assimp codec); classic already carries the library via ogre's assimp feature
find_package(assimp CONFIG REQUIRED)

# --- backend-neutral sources (both flavors) ---------------------------------
set(ORKIGE_ENGINE_SOURCES
    engine_module/EnginePrerequisites.h
    engine_module/module.cpp
    # engine_render: the renderer-agnostic facade (Docs/render-abstraction.md).
    # Interface headers + compile check; the backend implementation directory
    # is appended below per ORKIGE_RENDER_BACKEND.
    engine_render/RenderPrerequisites.h
    engine_render/RenderMath.h
    engine_render/RenderSystem.h
    engine_render/RenderWorld.h
    engine_render/RenderNode.h
    engine_render/MeshInstance.h
    engine_render/SpriteQuad.h
    engine_render/SpriteBatch.h
    engine_render/RenderCamera.h
    engine_render/RenderLight.h
    engine_render/RenderTexture.h
    engine_render/DrawLayer2D.h
    engine_render/DrawLayer2DClip.h
    engine_render/RenderFacadeCheck.cpp
    # the backend-neutral skinned-rig vocabulary + its assimp extraction
    # (the ONE skeleton/clip semantics both importer roads share)
    engine_render/SkinnedRig.h
    engine_render/SkinnedRig.cpp
    engine_render/SkinnedRigExtract.h
    engine_render/SkinnedRigExtract.cpp
    engine_util/StringConverter.h
    engine_util/StringUtil.h
    engine_util/ConfigFileUtil.h
    engine_util/ResourceUtil.h
    engine_util/ResourceUtil.cpp
    engine_util/NodeUtil.h
    engine_util/FrameStatsUtil.h
    engine_util/PlatformWindow.h
    engine_util/PlatformWindow.cpp
    engine_util/SceneNodeGuard.h
    engine_util/SceneNodeGuard.cpp
    # Engine.h dispatches per flavor: the classic bootstrapper below or the
    # facade-only EngineNext sibling - apps/scripts see ONE Engine surface
    engine_graphic/Engine.h
    engine_graphic/FrameEventData.h
    engine_graphic/FrameEventData.cpp
    engine_graphic/ScreenFade.h
    engine_graphic/ScreenFade.cpp
    engine_graphic/ScreenShake.h
    engine_graphic/ScreenShake.cpp
    engine_base/EngineLog.h
    engine_base/EngineLog.cpp
    engine_input/KeyEventData.h
    engine_input/KeyEventData.cpp
    engine_input/MouseEventData.h
    engine_input/MouseEventData.cpp
    engine_input/TouchEventData.h
    engine_input/TouchEventData.cpp
    engine_input/GestureEventData.h
    engine_input/GestureEventData.cpp
    engine_input/AccelerationEventData.h
    engine_input/AccelerationEventData.cpp
    engine_input/InputManager.h
    engine_input/InputManager.cpp
    engine_input/HapticManager.h
    engine_input/HapticManager.cpp
    engine_input/InputAction.h
    engine_input/InputActionMap.h
    engine_input/InputActionMap.cpp
    engine_physic/PhysicsWorld.h
    engine_physic/PhysicsWorld.cpp
    engine_gocomponent/TransformComponent.h
    engine_gocomponent/TransformComponent.cpp
    engine_gocomponent/ModelComponent.h
    engine_gocomponent/ModelComponent.cpp
    engine_gocomponent/WaterComponent.h
    engine_gocomponent/WaterComponent.cpp
    engine_gocomponent/SpriteComponent.h
    engine_gocomponent/SpriteComponent.cpp
    engine_gocomponent/SpriteBatcher.h
    engine_gocomponent/SpriteBatcher.cpp
    engine_gocomponent/VectorShapeComponent.h
    engine_gocomponent/VectorShapeComponent.cpp
    engine_gocomponent/VectorAnimationComponent.h
    engine_gocomponent/VectorAnimationComponent.cpp
    engine_gocomponent/SpriteAnimationComponent.h
    engine_gocomponent/SpriteAnimationComponent.cpp
    engine_gocomponent/ParticleSim.h
    engine_gocomponent/ParticleComponent.h
    engine_gocomponent/ParticleComponent.cpp
    engine_gocomponent/AnimationComponent.h
    engine_gocomponent/AnimationComponent.cpp
    engine_gocomponent/CameraComponent.h
    engine_gocomponent/CameraComponent.cpp
    engine_gocomponent/CameraDefaultModes.h
    engine_gocomponent/LightComponent.h
    engine_gocomponent/LightComponent.cpp
    engine_gocomponent/SoundComponent.h
    engine_gocomponent/SoundComponent.cpp
    engine_gocomponent/RigidBodyComponent.h
    engine_gocomponent/RigidBodyComponent.cpp
    engine_gocomponent/ScriptComponent.h
    engine_gocomponent/ScriptComponent.cpp
    engine_gocomponent/ScriptComponentRegistry.h
    engine_gocomponent/ScriptComponentRegistry.cpp
    engine_runtime/PlayerRuntime.h
    engine_runtime/PlayerRuntime.cpp
    # the shared boot/teardown spine of every app host (samples, player,
    # editor, native game modules); hosts keep their own frame loops
    engine_runtime/AppHost.h
    engine_runtime/AppHost.cpp
    engine_sound/SoundPlatform.h
    engine_sound/SoundPlatform.cpp
    engine_sound/SoundError.h
    engine_sound/SoundError.cpp
    engine_sound/SoundSource.h
    engine_sound/SoundSource.cpp
    engine_sound/SoundManager.h
    engine_sound/SoundManager.cpp
    engine_sound/LoadWavData.cpp
    # streamed music: the queued-buffer streamer + the ONE TU that compiles the
    # single-file OGG Vorbis decoder (StbVorbisImpl.cpp - nothing else includes
    # it, so the decoder never touches the umbrella or the PCH)
    engine_sound/MusicStream.h
    engine_sound/MusicStream.cpp
    engine_sound/StbVorbisImpl.cpp
    # gui: flavor-neutral (the UiRenderer
    # submits to engine_render/DrawLayer2D; atlas parsing/glyph metrics/
    # widget math were always renderer-blind). Gorilla is DELETED - the
    # .ogui atlas lives in UiAtlas, the 2D renderer in UiRenderer.
    engine_gui/UiAtlas.h
    engine_gui/UiAtlas.cpp
    # runtime font/vector-sprite baking: the pure shelf packer, the single-
    # file-lib seams (stb_truetype / nanosvg confined to their *Impl.cpp like
    # StbVorbisImpl.cpp) and the mutable baked atlas built on them
    engine_gui/FontPacker.h
    engine_gui/FontPacker.cpp
    engine_gui/FontBake.h
    engine_gui/FontBakeImpl.cpp
    engine_gui/SvgRaster.h
    engine_gui/SvgRasterImpl.cpp
    engine_gui/FontAtlas.h
    engine_gui/FontAtlas.cpp
    engine_gui/UiRenderer.h
    engine_gui/UiRenderer.cpp
    engine_gui/IGuiObject.h
    engine_gui/IGuiObject.cpp
    engine_gui/DragEventData.h
    engine_gui/DragEventData.cpp
    engine_gui/GuiWidget.h
    engine_gui/GuiWidget.cpp
    engine_gui/GuiView.h
    engine_gui/GuiView.cpp
    engine_gui/GuiTextbox.h
    engine_gui/GuiTextbox.cpp
    engine_gui/GuiTextEntry.h
    engine_gui/GuiTextEntry.cpp
    engine_gui/GuiTextEdit.h
    engine_gui/GuiSlider.h
    engine_gui/GuiSlider.cpp
    engine_gui/GuiSelectMenu.h
    engine_gui/GuiSelectMenu.cpp
    engine_gui/GuiProgressBar.h
    engine_gui/GuiProgressBar.cpp
    engine_gui/GuiManager.h
    engine_gui/GuiManager.cpp
    engine_gui/GuiLabel.h
    engine_gui/GuiLabel.cpp
    engine_gui/GuiFactory.h
    engine_gui/GuiFactory.cpp
    engine_gui/GuiDragDropButton.h
    engine_gui/GuiDragDropButton.cpp
    engine_gui/GuiDecorWidget.h
    engine_gui/GuiDecorWidget.cpp
    engine_gui/GuiScrollView.h
    engine_gui/GuiScrollView.cpp
    engine_gui/GuiLayout.h
    engine_gui/GuiLayout.cpp
    engine_gui/GuiCheckBox.h
    engine_gui/GuiCheckBox.cpp
    engine_gui/GuiButtonBlink.h
    engine_gui/GuiButtonBlink.cpp
    engine_gui/GuiButton.h
    engine_gui/GuiButton.cpp
    engine_gui/GuiModalScrim.h
    engine_gui/GuiModalScrim.cpp
    engine_gui/GuiToggleGroup.h
    engine_gui/GuiToggleGroup.cpp
    engine_gui/GuiToast.h
    engine_gui/GuiToast.cpp
    engine_gui/GuiDropDown.h
    engine_gui/GuiDropDown.cpp
)

# --- classic-only sources (the classic umbrella zone) -----------------------
if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    list(APPEND ORKIGE_ENGINE_SOURCES
        engine_module/EnginePrerequisitesClassic.h
        engine_filesystem/BigZipArchive.h
        engine_filesystem/BigZipArchive.cpp
        engine_filesystem/BigZipArchiveFactory.h
        engine_filesystem/BigZipArchiveFactory.cpp
        engine_util/PrimitiveUtil.h
        engine_graphic/Engine.cpp
        # ColoredBoundingBox, LightMap and MovableText were unbuilt AND deleted
        # (zero callers - see Docs/render-abstraction.md;
        # recoverable from git). DynamicRenderable/DynamicLines stay compiled: the
        # editor-era debug-line drawing is expected back, but they are now
        # caller-less too - candidates for the same treatment later.
        engine_graphic/DynamicRenderable.h
        engine_graphic/DynamicRenderable.cpp
        engine_graphic/DynamicLines.h
        engine_graphic/DynamicLines.cpp
        engine_graphic/IngameConsole.h
        engine_graphic/IngameConsole.cpp
        # the classic backend implementing the engine_render facade
        engine_render_classic/ClassicBackend.h
        engine_render_classic/ClassicBackend.cpp
        engine_render_classic/RenderSystemClassic.cpp
        engine_render_classic/RenderWorldClassic.cpp
        engine_render_classic/RenderNodeClassic.cpp
        engine_render_classic/MeshInstanceClassic.cpp
        engine_render_classic/StaticBakeClassic.cpp
        engine_render_classic/SpriteQuadClassic.cpp
        engine_render_classic/SpriteBatchClassic.cpp
        engine_render_classic/VectorMeshClassic.cpp
        engine_render_classic/RenderCameraClassic.cpp
        engine_render_classic/RenderLightClassic.cpp
        engine_render_classic/RenderTextureClassic.cpp
        engine_render_classic/DrawLayer2DClassic.cpp
    )
endif()

# --- next-only sources (the Ogre-Next backend + its Engine sibling) ---------
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    list(APPEND ORKIGE_ENGINE_SOURCES
        engine_graphic/EngineNext.h
        engine_graphic/EngineNext.cpp
        engine_render_next/NextBackend.h
        engine_render_next/NextBackend.cpp
        engine_render_next/RenderSystemNext.cpp
        engine_render_next/RenderWorldNext.cpp
        engine_render_next/RenderNodeNext.cpp
        engine_render_next/MeshInstanceNext.cpp
        engine_render_next/MeshLoaderNext.cpp
        engine_render_next/SpriteQuadNext.cpp
        engine_render_next/SpriteBatchNext.cpp
        engine_render_next/VectorMeshNext.cpp
        engine_render_next/RenderCameraNext.cpp
        engine_render_next/RenderLightNext.cpp
        engine_render_next/RenderTextureNext.cpp
        engine_render_next/DrawLayer2DNext.cpp
    )
endif()

if(APPLE)
    # AudioToolbox-backed .caf decoding (Apple platforms only)
    list(APPEND ORKIGE_ENGINE_SOURCES engine_sound/LoadCafData.mm)
    # SDL window -> NSWindow handle bridge for the SDL-hosted apps
    list(APPEND ORKIGE_ENGINE_SOURCES engine_util/SDLNativeWindow.mm)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Engine.cpp's ORKIGE_IPHONE blocks (iPhone/iPad config selection) use
    # Objective-C UIKit calls - the file was always ObjC++ on iOS
    set_source_files_properties(engine_graphic/Engine.cpp
        PROPERTIES LANGUAGE OBJCXX)
    # the taptic-engine bridge behind HapticManager (UIFeedbackGenerator, UIKit);
    # iOS-only - macOS has no phone vibrator and takes the desktop no-op path.
    # UIKit resolves at the final app link (SDL's UIKit main already pulls it).
    list(APPEND ORKIGE_ENGINE_SOURCES engine_input/HapticBridgeApple.mm)
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        # iOS attaches the Ogre-Next Metal view into SDL's UIKit window via a
        # small ObjC++ bridge (the render-system-created view is otherwise
        # detached - see engine_render_next/NextBackend.cpp)
        list(APPEND ORKIGE_ENGINE_SOURCES engine_util/OgreMetalViewBridge.mm)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # SDL window -> ANativeWindow handle bridge (the Android counterpart of
    # engine_util/SDLNativeWindow.mm below)
    list(APPEND ORKIGE_ENGINE_SOURCES engine_util/SDLNativeWindowAndroid.cpp)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # SDL window -> X11 handle bridge (the Linux desktop counterpart of
    # engine_util/SDLNativeWindow.mm; X11/XWayland only - Wayland is a
    # TODO(linux) inside the TU). Flavor-aware: classic hands out the X11
    # Window id, next the {Display*, Window} pair VulkanXcbWindow expects.
    list(APPEND ORKIGE_ENGINE_SOURCES engine_util/SDLNativeWindowLinux.cpp)
elseif(WIN32)
    # the Win32 HWND bridge (same extern-C contract as the other platforms,
    # engine_util/SDLNativeWindowWindows.cpp)
    list(APPEND ORKIGE_ENGINE_SOURCES engine_util/SDLNativeWindowWindows.cpp)
elseif(EMSCRIPTEN)
    # the browser bridge: no native handle exists - the page's one canvas is
    # both SDL's window and the GLES2 render surface (the bridge returns
    # null, the engine boots its own render window on the canvas)
    list(APPEND ORKIGE_ENGINE_SOURCES engine_util/SDLNativeWindowWeb.cpp)
endif()

add_library(orkige_engine STATIC ${ORKIGE_ENGINE_SOURCES})
add_library(Orkige::Engine ALIAS orkige_engine)

target_include_directories(orkige_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# PCH: every engine TU compiles against Ogre math (and, with the Lua backend,
# sol2 via the core Meta macros). The classic flavor precompiles the whole
# <Ogre.h> umbrella (its backend + gui zone includes it everywhere); the
# next flavor precompiles only sol2 - its TUs include exactly what they need.
# CXX-only guard: the target has ObjC++ sources on Apple platforms, and on
# iOS Engine.cpp itself is compiled as OBJCXX (see above).
if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    target_precompile_headers(orkige_engine PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<Ogre.h$<ANGLE-R>>"
    )
endif()
if(ORKIGE_SCRIPTING STREQUAL "LUA")
    target_precompile_headers(orkige_engine PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<sol/sol.hpp$<ANGLE-R>>"
    )
endif()
orkige_pch_ccache_compat(orkige_engine)

# The facade compile check must prove the engine_render headers are
# self-contained (no Ogre.h umbrella) - the target PCH would mask that.
set_source_files_properties(engine_render/RenderFacadeCheck.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# the single-file OGG Vorbis decoder is compiled in exactly one TU: give ONLY
# that TU the stb include and keep it off the PCH (the decoder stays out of the
# sol2/Ogre precompiled header). Nothing else sees the stb headers.
set_source_files_properties(engine_sound/StbVorbisImpl.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${Stb_INCLUDE_DIR}")

# the single-file TrueType rasteriser (stb_truetype.h) is likewise compiled in
# exactly one TU: only it sees the stb include, and it stays off the PCH.
set_source_files_properties(engine_gui/FontBakeImpl.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${Stb_INCLUDE_DIR}")

# nanosvg is confined to SvgRasterImpl.cpp; keep it off the PCH so its headers
# never join the sol2/Ogre precompiled header (the NanoSVG:: link targets carry
# the include dir).
set_source_files_properties(engine_gui/SvgRasterImpl.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON)

# The whole 2026 target set (GL3+/Metal/Vulkan/GLES3) is shader-based; the
# fixed-function fallbacks from the 1.7 era are gone for good.
# ORKIGE_OPENAL_SOUND: engine_sound compiles against OpenAL Soft (vcpkg
# openal-soft) on every platform; without the define the sound classes
# degrade to inert stubs (the historical no-sound configuration).
# ORKIGE_ENGINE_HAS_GOCOMPONENT: engine_gocomponent is ported; enables the
# TransformComponent/ModelComponent code paths guarded in engine_util/NodeUtil.h.
target_compile_definitions(orkige_engine PUBLIC
    ORKIGE_OPENAL_SOUND
    ORKIGE_ENGINE_HAS_GOCOMPONENT
)

# Render backend flavor (see ORKIGE_RENDER_BACKEND in the root CMakeLists):
# exactly one ORKIGE_RENDER_* define is set per build - engine_render's
# compile check enforces that invariant.
if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    target_compile_definitions(orkige_engine PUBLIC
        ORKIGE_RENDER_CLASSIC
        USE_RTSHADER_SYSTEM
    )
elseif(ORKIGE_RENDER_BACKEND STREQUAL "next")
    target_compile_definitions(orkige_engine PUBLIC ORKIGE_RENDER_NEXT)
    target_compile_definitions(orkige_engine PRIVATE
        # dev-tree default for EngineNext's Hlms shader-template registration
        # (bundled apps override via Engine::setHlmsMediaDir once next-flavor
        # export exists)
        ORKIGE_NEXT_HLMS_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media"
    )
endif()

target_link_libraries(orkige_engine
    PUBLIC
        Orkige::Core
        SDL3::SDL3
        OpenAL::OpenAL
    PRIVATE
        # backend-agnostic physics: Jolt types never leave the engine_physic
        # translation units, so the dependency stays private
        Jolt::Jolt
        # vector-sprite rasteriser: nanosvg symbols never leave SvgRasterImpl.cpp
        NanoSVG::nanosvg
        NanoSVG::nanosvgrast
        # skinned-rig extraction: assimp types never leave
        # SkinnedRigExtract.cpp (plus the next backend's mesh import TU)
        assimp::assimp
)

if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    target_link_libraries(orkige_engine
        PUBLIC
            OgreNext::Main
            OgreNext::HlmsPbs
            OgreNext::HlmsUnlit
            # sky + object fog + sun-linked day/night (engine_render environment
            # surface; RenderWorld::setAtmosphere on the next flavor)
            OgreNext::Atmosphere
    )
    # exactly one Ogre-Next render system per platform (the backend boot in
    # engine_render_next/NextBackend.cpp installs the matching plugin):
    # Metal on Apple, Vulkan everywhere else (ports/ogre-next builds it with
    # the XCB windowing on Linux). TODO(linux): first real Linux run pending.
    if(APPLE)
        target_link_libraries(orkige_engine PUBLIC OgreNext::RenderSystem_Metal)
    else()
        target_link_libraries(orkige_engine PUBLIC OgreNext::RenderSystem_Vulkan)
    endif()
    # everything below is classic-backend plumbing (render system plugins,
    # codecs, Vulkan loader wiring)
    return()
endif()

target_link_libraries(orkige_engine
    PUBLIC
        OgreMain
        OgreOverlay
        OgreRTShaderSystem
        Codec_STBI
        Codec_Assimp # mesh import (glTF/glb, obj, ...); pulls in libassimp
)

# static build: render systems and codecs are linked in and registered via
# Root::installPlugin / codec startup (see Engine.cpp). Desktop default is
# GL3Plus; iOS, Android and the browser use the GLES2 render system (OGRE
# forces it on for APPLE_IOS/ANDROID/EMSCRIPTEN, and it is the only RS with a
# full RTSS path there - Metal has none yet, Vulkan-on-Android is a later
# phase; in the browser GLES2 renders through WebGL).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android"
        OR EMSCRIPTEN)
    target_link_libraries(orkige_engine PUBLIC RenderSystem_GLES2)
else()
    target_link_libraries(orkige_engine PUBLIC RenderSystem_GL3Plus)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # NDK system libs the static OGRE libs use but the vcpkg-exported
    # OgreTargets do not carry on their link interface: android/log
    # (OgreMain's Android platform code) and EGL/GLESv2 (RenderSystem_GLES2)
    target_link_libraries(orkige_engine PUBLIC android log EGL GLESv2)
    # OgrePlatformInformation calls android_getCpuFamily/Features from the
    # NDK's source-only 'cpufeatures' module; the installed static OgreMain
    # cannot carry it, so compile it here (exactly what OGRE's own Android
    # apps do). The one C source in the whole build.
    enable_language(C)
    add_library(orkige_ndk_cpufeatures STATIC
        "${CMAKE_ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c")
    target_include_directories(orkige_ndk_cpufeatures PUBLIC
        "${CMAKE_ANDROID_NDK}/sources/android/cpufeatures")
    target_link_libraries(orkige_engine PUBLIC orkige_ndk_cpufeatures)
endif()

if(APPLE)
    # LoadCafData.mm decodes .caf through AudioToolbox
    target_link_libraries(orkige_engine PUBLIC
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
    # Metal render system (selectable at runtime via
    # Engine::setPreferredRenderSystem / ORKIGE_RENDERSYSTEM; GL3Plus stays the
    # default). The RS itself needs the Metal/AppKit/QuartzCore frameworks -
    # they come through the exported OgreTargets link interface.
    target_link_libraries(orkige_engine PUBLIC RenderSystem_Metal)
endif()

# Vulkan render system - present when the ogre 'vulkan' vcpkg feature is on
# (see vcpkg.json; currently osx, later android/ios). Runtime-selectable via
# ORKIGE_RENDERSYSTEM=Vulkan; full RTSS support through the glslang plugin,
# which compiles the RTSS-generated GLSL to SPIR-V. On Apple platforms the
# driver is MoltenVK (system-tier, see CLAUDE.md hermeticity note); the
# SPIR-V is translated to MSL by MoltenVK at pipeline creation time.
if(TARGET RenderSystem_Vulkan)
    target_compile_definitions(orkige_engine PUBLIC ORKIGE_HAVE_VULKAN)
    target_link_libraries(orkige_engine PUBLIC
        RenderSystem_Vulkan
        Plugin_GLSLangProgramManager
    )
    # OGRE's bundled volk dlopens the Khronos loader ("libvulkan.dylib") at
    # runtime. Linking vcpkg's loader here (a) keeps the loader a vcpkg
    # dependency per the hermeticity rule and (b) puts its directory on the
    # executables' rpath, where dyld's leaf-name dlopen search finds it.
    if(APPLE)
        find_library(ORKIGE_VULKAN_LOADER_LIBRARY NAMES vulkan REQUIRED)
        target_link_libraries(orkige_engine PUBLIC ${ORKIGE_VULKAN_LOADER_LIBRARY})
    endif()
endif()
