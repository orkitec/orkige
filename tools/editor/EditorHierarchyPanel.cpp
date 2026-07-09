// EditorHierarchyPanel.cpp - the Scene Hierarchy panel: the GameObject TREE
// (expand/collapse, drag & drop re-parenting, dimmed inactive objects),
// filter box, inline rename, context menus, double-click focus and the
// remote play-mode tree.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <core_game/PrefabSerializer.h>

#include <cfloat>
#include <map>

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

//! drag & drop payload tag for hierarchy re-parenting (payload = the id text)
const char* const HIERARCHY_DND_PAYLOAD = "ORKIGE_HIERARCHY_OBJECT";

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

//! does the id itself pass the Hierarchy filter box
bool filterMatches(EditorState& state, std::string const& id)
{
	return state.hierarchyFilter.PassFilter(id.c_str());
}

//! @brief is the node visible under the active filter: it matches itself OR
//! any descendant does (a matching child must never be unreachable because
//! its parent's name doesn't match); the row being renamed always shows
bool hierarchyNodeVisible(EditorState& state,
	Orkige::GameObjectManager& manager, std::string const& id)
{
	if (filterMatches(state, id) || state.renamingObjectId == id)
	{
		return true;
	}
	for (Orkige::String const& childId : manager.getChildren(id))
	{
		if (hierarchyNodeVisible(state, manager, childId))
		{
			return true;
		}
	}
	return false;
}

//! drop-target handling shared by the tree rows and the unparent strip
void acceptHierarchyDrop(Orkige::EditorCore& core,
	std::string const& targetParentId)
{
	if (!ImGui::BeginDragDropTarget())
	{
		return;
	}
	if (ImGuiPayload const* payload =
		ImGui::AcceptDragDropPayload(HIERARCHY_DND_PAYLOAD))
	{
		const std::string draggedId(
			static_cast<const char*>(payload->Data),
			static_cast<std::size_t>(payload->DataSize));
		// canReparent-invalid drops (self, own descendant) are refused by
		// reparentObject - nothing happens, nothing enters the undo stack
		core.reparentObject(draggedId, targetParentId);
	}
	ImGui::EndDragDropTarget();
}

