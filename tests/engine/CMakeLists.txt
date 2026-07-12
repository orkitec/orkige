# orkige_engine unit tests - pure logic only, headless (no Ogre::Root, no
# window/GPU); same Catch2 + label conventions as tests/core.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_engine_tests
    TestRunListener.cpp
    EngineTestEnvironment.h
    RenderSystemSelectionTests.cpp
    GuiAtlasTests.cpp
    NineSliceLayoutTests.cpp
    GuiLayoutIoTests.cpp
    FontPackerTests.cpp
    FontBakeTests.cpp
    FontAtlasTests.cpp
    ScriptComponentTests.cpp
    ScriptComponentKindTests.cpp
    PlayerBundleTests.cpp
    TwoDSupportTests.cpp
    PropertyReflectionTests.cpp
    PhysicsLayerTests.cpp
    PhysicsContactTests.cpp
    ParticleSimTests.cpp
    SoundMixerTests.cpp
    MusicStreamTests.cpp
    TweenScriptTests.cpp
    InputActionTests.cpp
    CVarProtocolBehaviourTests.cpp
    ScreenFadeTests.cpp
    ScreenShakeTests.cpp
    TextEntryEditTests.cpp
    HapticTests.cpp
)

target_link_libraries(orkige_engine_tests PRIVATE
    Orkige::Engine
    Catch2::Catch2WithMain
)

# GuiAtlasTests parses the generated gui atlas through the real
# UiAtlas parser (headless constructor - the GPU half is covered by the
# jumper_selfcheck integration run)
target_compile_definitions(orkige_engine_tests PRIVATE
    ORKIGE_GUI_ATLAS_DIR="${Orkige_SOURCE_DIR}/samples/jumper/media"
    # the committed engine-default TrueType face (Nunito, SIL OFL) the font
    # bake + runtime atlas tests rasterise headlessly, straight off disk
    ORKIGE_ENGINE_FONT_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/fonts"
    # the committed tiny OGG fixture MusicStreamTests decodes (headless, no
    # device): read straight off disk to exercise the decode + refill seam
    ORKIGE_MUSIC_TEST_OGG="${Orkige_SOURCE_DIR}/tests/assets/blip.ogg"
)
if(TARGET orkige_generate_atlases)
    add_dependencies(orkige_engine_tests orkige_generate_atlases)
endif()

include(Catch)
catch_discover_tests(orkige_engine_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
