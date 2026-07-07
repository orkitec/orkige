# Orkige test suite.
#
#   ctest --preset unit   ->  headless Catch2 unit tests only (fast)
#   ctest --preset all    ->  unit tests + integration runs (open real windows)
#
# The integration tests reuse the self-checking apps: hello_orkige, the editor
# and the player verify themselves internally and exit non-zero on failure -
# that exit code IS the test contract. They need a window/GPU, so they carry
# LABELS integration and are excluded from the unit preset.

add_subdirectory(core)

if(TARGET hello_orkige)
    # frame-capped demo runs; 120 frames ~= 2s
    add_test(NAME demo_plain COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_plain PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=120")

    add_test(NAME demo_mesh COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_mesh PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_MESH=1;ORKIGE_DEMO_FRAMES=120")

    add_test(NAME demo_physics COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_physics PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_PHYSICS=1;ORKIGE_DEMO_FRAMES=120")

    add_test(NAME demo_sound COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_sound PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SOUND=1;ORKIGE_DEMO_FRAMES=120")

    # no frame cap on purpose: the synthesized ESC (at frame 60) must quit the
    # app through the input pipeline - if that path regresses, the run hangs
    # and the TIMEOUT fails the test
    add_test(NAME demo_synth_esc COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_synth_esc PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SYNTH_ESC=1")
endif()

if(TARGET orkige_editor)
    # picking checks at frames 45/65, scene round-trip self-check at frame 90
    add_test(NAME editor_selfcheck COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_selfcheck PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_SELFCHECK_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_selfcheck.oscene")

    # programmatic SDL_SetWindowSize at frame 80 (resize robustness)
    add_test(NAME editor_resize COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_resize PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_RESIZE_TEST=1;ORKIGE_DEMO_FRAMES=120")
endif()

if(TARGET orkige_player)
    # loads the committed example scene (path relative to the repo root)
    add_test(NAME player_example_scene
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_scene PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")
endif()

# shared properties for all integration tests
foreach(integration_test demo_plain demo_mesh demo_physics demo_sound
        demo_synth_esc editor_selfcheck editor_resize player_example_scene)
    if(TEST ${integration_test})
        set_tests_properties(${integration_test} PROPERTIES
            LABELS integration TIMEOUT 60)
    endif()
endforeach()