//! one edit-mode tree row plus (recursively) its children; visible row ids
//! land in orderedIds for the arrow-key navigation
void drawLocalHierarchyNode(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& sceneCamera, std::string const& id,
	std::vector<std::string>& orderedIds)
{
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	if (!hierarchyNodeVisible(state, manager, id))
	{
		return;
	}
	optr<Orkige::GameObject> gameObject = manager.getGameObject(id).lock();
	if (!gameObject)
	{
		return;
	}
	orderedIds.push_back(id);
	ImGui::PushID(id.c_str());
	if (state.renamingObjectId == id)
	{
		drawHierarchyRenameField(state, core, id);
		ImGui::PopID();
		return;
	}
	Orkige::StringVector const& childIds = manager.getChildren(id);
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;
	if (childIds.empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (core.isSelected(id))
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	// an active filter force-opens every visible branch so the matches
	// underneath are reachable; without a filter ImGui keeps the user's
	// expand/collapse state per node id
	if (state.hierarchyFilter.IsActive())
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	// deactivated objects render dimmed; prefab
	// instances - the marked root and its prefab-provided children - render
	// blue-ish (the prefab cue; deactivation wins over the tint)
	const bool dimmed = !gameObject->isActiveInHierarchy();
	const bool prefabTinted = !dimmed &&
		(!gameObject->getPrefabRef().empty() ||
			Orkige::PrefabSerializer::isPrefabProvided(manager, *gameObject));
	if (dimmed)
	{
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}
	else if (prefabTinted)
	{
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImVec4(0.55f, 0.76f, 1.0f, 1.0f));
	}
	const bool open = ImGui::TreeNodeEx("##node", flags, "%s", id.c_str());
	if (dimmed || prefabTinted)
	{
		ImGui::PopStyleColor();
	}
	// click selects (Cmd/Ctrl+click toggles membership); the arrow region
	// only expands/collapses (ImGui reports no click for it)
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsItemToggledOpen())
	{
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
				// double-click = select + frame
				focusObjectFromDoubleClick(state, core, sceneCamera, id);
			}
		}
	}
	// drag & drop re-parenting: every row is a source AND a target
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
	{
		ImGui::SetDragDropPayload(HIERARCHY_DND_PAYLOAD, id.c_str(),
			id.size());
		ImGui::TextUnformatted(id.c_str());
		ImGui::EndDragDropSource();
	}
	acceptHierarchyDrop(core, id);
	// right-click selects (a right-click on a member of a multi-selection
	// keeps the set - Duplicate/Delete then operate on ALL selected), then
	// offers the operations
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
		ImGui::Separator();
		if (ImGui::MenuItem("Group Selection",
			ORKIGE_EDITOR_MOD_LABEL "+G"))
		{
			core.groupSelected();
		}
		// prefabs live in the open project's assets/ (an instance root
		// re-makes its own prefab file); refusals log to the Console
		if (ImGui::MenuItem("Create Prefab", nullptr, false,
			state.project.isLoaded()))
		{
			createPrefabFromSelection(state, core);
		}
		// Apply / Revert on a prefab instance root (the prefab overflow
		// menu); enabled only when THIS object is an instance root
		if (core.canApplyOrRevertPrefab(id))
		{
			if (ImGui::MenuItem("Apply to Prefab"))
			{
				applyPrefabOverrides(state, core);
			}
			if (ImGui::MenuItem("Revert to Prefab"))
			{
				revertPrefabInstance(state, core);
			}
		}
		if (!gameObject->getParentId().empty() &&
			ImGui::MenuItem("Unparent"))
		{
			core.reparentObject(id, "");
		}
		ImGui::EndPopup();
	}
	if (open)
	{
		// iterate a copy: a context-menu delete/reparent above mutates the
		// child index mid-loop
		const Orkige::StringVector children = childIds;
		for (Orkige::String const& childId : children)
		{
			drawLocalHierarchyNode(state, core, sceneCamera, childId,
				orderedIds);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

//! the edit-mode Hierarchy: the local GameObject tree
void drawLocalHierarchy(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& sceneCamera)
{
	std::vector<std::string> orderedIds;
	// iterate a copy of the root list: tree edits mutate it mid-loop
	const Orkige::StringVector rootIds =
		core.getGameObjectManager().getRootObjectIds();
	for (Orkige::String const& id : rootIds)
	{
		drawLocalHierarchyNode(state, core, sceneCamera, id, orderedIds);
	}
	// while a hierarchy drag is in flight, offer a drop strip that
	// un-parents (drop "into empty space" = make it a root)
	if (ImGuiPayload const* payload = ImGui::GetDragDropPayload())
	{
		if (payload->IsDataType(HIERARCHY_DND_PAYLOAD))
		{
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::Selectable("  (drop here to unparent)");
			ImGui::PopStyleColor();
			acceptHierarchyDrop(core, "");
		}
		// Asset browser drop: a mesh/texture/prefab dragged from the Assets
		// panel over the Hierarchy adds it to the scene
		else if (payload->IsDataType(ASSET_DND_PAYLOAD))
		{
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::Selectable("  (drop asset here to add it to the scene)");
			ImGui::PopStyleColor();
			handleAssetDropTarget(state, core);
		}
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
	// keyboard: up/down moves the selection through the VISIBLE rows in
	// tree order; F2/Delete/Cmd+D/Cmd+G live in the central shortcut map
	// (handleEditorShortcuts), which covers the focused Hierarchy too
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

//! per-id remote tree lookup built once per frame from the parallel lists
struct RemoteTree
{
	std::map<std::string, std::vector<std::string>> children;
	std::map<std::string, std::string> parents;
	std::map<std::string, bool> activeSelf;
	std::vector<std::string> roots;

	//! effective active state: own flag AND all ancestors (the editor
	//! derives it - the wire carries activeSelf only)
	bool activeInHierarchy(std::string const& id) const
	{
		std::string current = id;
		// parent chains are runtime-validated (cycle guard), but stay
		// defensive against a malformed stream
		std::size_t guard = this->parents.size() + 1;
		while (guard-- > 0)
		{
			auto activeIt = this->activeSelf.find(current);
			if (activeIt != this->activeSelf.end() && !activeIt->second)
			{
				return false;
			}
			auto parentIt = this->parents.find(current);
			if (parentIt == this->parents.end() || parentIt->second.empty())
			{
				return true;
			}
			current = parentIt->second;
		}
		return true;
	}
};

//! @brief is the remote node visible under the filter (same subtree rule as
//! the local tree)
bool remoteNodeVisible(EditorState& state, RemoteTree const& tree,
	std::string const& id)
{
	if (state.hierarchyFilter.PassFilter(id.c_str()))
	{
		return true;
	}
	auto it = tree.children.find(id);
	if (it != tree.children.end())
	{
		for (std::string const& childId : it->second)
		{
			if (remoteNodeVisible(state, tree, childId))
			{
				return true;
			}
		}
	}
	return false;
}

//! one remote (play-mode) tree row plus its children - selection only, the
//! player owns the scene (no rename/reparent/delete during play)
void drawRemoteHierarchyNode(EditorState& state, PlaySession& session,
	RemoteTree const& tree, std::string const& id)
{
	if (!remoteNodeVisible(state, tree, id))
	{
		return;
	}
	ImGui::PushID(id.c_str());
	auto childIt = tree.children.find(id);
	const bool hasChildren =
		childIt != tree.children.end() && !childIt->second.empty();
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (session.remoteSelectedId == id)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (state.hierarchyFilter.IsActive())
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	// objects with a reported script error show in red - the cheap
	// always-visible cue (details are in the Console); inactive objects
	// render dimmed like in edit mode
	const bool scriptError = session.scriptErrorIds.count(id) != 0;
	const bool dimmed = !scriptError && !tree.activeInHierarchy(id);
	if (scriptError)
	{
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImVec4(0.94f, 0.35f, 0.35f, 1.0f));
	}
	else if (dimmed)
	{
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}
	const bool open = ImGui::TreeNodeEx("##node", flags, "%s", id.c_str());
	if (scriptError || dimmed)
	{
		ImGui::PopStyleColor();
	}
	if (scriptError)
	{
		ImGui::SetItemTooltip("script error - see Console");
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsItemToggledOpen() && session.remoteSelectedId != id)
	{
		selectRemoteObject(session, id);
	}
	if (open)
	{
		if (hasChildren)
		{
			for (std::string const& childId : childIt->second)
			{
				drawRemoteHierarchyNode(state, session, tree, childId);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

//! the play-mode Hierarchy: the remote tree streamed by the player (flat
//! fallback when an old player sends no parent list)
void drawRemoteHierarchy(EditorState& state, PlaySession& session)
{
	if (!session.hierarchyReceived)
	{
		ImGui::TextDisabled("waiting for the player...");
		return;
	}
	ImGui::TextDisabled("remote: %s", session.remoteScenePath.c_str());
	ImGui::Separator();
	if (session.remoteParents.size() != session.remoteHierarchy.size())
	{
		// pre-tree player: the historical flat list
		for (std::string const& id : session.remoteHierarchy)
		{
			if (!state.hierarchyFilter.PassFilter(id.c_str()))
			{
				continue;
			}
			const bool scriptError = session.scriptErrorIds.count(id) != 0;
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
		return;
	}
	RemoteTree tree;
	for (std::size_t index = 0; index < session.remoteHierarchy.size();
		++index)
	{
		const std::string& id = session.remoteHierarchy[index];
		const std::string& parentId = session.remoteParents[index];
		tree.parents[id] = parentId;
		tree.activeSelf[id] =
			index >= session.remoteActive.size() ||
			session.remoteActive[index] != "0";
		if (parentId.empty())
		{
			tree.roots.push_back(id);
		}
		else
		{
			tree.children[parentId].push_back(id);
		}
	}
	// ids whose parent is not in the list render as roots (defensive)
	for (auto const& [parentId, childIds] : tree.children)
	{
		if (tree.parents.find(parentId) == tree.parents.end())
		{
			for (std::string const& childId : childIds)
			{
				tree.roots.push_back(childId);
			}
		}
	}
	for (std::string const& id : tree.roots)
	{
		drawRemoteHierarchyNode(state, session, tree, id);
	}
}

} // namespace

// Hierarchy panel: the local scene TREE while editing; during play it shows
// the REMOTE tree streamed by the player ("(Remote)" in the title) and
// clicking an entry sends select so the player streams that object's state.
// Edit mode extras: drag & drop re-parenting (drop on a row = parent under
// it, drop on the strip below = unparent), double-click selects AND frames
// the object in the Scene viewport (inline rename is F2 or
// the context menu), right-click opens Duplicate/Rename/Delete/Group (per
// object) or Create Cube/Create Test Mesh (empty space), up/down arrows move
// the selection through the visible rows, deactivated objects render dimmed.
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
		// search/filter box (Hierarchy search): shared between edit
		// and remote mode, ImGuiTextFilter semantics ("a,b" = a or b,
		// "-a" = exclude a); an active filter never hides the row that is
		// being renamed (the edit field must stay reachable) and force-opens
		// the branches above every match
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##hierarchyFilter", "Filter",
			state.hierarchyFilter.InputBuf,
			IM_ARRAYSIZE(state.hierarchyFilter.InputBuf)))
		{
			state.hierarchyFilter.Build();
		}
		if (remote)
		{
			drawRemoteHierarchy(state, session);
		}
		else
		{
			drawLocalHierarchy(state, core, sceneCamera);
		}
	}
	ImGui::End();
}
