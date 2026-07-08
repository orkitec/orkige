# jumper sample unit tests - pure gameplay math (samples/jumper/JumperLogic.h),
# headless (no window/GPU); same Catch2 + label conventions as tests/engine.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(jumper_logic_tests
    JumperLogicTests.cpp
)

target_include_directories(jumper_logic_tests PRIVATE
    "${Orkige_SOURCE_DIR}/samples/jumper"
)

target_link_libraries(jumper_logic_tests PRIVATE
    Orkige::Engine
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(jumper_logic_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
