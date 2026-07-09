# orkige_editor - the in-engine editor shell (Phase 4 pulled forward).
# An Orkige app: SDL3 owns the window/event loop, Orkige::Engine renders into
# it (externalWindowHandle), the UI is Dear ImGui drawn through the
# engine_render facade (ImGuiFacadeRenderer on DrawLayer2D - no imgui SDL
# backend either, see ImGuiSDL3Input.cpp). Builds on BOTH render flavors.

# the editor calls ImGui:: directly - it needs its own find_package for the
# imgui include directories (imgui is a base manifest dependency)
find_package(imgui CONFIG REQUIRED)
# transform gizmos in the Scene panel (built against the same docking imgui)
find_package(imguizmo CONFIG REQUIRED)

# EditorCore: the UI-independent editor logic (selection, undo/redo command
# stack, object operations). Split into its own library so the headless unit
# tests (tests/editor_core) exercise exactly what the editor links.
add_library(orkige_editor_core STATIC
    EditorCore.h
    EditorCore.cpp
    EditorCamera.h
    EditorCamera.cpp
    FileDialog.h
    FileDialog.cpp
    MeshImport.h
    MeshImport.cpp
)
target_include_directories(orkige_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orkige_editor_core PUBLIC Orkige::Engine)

# PCH (no imgui here - EditorCore stays UI-independent and does not link it)
target_precompile_headers(orkige_editor_core PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:<Ogre.h$<ANGLE-R>>"
)
orkige_pch_ccache_compat(orkige_editor_core)

# The editor shell: main.cpp (boot + frame loop) plus one TU per concern
# (EditorApp.h is the shared-state/seam header) so a single-panel edit
# recompiles one small TU instead of the former 8600-line monolith.
add_executable(orkige_editor
    main.cpp
    EditorApp.h
    EditorConsole.cpp
    EditorDeviceTargets.cpp
    EditorDocument.cpp
    EditorExport.cpp
    EditorFileDialogs.cpp
    EditorHierarchyPanel.cpp
    EditorInspectorPanel.cpp
    EditorMenus.cpp
    EditorPlaySession.cpp
    EditorScenePanel.cpp
    EditorSettings.cpp
    EditorShortcuts.cpp
    EditorStatsPanel.cpp
    EditorToolbar.cpp
    ImGuiFacadeRenderer.h
    ImGuiFacadeRenderer.cpp
    ImGuiSDL3Input.h
    ImGuiSDL3Input.cpp
    EditorTheme.h
    EditorTheme.cpp
)

target_link_libraries(orkige_editor PRIVATE
    orkige_editor_core
    imgui::imgui
    imguizmo::imguizmo
)

# PCH: every editor TU pulls in OGRE (via the engine) and ImGui. CXX-only
# guard: MacMenu.mm below is ObjC++.
target_precompile_headers(orkige_editor PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:<Ogre.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<imgui.h$<ANGLE-R>>"
)
orkige_pch_ccache_compat(orkige_editor)

