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

# portability lint (Docs/render-abstraction.md): mechanical guard for two
# Windows-only compile hazards invisible to a local clang/libc++ build -
# identifiers colliding with windef.h macros (near/far/small/interface) and
# curated std symbols used without their header directly included (libc++
# leaks the header transitively, MSVC does not). Pure source scan, headless,
# LABELS unit so it runs in the unit AND desktop presets.
add_test(NAME portability_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_portability.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(portability_lint PROPERTIES
    LABELS unit TIMEOUT 120)

# portability lint's own self-test: runs both checks over embedded fixtures
# (each hazard class caught, each legit pattern passes, suppression works).
add_test(NAME portability_lint_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_portability.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(portability_lint_selftest PROPERTIES
    LABELS unit TIMEOUT 120)

# doctrine lint: four source-hygiene gates the project brief mandates -
# meta-macro containment (raw ORKIGE_LUA/ORKIGE_NOSCRIPT only in the meta
# seam), banned third-party product names (comments/strings/docs), the
# core+engine filesystem funnel gate (raw fs only through the sanctioned
# backlog) and the core+engine copyright header block. Each sub-check has its
# own scope + suppression section in Util/doctrine_lint.json (the
# ogre_containment model). Pure source scan, headless, LABELS unit so it runs
# in the unit AND desktop presets.
add_test(NAME doctrine_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_doctrine.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(doctrine_lint PROPERTIES
    LABELS unit TIMEOUT 120)

# third-party notices currency gate: several licenses in the statically linked
# closure require their text to travel with the BINARY, so a dependency added
# to vcpkg.json or ports/ has to be answered in THIRD-PARTY-NOTICES.md in the
# same change - with a license section, or with a written reason why it reaches
# no shipped artifact. Pure source scan, headless, LABELS unit.
add_test(NAME third_party_notices_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_third_party_notices.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(third_party_notices_lint PROPERTIES
    LABELS unit TIMEOUT 30)

# ...and its own self-test: fixture trees prove the gate flags a new dependency
# with no answer, honours the build-time-only and pending-text exemptions, and
# refuses a section that carries no license text.
add_test(NAME third_party_notices_lint_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_third_party_notices.py"
        --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(third_party_notices_lint_selftest PROPERTIES
    LABELS unit TIMEOUT 30)

# doctrine lint's own self-test: embedded fixture trees prove every sub-check
# passes on a clean tree, flags its violation and honours its suppression -
# no real source touched. Stdlib-only, headless.
add_test(NAME doctrine_lint_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_doctrine.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(doctrine_lint_selftest PROPERTIES
    LABELS unit TIMEOUT 120)

# doc-link lint: every doc the CODE points a person at must exist. The help
# portal's own generator fails on a broken link BETWEEN docs, which is what
# keeps the corpus honest - but it cannot see a reference inside a C++ string
# (a refusal that ends by naming a doc, or a help page name the editor composes
# a URL from). Rename that doc and the portal stays green while a shipped binary
# carries a dead pointer. This resolves both shapes against Docs/ on disk.
# NO NETWORK: a page exists on the published site exactly when its doc exists
# in the corpus, so the site is never a test dependency. Stdlib-only, headless.
add_test(NAME doc_link_lint
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/check_doc_links.py"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(doc_link_lint PROPERTIES
    LABELS unit TIMEOUT 120)

# The cross-flavor parity drivers' own self-tests. Each driver compares two
# flavors that a single machine does not always hold, so each carries a
# captured-output road beside its run-both-binaries road - and the decision
# that matters is what happens when a capture is absent, empty or unreadable:
# it FAILS. A parity gate that compares nothing must never report parity, so
# every refusal is asserted here, on fixtures, with no window and no GPU.
add_test(NAME render_parity_driver_selftest
    COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/compare_backend_screenshots.py"
        --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(render_parity_driver_selftest PROPERTIES
    LABELS unit TIMEOUT 60)

add_test(NAME grade_parity_driver_selftest
    COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_grade_probe_test.py"
        --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(grade_parity_driver_selftest PROPERTIES
    LABELS unit TIMEOUT 60)

add_test(NAME crossflavor_parity_driver_selftest
    COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_crossflavor_parity_test.py"
        --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(crossflavor_parity_driver_selftest PROPERTIES
    LABELS unit TIMEOUT 60)

# sprite-atlas packer self-validation: make_sprite_atlas.py packs a
# handful of synthetic sprites, re-parses its own .oatlas and asserts no region
# overlaps, all rects in bounds, deterministic order. Stdlib-only, headless -
# LABELS unit so it runs in the unit AND desktop presets.
add_test(NAME sprite_atlas_packer
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_sprite_atlas.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(sprite_atlas_packer PROPERTIES LABELS unit TIMEOUT 60)

# The export-time texture cook - the format auto-table per platform/flavor
# (incl. web -> PNG and the classic GLES2 verdict), resize/premultiply/
# per-platform overrides, the impossible-pair refusals and the sidecar rename -
# is asserted in the Catch2 suite tests/exporter (ExportTextureCookTests.cpp),
# because the cook is part of the exporter library.
# The GPU texture encoder's structural cases - every format (BCn, ETC2, ASTC
# block sizes) over a non-block-aligned gradient, plus the dds/ktx/oitd
# container layouts and the cubemap round trip - live in the Catch2 suite
# tests/exporter (TextureEncodeTests.cpp), because the encoder is a library the
# exporter links rather than a tool it spawns. The runtime-load proof is
# player_cooked_textures below.

# vector-shape SVG cook self-validation: shapecook flattens a synthetic SVG
# (rect + path + circle) to an .oshape and re-parses it through the REAL runtime
# parser, asserting well-formed regions with round-tripped fill colours, vertex
# counts and the requested world extent - plus donut hole detection, a morph set
# with its structure-mismatch refusal and the empty-drawing verdict. The CLI
# face of the same in-process cook the editor imports with; headless.
add_test(NAME shapecook_selftest
    COMMAND shapecook --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(shapecook_selftest PROPERTIES LABELS unit TIMEOUT 60)

# The Lottie -> .oanim cook is a library (core_util/VectorAnimCook), so its
# self-validation lives in the Catch2 suite: VectorAnimCookTests re-cooks the
# committed fixtures and the character corpus BYTE-IDENTICALLY, names every
# out-of-subset refusal and covers the mapped features (clips override, the
# static .oshape case, image layers, transform-link expressions), and
# CookVectorAnimRoundTripTests feeds the same .oanim through the REAL parser +
# evaluator. `animcook` is the argv face of that one cook.

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

# skinned character-rig generator self-validation: make_character_rig.py builds
# the blocky mannequin .glb in memory and asserts the skinning structure (7-joint
# skeleton, one inverse bind matrix per joint, per-vertex JOINTS_0/WEIGHTS_0 that
# sum to 1 with a real multi-joint blend, in-range indices), the walk/idle clip
# durations and rotation channels, determinism (byte-identical .glb) and the
# undeformed rest pose. Stdlib-only, headless - the character pipeline at the
# tooling level.
add_test(NAME make_character_rig_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_character_rig.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_character_rig_selftest PROPERTIES LABELS unit TIMEOUT 60)

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

# mobile HTTP driver self-validation: run_device_http_test.py runs the engine's
# HTTP client inside a REAL exported app on a device (http_device_android /
# http_device_ios below). The selftest covers everything that does not need
# one - every endpoint decision the fixture drives, the verdict-line parser
# (including that a run without a RESULT line is never a pass), argument
# handling, and a live loopback round trip on an ephemeral port. It also
# staleness-gates the single-definition claim: the endpoint lives in the
# fixture manifest, and the script must read that same cvar without repeating
# the port. Stdlib-only, headless.
add_test(NAME http_device_selftest
    COMMAND python3
        "${Orkige_SOURCE_DIR}/tests/integration_driver/run_device_http_test.py"
        --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(http_device_selftest PROPERTIES LABELS unit TIMEOUT 120)

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

# sky cubemap generator self-validation: make_sky_assets.py bakes the stock
# skybox cubemaps in memory and asserts the container structure (a six-face
# BGRA8 cubemap .dds with a complete mip chain, exact body length), byte
# determinism per seed, the six distinct debug face colours and the day/night
# looks (bright blue-dominant vs dark zenith). Stdlib-only, headless - the
# skybox asset pipeline at the tooling level.
add_test(NAME make_sky_assets_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_sky_assets.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_sky_assets_selftest PROPERTIES LABELS unit TIMEOUT 60)

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

# benchmark showcase currency gate: --check regenerates the self-authored text
# tree (scenes, levels, layers, manifest, the .omat/.oshape/.oui/.xlf assets)
# into a temp dir and compares it byte-for-byte against the committed project,
# exiting non-zero on any difference. A hand edit to a generated scene - like a
# restaged water vignette - that was never back-ported to the generator fails
# here, so the two never silently diverge. Deterministic, stdlib-only, no
# subprocess or binary art (the shared binary/asset generators track their own
# tooling, not this one, and are out of scope).
add_test(NAME make_benchmark_assets_check
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_benchmark_assets.py" --check
            "${Orkige_SOURCE_DIR}/projects/benchmark"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_benchmark_assets_check PROPERTIES LABELS unit TIMEOUT 60)

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

# The export app-icon pipeline (the macOS iconset / iOS icons / Android
# mipmaps and each entry's pixel size) and the signing-config gates (iOS
# entitlements composition, identity/profile precedence, launch-colour
# validation) are asserted in the Catch2 suite tests/exporter
# (ExportIconsTests.cpp / ExportSettingsTests.cpp) - both are exporter library
# code, so they need no certificate and no interpreter.

# the checked-in engine default icon renders (corner/centre alpha invariant),
# mirroring make_editor_icon's own self-check.
add_test(NAME make_default_icon_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/make_default_icon.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(make_default_icon_selftest PROPERTIES LABELS unit TIMEOUT 60)

# editor-binary packaging (Util/orkige_nightly_package.py): the ORDERED version
# and its filename rendering (pinned to the same literals VersionOrderTests
# asserts, so the two implementations of the grammar cannot drift), the changelog
# extraction and formatting over synthetic git log output plus its honest
# degradations, the .sha256 sidecar over real bytes and the refusal when a file
# and its sidecar disagree, the release notes an updater reads (the publish job's
# own shell block, run against stubbed job outputs: both machine-readable markers
# with the right values, and the security wording matching what the macOS build
# was actually signed with), the dated release archive (which tags a prune may
# EVER reach, over a listing full of decoys, and the publish + prune steps' own
# shell driven against a recorded-argv stand-in for the GitHub CLI: the rolling
# release replaced, the dated one added with the identical assets, no tag
# outside the dated shape ever handed to a delete, and housekeeping failures
# leaving the night published), the known-limitations table and its per-platform
# AND per-signature-state rendering, the whole macOS signing decision layer
# (credential precedence, the honest refusal of a half-configured set, the
# codesign/notarytool/stapler/spctl argv, the redaction that keeps a credential
# out of an echoed command line, Apple's verdict read strictly, a rejection
# fetching the notarization log, and the inside-out seal order over a synthetic
# bundle - including the invariant that a certificate-less run issues exactly the
# ad-hoc command it always did), the engine media staging over a synthetic build
# tree, the archive round-trip, and every verdict the unpack-and-run verifier can
# reach - including a binary that reports the WRONG commit stamp or a version the
# packaging did not compose, which must fail rather than publish. Needs no built
# editor and NO certificate (every signing decision is pure; the run leg drives a
# stand-in that answers --version).
add_test(NAME orkige_nightly_package_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_nightly_package.py" --selftest
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_nightly_package_selftest PROPERTIES
    LABELS unit TIMEOUT 120)

# the macOS disk image, built and MOUNTED for real over a synthetic app: that
# it carries the complete bundle and the /Applications symlink which makes the
# drag an install (an app launched from an unpacked download folder is subject
# to path randomization) - and that the verifier refuses an image whose link
# points anywhere else. hdiutil exists only on macOS, so this SKIPS with 77
# elsewhere rather than passing without having checked anything; the text-level
# properties of both installable artifacts are asserted by the selftest above,
# which runs on every platform.
add_test(NAME orkige_nightly_dmg_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_nightly_package.py" --selftest-dmg
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_nightly_dmg_selftest PROPERTIES
    LABELS unit TIMEOUT 180 SKIP_RETURN_CODE 77)

# the Linux single-file bundle, ASSEMBLED AND PACKED for real over a synthetic
# AppDir whose stand-in editor is a genuine dynamic executable off this
# machine - so the library inclusion rule runs against a closure the loader
# really resolved. It then unpacks what was packed (through the FUSE-free
# path) and asserts where the editor inside finds each library: the bundled
# names from INSIDE the image, the driver/libc/toolchain families from the
# host. Needs Linux and appimagetool, and SKIPS with 77 without either rather
# than passing without having checked anything; the rule itself, the argv, the
# AppRun and the desktop entry are asserted by the selftest above, which runs
# on every platform.
add_test(NAME orkige_nightly_appimage_selftest
    COMMAND python3 "${Orkige_SOURCE_DIR}/Util/orkige_nightly_package.py" --selftest-appimage
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(orkige_nightly_appimage_selftest PROPERTIES
    LABELS unit TIMEOUT 300 SKIP_RETURN_CODE 77)

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
# The budget is generous because this one renders the WHOLE committed corpus
# twice over (a synthetic fixture, then the real docs) while sharing a machine
# with the rest of the unit pack - it is the slowest test in that label by an
# order of magnitude, and its cost grows with every doc written. A tight bound
# here does not catch a slow renderer, it catches a busy runner: the old 60s
# fired against a 17s standalone run, purely from parallel build load.
set_tests_properties(make_help_portal_selftest PROPERTIES LABELS unit TIMEOUT 180)

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
            ORKIGE_SELFCHECK_OGRE_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media"
            # the classic-flavor bloom compositor media (bright/blur/combine
            # material + OgreUnifiedShader GLSL) the bloom leg registers
            ORKIGE_SELFCHECK_BLOOM_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/bloom/classic"
            # the classic-flavor output-grade compositor media the grade leg registers
            ORKIGE_SELFCHECK_GRADE_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/grade/classic")
    elseif(ORKIGE_RENDER_BACKEND STREQUAL "next")
        target_compile_definitions(render_facade_selfcheck PRIVATE
            # backend-internal media the next bootstrap registers (the HLMS
            # shader templates the ogre-next overlay port ships)
            ORKIGE_SELFCHECK_NEXT_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media"
            # the next-flavor bloom compositor media (bright/blur/combine
            # material + Metal/GLSL shaders) the bloom leg registers
            ORKIGE_SELFCHECK_BLOOM_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/bloom/next"
            # the next-flavor output-grade compositor media the grade leg registers
            # (its material reuses the bloom media's Ogre/Compositor/Quad_vs)
            ORKIGE_SELFCHECK_GRADE_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/grade/next")
    endif()
    target_compile_definitions(render_facade_selfcheck PRIVATE
        # facade-registered test assets: a vertex-coloured glb + a textured
        # glb/png (reused from the committed sample media)
        ORKIGE_SELFCHECK_MESH_DIR="${Orkige_SOURCE_DIR}/samples/hello_orkige/media"
        ORKIGE_SELFCHECK_TEXTURE_DIR="${Orkige_SOURCE_DIR}/samples/jumper/media"
        # the engine decal media (default mark + blob-shadow textures) so the
        # decal leg can load them by name (mirrors the player/editor registration)
        ORKIGE_SELFCHECK_DECAL_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/decals")
    add_test(NAME render_facade_selfcheck COMMAND render_facade_selfcheck
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(render_facade_selfcheck PROPERTIES
        ENVIRONMENT "ORKIGE_SELFCHECK_OUT=${CMAKE_CURRENT_BINARY_DIR}/render_facade")

    # boot_cycle_selfcheck - the real-stack boot/teardown cycler (both
    # flavors). Boots the full AppHost spine (window + Engine + render system +
    # GameObjectManager), fills a scene with a live skybox + IBL + update-wanting
    # ModelComponents, unloads it through GameObjectManager::clear, then tears the
    # whole stack down - N times in one process. Catches teardown-ORDER / lifetime
    # faults a single-boot selfcheck masks (the fixed skybox-reattach and
    # GameObjectManager update-list use-after-frees). Desktop-labelled (not
    # device), so the CI AddressSanitizer suite runs it - where a UAF actually
    # gets caught.
    add_executable(boot_cycle_selfcheck lifecycle/boot_cycle_main.cpp)
    target_link_libraries(boot_cycle_selfcheck PRIVATE Orkige::Engine)
    target_compile_definitions(boot_cycle_selfcheck PRIVATE
        # the committed sky cubemap (sky_day.dds) the skybox leg loads by name
        ORKIGE_CYCLE_MEDIA_DIR="${Orkige_SOURCE_DIR}/samples/hello_orkige/media")
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        target_compile_definitions(boot_cycle_selfcheck PRIVATE
            # classic RTSS media, registered before Engine::setup (ignored on next)
            ORKIGE_CYCLE_OGRE_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media")
    endif()
    add_test(NAME boot_cycle_selfcheck COMMAND boot_cycle_selfcheck
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(boot_cycle_selfcheck PROPERTIES
        LABELS integration TIMEOUT 300)

    # http_network - the ONE test that talks to the real internet, and the only
    # place certificate CHAIN VERIFICATION is proven (the rest of the HTTP suite
    # runs against the tree's own loopback server, which speaks no TLS). It
    # SKIPS (77) when the machine cannot reach the endpoint, when
    # ORKIGE_NO_NETWORK is set or when the build carries no HTTP client, and
    # FAILS only when a server WAS reached and the result was wrong - so a
    # missing network never turns it red. Labelled `network` so a suite can
    # exclude it by label; endpoint override: ORKIGE_HTTP_NETWORK_URL.
    if(ORKIGE_HTTP)
        add_executable(http_network_selfcheck http_network/network_main.cpp)
        target_link_libraries(http_network_selfcheck PRIVATE Orkige::Core)
        add_test(NAME http_network COMMAND http_network_selfcheck
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(http_network PROPERTIES
            LABELS "integration;network" TIMEOUT 120 SKIP_RETURN_CODE 77)
    endif()

    # cooked-cubemap runtime-load proof (both flavors): the driver
    # block-compresses the stock debug cubemap through the REAL export cook
    # (`orkige_export cook-textures`, the same code an export runs) and boots
    # the selfcheck against it - the skybox leg proves the compressed cube
    # LOADS with its +X face and baked mip chain intact. Desktop-BC only here
    # (a host cannot boot the mobile ASTC/ETC2 cube containers - those ride
    # ExportTextureCookTests + the device tests), so it registers wherever the
    # exporter builds.
    if(TARGET orkige_export)
        add_test(NAME render_cooked_cubemap
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_cooked_cubemap_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --selfcheck "$<TARGET_FILE:render_facade_selfcheck>"
                --exporter "$<TARGET_FILE:orkige_export>"
                --flavor "${ORKIGE_RENDER_BACKEND}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(render_cooked_cubemap PROPERTIES
            LABELS integration TIMEOUT 300)
    endif()
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
            LABELS integration TIMEOUT 700 SKIP_RETURN_CODE 77)

        # grade_look_parity - the shared-output-grade look-parity gate: run the
        # selfcheck on BOTH flavors and assert the grade moves saturation +
        # contrast measurably on each AND the induced deltas MATCH across flavors
        # (the shared core_util/GradeMath curve moves both the same way). Same
        # cross-preset skip-77 as render_backend_parity when the classic tree is
        # absent. Registered on the NEXT (default) preset only.
        add_test(NAME grade_look_parity
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_grade_probe_test.py"
                --next-binary $<TARGET_FILE:render_facade_selfcheck>
                --classic-binary "${Orkige_SOURCE_DIR}/${ORKIGE_PARITY_CLASSIC_TREE}/tests/render_facade_selfcheck"
                --out "${CMAKE_CURRENT_BINARY_DIR}/grade_look_parity"
                --repo "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(grade_look_parity PROPERTIES
            LABELS integration TIMEOUT 180 SKIP_RETURN_CODE 77)
    endif()
endif()

if(TARGET orkige_exporter)
    # the pure half of packaging a project: manifest facts, setting
    # vocabularies, credential gates, the Apple declarations, image arithmetic
    add_subdirectory(exporter)
endif()

if(TARGET orkige_editor_core)
    # UI-independent editor logic: command stack, naming, object snapshots
    add_subdirectory(editor_core)

    # editor_update: the whole updater loop against a clean room. The driver
    # serves a fake release feed, a REAL archive packed with the platform's own
    # tool and a checksum sidecar from the tree's own HttpServer on 127.0.0.1,
    # points the real EditorUpdater at it with a scratch directory standing in
    # for an installed editor, and asserts every outcome a person can reach:
    # nothing newer, a downgrade refused, a notify-only check, a download that
    # verifies and stages, a download whose digest does not match, a payload
    # whose signature is rejected, the refusal to rearrange a build tree, the
    # swap itself, the swap's ROLLBACK when the second move fails, and the
    # automated-run veto. Headless (no window, no GPU), so it is a unit-labeled
    # gate rather than an integration one - but it does open a loopback socket
    # and spawn the unpacker, hence its own name. POSIX-only for now: the
    # driver's process helpers are popen/sh.
    if(NOT WIN32)
        add_executable(editor_update_driver
            integration_driver/editor_update_driver.cpp)
        target_link_libraries(editor_update_driver PRIVATE orkige_editor_core)
        add_test(NAME editor_update${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND editor_update_driver
                "${CMAKE_CURRENT_BINARY_DIR}/editor_update${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_update${ORKIGE_FLAVOR_TEST_SUFFIX}
            PROPERTIES LABELS unit TIMEOUT 180 SKIP_RETURN_CODE 77)

        # editor_payload_fetch: the device-player download, same shape and
        # same clean room. The driver serves a fake DATED release with a REAL
        # payload archive packed by the platform's own tool and the checksum
        # sidecar published beside it, points the real EditorPayloadFetcher at
        # it with a scratch directory standing in for the editor's writable
        # state, and asserts every outcome: the fetch that verifies and
        # installs, a release with no such asset, one with no checksum, bytes
        # that do not match theirs, a release published for ANOTHER build (the
        # pairing this rests on), an archive that unpacks incomplete, the
        # prune that keeps only what this build needs, and the automated-run
        # veto. POSIX-only for now, like its sibling: the driver's process
        # helpers are popen/sh.
        add_executable(editor_payload_driver
            integration_driver/editor_payload_driver.cpp)
        target_link_libraries(editor_payload_driver PRIVATE orkige_editor_core)
        add_test(NAME editor_payload_fetch${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND editor_payload_driver
                "${CMAKE_CURRENT_BINARY_DIR}/editor_payload_fetch${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_payload_fetch${ORKIGE_FLAVOR_TEST_SUFFIX}
            PROPERTIES LABELS unit TIMEOUT 180 SKIP_RETURN_CODE 77)
    endif()
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
    set_tests_properties(demo_layout PROPERTIES LABELS integration TIMEOUT 120
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
    set_tests_properties(demo_luateardown PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_LUATEARDOWN=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAORPHAN: the weak-handle orphan contract. Under option-C weak
    # handles Lua never owns a widget, so the old widget-outlives-its-layer
    # use-after-free cannot happen; the leg asserts the honest replacement - a live
    # handle works, and after the owning GuiManager is destroyed, touching the
    # still-held handle raises the catchable "widget handle is dead" error (never a
    # crash). Clean exit is the contract. Flavor-neutral (gui draws through DrawLayer2D).
    add_test(NAME demo_luaorphan COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luaorphan PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_LUAORPHAN=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAHANDLE: the weak Lua widget-handle (option C) acceptance test.
    # findLabel hands a weak WidgetHandle; a leaf method, an inherited base method
    # and setParent (a widget-valued parameter) dispatch through it; a wrong-leaf
    # method on a LIVE widget errors distinctly; touching the handle after its
    # widget is destroyed raises the honest "widget handle is dead" script error at
    # the touching line (catchable, no crash). Flavor-neutral.
    add_test(NAME demo_luahandle COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luahandle PROPERTIES LABELS integration TIMEOUT 120
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
    set_tests_properties(demo_luagohandle PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_LUAGOHANDLE=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LUAHANDLE_SURFACE: the weak-handle completeness sweep. One live
    # GameObject carries every scriptable component; the probe script calls every
    # method the GameObject / per-component / layer handles bind and hard-fails on
    # any name that is not a bound function (a dropped/renamed binding is caught
    # here). The living spec of the whole handle surface. Flavor-neutral.
    add_test(NAME demo_luahandle_surface COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_luahandle_surface PROPERTIES LABELS integration TIMEOUT 120
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
    set_tests_properties(demo_oui PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_OUI=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_GUI_INPUT: the gui input-enablement selfcheck - proves the
    # ratified policy on a real GuiManager: input events subscribe themselves
    # the first time a screen gains an interactive widget, a label-only screen
    # stays inert, an explicit disable (before OR after) sticks against the
    # auto-enable, and an .oui `input off` suppresses the auto-enable for a
    # screen with buttons. Flavor-neutral (gui on DrawLayer2D).
    add_test(NAME demo_gui_input COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_gui_input PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_GUI_INPUT=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_LOC: the localisation-runtime selfcheck - loads a project's
    # XLIFF loc/ registry (en source + de target + the generated en-XA pseudo-
    # locale) into the StringTable, loads a committed .oui whose captions are all
    # @keys, and reads the RESOLVED widget text back: the source resolves the
    # English source, a live switch to de re-skins the screen from the target
    # (text differs and renders), and en-XA resolves an accented pseudo variant
    # that also renders. Flavor-neutral (gui @key routing on DrawLayer2D).
    add_test(NAME demo_loc COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_loc PROPERTIES LABELS integration TIMEOUT 120
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
    set_tests_properties(demo_gui_modal PROPERTIES LABELS integration TIMEOUT 120
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
    set_tests_properties(demo_gui_matrix PROPERTIES LABELS integration TIMEOUT 120
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
    set_tests_properties(demo_gui_flow PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_GUI_FLOW=1;ORKIGE_DEMO_FRAMES=4")

    # ORKIGE_DEMO_GUI_LUA: the FULLY-SCRIPTABLE contract - the whole widget
    # matrix authored/driven/asserted IN LUA through the ScriptRuntime surface
    # (C++ only injects one synthetic press + reads probes). A missing/renamed
    # gui binding raises a Lua error, so the suite fails by construction. Also
    # proves the .oui<->Lua bridge (findWidget by id). Scripting-gated: the
    # ORKIGE_SCRIPTING=OFF build skips the body (ScriptRuntime unavailable).
    add_test(NAME demo_gui_lua COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_gui_lua PROPERTIES LABELS integration TIMEOUT 120
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
    # loopable OGG through the ring-fed MusicStream path and assert the
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

    # ORKIGE_DEMO_WORLDTEXT: the WorldTextComponent headed selfcheck - a
    # world-space text label built from the shared gui font page as
    # camera-facing glyph quads through one SpriteBatch (the 3D-particle
    # billboard recipe, with glyph UVs). Asserts one quad per INKED glyph
    # (spaces excluded), that a text change re-lays-out, that a non-Latin
    # (Cyrillic) string pages its glyphs, and that a hide/show toggle moves the
    # frame-stats triangle count (it renders). Reads state/stats, not pixels -
    # runs on both render flavors.
    add_test(NAME demo_worldtext COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_worldtext PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_WORLDTEXT=1;ORKIGE_DEMO_FRAMES=60")

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
    # actually renders. Runs on both flavors: next = the full PBS datablock,
    # classic = its RTSS Cook-Torrance counterpart (both honor the normal +
    # emissive maps over generated tangents) - a per-flavor probe, NOT a pixel
    # parity case (lit content legitimately differs between the flavors).
    add_test(NAME demo_material COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_material PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_MATERIAL=1;ORKIGE_DEMO_FRAMES=120")

    # material_looks_right: the PIXEL probe for what the `.omat` maps LOOK
    # like (demo_material's counts prove "renders", not "renders right").
    # Drives the ORKIGE_DEMO_MATLOOKS rig twice - full maps vs the flat
    # sibling material - and asserts normal/emissive maps CHANGE the lit hero,
    # the cast shadow composes on the normal-mapped receiver (the receiver
    # stage and the normal-map stage feed the same lighting stage), and the
    # emissive map glows after lights-out while everything else reads dark
    # (which caught classic double-applying the emissive colour as a
    # whole-surface glow). Both flavors. Skips (77) if the app is unbuilt.
    add_test(NAME material_looks_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_material_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/material_looks_right"
        --leg looks
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(material_looks_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

    # material_cutout_right: the alpha-test + two-sided PIXEL probe. Drives
    # the ORKIGE_DEMO_CUTOUT rig (a cutout leaf quad + a back-facing sibling
    # over a lit ground with a casting sun) and asserts the hole renders as
    # backdrop, the back face renders at all (two-sided), and the CAST SHADOW
    # carries the hole - the caster is a cutout too (native Hlms alpha-test
    # casters on next; the generated per-material caster override on classic).
    # Both flavors. Skips (77) if the app is unbuilt.
    add_test(NAME material_cutout_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_material_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/material_cutout_right"
        --leg cutout
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(material_cutout_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

    # material_accents_right: the runtime-accent PIXEL probe. Drives the
    # ORKIGE_DEMO_ACCENTS rig (two instances of ONE .omat; a tint + emissive
    # boost lands on instance A mid-run and clears again) and asserts the
    # accent changes A, leaves the sibling B byte-untouched (per-instance
    # isolation on a shared material - the accent variant machinery), and
    # that clearing restores A exactly (toggle identity). Both flavors.
    # Skips (77) if the app is unbuilt.
    add_test(NAME material_accents_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_material_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/material_accents_right"
        --leg accents
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(material_accents_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

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
    set_tests_properties(demo_water PROPERTIES LABELS integration TIMEOUT 120
        ENVIRONMENT "ORKIGE_DEMO_WATER=1;ORKIGE_DEMO_FRAMES=120")

    # ORKIGE_DEMO_SKY: the sky-type + IBL + bloom showcase selfcheck - one lit
    # scene cycles the three atmosphere sky types (procedural dome -> skybox
    # cubemap -> flat colour, each round-tripped through world->getAtmosphere),
    # turns skybox-sourced image-based lighting on a reflective metal cube
    # (getImageLightingEnabled + the IblReflections capability), and enables LDR
    # bloom on an emissive cube - asserting the bloom capability PER CONTEXT: it
    # renders (the compositor pass builds) on both desktop flavors and is a
    # gated honest no-op on a bloom-less GLES2/WebGL classic context (one log
    # line, byte-identical output). The frame-60 ORKIGE_DEMO_SCREENSHOT hook dumps the
    # final showcase frame. Runs on both flavors - a per-flavor selfcheck, NOT
    # a pixel-parity case (the render_facade selfcheck pixel-proves the glow /
    # reflection); see Docs/materials.md.
    add_test(NAME demo_sky COMMAND hello_orkige
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(demo_sky PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_SKY=1;ORKIGE_DEMO_FRAMES=120")

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

    # water_refraction_looks_right: the Stage-3 screen-space refraction probe.
    # Runs the demo_water refraction leg two ways (a high-contrast checkerboard
    # LAKEBED under the surface): refraction ON (wavy, moving) vs OFF (the
    # straight/stable Stage-1 baseline), captures two frames apart per run, and
    # asserts the water band shows strong WAVY DISPLACEMENT of the bed vs the
    # straight baseline AND that the refraction CHANGES between frames (the
    # ripple scrolls it) while the baseline stays put. Capability-gated: a
    # backend/context that cannot refract (next today; a GLES/WebGL classic
    # context) renders the Stage-1 look and the probe SKIPS (77). Skips (77)
    # too if the app is unbuilt.
    add_test(NAME water_refraction_looks_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_water_refraction_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/water_refraction_looks_right"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(water_refraction_looks_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

    # water_reflection_looks_right: the planar (mirror-of-scene) reflection probe.
    # Runs the demo_water reflection leg two ways (a TALL magenta marker wall
    # above the surface): reflection ON (the marker's magenta mirror fills the
    # water band) vs OFF (the byte-stable sky-reflection look), and asserts the
    # marker's magenta appears in the water band ON, is essentially absent OFF,
    # and the band ON differs strongly from the OFF baseline (the mirror renders).
    # Capability-gated: a backend/context that cannot do planar reflection (next;
    # a GLES/WebGL classic context) reflects the sky IBL cubemap and the probe
    # SKIPS (77). Skips (77) too if the app is unbuilt.
    add_test(NAME water_reflection_looks_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_water_reflection_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/water_reflection_looks_right"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(water_reflection_looks_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

    # water_swell_looks_right: the GEOMETRIC swell probe. Runs the demo_water
    # swell leg two ways (a high-contrast backdrop wall behind the far water edge,
    # the scene orbit + ripple scroll FROZEN so the swell is the only thing that
    # can move the surface): swell ON (waveHeight > 0) vs OFF (the byte-stable
    # waveHeight 0 flat plane), captures two frames apart per run, and in the
    # silhouette band asserts the swelling surface MOVES between the two frames
    # (the swell clock advances) and that the waveHeight-0 baseline is byte-stable
    # over time. Runs on BOTH flavors: next displaces the plain PBS surface via a
    # custom vertex piece, classic via its refraction/reflection water vertex
    # program. Skips (77) if the app is unbuilt.
    add_test(NAME water_swell_looks_right COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_water_swell_probe_test.py"
        --binary "$<TARGET_FILE:hello_orkige>"
        --out "${CMAKE_CURRENT_BINARY_DIR}/water_swell_looks_right"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(water_swell_looks_right PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

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
	# ORKIGE_EDITOR_GIT_FIXTURE points the git change-markers leg at a TRACKED
	# repo file (absolute - the .app bundle's cwd is not the source tree); the
	# leg dirties its BUFFER only, never the disk or the git index
	set_tests_properties(editor_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
		ENVIRONMENT "ORKIGE_EDITOR_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120;ORKIGE_EDITOR_SELFCHECK_SCENE=${CMAKE_CURRENT_BINARY_DIR}/editor_selfcheck.oscene;ORKIGE_EDITOR_GIT_FIXTURE=${Orkige_SOURCE_DIR}/projects/jumper-lua/scripts/player.lua")

	# The two human preview tabs, end to end. Opens the benchmark project,
	# double-click-routes a real Lottie character and .oui through the same
	# request seams as the Assets panel, verifies both dock beside Scene, and
	# proves their live texture IDs occur in ImGui's submitted draw data. The
	# animation path also checks its paused upload is stable and a changed frame
	# defers display until the next upload-ring handoff. GUI Preview needs offscreen-owned
	# layers, which is an Ogre-Next capability; classic is covered by its honest
	# unsupported-path assertion in editor_control.
	if(ORKIGE_RENDER_BACKEND STREQUAL "next")
		# guarded: this leg opens the committed (generator-owned) benchmark
		# project, so the wrapper trips the test if it ever WRITES a benchmark
		# .oui (the canonicalising drift that names no author otherwise)
		add_test(NAME editor_previews_next
			COMMAND python3 "${Orkige_SOURCE_DIR}/Util/guard_tree_unchanged.py"
				--path "${Orkige_SOURCE_DIR}/projects/benchmark/assets/hud.oui"
				--path "${Orkige_SOURCE_DIR}/projects/benchmark/assets/settings.oui"
				-- $<TARGET_FILE:orkige_editor>
			WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
		set_tests_properties(editor_previews_next PROPERTIES LABELS integration TIMEOUT 300
			ENVIRONMENT "ORKIGE_EDITOR_PREVIEW_SELFCHECK=${Orkige_SOURCE_DIR}/projects/benchmark;ORKIGE_DEMO_FRAMES=90")
	endif()

    # The asset-browser thumbnailer must SURVIVE a cubemap .dds: a cubemap
    # cannot back a 2D thumbnail, so its tile falls back to the kind glyph
    # (thumbnail id 0) with no crash. tests/projects/cubemap_preview carries a
    # generated six-colour cubemap; the selfcheck opens it, shows the Assets
    # panel, walks the tile through the real thumbnail service and asserts the
    # honest glyph fallback. Both flavors (no offscreen layers needed, unlike
    # editor_previews).
    add_test(NAME editor_cubemap_thumbnail${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_cubemap_thumbnail${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_CUBEMAP_SELFCHECK=${Orkige_SOURCE_DIR}/tests/projects/cubemap_preview;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 90)

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

    # camera authoring: Create Camera command (spawns at the view pose), the
    # frustum gizmo (asserted via the uploaded-vertex seam, not pixels), the
    # new-scene default Main Camera template, add_component/set_component
    # reaching CameraComponent by name (MCP-parity path) and a save/reload
    # round-trip. Runs the editor windowed on both flavors (editor_camera).
    add_test(NAME editor_camera${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_camera${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_CAMERA_SELFCHECK=${CMAKE_CURRENT_BINARY_DIR}/editor_camera.oscene;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 120)

    # Scene view display-option overlays, BOTH flavors: the toolbar Display
    # dropdown's toggles drive editor-only facade line meshes (colliders /
    # bounding boxes / all-camera frames). The leg round-trips the view-option
    # persistence, arms every toggle over a renderable-cube + rigid-body + camera
    # fixture and asserts each overlay built its vertices (the uploaded-vertex
    # seam, not pixels), then disarms and asserts they clear (editor_overlays).
    add_test(NAME editor_overlays${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_overlays${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_OVERLAY_SELFCHECK=1;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 120)

    # Visual .oui editor edit loop, BOTH flavors: the Preview panel's Edit UI
    # mode drives the ONE GuiLayout document through the same hook API the mouse
    # uses. The leg writes a starter screen, then load -> select -> move (asserts
    # anchoredPos updated + an undo step) -> undo (asserts restored) -> add widget
    # -> save -> reload, asserting the widget landed and the saved file is canonical
    # (the round-trip contract). Doc-and-file level, so it passes on classic too
    # (the overlay render is Ogre-Next only; the editing plumbing is not). The pure
    # hit-test/drag/palette/undo math is the EditorUiEdit unit suite (editor_uiedit).
    add_test(NAME editor_uiedit${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_uiedit${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_UIEDIT_SELFCHECK=1;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 120)

    # Visual .oui editor CANVAS driven by REAL synthetic SDL mouse events, BOTH
    # flavors: sibling to editor_uiedit but exercising the whole event-pump ->
    # ImGui -> InvisibleButton path a user's mouse takes (not the hook API). It
    # copies a fixture project, opens a starter screen as the Preview overlay
    # (editing IS on when a screen shows), then a frame-phase machine injects
    # SDL_EVENT_MOUSE_MOTION/BUTTON_DOWN/BUTTON_UP to click-select a label, grab
    # its bottom-right resize grip and drag it (asserting the doc-resolved box GREW
    # with the anchor preserved, and on next the live overlay rect sits inside the
    # box), repeats the grip-resize for a STRETCH-anchored label and a button,
    # adds a fresh label via the panel path and immediately grip-resizes it, and
    # drag-moves a widget's body (asserting the anchored position moved). The
    # canvas boxes are doc-resolved so it runs on classic too (the live overlay
    # text-in-box leg is next-only and skipped honestly there). The pure canvas
    # math is the EditorUiEdit unit suite (editor_uidrag).
    add_test(NAME editor_uidrag${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_uidrag${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_UIDRAG=${Orkige_SOURCE_DIR}/tests/projects/uihotreload;ORKIGE_DEMO_FRAMES=700"
        # coarse backstop only: the run is deadline-driven inside the editor
        # (which fails with the step's own reason), and this frame cap just has
        # to exceed the bounded worst case - a gesture the display server
        # interrupts is redone, up to a logged limit - on a sanitizer-slowed
        # frame cadence. A clean run finishes inside ~180 frames.
        LABELS integration TIMEOUT 240)

    # Display dropdown v2 view mode + lighting, BOTH flavors, over one lit
    # water-plane + directional-light + camera fixture. WIREFRAME leg (classic, a
    # PER-TARGET per-camera polygon-mode flip): the Scene RTT PIXELS change while the
    # Preview stays byte-identical (the per-target scoping proof). LIGHTING leg
    # (both, a GLOBAL per-frame flatten armed via the Scene-only dock-tab visibility
    # rule - @see RenderWorld::setLightingSuppressed): arming lighting-off flattens
    # the Scene RTT and disarming restores it EXACTLY (byte-identical). The
    # visibility rule + capability gating are the EditorViewModes unit test; the MCP
    # verbs are editor_control (editor_viewmode). SHADED+WIREFRAME OVERLAY leg (both,
    # OVERLAY ITEMS - a second wireframe renderable per scene mesh ON TOP of the
    # untouched shaded pass, @see RenderWorld::setSceneWireframeOverlay): the armed
    # frame differs from BOTH the shaded baseline AND the pure-wireframe flip, one
    # overlay exists per scene mesh, a live add/delete grows/shrinks the set, the
    # Preview stays byte-identical (the editor-only-bit mask proof) and disarm
    # restores the Scene RTT byte-exact + destroys the companions.
    add_test(NAME editor_viewmode${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_viewmode${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_VIEWMODE_SELFCHECK=1;ORKIGE_DEMO_FRAMES=120"
        LABELS integration TIMEOUT 120)

    # Preview panel + the Scene-panel selected-camera inset, BOTH flavors:
    # the composite renders the scene through its OWN CameraComponent at a notch
    # device preset with the procedural device frame + safe-area guides, the
    # animate-materials clock advances water time, the selected-camera inset
    # draws tracking that camera, and deleting the camera yields the honest
    # no-camera state - all asserted through non-pixel draw seams (the camera
    # gizmo vertex-seam pattern). The .oui overlay leg is next-only, covered by
    # editor_previews + the editor_control preview_game leg (editor_game_preview).
    add_test(NAME editor_game_preview${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_game_preview${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_GAME_PREVIEW_SELFCHECK=1;ORKIGE_DEMO_FRAMES=40"
        LABELS integration TIMEOUT 120)

    # Scene-authored atmosphere (AtmosphereComponent), BOTH flavors: the
    # new-scene Environment template arms the DAY sky, save/reload re-arms,
    # a live property edit re-arms with the preset-seeds/fields-override
    # precedence, a second instance stays dormant and the take-over contract
    # promotes/takes back on (de)activation; the Hierarchy owner glyphs draw
    # accent-vs-dimmed (seam) and the Preview's captured PIXELS show the
    # day sky band vs the disabled backdrop (editor_atmosphere).
    add_test(NAME editor_atmosphere${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_atmosphere${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_ATMOSPHERE_SELFCHECK=${CMAKE_CURRENT_BINARY_DIR}/editor_atmosphere.oscene;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 120)

    # Refractive-water crash guard, BOTH flavors: opening a scene whose water
    # carries screen-space refraction with the Preview panel active renders
    # it through offscreen RTTs whose workspaces cannot satisfy an HlmsPbs
    # Refractive datablock - the editor used to ABORT (SIGABRT out of
    # Root::renderOneFrame). The next backend downgrades such water to a
    # transparent look in the editor, and any stray shader-generation exception
    # is contained at the render entry; classic renders its (non-Hlms) water
    # natively. The leg opens the benchmark lake scene and asserts no crash +
    # water present (editor_refraction).
    # guarded like editor_previews: opens the committed benchmark project, so
    # the wrapper trips if the leg ever writes a benchmark .oui
    add_test(NAME editor_refraction${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3 "${Orkige_SOURCE_DIR}/Util/guard_tree_unchanged.py"
            --path "${Orkige_SOURCE_DIR}/projects/benchmark/assets/hud.oui"
            --path "${Orkige_SOURCE_DIR}/projects/benchmark/assets/settings.oui"
            -- $<TARGET_FILE:orkige_editor>
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_refraction${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_REFRACTION_SELFCHECK=${Orkige_SOURCE_DIR}/projects/benchmark;ORKIGE_DEMO_FRAMES=40"
        LABELS integration TIMEOUT 120)

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

    # The editor's COMMAND LINE, driven as a build script drives it
    # (integration_driver/run_editor_cli_test.py). The pure argument router is
    # unit-tested; what needs the real binary is the part a regression would
    # otherwise show as a HUNG JOB rather than a failure: a subcommand must
    # finish on its own, with no window, on a machine that may have no display.
    # The driver also asserts the three exit codes and, where this host
    # packages a desktop app, that `export` produces one and prints the
    # `orkige_editor: OK <artifact>` line callers grep for.
    # No window and no GPU - but it spawns the editor binary and (on macOS)
    # packages a real app, so it is an integration test, not a unit one.
    if(APPLE)
        set(ORKIGE_CLI_EXPORT_ARGS --platform macos)
    else()
        set(ORKIGE_CLI_EXPORT_ARGS --skip-export)
    endif()
    add_test(NAME editor_cli${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_cli_test.py"
            --editor "$<TARGET_FILE:orkige_editor>"
            --project "${Orkige_SOURCE_DIR}/projects/example"
            --output "${CMAKE_CURRENT_BINARY_DIR}/editor_cli${ORKIGE_FLAVOR_TEST_SUFFIX}"
            ${ORKIGE_CLI_EXPORT_ARGS}
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_cli${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        LABELS integration TIMEOUT 2400)

    # `orkige_editor test`: the shipped door onto a project's own Lua suite
    # (integration_driver/run_editor_cli_test_command.py). Separate from the
    # test above because it is a different SHAPE - that one proves no
    # subcommand ever boots anything, this one deliberately does: the run is
    # delegated to the player, which brings up the whole runtime.
    #
    # It runs projects/jumper-lua's REAL suite, whose playthrough.test.lua
    # declares a scene. That case is the point: a play-mode test needs a live
    # world, the editor process has none before its window, and the player
    # does - so proving it passes THROUGH THIS DOOR is what makes the
    # delegation more than an assertion in a doc. The driver then builds a
    # deliberately failing suite of its own, because a door that could only
    # report success would be worse than none.
    # Scripting-gated: an ORKIGE_SCRIPTING=OFF build has no interpreter and
    # this door says so instead of running anything.
    if(TARGET orkige_player AND ORKIGE_SCRIPTING STREQUAL "LUA")
        add_test(NAME editor_cli_test${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_cli_test_command.py"
                --editor "$<TARGET_FILE:orkige_editor>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --template-project "${Orkige_SOURCE_DIR}/projects/example"
                --output "${CMAKE_CURRENT_BINARY_DIR}/editor_cli_test${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_cli_test${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 1200
            ENVIRONMENT "ALSOFT_DRIVERS=null")
    endif()

    # --- distribution readiness: a COPIED editor stands alone --------------
    # The editor ships as an app somebody copies onto a machine with no clone
    # of this repository, no engine build tree and no Python. The driver
    # (integration_driver/run_editor_bundle_test.py) stages the built app plus
    # a copy of a project into the build tree, DETACHES it (every
    # developer-tree fallback refused via ORKIGE_EDITOR_BUNDLE_ONLY, plus - on
    # macOS - a sandbox that denies reads of the repository, the vcpkg tree and
    # Homebrew, which also catches a baked path reached OUTSIDE the resource
    # resolver), and drives the copy over its own MCP endpoint: it must boot
    # with a rendering window, open the copied project, render a scene
    # screenshot and PLAY through the player INSIDE the app. A second leg asks
    # it to PACKAGE that project: it must either export a runnable .app out of
    # the engine payload it carries (which the leg then runs, in the same clean
    # room) or refuse with a sentence naming what is missing - and an iOS
    # request, which a copy genuinely cannot produce, must say exactly that;
    # a missing-file error naming a directory from the machine that built the
    # binary fails the leg. A third leg makes
    # the staged shader media unreadable and asserts the render backend
    # degrades with its honest warning instead of aborting the boot. A fourth
    # reads back the changelog the About box shows (`--changelog`, the same
    # text, display-free) in BOTH of its states: a copy that was never
    # packaged says so in one line, and a copy carrying the CHANGELOG.md the
    # packaging pipeline writes at its resource root shows THAT. So
    # distribution readiness stops being something we re-audit by hand.
    # Both flavors (each stages its own flavor's shader media).
    # APPLE-gated: the .app is the distributable shape the tree produces today,
    # and the path sandbox that makes the clean room REAL is macOS machinery.
    # TODO(linux/windows): the build already stages the share/orkige layout
    # beside the executable, so enabling this here needs only a mechanism that
    # makes the tree unreachable on those platforms (a mount namespace, or a
    # staged copy on a machine without the tree) - ORKIGE_EDITOR_BUNDLE_ONLY
    # alone would assert the resolver, not the layout.
    if(APPLE)
        add_test(NAME editor_bundle${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_bundle_test.py"
                --editor-app "$<TARGET_BUNDLE_DIR:orkige_editor>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --scene "scenes/main.oscene"
                --stage-root "${CMAKE_CURRENT_BINARY_DIR}/editor_bundle${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --repo-root "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_bundle${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 900)

        # The BROWSER half of the same question, as its own test because it can
        # honestly skip: a distributed editor packages a web export out of the
        # payload the packaging pipeline stages inside it (the wasm player, the
        # shell page and the classic engine media the browser player renders
        # through), on a host with no Emscripten toolchain - a web export
        # compiles nothing. The leg stages that payload into the copied app
        # through the packaging tool's OWN function, then drives the clean-room
        # copy over MCP to export for the web and asserts the artifact set the
        # web suite defines, plus that the shipped wasm player is byte-for-byte
        # the one the app carried. It runs on BOTH flavors and must pass on
        # both: the browser player is the classic flavor whatever the editor
        # is, so this is what makes the browser target flavor-independent.
        # The wasm tree is resolved the packaging tool's own way (the env
        # ORKIGE_WEB_BUILD, else the repository's build/web-release), so the
        # test and a release use ONE rule. Skips (exit 77) when there is none -
        # which is why it is not a leg of editor_bundle above: a machine
        # without a wasm toolchain must not turn that test's verdicts into a
        # skip.
        add_test(NAME editor_bundle_web${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_bundle_test.py"
                --leg web
                --editor-app "$<TARGET_BUNDLE_DIR:orkige_editor>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --scene "scenes/main.oscene"
                --stage-root "${CMAKE_CURRENT_BINARY_DIR}/editor_bundle_web${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --repo-root "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_bundle_web${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 900 SKIP_RETURN_CODE 77)

        # the iOS preset trees are flavor-suffixed like the desktop ones: a
        # payload is flavor-bound, so each flavor composes its OWN player
        if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
            set(ORKIGE_IOS_PAYLOAD_TREE_SUFFIX "-classic")
        else()
            set(ORKIGE_IOS_PAYLOAD_TREE_SUFFIX "")
        endif()
        # The MOBILE half: a distributed editor packages a game for a PHONE.
        # That player is the one engine piece a release does not carry - a
        # phone runs another architecture's binary - so it is published as its
        # own release asset and fetched on demand into the editor's writable
        # state directory (Docs/device-payloads.md). The DOWNLOAD is proven by
        # editor_payload_fetch against a loopback service; this proves the
        # other half: an installed payload turns a copied editor into one that
        # exports an iOS simulator app, in the same clean room as every other
        # bundle leg - and that without one, the refusal names the way to get
        # it. The payload is composed by the packaging tool's OWN function from
        # the iOS preset tree, so the shipped composition is what is under
        # test. Skips (exit 77) where that preset was never built, which is why
        # it is not a leg of editor_bundle above.
        add_test(NAME editor_bundle_ios${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_bundle_test.py"
                --leg ios
                --editor-app "$<TARGET_BUNDLE_DIR:orkige_editor>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --scene "scenes/main.oscene"
                --device-build
                    "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_IOS_PAYLOAD_TREE_SUFFIX}"
                --stage-root "${CMAKE_CURRENT_BINARY_DIR}/editor_bundle_ios${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --repo-root "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_bundle_ios${ORKIGE_FLAVOR_TEST_SUFFIX}
            PROPERTIES LABELS integration TIMEOUT 900 SKIP_RETURN_CODE 77)

        # ...and its ANDROID sibling. An APK is ASSEMBLED around the player
        # rather than copied whole like a .app, so this leg is where the three
        # prerequisite tiers have to stay three answers: the PLAYER is fetched
        # (a download), the Android SDK build tools and a JDK are the MACHINE's
        # (named one program at a time - we ship the engine, never a
        # toolchain), and the engine SDK pack belongs to compiled C++ game code
        # ALONE. The project packaged here is pure Lua, so no pack is installed
        # anywhere in the clean room and neither refusal may mention one - the
        # negative that keeps a Lua game from ever needing a C++ SDK.
        # Skips (77) without an Android player or without the SDK.
        add_test(NAME editor_bundle_android${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_bundle_test.py"
                --leg android
                --editor-app "$<TARGET_BUNDLE_DIR:orkige_editor>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --scene "scenes/main.oscene"
                --device-build
                    "${Orkige_SOURCE_DIR}/build/android-debug${ORKIGE_IOS_PAYLOAD_TREE_SUFFIX}"
                --stage-root "${CMAKE_CURRENT_BINARY_DIR}/editor_bundle_android${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --repo-root "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_bundle_android${ORKIGE_FLAVOR_TEST_SUFFIX}
            PROPERTIES LABELS integration TIMEOUT 900 SKIP_RETURN_CODE 77)

        # COMPILED GAME CODE from a copied editor: the third distribution
        # question, and the one that needs an engine rather than a payload. A
        # downloaded editor has no checkout and no build tree, so a project
        # whose behaviour is C++ builds against an installed SDK PACK
        # (Docs/sdk-pack.md) - which this leg installs into the copy's own
        # writable state directory, exactly where the editor looks for one.
        #
        # It runs in a clean room of its OWN: the repository and the machine's
        # vcpkg root stay denied (a build tree at its usual absolute path would
        # silently satisfy a configure that should have failed), but cmake and
        # ninja are handed back as individual files, because a native build
        # genuinely needs a toolchain - we ship the engine, never a compiler.
        # The leg asserts all three outcomes: no pack -> a refusal naming the
        # SDK, a pack without build programs -> a DIFFERENT refusal naming the
        # toolchain, and both present -> the module builds against the pack,
        # PLAYS, and packages into a runnable app.
        #
        # Its own test rather than a leg of editor_bundle: it installs a pack
        # (gigabytes, minutes) and compiles a real module, so it carries the
        # native-module RESOURCE_LOCK and a build-sized timeout.
        add_test(NAME editor_bundle_native${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_editor_bundle_test.py"
                --leg native
                --editor-app "$<TARGET_BUNDLE_DIR:orkige_editor>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-native"
                --scene "scenes/main.oscene"
                --shared-headers "${Orkige_SOURCE_DIR}/samples/jumper"
                --engine-build "${CMAKE_BINARY_DIR}"
                --flavor "${ORKIGE_RENDER_BACKEND}"
                --cmake "${CMAKE_COMMAND}"
                --ninja "${CMAKE_MAKE_PROGRAM}"
                --stage-root "${CMAKE_CURRENT_BINARY_DIR}/editor_bundle_native${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --repo-root "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(editor_bundle_native${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 3600
            # it compiles the same OGRE-including module sources the
            # compile-on-Play and export tests do; keep them off each other
            # under -j
            RESOURCE_LOCK native_module_build)
    endif()

    # Asset browser (v2): open a temp COPY of
    # projects/example and drive the browser headlessly - enumerate assets
    # (id-carrying vs. dimmed sidecar-less), run the generic importAssetFile
    # (copy + sidecar mint + enumerable), instantiate a sprite via
    # CreateSpriteObjectCommand (+ undo) and a prefab through the drag path
    # (instantiateAssetIntoScene). v2 also: folder enumeration + navigate into
    # a subfolder + up, New Folder, New Script (file + sidecar + enumerable),
    # the texture-thumbnail path (a real png -> a valid ImGui texture handle;
    # a non-image fails gracefully) and the openWithDefaultApp URL resolver
    # (asserted without spawning a GUI app). It also covers New Sound + the
    # Inspector's AUDITION of a procedural `.osfx`: the written text parses back
    # to its archetype and renders the archetype's own samples, classifies as
    # audio, and plays through the engine's audio path with no play session
    # (the silent audio device every test here runs on makes that the audible
    # branch; a device-less machine asserts the honest refusal instead).
    # Flavor-agnostic; both flavors.
    add_test(NAME editor_asset_browser${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    # The SVG-import leg proves the .oshape cook runs in process - no
    # interpreter, no subprocess, nothing staged beside the binary. That the
    # editor spawns none ANYWHERE is asserted structurally by editor_bundle,
    # which runs a copied app out of a clean room with no interpreter on PATH.
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

    # Source Control panel end to end: build a THROWAWAY temp git repo under the
    # test binary dir (never the real repo), with a fixture project nested deep in
    # it (games/myproj), and drive the EditorGit seam headlessly - status shows a
    # dirty file, stage moves it to Staged, commit clears the tree + a commit
    # exists in the log, push clears the ahead badge against a bare-remote origin,
    # the nested browser-badge snapshot resolves through the project prefix, and a
    # per-file discard resets the file to committed AND reloads its open document
    # buffer. Uses the panel's own merged-stream git runner; skips silently
    # (exit 0) when git is absent. Both flavors. Frame budget is the backstop.
    add_test(NAME editor_source_control${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_source_control${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_SOURCECONTROL_TEST=${CMAKE_CURRENT_BINARY_DIR};ORKIGE_DEMO_FRAMES=90"
        LABELS integration TIMEOUT 120)

    # embedded terminal end to end: spawn a REAL pty running a scripted echo,
    # assert the VT grid seam (printed text + SGR colour reach the cell grid),
    # type a known word through the pure key encoder and assert the child echoed
    # it back, then close and assert the child died. Window-independent - it runs
    # and exits before the render boot, so it needs no display. Skips (exit 77)
    # when no pty/shell is available. Both flavors (the seam is flavor-neutral).
    # The Windows CI job (windows-next) runs it against cmd.exe via ConPTY.
    add_test(NAME editor_terminal${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    # RESOURCE_LOCK: the clipboard legs assert the content of the ONE OS
    # clipboard, so two of them running at once would read each other's probe
    # string (and on X11 trade selection requests across processes).
    set_tests_properties(editor_terminal${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_TERMINAL_TEST=1"
        RESOURCE_LOCK os_clipboard
        LABELS integration TIMEOUT 120 SKIP_RETURN_CODE 77)

    # embedded terminal REAL-EVENT end to end: the interactive path a seam test
    # cannot see. Runs the real editor frame loop with the terminal panel opted
    # back in under the automated run, spawns a session, seeds a marker, then
    # drives REAL synthetic SDL mouse (drag-select the marker) + the platform
    # copy chord and asserts the OS clipboard carries it, then proves follow-tail
    # scrolling through the true ImGui path (growth stays pinned, a wheel-up
    # unpins and stays put under more output, a keypress re-pins), a
    # Cmd/Ctrl+click path-open, and finally the opposite contract: TYPED text
    # plus a physical Ctrl+C must reach the child as SIGINT (asserted as the
    # shell's 130 exit status). SHELL is pinned so the typed legs read ONE
    # deterministic shell: the status question `$?` is POSIX spelling, which the
    # machine's own login shell need not speak. Needs a display; skips (exit 77)
    # when no pty/shell is available. Both flavors.
    add_test(NAME editor_terminal_copy${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    # RESOURCE_LOCK os_clipboard: shares the OS clipboard with editor_terminal
    # above - the two must never overlap.
    set_tests_properties(editor_terminal_copy${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_TERMINAL_UITEST=${Orkige_SOURCE_DIR}/projects/jumper-lua;ORKIGE_DEMO_FRAMES=2700;SHELL=/bin/bash"
        RESOURCE_LOCK os_clipboard
        LABELS integration TIMEOUT 240 SKIP_RETURN_CODE 77)

    # Claude-IDE integration end to end: the editor hosts its IDE endpoint
    # (lock + MCP-over-WebSocket) and a worker thread drives a FAKE IDE client
    # through the real server - the WebSocket handshake with the lock token
    # (plus a bad-token rejection), then initialize, getWorkspaceFolders,
    # getOpenEditors, getDiagnostics and an openFile that must land the target
    # in the Script panel (cross-checked against the published bridge). It also
    # asserts the SELECTION CONTEXT reaches claude both ways: a file the editor
    # opened before the client connected surfaces via the initial-state push +
    # getCurrentSelection (open-then-connect), and a file opened while connected
    # arrives as a live selection_changed (connect-then-open) - both with the
    # cursor-only/active-file shape, no user action. Window-independent,
    # condition-driven. Both flavors (the seam is flavor-neutral).
    add_test(NAME editor_ide${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_ide${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_IDE_TEST=${Orkige_SOURCE_DIR}/projects/jumper-lua/scripts/player.lua;ORKIGE_EDITOR_IDE_TEST_PREOPEN=${Orkige_SOURCE_DIR}/projects/jumper-lua/scripts/game.lua"
        LABELS integration TIMEOUT 120 SKIP_RETURN_CODE 77)

    # project-scope MCP discovery file end to end: the editor's default-on MCP
    # endpoint (ephemeral loopback port + a token file written to a temp path)
    # comes up, and while a project is open the reconciler writes
    # <root>/.mcp.json carrying our marked orkige server entry at the live URL;
    # closing the project strips it back out (only-server -> file removed). The
    # env FORCES the interactive default-on path under an automated run and
    # names the source project the test copies to a temp dir. Both flavors.
    add_test(NAME editor_mcp_default${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_mcp_default${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_MCPDEFAULT_TEST=${Orkige_SOURCE_DIR}/projects/jumper-lua;ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 120)

    # the REAL-`claude`-binary drift alarm: launch the actual CLI pointed at the
    # editor's own lock/port and assert it dials the IDE endpoint (the protocol
    # is external and undocumented - a real connection here proves it still
    # matches). Skips (77) when claude is not on PATH, when it cannot be spawned,
    # or when it runs but never connects for an environment reason; a clean run
    # that never connects is a protocol-drift FAILURE. Both flavors.
    add_test(NAME editor_ide_claude${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_ide_claude${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_IDE_CLAUDE_TEST=1"
        LABELS "integration;device" TIMEOUT 120 SKIP_RETURN_CODE 77)

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

    # play-mode SCENE-VIEW MIRROR: play a scene with a falling RigidBody cube
    # and require the editor's OWN Scene-view node to mirror the running game's
    # motion (moving live, without dirtying the document), then be restored
    # EXACTLY to its authored pose on Stop. Both flavors.
    add_test(NAME editor_play_mirror${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_mirror${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_MIRROR_PLAYTEST=${Orkige_SOURCE_DIR}/tests/scenes/falling_cube.oscene")

    # mirror v2, runtime-SPAWN leg: on a temp copy of the mirror fixture
    # project, Play a scene whose script SPAWNS a moving object mid-play
    # (world.spawn off the project prefab). The editor world must gain a
    # mirror stand-in that MOVES (document untouched), lose it on the script's
    # despawn, and after Stop the authored scene must re-serialize
    # byte-exactly. Both flavors.
    add_test(NAME editor_play_mirror_spawn${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_mirror_spawn${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_MIRROR_SPAWN_PLAYTEST=${Orkige_SOURCE_DIR}/tests/projects/mirror")

    # mirror v2, mid-play SCENE-SWITCH leg: same fixture project, playing a
    # scene whose script requests a deferred switch (world.loadScene). The
    # editor's Scene view must swap to a VIEW-ONLY mirror document of the new
    # scene (sentinel present, authored object gone, path/dirty untouched) and
    # Stop must restore the authored document byte-exactly. Both flavors.
    add_test(NAME editor_play_mirror_switch${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_play_mirror_switch${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_MIRROR_SWITCH_PLAYTEST=${Orkige_SOURCE_DIR}/tests/projects/mirror")

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
        ENVIRONMENT "ORKIGE_EDITOR_MARQUEE=${Orkige_SOURCE_DIR}/projects/roller;ORKIGE_DEMO_FRAMES=700"
        # coarse backstop only: the run is deadline-driven inside the editor
        # (which fails with the step's own reason), and this wall just has to
        # exceed the bounded worst case - a gesture the display server
        # interrupts is redone, up to a logged limit - on a sanitizer-slowed
        # frame cadence.
        LABELS integration TIMEOUT 240)

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

    # THE AGENT LOOP: the standing gate on agent-buildability. The editor hosts
    # its MCP endpoint on an ephemeral in-process port and a worker thread
    # drives a RAW socket through the whole build-and-play arc an autonomous
    # agent would - new_project in a temp dir, new_scene, then the GAME authored
    # as TEXT via write_project_file (a Lua component that reads the named-action
    # map, a runtime-baked .ogui TrueType atlas and a .oui HUD - no binary asset
    # an agent could not write), assembled with create_object/add_component/
    # set_component, saved, PLAYED on the desktop target, and then DRIVEN:
    # send_input holds RIGHT for 12 frames and the Hero's streamed transform
    # must have advanced (real gameplay, not a smoke test), two injected SPACE
    # taps must each count exactly once with no key left stuck, the authored HUD
    # must be live in get_ui_layout and screenshot_game must land a readable PNG
    # of the running game. Frame-count driven: each gesture's own confirmation
    # (every injected frame stepped) is the synchronisation point, so there is
    # no sleep inside an assertion. Needs the built player; the temp project is
    # removed on every exit path.
    add_test(NAME editor_agent_loop${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_agent_loop${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_CONTROL_AGENTLOOP=${CMAKE_CURRENT_BINARY_DIR}/editor_agent_loop_shot.png;ORKIGE_BREADCRUMB_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor_agent_loop_breadcrumbs${ORKIGE_FLAVOR_TEST_SUFFIX}"
        # coarse backstop only, like editor_control_debug: the conversation's own
        # condition-driven polls (project+scene authoring, a player boot, three
        # gesture confirmations, a screenshot) carry the real deadlines and each
        # reports its exact failing step.
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

    # build settings, both groups: open projects/example, open Build > Project
    # Settings and prove the window draws AND that the line between the two
    # groups holds - the credentials this surface writes land in the editor's
    # own state directory (redirected here so the run never touches the real
    # user's), never inside the project, and the committed manifest carries
    # neither a credential nor a machine key. The headless half of the same
    # rule is EditorBuildSettingsTests.
    add_test(NAME editor_build_settings${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_build_settings${ORKIGE_FLAVOR_TEST_SUFFIX}
        PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_BUILDSETTINGS_TEST=${Orkige_SOURCE_DIR}/projects/example;ORKIGE_EDITOR_STATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor_build_settings${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_DEMO_FRAMES=60"
        LABELS integration TIMEOUT 120)

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
    # both render flavors: the module builds against THIS editor's own engine
    # tree (its flavor), into the flavor-suffixed native/build-<flavor> tree, so
    # the two flavor suites never share - and thus never poison - a cmake cache
    # (the break variant builds in a temp copy, but shares the RESOURCE_LOCK/
    # serialization story). See cmake/OrkigeGameModule.cmake.
    add_test(NAME editor_project_native_play${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_native_play${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_NATIVE_PLAYTEST=${Orkige_SOURCE_DIR}/projects/jumper-native;ORKIGE_JUMPER_NATIVE_SELFCHECK=1")

    # the deliberate-breakage variant: the hook copies jumper-native to a
    # TEMP directory (the real project is never touched), injects a syntax
    # error into the module source and presses Play - the build failure
    # must keep the editor in edit mode with [build] error lines in the
    # Console and nothing launched. Always compiles from scratch (fresh
    # temp copy), so it shares the fat TIMEOUT.
    add_test(NAME editor_project_native_play_break${ORKIGE_FLAVOR_TEST_SUFFIX} COMMAND orkige_editor
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(editor_project_native_play_break${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_EDITOR_NATIVE_PLAYTEST=${Orkige_SOURCE_DIR}/projects/jumper-native;ORKIGE_EDITOR_NATIVE_PLAYTEST_BREAK=1")

    # the ABI-stamp guard regression (the crash this whole find_package(Orkige)
    # package exists to prevent). Two properties in one hermetic, configure-only
    # run (no full module compile, so it stays fast): (1) the guard MECHANISM -
    # configure the jumper-native module against THIS engine tree's package but
    # with a version-mismatched expected stamp (a stand-in for a stale
    # liborkige_engine.a whose object layout no longer matches the headers) and
    # assert the module's cmake CONFIGURE refuses with the ABI-mismatch error;
    # (2) the stamp SCOPING, both directions - an engine-source edit moves the
    # stamp (a stale lib would be refused) while a game-file edit does NOT (the
    # guard stays silent through the ordinary edit-and-replay loop). The scoping
    # legs run against a throwaway synthetic git repo, so no real working-tree
    # file is ever touched and this races nothing. See cmake/OrkigeGameModule.cmake
    # + cmake/OrkigeAbiStamp.cmake + integration_driver/run_module_mismatch_test.py.
    add_test(NAME module_abi_mismatch${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_module_mismatch_test.py"
            --repo "${Orkige_SOURCE_DIR}"
            --engine-build "${CMAKE_BINARY_DIR}"
            --module-source "${Orkige_SOURCE_DIR}/projects/jumper-native/native"
            --cmake "${CMAKE_COMMAND}"
            --ninja "${CMAKE_MAKE_PROGRAM}"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(module_abi_mismatch${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        LABELS integration TIMEOUT 120)

    # the installed SDK pack: `cmake --install` this engine tree into a scratch
    # prefix, RENAME the result (a pack a consumer downloads is never at the
    # path it was built at), then prove a project's compiled C++ game code
    # configures, builds and RUNS against it with the engine source tree and
    # the engine build tree DENIED - a build tree at its usual absolute path
    # would otherwise silently satisfy a configure that should have failed.
    # Also: the whole engine header set must be IN the pack and compile from
    # it in one translation unit (a header missing from the install rules
    # fails here, not in a consumer's build), no build-machine path may
    # survive into the pack's cmake surface, and an edited installed header
    # must be a hard ABI-mismatch configure error. See cmake/OrkigeSdk.cmake +
    # Docs/sdk-pack.md. Skips (77) where the machine cannot run it.
    add_test(NAME sdk_pack${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_sdk_pack_test.py"
            --repo "${Orkige_SOURCE_DIR}"
            --engine-build "${CMAKE_BINARY_DIR}"
            --project "${Orkige_SOURCE_DIR}/projects/jumper-native"
            --shared-headers "${Orkige_SOURCE_DIR}/samples/jumper"
            --stage "${CMAKE_CURRENT_BINARY_DIR}/sdk-pack"
            --flavor "${ORKIGE_RENDER_BACKEND}"
            --cmake "${CMAKE_COMMAND}"
            --ignore-prefix-path "${CMAKE_IGNORE_PREFIX_PATH}"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(sdk_pack${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        SKIP_RETURN_CODE 77 LABELS integration TIMEOUT 1800
        # it compiles the same OGRE-including module sources the
        # compile-on-Play and export tests do (plus a translation unit over
        # every engine header); keep them off each other under -j
        RESOURCE_LOCK native_module_build)

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
        # launch, and shuts the simulator down again afterwards. The ONE
        # exception: a designated warm device (ORKIGE_CI_SIMULATOR_SHUTDOWN)
        # left Booted by an earlier failed run is shut down and reclaimed
        # (pickShutdownSimulator), so a retry re-boots the warm device
        # instead of cold-booting an arbitrary stock one. SKIPPED (exit 77)
        # when no shutdown simulator (or no built player app) exists.
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
            # a hosted runner boots even a warm simulator in 4-6 minutes, and
            # this leg took ~129s once booted - so the budget is the BOOT, not
            # the check
            TIMEOUT 900
            SKIP_RETURN_CODE 77)
    endif()

    # play mode on Android: same stop flow, but the player is the APK
    # launched on an adb-connected device/emulator (temp scene delivered via
    # adb push + run-as, debug link over an 'adb forward tcp' bridge).
    # SKIPPED (exit 77) when no adb device has the APK installed - build
    # (android-debug preset), package (`orkige_export android-player`)
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
    set_tests_properties(player_tilt_calibration_selfcheck PROPERTIES LABELS integration TIMEOUT 300
        ENVIRONMENT "ORKIGE_TILT_CALIB_SELFCHECK=1;ORKIGE_TILT_CALIB_FILE=${CMAKE_CURRENT_BINARY_DIR}/tilt_calib_selfcheck.osave")

    # haptics honest desktop no-op + API shape (synchronous, no render loop):
    # isAvailable() is false on desktop and play/pattern calls no-op cleanly
    # (see the ORKIGE_HAPTIC_SELFCHECK block in tools/player/main.cpp). The
    # device buzz itself is manual-only; the pure mapping is unit-tested.
    add_test(NAME player_haptic_selfcheck
        COMMAND orkige_player samples/scenes/example.oscene
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_haptic_selfcheck PROPERTIES LABELS integration TIMEOUT 300
        ENVIRONMENT "ORKIGE_HAPTIC_SELFCHECK=1")

    # THE pak-mount contract, end to end and flavor-neutral (the classic BigZip
    # acceptance test reborn): a STORED zip is built by a stdlib packer whose
    # "game/" sub-tree holds a v7 scene, a texture and an OGG. The player mounts
    # it (RenderSystem::mountPak), loads the scene THROUGH the resource system
    # (no fopen against a zip entry), resolves the texture from it and STREAMS
    # the OGG from it - all resolving like loose files. Runs on both render
    # flavors (mountPak is backend-neutral over MiniZip). The setup fixture
    # builds the pak into the build tree.
    # See the ORKIGE_PAK_SELFCHECK block in tools/player/PlayerSelfChecks.cpp.
    set(ORKIGE_PAK_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/pak_fixture${ORKIGE_FLAVOR_TEST_SUFFIX}/game.pak")
    add_test(NAME pak_fixture_build${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3 "${Orkige_SOURCE_DIR}/tests/pak/make_pak_fixture.py"
            "${ORKIGE_PAK_FIXTURE}" "${Orkige_SOURCE_DIR}/tests/assets/blip.ogg")
    set_tests_properties(pak_fixture_build${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        FIXTURES_SETUP PAK_FIXTURE${ORKIGE_FLAVOR_TEST_SUFFIX}
        LABELS integration TIMEOUT 30)
    add_test(NAME player_pak_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_player
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_pak_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        FIXTURES_REQUIRED PAK_FIXTURE${ORKIGE_FLAVOR_TEST_SUFFIX}
        LABELS integration TIMEOUT 60
        ENVIRONMENT "ORKIGE_PAK_SELFCHECK=${ORKIGE_PAK_FIXTURE}")

    # THE archive-in-place SCRIPT read (Stage 1 of the VFS unification): a STORED
    # zip carrying ONE "scripts/pak_script.lua" and nothing else. The player
    # mounts it (RenderSystem::mountPak) and a path-bound ScriptComponent whose
    # file lives ONLY in that pak - no --project, no loose file, so its on-disk
    # resolution MUST miss - LOADS AND RUNS from the mounted archive through the
    # injected ResourceReader (RenderSystem::readResourceText), no fopen. Both
    # render flavors (mountPak + the reader are backend-neutral). See the
    # ORKIGE_PAK_SCRIPT_SELFCHECK block in tools/player/PlayerSelfChecks.cpp.
    set(ORKIGE_SCRIPT_PAK_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/pak_fixture${ORKIGE_FLAVOR_TEST_SUFFIX}/script.pak")
    add_test(NAME script_pak_fixture_build${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3 "${Orkige_SOURCE_DIR}/tests/pak/make_script_pak_fixture.py"
            "${ORKIGE_SCRIPT_PAK_FIXTURE}")
    set_tests_properties(script_pak_fixture_build${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        FIXTURES_SETUP SCRIPT_PAK_FIXTURE${ORKIGE_FLAVOR_TEST_SUFFIX}
        LABELS integration TIMEOUT 30)
    add_test(NAME player_pak_script_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_player
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_pak_script_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        FIXTURES_REQUIRED SCRIPT_PAK_FIXTURE${ORKIGE_FLAVOR_TEST_SUFFIX}
        LABELS integration TIMEOUT 60
        ENVIRONMENT "ORKIGE_PAK_SCRIPT_SELFCHECK=${ORKIGE_SCRIPT_PAK_FIXTURE}")

    # THE AUTHORED TEXTURE SAMPLER AGAINST MOUNTED MEDIA - the setting a stored
    # APK and a browser export are structurally unable to read off disk. The
    # fixture packages a project the way PlayerBundle::isMountedMediaPath splits
    # one: the texture lives ONLY inside a STORED pak, while the manifest and
    # the scene are real files (opened by path), and there is NO .orkmeta
    # anywhere - a packaged payload carries none. The texture is authored
    # point/wrap, so the sprite can only sample that way if the export's baked
    # <TextureSamplers> manifest block reached it; a miss degrades silently to
    # bilinear/clamp. Both render flavors; the desktop is the cheap place to
    # prove a rule the packaged platforms share.
    # See the ORKIGE_PAK_SAMPLER_SELFCHECK block in tools/player/PlayerSelfChecks.cpp.
    set(ORKIGE_PAK_SAMPLER_DIR "${CMAKE_CURRENT_BINARY_DIR}/pak_sampler_fixture${ORKIGE_FLAVOR_TEST_SUFFIX}")
    add_test(NAME pak_sampler_fixture_build${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND python3 "${Orkige_SOURCE_DIR}/tests/pak/make_pak_project_fixture.py"
            "${ORKIGE_PAK_SAMPLER_DIR}")
    set_tests_properties(pak_sampler_fixture_build${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        FIXTURES_SETUP PAK_SAMPLER_FIXTURE${ORKIGE_FLAVOR_TEST_SUFFIX}
        LABELS integration TIMEOUT 30)
    add_test(NAME player_pak_sampler_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_player --project "${ORKIGE_PAK_SAMPLER_DIR}/project"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_pak_sampler_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        FIXTURES_REQUIRED PAK_SAMPLER_FIXTURE${ORKIGE_FLAVOR_TEST_SUFFIX}
        LABELS integration TIMEOUT 60
        ENVIRONMENT "ORKIGE_PAK_SAMPLER_SELFCHECK=${ORKIGE_PAK_SAMPLER_DIR}/game.pak")

    # whole-project run: --project loads projects/example's manifest,
    # registers its assets/ and scenes/ as resource locations and plays the
    # project's main scene (no scene argument)
    add_test(NAME player_example_project
        COMMAND orkige_player --project projects/example
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_example_project PROPERTIES
        ENVIRONMENT "ORKIGE_DEMO_FRAMES=90")

    # THE DEVICELESS RUN: the same project, the same player, with no window
    # and no GPU (ORKIGE_RENDERSYSTEM=NULL - @see engine_render/
    # RenderSystemSelection.h). It boots, loads the scene, ticks frames and
    # exits 0 without ever initialising SDL's video subsystem, so this is the
    # ONE player registration that carries LABELS unit: a display server is
    # not among its prerequisites, and if that ever regresses the unit preset
    # says so. Next flavor only - the classic flavor carries no deviceless
    # render system and refuses the request honestly instead.
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        add_test(NAME player_deviceless${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/example
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_deviceless${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_DEMO_FRAMES=90;ORKIGE_RENDERSYSTEM=NULL"
            LABELS unit TIMEOUT 120)
    endif()

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

    # THE PARAMETRIC MESH TIER, end to end: tests/projects/mesh
    # scenes/blockout.oscene - three ModelComponents naming three `.omesh` TEXT
    # assets (a seven-shape two-material blockout, a revolved `.oshape` profile
    # and an extruded `.oshape` outline with a window). The check asserts each
    # became a real LIT mesh resource: the two-material one carries exactly two
    # sub-meshes (same-material shapes merged, different ones split), the
    # revolve's and extrude's local bounds match the authored parameters, and
    # hiding all three drops the frame's triangle count by the whole generated
    # budget - so the geometry is genuinely submitted. Script-free on purpose:
    # it runs in noscript builds too. Both flavors (the facade
    # createMeshFromData road is implemented per backend; the geometry and the
    # parser are flavor-neutral). See the ORKIGE_MESH_SELFCHECK block in
    # tools/player/PlayerSelfChecks.cpp.
    add_test(NAME player_mesh_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
        COMMAND orkige_player tests/projects/mesh/scenes/blockout.oscene
            --project tests/projects/mesh
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_mesh_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        ENVIRONMENT "ORKIGE_MESH_SELFCHECK=1")

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

        # THE PROJECT'S OWN SUITE, run in Lua: projects/jumper-lua/tests/*.test.lua
        # exercise the game's real library (scripts/jumperlib.lua, which
        # scripts/player.lua runs on) and its shipped data/tuning.json, through
        # the SAME script.require and `data` reads the game uses. The exit code
        # is the verdict; the JSONL artifact lands in the build tree so a failed
        # run says WHICH test and WHY without scraping the log. Both flavors -
        # the tier is renderer-neutral, and running it on each proves the
        # runtime it rides in boots the same either way.
        #
        # BOTH TIERS, ONE RUN, ONE PLAYER BOOT: tests/playthrough.test.lua
        # declares a `scene`, so it runs in a live world - the player loads
        # that scene fresh per test and drives real frames while the test body
        # suspends on its waits. Each play-mode test carries its own frame
        # budget, so a wedged wait fails by name instead of burning the
        # timeout below.
        add_test(NAME player_project_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/jumper-lua --run-tests
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_project_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 180
            ENVIRONMENT "ORKIGE_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX}")

        # THE PURCHASE TIER END TO END, against the SIMULATED store - the leg a
        # real storefront cannot provide, because no CI machine has a signed
        # app, a store account or a device. projects/example carries a
        # store.ocatalog and asks for the simulator BY NAME in its manifest, and
        # tests/store.test.lua drives the whole road a game takes: the catalog
        # resolving to logical products, a purchase reaching game code at a
        # frame boundary, the entitlement cache, the no-ads link between the two
        # seams, a restore named through the catalog's reverse index, and the
        # refusals that have to say WHY. The platform bridge's own decisions are
        # proved separately and headlessly in tests/core (the ledger, the
        # request/delivery correlation and the two integer translations).
        # Filtered to its own file so the two monetization legs stay two
        # verdicts: an ads failure must not be reported as a store failure.
        add_test(NAME player_store_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/example --run-tests
                --test-filter store.test
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        # ORKIGE_PROGRESS_DIR/RESET isolate the save store into the build tree
        # and wipe it before each run: the test grants a purchase the way the
        # doc says to (durable FIRST, acknowledge SECOND), so it really does
        # write a save, and that write belongs nowhere near the source tree.
        set_tests_properties(player_store_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 180
            ENVIRONMENT "ALSOFT_DRIVERS=null;ORKIGE_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/store_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/store_lua_save${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_PROGRESS_RESET=1")

        # THE ADVERTISING TIER END TO END, against the SIMULATED ad surface -
        # the leg no real network can provide, because no CI machine has an ad
        # account and a real network answers what it feels like answering. The
        # engine MEDIATES rather than advertises, so what is proved here is
        # everything the engine owns: consent gating initialization (the
        # ordering constraint), the placement lifecycle, no-fill as an ordinary
        # answer, THE REWARD ARRIVING ONLY WITH THE EARNED SIGNAL - never on
        # the advert closing - and a no-ads purchase silencing the interruptive
        # formats while deliberately leaving opt-in rewarded running. Unhappy
        # paths are pinned through the `ads.sim` cvar, which exists only while
        # the simulator is installed. The per-unit state machine is proved
        # separately and headlessly in tests/core/AdPlacementTests.cpp.
        add_test(NAME player_ads_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/example --run-tests
                --test-filter ads.test
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        # the same save isolation the store leg needs, and for the same reason:
        # the reward is granted the way the doc says to (durable, inside the
        # callback), so this really does write a save.
        set_tests_properties(player_ads_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 180
            ENVIRONMENT "ALSOFT_DRIVERS=null;ORKIGE_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ads_lua_tests${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_PROGRESS_DIR=${CMAKE_CURRENT_BINARY_DIR}/ads_lua_save${ORKIGE_FLAVOR_TEST_SUFFIX};ORKIGE_PROGRESS_RESET=1")

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

        # DYNAMIC 3D LINES, end to end: tests/projects/lines scenes/lines.oscene
        # - an authored LineComponent strip (its points round-trip through the
        # scene file), reshaped every frame at the SAME point count by a script
        # so it rides the LineMesh dynamic updateVertices fast path (the rebuild
        # count stays the small boot constant, no per-frame churn), plus the
        # immediate-mode `draw` table queuing line/box/sphere primitives whose
        # DebugDraw mesh reaches the expected segment vertex count and then
        # DRAINS once the script stops drawing (frame-only + TTL lifetimes). Runs
        # on both render flavors (the line mesh + collector are renderer-neutral;
        # the upload rides the LineMesh dynamic path on each backend). See the
        # ORKIGE_LINES_SELFCHECK block in tools/player/PlayerSelfChecks.cpp.
        add_test(NAME player_lines_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player tests/projects/lines/scenes/lines.oscene
                --project tests/projects/lines
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_lines_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
            ENVIRONMENT "ORKIGE_LINES_SELFCHECK=1")

        # RAW INPUT REACH, end to end: tests/projects/rawinput - synthetic
        # fingers, a pointer press and a controller are injected on the REAL
        # SDL path while a game runs, and the fixture script reads them back
        # through the `input` table (touch phases + window-pixel positions +
        # per-frame delta, the pointer's one-frame press edge, the raw pad
        # reads) AND through the named action map (the built-in "jump"/"move"
        # answering a pad with zero project authoring). What only a running
        # game can prove is the FRAME WIRING: the once-per-frame snapshot
        # taken in the loop's input slot before the scripts that read it. The
        # pure halves are unit-tested in orkige_engine_tests. Renderer-neutral,
        # so it runs on both flavors. See the ORKIGE_RAWINPUT_SELFCHECK block
        # in tools/player/PlayerSelfChecks.cpp.
        add_test(NAME player_raw_input_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player tests/projects/rawinput/scenes/rawinput.oscene
                --project tests/projects/rawinput
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_raw_input_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
            ENVIRONMENT "ORKIGE_RAWINPUT_SELFCHECK=1")

        # THE UI GALLERY, end to end: projects/gallery - the example project
        # that shows EVERY runtime widget kind on one declarative .oui screen
        # suite (labels plain/wrapped/localised, buttons incl. nine-slice,
        # checkbox, toggle group, slider, progress bar, select menu, dropdown,
        # single- and multi-line text entry, scroll view, a 1000-row virtualized
        # list, a tab bar, decor draw modes, modal/confirm/toast triggers and a
        # safe-area HUD). The selfcheck drives the REAL input path with
        # synthetic mouse/key/text events and asserts: every tab is reachable by
        # a click, the WRAPPED label is taller than the single-line one, a typed
        # Return INSERTS a line break in the multi-line field (never submits),
        # the 1000-row list keeps only its viewport window of rows alive and a
        # scrolled row still hit-tests at its rect, a modal raises and
        # dismisses, the STYLING tier resolved (a named style's font by role
        # name / ink / scale reached its widgets and a local key overrode the
        # bundle), INLINE RICH TEXT resolved (a markup label reports itself as
        # such and its tags cost no width, a heading run plus an inline icon
        # raise the wrapped block) and the ANIMATION tier really animates (the
        # title's load-time enter settled at exactly full opacity, a revealed
        # tab panel's alpha sampled mid-flight at two ticks then landing on the
        # EXACT rest position - on a replay too, so a re-entered transition
        # never drifts - a held button scaled down, and a Lua guitween round
        # trip returned its widget to exactly alpha 1). Runs on both render
        # flavors (gui renders through the DrawLayer2D facade). See the
        # ORKIGE_GALLERY_SELFCHECK block in tools/player/PlayerSelfChecks.cpp.
        add_test(NAME player_gallery_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project projects/gallery
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_gallery_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_GALLERY_SELFCHECK=1"
            LABELS integration TIMEOUT 120)

        # SHAPE COLLIDERS, end to end: tests/projects/shapecollider
        # scenes/shapecollider.oscene - a dynamic ball drops into a STATIC
        # CONCAVE U-cup collider (RigidBodyComponent ST_SHAPE defaulting to the
        # sibling VectorShapeComponent's `.oshape`) and settles INSIDE the notch,
        # well below the cup rim - a position a box approximation of the same
        # outline makes unreachable (the concavity existence proof); a second
        # DYNAMIC ST_SHAPE body (explicit shapeAsset, no sibling shape) drops onto
        # a floor and rests as its convex HULL. Runs on both render flavors (the
        # collision geometry is renderer-neutral - built by the pure
        # core_util/ShapeCollider and consumed by the Jolt facade). See the
        # ORKIGE_SHAPECOLLIDER_SELFCHECK block in tools/player/PlayerSelfChecks.cpp.
        add_test(NAME player_shapecollider_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player
                tests/projects/shapecollider/scenes/shapecollider.oscene
                --project tests/projects/shapecollider
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_shapecollider_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_SHAPECOLLIDER_SELFCHECK=1")

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
        set_tests_properties(player_vectoranim_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
            ENVIRONMENT "ORKIGE_VECTORANIM_SELFCHECK=1")

        # the WATER TEST BENCH project (projects/watertest, generated by
        # Util/make_watertest_project.py): a hand-testing playground - water
        # plane over a bright bed from a raised vantage. The boot contract:
        # the scene loads, the camera script runs (a script error disables the
        # instance but the run still exits 0, so the guard greps stderr in the
        # driver-less form ctest can express: the player exits non-zero on a
        # scene/project load failure, which is the regression this catches).
        add_test(NAME player_watertest_boot${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player scenes/main.oscene
                --project projects/watertest
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_watertest_boot${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_DEMO_FRAMES=90"
            LABELS integration TIMEOUT 120)

        # TEXTURED CUTOUT PARTS on a vector rig, end to end:
        # projects/vectorshapes scenes/cutout.oscene - a hand-authored v3
        # rig mixing one flat-colour region (the shadow) with three textured
        # quad parts (body, two-band head, waving arm). The player asserts
        # the per-texture draw-run split and that the wave clip animates the
        # pose through the dynamic per-section upload; the driver then
        # PIXEL-PROBES the mid-clip screenshot - each texture's signature
        # colour really samples (never a tint-coloured silhouette), the
        # head's dark band renders ABOVE its light band (the texture-row-0-
        # at-rect-top v contract) and the flat shadow coexists. Runs on both
        # render flavors. See the ORKIGE_CUTOUT_SELFCHECK block in
        # tools/player/PlayerSelfChecks.cpp + run_cutout_test.py.
        add_test(NAME player_cutout_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_cutout_test.py"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/cutout${ORKIGE_FLAVOR_TEST_SUFFIX}"
                --source "${Orkige_SOURCE_DIR}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_cutout_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 240)

        # 3D SKELETAL character animation, end to end: tests/projects/character
        # scenes/main.oscene - a generated blocky mannequin (assets/
        # character_rig.glb from Util/make_character_rig.py: a 7-joint skeleton,
        # per-vertex weights, walk + idle clips) with a ModelComponent +
        # AnimationComponent. The check plays walk and asserts the skeleton-driven
        # bounds SPREAD (a swinging limb moves the skinned vertices) and, at an
        # interpolated mid-clip frame, reads the four limb bones through the
        # facade to assert the figure keeps its SHAPE (legs' world-X separation
        # near the bind gap, arms above legs) - closing the gap that motion-only
        # bounds leave, since a pose COLLAPSE to the skeleton root still spreads
        # the bounds. Then crossFadeTo blends walk->idle (both clips run together,
        # then walk drops) and idle's sway keeps moving the bounds. Runs on both flavors - the
        # DRIFT ALARM between the two importer roads (classic = the upstream
        # assimp codec, next = MeshLoaderNext over the neutral SkinnedRig
        # extraction; Docs/character-animation.md): the same generated rig must
        # produce comparable measured motion on each. On a hypothetical flavor
        # that imports glTF statically, the rig loads with no animations and the
        # check SKIPS HONESTLY (exit 0) with that finding as the message. See the
        # ORKIGE_CHARACTER_RIG_SELFCHECK block in PlayerSelfChecks.
        add_test(NAME player_character_rig_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player tests/projects/character/scenes/main.oscene
                --project tests/projects/character
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_character_rig_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_CHARACTER_RIG_SELFCHECK=1")

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

        # procedural sound effects, end to end: tests/projects/sfx carries a
        # `.osfx` TEXT asset (a preset seed plus overrides) instead of a wave
        # file, and the leg proves it IS a sound file to everything above -
        # the mixer holds exactly the bytes the pure synthesizer renders from the
        # same asset (the device-side proof), the source plays, the unchanged
        # SoundComponent/Lua surface added it into its mixer group, and a
        # deliberately malformed sibling asset stays honestly silent instead of
        # unwinding into game code. See the ORKIGE_SFX_SELFCHECK block in
        # tools/player/PlayerSelfChecks.cpp. Runs on both flavors (the
        # synthesizer is pure core, the upload is renderer-neutral); the null
        # audio device every test in this directory runs on makes the
        # audible branch the one that executes.
        add_test(NAME player_sfx_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/sfx
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_sfx_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_SFX_SELFCHECK=1")

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
        set_tests_properties(player_breadcrumb_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
            ENVIRONMENT "ORKIGE_BREADCRUMB_SELFCHECK=1;ORKIGE_BREADCRUMB_DIR=${CMAKE_CURRENT_BINARY_DIR}/breadcrumb_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}")

        # the fatal-signal crash marker, end to end: the driver runs the player
        # once with ORKIGE_CRASH_SELFCHECK arming the marker + raising SIGSEGV at
        # a known frame (the run MUST die by the signal), then reboots it clean
        # and asserts (a) the "previous run crashed" boot warning and (b) the
        # rotated breadcrumbs.prev.jsonl ending in a "crash" crumb. NOTE: the
        # deliberate crash drops a macOS DiagnosticReports / Linux core entry per
        # run - expected. Under a sanitizer build the marker stands down (ASan
        # owns the fatal handlers) and the driver SKIPs (77). Both flavors - the
        # crash marker is renderer-neutral. Isolated breadcrumb dir in the build
        # tree. See the ORKIGE_CRASH_SELFCHECK block in tools/player/main.cpp and
        # Breadcrumbs::installCrashHandler.
        add_test(NAME player_crash_marker_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_crash_marker_test.py"
            --binary "$<TARGET_FILE:orkige_player>"
            --project tests/projects/crash
            --crash-dir "${CMAKE_CURRENT_BINARY_DIR}/crash_marker_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_crash_marker_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 300)

        # full-screen fade transition, end to end: tests/projects/fade drives a
        # fade-out -> deferred scene switch (sceneA -> sceneB, applied while the
        # overlay is opaque) -> fade-in through the real render loop; asserts the
        # overlay reached opacity, the switch happened while covered, and the fade
        # cleared (see the ORKIGE_FADE_SELFCHECK block in tools/player/main.cpp).
        # Runs on BOTH flavors (the fade is facade-only).
        add_test(NAME player_fade_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player --project tests/projects/fade
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_fade_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
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
        set_tests_properties(player_lifecycle_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
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
        set_tests_properties(player_perf_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES LABELS integration TIMEOUT 300
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

        # persistent objects across a mid-play scene switch, end to end
        # (tests/projects/persistent): Hero is marked persistent in levelA and
        # carries a rigid body + a counting script sandbox. It lands on levelA's
        # ground then requests world.loadScene("scenes/levelB.oscene"); the
        # deferred switch must SPARE Hero (whole live state) while the rest of
        # the world tears down. The selfcheck asserts post-switch that Hero's
        # sandbox kept counting (init once, ticks never reset), its body
        # re-collided with levelB's ground, the non-persistent sibling is gone
        # and levelB's own object loaded (see the ORKIGE_PERSISTENT_SELFCHECK
        # block in tools/player/PlayerSelfChecks.cpp). Both flavors.
        add_test(NAME player_persistent_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player scenes/levelA.oscene --project tests/projects/persistent
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_persistent_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_PERSISTENT_SELFCHECK=1"
            LABELS integration TIMEOUT 120)

        # the generic component enable/disable feature's LIVE side effects, both
        # flavors (the headless ComponentEnableTests cover the schema/inheritance/
        # serialization half). Self-contained: the selfcheck spawns its own
        # objects and inits physics, so it boots on any scene (a minimal fixture
        # here) and asserts a disabled RigidBodyComponent leaves the Jolt sim and
        # re-enters at rest, and that enabled composes with the owner active state
        # (the ORKIGE_COMPONENT_ENABLE_SELFCHECK block in PlayerSelfChecks.cpp).
        add_test(NAME player_component_enable_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND orkige_player scenes/lines.oscene --project tests/projects/lines
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_component_enable_selfcheck${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            ENVIRONMENT "ORKIGE_COMPONENT_ENABLE_SELFCHECK=1"
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
            LABELS integration TIMEOUT 540)
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
            LABELS integration TIMEOUT 540)
        # the results-card RESTART button, per flavor: run the tour to its
        # results card, then the director's autoRestart seam fires the SAME
        # restartTour() path the button's click calls. Asserts (off the
        # director's own log) that the button EXISTS on the card with a
        # touch-friendly, safe-area-clamped rect clear of the results panel,
        # that the restart path fired, and that the sequence actually replayed
        # from scene 1 (a fresh Terrace Vista init after the card). The
        # interactive click and the seam share one restart path, so this proves
        # the button's wiring without a synthetic mouse event.
        add_test(NAME player_benchmark_restart${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_restart_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_restart${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_benchmark_restart${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 540)
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

        # the scene-authored atmosphere end to end, per flavor
        # (projects/benchmark fixture_atmo* scenes): a component-carrying
        # scene boots the DAY sky with NO scripts (pixel band vs the
        # component-less control), deactivating the owning instance promotes
        # the dormant night sibling (the sky visibly switches between two
        # captures of one boot), and a script's engine:setAtmosphere beats
        # the component base after boot.
        add_test(NAME player_atmosphere${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_atmosphere_component_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --player "$<TARGET_FILE:orkige_player>"
                --dir "${CMAKE_CURRENT_BINARY_DIR}/atmosphere${ORKIGE_FLAVOR_TEST_SUFFIX}"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(player_atmosphere${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
            LABELS integration TIMEOUT 480)

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
        # them out on classic + web);
        # vistashadow = the daylit vista rendered with r.shadowQuality off vs
        # medium must show a shadow-darkened terrain band (the casting sun
        # throws REAL shadows on this flavor - differential, so it needs no
        # fixed shadow positions).
        # anim = the character-cast vignette: two frozen-camera captures apart
        # in time - the skinned crowd band must CHANGE while a static sky band
        # holds, the 2D foreground cutout hero must show its colour signature,
        # and the scripts must report the animation seam working (self.animation
        # + world.getAnimation drove the rigs).
        # terraintex = the lake terrain's tiling grass texture must actually
        # TILE: the terrain band must carry high-frequency detail, not collapse
        # to a flat averaged colour (the content-material texture-WRAP regression
        # that clamped tiling UVs on the next flavor).
        foreach(ORKIGE_SCENE_PROBE lumens field hud2x flatland vistashadow anim terraintex)
            add_test(NAME player_benchmark_${ORKIGE_SCENE_PROBE}_probe${ORKIGE_FLAVOR_TEST_SUFFIX}
                COMMAND python3
                    "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_benchmark_scene_probe.py"
                    --repo "${Orkige_SOURCE_DIR}"
                    --player "$<TARGET_FILE:orkige_player>"
                    --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_${ORKIGE_SCENE_PROBE}_probe${ORKIGE_FLAVOR_TEST_SUFFIX}"
                    --probe "${ORKIGE_SCENE_PROBE}"
                WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
            set_tests_properties(player_benchmark_${ORKIGE_SCENE_PROBE}_probe${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
                LABELS integration TIMEOUT 540)
        endforeach()

        # cross-FLAVOR look parity (run_crossflavor_parity_test.py): the lake
        # vignette captured on BOTH flavors' players and compared region-wise
        # (sky/water/terrain band means + the water's sun-streak highlight) -
        # the one gate that notices the two flavors drifting apart visually
        # (every other probe bands each flavor separately). Registered on the
        # next (default) tree only; SKIPS (exit 77) when the sibling classic
        # tree's player is not built, so this road bites on a developer machine
        # carrying both trees (the host's conventional classic preset,
        # ORKIGE_PARITY_CLASSIC_TREE - the same one the facade parity gates
        # read). A build job holds ONE flavor, so CI reaches the same
        # comparison the other way: each flavor job captures its frames with
        # the driver's --capture mode and the render-parity job compares them.
        if(ORKIGE_RENDER_BACKEND STREQUAL "next")
            add_test(NAME benchmark_crossflavor_parity
                COMMAND python3
                    "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_crossflavor_parity_test.py"
                    --repo "${Orkige_SOURCE_DIR}"
                    --player-next "$<TARGET_FILE:orkige_player>"
                    --player-classic "${Orkige_SOURCE_DIR}/${ORKIGE_PARITY_CLASSIC_TREE}/tools/player/$<TARGET_FILE_NAME:orkige_player>"
                    --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_crossflavor_parity"
                WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
            set_tests_properties(benchmark_crossflavor_parity PROPERTIES
                LABELS integration TIMEOUT 900 SKIP_RETURN_CODE 77)
            # the planar-mirror sibling scene, its own measured regions
            # (waterline mirror strips, the rock-mirror band, the open
            # water) - the standing cross-flavor gate on planar mirror
            # STRENGTH parity: the water-reflection probe floors each
            # flavor separately, only this comparison notices the two
            # calibrations drifting apart on real scene content.
            add_test(NAME benchmark_crossflavor_parity_mirror
                COMMAND python3
                    "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_crossflavor_parity_test.py"
                    --repo "${Orkige_SOURCE_DIR}"
                    --player-next "$<TARGET_FILE:orkige_player>"
                    --player-classic "${Orkige_SOURCE_DIR}/${ORKIGE_PARITY_CLASSIC_TREE}/tools/player/$<TARGET_FILE_NAME:orkige_player>"
                    --scene "scenes/mirrorlake.oscene"
                    --dir "${CMAKE_CURRENT_BINARY_DIR}/benchmark_crossflavor_parity_mirror"
                WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
            set_tests_properties(benchmark_crossflavor_parity_mirror PROPERTIES
                LABELS integration TIMEOUT 900 SKIP_RETURN_CODE 77)
            # water_mirror_wobble: the wave-perturbed mirror EXISTENCE gate.
            # Ogre's native planar reflection is a flat screen-projected mirror
            # (glassy no matter the ripple); the next backend overrides the
            # DoPlanarReflectionsPS piece so the mirror UV rides the wave slope,
            # matching the classic water program. This gate boots Mirror Lake
            # with the camera frozen, captures several wave phases and asserts
            # the reflective band's between-phase luminance change clears the
            # glassy floor - guarding the fix from silently regressing back to a
            # static mirror. Gates the next flavor ALWAYS (runs on next-only CI
            # too, unlike the sibling parity gates); also gates classic when its
            # sibling player is built. SKIPS (77) only if the next player is
            # absent.
            add_test(NAME water_mirror_wobble
                COMMAND python3
                    "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_water_wobble_probe_test.py"
                    --repo "${Orkige_SOURCE_DIR}"
                    --player-next "$<TARGET_FILE:orkige_player>"
                    --player-classic "${Orkige_SOURCE_DIR}/${ORKIGE_PARITY_CLASSIC_TREE}/tools/player/$<TARGET_FILE_NAME:orkige_player>"
                    --dir "${CMAKE_CURRENT_BINARY_DIR}/water_mirror_wobble"
                WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
            set_tests_properties(water_mirror_wobble PROPERTIES
                LABELS integration TIMEOUT 1200 SKIP_RETURN_CODE 77)
        endif()
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
    # texture cook (`orkige_export cook-textures`, the same code an export
    # runs over its payload) over a copy of the asset_rename fixture and boots the player on the cooked
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
            --exporter "$<TARGET_FILE:orkige_export>"
            --flavor "${ORKIGE_RENDER_BACKEND}"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    set_tests_properties(player_cooked_textures${ORKIGE_FLAVOR_TEST_SUFFIX} PROPERTIES
        LABELS integration TIMEOUT 300)

    # --- project export (tools/exporter) ----------------------------------
    # Both render flavors: the exporter bundles the tree's engine media
    # (classic RTSS libs or Ogre-Next Hlms templates - keyed off
    # ORKIGE_RENDER_BACKEND) and each flavor prefers its own Release sibling
    # (build/macos-release-classic vs build/macos-release - see
    # siblingReleaseTree in tools/exporter/ExportBuildTree.h). The exported-app
    # contract is verified on the running tree's flavor.
    # APPLE-gated: the export driver checks/runs .app bundles with otool -
    # macOS host machinery (a Linux registration would hard-fail at otool).
    # TODO(linux): a Linux export target needs its own bundle layout +
    # driver checks in tools/exporter first.
    if(APPLE)
        # The driver (integration_driver/run_export_test.py) runs the exporter,
        # asserts the artifact's structure and self-containment, and - on macOS -
        # RUNS the exported app from a NEUTRAL cwd (the output dir, never the
        # source tree, whose files could mask a missing bundled resource): a
        # clean frame-capped exit proves the bundle genuinely stands alone.
        set(ORKIGE_EXPORT_DRIVER
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_export_test.py")
        # its device sibling: export, install, launch, wait for the bundled
        # project to boot (@see run_export_device_test.py)
        set(ORKIGE_EXPORT_DEVICE_DRIVER
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_export_device_test.py")
        # ...and the third question, which neither of the two above asks:
        # does the game that SHIPS still pass its own tests? A TEST BUILD
        # (`--with-tests`) carries the project's tests/ suite into the payload
        # and marks the artifact to run it; the verdict comes back as the
        # runner's JSONL report (@see run_export_tests.py).
        set(ORKIGE_EXPORT_TESTS_DRIVER
            "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_export_tests.py")

        # the pure-Lua jumper as a double-clickable macOS .app (release player
        # when build/macos-release carries one, else debug + warning)
        add_test(NAME export_macos_lua
            COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform macos --engine-build "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/macos-lua"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")

        # the same project's own Lua suite, run INSIDE the package: the
        # payload's cooked textures, baked samplers and staged media are what
        # the tests see, so a payload that quietly dropped a file fails here
        # instead of shipping. No device, so this is not device-labeled.
        add_test(NAME export_macos_tests${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3 "${ORKIGE_EXPORT_TESTS_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform macos --engine-build "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/macos-tests"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_macos_tests${ORKIGE_FLAVOR_TEST_SUFFIX}
            PROPERTIES LABELS integration TIMEOUT 900)

        # the native-module jumper: the exporter builds the module executable
        # (arch-pinned, into the per-flavor native/build-export-<flavor> tree -
        # separate from the editor's compile-on-Play tree) and ships IT instead
        # of the generic player. Both render flavors: the exporter resolves the
        # module + engine closure for the given tree's flavor
        # (cmake/OrkigeGameModule.cmake), so each flavor's tree checks the export
        # of ITS OWN native module.
        add_test(NAME export_macos_native${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-native"
                --platform macos --engine-build "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/macos-native"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")

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
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform ios-simulator
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-lua"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_ios_simulator PROPERTIES SKIP_RETURN_CODE 77)

        # the Android ASSEMBLY, run for real against this machine's own SDK
        # without an android-* build tree: the engine source is synthesised
        # (a CMake cache, a stand-in library and shader tree) and everything
        # else is the real thing - the real SDK programs, a real signed
        # archive that `apksigner verify` and `aapt2 dump` accept.
        # export_android proves the same road with a real cross tree, which
        # most machines do not have; this one skips (77) only when the SDK
        # build tools, a platform jar or a JDK are missing.
        #
        # Two phases. The PLAYER phase packages the dev player off the real
        # SDL Java glue and announces itself skipped where the vcpkg root has
        # no sources. The LIBRARY phase packages a project depending on an
        # ANDROID LIBRARY ARCHIVE generated by the driver (never a committed
        # binary) and asserts what fails SILENTLY otherwise: the archive's
        # permission, component, meta-data, asset, native library and
        # generated resource ids all reach the finished package, and the two
        # conflicts that would ship a broken app are refused by name.
        #
        # The no-shell property itself is a unit gate
        # (ExportAndroidAssembleTests.cpp) - this asserts the other half, that
        # the argv road produces a package the platform accepts.
        add_test(NAME export_android_assemble
            COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/integration_driver/run_android_assemble_test.py"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-assemble"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_android_assemble PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 600)

        add_test(NAME export_android
            COMMAND python3 "${ORKIGE_EXPORT_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
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
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform android-aab
                --engine-build "${Orkige_SOURCE_DIR}/build/android-debug"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-aab-lua"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_android_aab PROPERTIES SKIP_RETURN_CODE 77)

        # ...and the same package RUN. A structural check proves what a package
        # CONTAINS; only an install-and-launch proves that what it contains is
        # enough - a payload missing a scene, a shader tree or a texture
        # installs perfectly and then boots into nothing. Device-labeled: needs
        # a BOOTED simulator (this test never boots one - that costs minutes
        # and belongs to the tests that exist for it), skipping 77 otherwise.
        add_test(NAME export_ios_simulator_run
            COMMAND python3 "${ORKIGE_EXPORT_DEVICE_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform ios-simulator
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-run"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_ios_simulator_run PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 900)

        # the project's own Lua suite, run on the DEVICE it ships to: the
        # package is installed and launched exactly as a shipped one is, and
        # the runner's JSONL report is read back out of the app's own data
        # container - the existing ORKIGE_TEST_REPORT_DIR seam, pointed at a
        # directory the host can read. A report with no summary is a run that
        # DIED and a report over zero tests is a package that carried no suite;
        # both fail by name, because a harness that reports success when
        # nothing ran is the one outcome worth nothing at all.
        add_test(NAME export_ios_simulator_tests
            COMMAND python3 "${ORKIGE_EXPORT_TESTS_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform ios-simulator
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-tests"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_ios_simulator_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 1200)

        # ...and the SAME run for the package a DOWNLOADED editor produces. It
        # carries no phone player, so it packages from a fetched device payload
        # beside its own staged resources (Docs/device-payloads.md), and
        # editor_bundle_ios proves the editor drives exactly that - by
        # comparing BYTES. Bytes are not a game: a package whose engine media
        # arrived from the payload rather than the source tree installs
        # perfectly and can still boot into nothing, which is why the payload
        # path gets the same install-and-launch verdict as the build tree. The
        # payload is composed by the packaging tool's OWN function, so the
        # shipped composition is what runs. Device-labeled and skipping 77 for
        # the same reasons as its sibling above.
        add_test(NAME export_ios_simulator_payload_run
            COMMAND python3 "${ORKIGE_EXPORT_DEVICE_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform ios-simulator
                --engine-source payload
                --host-build "${CMAKE_BINARY_DIR}"
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-payload-run"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_ios_simulator_payload_run PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 900)

        # ...and the same run for the OTHER kind of app: a project whose game
        # code is COMPILED ships the module itself, so there is no player to
        # copy. The engine is an installed iOS SDK pack - installed off the
        # simulator tree here and RELOCATED, the way a downloaded one arrives -
        # and the exporter is handed nothing else, so the app on the device was
        # cross-compiled and linked entirely out of the pack. That is the whole
        # claim of a cross pack, and only an install-and-launch settles it: a
        # module that links against the wrong architecture's archives, or a
        # bundle without its platform entry point or its shader media, is a
        # package that installs perfectly and never draws a frame. Device-
        # labeled and skipping 77 for the same reasons as its siblings above.
        add_test(NAME export_ios_simulator_native_run
            COMMAND python3 "${ORKIGE_EXPORT_DEVICE_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-native"
                --platform ios-simulator
                --engine-source pack
                --log-tag jumper_native
                --shared-headers "${Orkige_SOURCE_DIR}/samples/jumper"
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-simulator-native-run"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_ios_simulator_native_run PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 1800)

        add_test(NAME export_android_run
            COMMAND python3 "${ORKIGE_EXPORT_DEVICE_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform android
                --engine-build "${Orkige_SOURCE_DIR}/build/android-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-run"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_android_run PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 900)

        # ...and the APK a DOWNLOADED editor produces: assembled out of a
        # fetched device payload with NO engine build tree in the picture, then
        # installed and launched exactly like its build-tree sibling. Bytes are
        # not a game - a package whose player, media, dex and Java all arrived
        # through a payload installs perfectly and can still boot into nothing.
        add_test(NAME export_android_payload_run
            COMMAND python3 "${ORKIGE_EXPORT_DEVICE_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --exporter "$<TARGET_FILE:orkige_export>"
                --project "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                --platform android
                --engine-source payload
                --host-build "${CMAKE_BINARY_DIR}"
                --engine-build "${Orkige_SOURCE_DIR}/build/android-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-payload-run"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(export_android_payload_run PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 900)
    endif()

    # the HTTP client on a REAL mobile runtime. The desktop suite proves the
    # client's contract in process; these run it inside an exported app on a
    # device, against a loopback server the driver hosts: status codes, a
    # response header, a POST echo, progress over a paced multi-hundred-
    # kilobyte body, the response cap, a timeout, a mid-transfer cancel,
    # save-to-file (whose bytes the driver reads back OFF the device) and the
    # whole plain-http/https policy gate - the last one riding on top of each
    # platform's OWN cleartext gate (iOS app transport security, Android's
    # network security config), so a packaged app that never declared its
    # loopback exemption fails it. The app reaches the host directly on a
    # simulator and through 'adb reverse' on Android, and reports its verdict
    # lines back over the same channel.
    # SKIPPED (exit 77) when no device/emulator/simulator is attached or the
    # platform's player is not built. Once the app has LAUNCHED, no verdict is
    # a failure - never a skip.
    set(ORKIGE_DEVICE_HTTP_DRIVER
        "${Orkige_SOURCE_DIR}/tests/integration_driver/run_device_http_test.py")
    # the mobile preset trees are flavor-suffixed like the desktop ones (the
    # export block above computes this too, but only on Apple hosts)
    if(ORKIGE_RENDER_BACKEND STREQUAL "classic")
        set(ORKIGE_MOBILE_TREE_SUFFIX "-classic")
    else()
        set(ORKIGE_MOBILE_TREE_SUFFIX "")
    endif()
    if(UNIX)
        add_test(NAME http_device_android
            COMMAND python3 "${ORKIGE_DEVICE_HTTP_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --platform android
                --engine-build "${Orkige_SOURCE_DIR}/build/android-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/android-httptest"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(http_device_android PROPERTIES
            SKIP_RETURN_CODE 77)
    endif()
    if(APPLE)
        add_test(NAME http_device_ios
            COMMAND python3 "${ORKIGE_DEVICE_HTTP_DRIVER}"
                --repo "${Orkige_SOURCE_DIR}"
                --platform ios-simulator
                --engine-build "${Orkige_SOURCE_DIR}/build/ios-simulator-debug${ORKIGE_MOBILE_TREE_SUFFIX}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/export/ios-httptest"
            WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
        set_tests_properties(http_device_ios PROPERTIES SKIP_RETURN_CODE 77)
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

        # script-debugger contract: the driver spawns a real player on the
        # jumper-lua project, sets a breakpoint on update()'s first statement
        # (located by scanning scripts/player.lua - never a hardcoded line),
        # and asserts break-hit/stack/locals/expand/step/re-hit/mid-break
        # deferral/clear/disconnect-auto-resume end to end (see
        # script_debug_driver.cpp). Both flavors register it - the debugger
        # rides the flavor-neutral script runtime.
        add_executable(script_debug_driver
            integration_driver/script_debug_driver.cpp)
        target_link_libraries(script_debug_driver PRIVATE Orkige::Core)
        # the fixture project (tests/projects/scripterror) whose boom.lua throws
        # an uncaught error once cvar test.boom is set drives the BREAK ON SCRIPT
        # ERROR legs (argv[3]): armed pauses AT the fault with text/stack/locals
        # and the honest failure still flows on Continue; unarmed is unchanged.
        add_test(NAME player_script_debug${ORKIGE_FLAVOR_TEST_SUFFIX}
            COMMAND script_debug_driver $<TARGET_FILE:orkige_player>
                "${Orkige_SOURCE_DIR}/projects/jumper-lua"
                "${Orkige_SOURCE_DIR}/tests/projects/scripterror"
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
        demo_worldtext
        demo_light demo_material demo_terrain demo_sky demo_ui_scale demo_ui_safe_area demo_textentry demo_ttf
        demo_synth_esc editor_selfcheck editor_resize editor_edittest
        editor_selfcheck_next editor_resize_next editor_edittest_next
        player_example_scene player_example_project player_vectorshape_scene
        player_debug_pause player_script_debug player_script_debug_next
        player_jumper_lua_selfcheck player_roller_selfcheck
        player_jumper_lua_selfcheck_next player_roller_selfcheck_next
        player_project_lua_tests player_project_lua_tests_next
        player_softbody_selfcheck player_softbody_selfcheck_next
        player_mesh_selfcheck player_mesh_selfcheck_next
        player_shapecollider_selfcheck player_shapecollider_selfcheck_next
        player_character_rig_selfcheck player_character_rig_selfcheck_next
        player_roller_progression_selfcheck
        player_roller_progression_selfcheck_next
        player_tween_selfcheck player_tween_selfcheck_next
        player_sfx_selfcheck player_sfx_selfcheck_next
        player_gamesupport_selfcheck player_gamesupport_selfcheck_next
        player_gameplay_selfcheck player_gameplay_selfcheck_next
        player_hotreload_selfcheck player_hotreload_selfcheck_next
        player_scriptprop_selfcheck player_scriptprop_selfcheck_next
        player_assetid_selfcheck player_assetid_selfcheck_next
        editor_play_stop editor_play_crash editor_play_mirror editor_project_play
        editor_play_script_error editor_play_hotreload editor_ui_hotreload
        editor_anim_hotreload
        editor_play_mirror_next
        editor_play_mirror_spawn editor_play_mirror_spawn_next
        editor_play_mirror_switch editor_play_mirror_switch_next
        editor_play_stop_next editor_play_crash_next editor_project_play_next
        editor_play_script_error_next editor_play_hotreload_next
        editor_ui_hotreload_next editor_anim_hotreload_next
        editor_project_native_play editor_project_native_play_break
        editor_project_native_play_next editor_project_native_play_break_next
        editor_play_simulator editor_play_simulator_boot
        editor_play_android editor_play_ios_device
        player_resize_selfcheck player_resize_selfcheck_next
        player_rotation_android player_rotation_ios
        export_macos_lua export_macos_native export_macos_native_next
        export_ios_simulator export_android export_android_aab
        export_android_assemble
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
foreach(play_test player_debug_pause player_script_debug
        player_script_debug_next editor_play_stop editor_play_crash
        editor_play_mirror editor_play_mirror_next
        editor_play_mirror_spawn editor_play_mirror_spawn_next
        editor_play_mirror_switch editor_play_mirror_switch_next
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
        export_macos_lua export_ios_simulator export_android
        export_android_assemble)
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
        editor_project_native_play_next
        editor_project_native_play_break_next
        # exports jumper-native: the exporter's module build (its separate
        # per-flavor native/build-export-<flavor> tree persists, so re-runs are
        # incremental) compiles the same OGRE-including module sources - same
        # headroom, same serialization against the compile-on-Play tests
        export_macos_native
        export_macos_native_next)
    if(TEST ${native_test})
        # above the editor's own 600s build deadline, so a slow (sanitizer-
        # instrumented) build fails through the editor's honest path instead
        # of a ctest kill
        set_tests_properties(${native_test} PROPERTIES TIMEOUT 900
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
    # connect (90s) + the playtest phase slack - see EditorApp.h budgets. The
    # slack scales with the runner: a loaded hosted machine has measured 3-4x
    # slower end to end than the same commit's earlier run (453s green vs a
    # 1400s cap blown), so the cap sits above the worst measured pace while
    # the inner phase budgets still fail honestly first on a true hang
    set_tests_properties(editor_play_simulator_boot PROPERTIES TIMEOUT 2100)
endif()

# device tests boot iOS simulators / Android emulators - by far the slowest
# part of a full suite run (up to minutes when a device cold-boots). They
# carry the extra "device" label so the default development preset
# (ctest --preset desktop) can exclude them; run them via ctest --preset all
# when deploy/device code changed or before handing work over.
foreach(device_test editor_play_simulator editor_play_simulator_boot
        editor_play_android editor_play_ios_device player_safearea_device
        player_rotation_android player_rotation_ios
        http_device_android http_device_ios
        export_ios_simulator_run export_ios_simulator_payload_run
        export_ios_simulator_native_run
        export_ios_simulator_tests
        export_android_run export_android_payload_run)
    if(TEST ${device_test})
        # RESOURCE_LOCK: device tests share one simulator/emulator + adb -
        # they must not run concurrently when the suite runs with -j
        set_tests_properties(${device_test} PROPERTIES
            LABELS "integration;device" RESOURCE_LOCK mobile_device)
    endif()
endforeach()

# the mobile HTTP runs package a whole app, install it and let it drive a
# dozen network legs (one of them a deliberate timeout) - and the iOS one may
# have to boot a simulator first. These are the LAST property writes for them,
# so they are the authoritative timeouts; the driver's own --deadline fails
# honestly first on a wedged app.
if(TEST http_device_android)
    set_tests_properties(http_device_android PROPERTIES TIMEOUT 900)
endif()
if(TEST http_device_ios)
    set_tests_properties(http_device_ios PROPERTIES TIMEOUT 1500)
endif()

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

# Tests are SILENT on a developer's machine, and that is a property of the
# RUN, never of a test: an automated run opens the SILENT audio device
# (AppHost::initialise -> SoundManager::setAutomatedRun), and every app a test
# starts - editor, player, sample, native game module - boots through it. No
# test registration here asks for silence, and none may: a per-test opt-in is
# how a new test ends up audible.
#
# This ONE directory-wide line is the outer belt, for the corner the app-level
# rule cannot see. A few player selfchecks are deliberately NOT classified as
# automated runs (that classification also pins the frame delta to a fixed
# tick, which would change what they measure), so the suite states its own
# audio choice once, for everything it starts. It is an explicit setting, so
# it wins over the automated-run default the same way a developer's own
# ORKIGE_AUDIO_BACKEND=auto does. ENVIRONMENT_MODIFICATION layers over any
# per-test ENVIRONMENT.
get_property(_orkige_registered_tests DIRECTORY PROPERTY TESTS)
foreach(_orkige_test IN LISTS _orkige_registered_tests)
    set_property(TEST "${_orkige_test}" APPEND PROPERTY
        ENVIRONMENT_MODIFICATION "ORKIGE_AUDIO_BACKEND=set:null")
endforeach()
unset(_orkige_registered_tests)
unset(_orkige_test)

# A windowed binary answers 77 when this PROCESS reaches no display - on macOS
# that is a background login session, which owns no window server (fast user
# switching). Every windowed test can hit it, so ctest has to read 77 as
# SKIPPED across the board rather than on the 49 tests that happened to declare
# it. Applied to everything EXCEPT the `unit` label: those executables are
# Catch2, whose exit status is its failed-assertion count, so a blanket rule
# would read 77 failures as a skip - the one case this must not cover.
# Everything else runs a real binary and can meet a display-less session.
#
# The rule is NOT-unit rather than is-integration on purpose: a test registered
# without any label would otherwise slip through silently, which is exactly what
# had happened to 24 windowed demos and player selfchecks. Those now carry the
# label they always should have had, and the guard below keeps it that way.
#
# Without this the whole classic suite reports failures on a machine whose
# screen belongs to another user, and the real failures drown in them.
get_property(_orkige_registered_tests DIRECTORY PROPERTY TESTS)
foreach(_orkige_test IN LISTS _orkige_registered_tests)
    get_test_property("${_orkige_test}" LABELS _orkige_test_labels)
    if(NOT _orkige_test_labels MATCHES "unit")
        set_property(TEST "${_orkige_test}" PROPERTY SKIP_RETURN_CODE 77)
    endif()
endforeach()
unset(_orkige_registered_tests)
unset(_orkige_test)
unset(_orkige_test_labels)

# Every registered test carries a label. It is how the presets select their sets
# (`unit`, `integration`, `device`), so an unlabelled test runs in no preset that
# names a label and is invisible to `ctest -L`. That happened silently to 24
# tests; this refuses the next one at configure time rather than letting it hide.
get_property(_orkige_registered_tests DIRECTORY PROPERTY TESTS)
set(_orkige_unlabelled "")
foreach(_orkige_test IN LISTS _orkige_registered_tests)
    get_test_property("${_orkige_test}" LABELS _orkige_test_labels)
    if(NOT _orkige_test_labels)
        list(APPEND _orkige_unlabelled "${_orkige_test}")
    endif()
endforeach()
if(_orkige_unlabelled)
    message(FATAL_ERROR
        "these tests carry no LABELS, so no preset that selects by label will "
        "run them: ${_orkige_unlabelled}. Give each one 'unit', 'integration' "
        "or 'device'.")
endif()
unset(_orkige_registered_tests)
unset(_orkige_test)
unset(_orkige_test_labels)
unset(_orkige_unlabelled)

# Every integration test declares a TIMEOUT. Without one ctest falls back to a
# 25-minute default, so a windowed test that wedges holds a runner for that long
# and, if the run is interrupted, can outlive it entirely - a stray demo was
# found burning a core for over two hours. 24 tests had neither a label nor a
# timeout; both gaps are closed, and this refuses the next one at configure time
# rather than waiting for someone to notice a slow suite.
get_property(_orkige_registered_tests DIRECTORY PROPERTY TESTS)
set(_orkige_untimed "")
foreach(_orkige_test IN LISTS _orkige_registered_tests)
    get_test_property("${_orkige_test}" LABELS _orkige_test_labels)
    get_test_property("${_orkige_test}" TIMEOUT _orkige_test_timeout)
    if(_orkige_test_labels MATCHES "integration" AND NOT _orkige_test_timeout)
        list(APPEND _orkige_untimed "${_orkige_test}")
    endif()
endforeach()
if(_orkige_untimed)
    message(FATAL_ERROR
        "these integration tests declare no TIMEOUT, so a wedged run holds a "
        "runner for ctest's 25-minute default: ${_orkige_untimed}. Give each "
        "one a TIMEOUT it should comfortably fit inside.")
endif()
unset(_orkige_registered_tests)
unset(_orkige_test)
unset(_orkige_test_labels)
unset(_orkige_test_timeout)
unset(_orkige_untimed)

# EACH BUILD TREE GETS ITS OWN TEMP DIRECTORY. Test binaries scatter scratch
# files through std::filesystem::temp_directory_path(), and most name them
# after what they test rather than after the process - which is fine until the
# SAME binary runs from a second checkout (an agent worktree, a second CI job
# on one runner). Then two runs share one path and the first cleanup deletes
# what the second is still writing; it surfaced as an exporter icon case
# failing in the suite while passing alone.
#
# Keyed on a short digest of this binary directory, so it is unique per tree
# and still short: macOS caps a unix socket path near 104 characters, and a
# temp root under the build tree would spend most of that budget before the
# socket is even named.
string(MD5 _orkige_tree_digest "${CMAKE_CURRENT_BINARY_DIR}")
string(SUBSTRING "${_orkige_tree_digest}" 0 8 _orkige_tree_digest)
set(ORKIGE_TEST_TMPDIR "/tmp/orkige-tests-${_orkige_tree_digest}")
if(WIN32)
    set(ORKIGE_TEST_TMPDIR "${CMAKE_CURRENT_BINARY_DIR}/tmp")
endif()
file(MAKE_DIRECTORY "${ORKIGE_TEST_TMPDIR}")
get_property(_orkige_registered_tests DIRECTORY PROPERTY TESTS)
foreach(_orkige_test IN LISTS _orkige_registered_tests)
    set_property(TEST "${_orkige_test}" APPEND PROPERTY
        ENVIRONMENT_MODIFICATION
            "TMPDIR=set:${ORKIGE_TEST_TMPDIR}"
            "TMP=set:${ORKIGE_TEST_TMPDIR}"
            "TEMP=set:${ORKIGE_TEST_TMPDIR}")
endforeach()
unset(_orkige_registered_tests)
unset(_orkige_test)
unset(_orkige_tree_digest)
