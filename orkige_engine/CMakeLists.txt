# orkige_engine - the OGRE-facing layer.
# Ported module-by-module from OGRE 1.7 to 14.x; only ported modules/files are
# listed. Unported modules (engine_mygui) join as they are ported.
# engine_gocomponent/PythonScriptComponent stays out for good
# (ORKIGE_NOSCRIPT) - Lua script components return on sol2 in Phase 2.

find_package(OGRE CONFIG REQUIRED COMPONENTS Bites RTShaderSystem Overlay)
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
# vcpkg joltphysics installs a plain "Jolt" CMake package (target Jolt::Jolt)
find_package(Jolt CONFIG REQUIRED)

set(ORKIGE_ENGINE_SOURCES
    engine_module/EnginePrerequisites.h
    engine_module/module.cpp
    engine_filesystem/BigZipArchive.h
    engine_filesystem/BigZipArchive.cpp
    engine_filesystem/BigZipArchiveFactory.h
    engine_filesystem/BigZipArchiveFactory.cpp
    engine_util/StringConverter.h
    engine_util/StringUtil.h
    engine_util/ResourceUtil.h
    engine_util/ResourceUtil.cpp
    engine_util/NodeUtil.h
    engine_util/SceneNodeGuard.h
    engine_util/SceneNodeGuard.cpp
    engine_graphic/FrameEventData.h
    engine_graphic/FrameEventData.cpp
    engine_graphic/Engine.h
    engine_graphic/Engine.cpp
    engine_graphic/DynamicRenderable.h
    engine_graphic/DynamicRenderable.cpp
    engine_graphic/DynamicLines.h
    engine_graphic/DynamicLines.cpp
    engine_graphic/MovableText.h
    engine_graphic/MovableText.cpp
    engine_graphic/ColoredBoundingBox.h
    engine_graphic/ColoredBoundingBox.cpp
    engine_graphic/IngameConsole.h
    engine_graphic/IngameConsole.cpp
    engine_graphic/LightMap.h
    engine_graphic/LightMap.cpp
    engine_base/Localisation.h
    engine_base/Localisation.cpp
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
    engine_physic/CollisionTools.h
    engine_physic/CollisionTools.cpp
    engine_physic/PhysicsWorld.h
    engine_physic/PhysicsWorld.cpp
    engine_gocomponent/TransformComponent.h
    engine_gocomponent/TransformComponent.cpp
    engine_gocomponent/ModelComponent.h
    engine_gocomponent/ModelComponent.cpp
    engine_gocomponent/AnimationComponent.h
    engine_gocomponent/AnimationComponent.cpp
    engine_gocomponent/CameraComponent.h
    engine_gocomponent/CameraComponent.cpp
    engine_gocomponent/CameraDefaultModes.h
    engine_gocomponent/SoundComponent.h
    engine_gocomponent/SoundComponent.cpp
    engine_gocomponent/RigidBodyComponent.h
    engine_gocomponent/RigidBodyComponent.cpp
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
endif()

add_library(orkige_engine STATIC ${ORKIGE_ENGINE_SOURCES})
add_library(Orkige::Engine ALIAS orkige_engine)

target_include_directories(orkige_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

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

target_link_libraries(orkige_engine
    PUBLIC
        Orkige::Core
        OgreMain
        OgreOverlay
        OgreRTShaderSystem
        # static build: render systems and codecs are linked in and registered
        # via Root::installPlugin / codec startup (see Engine.cpp)
        RenderSystem_GL3Plus
        Codec_STBI
        Codec_Assimp # mesh import (glTF/glb, obj, ...); pulls in libassimp
        SDL3::SDL3
        OpenAL::OpenAL
    PRIVATE
        # backend-agnostic physics: Jolt types never leave the engine_physic
        # translation units, so the dependency stays private
        Jolt::Jolt
)

if(APPLE)
    # LoadCafData.mm decodes .caf through AudioToolbox
    target_link_libraries(orkige_engine PUBLIC
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
endif()
