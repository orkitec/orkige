# EditorCore unit tests - the UI-independent editor logic (undo/redo command
# stack, naming/rename validation, snapshot-based object commands, fly-camera
# math, mesh-import path handling, file-dialog result marshaling), fully
# headless with the core-only test components. The engine-component paths
# (TransformComponent apply, gizmo drags, the full import flow) need live
# scene nodes and run in the editor_edittest integration test instead.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_editor_core_tests
    TestRunListener.cpp
    EditorCoreTests.cpp
    EditorScriptToolsTests.cpp
    EditorLabelFormatTests.cpp
    EditorEulerTests.cpp
    PlayMirrorTests.cpp
	EditorCameraTests.cpp
	EditorCameraGizmoTests.cpp
	EditorOverlayGeometryTests.cpp
	EditorPanelRegistryTests.cpp
	ExternalEditorTests.cpp
	SyntaxHighlightTests.cpp
	ScriptBreakpointStoreTests.cpp
	ScriptCompletionTests.cpp
	EditorTabActionsTests.cpp
	EditorIdeProtocolTests.cpp
	EditorMcpConfigTests.cpp
	EditorLineDiffTests.cpp
	FileFormatIconTests.cpp
	EditorGitTests.cpp
	EditorViewModesTests.cpp
	EditorResourcePathsTests.cpp
	EditorExportPlanTests.cpp
	EditorBuildSettingsTests.cpp
	EditorCliTests.cpp
	EditorSecretStoreTests.cpp
	EditorUiEditTests.cpp
	EditorTreeDndTests.cpp
	EditorUpdateTests.cpp
	EditorPayloadsTests.cpp
	EditorTerminalKeysTests.cpp
	EditorTerminalScreenTests.cpp
	EditorTerminalSessionTests.cpp
    FileDialogTests.cpp
    MeshImportTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/TestComponents.cpp
)

target_include_directories(orkige_editor_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(orkige_editor_core_tests PRIVATE
    orkige_editor_core
    Catch2::Catch2WithMain
)

# EditorCore itself stays free of the exporter (the fields of an export request
# are MIRRORED, never included - see EditorExportPlan.h). The credential model
# mirrors its environment-variable names the same way, so the ONE place that
# can see both spellings at once is this test executable: it links the exporter
# purely to assert that the two never drift apart.
if(TARGET orkige_exporter)
    target_link_libraries(orkige_editor_core_tests PRIVATE orkige_exporter)
    target_compile_definitions(orkige_editor_core_tests PRIVATE
        ORKIGE_HAVE_EXPORTER=1)
endif()

# the shipped sample .oui files the round-trip test reads from the source tree
target_compile_definitions(orkige_editor_core_tests PRIVATE
    ORKIGE_UI_SAMPLE_SETTINGS="${Orkige_SOURCE_DIR}/samples/hello_orkige/media/settings_screen.oui"
    ORKIGE_UI_SAMPLE_MATRIX="${Orkige_SOURCE_DIR}/samples/hello_orkige/media/matrix_screen.oui"
    ORKIGE_UI_SAMPLE_BENCH_HUD="${Orkige_SOURCE_DIR}/projects/benchmark/assets/hud.oui"
    ORKIGE_UI_SAMPLE_BENCH_SETTINGS="${Orkige_SOURCE_DIR}/projects/benchmark/assets/settings.oui"
)

include(Catch)
catch_discover_tests(orkige_editor_core_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
