# Orkige test suite.
#
#   ctest --preset unit             ->  headless Catch2 unit tests only (fast;
#                                       the default next-flavor tree)
#   ctest --preset desktop          ->  the default (Ogre-Next) suite
#   ctest --preset desktop-classic  ->  the classic-flavor suite (exports,
#                                       Vulkan runs, native-module tests)
#   ctest --preset all              ->  classic tree incl. device tests
#
# The integration tests reuse the self-checking apps: hello_orkige, the editor
# and the player verify themselves internally and exit non-zero on failure -
# that exit code IS the test contract. They need a window/GPU, so they carry
# LABELS integration and are excluded from the unit preset.

add_subdirectory(core)

# Flavor suffix for test registrations that exist on BOTH render flavors
# (game selfchecks since B3, editor tests since the editor-on-Next port):
# the _next names keep the two presets' reports distinguishable.
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    set(ORKIGE_FLAVOR_TEST_SUFFIX "_next")
else()
    set(ORKIGE_FLAVOR_TEST_SUFFIX "")
endif()

# renderer containment lint (WP-A1.5, Docs/render-abstraction.md): fails
# when an Ogre:: spelling appears in code outside the backend directories,
# the RenderMath.h alias header and the sanctioned files/blocks maintained
# in Util/ogre_containment.json. Pure source scan - headless, LABELS unit,
# so it runs in the unit AND desktop presets.
add_test(NAME render_containment_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_ogre_containment.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(render_containment_lint PROPERTIES
    LABELS unit TIMEOUT 30)

if(TARGET orkige_engine)
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        # pure-logic engine unit tests (no window/GPU); still classic-only:
        # two of the five suites are classic-bound (FastGuiAtlasTests =
        # fastgui zone, RenderSystemSelectionTests = the classic Engine's
        # graphics-API pick) - splitting the executable per flavor buys
        # little while the default (next-flavor) desktop preset runs the
        # same logic through the game selfchecks
        add_subdirectory(engine)
    endif()

    # render_facade_selfcheck - THE backend conformance suite of the
    # engine_render facade (Docs/render-abstraction.md): one headed run
    # exercising window + world/nodes + mesh + sprite + ortho/perspective
    # cameras + light + RTT + ray query + stats + non-black screenshots.
    # The checks TU is backend-agnostic (facade includes only); each
    # backend contributes one bootstrap TU - every future backend
    # (ORKIGE_RENDER_BACKEND=next in A2) must pass this test UNCHANGED.
    set(RENDER_FACADE_SELFCHECK_SOURCES
        render_facade/selfcheck_main.cpp
        render_facade/SelfcheckBootstrap.h)
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        list(APPEND RENDER_FACADE_SELFCHECK_SOURCES
            render_facade/bootstrap_classic.cpp)
    elseif(ORKIGE_RENDER_BACKEND STREQUAL "next")
        list(APPEND RENDER_FACADE_SELFCHECK_SOURCES
            render_facade/bootstrap_next.cpp)
    endif()
    add_executable(render_facade_selfcheck ${RENDER_FACADE_SELFCHECK_SOURCES})
    target_link_libraries(render_facade_selfcheck PRIVATE Orkige::Engine)
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        target_compile_definitions(render_facade_selfcheck PRIVATE
            # backend-internal media the classic bootstrap registers (RTSS libs)
            ORKIGE_SELFCHECK_OGRE_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media")
    elseif(ORKIGE_RENDER_BACKEND STREQUAL "next")
        target_compile_definitions(render_facade_selfcheck PRIVATE
            # backend-internal media the next bootstrap registers (the HLMS
            # shader templates the ogre-next overlay port ships)
            ORKIGE_SELFCHECK_NEXT_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media")
    endif()
    target_compile_definitions(render_facade_selfcheck PRIVATE
        # facade-registered test assets: a vertex-coloured glb + a textured
        # glb/png (reused from the committed sample media)
        ORKIGE_SELFCHECK_MESH_DIR="${Orkige_SOURCE_DIR}/samples/hello_orkige/media"
        ORKIGE_SELFCHECK_TEXTURE_DIR="${Orkige_SOURCE_DIR}/samples/jumper/media")
    add_test(NAME render_facade_selfcheck COMMAND render_facade_selfcheck
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(render_facade_selfcheck PROPERTIES
        ENVIRONMENT "ORKIGE_SELFCHECK_OUT=${CMAKE_CURRENT_BINARY_DIR}/render_facade")
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        # B2 (WP-A2.2/A2.3, Docs/render-abstraction.md) filled the content
        # stubs: the conformance suite runs ENABLED on this flavor, same
        # command, same checks, zero backend-specific carve-outs.

        # render_next_smoke - the B1 flavor bar: window opens, clears to a
        # colour, the facade screenshot is non-black, clean exit 0
        add_executable(render_next_smoke
            render_facade/smoke_main.cpp
            render_facade/SelfcheckBootstrap.h
            render_facade/bootstrap_next.cpp)
        target_link_libraries(render_next_smoke PRIVATE Orkige::Engine)
        target_compile_definitions(render_next_smoke PRIVATE
            ORKIGE_SELFCHECK_NEXT_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media")
        add_test(NAME render_next_smoke COMMAND render_next_smoke
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(render_next_smoke PROPERTIES
            ENVIRONMENT "ORKIGE_SELFCHECK_OUT=${CMAKE_CURRENT_BINARY_DIR}/render_facade"
            LABELS integration TIMEOUT 60)

        # render_backend_parity - the WYSIWYG gate: run the selfcheck on
        # BOTH backends and pixel-compare the screenshots within tolerance
        # (Docs/render-abstraction.md, colour parity). Registered on the
        # NEXT (default) preset only - next is the flavor that must match
        # classic's established output. Cross-preset by nature: the classic
        # binary lives in the classic build tree; when it is absent the
        # driver SKIPs (exit 77) instead of failing - build the host's
        # conventional classic preset (macos-debug-classic /
        # linux-debug-classic) to arm the comparison.
        if(APPLE)
            set(ORKIGE_PARITY_CLASSIC_TREE "build/macos-debug-classic")
        else()
            set(ORKIGE_PARITY_CLASSIC_TREE "build/linux-debug-classic")
        endif()
        add_test(NAME render_backend_parity
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/compare_backend_screenshots.py"
                --next-binary $<TARGET_FILE:render_facade_selfcheck>
                --classic-binary "${Orkige_SOURCE_DIR}/${ORKIGE_PARITY_CLASSIC_TREE}/tests/render_facade_selfcheck"
                --out "${CMAKE_CURRENT_BINARY_DIR}/render_backend_parity"
                --repo "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(render_backend_parity PROPERTIES
            LABELS integration TIMEOUT 180 SKIP_RETURN_CODE 77)
    endif()
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
    # ORKIGE_DEMO_GUI: the fastgui link/lifecycle smoke (empty UI draw
    # layer over a frame + FastGuiManager's clean no-atlas refusal) rides
    # on the plain demo - flavor-neutral since the DrawLayer2D port
    set_tests_properties(demo_plain PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=120;ORKIGE_DEMO_GUI=1")

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

    # ORKIGE_DEMO_SPRITEANIM: the sprite flipbook (SpriteAnimationComponent)
    # headed selfcheck - a 4x1 grid clip ticked through the GameObjectManager;
    # asserts the sibling sprite's UV rect advances across frames and wraps on
    # the loop (frame 120). Runs on both render flavors (per-tree, like the
    # other demo_* tests).
    add_test(NAME demo_spriteanim COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_spriteanim PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SPRITEANIM=1;ORKIGE_DEMO_FRAMES=120")

    # no frame cap on purpose: the synthesized ESC (at frame 60) must quit the
    # app through the input pipeline - if that path regresses, the run hangs
    # and the TIMEOUT fails the test
    add_test(NAME demo_synth_esc COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_synth_esc PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SYNTH_ESC=1")
endif()

if(TARGET orkige_editor)
    # the editor builds/runs on BOTH render flavors since the ImGui-on-
    # DrawLayer2D port; the next preset registers these as *_next
    # empty-start assertion at frame 1 (the editor must open with an empty
    # untitled scene), fixture creation at frame 2, picking checks at frames
    # 45/65, scene round-trip self-check at frame 90
    add_test(NAME editor_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_SELFCHECK_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_selfcheck.oscene")

    # programmatic SDL_SetWindowSize at frame 80 (resize robustness)
    add_test(NAME editor_resize${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_resize${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_RESIZE_TEST=1;ORKIGE_DEMO_FRAMES=120")

    # scripted editing run against EditorCore: tool switching, undoable
    # transform command (do/undo/redo), duplicate with offset, delete +
    # undo restoring the full component state (transform + mesh), rename
    # + validation, drag-merge (one undo step), save/reload persistence,
    # add/remove component (dependency rule, state restore), mesh change,
    # multi-select delete/duplicate batches (one undo step each), mesh
    # import (copy into the scene media dir + undoable create + refusal
    # of non-mesh files), the fly-mode camera step and the GameObject tree
    # (reparent with world-pose preservation, render-node composition,
    # group, active-state propagation, v2 scene round-trip)
    add_test(NAME editor_edittest${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_edittest${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_EDITTEST=1;ORKIGE_DEMO_FRAMES=150;ORKIGE_EDITOR_EDITTEST_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_edittest.oscene")
endif()

if(TARGET orkige_editor AND TARGET orkige_player)
    # editor play mode: press Play programmatically, require the remote
    # hierarchy to arrive (streamed from the spawned player over the debug
    # protocol), then Stop and require a clean revert to edit mode
    add_test(NAME editor_play_stop${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_stop${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PLAYTEST=stop")

    # crash resilience: same flow, but the player is SIGKILLed instead of
    # stopped - the editor must notice the drop and revert cleanly
    add_test(NAME editor_play_crash${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_crash${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PLAYTEST=crash")

    # the project system end to end: open projects/example programmatically
    # (same function as File > Open Project), re-open it (resource-group
    # teardown on a project switch), assert the project roots everything
    # (window title, resource locations, main scene, import destination),
    # then run the full Play/Stop cycle against it - the spawned player
    # receives --project (see the ORKIGE_EDITOR_PROJECT_TEST block in
    # tools/editor/main.cpp)
    add_test(NAME editor_project_play${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_play${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PROJECT_TEST=${Orkige_SOURCE_DIR}/projects/example;ORKIGE_EDITOR_PLAYTEST=stop")

    # native project modules, compile-on-Play (milestone 3): open
    # projects/jumper-native (manifest carries native.target), press Play -
    # the editor must enter the Building state, run the real cmake
    # configure+build of the module (output as [build] Console lines),
    # launch the PROJECT'S OWN executable and receive its remote hierarchy
    # (incl. the module-spawned "Player" object), then Stop and revert
    # cleanly. The spawned module inherits ORKIGE_JUMPER_NATIVE_SELFCHECK
    # from the editor's environment and asserts its fastgui HUD booted from
    # the project atlas at frame 5 (exiting non-zero otherwise, which drops
    # the play process before the hierarchy wait completes - see the hook in
    # projects/jumper-native/native/main.cpp). The first run compiles the
    # module from scratch (the build tree under
    # projects/jumper-native/native/build persists across runs, so re-runs
    # are an incremental no-op build) - hence the fat TIMEOUT.
    # classic-only: the module's persistent build tree (native/build)
    # configures against ONE engine build tree - flip-flopping it between
    # flavors would poison the cmake cache (the break variant builds in a
    # temp copy, but shares the RESOURCE_LOCK/serialization story)
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
    add_test(NAME editor_project_native_play COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_native_play PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_NATIVE_PLAYTEST=${Orkige_SOURCE_DIR}/projects/jumper-native;ORKIGE_JUMPER_NATIVE_SELFCHECK=1")

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
    endif()

    # loud script failures: the hook copies projects/jumper-lua to a temp dir
    # (the real project is never touched), overwrites scripts/player.lua with
    # garbage and presses Play - the player must PUSH a script_error protocol
    # message (no selection involved - never-selected objects fail loudly
    # too), which must surface as a RED "[remote] SCRIPT ERROR on 'Player'"
    # Console line plus the toolbar marker state; Stop must clear both. Works
    # in ORKIGE_SCRIPTING=OFF builds too (the honest "scripting disabled"
    # failure travels the same path).
    add_test(NAME editor_play_script_error${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_script_error${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_SCRIPT_ERROR_PLAYTEST=${Orkige_SOURCE_DIR}/projects/jumper-lua")

    # play mode on an iOS simulator: same stop flow, but the player is the
    # OrkigePlayer.app launched on a simulator via simctl (temp scene read
    # straight off the host filesystem, debug link over the shared loopback).
    # "auto" prefers a booted simulator with the app installed and falls back
    # to booting a shutdown one (auto-installing the built app). SKIPPED
    # (exit 77) when neither exists - build the ios-simulator-debug preset.
    if(APPLE AND ORKIGE_RENDER_BACKEND STREQUAL "classic")
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
    # and adb install it. APPLE-gated with the simulator flow above: the
    # host-side adb/packaging flow is a dev-machine concern today.
    # TODO(linux): the adb flow itself is host-portable - ungate once a
    # Linux machine actually drives Android devices.
    if(APPLE AND ORKIGE_RENDER_BACKEND STREQUAL "classic")
        add_test(NAME editor_play_android COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_android PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_PLAYTEST=stop;ORKIGE_EDITOR_PLAY_ANDROID=auto"
            SKIP_RETURN_CODE 77)
    endif()
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
        # B3: the game selfchecks run on BOTH flavors - the next flavor's
        # registrations carry a _next suffix and the UI assertions are
        # compile-time-skipped there (fastgui is classic-only, decision #2;
        # game.lua probes engine:hasUISystem() and runs HUD-less)
        add_test(NAME player_jumper_lua_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/jumper-lua
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_jumper_lua_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_JUMPER_LUA_SELFCHECK=1")

        # the 2D tier, end to end: projects/roller (Continuity x Rolando
        # prototype - SpriteComponents, ortho camera, tilt gravity, physics
        # pause + kinematic tile-group teleports). The selfcheck injects
        # arrow/TAB keys through the real input path and asserts the tilt
        # roll, the paused tile slide (sprite AND body probed) and the win
        # (see the ORKIGE_ROLLER_SELFCHECK block in tools/player/main.cpp)
        add_test(NAME player_roller_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/roller
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_roller_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_ROLLER_SELFCHECK=1")

        # the tween + audio-mixer surface, end to end: tests/projects/tween's
        # fixture script runs the whole Lua tween vocabulary (tween.to closure
        # in the recommended re-fetch style, fade/volume typed helpers,
        # cancel, delay, exact landing + onComplete-once) and the first
        # SoundComponent binding (world.getSound, addSound/play/stop,
        # setVolume/setGroup); the manifest carries audio.master/audio.group.*
        # mixer settings that must be applied on project load (see the
        # ORKIGE_TWEEN_SELFCHECK block in tools/player/main.cpp)
        add_test(NAME player_tween_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/tween
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_tween_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_TWEEN_SELFCHECK=1")
    endif()

    # asset-id rename survival end to end (the asset database): the fixture
    # scene in tests/projects/asset_rename references its sprite texture by a
    # STALE name ("ball.png") plus the sidecar asset id of the renamed file
    # (assets/ball_renamed.png + .orkmeta) - the player must resolve the id
    # through the project's AssetDatabase and show the renamed texture (see
    # the ORKIGE_ASSETID_SELFCHECK block in tools/player/main.cpp)
    add_test(NAME player_assetid_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_player --project tests/projects/asset_rename
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_assetid_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_ASSETID_SELFCHECK=ball_renamed.png;ORKIGE_DEMO_FRAMES=60")

    # --- project export (milestone 4, Util/orkige_export.py) ---------------
    # Classic-only: the exporter bundles the classic media set (RTSS libs)
    # and the exported-app contract is classic-verified. The export
    # machinery is PINNED to the classic presets (this tree for Debug, the
    # sibling build/macos-release-classic for the shippable Release player -
    # see sibling_release_tree in Util/orkige_export.py, which also refuses
    # a next tree honestly). TODO(next-export): a next-flavor export needs
    # the Hlms shader-template media bundled + Engine::setHlmsMediaDir wired
    # into PlayerBundle before orkige_export.py can ship the next player.
    # APPLE-gated: the export driver checks/runs .app bundles with otool -
    # macOS host machinery (a Linux registration would hard-fail at otool).
    # TODO(linux): a Linux export target needs its own bundle layout +
    # driver checks in Util/orkige_export.py first.
    if(APPLE AND ORKIGE_RENDER_BACKEND STREQUAL "classic")
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
    endif()

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
if(APPLE AND TARGET hello_orkige AND ORKIGE_RENDER_BACKEND STREQUAL "classic")
    add_test(NAME demo_plain_vulkan COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_plain_vulkan PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Vulkan;ORKIGE_DEMO_FRAMES=120")
endif()
if(APPLE AND TARGET orkige_player AND ORKIGE_RENDER_BACKEND STREQUAL "classic")
    add_test(NAME player_example_scene_vulkan
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_scene_vulkan PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Vulkan;ORKIGE_DEMO_FRAMES=90")
endif()
if(APPLE AND TARGET orkige_editor AND ORKIGE_RENDER_BACKEND STREQUAL "classic")
    add_test(NAME editor_selfcheck_vulkan COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_selfcheck_vulkan PROPERTIES
        ENVIRONMENT "ORKIGE_RENDERSYSTEM=Vulkan;ORKIGE_EDITOR_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_SELFCHECK_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_selfcheck_vulkan.oscene")
endif()

# shared properties for all integration tests
foreach(integration_test demo_plain demo_mesh demo_physics demo_sound
        demo_spriteanim
        demo_synth_esc editor_selfcheck editor_resize editor_edittest
        editor_selfcheck_next editor_resize_next editor_edittest_next
        player_example_scene player_example_project player_debug_pause
        player_jumper_lua_selfcheck player_roller_selfcheck
        player_jumper_lua_selfcheck_next player_roller_selfcheck_next
        player_tween_selfcheck player_tween_selfcheck_next
        player_assetid_selfcheck player_assetid_selfcheck_next
        editor_play_stop editor_play_crash editor_project_play
        editor_play_script_error
        editor_play_stop_next editor_play_crash_next editor_project_play_next
        editor_play_script_error_next
        editor_project_native_play editor_project_native_play_break
        editor_play_simulator editor_play_simulator_boot
        editor_play_android
        export_macos_lua export_macos_native
        export_ios_simulator export_android
        demo_plain_vulkan player_example_scene_vulkan editor_selfcheck_vulkan
        jumper_selfcheck editor_open_jumper_level render_facade_selfcheck)
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
        editor_project_play editor_play_script_error
        editor_play_stop_next editor_play_crash_next editor_project_play_next
        editor_play_script_error_next
        editor_play_simulator editor_play_android
        jumper_selfcheck player_jumper_lua_selfcheck player_roller_selfcheck
        player_jumper_lua_selfcheck_next player_roller_selfcheck_next
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
