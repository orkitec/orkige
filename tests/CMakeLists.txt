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

if(TARGET orkige_engine)
    # pure-logic engine unit tests (no window/GPU)
    add_subdirectory(engine)
endif()

if(TARGET orkige_editor_core)
    # UI-independent editor logic: command stack, naming, object snapshots
    add_subdirectory(editor_core)
endif()

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

    # scripted editing run against EditorCore: tool switching, undoable
    # transform command (do/undo/redo), duplicate with offset, delete +
    # undo restoring the full component state (transform + mesh), rename
    # + validation, drag-merge (one undo step) and save/reload persistence
    add_test(NAME editor_edittest COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_edittest PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_EDITTEST=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_EDITTEST_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_edittest.oscene")
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

# --- Metal render system runs (macOS) - registered but DISABLED ------------
# The Metal RS is linked, statically registered and runtime-selectable
# (ORKIGE_RENDERSYSTEM=Metal -> Engine::setPreferredRenderSystem; the matching
# rule is unit-tested in tests/engine), and it initializes fully on the
# SDL-provided NSWindow (Metal device up, CAMetalLayer view attached).
# Rendering, however, cannot work yet: OGRE 14.5's Metal RS has no RTSS
# backend - the RTSS emits glsl/glsles/hlsl/glslang only (no MSL writer),
# the Metal RS accepts only the "metal" shader profile and claims
# RSC_FIXED_FUNCTION ("FIXME: hack to get past RTSS" in
# OgreMetalRenderSystem.mm) so everything renders through its built-in
# Media/Main/DefaultShaders.metal (MVP transform + one texture sample, no
# vertex colours, no lighting). That default vertex shader requires a
# TEXCOORD0 attribute, so Orkige's vertex-colour geometry aborts PSO creation
# with "Vertex attribute uv(8) is missing from the vertex descriptor"
# (MetalRenderSystem::getPipelineState). These runs stay DISABLED until OGRE
# grows a real RTSS-on-Metal path or Orkige ships "metal" programs for its
# materials (Phase 3 work); flip DISABLED off to re-check a new OGRE version.
if(APPLE AND TARGET hello_orkige)
    add_test(NAME demo_plain_metal COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_plain_metal PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Metal;ORKIGE_DEMO_FRAMES=120"
        DISABLED TRUE)
endif()
if(APPLE AND TARGET orkige_player)
    add_test(NAME player_example_scene_metal
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_scene_metal PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Metal;ORKIGE_DEMO_FRAMES=90"
        DISABLED TRUE)
endif()
if(APPLE AND TARGET orkige_editor)
    add_test(NAME editor_selfcheck_metal COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_selfcheck_metal PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Metal;ORKIGE_EDITOR_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_SELFCHECK_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_selfcheck_metal.oscene"
        DISABLED TRUE)
endif()

# shared properties for all integration tests
foreach(integration_test demo_plain demo_mesh demo_physics demo_sound
        demo_synth_esc editor_selfcheck editor_resize editor_edittest
        player_example_scene player_debug_pause editor_play_stop
        editor_play_crash demo_plain_metal player_example_scene_metal
        editor_selfcheck_metal)
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
