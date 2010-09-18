# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

# ---------------------------------------------------------

set(engine_gocomponent_SOURCE
    # ----- Source -----
    engine_gocomponent/AnimationComponent.cpp
    engine_gocomponent/CameraComponent.cpp
    engine_gocomponent/ModelComponent.cpp
    engine_gocomponent/PythonScriptComponent.cpp
    engine_gocomponent/SoundComponent.cpp
    engine_gocomponent/TransformComponent.cpp
)

set(engine_gocomponent_HEADER
    # ----- Headers -----
    engine_gocomponent/AnimationComponent.h
    engine_gocomponent/CameraComponent.h
    engine_gocomponent/CameraDefaultModes.h
    engine_gocomponent/ModelComponent.h
    engine_gocomponent/PythonScriptComponent.h
    engine_gocomponent/SoundComponent.h
    engine_gocomponent/TransformComponent.h
)

source_group(engine_gocomponent FILES ${engine_gocomponent_HEADER} ${engine_gocomponent_SOURCE})

# ---------------------------------------------------------

set(engine_graphic_SOURCE
    # ----- Source -----
    engine_graphic/ColoredBoundingBox.cpp
    engine_graphic/DynamicLines.cpp
    engine_graphic/DynamicRenderable.cpp
    engine_graphic/Engine.cpp
    engine_graphic/FrameEventData.cpp
    engine_graphic/LightMap.cpp
    engine_graphic/MovableText.cpp
)

set(engine_graphic_HEADER
    # ----- Headers -----
    engine_graphic/ColoredBoundingBox.h
    engine_graphic/DynamicLines.h
    engine_graphic/DynamicRenderable.h
    engine_graphic/Engine.h
    engine_graphic/FrameEventData.h
    engine_graphic/LightMap.h
    engine_graphic/MovableText.h
)

source_group(engine_graphic FILES ${engine_graphic_HEADER} ${engine_graphic_SOURCE})

# ---------------------------------------------------------

set(engine_gui_SOURCE
    # ----- Source -----
    engine_gui/Button.cpp
    engine_gui/CheckBox.cpp
    engine_gui/DecorWidget.cpp
    engine_gui/Dialog.cpp
    engine_gui/GuiFactory.cpp
    engine_gui/GuiManager.cpp
    engine_gui/IGuiObject.cpp
    engine_gui/IngameConsole.cpp
    engine_gui/Label.cpp
    engine_gui/OkDialog.cpp
    engine_gui/ParamsPanel.cpp
    engine_gui/ProgressBar.cpp
    engine_gui/SelectMenu.cpp
    engine_gui/Separator.cpp
    engine_gui/Slider.cpp
    engine_gui/TextBox.cpp
    engine_gui/Widget.cpp
    engine_gui/YesNoDialog.cpp
)

set(engine_gui_HEADER
    # ----- Headers -----
    engine_gui/Button.h
    engine_gui/CheckBox.h
    engine_gui/DecorWidget.h
    engine_gui/Dialog.h
    engine_gui/GuiFactory.h
    engine_gui/GuiManager.h
    engine_gui/IGuiObject.h
    engine_gui/IngameConsole.h
    engine_gui/Label.h
    engine_gui/OkDialog.h
    engine_gui/ParamsPanel.h
    engine_gui/ProgressBar.h
    engine_gui/SelectMenu.h
    engine_gui/Separator.h
    engine_gui/Slider.h
    engine_gui/TextBox.h
    engine_gui/Widget.h
    engine_gui/YesNoDialog.h
)

source_group(engine_gui FILES ${engine_gui_HEADER} ${engine_gui_SOURCE})

# ---------------------------------------------------------

set(engine_input_SOURCE
    # ----- Source -----
    engine_input/InputManager.cpp
    engine_input/KeyEventData.cpp
    engine_input/MouseEventData.cpp
)

set(engine_input_HEADER
    # ----- Headers -----
    engine_input/InputManager.h
    engine_input/KeyEventData.h
    engine_input/MouseEventData.h
)

source_group(engine_input FILES ${engine_input_HEADER} ${engine_input_SOURCE})

