# orkige_engine unit tests - pure logic only, headless (no Ogre::Root, no
# window/GPU); same Catch2 + label conventions as tests/core.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_engine_tests
    RenderSystemSelectionTests.cpp
)

target_link_libraries(orkige_engine_tests PRIVATE
    Orkige::Engine
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(orkige_engine_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
