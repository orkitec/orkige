// EditorMenus.cpp - the ImGui menu bar (mirrored natively by MacMenu.mm),
// the View Settings window, the modal popups (About/Scene Path/Unsaved
// Changes/target pickers) and the dockspace + first-run DockBuilder layout.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorAutosave.h"
#include "EditorScriptHost.h"

#include <imgui_internal.h> // DockBuilder API (programmatic first-run layout)

using Orkige::optr;
using Orkige::woptr;

// every quit path (File > Quit, the window close button / Cmd+Q via
// SDL_EVENT_QUIT, the native mac menu) funnels through here: unsaved changes
// raise the "Unsaved Changes" confirm modal instead of silently dropping the
// scene. ESC never quits (see the QuitOnEscape intercept in main). Automated
// runs quit directly - a scripted test must exit cleanly without stopping on
// the confirm modal (it stops the loop; here we belt-and-suspenders the case
// where a test drives a quit path with a dirty scene).
void requestQuit(EditorState& state, Orkige::EditorCore& core)
{
	if (gAutomatedRun)
	{
		state.quitRequested = true;
		return;
	}
	if (isPrefabEditActive(state))
	{
		// close the prefab stage first (its own Save/Discard/Cancel confirm
		// when dirty); a resolved close restores the scene and re-enters here,
		// so the normal unsaved-scene protection still applies afterwards
		requestClosePrefabEdit(state, core, /*quitAfter=*/true);
		return;
	}
	if (core.isSceneDirty())
	{
		state.openQuitConfirmPopup = true;
	}
	else
	{
		state.quitRequested = true;
	}
}

namespace
{

// View > Panels checkables - both menu variants (ImGui bar and the floating
// window opened from the native mac menu) route through here; true when a
// visibility flag flipped (caller persists)
bool drawPanelToggleItems(ViewSettings& viewSettings)
{
	bool changed = false;
#define ORKIGE_DRAW_PANEL_ITEM(id, label, visible, member) \
	changed |= ImGui::MenuItem(label, nullptr, &viewSettings.member);
	ORKIGE_EDITOR_PANEL_LIST(ORKIGE_DRAW_PANEL_ITEM)
#undef ORKIGE_DRAW_PANEL_ITEM
	return changed;
}

// the view-settings widgets (grid/gizmo toggles, camera feel sliders, FOV) -
// hosted by the ImGui View menu on non-mac and by the floating "View
// Settings" window the native menu opens on mac; true when anything changed
// (caller persists)
bool drawViewSettingsWidgets(ViewSettings& viewSettings,
	optr<Orkige::RenderCamera> const& sceneCamera)
{
	bool settingsChanged = false;
	// appearance: System follows the OS, Dark/Light pin one look. A change flags
	// a re-apply for the main loop (it owns the content scale + window clear).
	{
		const char* const themeLabels[] = { "System", "Dark", "Light" };
		int themeIndex = static_cast<int>(viewSettings.themeMode);
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::Combo("Theme", &themeIndex, themeLabels,
			IM_ARRAYSIZE(themeLabels)))
		{
			viewSettings.themeMode =
				static_cast<Orkige::EditorThemeMode>(themeIndex);
			if (gEditorState)
			{
				gEditorState->themeReapplyRequested = true;
			}
			settingsChanged = true;
		}
		ImGui::SetItemTooltip("editor colour scheme (System tracks macOS)");
	}
	ImGui::Separator();
	settingsChanged |= ImGui::Checkbox("Show Grid", &viewSettings.showGrid);
	settingsChanged |= ImGui::Checkbox("Orientation Gizmo",
		&viewSettings.showViewGizmo);
	settingsChanged |= ImGui::Checkbox("Reopen Last Project on Launch",
		&viewSettings.reopenLastProject);
	ImGui::SetItemTooltip(
		"start the editor in the most recent project");
	// external code editor: a command template opened for Console file:line
	// clicks and the "Open in External Editor" actions. Empty = autodetect an
	// installed CLI editor, else the platform file opener (no line jump).
	{
		char buffer[512];
		SDL_strlcpy(buffer, viewSettings.externalEditor.c_str(), sizeof(buffer));
		ImGui::SetNextItemWidth(240.0f);
		if (ImGui::InputTextWithHint("External Editor", "auto-detect",
			buffer, sizeof(buffer)))
		{
			viewSettings.externalEditor = buffer;
			settingsChanged = true;
		}
		ImGui::SetItemTooltip("command to open a file at a line, with {file} and "
			"{line} placeholders (e.g. \"code -g {file}:{line}\"). Empty "
			"auto-detects a CLI editor on PATH, else opens with the OS default.");
	}
	ImGui::Separator();
	ImGui::TextDisabled("Camera");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Orbit Speed",
		&viewSettings.orbitSpeed, 0.05f, 2.0f, "%.2f");
	ImGui::SetItemTooltip("Alt+left orbit drag (degrees per point)");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Look Speed",
		&viewSettings.lookSpeed, 0.02f, 1.0f, "%.2f");
	ImGui::SetItemTooltip(
		"fly-mode mouselook (degrees per count of mouse travel)");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Zoom Speed",
		&viewSettings.zoomSpeed, 0.1f, 3.0f, "%.2f");
	ImGui::SetNextItemWidth(160.0f);
	settingsChanged |= ImGui::SliderFloat("Fly Speed",
		&viewSettings.flySpeed, Orkige::FLY_SPEED_MIN,
		Orkige::FLY_SPEED_MAX, "%.1f");
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::SliderFloat("FOV", &viewSettings.fovDeg, 20.0f, 120.0f,
		"%.0f\xC2\xB0"))
	{
		sceneCamera->setFOVy(Orkige::Degree(viewSettings.fovDeg));
		settingsChanged = true;
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Reset View Settings"))
	{
		viewSettings.resetCameraAndDisplayDefaults();
		sceneCamera->setFOVy(Orkige::Degree(viewSettings.fovDeg));
		settingsChanged = true;
	}
	return settingsChanged;
}

} // namespace

