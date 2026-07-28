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
    PythonToolchainTests.cpp
	EditorCameraTests.cpp
	EditorCameraGizmoTests.cpp
	EditorOverlayGeometryTests.cpp
	EditorPanelRegistryTests.cpp
	ExternalEditorTests.cpp
	SyntaxHighlightTests.cpp
	ScriptBreakpointStoreTests.cpp
	ScriptCompletionTests.cpp
	EditorTabActionsTests.cpp
	EditorLineDiffTests.cpp
	FileFormatIconTests.cpp
	EditorGitTests.cpp
	EditorViewModesTests.cpp
	EditorUiEditTests.cpp
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
