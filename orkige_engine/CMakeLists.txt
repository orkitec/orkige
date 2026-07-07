# orkige_engine - the OGRE-facing layer.
# Ported module-by-module from OGRE 1.7 to 14.x; only ported modules/files are
# listed. Unported modules (engine_fastgui, engine_gocomponent, engine_sound,
# engine_input, engine_mygui, engine_physic) join as they are ported.

find_package(OGRE CONFIG REQUIRED COMPONENTS Bites RTShaderSystem Overlay)
find_package(SDL3 CONFIG REQUIRED)

set(ORKIGE_ENGINE_SOURCES
    engine_module/EnginePrerequisites.h
    engine_module/module.cpp
    engine_filesystem/BigZipArchive.h
    engine_filesystem/BigZipArchive.cpp
    engine_filesystem/BigZipArchiveFactory.h
    engine_filesystem/BigZipArchiveFactory.cpp
    engine_util/StringConverter.h
    engine_util/StringUtil.h
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
    engine_physic/CollisionTools.h
    engine_physic/CollisionTools.cpp
)

add_library(orkige_engine STATIC ${ORKIGE_ENGINE_SOURCES})
add_library(Orkige::Engine ALIAS orkige_engine)

target_include_directories(orkige_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The whole 2026 target set (GL3+/Metal/Vulkan/GLES3) is shader-based; the
# fixed-function fallbacks from the 1.7 era are gone for good.
target_compile_definitions(orkige_engine PUBLIC USE_RTSHADER_SYSTEM)

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
        SDL3::SDL3
)
