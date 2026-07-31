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
# the Script panel's code editor widget (built against the same docking imgui)
find_package(imgui-color-text-edit CONFIG REQUIRED)
# the embedded terminal's VT screen model (libvterm, confined to
# EditorTerminalScreen.cpp - see the overlay port ports/libvterm)
find_package(libvterm CONFIG REQUIRED)

# EditorCore: the UI-independent editor logic (selection, undo/redo command
# stack, object operations). Split into its own library so the headless unit
# tests (tests/editor_core) exercise exactly what the editor links.
add_library(orkige_editor_core STATIC
    EditorCore.h
    EditorCore.cpp
    EditorAutosave.h
    EditorAutosave.cpp
    EditorCamera.h
    EditorCamera.cpp
    FileDialog.h
    FileDialog.cpp
    MeshImport.h
    MeshImport.cpp
    EditorScriptTools.h
    EditorScriptTools.cpp
    PlayMirror.h
    PlayMirror.cpp
    EditorLabelFormat.h
    EditorLabelFormat.cpp
    EditorEuler.h
    EditorEuler.cpp
    ExternalEditor.h
    ExternalEditor.cpp
    SyntaxHighlight.h
    SyntaxHighlight.cpp
    PythonToolchain.h
    PythonToolchain.cpp
    ScriptBreakpoints.h
    ScriptBreakpoints.cpp
    ScriptCompletion.h
    ScriptCompletion.cpp
    EditorTabActions.h
    EditorTabActions.cpp
    # the ONE bundle-first / tree-second resource resolver (pure decision
    # table over an injectable existence probe - EditorResourcePathsTests);
    # the baked developer-tree fallbacks are bound in the editor executable
    # (EditorResourceBinding.cpp below)
    EditorResourcePaths.h
    EditorResourcePaths.cpp
    # the ONE export decision (which exporter runs, what it packages from, and
    # the actionable sentence when this editor cannot produce a platform) -
    # pure over resolved paths, unit-tested headlessly (EditorExportPlanTests)
    EditorExportPlan.h
    EditorExportPlan.cpp
    EditorTextDiagnostics.h
    EditorTextDiagnostics.cpp
    # the PURE half of the Claude-IDE integration (lock model + MCP message
    # builders); unit-tested headlessly (EditorIdeProtocolTests)
    EditorIdeProtocol.h
    EditorIdeProtocol.cpp
    # the PURE merge-or-skip logic for the project-scope .mcp.json discovery
    # file; unit-tested headlessly (EditorMcpConfigTests)
    EditorMcpConfig.h
    EditorMcpConfig.cpp
    FileFormatIcon.h
    FileFormatIcon.cpp
    EditorLineDiff.h
    EditorLineDiff.cpp
    EditorGit.h
    EditorGit.cpp
    EditorViewModes.h
    EditorViewModes.cpp
    EditorUiEdit.h
    EditorUiEdit.cpp
    # the pure tree drag-drop drop-zone classifier shared by the widget tree and
    # the scene Hierarchy (Into vs Before/After bands) - unit-tested headlessly
    EditorTreeDnd.h
    EditorTreeDnd.cpp
    # the updater: every DECISION as pure functions (EditorUpdateTests), and
    # the orchestration around them with its subprocess seams injected so the
    # whole sequence drives headlessly against a loopback server
    EditorUpdate.h
    EditorUpdate.cpp
    EditorUpdater.h
    EditorUpdater.cpp
    # the embedded terminal's VT screen model + pure key encoder (UI-free and
    # unit-tested headlessly - EditorTerminalScreenTests / EditorTerminalKeysTests)
    EditorTerminalScreen.h
    EditorTerminalScreen.cpp
    EditorTerminalKeys.h
    EditorTerminalKeys.cpp
    # the pure multi-session bookkeeping (title cleaning, agent glyph classifier,
    # tab-label composition, post-close active index) - unit-tested headlessly
    # (EditorTerminalSessionTests)
    EditorTerminalSession.h
    EditorTerminalSession.cpp
)
target_include_directories(orkige_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(tinyxml2 CONFIG REQUIRED)	# the live XML document diagnostics
target_link_libraries(orkige_editor_core PUBLIC Orkige::Engine)
target_link_libraries(orkige_editor_core PRIVATE tinyxml2::tinyxml2)
# libvterm is an implementation detail of EditorTerminalScreen.cpp; the header
# exposes only the editor's own cell/grid vocabulary, so it links PRIVATE
target_link_libraries(orkige_editor_core PRIVATE libvterm::vterm)

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
	EditorPanelRegistry.h
    # the build identity (--version / the About box): its own TU so the
    # per-build commit stamp recompiles one small file, not the whole editor
    EditorBuildInfo.h
    EditorBuildInfo.cpp
    AnimationPreviewStage.h
    AnimationPreviewStage.cpp
    AnimationPreviewPanel.cpp
    EditorAssetDnd.h
    EditorAssetBrowserPanel.cpp
    EditorConsole.cpp
    EditorControlServer.h
    EditorControlServer.cpp
    # the Claude-IDE integration server (lock lifecycle + MCP-over-WebSocket)
    EditorIdeServer.h
    EditorIdeServer.cpp
    # the project-scope .mcp.json discovery-file reconciler (file IO over the
    # pure EditorMcpConfig merge logic)
    EditorMcpConfigFile.h
    EditorMcpConfigFile.cpp
    EditorDeviceTargets.cpp
    EditorDocument.cpp
    EditorBrowserServe.cpp
    EditorExport.cpp
    EditorFileDialogs.cpp
    EditorHierarchyPanel.cpp
    EditorImageDecode.h
    EditorImageDecode.cpp
    EditorInspectorPanel.cpp
    EditorMenus.cpp
    GuiPreviewStage.h
    GuiPreviewStage.cpp
    GamePreviewStage.h
    GamePreviewStage.cpp
    OwnerComponentBadges.cpp
    MeshPreviewStage.h
    MeshPreviewStage.cpp
    SfxAuditionStage.h
    SfxAuditionStage.cpp
    EditorGamePreviewPanel.cpp
    EditorUiEditorPanel.h
    EditorUiEditorPanel.cpp
    EditorPlaySession.cpp
    EditorPropertyWidgets.h
    EditorPropertyWidgets.cpp
    # binds the pure resource locator to the paths CMake bakes below - the ONE
    # place a developer-tree path enters resource resolution
    EditorResourceBinding.cpp
    EditorScriptHost.h
    EditorScriptHost.cpp
    EditorScriptPanel.cpp
    EditorSourceControlPanel.h
    EditorSourceControlPanel.cpp
    # the embedded terminal: the OS pty backend seam (POSIX openpty / Windows
    # ConPTY) and the dockable ImGui panel + its headless selfcheck
    EditorTerminalPty.h
    EditorTerminalPty.cpp
    EditorTerminalPanel.h
    EditorTerminalPanel.cpp
    ExternalEditorLaunch.cpp
    EditorScenePanel.cpp
    EditorSettings.cpp
    EditorShortcuts.cpp
    EditorStatsPanel.cpp
    EditorTilePalettePanel.cpp
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
    imgui-color-text-edit::imgui-color-text-edit
)

