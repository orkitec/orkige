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
# (game selfchecks and editor tests both run on both flavors):
# the _next names keep the two presets' reports distinguishable.
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    set(ORKIGE_FLAVOR_TEST_SUFFIX "_next")
else()
    set(ORKIGE_FLAVOR_TEST_SUFFIX "")
endif()

# renderer containment lint (Docs/render-abstraction.md): fails
# when an Ogre:: spelling appears in code outside the backend directories,
# the RenderMath.h alias header and the sanctioned files/blocks maintained
# in Util/ogre_containment.json. Pure source scan - headless, LABELS unit,
# so it runs in the unit AND desktop presets.
add_test(NAME render_containment_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_ogre_containment.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(render_containment_lint PROPERTIES
    LABELS unit TIMEOUT 30)

# sprite-atlas packer self-validation: make_sprite_atlas.py packs a
# handful of synthetic sprites, re-parses its own .oatlas and asserts no region
# overlaps, all rects in bounds, deterministic order. Stdlib-only, headless -
# LABELS unit so it runs in the unit AND desktop presets.
add_test(NAME sprite_atlas_packer
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_sprite_atlas.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(sprite_atlas_packer PROPERTIES LABELS unit TIMEOUT 60)

# export-time texture cook self-validation: cook_textures.py resizes /
# premultiplies a synthetic payload per sidecar import settings (default AND a
# per-platform override), asserts the shipped PNG is downscaled AND still
# decodes, and that id-only / sidecar-less textures stay untouched. Stdlib-only,
# headless - the cook-then-load smoke at the tooling level.
add_test(NAME cook_textures_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/cook_textures.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(cook_textures_selftest PROPERTIES LABELS unit TIMEOUT 60)

# vector-shape SVG cook self-validation: cook_shapes.py flattens a synthetic
# SVG (rect + path + circle) to an .oshape and re-parses it, asserting
# well-formed regions with round-tripped fill colours and vertex counts.
# Stdlib-only, headless - the SVG->.oshape on-ramp at the tooling level.
add_test(NAME cook_shapes_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/cook_shapes.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(cook_shapes_selftest PROPERTIES LABELS unit TIMEOUT 60)

# export app-icon pipeline self-validation: orkige_icons.py synthesises a source
# PNG and generates the macOS iconset / iOS icons / Android mipmaps into a
# tempdir, asserting the file set and each entry's pixel size. Stdlib-only,
# headless - the icon-resize step at the tooling level.
add_test(NAME orkige_icons_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_icons.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_icons_selftest PROPERTIES LABELS unit TIMEOUT 60)

# the checked-in engine default icon renders (corner/centre alpha invariant),
# mirroring make_editor_icon's own self-check.
add_test(NAME make_default_icon_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_default_icon.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_default_icon_selftest PROPERTIES LABELS unit TIMEOUT 60)

# export signing-config logic (cert-free): the iOS entitlements composition, the
# identity/profile resolution precedence (arg over env) and the launch-colour
# validation - the pure parts that gate a signed device export, unit-testable
# without a certificate.
add_test(NAME orkige_export_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_export.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_export_selftest PROPERTIES LABELS unit TIMEOUT 60)

if(TARGET orkige_engine)
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        # pure-logic engine unit tests (no window/GPU); still classic-only:
        # two of the five suites are classic-bound (GuiAtlasTests =
        # gui zone, RenderSystemSelectionTests = the classic Engine's
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
        # the content stubs are now filled (Docs/render-abstraction.md):
        # the conformance suite runs ENABLED on this flavor, same
        # command, same checks, zero backend-specific carve-outs.

        # render_next_smoke - the flavor smoke bar: window opens, clears to a
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
    # ORKIGE_DEMO_GUI: the gui link/lifecycle smoke (empty UI draw
    # layer over a frame + GuiManager's clean no-atlas refusal) rides
    # on the plain demo - flavor-neutral via the DrawLayer2D facade
    set_tests_properties(demo_plain PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=120;ORKIGE_DEMO_GUI=1")

    # ORKIGE_DEMO_UI_SCALE: the DPI selfcheck - fake a content scale of 3 and
    # assert UiGlyph::scale snaps to (3,3) and an authored 160px button scales
    # to a tappable size (>= 120px). Flavor-neutral (gui on DrawLayer2D).
    add_test(NAME demo_ui_scale COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_ui_scale PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_UI_SCALE=1;ORKIGE_FAKE_CONTENT_SCALE=3;ORKIGE_FAKE_SAFE_TOP=96;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_UI_SCALE with a larger injected top inset: the safe-area +
    # settings-widget selfcheck - a top-anchored HUD label lands below the
    # inset, and the bound checkbox / select menu change value. Same block,
    # different inset, so the anchoring math is exercised against a real value.
    add_test(NAME demo_ui_safe_area COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_ui_safe_area PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_UI_SCALE=1;ORKIGE_FAKE_CONTENT_SCALE=2;ORKIGE_FAKE_SAFE_TOP=140;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_TEXTENTRY: the gui TextEntry selfcheck - boots a real
    # GuiManager and drives a text field through synthetic SDL events on the
    # real input path (tap-to-focus, SDL_EVENT_TEXT_INPUT typing, backspace,
    # caret move + mid-string insert, max length, Return submit + blur).
    # Flavor-neutral (gui on DrawLayer2D).
    add_test(NAME demo_textentry COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_textentry PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_TEXTENTRY=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_TTF: the runtime TrueType-font + vector-sprite selfcheck -
    # boots a real GuiManager on a runtime-baked atlas (Nunito TTF + an SVG
    # star baked into one page at boot), renders a Latin + a lazily-paged
    # Cyrillic label and the sprite, and asserts a non-empty HUD batch.
    # Flavor-neutral (gui on DrawLayer2D), so it runs on classic + next.
    add_test(NAME demo_ttf COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_ttf PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_TTF=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LAYOUT: the nine-slice + rect-anchor layout selfcheck - boots
    # a real GuiManager, resolves an anchored nine-sliced panel with a
    # parent-relative child, re-lays it out against a switched safe-area root
    # and a design reference scale, and asserts nine-slice adds geometry over a
    # plain stretched quad. Flavor-neutral (the resolver is pure, gui draws
    # through DrawLayer2D), so it runs on classic + next.
    add_test(NAME demo_layout COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_layout PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LAYOUT=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_OUI: the declarative-UI + layout-group + scroll selfcheck -
    # loads a whole settings screen from a committed .oui file (nine-slice
    # panel + a scroll viewport whose vertical group of rows overflows) through
    # GuiFactory::loadLayout, then asserts the content overflows, scrolling
    # shifts the resolved rects, the content layer's scissor equals the
    # viewport, and a checkbox in a scrolled row still hit-tests at its shifted
    # position. Flavor-neutral (pure resolver, gui on DrawLayer2D).
    add_test(NAME demo_oui COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_oui PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_OUI=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_GUI_MODAL: the modal / disabled / toggle-group / toast /
    # dropdown selfcheck - loads a declarative screen with a [Modal] and a
    # [ToggleGroup], then drives it with synthetic input on the real path: the
    # consuming scrim blocks a press bound for a button under it, the dialog
    # button still fires, dismiss frees the widgets, a disabled button is inert,
    # the toggle group enforces single-selection, a toast surfaces and a
    # dropdown opens/picks/closes. Flavor-neutral (gui on DrawLayer2D).
    add_test(NAME demo_gui_modal COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_gui_modal PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_GUI_MODAL=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_GUI_MATRIX: the widget interaction MATRIX - the completeness
    # contract. Builds every widget type from a .oui AND imperatively, drives
    # each with synthetic input, and asserts state/geometry PLUS the whole
    # animation layer: a widget property tween (with retarget-replace), the
    # scale/rotation render transform, cascading group alpha with the below-
    # threshold hit-test gate, a declarative show/hide transition, button press
    # feedback and scroll-view flick momentum. Flavor-neutral (gui on
    # DrawLayer2D), so it runs on classic + next and both must agree.
    add_test(NAME demo_gui_matrix COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_gui_matrix PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_GUI_MATRIX=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_GUI_LUA: the FULLY-SCRIPTABLE contract - the whole widget
    # matrix authored/driven/asserted IN LUA through the ScriptRuntime surface
    # (C++ only injects one synthetic press + reads probes). A missing/renamed
    # gui binding raises a Lua error, so the suite fails by construction. Also
    # proves the .oui<->Lua bridge (findWidget by id). Scripting-gated: the
    # ORKIGE_SCRIPTING=OFF build skips the body (ScriptRuntime unavailable).
    add_test(NAME demo_gui_lua COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_gui_lua PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_GUI_LUA=1;ORKIGE_DEMO_FRAMES=4")

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

    # ORKIGE_DEMO_MUSIC: the streamed-music selfcheck - stream a committed
    # loopable OGG through the queued-buffer MusicStream path and assert the
    # playhead advances (audio device present) or the honest no-op path (device
    # absent, headless CI). Device-tolerant; runs on both render flavors.
    add_test(NAME demo_music COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_music PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_MUSIC=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_SPRITEANIM: the sprite flipbook (SpriteAnimationComponent)
    # headed selfcheck - a 4x1 grid clip ticked through the GameObjectManager;
    # asserts the sibling sprite's UV rect advances across frames and wraps on
    # the loop (frame 120). Runs on both render flavors (per-tree, like the
    # other demo_* tests).
    add_test(NAME demo_spriteanim COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_spriteanim PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SPRITEANIM=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_SPRITEATLAS: the sprite-atlas + live-sampler headed
    # selfcheck - loads a region of the generated demo_sprite_atlas
    # and asserts the UV rect matches the shared pixelRectToUV over the
    # region's pixel rect (parsed independently from the .oatlas), distinct
    # regions differ, a bogus region is refused, and setFilter/setAddressing
    # apply. Runs on both render flavors (per-tree, like the other demo_*).
    add_test(NAME demo_spriteatlas COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_spriteatlas PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SPRITEATLAS=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_PARTICLES: the 2D particle-system headed selfcheck -
    # a burst-only additive ParticleComponent on one GameObject; fires a burst
    # of maxParticles and asserts the frame-stats triangle count rose by
    # ~maxParticles*2 (the single-draw SpriteBatch) while alive and decayed
    # back once every particle passed its lifetime. Reads stats, not pixels -
    # runs on both render flavors (per-tree, like the other demo_* tests).
    add_test(NAME demo_particles COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_particles PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_PARTICLES=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_VECTORSHAPE: the flat-colour vector-shape headed selfcheck -
    # a VectorShapeComponent loads demo_blob.oshape (concave blob + accent
    # region), which is tessellated (earcut) with a baked alpha-feather edge
    # into a facade VectorMesh; asserts the mesh tessellated (triangle count >
    # 0) and that toggling its visibility moves the frame-stats triangle count
    # (it renders). Reads stats, not pixels - runs on both render flavors.
    add_test(NAME demo_vectorshape COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_vectorshape PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_VECTORSHAPE=1;ORKIGE_DEMO_FRAMES=120")

    # no frame cap on purpose: the synthesized ESC (at frame 60) must quit the
    # app through the input pipeline - if that path regresses, the run hangs
    # and the TIMEOUT fails the test
    add_test(NAME demo_synth_esc COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_synth_esc PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SYNTH_ESC=1")
endif()

if(TARGET orkige_editor)
    # the editor builds/runs on BOTH render flavors via the ImGui-on-
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

    # MCP endpoint, headless end to end: the editor hosts its MCP
    # server on an ephemeral in-process port over Streamable HTTP and, on a
    # worker thread, drives a RAW TCP socket through a real MCP conversation -
    # a JSON-RPC initialize handshake, notifications/initialized, tools/list,
    # then tools/call: create_object (authed, + verify it appears in
    # list_hierarchy), an AUTH-REJECTED create_object (a mutation with no bearer
    # token) and a screenshot to a temp path (+ verify the file was written) -
    # asserting MCP-compliant JSON-RPC responses (id echo, result shape) at
    # every step. Proves the whole C++ MCP endpoint without Python. Screenshot
    # needs the GPU -> integration.
    add_test(NAME editor_control${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    # TIMEOUT is a coarse backstop only: the self-test's own condition-driven
    # polls (incl. a cmake configure+build of a throwaway ctest tree, waited
    # with a generous per-job ceiling) are the real deadlines and report the
    # exact failing step - so the ctest wall must sit well ABOVE their summed
    # budget, or a slow build under full-suite parallelism gets killed mid-run
    # (an opaque failure, and a crash window if the kill lands during teardown).
    set_tests_properties(editor_control${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_CONTROL_TEST=${CMAKE_CURRENT_BINARY_DIR}/editor_control_shot.png"
        LABELS integration TIMEOUT 240)

    # Asset browser (v2): open a temp COPY of
    # projects/example and drive the browser headlessly - enumerate assets
    # (id-carrying vs. dimmed sidecar-less), run the generic importAssetFile
    # (copy + sidecar mint + enumerable), instantiate a sprite via
    # CreateSpriteObjectCommand (+ undo) and a prefab through the drag path
    # (instantiateAssetIntoScene). v2 also: folder enumeration + navigate into
    # a subfolder + up, New Folder, New Script (file + sidecar + enumerable),
    # the texture-thumbnail path (a real png -> a valid ImGui texture handle;
    # a non-image fails gracefully) and the openWithDefaultApp URL resolver
    # (asserted without spawning a GUI app). Flavor-agnostic; both flavors.
    add_test(NAME editor_asset_browser${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_asset_browser${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_ASSETTEST=${Orkige_SOURCE_DIR}/projects/example;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 90)
endif()

# (the Python MCP sidecar Util/orkige_mcp.py and its mcp_bridge_selftest ctest
# were retired: the editor now hosts the MCP endpoint itself over
# HTTP - the JsonTests/HttpServerTests unit tests cover the codec + transport
# and editor_control drives the whole MCP endpoint end to end.)

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

    # the tile-prefab level-authoring loop end to end: on a temp copy of the
    # roller project, paint/replace/erase prefab instances snapped to a
    # LevelComponent grid through the EditorCore paint seams (one undo step per
    # stroke, DeleteSubtree-based erase, the openEdges/tag stamps), round-trip
    # the scene (the suppressed walls + openEdges + tags survive), append it to
    # the level sequence, then PLAY the painted scene and require the painted
    # roots + their non-suppressed wall children to arrive over the debug
    # protocol. Both flavors. Deadline-driven; the TIMEOUT is a coarse backstop.
    add_test(NAME editor_level_paint${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_level_paint${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_LEVELPAINT=${Orkige_SOURCE_DIR}/projects/roller"
        LABELS integration TIMEOUT 120)

    # MCP runtime debug tools end to end: the editor hosts its MCP endpoint on
    # an ephemeral in-process port and, on a worker thread, drives the WHOLE
    # runtime debug loop over a real socket - initialize, then play (boots the
    # built player over the debug protocol), runtime_hierarchy/runtime_select/
    # runtime_state (live inspection), pause + step, set_runtime_property (live
    # write, read back), screenshot_game (the running-game frame, verified on
    # disk), reload_script, an auth-rejected mutation on the live player, then
    # stop and revert to edit mode. Everything runs through the MCP tools,
    # exactly as an agent would. The screenshot path travels in the env value.
    add_test(NAME editor_control_debug${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_control_debug${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_CONTROL_PLAYTEST=${CMAKE_CURRENT_BINARY_DIR}/editor_control_debug_shot.png;ORKIGE_BREADCRUMB_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor_control_debug_breadcrumbs${ORKIGE_FLAVOR_TEST_SUFFIX}"
        # coarse backstop only: the self-test drives a spawned player through a
        # long chain of condition-driven polls (boot, live inspect, pause/step,
        # property read-back, screenshot + trace confirmation, stop), each with
        # its own generous ceiling sized for a GPU-starved player under full
        # parallelism. The ctest wall must exceed their summed worst case so a
        # genuine failure reports its precise step instead of an opaque kill.
        LABELS integration TIMEOUT 300)

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

    # native project modules, compile-on-Play: open
    # projects/jumper-native (manifest carries native.target), press Play -
    # the editor must enter the Building state, run the real cmake
    # configure+build of the module (output as [build] Console lines),
    # launch the PROJECT'S OWN executable and receive its remote hierarchy
    # (incl. the module-spawned "Player" object), then Stop and revert
    # cleanly. The spawned module inherits ORKIGE_JUMPER_NATIVE_SELFCHECK
    # from the editor's environment and asserts its gui HUD booted from
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

    # Lua hot-reload, the full editor->player path: the hook copies
    # tests/projects/hotreload to a temp dir (the real fixture is never
    # touched), presses Play, then edits scripts/reload_probe.lua on disk - the
    # editor's scripts/ watcher must detect the change, send MSG_RELOAD_SCRIPT,
    # emit a [reload] Console line, and the running player must recompile-and-
    # swap so the object's remote state reflects the NEW behavior (observed via
    # the object_state stream). Then a broken edit must surface a RED [remote]
    # SCRIPT ERROR while play continues, and a good edit must clear it. Only
    # meaningful with a live Lua backend (the watcher/protocol are neutral, but
    # the reload must actually change behavior), so it is LUA-gated.
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        add_test(NAME editor_play_hotreload${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_hotreload${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_EDITOR_HOTRELOAD_PLAYTEST=${Orkige_SOURCE_DIR}/tests/projects/hotreload")
    endif()

    # play mode on an iOS simulator: same stop flow, but the player is the
    # OrkigePlayer.app launched on a simulator via simctl (temp scene read
    # straight off the host filesystem, debug link over the shared loopback).
    # "auto" prefers a booted simulator with the app installed and falls back
    # to booting a shutdown one (auto-installing the built app). SKIPPED
    # (exit 77) when neither exists - build the matching-flavor iOS preset
    # (ios-simulator-debug for classic, ios-simulator-debug-next for next; the
    # editor installs the flavor its own build was configured with).
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

        # the safe-area DEVICE gate: play roller (its Lua HUD anchors to the
        # safe area) on a booted iPhone 16 simulator - a Dynamic Island device,
        # so a real non-zero top inset - and assert over the debug protocol that
        # the reported top inset is > 0 and every visible HUD widget sits inside
        # the safe box (MSG_STATS safe-area + MSG_UI_LAYOUT). SKIPPED (exit 77)
        # when no simulator with the player app is prepared - build the
        # ios-simulator-debug(-next) preset and boot an iPhone 16 sim first.
        add_test(NAME player_safearea_device COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_safearea_device PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_SAFEAREA_CHECK=${Orkige_SOURCE_DIR}/projects/roller;ORKIGE_EDITOR_PLAY_SIMULATOR=auto"
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
    if(APPLE)
        add_test(NAME editor_play_android COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_android PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_PLAYTEST=stop;ORKIGE_EDITOR_PLAY_ANDROID=auto"
            SKIP_RETURN_CODE 77)

        # Play on physical iOS hardware: the deploy GATE probe. Unlike the
        # simulator/Android flows this is an export-and-deploy (build + sign +
        # devicectl install/launch), not a live play session - a USB device has
        # no dependency-free debug-port tunnel (see Docs/ios-signing.md). A
        # headless test cannot drive a signed install (no certificate), so this
        # asserts the honest gate: "auto" resolves only when iOS signing is
        # configured AND a device is connected AND the arm64-ios device player
        # is built, else it SKIPS (exit 77). On this and every unsigned machine
        # it takes the SKIP branch (no cert -> the target is disabled). Full
        # deploy is owner-validated on real hardware.
        add_test(NAME editor_play_ios_device COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_ios_device PROPERTIES
            ENVIRONMENT "ORKIGE_EDITOR_PLAY_IOS_DEVICE=auto"
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

    # tilt calibration wiring end to end (synchronous, no render loop): set a
    # simulated tilt pose, calibrate it to neutral, steer past it, and round-trip
    # the auto-written save file (see the ORKIGE_TILT_CALIB_SELFCHECK block in
    # tools/player/main.cpp). The save file is isolated into the build tree.
    add_test(NAME player_tilt_calibration_selfcheck
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_tilt_calibration_selfcheck PROPERTIES
        ENVIRONMENT "ORKIGE_TILT_CALIB_SELFCHECK=1;ORKIGE_TILT_CALIB_FILE=${CMAKE_CURRENT_BINARY_DIR}/tilt_calib_selfcheck.osave")

    # haptics honest desktop no-op + API shape (synchronous, no render loop):
    # isAvailable() is false on desktop and play/pattern calls no-op cleanly
    # (see the ORKIGE_HAPTIC_SELFCHECK block in tools/player/main.cpp). The
    # device buzz itself is manual-only; the pure mapping is unit-tested.
    add_test(NAME player_haptic_selfcheck
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_haptic_selfcheck PROPERTIES
        ENVIRONMENT "ORKIGE_HAPTIC_SELFCHECK=1")

    # whole-project run: --project loads projects/example's manifest,
    # registers its assets/ and scenes/ as resource locations and plays the
    # project's main scene (no scene argument)
    add_test(NAME player_example_project
        COMMAND orkige_player --project projects/example
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_project PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")

    # the flat-colour vector-shape demo project (Util/make_vectorshape_demo.py):
    # loads scenes/main.oscene which places three VectorShapeComponent instances
    # (blob + concave star .oshape assets) at rising zOrders and distinct tints -
    # a clean-load + render smoke that the .oshape assets tessellate and the
    # scene round-trips through serialization. Runs on both render flavors.
    add_test(NAME player_vectorshape_scene
        COMMAND orkige_player --project projects/vectorshapes
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_vectorshape_scene PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")

    # THE scripting proof, end to end: the jumper game reimplemented in
    # pure Lua (projects/jumper-lua, scripts/player.lua on a ScriptComponent
    # - zero compiled game code). The player's selfcheck injects synthetic
    # key events through the real input path and asserts script boot,
    # movement, jump+landing, the kill-plane respawn and the goal win via
    # the Player object's transform and the stats the script publishes
    # (see the ORKIGE_JUMPER_LUA_SELFCHECK block in tools/player/main.cpp)
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        # the game selfchecks run on BOTH flavors - the next flavor's
        # registrations carry a _next suffix and the UI assertions are
        # compile-time-skipped there (gui is classic-only;
        # game.lua probes engine:hasUISystem() and runs HUD-less)
        add_test(NAME player_jumper_lua_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/jumper-lua
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_jumper_lua_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_JUMPER_LUA_SELFCHECK=1")

        # the 2D tier, end to end: projects/roller (a tilt-gravity ball +
        # sliding-tile puzzle prototype - SpriteComponents, ortho camera,
        # tilt gravity, physics
        # pause + kinematic tile-group teleports). The selfcheck injects
        # arrow/TAB keys through the real input path and asserts the tilt
        # roll, the paused tile slide (sprite AND body probed) and the win
        # (see the ORKIGE_ROLLER_SELFCHECK block in tools/player/main.cpp)
        # ORKIGE_PROGRESS_DIR/RESET isolate the progression save into the
        # build tree (a unique dir per test) and wipe it before each run so the
        # single-level win path stays deterministic and never resumes from a
        # stale save (game.lua persists on the win but exits on the banner here)
        add_test(NAME player_roller_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/roller
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_roller_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_ROLLER_SELFCHECK=1;ORKIGE_PROGRESS_RESET=1;ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/roller_progress_selfcheck")

        # SOFT, DEFORMABLE ORGANIC SHAPES, end to end: projects/vectorshapes
        # scenes/softbody.oscene - a blob with a dynamic RigidBody falls onto a
        # static floor and (a) SQUASHES on the contact + WOBBLES (the vertices
        # move off rest via the dynamic deform upload), then (b) returns to its
        # EXACT rest pose as the springs decay, while (c) a second blob is
        # deformed by a Lua self.shape:playMorph call. The player also logs the
        # MEASURED per-frame deform cost (the mobile budget). Runs on both render
        # flavors (the deform is renderer-neutral; the upload goes through the
        # VectorMesh dynamic path on each backend). See the ORKIGE_SOFTBODY_
        # SELFCHECK block in tools/player/main.cpp.
        add_test(NAME player_softbody_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player projects/vectorshapes/scenes/softbody.oscene
                --project projects/vectorshapes
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_softbody_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_SOFTBODY_SELFCHECK=1")

        # roller LEVELS + PROGRESSION, end to end: solve level 1 (the
        # proven slide + roll), assert the runtime SWITCHED to level 2 via the
        # DEFERRED scene load (shared.roller.levelIndex incremented + the
        # progression file written) and then solve the deterministic
        # straight-shot level 2 (see the ORKIGE_ROLLER_PROGRESSION_SELFCHECK
        # block in tools/player/main.cpp). Its own progress dir, reset each run.
        add_test(NAME player_roller_progression_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/roller
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_roller_progression_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_ROLLER_PROGRESSION_SELFCHECK=1;ORKIGE_PROGRESS_RESET=1;ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/roller_progression_selfcheck")

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

        # the game-support pack (Lua `save` API + screen shake + time scale),
        # end to end against the live player wiring (synchronous, any scene -
        # reuses tests/projects/tween): the Lua save table round-trips typed
        # values through the player's SaveStore file (write, flush, fresh-store
        # reload = a restart), ScreenShake deflects then restores the window
        # camera to its EXACT rest pose, and world.setTimeScale(0.5) halves a
        # tween's progress. See the ORKIGE_GAMESUPPORT_SELFCHECK block in
        # tools/player/main.cpp; the ortho fit policy is covered by the
        # CameraFit unit tests. ORKIGE_PROGRESS_DIR isolates the save file.
        add_test(NAME player_gamesupport_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/tween
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_gamesupport_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_GAMESUPPORT_SELFCHECK=1;ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/gamesupport_selfcheck")

        # Lua hot-reload, end to end: tests/projects/hotreload runs a
        # Probe script publishing shared.hotreload.value. The selfcheck
        # overwrites the script on disk, drives ScriptComponent::hotReload()
        # (the player-directed compile-before-swap) and asserts the behavior
        # changed AND an engine-side transform pose persisted across the swap;
        # a broken-file variant must keep the OLD instance ticking with a
        # non-fatal error (mFailed stays false). The committed fixture script
        # is restored on exit (see the ORKIGE_HOTRELOAD_SELFCHECK block in
        # tools/player/main.cpp). The RESOURCE_LOCK keeps concurrent runs (the
        # two flavors under -j) from racing on the in-place file rewrite.
        add_test(NAME player_hotreload_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/hotreload
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_hotreload_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_HOTRELOAD_SELFCHECK=1"
            RESOURCE_LOCK hotreload_fixture)

        # Lua script EXPORTED properties, end to end:
        # tests/projects/scriptprop runs a Mover whose script declares an
        # exported `moveSpeed`. The scene bakes moveSpeed at 5 (the script's own
        # default is 1), so the running script seeing 5 at init proves the
        # per-instance value round-tripped through serialization AND was injected
        # onto `self` before init; the script then moves at that speed and a
        # LIVE set of moveSpeed->0 through the reflected setter (the debug /
        # MCP write path) stops the motion (see the ORKIGE_SCRIPTPROP_SELFCHECK
        # block in tools/player/main.cpp).
        add_test(NAME player_scriptprop_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/scriptprop
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_scriptprop_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_SCRIPTPROP_SELFCHECK=1")

        # crash breadcrumbs, end to end: tests/projects/breadcrumb runs a script
        # that raises a Lua error at init; the player must record it as a
        # "script_error" line in breadcrumbs.jsonl (flushed to disk) while the
        # game keeps running (see the ORKIGE_BREADCRUMB_SELFCHECK block in
        # tools/player/main.cpp). The trail is isolated into the build tree.
        add_test(NAME player_breadcrumb_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/breadcrumb
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_breadcrumb_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_BREADCRUMB_SELFCHECK=1;ORKIGE_BREADCRUMB_DIR=${CMAKE_CURRENT_BINARY_DIR}/breadcrumb_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}")

        # full-screen fade transition, end to end: tests/projects/fade drives a
        # fade-out -> deferred scene switch (sceneA -> sceneB, applied while the
        # overlay is opaque) -> fade-in through the real render loop; asserts the
        # overlay reached opacity, the switch happened while covered, and the fade
        # cleared (see the ORKIGE_FADE_SELFCHECK block in tools/player/main.cpp).
        # Runs on BOTH flavors (the fade is facade-only).
        add_test(NAME player_fade_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/fade
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_fade_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_FADE_SELFCHECK=1")

        # mobile app lifecycle, end to end: tests/projects/lifecycle drives the
        # REAL player wiring with synthetic SDL background/foreground events and
        # asserts the contract - sim paused + rendering stopped + save FLUSHED +
        # onAppPause fired + "background" breadcrumb on background, then sim
        # resumed + onAppResume fired + "foreground" breadcrumb on foreground
        # (see the ORKIGE_LIFECYCLE_SELFCHECK block in tools/player/main.cpp).
        # Runs on BOTH flavors (the lifecycle contract is renderer-neutral); the
        # save + breadcrumb trails are isolated into the build tree and reset.
        add_test(NAME player_lifecycle_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/lifecycle
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_lifecycle_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_LIFECYCLE_SELFCHECK=1;ORKIGE_PROGRESS_RESET=1;ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/lifecycle_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_BREADCRUMB_DIR=${CMAKE_CURRENT_BINARY_DIR}/lifecycle_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}")

        # --- cross-feature integration selfchecks (tests/projects/integration,
        # generated by Util/make_integration_assets.py) -----------------------
        # These exercise system COMBINATIONS no single-feature selfcheck does.
        # Both run on BOTH flavors; both are -j-parallel-safe (they only READ
        # the committed fixture and assert via the shared table + engine-side
        # world queries, so no temp files, no ports, no in-place rewrites).

        # tags + input action + physics + contact events, end to end: a scripted
        # ball finds the goal BY TAG (world.findByTag), an injected named "jump"
        # action turns gravity on, and the physics drop overlaps the goal SENSOR
        # to fire onContactBegin - the whole chain in one run (see the
        # ORKIGE_INTEGRATION_CONTACT_SELFCHECK block in tools/player/main.cpp)
        add_test(NAME player_integration_contact_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/integration
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_integration_contact_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_INTEGRATION_CONTACT_SELFCHECK=1"
            LABELS integration TIMEOUT 120)

        # deferred level switch WITH a live tween + particle emitter, end to end:
        # director.lua (level A) starts a tween + a particle burst then requests
        # world.loadScene mid-flight; the switch must tear the running tween +
        # emitter down cleanly (GameObjectManager::clear) and level B must tick
        # with the shared table intact (see the
        # ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK block in tools/player/main.cpp)
        add_test(NAME player_integration_levelswitch_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player scenes/levelA.oscene --project tests/projects/integration
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_integration_levelswitch_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK=1"
            LABELS integration TIMEOUT 120)
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

    # --- project export (Util/orkige_export.py) ---------------------------
    # Both render flavors: the exporter bundles the tree's engine media
    # (classic RTSS libs or Ogre-Next Hlms templates - keyed off
    # ORKIGE_RENDER_BACKEND) and each flavor prefers its own Release sibling
    # (build/macos-release-classic vs build/macos-release - see
    # sibling_release_tree in Util/orkige_export.py). The exported-app
    # contract is verified on the running tree's flavor.
    # APPLE-gated: the export driver checks/runs .app bundles with otool -
    # macOS host machinery (a Linux registration would hard-fail at otool).
    # TODO(linux): a Linux export target needs its own bundle layout +
    # driver checks in Util/orkige_export.py first.
    if(APPLE)
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
        # compile-on-Play tree) and ships IT instead of the generic player.
        # CLASSIC-only: native modules link the classic OGRE closure
        # (cmake/OrkigeGameModule.cmake FATAL_ERRORs on a next engine tree), so
        # this test registers only on the classic flavor - the exporter refuses
        # a native+next combo honestly.
        if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
            add_test(NAME export_macos_native
                COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                    --repo "${Orkige_SOURCE_DIR}"
                    --project "${Orkige_SOURCE_DIR}/projects/jumper-native"
                    --platform macos --engine-build "${CMAKE_BINARY_DIR}"
                    --output "${CMAKE_CURRENT_BINARY_DIR}/export/macos-native"
                WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        endif()

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
    # set_property/quit end to end (see player_debug_driver.cpp).
    # posix_spawn-based - excluded on Windows until the driver grows a
    # CreateProcess (or SDL process) backend.
    if(NOT WIN32)
        add_executable(player_debug_driver
            integration_driver/player_debug_driver.cpp)
        target_link_libraries(player_debug_driver PRIVATE Orkige::Core)
        add_test(NAME player_debug_pause
            COMMAND player_debug_driver $<TARGET_FILE:orkige_player>
                "${Orkige_SOURCE_DIR}/tests/scenes/falling_cube.oscene"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    endif()
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
foreach(integration_test demo_plain demo_mesh demo_physics demo_sound demo_music
        demo_spriteanim demo_spriteatlas demo_particles demo_vectorshape
        demo_ui_scale demo_ui_safe_area demo_textentry demo_ttf
        demo_synth_esc editor_selfcheck editor_resize editor_edittest
        editor_selfcheck_next editor_resize_next editor_edittest_next
        player_example_scene player_example_project player_vectorshape_scene
        player_debug_pause
        player_jumper_lua_selfcheck player_roller_selfcheck
        player_jumper_lua_selfcheck_next player_roller_selfcheck_next
        player_softbody_selfcheck player_softbody_selfcheck_next
        player_roller_progression_selfcheck
        player_roller_progression_selfcheck_next
        player_tween_selfcheck player_tween_selfcheck_next
        player_gamesupport_selfcheck player_gamesupport_selfcheck_next
        player_hotreload_selfcheck player_hotreload_selfcheck_next
        player_scriptprop_selfcheck player_scriptprop_selfcheck_next
        player_assetid_selfcheck player_assetid_selfcheck_next
        editor_play_stop editor_play_crash editor_project_play
        editor_play_script_error editor_play_hotreload
        editor_play_stop_next editor_play_crash_next editor_project_play_next
        editor_play_script_error_next editor_play_hotreload_next
        editor_project_native_play editor_project_native_play_break
        editor_play_simulator editor_play_simulator_boot
        editor_play_android editor_play_ios_device
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
        editor_project_play editor_play_script_error editor_play_hotreload
        editor_play_stop_next editor_play_crash_next editor_project_play_next
        editor_play_script_error_next editor_play_hotreload_next
        editor_play_simulator editor_play_android
        jumper_selfcheck player_jumper_lua_selfcheck player_roller_selfcheck
        player_jumper_lua_selfcheck_next player_roller_selfcheck_next
        player_softbody_selfcheck player_softbody_selfcheck_next
        player_roller_progression_selfcheck
        player_roller_progression_selfcheck_next
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
        editor_play_android editor_play_ios_device player_safearea_device)
    if(TEST ${device_test})
        # RESOURCE_LOCK: device tests share one simulator/emulator + adb -
        # they must not run concurrently when the suite runs with -j
        set_tests_properties(${device_test} PROPERTIES
            LABELS "integration;device" RESOURCE_LOCK mobile_device)
    endif()
endforeach()
