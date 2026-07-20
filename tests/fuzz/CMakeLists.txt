# ============================================================================
# Fuzz targets (ORKIGE_FUZZ) - libFuzzer coverage over the engine's pure,
# headless parsers that consume external / untrusted bytes. This is a SELF-
# CONTAINED block: it never edits the global sanitizer options - each fuzz
# executable carries its own -fsanitize=fuzzer,address,undefined so libFuzzer's
# main() and the harness/parser TUs are always instrumented. For the linked
# orkige_core parser TUs to be instrumented too, configure the tree with
# -DORKIGE_ENABLE_SANITIZERS=ON alongside -DORKIGE_FUZZ=ON (the documented and
# CI path - see Docs/fuzzing.md). Clang / Apple-clang only (libFuzzer ships
# with the compiler).
# ============================================================================

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "ORKIGE_FUZZ requires a Clang or Apple-clang toolchain (libFuzzer is "
        "provided by -fsanitize=fuzzer)")
endif()

if(NOT ORKIGE_ENABLE_SANITIZERS)
    message(STATUS
        "ORKIGE_FUZZ: building without ORKIGE_ENABLE_SANITIZERS - the harness "
        "TUs are instrumented but the linked orkige_core parser code is NOT. "
        "Add -DORKIGE_ENABLE_SANITIZERS=ON for full coverage (Docs/fuzzing.md).")
endif()

# the flags every fuzz executable carries on top of whatever the tree already
# uses; fuzzer implies its own main(), asan+ubsan catch the memory/UB bugs
set(ORKIGE_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

set(ORKIGE_FUZZ_CORPUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/corpus")

# a fuzz target over a core parser: harness TU + any extra sources, linking
# orkige_core; registers a short bounded ctest (label "fuzz") over its seed
# corpus so `ctest -L fuzz` on a fuzz tree is a smoke run. CI runs the binary
# directly for longer (Docs/fuzzing.md).
function(orkige_add_fuzz_target name)
    set(multiValueArgs SOURCES INCLUDES LIBS)
    cmake_parse_arguments(FUZZ "" "" "${multiValueArgs}" ${ARGN})

    add_executable(${name} ${FUZZ_SOURCES})
    target_link_libraries(${name} PRIVATE Orkige::Core ${FUZZ_LIBS})
    if(FUZZ_INCLUDES)
        target_include_directories(${name} PRIVATE ${FUZZ_INCLUDES})
    endif()
    target_compile_options(${name} PRIVATE ${ORKIGE_FUZZ_FLAGS})
    target_link_options(${name} PRIVATE ${ORKIGE_FUZZ_FLAGS})

    set(corpus "${ORKIGE_FUZZ_CORPUS_ROOT}/${name}")
    # new interesting units go to a throwaway dir in the BUILD tree (the first
    # corpus arg is libFuzzer's writable output); the committed seed corpus is
    # passed as a read-only input so a smoke run never mutates it in place
    set(runDir "${CMAKE_CURRENT_BINARY_DIR}/run/${name}")
    file(MAKE_DIRECTORY "${runDir}")
    # a bounded smoke leg: replay the seed corpus + a few seconds of mutation.
    # ORKIGE_FUZZ_SECONDS overrides the budget (CI runs the binaries directly
    # for longer - Docs/fuzzing.md).
    if(NOT DEFINED ORKIGE_FUZZ_SECONDS)
        set(ORKIGE_FUZZ_SECONDS 20)
    endif()
    add_test(NAME ${name}
        COMMAND ${name} "${runDir}" "${corpus}"
            -error_exitcode=1 -timeout=25 -rss_limit_mb=4096
            -malloc_limit_mb=2048 -max_total_time=${ORKIGE_FUZZ_SECONDS})
    set_tests_properties(${name} PROPERTIES
        LABELS "fuzz"
        TIMEOUT 180)
endfunction()

# --- the pure core-parser targets ------------------------------------------
orkige_add_fuzz_target(fuzz_material   SOURCES fuzz_material.cpp)
orkige_add_fuzz_target(fuzz_json       SOURCES fuzz_json.cpp)
orkige_add_fuzz_target(fuzz_vectoranim SOURCES fuzz_vectoranim.cpp)
orkige_add_fuzz_target(fuzz_vectorshape SOURCES fuzz_vectorshape.cpp)

# --- MiniZip: the parser lives in orkige_engine, but it is renderer-free and
# needs only zlib + core (PathJail, logging), so it is compiled straight into
# the fuzz target rather than dragging in the whole OGRE engine closure.
find_package(ZLIB REQUIRED)
orkige_add_fuzz_target(fuzz_minizip
    SOURCES
        fuzz_minizip.cpp
        "${CMAKE_SOURCE_DIR}/orkige_engine/engine_filesystem/MiniZip.cpp"
    INCLUDES
        "${CMAKE_SOURCE_DIR}/orkige_engine"
    LIBS
        ZLIB::ZLIB)