if(APPLE)
    # native macOS menu bar (NSMenu mirror of the editor menus; ObjC++)
    target_sources(orkige_editor PRIVATE MacMenu.h MacMenu.mm)
    target_link_libraries(orkige_editor PRIVATE "-framework AppKit")

    # a proper .app bundle (Orkige.app): Dock/Cmd-Tab icon + name instead of
    # a bare Ninja executable. The icon is generated procedurally
    # (Util/make_editor_icon.py -> iconset -> iconutil, build-dir artifact),
    # same stdlib-only-Python policy as the fastgui atlas generator - and the
    # same find_program rationale (FindPython3 ignores CMAKE_IGNORE_PREFIX_PATH
    # and would pick the banned /usr/local interpreter).
    # $<TARGET_FILE:orkige_editor> resolves into the bundle, so ctest
    # registrations and the settings inis (SDL_GetBasePath = the bundle's
    # Resources dir) follow along untouched.
    find_program(ORKIGE_PYTHON3 NAMES python3 HINTS /usr/bin REQUIRED)
    set(_editor_icon_script "${Orkige_SOURCE_DIR}/Util/make_editor_icon.py")
    set(_editor_iconset "${CMAKE_CURRENT_BINARY_DIR}/orkige_editor.iconset")
    set(_editor_icns "${CMAKE_CURRENT_BINARY_DIR}/orkige_editor.icns")
    add_custom_command(
        OUTPUT "${_editor_icns}"
        COMMAND "${ORKIGE_PYTHON3}" "${_editor_icon_script}" "${_editor_iconset}"
        COMMAND iconutil -c icns "${_editor_iconset}" -o "${_editor_icns}"
        DEPENDS "${_editor_icon_script}"
        COMMENT "Generating the editor app icon (make_editor_icon.py + iconutil)"
        VERBATIM
    )
    target_sources(orkige_editor PRIVATE "${_editor_icns}")
    set_source_files_properties("${_editor_icns}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(orkige_editor PROPERTIES
        OUTPUT_NAME "Orkige"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Orkige"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.orkitec.orkige-editor"
        MACOSX_BUNDLE_ICON_FILE "orkige_editor.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Cross-flavor play targets (the toolbar's "Desktop (classic OGRE)" /
# "Desktop (Ogre-Next)" entries): this flavor's own player is the TARGET_FILE;
# the OTHER flavor's binary sits in its conventional preset build tree and the
# entry greys out at runtime while that path does not exist. The conventional
# tree names follow the host's presets (macos-* / linux-*, see
# CMakePresets.json).
if(APPLE)
    set(_orkige_sibling_tree_next "build/macos-debug")
    set(_orkige_sibling_tree_classic "build/macos-debug-classic")
else()
    set(_orkige_sibling_tree_next "build/linux-debug-next")
    set(_orkige_sibling_tree_classic "build/linux-debug-classic")
endif()
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    set(ORKIGE_EDITOR_PLAYER_CLASSIC
        "${CMAKE_SOURCE_DIR}/${_orkige_sibling_tree_classic}/tools/player/orkige_player")
    set(ORKIGE_EDITOR_PLAYER_NEXT "$<TARGET_FILE:orkige_player>")
else()
    set(ORKIGE_EDITOR_PLAYER_CLASSIC "$<TARGET_FILE:orkige_player>")
    set(ORKIGE_EDITOR_PLAYER_NEXT
        "${CMAKE_SOURCE_DIR}/${_orkige_sibling_tree_next}/tools/player/orkige_player")
endif()

# Editor-only: point at the RTSS shader library shipping with the
# vcpkg-installed OGRE so the editor runs straight out of the build tree
# (same approach as samples/hello_orkige). Only the classic boot block reads
# it (the next flavor's Hlms media is EngineNext's built-in default).
target_compile_definitions(orkige_editor PRIVATE
    ORKIGE_EDITOR_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media"
    ORKIGE_EDITOR_PLAYER_PATH_CLASSIC="${ORKIGE_EDITOR_PLAYER_CLASSIC}"
    ORKIGE_EDITOR_PLAYER_PATH_NEXT="${ORKIGE_EDITOR_PLAYER_NEXT}"
    # sample assets (test_mesh.glb, generated by Util/make_test_mesh.py)
    ORKIGE_EDITOR_ASSET_DIR="${CMAKE_SOURCE_DIR}/samples/hello_orkige/media"
    # jumper sample assets (textured .glb meshes, Util/make_jumper_assets.py)
    ORKIGE_EDITOR_JUMPER_ASSET_DIR="${CMAKE_SOURCE_DIR}/samples/jumper/media"
    # default directory the Scene Path modal offers for .oscene files
    ORKIGE_EDITOR_SCENE_DIR="${CMAKE_SOURCE_DIR}/samples/scenes"
    ORKIGE_EDITOR_VERSION="${PROJECT_VERSION}"
    # play mode spawns this binary (Play toolbar button / playtest hooks)
    ORKIGE_EDITOR_PLAYER_PATH="$<TARGET_FILE:orkige_player>"
    # the iOS-simulator player app the editor auto-installs onto a simulator
    # that does not have it yet (produced by the ios-simulator-debug preset;
    # Play surfaces an honest error when neither installed nor built)
    ORKIGE_EDITOR_IOS_PLAYER_APP="${CMAKE_SOURCE_DIR}/build/ios-simulator-debug/tools/player/OrkigePlayer.app"
    # native project modules (compile-on-Play): the module build links
    # against THIS build tree with the SAME toolchain settings - see
    # cmake/OrkigeGameModule.cmake and startPlay's native branch
    ORKIGE_EDITOR_ENGINE_ROOT="${CMAKE_SOURCE_DIR}"
    ORKIGE_EDITOR_ENGINE_BUILD_DIR="${CMAKE_BINARY_DIR}"
    ORKIGE_EDITOR_CMAKE="${CMAKE_COMMAND}"
    ORKIGE_EDITOR_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    ORKIGE_EDITOR_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    ORKIGE_EDITOR_SCRIPTING="${ORKIGE_SCRIPTING}"
    ORKIGE_EDITOR_OSX_SYSROOT="${CMAKE_OSX_SYSROOT}"
)

# play mode needs the player binary present at runtime
add_dependencies(orkige_editor orkige_player)
