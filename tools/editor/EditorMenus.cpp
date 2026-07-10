// EditorMenus.cpp - the ImGui menu bar (mirrored natively by MacMenu.mm),
// the View Settings window, the modal popups (About/Scene Path/Unsaved
// Changes/target pickers) and the dockspace + first-run DockBuilder layout.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <imgui_internal.h> // DockBuilder API (programmatic first-run layout)

#include <filesystem>

using Orkige::optr;
using Orkige::woptr;

// every quit path (File > Quit, ESC with nothing selected, the window close
// button / Cmd+Q via SDL_EVENT_QUIT, the native mac menu) funnels through
// here: unsaved changes raise the "Unsaved Changes" confirm modal instead of
// silently dropping the scene. Frame-capped automation runs bypass this on
// purpose (they stop the loop directly).
void requestQuit(EditorState& state, Orkige::EditorCore& core)
{
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
	changed |= ImGui::MenuItem("Scene Hierarchy", nullptr,
		&viewSettings.showHierarchyPanel);
	changed |= ImGui::MenuItem("Inspector", nullptr,
		&viewSettings.showInspectorPanel);
	changed |= ImGui::MenuItem("Console", nullptr,
		&viewSettings.showConsolePanel);
	changed |= ImGui::MenuItem("Stats", nullptr,
		&viewSettings.showStatsPanel);
	changed |= ImGui::MenuItem("Scene", nullptr,
		&viewSettings.showScenePanel);
	changed |= ImGui::MenuItem("Assets", nullptr,
		&viewSettings.showAssetBrowserPanel);
	changed |= ImGui::MenuItem("Tile Palette", nullptr,
		&viewSettings.showTilePalettePanel);
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
	settingsChanged |= ImGui::Checkbox("Show Grid", &viewSettings.showGrid);
	settingsChanged |= ImGui::Checkbox("Orientation Gizmo",
		&viewSettings.showViewGizmo);
	settingsChanged |= ImGui::Checkbox("Reopen Last Project on Launch",
		&viewSettings.reopenLastProject);
	ImGui::SetItemTooltip(
		"start the editor in the most recent project");
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

// the in-window ImGui menu bar. NOT drawn on macOS - the native NSMenu bar
// (MacMenu.mm) mirrors this structure there and routes into the exact same
// functions; keeping both would duplicate every menu.
void drawMainMenuBar(EditorState& state, Orkige::EditorCore& core,
	ViewSettings& viewSettings, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window)
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project..."))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::NewProject);
			}
			if (ImGui::MenuItem("Open Project..."))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::OpenProject);
			}
			if (ImGui::BeginMenu("Open Recent Project",
				!viewSettings.recentProjects.empty()))
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
				state.project.isLoaded()))
			{
				closeProject(state, core);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("New Scene", ORKIGE_EDITOR_MOD_LABEL "+N"))
			{
				newScene(state, core);
			}
			if (ImGui::MenuItem("Open Scene...",
				ORKIGE_EDITOR_MOD_LABEL "+O"))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::OpenScene);
			}
			if (ImGui::BeginMenu("Open Recent",
				!viewSettings.recentScenes.empty()))
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
			if (ImGui::MenuItem("Save Scene", ORKIGE_EDITOR_MOD_LABEL "+S"))
			{
				if (state.currentScenePath.empty())
				{
					requestFileDialog(state, window,
						Orkige::FileDialogAction::SaveSceneAs);
				}
				else
				{
					saveSceneToPath(state, core, state.currentScenePath);
				}
			}
			if (ImGui::MenuItem("Save Scene As...",
				"Shift+" ORKIGE_EDITOR_MOD_LABEL "+S"))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::SaveSceneAs);
			}
			ImGui::Separator();
			// append the current (saved) scene to the project's level sequence
			// (levels.olevels) - a filesystem side effect, not undoable;
			// refusals log to the Console
			if (ImGui::MenuItem("Add Scene to Level Sequence", nullptr, false,
				state.project.isLoaded() && !state.currentScenePath.empty()))
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
			// a project AND a selection (refusals log to the Console)
			if (ImGui::MenuItem("Create Prefab", nullptr, false,
				core.hasSelection() && state.project.isLoaded()))
			{
				createPrefabFromSelection(state, core);
			}
			// Apply / Revert the selected prefab INSTANCE (enabled only on an
			// instance root); refusals log to the Console
			const bool onInstance = core.hasSelection() &&
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
	ViewSettings& viewSettings)
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
		// View > Reset Layout: rebuild from scratch and re-open every panel
		// (a hidden panel would otherwise stay lost in the fresh layout)
		viewSettings.showAllPanels();
		viewSettings.save();
	}
	else
	{
		ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId);
		if (rootNode && rootNode->IsSplitNode())
		{
			return; // imgui.ini restored a layout - keep it
		}
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
	// the Tile Palette shares the left node with the Hierarchy (painting
	// alternates with hierarchy inspection; the bottom node is already tabbed)
	ImGui::DockBuilderDockWindow("Tile Palette###TilePalette", leftId);
	ImGui::DockBuilderDockWindow(INSPECTOR_WINDOW_EDIT, rightId);
	ImGui::DockBuilderDockWindow("Console", bottomId);
	ImGui::DockBuilderDockWindow("Stats", bottomId);
	ImGui::DockBuilderDockWindow("Assets###Assets", bottomId);
	ImGui::DockBuilderDockWindow("Scene", centerId);
	ImGui::DockBuilderFinish(dockspaceId);
}
