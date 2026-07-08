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

if(TARGET jumper)
    # pure gameplay math of the jumper sample (no window/GPU)
    add_subdirectory(jumper)
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
    # empty-start assertion at frame 1 (the editor must open with an empty
    # untitled scene), fixture creation at frame 2, picking checks at frames
    # 45/65, scene round-trip self-check at frame 90
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
    # + validation, drag-merge (one undo step), save/reload persistence,
    # add/remove component (dependency rule, state restore), mesh change,
    # multi-select delete/duplicate batches (one undo step each), mesh
    # import (copy into the scene media dir + undoable create + refusal
    # of non-mesh files) and the fly-mode camera step
    add_test(NAME editor_edittest COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_edittest PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_EDITTEST=1;ORKIGE_DEMO_FRAMES=140;ORKIGE_EDITOR_EDITTEST_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_edittest.oscene")
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

    # the project system end to end: open projects/example programmatically
    # (same function as File > Open Project), re-open it (resource-group
    # teardown on a project switch), assert the project roots everything
    # (window title, resource locations, main scene, import destination),
    # then run the full Play/Stop cycle against it - the spawned player
    # receives --project (see the ORKIGE_EDITOR_PROJECT_TEST block in
    # tools/editor/main.cpp)
    add_test(NAME editor_project_play COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_play PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PROJECT_TEST=${Orkige_SOURCE_DIR}/projects/example;ORKIGE_EDITOR_PLAYTEST=stop")

    # native project modules, compile-on-Play (milestone 3): open
    # projects/jumper-native (manifest carries native.target), press Play -
    # the editor must enter the Building state, run the real cmake
    # configure+build of the module (output as [build] Console lines),
    # launch the PROJECT'S OWN executable and receive its remote hierarchy
    # (incl. the module-spawned "Player" object), then Stop and revert
    # cleanly. The first run compiles the module from scratch (the build
    # tree under projects/jumper-native/native/build persists across runs,
    # so re-runs are an incremental no-op build) - hence the fat TIMEOUT.
    add_test(NAME editor_project_native_play COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_native_play PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_NATIVE_PLAYTEST=${Orkige_SOURCE_DIR}/projects/jumper-native")

    # the deliberate-breakage variant: the hook copies jumper-native to a
    # TEMP directory (the real project is never touched), injects a syntax
    # error into the module source and presses Play - the build failure
    # must keep the editor in edit mode with [build] error lines in the
    # Console and nothing launched. Always compiles from scratch (fresh
    # temp copy), so it shares the fat TIMEOUT.
    add_test(NAME editor_project_native_play_break COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_native_play_break PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_NATIVE_PLAYTEST=${Orkige_SOURCE_DIR}/projects/jumper-native;ORKIGE_EDITOR_NATIVE_PLAYTEST_BREAK=1")

    # play mode on an iOS simulator: same stop flow, but the player is the
    # OrkigePlayer.app launched on a simulator via simctl (temp scene read
    # straight off the host filesystem, debug link over the shared loopback).
    # "auto" prefers a booted simulator with the app installed and falls back
    # to booting a shutdown one (auto-installing the built app). SKIPPED
    # (exit 77) when neither exists - build the ios-simulator-debug preset.
    if(APPLE)
        add_test(NAME editor_play_simulator COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_simulator PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_PLAYTEST=stop;ORKIGE_EDITOR_PLAY_SIMULATOR=auto"
            SKIP_RETURN_CODE 77)

        # the boot-from-shutdown path, exercised explicitly: "auto-shutdown"
        # only accepts a SHUTDOWN simulator, so the run really covers simctl
        # boot + open Simulator.app + poll-until-Booted + app auto-install +
        # launch, and shuts the simulator down again afterwards. SKIPPED
        # (exit 77) when no shutdown simulator (or no built player app)
        # exists.
        add_test(NAME editor_play_simulator_boot COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_simulator_boot PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_PLAYTEST=stop;ORKIGE_EDITOR_PLAY_SIMULATOR=auto-shutdown"
            SKIP_RETURN_CODE 77)
    endif()

    # play mode on Android: same stop flow, but the player is the APK
    # launched on an adb-connected device/emulator (temp scene delivered via
    # adb push + run-as, debug link over an 'adb forward tcp' bridge).
    # SKIPPED (exit 77) when no adb device has the APK installed - build
    # (android-debug preset), package (tools/player/android/package_apk.sh)
    # and adb install it.
    add_test(NAME editor_play_android COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_android PROPERTIES
        ENVIRONMENT
            "ORKIGE_EDITOR_PLAYTEST=stop;ORKIGE_EDITOR_PLAY_ANDROID=auto"
        SKIP_RETURN_CODE 77)
endif()

if(TARGET jumper)
    # the jumper sample's scripted verification run: synthetic key events
    # through the input pipeline, asserts the textured meshes loaded (texture
    # units on the platform/crate/player materials), movement under RIGHT,
    # jump + landing, fall-off respawn and the goal/win path (see the
    # ORKIGE_JUMPER_SELFCHECK block in samples/jumper/main.cpp)
    add_test(NAME jumper_selfcheck COMMAND jumper
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(jumper_selfcheck PROPERTIES
        ENVIRONMENT "ORKIGE_JUMPER_SELFCHECK=1")
endif()

if(TARGET orkige_editor AND TARGET jumper)
    # editor compatibility of the jumper level: open level1.oscene through
    # the same code path as File > Open, then assert the platforms came in
    # textured and are selectable/movable via an undoable transform command
    add_test(NAME editor_open_jumper_level COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_open_jumper_level PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_OPEN_SCENE=${Orkige_SOURCE_DIR}/samples/jumper/level1.oscene;ORKIGE_DEMO_FRAMES=90")
endif()

if(TARGET orkige_player)
    # loads the committed example scene (path relative to the repo root)
    add_test(NAME player_example_scene
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_scene PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")

    # Unity-style project run: --project loads projects/example's manifest,
    # registers its assets/ and scenes/ as resource locations and plays the
    # project's main scene (no scene argument)
    add_test(NAME player_example_project
        COMMAND orkige_player --project projects/example
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_project PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")

    # THE scripting milestone, end to end: the jumper game reimplemented in
    # pure Lua (projects/jumper-lua, scripts/player.lua on a ScriptComponent
    # - zero compiled game code). The player's selfcheck injects synthetic
    # key events through the real input path and asserts script boot,
    # movement, jump+landing, the kill-plane respawn and the goal win via
    # the Player object's transform and the stats the script publishes
    # (see the ORKIGE_JUMPER_LUA_SELFCHECK block in tools/player/main.cpp)
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        add_test(NAME player_jumper_lua_selfcheck
            COMMAND orkige_player --project projects/jumper-lua
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_jumper_lua_selfcheck PROPERTIES
            ENVIRONMENT "ORKIGE_JUMPER_LUA_SELFCHECK=1")

        # the 2D tier, end to end: projects/roller (Continuity x Rolando
        # prototype - SpriteComponents, ortho camera, tilt gravity, physics
        # pause + kinematic tile-group teleports). The selfcheck injects
        # arrow/TAB keys through the real input path and asserts the tilt
        # roll, the paused tile slide (sprite AND body probed) and the win
        # (see the ORKIGE_ROLLER_SELFCHECK block in tools/player/main.cpp)
        add_test(NAME player_roller_selfcheck
            COMMAND orkige_player --project projects/roller
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_roller_selfcheck PROPERTIES
            ENVIRONMENT "ORKIGE_ROLLER_SELFCHECK=1")
    endif()

    # --- project export (milestone 4, Util/orkige_export.py) ---------------
    # The driver (integration_driver/run_export_test.py) runs the exporter,
    # asserts the artifact's structure and self-containment, and - on macOS -
    # RUNS the exported app from a NEUTRAL cwd (the output dir, never the
    # source tree, whose files could mask a missing bundled resource): a
    # clean frame-capped exit proves the bundle genuinely stands alone.
    set(ORKIGE_EXPORT_DRIVER
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_export_test.py")

    # the pure-Lua jumper as a double-clickable macOS .app (release player
    # when build/macos-release carries one, else debug + warning)
    add_test(NAME export_macos_lua
        COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
            --repo "${Orkige_SOURCE_DIR}"
            --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
            --platform macos --engine-build "${CMAKE_BINARY_DIR}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/export/macos-lua"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")

    # the native-module jumper: the exporter builds the module executable
    # (arch-pinned, into native/build-export - separate from the editor's
    # compile-on-Play tree) and ships IT instead of the generic player
    add_test(NAME export_macos_native
        COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
            --repo "${Orkige_SOURCE_DIR}"
            --project "${Orkige_SOURCE_DIR}/projects/jumper-native"
            --platform macos --engine-build "${CMAKE_BINARY_DIR}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/export/macos-native"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")

    # mobile exports: structural assertions only (no simulator/emulator
    # boots, so these stay desktop-runnable); SKIPPED (exit 77) when the
    # platform's preset tree (or the Android build-tools) is absent
    add_test(NAME export_ios_simulator
        COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
            --repo "${Orkige_SOURCE_DIR}"
            --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
            --platform ios-simulator
            --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug"
            --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-lua"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(export_ios_simulator PROPERTIES SKIP_RETURN_CODE 77)

    add_test(NAME export_android
        COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
            --repo "${Orkige_SOURCE_DIR}"
            --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
            --platform android
            --engine-build "${Orkige_SOURCE_DIR}/build/android-debug"
            --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-lua"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(export_android PROPERTIES SKIP_RETURN_CODE 77)

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

# --- Vulkan render system runs (macOS via MoltenVK) -------------------------
# These slots used to be the DISABLED Metal runs: OGRE 14.5's Metal RS has no
# RTSS backend (no MSL writer, "metal"-profile-only, renders through its
# built-in DefaultShaders.metal which aborts on Orkige's vertex-colour
# geometry), so Metal cannot render Orkige content until OGRE grows an
# RTSS-on-Metal path - the Metal RS stays linked and selectable
# (ORKIGE_RENDERSYSTEM=Metal) for init-level experiments only.
# The resolution is Vulkan: OGRE's Vulkan RS has full RTSS support (glslang
# plugin compiles the generated GLSL to SPIR-V) and runs on macOS through
# MoltenVK via the VK_EXT_metal_surface port patch (ports/ogre). MoltenVK is
# driver-tier ('brew install molten-vk'); Engine points the Khronos loader at
# the brew ICD manifest when VK_DRIVER_FILES/VK_ICD_FILENAMES are unset.
if(APPLE AND TARGET hello_orkige)
    add_test(NAME demo_plain_vulkan COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_plain_vulkan PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Vulkan;ORKIGE_DEMO_FRAMES=120")
endif()
if(APPLE AND TARGET orkige_player)
    add_test(NAME player_example_scene_vulkan
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_scene_vulkan PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Vulkan;ORKIGE_DEMO_FRAMES=90")
endif()
if(APPLE AND TARGET orkige_editor)
    add_test(NAME editor_selfcheck_vulkan COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_selfcheck_vulkan PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Vulkan;ORKIGE_EDITOR_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_SELFCHECK_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_selfcheck_vulkan.oscene")
endif()

# shared properties for all integration tests
foreach(integration_test demo_plain demo_mesh demo_physics demo_sound
        demo_synth_esc editor_selfcheck editor_resize editor_edittest
        player_example_scene player_example_project player_debug_pause
        player_jumper_lua_selfcheck player_roller_selfcheck
        editor_play_stop editor_play_crash editor_project_play
        editor_project_native_play editor_project_native_play_break
        editor_play_simulator editor_play_simulator_boot
        editor_play_android
        export_macos_lua export_macos_native
        export_ios_simulator export_android
        demo_plain_vulkan player_example_scene_vulkan editor_selfcheck_vulkan
        jumper_selfcheck editor_open_jumper_level)
    if(TEST ${integration_test})
        set_tests_properties(${integration_test} PROPERTIES
            LABELS integration TIMEOUT 60)
    endif()
endforeach()

# the play-mode tests boot a second engine process (the player) inside the
# run - give them more headroom than the single-process tests; the jumper
# selfcheck simulates a whole scripted play session (jump flight, fall past
# the kill plane, goal walk-in), so it gets the same headroom
foreach(play_test player_debug_pause editor_play_stop editor_play_crash
        editor_project_play editor_play_simulator editor_play_android
        jumper_selfcheck player_jumper_lua_selfcheck player_roller_selfcheck
        # the export tests package a whole bundle and (macos) run the
        # exported app for 90 frames; android runs javac/d8/aapt2
        export_macos_lua export_ios_simulator export_android)
    if(TEST ${play_test})
        set_tests_properties(${play_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()

# the native compile-on-Play tests run a REAL cmake configure + compile of
# the jumper-native module inside the run - the first (or break-variant,
# always-cold) build of the OGRE-including TU plus the full static link
# needs real headroom; warm success re-runs are an incremental no-op
foreach(native_test editor_project_native_play
        editor_project_native_play_break
        # exports jumper-native: the exporter's module build (its separate
        # native/build-export tree persists, so re-runs are incremental)
        # compiles the same OGRE-including module sources - same headroom,
        # same serialization against the compile-on-Play tests
        export_macos_native)
    if(TEST ${native_test})
        set_tests_properties(${native_test} PROPERTIES TIMEOUT 420
            # both drive a build in/from the same module source; keep them
            # serialized so -j runs cannot interleave cmake processes
            RESOURCE_LOCK native_module_build)
    endif()
endforeach()

# the simulator tests may have to cold-boot a simulator (+ install the app)
# before the player even starts - give the boot flow real headroom
if(TEST editor_play_simulator)
    set_tests_properties(editor_play_simulator PROPERTIES TIMEOUT 360)
endif()
if(TEST editor_play_simulator_boot)
    set_tests_properties(editor_play_simulator_boot PROPERTIES TIMEOUT 360)
endif()

# device tests boot iOS simulators / Android emulators - by far the slowest
# part of a full suite run (up to minutes when a device cold-boots). They
# carry the extra "device" label so the default development preset
# (ctest --preset desktop) can exclude them; run them via ctest --preset all
# when deploy/device code changed or before handing work over.
foreach(device_test editor_play_simulator editor_play_simulator_boot
        editor_play_android)
    if(TEST ${device_test})
        # RESOURCE_LOCK: device tests share one simulator/emulator + adb -
        # they must not run concurrently when the suite runs with -j
        set_tests_properties(${device_test} PROPERTIES
            LABELS "integration;device" RESOURCE_LOCK mobile_device)
    endif()
endforeach()
