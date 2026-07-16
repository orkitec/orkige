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
