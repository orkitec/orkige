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
    SafeAreaTests.cpp
    UiLayoutResolverTests.cpp
    ReferenceScaleTests.cpp
    LayoutGroupTests.cpp
    GuiStateTests.cpp
    ScrollClipTests.cpp
    TiltCalibrationTests.cpp
    StringTableTests.cpp
    VectorTessellatorTests.cpp
    VectorShapeAssetTests.cpp
    VectorShapeRasterTests.cpp
    VectorAnimAssetTests.cpp
    VectorAnimEvalTests.cpp
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

include(Catch)
catch_discover_tests(orkige_core_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
