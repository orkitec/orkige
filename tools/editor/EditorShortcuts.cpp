// EditorShortcuts.cpp - the editor-wide keyboard map (tool switching, file
// ops, play toggle, frame/rename/delete/duplicate).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

// The keyboard shortcut map (checked once per frame, after the panels have
// recorded their hover/focus state; inactive while a text field is being
// edited; only Cmd/Ctrl+P works while a play session runs):
//   global: Cmd/Ctrl+P play/stop toggle (Unity), Cmd/Ctrl+Z undo,
//   Shift+Cmd/Ctrl+Z redo, Cmd/Ctrl+N new scene, Cmd/Ctrl+O open scene,
//   Cmd/Ctrl+S save, Shift+Cmd/Ctrl+S save as
//   Scene panel hovered/focused: Q select, W translate, E rotate, R scale,
//   X world/local, F frame selected
//   Scene panel OR focused Hierarchy: F2 rename, Delete/Backspace delete,
//   Cmd/Ctrl+D duplicate (Unity's hierarchy shortcuts work there too)
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
	if (io.WantTextInput)
	{
		return;
	}
	const bool commandDown = io.KeySuper || io.KeyCtrl;
	// Cmd/Ctrl+P: Unity's play toggle - Play in edit mode, Stop while a
	// session runs (this calls the exact functions the toolbar buttons call;
	// no shortcut for Pause, that stays a toolbar action)
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_P, false))
	{
		if (!session.isActive())
		{
			startPlay(session, core.getGameObjectManager(), state.project);
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
		newScene(state, core);
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_O, false))
	{
		requestFileDialog(state, window, Orkige::FileDialogAction::OpenScene);
		return;
	}
	if (commandDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		// Shift = Save As; a plain save on an unsaved scene also needs the
		// dialog (same rule as the File menu)
		if (io.KeyShift || state.currentScenePath.empty())
		{
			requestFileDialog(state, window,
				Orkige::FileDialogAction::SaveSceneAs);
		}
		else
		{
			saveSceneToPath(state, core, state.currentScenePath);
		}
		return;
	}
	// object shortcuts (duplicate/rename/delete) work from the Scene panel
	// AND the focused Hierarchy - Unity muscle memory; tool switching stays
	// Scene-panel-only (letters typed while other panels have focus must
	// not silently flip tools)
	const bool sceneContext = state.scenePanelHovered ||
		state.scenePanelFocused;
	if (!sceneContext && !state.hierarchyFocused)
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
