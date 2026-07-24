// EditorShortcuts.cpp - the editor-wide keyboard map (tool switching, file
// ops, play toggle, frame/rename/delete/duplicate).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

// The keyboard shortcut map (checked once per frame, after the panels have
// recorded their hover/focus state; inactive while a text field is being
// edited; only Cmd/Ctrl+P works while a play session runs):
//   global: Cmd/Ctrl+P play/stop toggle, Cmd/Ctrl+Z undo,
//   Shift+Cmd/Ctrl+Z redo, Cmd/Ctrl+N new scene, Cmd/Ctrl+O open scene,
//   Cmd/Ctrl+S save, Shift+Cmd/Ctrl+S save as
//   Scene panel hovered/focused: Q select, W translate, E rotate, R scale,
//   X world/local, F frame selected
//   Scene panel OR focused Hierarchy: F2 rename, Delete/Backspace delete,
//   Cmd/Ctrl+D duplicate (the hierarchy shortcuts work there too)
//   ... all of which stand down while fly mode is active (right mouse held):
//   W/A/S/D/Q/E are camera movement then
// On mac WITH the native menu bar installed the File shortcuts never reach
// this map: AppKit consumes the menu key equivalents (Cmd+N/O/S/Shift+S)
// before SDL sees them - the ImGui-side bindings below cover the non-mac
// and headless-fallback cases with the same keys, no double execution.
void handleEditorShortcuts(EditorState& state, Orkige::EditorCore& core,
	PlaySession& session, optr<Orkige::RenderCamera> const& sceneCamera,
	SDL_Window* window)
{
	ImGuiIO& io = ImGui::GetIO();
	// the script debugger's Continue/Step keys work while a break is held -
	// BEFORE the text-input stand-down, since the code editor holds keyboard
	// focus exactly then (F-keys and Cmd/Ctrl+Alt chords never type text)
	handleScriptDebugShortcuts(state, session);
	if (io.WantTextInput)
	{
		return;
	}
	const bool commandDown = io.KeySuper || io.KeyCtrl;
	// Cmd/Ctrl+P: play toggle - Play in edit mode, Stop while a
	// session runs (this calls the exact functions the toolbar buttons call;
	// no shortcut for Pause, that stays a toolbar action)
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_P, false))
	{
		if (!session.isActive())
		{
			// Play needs the SCENE world; the prefab stage refuses it (the
			// toolbar button greys out for the same reason)
			if (!prefabEditBlocks(state, "Play"))
			{
				startPlay(session, core.getGameObjectManager(),
					state.project);
			}
		}
		else if (session.mode != PlaySession::Mode::Stopping)
		{
			requestStopPlay(session);
		}
		return;
	}
	if (session.isActive())
	{
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_Z, false))
	{
		if (io.KeyShift)
		{
			core.redo();
		}
		else
		{
			core.undo();
		}
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		newScene(state, core);	// refuses itself while a prefab is staged
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_O, false))
	{
		// don't raise a dialog whose outcome would only be refused
		if (!prefabEditBlocks(state, "Open Scene"))
		{
			requestFileDialog(state, window,
				Orkige::FileDialogAction::OpenScene);
		}
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		// Shift = Save As (scene mode only); the plain save routes to Save
		// Prefab while a prefab stage is open (saveCurrentDocument), and to
		// the dialog on an unsaved scene (same rule as the File menu)
		if (io.KeyShift)
		{
			if (!prefabEditBlocks(state, "Save Scene As"))
			{
				requestFileDialog(state, window,
					Orkige::FileDialogAction::SaveSceneAs);
			}
		}
		else
		{
			saveCurrentDocument(state, core, window);
		}
		return;
	}
	// object shortcuts (duplicate/rename/delete) work from the Scene panel
	// AND the focused Hierarchy - familiar shortcuts; tool switching stays
	// Scene-panel-only (letters typed while other panels have focus must
	// not silently flip tools). The Asset browser owns the SAME keys for its
	// own selection when it has focus, so stand down entirely there.
	const bool sceneContext = state.scenePanelHovered ||
		state.scenePanelFocused;
	if (state.assetBrowser.focused ||
		(!sceneContext && !state.hierarchyFocused))
	{
		return;
	}
	// while flying (right mouse held) the letter keys belong to the camera
	// (WASD move, Q/E vertical) - no tool switching, no destructive edits
	if (state.flyActive || ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		return;
	}
	if (commandDown)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_D, false))
		{
			core.duplicateSelected();
		}
		// Cmd/Ctrl+G: group the selection under a new empty parent (undoable)
		if (ImGui::IsKeyPressed(ImGuiKey_G, false))
		{
			core.groupSelected();
		}
		return;
	}
	if (sceneContext)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
		{
			core.setActiveTool(Orkige::EditorTool::Select);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_W, false))
		{
			core.setActiveTool(Orkige::EditorTool::Translate);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E, false))
		{
			core.setActiveTool(Orkige::EditorTool::Rotate);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R, false))
		{
			core.setActiveTool(Orkige::EditorTool::Scale);
		}
		// H is the grab-the-world Hand (pan) tool; a momentary Space-drag pans
		// too (handled in the Scene panel) without changing the active tool
		if (ImGui::IsKeyPressed(ImGuiKey_H, false))
		{
			core.setActiveTool(Orkige::EditorTool::Hand);
		}
		// B arms the 2D grid-paint tool - only meaningful once a prefab is
		// armed in the Tile Palette (a no-op otherwise)
		if (ImGui::IsKeyPressed(ImGuiKey_B, false) &&
			!state.tilePalette.armedAssetPath.empty())
		{
			core.setActiveTool(Orkige::EditorTool::Paint);
		}
		// Escape leaves paint mode entirely: disarm the palette and restore
		// the translate tool. While a prefab is armed the Paint tool consumes
		// every viewport click, so an obvious exit matters.
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
			core.getActiveTool() == Orkige::EditorTool::Paint)
		{
			paletteArmAsset(state, core, std::string());
		}
		if (ImGui::IsKeyPressed(ImGuiKey_X, false))
		{
			core.toggleTransformSpace();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			frameSelectedObject(state, core, sceneCamera);
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		startRenameSelected(state, core);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
		ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
	{
		core.deleteSelected();
	}
}
