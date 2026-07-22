# Web (wasm) integration tests - registered in the web build tree only (the
# artifacts under test are this tree's own player + export output; the
# desktop trees carry the desktop suites). run_export_web.py is stdlib-only,
# like every Util/ tool.
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
#                         shutdown. SKIPs (77) without a headless browser.
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
