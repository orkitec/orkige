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

if(TARGET orkige_editor AND TARGET orkige_player)
    # editor play mode: press Play programmatically, require the remote
    # hierarchy to arrive (streamed from the spawned player over the debug
    # protocol), then Stop and require a clean revert to edit mode
    add_test(NAME editor_play_stop COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_stop PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PLAYTEST=stop")

    # crash resilience: same flow, but the player is SIGKILLed instead of
    # stopped - the editor must notice the drop and revert cleanly
    add_test(NAME editor_play_crash COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_crash PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PLAYTEST=crash")
endif()

if(TARGET orkige_player)
    # loads the committed example scene (path relative to the repo root)
    add_test(NAME player_example_scene
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_scene PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")

    # remote-debug contract: the driver spawns a real player on the falling
    # cube scene and asserts hello/hierarchy/select/pause/resume/step/
    # set_property/quit end to end (see player_debug_driver.cpp)
    add_executable(player_debug_driver
        integration_driver/player_debug_driver.cpp)
    target_link_libraries(player_debug_driver PRIVATE Orkige::Core)
    add_test(NAME player_debug_pause
        COMMAND player_debug_driver $<TARGET_FILE:orkige_player>
            "${Orkige_SOURCE_DIR}/tests/scenes/falling_cube.oscene"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
endif()

# shared properties for all integration tests
foreach(integration_test demo_plain demo_mesh demo_physics demo_sound
        demo_synth_esc editor_selfcheck editor_resize player_example_scene
        player_debug_pause editor_play_stop editor_play_crash)
    if(TEST ${integration_test})
        set_tests_properties(${integration_test} PROPERTIES
            LABELS integration TIMEOUT 60)
    endif()
endforeach()

# the play-mode tests boot a second engine process (the player) inside the
# run - give them more headroom than the single-process tests
foreach(play_test player_debug_pause editor_play_stop editor_play_crash)
    if(TEST ${play_test})
        set_tests_properties(${play_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()
