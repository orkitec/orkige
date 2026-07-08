# orkige_engine - the OGRE-facing layer.
# Ported module-by-module from OGRE 1.7 to 14.x; only ported modules/files are
# listed. Unported modules (engine_mygui) join as they are ported.
# engine_gocomponent/PythonScriptComponent stays out for good; its successor
# is the sol2-based engine_gocomponent/ScriptComponent.

find_package(OGRE CONFIG REQUIRED COMPONENTS Bites RTShaderSystem Overlay)
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
# vcpkg joltphysics installs a plain "Jolt" CMake package (target Jolt::Jolt)
find_package(Jolt CONFIG REQUIRED)

set(ORKIGE_ENGINE_SOURCES
    engine_module/EnginePrerequisites.h
    engine_module/module.cpp
    # engine_render: the renderer-agnostic facade (Docs/render-abstraction.md).
    # Interface headers + compile check; the backend implementation directory
    # is appended below per ORKIGE_RENDER_BACKEND (classic today, next in A2).
    engine_render/RenderPrerequisites.h
    engine_render/RenderMath.h
    engine_render/RenderSystem.h
    engine_render/RenderWorld.h
    engine_render/RenderNode.h
    engine_render/MeshInstance.h
    engine_render/SpriteQuad.h
    engine_render/RenderCamera.h
    engine_render/RenderLight.h
    engine_render/RenderTexture.h
    engine_render/RenderFacadeCheck.cpp
    engine_filesystem/BigZipArchive.h
    engine_filesystem/BigZipArchive.cpp
    engine_filesystem/BigZipArchiveFactory.h
    engine_filesystem/BigZipArchiveFactory.cpp
    engine_util/StringConverter.h
    engine_util/StringUtil.h
    engine_util/ResourceUtil.h
    engine_util/ResourceUtil.cpp
    engine_util/NodeUtil.h
    engine_util/PrimitiveUtil.h
    engine_util/SceneNodeGuard.h
    engine_util/SceneNodeGuard.cpp
    engine_graphic/FrameEventData.h
    engine_graphic/FrameEventData.cpp
    engine_graphic/Engine.h
    engine_graphic/Engine.cpp
    # ColoredBoundingBox, LightMap and MovableText were unbuilt AND deleted in
    # phase A1 (zero callers - decided question #6 in Docs/render-abstraction.md;
    # recoverable from git). DynamicRenderable/DynamicLines stay compiled: the
    # editor-era debug-line drawing is expected back, but they are now
    # caller-less too - candidates for the same treatment if A2 says so.
    engine_graphic/DynamicRenderable.h
    engine_graphic/DynamicRenderable.cpp
    engine_graphic/DynamicLines.h
    engine_graphic/DynamicLines.cpp
    engine_graphic/IngameConsole.h
    engine_graphic/IngameConsole.cpp
    engine_base/Localisation.h
    engine_base/Localisation.cpp
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
    engine_physic/PhysicsWorld.h
    engine_physic/PhysicsWorld.cpp
    engine_gocomponent/TransformComponent.h
    engine_gocomponent/TransformComponent.cpp
    engine_gocomponent/ModelComponent.h
    engine_gocomponent/ModelComponent.cpp
    engine_gocomponent/SpriteComponent.h
    engine_gocomponent/SpriteComponent.cpp
    engine_gocomponent/AnimationComponent.h
    engine_gocomponent/AnimationComponent.cpp
    engine_gocomponent/CameraComponent.h
    engine_gocomponent/CameraComponent.cpp
    engine_gocomponent/CameraDefaultModes.h
    engine_gocomponent/SoundComponent.h
    engine_gocomponent/SoundComponent.cpp
    engine_gocomponent/RigidBodyComponent.h
    engine_gocomponent/RigidBodyComponent.cpp
    engine_gocomponent/ScriptComponent.h
    engine_gocomponent/ScriptComponent.cpp
    engine_runtime/PlayerRuntime.h
    engine_runtime/PlayerRuntime.cpp
    engine_sound/SoundPlatform.h
    engine_sound/SoundPlatform.cpp
    engine_sound/SoundError.h
    engine_sound/SoundError.cpp
    engine_sound/SoundSource.h
    engine_sound/SoundSource.cpp
    engine_sound/SoundManager.h
    engine_sound/SoundManager.cpp
    engine_sound/LoadWavData.cpp
    engine_fastgui/Gorilla.h
    engine_fastgui/Gorilla.cpp
    engine_fastgui/IGuiObject.h
    engine_fastgui/IGuiObject.cpp
    engine_fastgui/DragEventData.h
    engine_fastgui/DragEventData.cpp
    engine_fastgui/FastGuiWidget.h
    engine_fastgui/FastGuiWidget.cpp
    engine_fastgui/FastGuiView.h
    engine_fastgui/FastGuiView.cpp
    engine_fastgui/FastGuiTextbox.h
    engine_fastgui/FastGuiTextbox.cpp
    engine_fastgui/FastGuiSlider.h
    engine_fastgui/FastGuiSlider.cpp
    engine_fastgui/FastGuiSelectMenu.h
    engine_fastgui/FastGuiSelectMenu.cpp
    engine_fastgui/FastGuiProgressBar.h
    engine_fastgui/FastGuiProgressBar.cpp
    engine_fastgui/FastGuiManager.h
    engine_fastgui/FastGuiManager.cpp
    engine_fastgui/FastGuiLabel.h
    engine_fastgui/FastGuiLabel.cpp
    engine_fastgui/FastGuiFactory.h
    engine_fastgui/FastGuiFactory.cpp
    engine_fastgui/FastGuiDragDropButton.h
    engine_fastgui/FastGuiDragDropButton.cpp
    engine_fastgui/FastGuiDecorWidget.h
    engine_fastgui/FastGuiDecorWidget.cpp
    engine_fastgui/FastGuiCheckBox.h
    engine_fastgui/FastGuiCheckBox.cpp
    engine_fastgui/FastGuiButtonBlink.h
    engine_fastgui/FastGuiButtonBlink.cpp
    engine_fastgui/FastGuiButton.h
    engine_fastgui/FastGuiButton.cpp
)

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
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # SDL window -> ANativeWindow handle bridge (the Android counterpart of
    # engine_util/SDLNativeWindow.mm below)
    list(APPEND ORKIGE_ENGINE_SOURCES engine_util/SDLNativeWindowAndroid.cpp)
