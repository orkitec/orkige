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
    LogLevelsTests.cpp
    PropertyReflectionTests.cpp
    TraceWriterTests.cpp
    BreadcrumbsTests.cpp
    MemorySamplerTests.cpp
    BenchmarkRecorderTests.cpp
    JsonTests.cpp
    WebSocketCodecTests.cpp
    EventTests.cpp
    ComponentTests.cpp
    TypeManagerTests.cpp
    ScriptRuntimeTests.cpp
    ScriptEventBusTests.cpp
    SceneSerializerTests.cpp
    PrefabSerializerTests.cpp
    PrefabOverrideMergeTests.cpp
    HierarchyTests.cpp
    GameObjectManagerTeardownTests.cpp
    GlobalEventManagerTeardownTests.cpp
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
    AtmosphereDescTests.cpp
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
    LottieCorpusTests.cpp
    PngWriterTests.cpp
    WobbleSpringTests.cpp
    SoftBodyDeformTests.cpp
    GuiAnimationTests.cpp
    ScreenStackTests.cpp
    SoundVariationTests.cpp
)

if(NOT EMSCRIPTEN)
    # host-only test TUs: the loopback-socket suites (HttpServerTests,
    # DebugProtocolTests and DebugWebSocketLinkTests bind/connect on
    # 127.0.0.1 - the single-threaded wasm runtime has no working host-side
    # socket transport; the wasm player's debug link instead DIALS OUT over
    # the page's WebSocket, whose editor side these host suites verify) and
    # the worker-thread suites (MemoryManagerTests, ProfileManagerTests
    # spawn std::thread - the v1 web build compiles without pthreads, so
    # thread creation aborts at runtime). All still run on every desktop
    # flavor gate.
    target_sources(orkige_core_tests PRIVATE
        DebugProtocolTests.cpp
        HttpServerTests.cpp
        DebugWebSocketLinkTests.cpp
        MemoryManagerTests.cpp
        ProfileManagerTests.cpp
    )
endif()

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
    ORKIGE_BENCHMARK_ASSET_DIR="${Orkige_SOURCE_DIR}/projects/benchmark/assets"
)

if(EMSCRIPTEN)
    # web tree: the binary is a wasm module node hosts. NODERAWFS maps the
    # suite's real file IO (archives, sidecars, save stores, temp fixtures)
    # straight onto the host filesystem; the stack/heap knobs cover the
    # deepest fixtures (tessellation, Lottie corpus). One ctest registration
    # runs the whole suite through node - per-case discovery would execute
    # the binary at build time, which a cross build cannot.
    target_link_options(orkige_core_tests PRIVATE
        -sNODERAWFS=1
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=5242880
        -sEXIT_RUNTIME=1
        # a failing suite must name its abort reason, not "Aborted(undefined)"
        -sASSERTIONS=1
    )
    # node: the emsdk bundles its own (the same runtime that hosts emcc);
    # fall back to a PATH install
    file(GLOB ORKIGE_EMSDK_NODE "$ENV{EMSDK}/node/*/bin/node")
    if(ORKIGE_EMSDK_NODE)
        list(GET ORKIGE_EMSDK_NODE 0 ORKIGE_NODE)
    else()
        find_program(ORKIGE_NODE node REQUIRED)
    endif()
    add_test(NAME core_unit_wasm
        COMMAND "${ORKIGE_NODE}" "$<TARGET_FILE_DIR:orkige_core_tests>/orkige_core_tests.js"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(core_unit_wasm PROPERTIES
        LABELS unit TIMEOUT 300)
else()
    include(Catch)
    catch_discover_tests(orkige_core_tests
        PROPERTIES LABELS unit TIMEOUT 30
    )
endif()
