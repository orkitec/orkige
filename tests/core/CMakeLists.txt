# orkige_core unit tests - headless, fast, Catch2-based.
# Each TEST_CASE is registered as its own ctest test with LABELS unit.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_core_tests
    CoreTestEnvironment.h
    TestComponents.h
    TestComponents.cpp
    XMLArchiveTests.cpp
    DebugProtocolTests.cpp
    EventTests.cpp
    ComponentTests.cpp
    TypeManagerTests.cpp
    MetaLuaTests.cpp
    SceneSerializerTests.cpp
)

target_link_libraries(orkige_core_tests PRIVATE
    Orkige::Core
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(orkige_core_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
