# orkige_engine unit tests - pure logic only, headless (no Ogre::Root, no
# window/GPU); same Catch2 + label conventions as tests/core.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_engine_tests
    TestRunListener.cpp
    EngineTestEnvironment.h
    GuiAtlasTests.cpp
    NineSliceLayoutTests.cpp
    GuiLayoutIoTests.cpp
    FontPackerTests.cpp
    FontBakeTests.cpp
    FontAtlasTests.cpp
    LightBudgetTests.cpp
    ScriptComponentTests.cpp
    ScriptComponentKindTests.cpp
    ScriptEventBusKindTests.cpp
    PlayerBundleTests.cpp
    TwoDSupportTests.cpp
    PropertyReflectionTests.cpp
    StaticFlagTests.cpp
    DecalComponentTests.cpp
    PhysicsLayerTests.cpp
    PhysicsContactTests.cpp
    ParticleSimTests.cpp
    SoundMixerTests.cpp
    MusicStreamTests.cpp
    SoundTeardownTests.cpp
    TweenScriptTests.cpp
    InputActionTests.cpp
    CVarProtocolBehaviourTests.cpp
    ScreenFadeTests.cpp
    ScreenShakeTests.cpp
    TextEntryEditTests.cpp
    HapticTests.cpp
    AppHostPolicyTests.cpp
    SkinnedRigExtractTests.cpp
    PakMountTests.cpp
)

# the small zip PakMountTests reads through MiniZip: one STORED entry, two
# DEFLATE (incl. a nested path). Built once at configure with the stdlib
# zipfile so the test exercises real STORED + DEFLATE decoding headlessly.
set(ORKIGE_MINIZIP_TEST_ZIP "${CMAKE_CURRENT_BINARY_DIR}/minizip_test.zip")
execute_process(COMMAND python3 -c
    "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.writestr('stored.txt', b'stored payload contents 12345', zipfile.ZIP_STORED); z.writestr('deflate.txt', b'x'*4096, zipfile.ZIP_DEFLATED); z.writestr('sub/nested.bin', bytes(range(256)), zipfile.ZIP_DEFLATED); z.close()"
    "${ORKIGE_MINIZIP_TEST_ZIP}"
    RESULT_VARIABLE ORKIGE_MINIZIP_ZIP_RESULT)
if(NOT ORKIGE_MINIZIP_ZIP_RESULT EQUAL 0)
    message(FATAL_ERROR "failed to build the MiniZip test fixture zip")
endif()

target_link_libraries(orkige_engine_tests PRIVATE
    Orkige::Engine
    Catch2::Catch2WithMain
)

# the ONE classic-bound suite: Engine::matchRenderSystemName (the runtime
# graphics-API pick behind ORKIGE_RENDERSYSTEM) exists only on the classic
# Engine - the next flavor boots its fixed per-platform render system and
# has no matcher to test
if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    target_sources(orkige_engine_tests PRIVATE RenderSystemSelectionTests.cpp)
endif()

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
    # the generated skinned mannequin SkinnedRigExtractTests parses through
    # the backend-neutral rig extraction (make_character_rig.py fixture)
    ORKIGE_CHARACTER_RIG_GLB="${Orkige_SOURCE_DIR}/tests/projects/character/assets/character_rig.glb"
    # the MiniZip fixture zip (STORED + DEFLATE) and a known non-zip file
    ORKIGE_MINIZIP_TEST_ZIP="${ORKIGE_MINIZIP_TEST_ZIP}"
    ORKIGE_MINIZIP_TEST_SRC="${CMAKE_CURRENT_SOURCE_DIR}/PakMountTests.cpp"
)
if(TARGET orkige_generate_atlases)
    add_dependencies(orkige_engine_tests orkige_generate_atlases)
endif()

include(Catch)
catch_discover_tests(orkige_engine_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