// the floating View Settings window: on mac the native menu bar replaces the
// ImGui menu bar that used to host these widgets, so View > View Settings...
// opens them here instead (available on every platform)
void drawViewSettingsWindow(EditorState& state, ViewSettings& viewSettings,
	optr<Orkige::RenderCamera> const& sceneCamera)
{
	if (!state.showViewSettingsWindow)
	{
		return;
	}
	if (ImGui::Begin("View Settings", &state.showViewSettingsWindow,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (drawViewSettingsWidgets(viewSettings, sceneCamera))
		{
			viewSettings.save();
		}
	}
	ImGui::End();
}

// the Project Settings window: the manifest Settings that shape an export (the
// screen orientation today). Reads/writes the OPEN project's Settings and saves
// the .orkproj on change - the same key (export.orientation) the exporter reads
// and the set_project_setting MCP verb writes. Portrait/landscape lock the app
// on device; Auto keeps every orientation. A no-op with no project open.
void drawProjectSettingsWindow(EditorState& state)
{
	if (!state.showProjectSettingsWindow)
	{
		return;
	}
	if (ImGui::Begin("Project Settings", &state.showProjectSettingsWindow,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!state.project.isLoaded())
		{
			ImGui::TextUnformatted("Open a project to edit its settings.");
		}
		else
		{
			// the value maps 1:1 to the exporter's export.orientation vocabulary
			static const char* const kLabels[] = { "Auto", "Portrait",
				"Landscape" };
			static const char* const kValues[] = { "auto", "portrait",
				"landscape" };
			const Orkige::String current =
				state.project.getSetting("export.orientation", "portrait");
			int index = 0;
			for (int i = 0; i < 3; ++i)
			{
				if (current == kValues[i]) { index = i; break; }
			}
			ImGui::TextUnformatted("Screen orientation");
			ImGui::SameLine();
			if (ImGui::Combo("##orientation", &index, kLabels, 3))
			{
				state.project.setSetting("export.orientation", kValues[index]);
				Orkige::String saveError;
				if (!state.project.save(&saveError))
				{
					oDebugError("editor.project", 0, "Project Settings: could "
						"not save the manifest - " << saveError);
				}
			}
			ImGui::TextDisabled("Locks the exported app's orientation "
				"(iOS Info.plist + Android manifest) and the runtime window.");
		}
	}
	ImGui::End();
}

// the in-window ImGui menu bar. NOT drawn on macOS - the native NSMenu bar
// (MacMenu.mm) mirrors this structure there and routes into the exact same
// functions; keeping both would duplicate every menu.
void drawMainMenuBar(EditorState& state, Orkige::EditorCore& core,
	ViewSettings& viewSettings, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window)
{
	// while a prefab stage is open the scene/project lifecycle items grey out
	// (their functions refuse anyway - the disable is the honest UI); Save
	// routes to Save Prefab and Close Prefab appears
	const bool prefabMode = isPrefabEditActive(state);
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project...", nullptr, false, !prefabMode))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::NewProject);
			}
			if (ImGui::MenuItem("Open Project...", nullptr, false,
				!prefabMode))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::OpenProject);
			}
			if (ImGui::BeginMenu("Open Recent Project",
				!viewSettings.recentProjects.empty() && !prefabMode))
			{
				// iterate a copy: a click reorders the list mid-iteration
				const std::vector<std::string> recents =
					viewSettings.recentProjects;
				for (std::string const& recentRoot : recents)
				{
					if (ImGui::MenuItem(recentRoot.c_str()))
					{
						openProjectFromPath(state, core, recentRoot);
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Close Project", nullptr, false,
				state.project.isLoaded() && !prefabMode))
			{
				closeProject(state, core);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("New Scene", ORKIGE_EDITOR_MOD_LABEL "+N",
				false, !prefabMode))
			{
				newScene(state, core);
			}
			if (ImGui::MenuItem("Open Scene...",
				ORKIGE_EDITOR_MOD_LABEL "+O", false, !prefabMode))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::OpenScene);
			}
			if (ImGui::BeginMenu("Open Recent",
				!viewSettings.recentScenes.empty() && !prefabMode))
			{
				// iterate a copy: a click reorders the list mid-iteration
				const std::vector<std::string> recents =
					viewSettings.recentScenes;
				for (std::string const& recentPath : recents)
				{
					if (ImGui::MenuItem(recentPath.c_str()))
					{
						openSceneFromPath(state, core, recentPath);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem(prefabMode ? "Save Prefab" : "Save Scene",
				ORKIGE_EDITOR_MOD_LABEL "+S"))
			{
				saveCurrentDocument(state, core, window);
			}
			if (ImGui::MenuItem("Save Scene As...",
				"Shift+" ORKIGE_EDITOR_MOD_LABEL "+S", false, !prefabMode))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::SaveSceneAs);
			}
			if (prefabMode && ImGui::MenuItem("Close Prefab"))
			{
				requestClosePrefabEdit(state, core);
			}
			ImGui::Separator();
			// append the current (saved) scene to the project's level sequence
			// (levels.olevels) - a filesystem side effect, not undoable;
			// refusals log to the Console
			if (ImGui::MenuItem("Add Scene to Level Sequence", nullptr, false,
				state.project.isLoaded() &&
				!state.currentScenePath.empty() && !prefabMode))
			{
				addCurrentSceneToLevels(state, core);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Import Mesh..."))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::ImportMesh);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Quit", "Esc"))
			{
				requestQuit(state, core);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			// undo/redo labels carry the description of the command they
			// would apply ("Undo Delete Cube1")
			const std::string undoLabel = core.canUndo()
				? "Undo " + core.getUndoDescription() : std::string("Undo");
			if (ImGui::MenuItem(undoLabel.c_str(),
				ORKIGE_EDITOR_MOD_LABEL "+Z", false, core.canUndo()))
			{
				core.undo();
			}
			const std::string redoLabel = core.canRedo()
				? "Redo " + core.getRedoDescription() : std::string("Redo");
			if (ImGui::MenuItem(redoLabel.c_str(),
				"Shift+" ORKIGE_EDITOR_MOD_LABEL "+Z", false, core.canRedo()))
			{
				core.redo();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate", ORKIGE_EDITOR_MOD_LABEL "+D",
				false, core.hasSelection()))
			{
				core.duplicateSelected();
			}
			if (ImGui::MenuItem("Rename", "F2", false, core.hasSelection()))
			{
				startRenameSelected(state, core);
			}
			if (ImGui::MenuItem("Delete", "Del", false, core.hasSelection()))
			{
				core.deleteSelected();
			}
			if (ImGui::MenuItem("Group Selection",
				ORKIGE_EDITOR_MOD_LABEL "+G", false, core.hasSelection()))
			{
				core.groupSelected();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Cube"))
			{
				core.createCube();
			}
			if (ImGui::MenuItem("Create Test Mesh"))
			{
				core.createTestMesh();
			}
			ImGui::Separator();
			// prefabs live in the open project's assets/ - enabled only with
			// a project AND a selection (refusals log to the Console); all
			// three grey out while a prefab stage is open (no nesting)
			if (ImGui::MenuItem("Create Prefab", nullptr, false,
				core.hasSelection() && state.project.isLoaded() &&
				!prefabMode))
			{
				createPrefabFromSelection(state, core);
			}
			// Apply / Revert the selected prefab INSTANCE (enabled only on an
			// instance root); refusals log to the Console
			const bool onInstance = core.hasSelection() && !prefabMode &&
				core.canApplyOrRevertPrefab(core.getSelectedObjectId());
			if (ImGui::MenuItem("Apply to Prefab", nullptr, false, onInstance))
			{
				applyPrefabOverrides(state, core);
			}
			if (ImGui::MenuItem("Revert to Prefab", nullptr, false, onInstance))
			{
				revertPrefabInstance(state, core);
			}
			ImGui::EndMenu();
		}
		// project export (async - Util/orkige_export.py; the output lands in
		// the Console as [export] lines); enabled only with a project open
		if (ImGui::BeginMenu("Build"))
		{
			const bool canExport = state.project.isLoaded();
			if (ImGui::MenuItem("Build for macOS", nullptr, false, canExport))
			{
				state.requestedExport = "macos";
			}
			if (ImGui::MenuItem("Build for iOS Simulator", nullptr, false,
				canExport))
			{
				state.requestedExport = "ios-simulator";
			}
			if (ImGui::MenuItem("Build for Android APK", nullptr, false,
				canExport))
			{
				state.requestedExport = "android";
			}
			ImGui::Separator();
			// export config on the project manifest (orientation today); the
			// window edits Project Settings + saves the .orkproj
			if (ImGui::MenuItem("Project Settings…", nullptr, false, canExport))
			{
				state.showProjectSettingsWindow = true;
			}
			ImGui::EndMenu();
		}
		// Tools menu: the project's editor scripts (scripts/*.editor.lua). Each
		// runs once through the verb handler as one undo step; a click just
		// records the request (the frame loop runs it). Conditional states match
		// the other gated menus (a single disabled hint when nothing applies).
		if (ImGui::BeginMenu("Tools"))
		{
			if (!Orkige::EditorScriptHost::scriptingAvailable())
			{
				ImGui::MenuItem("Editor scripts need a scripting build",
					nullptr, false, false);
			}
			else if (!state.project.isLoaded())
			{
				ImGui::MenuItem("Open a project to see its editor scripts",
					nullptr, false, false);
			}
			else if (!state.editorScripts || state.editorScripts->empty())
			{
				ImGui::MenuItem("No editor scripts (scripts/*.editor.lua)",
					nullptr, false, false);
			}
			else
			{
				for (Orkige::EditorScriptTool const& tool :
					state.editorScripts->tools())
				{
					if (ImGui::MenuItem(tool.label.c_str()))
					{
						state.requestedEditorScript = tool.name;
					}
				}
			}
			ImGui::EndMenu();
		}
		// panel visibility, layout reset and the viewport settings (grid,
		// orientation gizmo, camera feel) - persisted to the key=value file
		// next to the imgui ini on change
		if (ImGui::BeginMenu("View"))
		{
			bool settingsChanged = false;
			ImGui::TextDisabled("Panels");
			settingsChanged |= drawPanelToggleItems(viewSettings);
			if (ImGui::MenuItem("Reset Layout"))
			{
				state.resetDockLayout = true;
			}
			ImGui::Separator();
			settingsChanged |=
				drawViewSettingsWidgets(viewSettings, sceneCamera);
			if (settingsChanged)
			{
				viewSettings.save();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			// the published documentation site: the frame loop opens it in
			// the default browser (HELP_PORTAL_URL)
			if (ImGui::MenuItem("Orkige Help"))
			{
				state.requestedHelpPortal = true;
			}
			if (ImGui::MenuItem("About"))
			{
				state.openAboutPopup = true;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

// The editor's modal popups (About, Scene Path, Unsaved Changes) - drawn
// every frame INDEPENDENTLY of the menu bar: on mac the native menu opens
// them via the EditorState flags while the ImGui menu bar is not drawn.
void drawEditorModals(EditorState& state, Orkige::EditorCore& core)
{
	if (state.openAboutPopup)
	{
		ImGui::OpenPopup("About Orkige Editor");
		state.openAboutPopup = false;
	}
	if (ImGui::BeginPopupModal("About Orkige Editor", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Orkige Editor");
		ImGui::Separator();
		ImGui::Text("orkige - the orkitec game engine, version %s",
			ORKIGE_EDITOR_VERSION);
		ImGui::Text("OGRE %d.%d.%d", OGRE_VERSION_MAJOR, OGRE_VERSION_MINOR,
			OGRE_VERSION_PATCH);
		ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
		ImGui::Spacing();
		if (ImGui::Button("Close"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	// "Scene Path" modal: the plain path-input FALLBACK for when a native
	// file dialog failed (see requestFileDialog); confirms the pending
	// SaveSceneAs/OpenScene/ImportMesh action
	if (state.openScenePathPopup)
	{
		ImGui::OpenPopup("Scene Path");
		state.openScenePathPopup = false;
	}
	if (ImGui::BeginPopupModal("Scene Path", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		const char* prompt = "Open scene (.oscene):";
		const char* confirmLabel = "Open";
		if (state.scenePathAction == Orkige::FileDialogAction::SaveSceneAs)
		{
			prompt = "Save scene as (.oscene):";
			confirmLabel = "Save";
		}
		else if (state.scenePathAction ==
			Orkige::FileDialogAction::ImportMesh)
		{
			prompt = "Import mesh (.glb/.gltf/.obj/.fbx/...):";
			confirmLabel = "Import";
		}
		else if (state.scenePathAction ==
			Orkige::FileDialogAction::NewProject)
		{
			prompt = "New project folder:";
			confirmLabel = "Create";
		}
		else if (state.scenePathAction ==
			Orkige::FileDialogAction::OpenProject)
		{
			prompt = "Open project (folder or .orkproj):";
			confirmLabel = "Open";
		}
		ImGui::TextUnformatted(prompt);
		ImGui::SetNextItemWidth(620.0f);
		ImGui::InputText("##scenepath", state.scenePathInput,
			sizeof(state.scenePathInput));
		if (ImGui::Button(confirmLabel))
		{
			const std::string path(state.scenePathInput);
			if (!path.empty())
			{
				switch (state.scenePathAction)
				{
				case Orkige::FileDialogAction::SaveSceneAs:
					saveSceneToPath(state, core, path);
					break;
				case Orkige::FileDialogAction::OpenScene:
					openSceneFromPath(state, core, path);
					break;
				case Orkige::FileDialogAction::ImportMesh:
					importMeshFromPath(state, core, path);
					break;
				case Orkige::FileDialogAction::NewProject:
					newProjectAtPath(state, core, path);
					break;
				case Orkige::FileDialogAction::OpenProject:
					openProjectFromPath(state, core, path);
					break;
				case Orkige::FileDialogAction::None:
					break;
				}
			}
			state.scenePathAction = Orkige::FileDialogAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			state.scenePathAction = Orkige::FileDialogAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	// "Close Prefab" confirm: raised by requestClosePrefabEdit when the
	// prefab stage is dirty - Save/Discard/Cancel; with quit intent a
	// resolved close continues into requestQuit (whose normal unsaved-scene
	// confirm then applies to the restored scene)
	if (state.openPrefabClosePopup)
	{
		ImGui::OpenPopup("Close Prefab");
		state.openPrefabClosePopup = false;
	}
	if (ImGui::BeginPopupModal("Close Prefab", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("The prefab has unsaved changes.");
		ImGui::Spacing();
		const bool quitAfter = state.prefabCloseQuitIntent;
		if (ImGui::Button("Save and Close"))
		{
			// a refused save (stray roots, deleted root) keeps the stage
			// open - closePrefabEdit reports it and never discards silently
			if (closePrefabEdit(state, core, PrefabClosePolicy::Save) &&
				quitAfter)
			{
				requestQuit(state, core);
			}
			state.prefabCloseQuitIntent = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard Changes"))
		{
			if (closePrefabEdit(state, core, PrefabClosePolicy::Discard) &&
				quitAfter)
			{
				requestQuit(state, core);
			}
			state.prefabCloseQuitIntent = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			state.prefabCloseQuitIntent = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	// "Unsaved Changes" confirm: raised by requestQuit when the scene is
	// dirty - the ONLY way a quit proceeds past a dirty scene
	if (state.openQuitConfirmPopup)
	{
		ImGui::OpenPopup("Unsaved Changes");
		state.openQuitConfirmPopup = false;
	}
	if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("The scene has unsaved changes.");
		ImGui::Spacing();
		if (!state.currentScenePath.empty())
		{
			if (ImGui::Button("Save and Quit"))
			{
				if (saveSceneToPath(state, core, state.currentScenePath))
				{
					state.quitRequested = true;
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("Quit Without Saving"))
		{
			state.quitRequested = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	// "Autosave Recovery" confirm: openSceneFromPath raises it when the opened
	// scene has a newer ".autosave" sibling - Restore loads the recovered work
	// (marked dirty), Discard drops the autosave and keeps the on-disk scene
	if (state.openAutosaveRecoveryPopup)
	{
		ImGui::OpenPopup("Autosave Recovery");
		state.openAutosaveRecoveryPopup = false;
	}
	if (ImGui::BeginPopupModal("Autosave Recovery", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("A newer autosave of this scene was found -");
		ImGui::TextUnformatted("a previous session may have ended unexpectedly.");
		ImGui::Spacing();
		if (ImGui::Button("Restore"))
		{
			restoreSceneAutosave(state, core, state.autosaveRecoveryScenePath);
			state.autosaveRecoveryScenePath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			Orkige::EditorAutosave::removeAutosave(
				state.autosaveRecoveryScenePath);
			state.autosaveRecoveryScenePath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// Dockspace filling the work area below the toolbar strip. The first run
// builds the default layout programmatically with the DockBuilder:
// Hierarchy left, Inspector right, Console + Stats tabbed at the
// bottom, Scene panel filling the centre. Afterwards the layout persists
// through imgui.ini (stored next to the executable, see main()) and the
// builder stays out of the way - until View > Reset Layout sets
// state.resetDockLayout, which reruns the builder from scratch (and re-opens
// every panel).
void drawDockspace(EditorState& state, float toolbarHeight,
	ViewSettings& viewSettings, float contentScale)
{
	const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x,
		mainViewport->WorkPos.y + toolbarHeight));
	const ImVec2 hostSize(mainViewport->WorkSize.x,
		mainViewport->WorkSize.y - toolbarHeight);
	ImGui::SetNextWindowSize(hostSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##EditorDockHost", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground);
	ImGui::PopStyleVar(3);
	const ImGuiID dockspaceId = ImGui::GetID("OrkigeEditorDockspace");
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	ImGui::End();
	const bool resetRequested = state.resetDockLayout;
	if ((state.dockLayoutChecked && !resetRequested) ||
		hostSize.x <= 0.0f || hostSize.y <= 0.0f)
	{
		// the very first frame may have no display size yet - try again
		// next frame, DockBuilderSetNodeSize needs a real size (a pending
		// reset request also survives until a frame with a real size)
		return;
	}
	state.dockLayoutChecked = true;
	state.resetDockLayout = false;
	if (resetRequested)
	{
		// View > Reset Layout: rebuild from scratch and restore each panel to
		// its default visibility (the default-closed Tile Palette / GUI Preview
		// stay closed, matching a fresh launch; they re-open on demand)
		viewSettings.resetPanelVisibility();
		viewSettings.save();
	}
	else
	{
		ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId);
		// keep an imgui.ini-restored layout, but only when it was saved at the
		// live content scale - its node sizes are absolute pixels, so a layout
		// authored at another density (moved between a 1x and a retina display)
		// would load mis-proportioned. On a scale mismatch, fall through and
		// rebuild the ratio-based default at the current density.
		const bool scaleMatches =
			std::abs(viewSettings.layoutContentScale - contentScale) < 0.01f;
		if (rootNode && rootNode->IsSplitNode() && scaleMatches)
		{
			return;
		}
	}
	// (re)building the default layout adopts the live density; persist it only
	// on interactive runs (automated runs rebuild fresh every launch and must
	// not race on the shared settings file - see gRecordRecents)
	viewSettings.layoutContentScale = contentScale;
	if (gRecordRecents)
	{
		viewSettings.save();
	}
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, hostSize);
	ImGuiID centerId = dockspaceId;
	ImGuiID leftId = 0;
	ImGuiID rightId = 0;
	ImGuiID bottomId = 0;
	ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f,
		&leftId, &centerId);
	ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.30f,
		&rightId, &centerId);
	ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.30f,
		&bottomId, &centerId);
	ImGui::DockBuilderDockWindow(HIERARCHY_WINDOW_EDIT, leftId);
	ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW_EDIT, rightId);
	ImGui::DockBuilderDockWindow("Console", bottomId);
	ImGui::DockBuilderDockWindow("Stats", bottomId);
	ImGui::DockBuilderDockWindow("Assets###Assets", bottomId);
	// the Tile Palette tabs into the bottom node beside the Asset Browser
	// (both are asset pickers; it auto-opens here when the Scene enters 2D)
	ImGui::DockBuilderDockWindow("Tile Palette###TilePalette", bottomId);
	ImGui::DockBuilderDockWindow("Scene", centerId);
	// the GUI Preview shares the center node with the Scene (a tab beside
	// it - the human flips between the 3D scene and the UI screen)
	ImGui::DockBuilderDockWindow("GuiPreview", centerId);
	ImGui::DockBuilderFinish(dockspaceId);
}

void dockPreviewBesideSceneOnce(const char* panelWindowName, bool& attempted)
{
	if (attempted)
	{
		return;
	}
	ImGuiWindow* scene = ImGui::FindWindowByName("Scene");
	if (!scene || scene->DockId == 0)
	{
		return; // Scene may not have been submitted yet; retry next frame
	}
	ImGuiWindow* preview = ImGui::FindWindowByName(panelWindowName);
	if (!preview || preview->DockId == 0)
	{
		ImGui::SetNextWindowDockID(scene->DockId, ImGuiCond_Always);
	}
	attempted = true;
}