endif()

add_library(orkige_engine STATIC ${ORKIGE_ENGINE_SOURCES})
add_library(Orkige::Engine ALIAS orkige_engine)

target_include_directories(orkige_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# PCH: every engine TU compiles against OGRE (and, with the Lua backend,
# sol2 via the core Meta macros) - precompile both. CXX-only guard: the
# target has ObjC++ sources on Apple platforms, and on iOS Engine.cpp itself
# is compiled as OBJCXX (see below).
target_precompile_headers(orkige_engine PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:<Ogre.h$<ANGLE-R>>"
)
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

# The whole 2026 target set (GL3+/Metal/Vulkan/GLES3) is shader-based; the
# fixed-function fallbacks from the 1.7 era are gone for good.
# ORKIGE_OPENAL_SOUND: engine_sound compiles against OpenAL Soft (vcpkg
# openal-soft) on every platform; without the define the sound classes
# degrade to inert stubs (the historical no-sound configuration).
# ORKIGE_ENGINE_HAS_GOCOMPONENT: engine_gocomponent is ported; enables the
# TransformComponent/ModelComponent code paths guarded in engine_util/NodeUtil.h
# and engine_base/Localisation.cpp.
target_compile_definitions(orkige_engine PUBLIC
    USE_RTSHADER_SYSTEM
    ORKIGE_OPENAL_SOUND
    ORKIGE_ENGINE_HAS_GOCOMPONENT
)

# Render backend flavor (see ORKIGE_RENDER_BACKEND in the root CMakeLists):
# exactly one ORKIGE_RENDER_* define is set per build - engine_render's
# compile check enforces that invariant.
if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    target_compile_definitions(orkige_engine PUBLIC ORKIGE_RENDER_CLASSIC)
elseif(ORKIGE_RENDER_BACKEND STREQUAL "next")
    target_compile_definitions(orkige_engine PUBLIC ORKIGE_RENDER_NEXT)
endif()

# The backend implementation of the engine_render facade: exactly one impl
# directory compiles per build (the A2 'next' flavor swaps in
# engine_render_next/ here; the root CMakeLists FATAL_ERRORs on 'next' until
# that backend exists).
if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    target_sources(orkige_engine PRIVATE
        engine_render_classic/ClassicBackend.h
        engine_render_classic/ClassicBackend.cpp
        engine_render_classic/RenderSystemClassic.cpp
        engine_render_classic/RenderWorldClassic.cpp
        engine_render_classic/RenderNodeClassic.cpp
        engine_render_classic/MeshInstanceClassic.cpp
        engine_render_classic/SpriteQuadClassic.cpp
        engine_render_classic/RenderCameraClassic.cpp
        engine_render_classic/RenderLightClassic.cpp
        engine_render_classic/RenderTextureClassic.cpp
    )
endif()

target_link_libraries(orkige_engine
    PUBLIC
        Orkige::Core
        OgreMain
        OgreOverlay
        OgreRTShaderSystem
        Codec_STBI
        Codec_Assimp # mesh import (glTF/glb, obj, ...); pulls in libassimp
        SDL3::SDL3
        OpenAL::OpenAL
    PRIVATE
        # backend-agnostic physics: Jolt types never leave the engine_physic
        # translation units, so the dependency stays private
        Jolt::Jolt
)

# static build: render systems and codecs are linked in and registered via
# Root::installPlugin / codec startup (see Engine.cpp). Desktop default is
# GL3Plus; iOS and Android use the GLES2 render system (OGRE forces it on for
# APPLE_IOS/ANDROID, and it is the only RS with a full RTSS path there -
# Metal has none yet, Vulkan-on-Android is a later phase).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
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
