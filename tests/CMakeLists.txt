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

# python stdlib-only lint: walks every Util/*.py, parses its imports (ast) and
# fails on any import that is neither Python stdlib (sys.stdlib_module_names)
# nor a sibling Util module - the Util toolchain stays stdlib-only (no pip),
# and the shipped player never needs Python. Also staleness-gates the
# documented python3 >= 3.10 floor in CLAUDE.md. Pure source scan, headless,
# LABELS unit so it runs in the unit AND desktop presets.
add_test(NAME python_stdlib_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_python_stdlib.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(python_stdlib_lint PROPERTIES
    LABELS unit TIMEOUT 30)

# sprite-atlas packer self-validation: make_sprite_atlas.py packs a
# handful of synthetic sprites, re-parses its own .oatlas and asserts no region
# overlaps, all rects in bounds, deterministic order. Stdlib-only, headless -
# LABELS unit so it runs in the unit AND desktop presets.
add_test(NAME sprite_atlas_packer
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_sprite_atlas.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(sprite_atlas_packer PROPERTIES LABELS unit TIMEOUT 60)

# export-time texture cook self-validation: cook_textures.py resolves the
# format auto-table per platform/flavor (incl. web -> PNG and the classic
# GLES2 verdict), applies resize/premultiply/per-platform+web overrides,
# refuses impossible format pairs and refuses compression without an encoder;
# with the tree's texcook it also runs the real encode legs (dds/oitd/ktx
# containers, sidecar rename, offline mip chains). Stdlib-only + the texcook
# host tool - the cook-then-load smoke at the tooling level.
add_test(NAME cook_textures_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/cook_textures.py" --selftest
        $<TARGET_FILE:texcook>
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(cook_textures_selftest PROPERTIES LABELS unit TIMEOUT 120)

# the GPU texture encoder's own structural self-test: every format (BCn,
# ETC2, ASTC block sizes) encodes a non-block-aligned gradient and the
# dds/ktx/oitd container layouts verify (magic, header fields, payload
# sizes). The runtime-load proof is player_cooked_textures below.
add_test(NAME texcook_selftest
    COMMAND texcook --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(texcook_selftest PROPERTIES LABELS unit TIMEOUT 120)

# vector-shape SVG cook self-validation: cook_shapes.py flattens a synthetic
# SVG (rect + path + circle) to an .oshape and re-parses it, asserting
# well-formed regions with round-tripped fill colours and vertex counts.
# Stdlib-only, headless - the SVG->.oshape on-ramp at the tooling level.
add_test(NAME cook_shapes_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/cook_shapes.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(cook_shapes_selftest PROPERTIES LABELS unit TIMEOUT 60)

# vector-animation cook self-validation: cook_vector_anim.py cooks embedded
# Lottie fixtures covering every mapped feature (transforms/easing/parenting,
# path and colour keyframes, precomp inlining, densification, holes, markers,
# the static .oshape case) plus every out-of-subset error path, and re-cooks
# the committed round-trip fixture (tests/assets/vectoranim/) byte-identically
# - the same .oanim the cook_vector_anim_roundtrip unit test feeds through
# the REAL parser + evaluator. Stdlib-only, headless.
add_test(NAME cook_vector_anim_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/cook_vector_anim.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(cook_vector_anim_selftest PROPERTIES LABELS unit TIMEOUT 60)

# baked-mesh terrain generator self-validation: make_terrain_mesh.py builds a
# chunked terrain in memory and asserts mesh validity (indices in range, unit
# normals, finite UVs, chunk/sub-mesh counts), chunk-border seam matching,
# determinism (same seed -> byte-identical .glb + textures) and the 16-bit
# vertex-per-chunk budget cap. Stdlib-only, headless - the terrain pipeline at
# the tooling level.
add_test(NAME make_terrain_mesh_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_terrain_mesh.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_terrain_mesh_selftest PROPERTIES LABELS unit TIMEOUT 60)

# device-session prep self-validation: orkige_device.py drives the one-command
# phone loop (doctor readiness report + android/ios deploy-and-run). The
# selftest covers the pure parts - argument routing, doctor report assembly
# from injected probe facts (incl. the honest "never fail on absent signing"
# rule), the iOS env-seam status, and the android/ios command construction -
# with no real devices, subprocess or certificate. Stdlib-only, headless.
add_test(NAME orkige_device_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_device.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_device_selftest PROPERTIES LABELS unit TIMEOUT 60)

# water surface generator self-validation: make_water_mesh.py builds the shared
# engine water plane in memory and asserts mesh validity (flat +Y unit plane,
# UVs present for tangents, 16-bit indices in range), determinism (same seed ->
# byte-identical .glb + normal map) and that the tiling water normal map is
# seamless and +Z-dominant. Stdlib-only, headless - the water asset pipeline at
# the tooling level.
add_test(NAME make_water_mesh_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_water_mesh.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_water_mesh_selftest PROPERTIES LABELS unit TIMEOUT 60)

# benchmark showcase generator self-validation: make_benchmark_assets.py builds
# every scene through its pure writers and asserts the serialized shape (scene
# version marker, a director in each), the localisation key set, the vector
# blob contour and the two-clip vector-animation source - all in memory, no
# subprocesses. Stdlib-only, headless - the showcase content pipeline at the
# tooling level.
add_test(NAME make_benchmark_assets_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_benchmark_assets.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_benchmark_assets_selftest PROPERTIES LABELS unit TIMEOUT 60)

# tests/projects/integration fixture generator self-validation:
# make_integration_assets.py builds its three scenes into a scratch dir and
# asserts the emitted shape (the low-level path-bound ScriptComponent tag,
# the ParticleComponent field count) plus determinism across reruns - so a
# call-shape drift in the reused make_roller_assets writer helpers (like the
# script() (path) -> (kind, path) signature change that once broke this
# generator outright) fails here instead of silently. Stdlib-only, headless.
add_test(NAME make_integration_assets_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_integration_assets.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_integration_assets_selftest PROPERTIES LABELS unit TIMEOUT 60)

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

# site generator self-validation: make_help_portal.py renders a synthetic
# corpus (nested lists, indented fences inside list items, tables with pipe
# escapes, cross-page links) asserting the emitted HTML AND the whole REAL
# docs corpus - every page (landing, portal, legal) renders with balanced
# tags, ZERO broken internal links/anchors (a broken one is reported as
# file:line and fails - the gate that keeps main deployable to the public
# site), the legal pages are footer-linked everywhere but stay out of nav and
# search, the /api/ reference link is honoured (the one target the Pages
# workflow satisfies), the search index carries the expected sections and the
# sha256 stale-stamp skips/rebuilds correctly. Stdlib-only, headless.
add_test(NAME make_help_portal_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_help_portal.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_help_portal_selftest PROPERTIES LABELS unit TIMEOUT 60)

# the /api/ class-reference config (Docs/api/Doxyfile) actually renders: run
# the documentation tool over the real headers into a temp dir and assert a
# clean exit, known engine classes in the class list and the site-root +
# imprint/privacy footer links on the emitted pages. The tooling is CI-only
# (the Pages workflow installs it) - machines without it SKIP (exit 77).
add_test(NAME check_doxyfile
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_doxyfile.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(check_doxyfile PROPERTIES LABELS unit TIMEOUT 300
    SKIP_RETURN_CODE 77)

# doc-generator self-validation: update_docs.py parses the binding sources into
# a non-empty Lua API model, renders a budget-fitting index and asserts the .oui
# -> mermaid tree shape on a known layout. Stdlib-only, headless.
add_test(NAME update_docs_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/update_docs.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(update_docs_selftest PROPERTIES LABELS unit TIMEOUT 60)

# DOC CURRENCY GATE (the containment-lint precedent, now for generated docs):
# update_docs.py --check regenerates every generated doc section to memory and
# diffs it against the committed files (Docs/lua-api.md, Docs/gui.md, the
# embedded GeneratedLuaApi.h). A drift - a new/renamed Lua binding or gui widget
# whose generated block was not regenerated - fails here, so 'ctest before
# commit' now enforces that the docs track the code. Fix: run
# 'python3 Util/update_docs.py --write' and commit. Headless, LABELS unit.
add_test(NAME docs_currency
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/update_docs.py" --check
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(docs_currency PROPERTIES LABELS unit TIMEOUT 30)

# localisation tool self-validation: orkige_loc.py builds a fixture project in a
# tempdir and drives extract/merge/check/pseudo end to end - the .oui + Lua key
# extraction grammar, every merge-semantics row, placeholder <x/> round-trip,
# pseudo generation (font-safe accents, guards, expansion), write-twice
# determinism and the --check drift verdicts. Stdlib-only, headless.
add_test(NAME orkige_loc_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_loc.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_loc_selftest PROPERTIES LABELS unit TIMEOUT 60)

# LOCALISATION CURRENCY GATE (the docs_currency twin, now for the string
# registry): orkige_loc.py --check scans the seeded sample project for
# translatable keys (.oui @keys + Lua loc() calls) and fails if loc/en.xlf and
# the target files are out of sync - a missing / orphaned / stale key, a
# duplicate, or an inline-placeholder parity violation in any translation. Fix:
# run 'python3 Util/orkige_loc.py --write samples/hello_orkige' and commit.
# Headless, LABELS unit.
add_test(NAME loc_currency
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_loc.py" --check
            "${Orkige_SOURCE_DIR}/samples/hello_orkige"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(loc_currency PROPERTIES LABELS unit TIMEOUT 30)

if(TARGET orkige_engine)
    # pure-logic engine unit tests (no window/GPU, no flavor #ifdefs) - they
    # register on BOTH render flavors so the default (next) tree's unit gate
    # covers the engine layer too; every suite links only the backend-neutral
    # engine surface (RenderSystemSelectionTests exercises the pure
    # Engine::matchRenderSystemName matcher, GuiAtlasTests the headless
    # UiAtlas parser - gui runs on both flavors)
    add_subdirectory(engine)

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
    # boot_failure_clean_exit: a failed engine boot must unwind to a CLEAN
    # non-zero exit, never an uncaught throw. The driver forces a failure at each
    # staged point between window creation and the first frame (window creation,
    # the post-window engine region, resource-group init) and asserts the process
    # exits without a crash signal or a hang. Guards the classic MoltenVK boot
    # crash the CI hosts hit under contention. Classic-scoped: the window/post-
    # window failure seams live in the classic Engine bootstrapper (the next
    # flavor's EngineNext has no equivalent boot stages); the shared exception
    # guard in AppHost::setupEngine protects the next flavor too and is exercised
    # here through the resource-init stage. Skips (77) if the app is unbuilt.
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        add_test(NAME boot_failure_clean_exit COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_boot_failure_test.py"
            --binary "$<TARGET_FILE:hello_orkige>"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(boot_failure_clean_exit PROPERTIES
            LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 240)
    endif()

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

    # ORKIGE_DEMO_LUATEARDOWN: the shutdown-after-Lua-GUI-use acceptance test -
    # a script acquires widget + finder + engine-owned (render node, camera)
    # handles and holds them with NO cleanup, then the app tears down. The
    # CLEAN EXIT is the contract: the ScriptRuntime must finalize while the GUI
    # + render systems its userdata reach are still alive (the AppHost teardown
    # order). A widget/node destructor into a dead system is a SIGSEGV, so a
    # nonzero exit fails here. Flavor-neutral.
    add_test(NAME demo_luateardown COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luateardown PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LUATEARDOWN=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAORPHAN: the weak-handle orphan contract. Under option-C weak
    # handles Lua never owns a widget, so the old widget-outlives-its-layer
    # use-after-free cannot happen; the leg asserts the honest replacement - a live
    # handle works, and after the owning GuiManager is destroyed, touching the
    # still-held handle raises the catchable "widget handle is dead" error (never a
    # crash). Clean exit is the contract. Flavor-neutral (gui draws through DrawLayer2D).
    add_test(NAME demo_luaorphan COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luaorphan PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LUAORPHAN=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAHANDLE: the weak Lua widget-handle (option C) acceptance test.
    # findLabel hands a weak WidgetHandle; a leaf method, an inherited base method
    # and setParent (a widget-valued parameter) dispatch through it; a wrong-leaf
    # method on a LIVE widget errors distinctly; touching the handle after its
    # widget is destroyed raises the honest "widget handle is dead" script error at
    # the touching line (catchable, no crash). Flavor-neutral.
    add_test(NAME demo_luahandle COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luahandle PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LUAHANDLE=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAGOHANDLE: the GameObject/component weak-handle acceptance
    # test. world.get(id) and a ScriptComponent's self.gameObject /
    # self.transform hand Lua weak handles; a live handle dispatches, and the
    # SAME handle after its GameObject is destroyed raises the honest, catchable
    # "handle is dead (GameObject 'hero')" / "component handle is dead
    # (TransformComponent 'hero')" error at the touching line (never a crash,
    # never a silent no-op). Includes a self.gameObject tick-loop per-call cost
    # probe. Scripting-gated. Flavor-neutral (reads ids/flags/error strings).
    add_test(NAME demo_luagohandle COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luagohandle PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LUAGOHANDLE=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAHANDLE_SURFACE: the weak-handle completeness sweep. One live
    # GameObject carries every scriptable component; the probe script calls every
    # method the GameObject / per-component / layer handles bind and hard-fails on
    # any name that is not a bound function (a dropped/renamed binding is caught
    # here). The living spec of the whole handle surface. Flavor-neutral.
    add_test(NAME demo_luahandle_surface COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luahandle_surface PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LUAHANDLE_SURFACE=1;ORKIGE_DEMO_FRAMES=4")

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

    # ORKIGE_DEMO_LOC: the localisation-runtime selfcheck - loads a project's
    # XLIFF loc/ registry (en source + de target + the generated en-XA pseudo-
    # locale) into the StringTable, loads a committed .oui whose captions are all
    # @keys, and reads the RESOLVED widget text back: the source resolves the
    # English source, a live switch to de re-skins the screen from the target
    # (text differs and renders), and en-XA resolves an accented pseudo variant
    # that also renders. Flavor-neutral (gui @key routing on DrawLayer2D).
    add_test(NAME demo_loc COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_loc PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LOC=1;ORKIGE_DEMO_FRAMES=4")

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

    # ORKIGE_DEMO_GUI_FLOW: the screen-stack (screen-flow router) selfcheck.
    # Registers a title + settings screen (.oui) and drives title -> settings ->
    # back through the router with a synthetic Android-back key on the real input
    # path, asserting enter/exit transitions fire, a covered/popped screen's
    # widgets are torn down once its exit finishes, the back key pops the stack
    # and a root back falls through. Flavor-neutral (gui on DrawLayer2D), so it
    # runs on classic + next and both must agree.
    add_test(NAME demo_gui_flow COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_gui_flow PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_GUI_FLOW=1;ORKIGE_DEMO_FRAMES=4")

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

    # ORKIGE_DEMO_PARTICLES3D: the 3D particle + weather headed selfcheck - two
    # WORLD-space ParticleComponents (a rain preset: box volume above the scene,
    # fast fall, wind shear, velocity-stretched streak billboards; and a snow
    # preset: small flakes, slow drift + sinusoidal flutter), authored purely
    # through the reflected tunables. CPU-billboarded camera-facing quads, one
    # SpriteBatch draw per emitter. Asserts both emitters stay live inside the
    # mobile particle budget, that a hide/show toggle moves the frame-stats
    # triangle count by ~2 per live particle, and LOGS a measured per-frame
    # sim+billboard cost. Reads stats, not pixels - runs on both flavors.
    add_test(NAME demo_particles3d COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_particles3d PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_PARTICLES3D=1;ORKIGE_DEMO_FRAMES=120")

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

    # ORKIGE_DEMO_LIGHT: the LightComponent headed selfcheck - a GameObject
    # carries a LightComponent (its own child scene node + a live facade light);
    # asserts the component built the light, that its reflected parameters
    # (type/colour/intensity/range/spot angles/shadow flag) applied, that the
    # light follows the transform node, and that a serialize round-trip carries
    # every field. Reads component state, not pixels - runs on both flavors.
    add_test(NAME demo_light COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_light PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_LIGHT=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_MATERIAL: the `.omat` PBS-material selfcheck - a
    # ModelComponent imports the generated UV-mapped demo cube (textureless
    # baseline), takes demo_material.omat through its reflected material
    # reference (parse -> createMaterial -> setMaterial) and must then sample
    # the albedo map; a hide/show triangle-count probe proves the material
    # actually renders. Runs on both flavors: next = the full PBS datablock
    # (metal-rough + normal/emissive maps over generated tangents), classic =
    # its documented Blinn-Phong subset - a per-flavor probe, NOT a pixel
    # parity case (lit content legitimately differs between the flavors).
    add_test(NAME demo_material COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_material PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_MATERIAL=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_TERRAIN: the baked-mesh terrain selfcheck - a ModelComponent
    # imports the committed chunked terrain .glb (Util/make_terrain_mesh.py:
    # a 3x3 grid of geometry chunks -> 9 engine sub-meshes with normals + UVs)
    # and applies the generated tiling ground demo_terrain.omat across every
    # chunk through the reflected material reference; a hide/show triangle-count
    # probe proves the whole terrain renders. Runs on both flavors: next = the
    # PBS ground (normal-mapped over generated tangents), classic = its
    # Blinn-Phong subset - a per-flavor probe, NOT a pixel parity case.
    add_test(NAME demo_terrain COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_terrain PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_TERRAIN=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_WATER: the animated water-surface selfcheck - a WaterComponent
    # builds the shared engine water plane (water_plane.glb, UV-mapped so next
    # gets tangents) and a per-instance scrolling water material
    # (createWaterMaterial -> setMaterial), then a hide/show triangle-count
    # probe proves the transparent surface renders AND the per-frame scroll
    # clock advances (the setWaterTime ripple driver is ticked). Runs on both
    # flavors: next = PBS water (two scrolling detail normals + fresnel
    # transparency), classic = its transparent Blinn-Phong subset (flat tint +
    # scrolling shimmer) - a per-flavor probe, NOT a pixel parity case.
    add_test(NAME demo_water COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_water PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_WATER=1;ORKIGE_DEMO_FRAMES=120")

    # water_looks_right: the PIXEL probe demo_water's triangle count could not be
    # (the fog-leg lesson - a near-black slab passed "renders"). Captures the
    # water at two frames and asserts it is LIT (not near-black), has a SPECULAR
    # sun GLINT in the centre-bottom reflection (an unlit/flat slab has ~0 glint
    # contrast - the exact next-water regression), and SCROLLS (the ripple moves
    # between frames). Both flavors (the demo carries a directional sun so PBS
    # water has something to light). Skips (77) if the app is unbuilt.
    add_test(NAME water_looks_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_water_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/water_looks_right"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(water_looks_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 240)

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

	# The two human preview tabs, end to end. Opens the benchmark project,
	# double-click-routes a real Lottie character and .oui through the same
	# request seams as the Assets panel, verifies both dock beside Scene, and
	# proves their live texture IDs occur in ImGui's submitted draw data. The
	# animation path also checks its paused upload is stable and a changed frame
	# defers display until the next upload-ring handoff. GUI Preview needs offscreen-owned
	# layers, which is an Ogre-Next capability; classic is covered by its honest
	# unsupported-path assertion in editor_control.
	if(ORKIGE_RENDER_BACKEND STREQUAL "next")
		add_test(NAME editor_previews_next COMMAND orkige_editor
			WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
		set_tests_properties(editor_previews_next PROPERTIES
			ENVIRONMENT "ORKIGE_EDITOR_PREVIEW_SELFCHECK=${Orkige_SOURCE_DIR}/projects/benchmark;ORKIGE_DEMO_FRAMES=90")
	endif()

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

    # Editor chrome visual evidence: boot the editor in each theme at the run's
    # native display density and dump a whole-window PNG under a stable build
    # path (build/<preset>/tests/visual/editor_<theme>[_next].png). Every suite
    # run leaves a reviewable capture of both looks - no golden compare, but the
    # wrapper fails if the editor cannot boot or produce the shot. The two
    # themes run as separate processes (one variant per boot, before the atlas).
    foreach(_visualTheme dark light)
        add_test(NAME editor_visual_${_visualTheme}${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND ${CMAKE_COMMAND}
                -DEDITOR_EXE=$<TARGET_FILE:orkige_editor>
                "-DOUT_PNG=${CMAKE_CURRENT_BINARY_DIR}/visual/editor_${_visualTheme}${ORKIGE_FLAVOR_TEST_SUFFIX}.png"
                -DTHEME=${_visualTheme}
                -P "${Orkige_SOURCE_DIR}/Util/run_editor_visual_shot.cmake"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_visual_${_visualTheme}${ORKIGE_FLAVOR_TEST_SUFFIX}
            PROPERTIES LABELS integration TIMEOUT 90)
    endforeach()

    # live theme switching: drive the runtime re-apply path (the settings
    # toggle / OS-appearance change) between Dark, Light and System and assert
    # the active variant follows - guards the reapply lambda + the accessor.
    add_test(NAME editor_theme_switch${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_theme_switch${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_THEME_SWITCH=1;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 90)

    # editor SCRIPT TOOLS end to end: on a temp copy of the roller project,
    # write fixture *.editor.lua tools and run them through the EditorScriptHost -
    # asserting discovery (+ the -- tool: label), the editor.* verb surface
    # (create/set/reparent authoring a subtree), the ONE-undo-step contract, the
    # error-path rollback (a failing tool reports file:line and leaves no partial
    # edits) and the shipped border_walls sample. In ORKIGE_SCRIPTING=OFF builds
    # it takes the noscript leg (project loads, running is an honest no-op). No
    # Play needed - pure edit-world. Both flavors. Frame budget is the backstop.
    add_test(NAME editor_scripts${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_scripts${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_SCRIPTTEST=${Orkige_SOURCE_DIR}/projects/roller;ORKIGE_DEMO_FRAMES=120"
        LABELS integration TIMEOUT 120)

    # prefab EDIT MODE end to end: on a temp copy of the roller project, author
    # a tile instance with a per-instance override, then drive the isolation
    # stage through the same free functions the UI calls - open (the one world
    # swaps to the prefab subtree, undo scope resets, scene/project lifecycle +
    # Play/Paint guards refuse), edit a child property + add an object under
    # the root, the save-guard refusals (stray root, deleted root), save, a
    # dirty close with the explicit Discard policy, and the restored scene:
    # the instance refreshed with the prefab edit while its per-instance
    # override survived, selection/path/dirty restored, the temp snapshot
    # deleted. Single-shot, condition-driven. Both flavors.
    add_test(NAME editor_prefab_edit${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_prefab_edit${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_PREFABEDIT=${Orkige_SOURCE_DIR}/projects/roller;ORKIGE_DEMO_FRAMES=120"
        LABELS integration TIMEOUT 120)

    # editor safety + convenience end to end (no Play, pure edit-world): on a
    # temp loose scene, a clean save clears the dirty flag and leaves no
    # autosave; a dirty edit autosaves to the ".autosave" sibling (never the
    # real file, dirty preserved); a clean save removes the autosave and writes
    # a ".bak"; an automated-run open with a NEWER autosave auto-discards it
    # silently (no recovery modal) and loads the real scene; and a component
    # copy/paste across two objects matches values in ONE undoable step. Both
    # flavors. Frame budget is the backstop.
    add_test(NAME editor_autosave${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_autosave${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_AUTOSAVETEST=1;ORKIGE_DEMO_FRAMES=120"
        LABELS integration TIMEOUT 120)
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

    # one-time layout-defaults migration: synthesize an ini from before the
    # panel-defaults rework (Tile Palette + GUI Preview open, no version stamp)
    # and verify the migration closes both, flags the palette re-dock, stamps +
    # persists the version, and is idempotent on a re-load. Pure settings IO -
    # exits before the render boot.
    add_test(NAME editor_migrate${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_migrate${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_MIGRATE_TEST=1"
        LABELS integration TIMEOUT 60)

    # 2D editing-ergonomics: paint three tiles in 2D mode, then drive the REAL
    # scene-panel mouse path with SYNTHETIC SDL events - a rubber-band marquee
    # selects all three tiles, a plain click selects exactly one (no marquee), a
    # Cmd-drag EXTENDS the selection - plus the ghost-preview handle is non-null
    # while a sprite prefab is armed and the drop-cell paint is undoable. Both
    # flavors. Deadline-driven; the TIMEOUT is a coarse backstop.
    add_test(NAME editor_marquee${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_marquee${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_MARQUEE=${Orkige_SOURCE_DIR}/projects/roller;ORKIGE_DEMO_FRAMES=150"
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

    # Play in Browser end to end over MCP: the editor hosts its endpoint on an
    # ephemeral in-process port and, on a worker thread, drives the browser
    # target - list_play_targets (the "browser" entry, gated vs ready), play
    # {target:"browser"} on projects/jumper-lua (a real "web" export through
    # the async export machinery), poll get_state to "serving", then GET the
    # served index.html AND the wasm module (Content-Type: application/wasm -
    # streaming compilation requires it) off the editor's loopback static
    # server, and prove the path jail 404s an escape. The live-session side is
    # covered by its degradation half here: NO page ever opens, so the waiting
    # session must time out back to edit mode (the env shrinks that timeout),
    # serving must continue, and a late debug upgrade must be refused with a
    # 409 - the standalone flow stays honest when no debug link forms. SKIPPED
    # (exit 77) when the web-release preset never built the wasm player - the
    # target honestly reports "gated" then. Pixel-level proof of the SERVED
    # game lives in the web tree's export_web_boot (a headless browser boots
    # the same artifact); the CONNECTED session lives in
    # editor_play_browser_session below.
    add_test(NAME editor_play_browser${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_browser${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_CONTROL_BROWSERTEST=${CMAKE_CURRENT_BINARY_DIR}/editor_play_browser_shot.png;ORKIGE_BROWSER_CONNECT_TIMEOUT_MS=4000"
        LABELS integration TIMEOUT 300
        SKIP_RETURN_CODE 77)

    # Play in Browser as a LIVE debug session: the same MCP-driven front half
    # (open projects/jumper-lua, play {target:"browser"}, poll to "serving"),
    # then the editor's worker spawns a REAL headless Chrome/Chromium at the
    # served URL - the page dials the debug link back into the waiting
    # session over the serve port's WebSocket upgrade - and drives the whole
    # loop over the live link: get_state reaches "connected"/playing, a
    # [remote] log line lands in console_tail, runtime_hierarchy lists the
    # game's objects, pause/resume round-trips, screenshot_game and
    # reload_script refuse honestly (a page's in-memory files never reach the
    # editor's disk; the page runs its packaged snapshot), and stop quits the
    # page's game over the link (the closing socket confirms it) while
    # serving continues. SKIPPED (exit 77) without the wasm player OR without
    # a headless browser (discovery mirrors tests/web/run_export_web.py;
    # ORKIGE_CHROME overrides).
    add_test(NAME editor_play_browser_session${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_browser_session${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        # coarse backstop only: export + a COLD headless-browser boot of a
        # multi-megabyte wasm module + the polled session legs, each with its
        # own attempt-capped budget inside the self-test - the wall must sit
        # above their summed worst case on a loaded machine
        ENVIRONMENT "ORKIGE_EDITOR_CONTROL_BROWSERSESSION=${CMAKE_CURRENT_BINARY_DIR}/editor_play_browser_session_shot.png"
        LABELS integration TIMEOUT 600
        SKIP_RETURN_CODE 77)

    # Help > "Orkige Help": the hook fires the menu action's request flag
    # and asserts the frame loop resolved the published documentation URL
    # (HELP_PORTAL_URL). Deliberately reduced to a wiring + gating check:
    # the site is CI-deployed (never generated or served by the editor), so
    # the automated run must open NO browser and touch NO network - the
    # site's content is gated by make_help_portal_selftest instead.
    add_test(NAME editor_help_portal${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_help_portal${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_HELPTEST=1"
        LABELS integration TIMEOUT 60)

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

    # .oui hot-reload, the full editor->player path: the hook copies
    # tests/projects/uihotreload to a temp dir (the real fixture is never
    # touched), presses Play (a script loads assets/hud.oui - a positioned
    # label), then overwrites the .oui on disk. The editor's .oui watcher must
    # detect the change, send MSG_RELOAD_UI, emit a [reload] Console line, and
    # the running player must destroy-and-rebuild the screen so the label's rect
    # moves (observed over MSG_UI_LAYOUT / the remoteUiLayout stream). Then a
    # broken (unparseable) .oui must surface a [remote] error while the OLD
    # (moved) screen stays up, and Stop must revert cleanly. Loading an .oui
    # needs a live script, so it is LUA-gated (the watcher/protocol are neutral).
    if(ORKIGE_SCRIPTING STREQUAL "LUA")
        add_test(NAME editor_ui_hotreload${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_ui_hotreload${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_EDITOR_UI_HOTRELOAD_PLAYTEST=${Orkige_SOURCE_DIR}/tests/projects/uihotreload")
    endif()

    # vector-animation hot-reload, the full editor->player path: the hook
    # copies tests/projects/animhotreload to a temp dir, OPENS it (the
    # cooked-pair drift scan must re-cook the fixture's deliberately drifted
    # probe pair and leave the record-less legacy pair byte-untouched),
    # presses Play, then edits assets/probe.json on disk. The editor's
    # animation watcher must re-cook the pair (the [import] summary carries
    # the renamed clip) and send MSG_RELOAD_ANIM so the running player
    # rebuilds the rig; a directly-edited source-less orphan.oanim reloads
    # without a cook; a BROKEN source fails the cook honestly (error line, no
    # reload, play continues) and a broken .oanim is refused by the player's
    # parse-before-swap ([remote] error, old rig keeps playing). Not
    # script-gated: the rig components play without Lua.
    add_test(NAME editor_anim_hotreload${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_anim_hotreload${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_ANIM_HOTRELOAD_PLAYTEST=${Orkige_SOURCE_DIR}/tests/projects/animhotreload")

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
        # NOTE the timeout is set at the simulator-headroom block at the end
        # of this file - properties are last-write-wins, a TIMEOUT here would
        # be silently shadowed

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
    # and adb install it. The host-side flow is portable across macOS and
    # Linux; Linux CI drives an x86_64 emulator while shipping builds remain
    # arm64-v8a.
    if(UNIX)
        add_test(NAME editor_play_android COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_play_android PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_PLAYTEST=stop;ORKIGE_EDITOR_PLAY_ANDROID=auto"
            SKIP_RETURN_CODE 77)

        # device ROTATION on Android: play tests/projects/rotation (its
        # manifest opts into every orientation - export.orientation=auto,
        # delivered to the device as the --orientation launch argument) on an
        # adb device/emulator, force a landscape rotation through the system
        # user_rotation setting, and assert over the debug protocol that the
        # drawable dimensions swapped orientation while the player kept
        # rendering (MSG_STATS carries the flipped size), then rotate back and
        # require the original size again. The device's rotation settings are
        # restored on every exit path. SKIPPED (exit 77) when no adb device
        # has the player APK installed (same preparation as
        # editor_play_android).
        add_test(NAME player_rotation_android COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_rotation_android PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_ROTATION_CHECK=${Orkige_SOURCE_DIR}/tests/projects/rotation;ORKIGE_EDITOR_PLAY_ANDROID=auto"
            SKIP_RETURN_CODE 77)
    endif()

    if(APPLE)
        # device ROTATION on iOS: the same run against a booted iPhone
        # simulator - Simulator.app's Device menu is the only scriptable
        # rotation, driven over osascript (a LOCAL-ONLY affordance: without
        # the Automation/Accessibility permission the run SKIPS, exit 77,
        # after a clean stop - never a false red on CI). Asserts the Metal
        # drawable swapped orientation over MSG_STATS AND that the notch
        # safe-area inset moved to a side edge in landscape, then rotates
        # back. Same preparation as editor_play_simulator (a booted simulator
        # with OrkigePlayer.app installed, or a shutdown one + the built app).
        add_test(NAME player_rotation_ios COMMAND orkige_editor
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_rotation_ios PROPERTIES
            ENVIRONMENT
                "ORKIGE_EDITOR_ROTATION_CHECK=${Orkige_SOURCE_DIR}/tests/projects/rotation;ORKIGE_EDITOR_PLAY_SIMULATOR=auto"
            SKIP_RETURN_CODE 77)
    endif()

    if(APPLE)
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
        # registrations carry a _next suffix. gui renders on both flavors,
        # so the UI assertions run everywhere (game.lua still probes
        # engine:hasUISystem() so a future UI-less flavor degrades honestly)
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

        # VECTOR (Lottie) ANIMATION RIGS, end to end: projects/vectorshapes
        # scenes/vectoranim.oscene - a two-blob hero (assets/hero.json cooked to
        # hero.oanim) whose VectorAnimationComponent plays an `idle` clip (the
        # composed pose moves as frames advance), a script crossFades it to the
        # one-shot `hop`, and the hop's completion fires the ended event into the
        # script (shared.heroanim). The player also logs the MEASURED per-frame
        # evaluate+tessellate cost per character (the mobile budget). Runs on
        # both render flavors (the pose evaluation is renderer-neutral; the
        # upload goes through the VectorMesh dynamic path on each backend). See
        # the ORKIGE_VECTORANIM_SELFCHECK block in tools/player/main.cpp.
        add_test(NAME player_vectoranim_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player projects/vectorshapes/scenes/vectoranim.oscene
                --project projects/vectorshapes
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_vectoranim_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_VECTORANIM_SELFCHECK=1")

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

        # the gameplay-conveniences pack (Lua `timer` / `game` tables +
        # music.crossFade + CameraComponent smooth follow), end to end against
        # the live player wiring (synchronous, any scene - reuses
        # tests/projects/tween): timer.after fires once / timer.every repeats /
        # timer.cancel stops one (ticked through the loop's TimerManager),
        # game.setState round-trips + fires game.stateChanged to an events
        # subscriber, music.crossFade ramps the incoming track to full while the
        # outgoing one is stopped (riding ONE core_tween tween), and a
        # CameraComponent in ortho + FM_WIDTH fit eases onto a moved target
        # (follow composes with the fit policy). See the
        # ORKIGE_GAMEPLAY_SELFCHECK block in tools/player/main.cpp; the timer
        # auto-cancel, follow and crossfade-gain math are the unit tests.
        add_test(NAME player_gameplay_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/tween
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_gameplay_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_GAMEPLAY_SELFCHECK=1")

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

        # window-resize plumbing, end to end - the same path a device rotation
        # takes (drawable size change -> SDL resize event -> the player's
        # notifyWindowResized -> swapchain/drawable recreate + window-camera
        # aspect re-derive): the check requests a smaller host window and
        # requires the render system to report the new drawable size with the
        # camera aspect matching it (the stale-aspect/stretched-image guard;
        # see the ORKIGE_RESIZE_SELFCHECK block in tools/player/
        # PlayerSelfChecks.cpp). Runs on BOTH flavors; desktop-only - a mobile
        # window ignores size requests, rotation itself is covered by the
        # device-labeled player_rotation_* tests.
        add_test(NAME player_resize_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player samples/scenes/example.oscene
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_resize_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_RESIZE_SELFCHECK=1"
            SKIP_RETURN_CODE 77)

        # performance instruments, end to end: run the roller project (scripts,
        # physics, sprites, HUD all live) for ~70 frames and assert the
        # profiler snapshot carries the canonical tick phases at depth 0, the
        # frame boundary folded the counters and a frame duration was measured
        # (see the ORKIGE_PERF_SELFCHECK block in tools/player/main.cpp). The
        # measured numbers are logged, never gated - no wall-clock thresholds.
        # Runs on BOTH flavors (the instruments are renderer-neutral).
        add_test(NAME player_perf_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/roller
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_perf_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_PERF_SELFCHECK=1;ORKIGE_PROGRESS_RESET=1;ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/perf_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}")

        # per-scene benchmark recorder, end to end: run the roller project armed
        # (ORKIGE_BENCHMARK) for ~90 frames, then finalize and assert the JSONL
        # results artifact exists and parses with a meta line, a scene record
        # carrying frames>0 and a measured frameMs.avg, and a clean summary (see
        # the ORKIGE_BENCHMARK_SELFCHECK block in tools/player/main.cpp). The
        # artifact is isolated into the build tree via ORKIGE_BENCHMARK_DIR.
        # Runs on BOTH flavors: the recorder is renderer-neutral and the classic
        # 2D-composite fix makes the armed run robust on classic too.
        add_test(NAME player_benchmark_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/roller
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_benchmark_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_BENCHMARK_SELFCHECK=1;ORKIGE_BENCHMARK=1;ORKIGE_BENCHMARK_DIR=${CMAKE_CURRENT_BINARY_DIR}/benchmark_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_PROGRESS_RESET=1;ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/benchmark_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}"
            LABELS integration TIMEOUT 120)

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

        # the benchmark showcase (projects/benchmark), end to end: the driver
        # runs the standalone player over the WHOLE autonomous scene sequence,
        # armed (ORKIGE_BENCHMARK) and driven fast + deterministically (a tiny
        # benchmark.sceneScale via ORKIGE_CVARS, no wipe, ORKIGE_DEMO_FRAMES
        # capping past a full loop), then asserts the JSONL results artifact
        # parses and carries EVERY scene of the tour by its recorder label plus
        # a clean summary. The director advances the sequence with no input, so
        # a complete artifact + clean exit is the whole contract. Artifact is
        # isolated into the build tree.
        #
        # Runs on BOTH flavors: the long showcase SEQUENCE used to trip the
        # classic GL/RTSS backend (the 2D-overlay materials share the RTSS
        # scheme, so a dynamic light-count change or a scene-teardown material
        # churn transiently drops their generated technique - the DrawLayer2D
        # composite then dereferenced a null best technique). The classic 2D
        # composite now recompiles-or-skips on that transient, so the whole
        # tour runs cleanly on classic too.
        add_test(NAME player_benchmark_vista${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_vista${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_benchmark_vista${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 240)
        # the SAME traversal through the interactive fade-wipe switch (the
        # path a human playing the project sees; the bare-switch run above
        # cannot catch a wipe-path regression - the two switches must both
        # advance the level sequence)
        add_test(NAME player_benchmark_vista_wipe${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_vista_wipe${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --wipe 1 --frames 2600
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_benchmark_vista_wipe${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 240)
        # the static mobility flag's whole contract in one run, per flavor
        # (projects/benchmark scenes/fixture_static.oscene): toggle identity
        # (r.staticScene on/off renders the same pixels - byte-exact on next,
        # a tight StaticGeometry rounding tolerance on classic), the classic
        # draw-call win, and the mobility contract (a runtime move of a
        # static object warns EXACTLY once, lands correctly through the
        # repair path, and demotes with the expected flavor batch delta).
        add_test(NAME player_static_contract${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_static_contract_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/static_contract${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --flavor "${ORKIGE_RENDER_BACKEND}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_static_contract${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 240)

        # sprite-run batching end to end, per flavor (projects/benchmark
        # scenes/fixture_sprites.oscene, exact known grouping): the realized
        # run count and draw-batch counts are pinned EXACTLY (a silent
        # grouping regression shifts them), the live r.spriteBatching escape
        # hatch must release and re-form the runs, a moved member re-uploads
        # its run, and batching on/off renders identical pixels (byte-exact
        # on classic; a 1-LSB edge-rounding fringe allowed on next).
        add_test(NAME player_spritebatch${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_spritebatch_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/spritebatch${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --flavor "${ORKIGE_RENDER_BACKEND}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_spritebatch${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 240)

        # the standing structural budget gate: every benchmark scene boots
        # deterministically and its draw-batch count must land INSIDE the
        # checked-in per-scene x per-flavor corridor (above = a batching/
        # facade regression; dramatically below = something stopped
        # rendering), with a triangle ceiling on top. Frame-ms is reported,
        # never gated. Budgets update deliberately - edit
        # integration_driver/benchmark_budgets.json in the SAME commit as
        # the change that moves a number (Docs/performance.md).
        add_test(NAME benchmark_budget${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_budget_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_budget${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --flavor "${ORKIGE_RENDER_BACKEND}"
                --budgets "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/benchmark_budgets.json"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(benchmark_budget${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 600)

        # the vista must also LOOK right: measured pixel discriminators over a
        # captured frame - HUD glyph presence (per-frame-dirty gui batches
        # reach the frame) and a daylit green-over-blue terrain band (the
        # atmosphere-linked sun samples ABOVE the horizon). Catches the wrong-
        # render classes the artifact test cannot see, per flavor.
        add_test(NAME player_benchmark_pixels${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_pixel_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_pixels${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_benchmark_pixels${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 240)
        # per-vignette look probes (run_benchmark_scene_probe.py): direct scene
        # boots with the stress ramp forced to its ceiling and the wall-time
        # camera orbit frozen (deterministic frames on any machine). lumens =
        # readable moonlit night + dark dome + visible lamp pools (the sun-
        # exposure linkage and the point-light path per flavor); field = the
        # ramped instance cubes render LIT (the black-cube regression class);
        # hud2x = the HUD title/stats rows stay vertically disjoint at a
        # simulated 2x display density (the overlap regression class);
        # flatland = the flat-colour 2D shapes paint OVER the full-screen
        # backdrop (the 2D painter-order/render-queue regression that washed
        # them out on classic + web).
        foreach(ORKIGE_SCENE_PROBE lumens field hud2x flatland)
            add_test(NAME player_benchmark_${ORKIGE_SCENE_PROBE}_probe${ORKIGE_FLAVOR_TEST_SUFFIX}
                COMMAND python3
                    "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_scene_probe.py"
                    --repo "${Orkige_SOURCE_DIR}"
                    --player "$<TARGET_FILE:orkige_player>"
                    --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_${ORKIGE_SCENE_PROBE}_probe${ORKIGE_FLAVOR_TEST_SUFFIX}"
                    --probe "${ORKIGE_SCENE_PROBE}"
                WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
            set_tests_properties(player_benchmark_${ORKIGE_SCENE_PROBE}_probe${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
                LABELS integration TIMEOUT 240)
        endforeach()
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

    # cooked-texture runtime-load proof: the driver runs the REAL export
    # texture cook (cook_textures.py + this tree's texcook encoder) over a
    # copy of the asset_rename fixture and boots the player on the cooked
    # payload - the sprite must render from the block-compressed container
    # (.dds), reached through BOTH reference shapes (asset id resolving to
    # the renamed file, and a bare id-less name through the backend's
    # cooked-extension fallback). The mobile ASTC/ETC2 cooks are asserted
    # structurally (.oitd shape) on the next flavor - a desktop host cannot
    # boot them (the next desktop renderer maps ASTC/ETC2 only in its mobile
    # builds); their load proof rides the simulator/emulator device tests.
    add_test(NAME player_cooked_textures${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_cooked_textures_test.py"
            --repo "${Orkige_SOURCE_DIR}"
            --player "$<TARGET_FILE:orkige_player>"
            --texcook "$<TARGET_FILE:texcook>"
            --flavor "${ORKIGE_RENDER_BACKEND}"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_cooked_textures${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        LABELS integration TIMEOUT 300)

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
        # platform's preset tree (or the Android build-tools) is absent.
        # The mobile preset trees are flavor-suffixed like the desktop ones -
        # each flavor's tree checks the export of ITS OWN mobile player.
        if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
            set(ORKIGE_MOBILE_TREE_SUFFIX "-classic")
        else()
            set(ORKIGE_MOBILE_TREE_SUFFIX "")
        endif()
        add_test(NAME export_ios_simulator
            COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform ios-simulator
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-lua"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_ios_simulator PROPERTIES SKIP_RETURN_CODE 77)

        add_test(NAME export_android
            COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform android
                --engine-build "${Orkige_SOURCE_DIR}/build/android-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-lua"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_android PROPERTIES SKIP_RETURN_CODE 77)

        # the RELEASE App Bundle (.aab) path: asserts the proto bundle MODULE
        # structure (the bundletool-free, keystore-free slice). A real signed
        # .aab needs bundletool + a release keystore (machine-local secrets the
        # exporter gates on, exercised cert-free by its --selftest); the driver
        # passes --aab-unsigned-module. SKIPPED (77) without the android tree /
        # build-tools, like export_android.
        add_test(NAME export_android_aab
            COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform android-aab
                --engine-build "${Orkige_SOURCE_DIR}/build/android-debug"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-aab-lua"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_android_aab PROPERTIES SKIP_RETURN_CODE 77)
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
        demo_spriteanim demo_spriteatlas demo_particles demo_particles3d demo_vectorshape
        demo_light demo_material demo_terrain demo_ui_scale demo_ui_safe_area demo_textentry demo_ttf
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
        player_gameplay_selfcheck player_gameplay_selfcheck_next
        player_hotreload_selfcheck player_hotreload_selfcheck_next
        player_scriptprop_selfcheck player_scriptprop_selfcheck_next
        player_assetid_selfcheck player_assetid_selfcheck_next
        editor_play_stop editor_play_crash editor_project_play
        editor_play_script_error editor_play_hotreload editor_ui_hotreload
        editor_anim_hotreload
        editor_play_stop_next editor_play_crash_next editor_project_play_next
        editor_play_script_error_next editor_play_hotreload_next
        editor_ui_hotreload_next editor_anim_hotreload_next
        editor_project_native_play editor_project_native_play_break
        editor_play_simulator editor_play_simulator_boot
        editor_play_android editor_play_ios_device
        player_resize_selfcheck player_resize_selfcheck_next
        player_rotation_android player_rotation_ios
        export_macos_lua export_macos_native
        export_ios_simulator export_android export_android_aab
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
        editor_ui_hotreload editor_ui_hotreload_next
        editor_anim_hotreload editor_anim_hotreload_next
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

# the render-facade conformance suite decodes its screenshot once per pixel
# probe, and the atmosphere exposure/fog legs put the honest runtime past the
# blanket 60s - a coarse backstop, not a perf bar
if(TEST render_facade_selfcheck)
    set_tests_properties(render_facade_selfcheck PROPERTIES TIMEOUT 240)
endif()

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

# the simulator tests may have to boot a simulator (+ install the app)
# before the player even starts - and a hosted CI runner boots even a warm
# simulator in 4-5 minutes, so the boot-path test's budget must clear the
# editor's own 540s in-test boot deadline (this is the LAST property write
# for these tests and therefore the authoritative timeout)
if(TEST editor_play_simulator)
    set_tests_properties(editor_play_simulator PROPERTIES TIMEOUT 600)
endif()
# the rotation runs add two device rotations + their stats round-trips on top
# of the play flow; the iOS one may additionally have to boot a simulator
# first (same headroom reasoning as editor_play_simulator)
if(TEST player_rotation_android)
    set_tests_properties(player_rotation_android PROPERTIES TIMEOUT 300)
endif()
if(TEST player_rotation_ios)
    set_tests_properties(player_rotation_ios PROPERTIES TIMEOUT 600)
endif()
if(TEST editor_play_simulator_boot)
    # covers the editor's whole in-test chain: sim prep (900s) + debug-link
    # connect (90s) + the playtest phase slack - see EditorApp.h budgets
    set_tests_properties(editor_play_simulator_boot PROPERTIES TIMEOUT 1400)
endif()

# device tests boot iOS simulators / Android emulators - by far the slowest
# part of a full suite run (up to minutes when a device cold-boots). They
# carry the extra "device" label so the default development preset
# (ctest --preset desktop) can exclude them; run them via ctest --preset all
# when deploy/device code changed or before handing work over.
foreach(device_test editor_play_simulator editor_play_simulator_boot
        editor_play_android editor_play_ios_device player_safearea_device
        player_rotation_android player_rotation_ios)
    if(TEST ${device_test})
        # RESOURCE_LOCK: device tests share one simulator/emulator + adb -
        # they must not run concurrently when the suite runs with -j
        set_tests_properties(${device_test} PROPERTIES
            LABELS "integration;device" RESOURCE_LOCK mobile_device)
    endif()
endforeach()

# LaunchServices can abort while registering two copies of the same macOS app
# bundle concurrently (the desktop presets intentionally run several tests in
# parallel). Keep unrelated unit/player work parallel, but serialize every
# test in this directory whose name denotes an editor-app process. The lock is
# a CTest scheduling resource, so it has no effect on normal editor launches or
# on other platforms.
if(APPLE AND TARGET orkige_editor)
    get_property(_orkige_registered_tests DIRECTORY PROPERTY TESTS)
    foreach(_orkige_test IN LISTS _orkige_registered_tests)
        if(_orkige_test MATCHES "^editor_")
            set_property(TEST "${_orkige_test}" APPEND PROPERTY
                RESOURCE_LOCK orkige_editor_app)
        endif()
    endforeach()
    unset(_orkige_registered_tests)
    unset(_orkige_test)
endif()
