# orkige_core unit tests - headless, fast, Catch2-based.
# Each TEST_CASE is registered as its own ctest test with LABELS unit.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_core_tests
    TestRunListener.cpp
    CoreTestEnvironment.h
    TestComponents.h
    TestComponents.cpp
    XMLArchiveTests.cpp
    CVarTests.cpp
    PropertyReflectionTests.cpp
    DebugProtocolTests.cpp
    TraceWriterTests.cpp
    BreadcrumbsTests.cpp
    MemorySamplerTests.cpp
    MemoryManagerTests.cpp
    ProfileManagerTests.cpp
    JsonTests.cpp
    HttpServerTests.cpp
    EventTests.cpp
    ComponentTests.cpp
    TypeManagerTests.cpp
    ScriptRuntimeTests.cpp
    ScriptEventBusTests.cpp
    SceneSerializerTests.cpp
    PrefabSerializerTests.cpp
    PrefabOverrideMergeTests.cpp
    HierarchyTests.cpp
    TagTests.cpp
    ProjectTests.cpp
    AssetDatabaseTests.cpp
    TweenTests.cpp
    PropertyTweenTests.cpp
    LevelGridTests.cpp
    LevelSequenceTests.cpp
    SaveStoreTests.cpp
    AppLifecycleTests.cpp
    CameraFitTests.cpp
    ShadowPresetTests.cpp
    CameraFollowTests.cpp
    TimerManagerTests.cpp
    MusicFadeTests.cpp
    GameStateTests.cpp
    SafeAreaTests.cpp
    UiLayoutResolverTests.cpp
    ReferenceScaleTests.cpp
    LayoutGroupTests.cpp
    GuiStateTests.cpp
    ScrollClipTests.cpp
    TiltCalibrationTests.cpp
    StringTableTests.cpp
    LocaleMatchTests.cpp
    VectorTessellatorTests.cpp
    VectorShapeAssetTests.cpp
    MaterialAssetTests.cpp
    VectorShapeRasterTests.cpp
    VectorAnimAssetTests.cpp
    VectorAnimEvalTests.cpp
    CookVectorAnimRoundTripTests.cpp
    PngWriterTests.cpp
    WobbleSpringTests.cpp
    SoftBodyDeformTests.cpp
    GuiAnimationTests.cpp
    ScreenStackTests.cpp
    SoundVariationTests.cpp
)

if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # backend-specific tests (Meta_Lua.h / ScriptManager / raw sol2); the
    # backend-neutral scripting tests live in ScriptRuntimeTests.cpp and
    # compile in every configuration
    target_sources(orkige_core_tests PRIVATE MetaLuaTests.cpp)
endif()

target_link_libraries(orkige_core_tests PRIVATE
    Orkige::Core
    Catch2::Catch2WithMain
)

# committed fixture assets (e.g. the cook round-trip .oanim, whose currency
# against the live cook is enforced by cook_vector_anim_selftest)
target_compile_definitions(orkige_core_tests PRIVATE
    ORKIGE_TESTS_ASSET_DIR="${Orkige_SOURCE_DIR}/tests/assets"
)

include(Catch)
catch_discover_tests(orkige_core_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