# ---------------------------------------------------------

set(engine_module_SOURCE
    # ----- Source -----
    engine_module/module.cpp
)

set(engine_module_HEADER
    # ----- Headers -----
    engine_module/EnginePrerequisites.h
)

source_group(engine_module FILES ${engine_module_HEADER} ${engine_module_SOURCE})

# ---------------------------------------------------------

set(engine_physic_SOURCE
    # ----- Source -----
    engine_physic/CollisionTools.cpp
)

set(engine_physic_HEADER
    # ----- Headers -----
    engine_physic/CollisionTools.h
)

source_group(engine_physic FILES ${engine_physic_HEADER} ${engine_physic_SOURCE})

# ---------------------------------------------------------

set(engine_sound_SOURCE
    # ----- Source -----
    engine_sound/LoadWavData.cpp
    engine_sound/SoundError.cpp
    engine_sound/SoundManager.cpp
    engine_sound/SoundPlatform.cpp
    engine_sound/SoundSource.cpp
    engine_sound/LoadCafData.mm
)

set(engine_sound_HEADER
    # ----- Headers -----
    engine_sound/SoundError.h
    engine_sound/SoundManager.h
    engine_sound/SoundPlatform.h
    engine_sound/SoundSource.h
)

source_group(engine_sound FILES ${engine_sound_HEADER} ${engine_sound_SOURCE})

# ---------------------------------------------------------

set(engine_util_SOURCE
    # ----- Source -----
    engine_util/ResourceUtil.cpp
    engine_util/SerializationUtil.cpp
)

set(engine_util_HEADER
    # ----- Headers -----
    engine_util/CameraUtil.h
    engine_util/CImg.h
    engine_util/MeshUtil.h
    engine_util/NodeUtil.h
    engine_util/OverlayUtil.h
    engine_util/ResourceUtil.h
    engine_util/SerializationUtil.h
    engine_util/StringConverter.h
    engine_util/StringUtil.h
)

source_group(engine_util FILES ${engine_util_HEADER} ${engine_util_SOURCE})

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------

if (APPLE)
  include_directories( ${OGRE_INCLUDE_DIR}/OgreMain/include/OSX )
endif ()


if (ORKIGE_OPENAL_SOUND AND OPENAL_FOUND)
    add_definitions(-DORKIGE_OPENAL_SOUND)
endif()

if (ORKIGE_BUILD_IPHONE)
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_input/InputManager.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

include_directories()

include_directories(
    ${ORKIGE_INCLUDE}
    ${ORKIGE_OGRE_INCLUDE}
    ${OGRE_INCLUDE_DIR}/PlugIns/ParticleFX/include
    ${ORKIGE_OIS_INCLUDE}
    ${ORKIGE_ZLIB_INCLUDE}
    ${ORKIGE_LUA_INCLUDE}
    ${ORKIGE_OPENAL_INCLUDE}
    ./
    )

link_libraries(
    ${OGRE_LIBDIR}
    ${ORKIGE_OGRE_LIBS}
    ${ORKIGE_CORE_LIB}
    Plugin_ParticleFX
    OIS
    )

configure_rendersystem()

set(HiddenDepenCMakeLists ../Dependencies/CMakeLists.txt)
source_group(DependenciesCMakeLists FILES ${HiddenDepenCMakeLists})


# for swig no __declspec(dllexport)
add_definitions(-DSTATIC_LINKED)


add_library(orkige_engine STATIC
            ${engine_gocomponent_SOURCE}  ${engine_gocomponent_HEADER}
            ${engine_graphic_SOURCE}      ${engine_graphic_HEADER}
            ${engine_gui_SOURCE}          ${engine_gui_HEADER}
            ${engine_input_SOURCE}        ${engine_input_HEADER}
            ${engine_module_SOURCE}       ${engine_module_HEADER}
            ${engine_physic_SOURCE}       ${engine_physic_HEADER}
            ${engine_sound_SOURCE}        ${engine_sound_HEADER}
            ${engine_util_SOURCE}         ${engine_util_HEADER}
            ${HiddenDepenCMakeLists}
    )
