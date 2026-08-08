# Web (wasm) integration tests - registered in the web build tree only (the
# artifacts under test are this tree's own player + export output; the
# desktop trees carry the desktop suites). run_export_web.py is stdlib-only,
# like every Util/ tool.
#
# The exporter that packages these is a HOST tool, which a cross-compiled web
# tree cannot build - so the driver resolves it itself: ORKIGE_EXPORT_BINARY,
# else an `orkige_export` in any configured host build tree. Without one it
# refuses and names the preset to build.
#
# Every browser leg below reports a run that rendered NOTHING under the right
# name: a browser that hands the page no WebGL context is asked about
# explicitly and SKIPs (77) as "the browser provided no GPU context", so a
# GPU-less host never comes back as a verdict on the feature under test. The
# CI web job refuses a skipped web test, which is what keeps that skip a loud
# failure exactly where a GPU was expected.
#
#   export_web_structure  export the reference Lua project for the web and
#                         assert the artifact set (shell page, wasm player
#                         pair, packed payload image + loader, icon)
#   export_web_boot       the same export BOOTED headlessly: a Chrome/
#                         Chromium session must report the bundled-project
#                         boot, reach the orderly shutdown after a frame-
#                         capped run, and render an actual scene in a mid-run
#                         screenshot (pixel-level check). SKIPs (77) on a
#                         machine without a headless browser.
#   export_web_roller     the 2D-tier gameplay bar IN the browser: the
#                         exported roller runs its full player selfcheck
#                         (tilt roll via the key simulation, move mode, tile
#                         slide, win path) headlessly - wasm physics, input
#                         and rendering held to the desktop ctest's standard.
#                         SKIPs (77) without a headless browser.

add_test(NAME export_web_structure
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_export_web.py"
        --engine-build "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/export_web_structure"
        --mode structure
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(export_web_structure PROPERTIES
    LABELS integration TIMEOUT 180)

add_test(NAME export_web_boot
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_export_web.py"
        --engine-build "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/export_web_boot"
        --mode boot
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(export_web_boot PROPERTIES
    LABELS integration TIMEOUT 600
    SKIP_RETURN_CODE 77)

add_test(NAME export_web_roller
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_export_web.py"
        --engine-build "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/export_web_roller"
        --mode roller
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(export_web_roller PROPERTIES
    LABELS integration TIMEOUT 600
    SKIP_RETURN_CODE 77)

#   export_web_water      the ADVANCED water bar IN the browser: a generated
#                         single-scene refractive-water project booted headless
#                         must report screenSpaceRefraction supported on the
#                         WebGL2/GLES3 context (the ES-300 grab-pass water
#                         variant), build its programs with no fallback refusal
#                         and render the surface through to the orderly
#                         shutdown. SKIPs (77) without a headless browser, and
#                         under its own name where the browser gave the page no
#                         GPU context (a run that never reached WebGL has no
#                         verdict on an ES-300 gate).
add_test(NAME export_web_water
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_export_web.py"
        --engine-build "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/export_web_water"
        --mode water
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(export_web_water PROPERTIES
    LABELS integration TIMEOUT 600
    SKIP_RETURN_CODE 77)

#   export_web_bloom      the LDR bloom bar IN the browser: a generated
#                         single-scene emissive-cube project booted headless
#                         must report bloom supported on the WebGL2/GLES3
#                         context (the ES-300 quad-pass compositor), build its
#                         chain with no fallback refusal, render through to the
#                         orderly shutdown, AND prove the glow at the pixel
#                         level - the same static scene is measurably brighter
#                         with bloom on than off (the additive halo). SKIPs
#                         (77) without a headless browser.
add_test(NAME export_web_bloom
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_export_web.py"
        --engine-build "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/export_web_bloom"
        --mode bloom
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
set_tests_properties(export_web_bloom PROPERTIES
    LABELS integration TIMEOUT 600
    SKIP_RETURN_CODE 77)

#   web_parity_capture_<scene>  the BROWSER half of the browser-render-parity
#                         gate (run_web_parity_test.py): export the benchmark
#                         vignette as its own main scene, boot it headless at
#                         the pinned 1280x720 capture rectangle with the
#                         parity cvar set, and write the frame. The capture
#                         itself is asserted - right size, an actual scene,
#                         never the black canvas a dead page composites - and
#                         the frame is what the CI web-parity job compares
#                         against the linux-classic job's own capture of the
#                         same scene. SKIPs (77) without a headless browser,
#                         and where the browser gave the page no GPU context
#                         (there is no render to picture, and nothing to say
#                         about the WebGL tier); the web job refuses a skipped
#                         web test, so either skip is a failure exactly where
#                         the artifact is needed.
#                         The comparison road (--compare-shots) and the
#                         both-sides developer road (--engine-build +
#                         --player-classic) live in the same driver.
foreach(ORKIGE_WEB_PARITY_SCENE lake mirrorlake)
    add_test(NAME web_parity_capture_${ORKIGE_WEB_PARITY_SCENE}
        COMMAND python3
            "${Orkige_SOURCE_DIR}/tests/integration_driver/run_web_parity_test.py"
            --capture web
            --repo "${Orkige_SOURCE_DIR}"
            --engine-build "${CMAKE_BINARY_DIR}"
            --scene "scenes/${ORKIGE_WEB_PARITY_SCENE}.oscene"
            --dir "${CMAKE_CURRENT_BINARY_DIR}/web_parity/${ORKIGE_WEB_PARITY_SCENE}"
        WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
    # the wall covers export + a software-rasterized WebGL boot of a full 3D
    # vignette on a hosted runner, with slack (the embed test measures ~180s
    # for the first scene there)
    set_tests_properties(web_parity_capture_${ORKIGE_WEB_PARITY_SCENE} PROPERTIES
        LABELS integration TIMEOUT 900
        SKIP_RETURN_CODE 77)
endforeach()

#   export_web_embed_click  the benchmark export must render AND be clickable
#                           inside a small iframe-sized viewport (the way the
#                           public site embeds it), not just in a full tab. A
#                           headless Chrome driven over the DevTools Protocol
#                           at the exact embed viewport asserts the HUD title
#                           renders (not cropped), then drives a REAL mouse
#                           click on the results-card Restart button and
#                           asserts the tour restarts - guarding both the
#                           responsive-canvas crop fix and the results screen's
#                           input-events wiring (the autoRestart seam does not
#                           exercise the click path). SKIPs (77) without a
#                           headless browser, and under its own name where the
#                           browser gave the page no GPU context.
add_test(NAME export_web_embed_click
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/run_embed_click.py"
        --engine-build "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/export_web_embed_click"
    WORKING_DIRECTORY "${Orkige_SOURCE_DIR}")
# the wall covers the driver's own worst-case waits (180s vista + 600s tour
# on CI's software-rasterized WebGL + export/boot/replay overhead) with slack
set_tests_properties(export_web_embed_click PROPERTIES
    LABELS integration TIMEOUT 1100
    SKIP_RETURN_CODE 77)
