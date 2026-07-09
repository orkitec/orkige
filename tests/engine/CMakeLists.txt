# orkige_engine unit tests - pure logic only, headless (no Ogre::Root, no
# window/GPU); same Catch2 + label conventions as tests/core.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_engine_tests
    TestRunListener.cpp
    EngineTestEnvironment.h
    RenderSystemSelectionTests.cpp
    FastGuiAtlasTests.cpp
    ScriptComponentTests.cpp
    PlayerBundleTests.cpp
    TwoDSupportTests.cpp
    ParticleSimTests.cpp
    SoundMixerTests.cpp
    TweenScriptTests.cpp
    InputActionTests.cpp
)

target_link_libraries(orkige_engine_tests PRIVATE
    Orkige::Engine
    Catch2::Catch2WithMain
)

# FastGuiAtlasTests parses the generated fastgui atlas through the real
# UiAtlas parser (headless constructor - the GPU half is covered by the
# jumper_selfcheck integration run)
target_compile_definitions(orkige_engine_tests PRIVATE
    ORKIGE_FASTGUI_ATLAS_DIR="${Orkige_SOURCE_DIR}/samples/jumper/media"
)
if(TARGET orkige_generate_atlases)
    add_dependencies(orkige_engine_tests orkige_generate_atlases)
endif()

include(Catch)
catch_discover_tests(orkige_engine_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
