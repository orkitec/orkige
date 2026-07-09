// EditorHierarchyPanel.cpp - the Scene Hierarchy panel: filter box, inline
// rename, context menus, double-click focus and the remote play-mode list.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <cfloat>

void startRenameSelected(EditorState& state, Orkige::EditorCore& core)
{
	if (!core.hasSelection())
	{
		return;
	}
	state.renamingObjectId = core.getSelectedObjectId();
	SDL_strlcpy(state.renameBuffer, state.renamingObjectId.c_str(),
		sizeof(state.renameBuffer));
	state.renameFocusPending = true;
}

namespace
{

// commit/cancel handling for the inline rename field in the Hierarchy
void drawHierarchyRenameField(EditorState& state, Orkige::EditorCore& core,
	std::string const& id)
{
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (state.renameFocusPending)
	{
		ImGui::SetKeyboardFocusHere();
		state.renameFocusPending = false;
	}
	const bool commit = ImGui::InputText("##rename", state.renameBuffer,
		sizeof(state.renameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_AutoSelectAll);
	if (commit)
	{
		const Orkige::EditorCore::NameValidation validation =
			core.validateRename(id, state.renameBuffer);
		if (validation == Orkige::EditorCore::NameValidation::Ok)
		{
			core.renameObject(id, state.renameBuffer);
			state.renamingObjectId.clear();
		}
		else if (validation ==
			Orkige::EditorCore::NameValidation::Unchanged)
		{
			state.renamingObjectId.clear(); // no-op rename = cancel
		}
		else
		{
			// empty/duplicate rejected: stay in edit so the user can fix it
			state.renameFocusPending = true;
		}
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
		ImGui::IsItemDeactivated())
	{
		state.renamingObjectId.clear(); // focus lost / ESC cancels
	}
}

} // namespace

// Hierarchy panel: the local scene while editing; during play it switches to
// the REMOTE hierarchy streamed by the player ("(Remote)" in the title) and
// clicking an entry sends select so the player streams that object's state.
// Edit mode extras: double-click selects AND frames the object in the Scene
// viewport (Unity behavior; inline rename is F2 or the context menu),
// right-click opens Duplicate/Rename/Delete (per object) or Create Cube/
// Create Test Mesh (empty space), up/down arrows move the selection.
void drawHierarchyPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, optr<Orkige::RenderCamera> const& sceneCamera,
	bool* visible)
{
	const bool remote = session.isActive();
	const bool open =
		ImGui::Begin(remote ? HIERARCHY_WINDOW_REMOTE : HIERARCHY_WINDOW_EDIT,
			visible);
	state.hierarchyFocused = open && !remote && ImGui::IsWindowFocused();
	if (open)
	{
		// search/filter box (Unity's Hierarchy search): shared between edit
		// and remote mode, ImGuiTextFilter semantics ("a,b" = a or b,
		// "-a" = exclude a); an active filter never hides the row that is
		// being renamed (the edit field must stay reachable)
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##hierarchyFilter", "Filter",
			state.hierarchyFilter.InputBuf,
			IM_ARRAYSIZE(state.hierarchyFilter.InputBuf)))
		{
			state.hierarchyFilter.Build();
		}
		if (remote)
		{
			if (!session.hierarchyReceived)
			{
				ImGui::TextDisabled("waiting for the player...");
			}
			else
			{
				ImGui::TextDisabled("remote: %s",
					session.remoteScenePath.c_str());
				ImGui::Separator();
				for (std::string const& id : session.remoteHierarchy)
				{
					if (!state.hierarchyFilter.PassFilter(id.c_str()))
					{
						continue;
					}
					// objects with a reported script error show in red - the
					// cheap always-visible cue (details are in the Console)
					const bool scriptError =
						session.scriptErrorIds.count(id) != 0;
					if (scriptError)
					{
						ImGui::PushStyleColor(ImGuiCol_Text,
							ImVec4(0.94f, 0.35f, 0.35f, 1.0f));
					}
					const bool selected = (session.remoteSelectedId == id);
					if (ImGui::Selectable(id.c_str(), selected) && !selected)
					{
						selectRemoteObject(session, id);
					}
					if (scriptError)
					{
						ImGui::PopStyleColor();
						ImGui::SetItemTooltip("script error - see Console");
					}
				}
			}
		}
		else
		{
			std::vector<std::string> orderedIds;
			for (auto const& [id, gameObject] :
				core.getGameObjectManager().getGameObjects())
			{
				if (!state.hierarchyFilter.PassFilter(id.c_str()) &&
					state.renamingObjectId != id)
				{
					continue;
				}
				orderedIds.push_back(id);
				ImGui::PushID(id.c_str());
				if (state.renamingObjectId == id)
				{
					drawHierarchyRenameField(state, core, id);
					ImGui::PopID();
					continue;
				}
				if (ImGui::Selectable(id.c_str(), core.isSelected(id),
					ImGuiSelectableFlags_AllowDoubleClick))
				{
					// Cmd/Ctrl+click toggles selection-set membership,
					// a plain click replaces the selection
					const bool additive = ImGui::GetIO().KeySuper ||
						ImGui::GetIO().KeyCtrl;
					if (additive)
					{
						core.toggleSelection(id);
					}
					else
					{
						core.selectObject(id);
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							// Unity-style: double-click = select + frame
							// (rename moved to F2 / the context menu)
							focusObjectFromDoubleClick(state, core,
								sceneCamera, id);
						}
					}
				}
				// right-click selects (a right-click on a member of a
				// multi-selection keeps the set - Duplicate/Delete then
				// operate on ALL selected), then offers the operations
				if (ImGui::BeginPopupContextItem("##objectmenu"))
				{
					if (!core.isSelected(id))
					{
						core.selectObject(id);
					}
					if (ImGui::MenuItem("Duplicate"))
					{
						core.duplicateSelected();
					}
					if (ImGui::MenuItem("Rename"))
					{
						startRenameSelected(state, core);
					}
					if (ImGui::MenuItem("Delete"))
					{
						core.deleteSelected();
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			// right-click on empty space: creation menu
			if (ImGui::BeginPopupContextWindow("##createmenu",
				ImGuiPopupFlags_MouseButtonRight |
				ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Create Cube"))
				{
					core.createCube();
				}
				if (ImGui::MenuItem("Create Test Mesh"))
				{
					core.createTestMesh();
				}
				ImGui::EndPopup();
			}
			// keyboard: up/down moves the selection through the (sorted,
			// filtered) list; F2/Delete/Cmd+D live in the central shortcut
			// map (handleEditorShortcuts), which covers the focused
			// Hierarchy too
			if (state.hierarchyFocused && !ImGui::GetIO().WantTextInput &&
				state.renamingObjectId.empty() && !orderedIds.empty())
			{
				int step = 0;
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				{
					step = -1;
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				{
					step = 1;
				}
				if (step != 0)
				{
					int index = -1;
					for (std::size_t i = 0; i < orderedIds.size(); ++i)
					{
						if (core.isSelected(orderedIds[i]))
						{
							index = static_cast<int>(i);
							break;
						}
					}
					index = (index < 0) ? (step > 0 ? 0 : static_cast<int>(
						orderedIds.size()) - 1)
						: std::clamp(index + step, 0,
							static_cast<int>(orderedIds.size()) - 1);
					core.selectObject(orderedIds[index]);
				}
			}
		}
	}
	ImGui::End();
}