# the embedded terminal's POSIX pty backend calls openpty(); on Linux that
# lives in libutil (macOS provides it in libSystem, Windows uses ConPTY)
if(UNIX AND NOT APPLE)
    target_link_libraries(orkige_editor PRIVATE util)
endif()

# the pty backend must set _WIN32_WINNT / NTDDI_VERSION before <windows.h> to
# unlock the ConPTY APIs, so it opts out of the PCH (which force-includes the
# Windows headers first at a lower version) - see EditorTerminalPty.cpp
set_source_files_properties(EditorTerminalPty.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON)

# PCH: every editor TU pulls in OGRE (via the engine) and ImGui. CXX-only
# guard: MacMenu.mm below is ObjC++.
target_precompile_headers(orkige_editor PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:<Ogre.h$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<imgui.h$<ANGLE-R>>"
)
orkige_pch_ccache_compat(orkige_editor)

# the single-file stb_image decoder is compiled in exactly one TU (the asset
# browser bakes a GPU-rendered preview to PNG then reads it back to RGBA for a
# thumbnail upload): only it sees the stb include, and it stays off the PCH.
find_package(Stb REQUIRED)
set_source_files_properties(EditorImageDecode.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${Stb_INCLUDE_DIR}")

# The build IDENTITY stamp (EditorBuildInfo.cpp): which source commit this
# binary was built from and when. Supplied at configure time by whoever
# packages a distributable build:
#     cmake --preset macos-release -DORKIGE_BUILD_COMMIT=a16c0227a \
#                                  -DORKIGE_BUILD_DATE=2026-07-30
# Unset in an ordinary developer build - the editor then reports "local build"
# instead of claiming a commit (@see EditorBuildInfo.h, Docs/nightly-builds.md).
# Scoped to this ONE source file so a re-stamp never invalidates the editor's
# whole object cache, and OFF the PCH because the extra command-line macros
# must not disagree with the precompiled header.
set(ORKIGE_BUILD_COMMIT "" CACHE STRING
    "source commit this editor binary is stamped with (empty = local build)")
set(ORKIGE_BUILD_DATE "" CACHE STRING
    "ISO date this editor binary is stamped with (empty = local build)")
set_source_files_properties(EditorBuildInfo.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_DEFINITIONS
        "ORKIGE_BUILD_COMMIT=\"${ORKIGE_BUILD_COMMIT}\";ORKIGE_BUILD_DATE=\"${ORKIGE_BUILD_DATE}\"")

if(APPLE)
    # native macOS menu bar (NSMenu mirror of the editor menus; ObjC++)
    target_sources(orkige_editor PRIVATE MacMenu.h MacMenu.mm)
    # NSWindow backgroundColor fixup for the rounded-corner slivers (ObjC++)
    target_sources(orkige_editor PRIVATE
        EditorWindowChrome.h EditorWindowChrome.mm)
    # real-NSEvent key injection for the real-event terminal selfcheck (ObjC++):
    # the only in-process way to walk the AppKit keyboard translation a
    # fabricated SDL_Event skips
    target_sources(orkige_editor PRIVATE
        EditorNativeKeyInject.h EditorNativeKeyInject.mm)
    target_link_libraries(orkige_editor PRIVATE "-framework AppKit")

    # a proper .app bundle (Orkige.app): Dock/Cmd-Tab icon + name instead of
    # a bare Ninja executable. The icon is generated procedurally
    # (Util/make_editor_icon.py -> iconset -> iconutil, build-dir artifact),
    # same stdlib-only-Python policy as the gui atlas generator - and the
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
    # ship the icon font (+ its licenses) inside the bundle so a distributed
    # Orkige.app finds it next to the executable via SDL_GetBasePath; out of the
    # build tree the editor falls back to ORKIGE_EDITOR_ICON_FONT_DIR (below)
    # the icon font, the mono symbols fallback (DejaVu Sans - braille + the TUI
    # blocks the system mono font lacks, merged into the terminal/Script mono
    # atlas) and their licenses
    set(_editor_icon_font "${CMAKE_CURRENT_SOURCE_DIR}/media/fa-solid-900.ttf")
    set(_editor_symbols_font "${CMAKE_CURRENT_SOURCE_DIR}/media/DejaVuSans.ttf")
    set(_editor_font_licenses
        "${CMAKE_CURRENT_SOURCE_DIR}/media/LICENSE-fontawesome.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/media/LICENSE-iconfontcppheaders.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/media/LICENSE-dejavu.txt")
    target_sources(orkige_editor PRIVATE
        "${_editor_icon_font}" "${_editor_symbols_font}" ${_editor_font_licenses})
    set_source_files_properties(
        "${_editor_icon_font}" "${_editor_symbols_font}"
        ${_editor_font_licenses} PROPERTIES
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
    # the iOS player app of this (next, the default) flavor rides in the
    # unsuffixed simulator preset tree; the physical-device player in the
    # matching -debug device tree (Play-on-iPhone exports + signs from it)
    set(ORKIGE_EDITOR_IOS_PLAYER_TREE "ios-simulator-debug")
    set(ORKIGE_EDITOR_IOS_DEVICE_TREE "ios-device-debug")
else()
    set(ORKIGE_EDITOR_PLAYER_CLASSIC "$<TARGET_FILE:orkige_player>")
    set(ORKIGE_EDITOR_PLAYER_NEXT
        "${CMAKE_SOURCE_DIR}/${_orkige_sibling_tree_next}/tools/player/orkige_player")
    # the classic-flavor iOS player rides in the -classic simulator/device trees
    set(ORKIGE_EDITOR_IOS_PLAYER_TREE "ios-simulator-debug-classic")
    set(ORKIGE_EDITOR_IOS_DEVICE_TREE "ios-device-debug-classic")
endif()

# The render flavor's shader media in the vcpkg tree - the DEVELOPER-TREE
# fallback behind the copy the app bundle carries (EditorResourcePaths.h):
# classic reads the RTSS library (Main/ + RTShaderLib/), next the Hlms shader
# templates. The marker subdirectory is what tells a real media root from an
# empty directory.
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    set(ORKIGE_EDITOR_VCPKG_MEDIA
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media")
    set(ORKIGE_EDITOR_MEDIA_MARKER "Hlms")
else()
    set(ORKIGE_EDITOR_VCPKG_MEDIA
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media")
    set(ORKIGE_EDITOR_MEDIA_MARKER "Main")
endif()
target_compile_definitions(orkige_editor PRIVATE
    ORKIGE_EDITOR_MEDIA_DIR="${ORKIGE_EDITOR_VCPKG_MEDIA}"
    ORKIGE_EDITOR_MEDIA_MARKER="${ORKIGE_EDITOR_MEDIA_MARKER}"
    # the export-time texture encoder rides beside the player in the bundle
    ORKIGE_EDITOR_TEXCOOK_PATH="$<TARGET_FILE:texcook>"
    ORKIGE_EDITOR_PLAYER_PATH_CLASSIC="${ORKIGE_EDITOR_PLAYER_CLASSIC}"
    ORKIGE_EDITOR_PLAYER_PATH_NEXT="${ORKIGE_EDITOR_PLAYER_NEXT}"
    # sample assets (test_mesh.glb, generated by Util/make_test_mesh.py)
    ORKIGE_EDITOR_ASSET_DIR="${CMAKE_SOURCE_DIR}/samples/hello_orkige/media"
    # jumper sample assets (textured .glb meshes, Util/make_jumper_assets.py)
    ORKIGE_EDITOR_JUMPER_ASSET_DIR="${CMAKE_SOURCE_DIR}/samples/jumper/media"
    # the jumper-lua project (carries a gui_default atlas) - the control
    # self-test copies it to a temp dir for the preview_ui/write_project_file leg
    ORKIGE_EDITOR_JUMPER_LUA_PROJECT="${CMAKE_SOURCE_DIR}/projects/jumper-lua"
    # default directory the Scene Path modal offers for .oscene files
    ORKIGE_EDITOR_SCENE_DIR="${CMAKE_SOURCE_DIR}/samples/scenes"
    # icon font source dir (fa-solid-900.ttf) - the out-of-bundle fallback the
    # editor loads from when running straight out of the build tree
    ORKIGE_EDITOR_ICON_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media"
    # the engine's Python tools (the project exporter, the animation cook) -
    # the source-tree fallback behind the copies the app stages below
    ORKIGE_EDITOR_PYTHON_TOOL_DIR="${Orkige_SOURCE_DIR}/Util"
    # the engine-default font dir (Nunito) the editor registers so a project's
    # .ogui can reference the font by name (matches what the player/exports do)
    ORKIGE_EDITOR_FONT_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/fonts"
    # the engine water media dir (the shared water plane mesh + tiling water
    # normal map WaterComponent references) so a scene's water surface shows its
    # static preview in the editor (matches what the player/exports register)
    ORKIGE_EDITOR_WATER_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/water"
    # the engine decal media dir (default mark + blob-shadow textures the
    # DecalComponent references) so a scene's decal shows its static preview in
    # the editor (matches what the player/exports register)
    ORKIGE_EDITOR_DECAL_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/decals"
    ORKIGE_EDITOR_VERSION="${PROJECT_VERSION}"
    # play mode spawns this binary (Play toolbar button / playtest hooks)
    ORKIGE_EDITOR_PLAYER_PATH="$<TARGET_FILE:orkige_player>"
    # the iOS-simulator player app the editor auto-installs onto a simulator
    # that does not have it yet (produced by the matching-flavor iOS preset -
    # a next editor installs the Ogre-Next player, a classic editor the classic
    # one; Play surfaces an honest error when neither installed nor built)
    ORKIGE_EDITOR_IOS_PLAYER_APP="${CMAKE_SOURCE_DIR}/build/${ORKIGE_EDITOR_IOS_PLAYER_TREE}/tools/player/OrkigePlayer.app"
    # the arm64-ios (physical-device) preset tree name Play-on-iPhone exports +
    # signs the player from (via Util/orkige_export.py --platform ios)
    ORKIGE_EDITOR_IOS_DEVICE_TREE="${ORKIGE_EDITOR_IOS_DEVICE_TREE}"
    # the wasm player the web-release preset builds: its presence makes the
    # Browser play target selectable (Play in Browser exports + serves + opens
    # the default browser - see EditorBrowserServe.cpp)
    ORKIGE_EDITOR_WEB_PLAYER_JS="${CMAKE_SOURCE_DIR}/build/web-release/tools/player/orkige_player.js"
    # native project modules (compile-on-Play): the module build links
    # against THIS build tree with the SAME toolchain settings - see
    # cmake/OrkigeGameModule.cmake and startPlay's native branch
    ORKIGE_EDITOR_ENGINE_ROOT="${CMAKE_SOURCE_DIR}"
    ORKIGE_EDITOR_ENGINE_BUILD_DIR="${CMAKE_BINARY_DIR}"
    # the render flavor THIS editor was built with ("next" or "classic"): the
    # native module links against this editor's own engine tree, so its build
    # dir is flavor-suffixed (native/build-<flavor>) - a module tree is
    # flavor-bound (see cmake/OrkigeGameModule.cmake)
    ORKIGE_EDITOR_RENDER_BACKEND="${ORKIGE_RENDER_BACKEND}"
    ORKIGE_EDITOR_CMAKE="${CMAKE_COMMAND}"
    # the test-runner MCP tools (run_tests / list_tests) drive this ctest
    ORKIGE_EDITOR_CTEST="${CMAKE_CTEST_COMMAND}"
    ORKIGE_EDITOR_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    ORKIGE_EDITOR_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    ORKIGE_EDITOR_SCRIPTING="${ORKIGE_SCRIPTING}"
    ORKIGE_EDITOR_OSX_SYSROOT="${CMAKE_OSX_SYSROOT}"
)
# the engine bloom compositor media - PER FLAVOR (bloom/next vs bloom/classic),
# so a Play session with engine:setBloom resolves its materials in the
# editor's own render context on either backend
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    target_compile_definitions(orkige_editor PRIVATE
        ORKIGE_EDITOR_BLOOM_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/bloom/next")
else()
    target_compile_definitions(orkige_editor PRIVATE
        ORKIGE_EDITOR_BLOOM_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/bloom/classic")
endif()
# the engine output-grade compositor media - PER FLAVOR (grade/next vs
# grade/classic), so a Play session with engine:setGrade resolves its materials
# in the editor's own render context on either backend
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    target_compile_definitions(orkige_editor PRIVATE
        ORKIGE_EDITOR_GRADE_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/grade/next")
else()
    target_compile_definitions(orkige_editor PRIVATE
        ORKIGE_EDITOR_GRADE_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/grade/classic")
endif()

# ============================================================================
# Bundle staging: what a COPIED editor carries with it
# ============================================================================
# The definitions above are DEVELOPER-TREE fallbacks. An editor copied to a
# machine with no repository and no engine build tree resolves everything from
# inside itself instead (EditorResourcePaths.h) - so the build stages that
# payload on every build, under the SAME layout a project export writes
# (Media/<flavor shader tree>, Media/{fonts,water,decals,bloom,grade}), plus
# the host-platform executables the editor spawns. macOS puts content in
# Contents/Resources and the sibling executables in Contents/MacOS (nested
# executables belong beside the main one, so one signature covers the bundle);
# every other platform puts content in share/orkige/ beside the executable.
if(APPLE)
    set(_editor_stage_resources "$<TARGET_BUNDLE_CONTENT_DIR:orkige_editor>/Resources")
    set(_editor_stage_tools "$<TARGET_BUNDLE_CONTENT_DIR:orkige_editor>/MacOS")
else()
    set(_editor_stage_resources "$<TARGET_FILE_DIR:orkige_editor>/share/orkige")
    set(_editor_stage_tools "$<TARGET_FILE_DIR:orkige_editor>")
endif()
set(_editor_stage_media "${_editor_stage_resources}/Media")

# the flavor's shader media: the Hlms templates (next - materials, and with
# them the editor's own ImGui UI, do not work without them) plus the
# Atmosphere sky media when the installed port ships it; or the classic RTSS
# library (Main + RTShaderLib) with the engine's own metal-rough shader
# library merged INTO the staged RTShaderLib, the one location the runtime
# registers (@see Engine::initializeRTShaderSystem).
set(_editor_stage_commands "")
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    set(_editor_media_subdirs "Hlms")
    if(IS_DIRECTORY "${ORKIGE_EDITOR_VCPKG_MEDIA}/Atmosphere")
        list(APPEND _editor_media_subdirs "Atmosphere")
    endif()
else()
    set(_editor_media_subdirs "Main" "RTShaderLib")
endif()
foreach(_subdir IN LISTS _editor_media_subdirs)
    list(APPEND _editor_stage_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${ORKIGE_EDITOR_VCPKG_MEDIA}/${_subdir}"
            "${_editor_stage_media}/${_subdir}")
endforeach()
if(NOT ORKIGE_RENDER_BACKEND STREQUAL "next"
    AND IS_DIRECTORY "${Orkige_SOURCE_DIR}/orkige_engine/media/rtss")
    list(APPEND _editor_stage_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${Orkige_SOURCE_DIR}/orkige_engine/media/rtss"
            "${_editor_stage_media}/RTShaderLib")
endif()
# the engine's own content media, each under the relative name the resolver
# asks for. Absent media stays absent (the consumer already degrades honestly),
# so the list is filtered here rather than failing the build.
foreach(_media_rel IN ITEMS "fonts" "water" "decals"
        "bloom/${ORKIGE_RENDER_BACKEND}" "grade/${ORKIGE_RENDER_BACKEND}")
    if(IS_DIRECTORY "${Orkige_SOURCE_DIR}/orkige_engine/media/${_media_rel}")
        list(APPEND _editor_stage_commands
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${Orkige_SOURCE_DIR}/orkige_engine/media/${_media_rel}"
                "${_editor_stage_media}/${_media_rel}")
    endif()
endforeach()
# the engine's Python tools the editor spawns, under the resource root's Util/
# - the SAME directory name and sibling layout they have in the source tree, so
# a staged script imports its helpers exactly as it does there. This is what
# lets a copied app export a project at all: without them the exporter is a
# path on the machine that built the binary. The set is the exporter's import
# closure (it imports the texture cook, the dylib-closure stager and the icon
# generator; two of those import the PNG codec) plus the animation cook the
# asset import spawns, plus the neutral default app icon an export falls back
# to when a project sets no export.icon.
list(APPEND _editor_stage_commands
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_editor_stage_resources}/Util/media")
foreach(_python_tool IN ITEMS "orkige_export.py" "cook_textures.py"
        "macos_self_contain.py" "orkige_icons.py" "orkige_png.py"
        "cook_vector_anim.py")
    list(APPEND _editor_stage_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${Orkige_SOURCE_DIR}/Util/${_python_tool}"
            "${_editor_stage_resources}/Util/")
endforeach()
list(APPEND _editor_stage_commands
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${Orkige_SOURCE_DIR}/Util/media/orkige_default_icon.png"
        "${_editor_stage_resources}/Util/media/")
if(NOT APPLE)
    # the editor's own UI fonts (+ their licenses): on macOS they ride in the
    # bundle's Resources through MACOSX_PACKAGE_LOCATION above; every other
    # platform stages them into the same resource root the resolver reads
    foreach(_ui_font IN ITEMS "fa-solid-900.ttf" "DejaVuSans.ttf"
            "LICENSE-fontawesome.txt" "LICENSE-iconfontcppheaders.txt"
            "LICENSE-dejavu.txt")
        list(APPEND _editor_stage_commands
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/media/${_ui_font}"
                "${_editor_stage_resources}/")
    endforeach()
endif()
# the host-platform executables the editor spawns: the player (Play) and the
# texture encoder (export cook). Mobile/web players are NOT staged - they are
# separate build trees and a copied editor cannot deploy to a device anyway.
list(APPEND _editor_stage_commands
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:orkige_player>" "${_editor_stage_tools}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:texcook>" "${_editor_stage_tools}/")
if(APPLE)
    # the dylib closure: the classic flavor links vcpkg's Vulkan loader as a
    # DYLIB, so a copied app dies in dyld before main unless the closure rides
    # in Contents/Frameworks and every build-tree rpath is gone. The operation
    # is `orkige_export self-contain` - the same code an export runs, so an app
    # the build stages and an app the exporter writes are self-contained the
    # same way. A fully statically linked flavor has no closure and it no-ops.
    list(APPEND _editor_stage_commands
        COMMAND "$<TARGET_FILE:orkige_export>" self-contain
            --frameworks "$<TARGET_BUNDLE_CONTENT_DIR:orkige_editor>/Frameworks"
            --search "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
            --search "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
            "$<TARGET_FILE:orkige_editor>"
            "${_editor_stage_tools}/$<TARGET_FILE_NAME:orkige_player>"
            "${_editor_stage_tools}/$<TARGET_FILE_NAME:texcook>")
endif()
# ALL + ordered AFTER the editor links: the payload staging always re-runs (a
# relinked player is re-staged and re-pointed the same build) and the dylib
# retargeting must see the final executable. A default build therefore always
# leaves a complete, self-contained app; `--target orkige_editor_bundle` is the
# scoped way to ask for one.
add_custom_target(orkige_editor_bundle ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_editor_stage_tools}"
    ${_editor_stage_commands}
    COMMENT "Staging the editor's bundled resources (engine media, player, texcook)"
    VERBATIM)
add_dependencies(orkige_editor_bundle orkige_editor orkige_player texcook
    orkige_export)

# play mode needs the player binary present at runtime
add_dependencies(orkige_editor orkige_player)
