// EditorAssetBrowserPanel.cpp - the project Asset browser (WP #76): the open
// project's assets/, scripts/ and scenes/ as a flat, filtered list. It is the
// codebase's FIRST user of ImGui drag & drop across panels (the "ORKIGE_ASSET"
// payload the Scene and Hierarchy panels accept), the home of the generic
// asset-instantiation dispatch (mesh/texture/prefab/scene) and the drop-target
// helper both those panels call.
//
// v1 is a live filesystem walk of the three project directories cross-
// referenced against the AssetDatabase for the stable ids: an fs-walk rather
// than AssetDatabase::listAssets() because the list must ALSO show sidecar-less
// files (dimmed) and the scenes/ directory (which the database does not track
// at all) - listAssets() returns only id-carrying assets under assets/+scripts/.
//
// Split out of main.cpp's per-panel decomposition (see EditorApp.h).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorApp.h"
#include "MeshImport.h"

#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>

// the drag-drop payload tag (declared extern in EditorApp.h)
const char* const ASSET_DND_PAYLOAD = "ORKIGE_ASSET";

namespace
{

namespace fs = std::filesystem;

//! lower-case file extension INCLUDING the dot (".png"); "" when none
std::string lowerExtension(std::string const& path)
{
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext;
}

//! first free "<base>"/"<base> <N>" GameObject id (mirrors the mesh import)
std::string uniqueObjectId(Orkige::GameObjectManager& manager, std::string base)
{
	if (base.empty())
	{
		base = "Object";
	}
	if (!manager.objectExists(base))
	{
		return base;
	}
	int suffix = 2;
	std::string candidate;
	do
	{
		candidate = base + " " + std::to_string(suffix);
		++suffix;
	} while (manager.objectExists(candidate));
	return candidate;
}

//! walk one project directory tree (recursive), appending every non-sidecar
//! file. idTracked = the tree the AssetDatabase indexes (assets/, scripts/):
//! a file with no id there is "sidecar-less" and renders dimmed. scenes/ is not
//! id-tracked, so its rows are never dimmed for a missing id.
void addTree(std::string const& directory, Orkige::Project const& project,
	optr<Orkige::AssetDatabase> const& database, bool idTracked,
	std::vector<AssetBrowserItem>& out)
{
	std::error_code ec;
	if (!fs::is_directory(directory, ec))
	{
		return;
	}
	for (fs::recursive_directory_iterator it(directory, ec), end;
		!ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file(ec))
		{
			continue;
		}
		if (lowerExtension(it->path().string()) ==
			Orkige::AssetDatabase::META_FILE_EXTENSION)
		{
			continue;	// the sidecars themselves are never listed
		}
		AssetBrowserItem entry;
		entry.absolutePath = it->path().string();
		entry.relativePath = project.makeProjectRelative(entry.absolutePath);
		if (entry.relativePath.empty())
		{
			entry.relativePath = it->path().filename().string();
		}
		entry.kind = classifyAsset(entry.absolutePath);
		const Orkige::String id = (idTracked && database)
			? database->idForPath(entry.relativePath) : Orkige::String();
		entry.hasId = !id.empty();
		entry.dimmed = idTracked && id.empty();
		out.push_back(entry);
	}
}

//! delete an asset file AND its sidecar (a filesystem side effect, NOT
//! undoable - like the import that created it; a scene reference to the gone
//! asset self-heals to a placeholder on the next load, Unity-style)
void deleteAssetFile(std::string const& absolutePath)
{
	std::error_code ec;
	fs::remove(absolutePath, ec);
	fs::remove(absolutePath +
		Orkige::AssetDatabase::META_FILE_EXTENSION, ec);
	SDL_Log("orkige_editor: deleted asset '%s'", absolutePath.c_str());
}

} // namespace

//---------------------------------------------------------------------------

std::vector<AssetBrowserItem> enumerateProjectAssets(
	Orkige::Project const& project)
{
	std::vector<AssetBrowserItem> entries;
	if (!project.isLoaded())
	{
		return entries;
	}
	optr<Orkige::AssetDatabase> const& database = project.getAssetDatabase();
	addTree(project.getAssetsDirectory(), project, database, true, entries);
	addTree(project.getScriptsDirectory(), project, database, true, entries);
	addTree(project.getScenesDirectory(), project, database, false, entries);
	std::sort(entries.begin(), entries.end(),
		[](AssetBrowserItem const& a, AssetBrowserItem const& b)
		{ return a.relativePath < b.relativePath; });
	return entries;
}

AssetKind classifyAsset(std::string const& path)
{
	if (Orkige::isSupportedMeshFile(path))
	{
		return AssetKind::Mesh;
	}
	const std::string ext = lowerExtension(path);
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" ||
		ext == ".bmp" || ext == ".gif" || ext == ".dds" || ext == ".ktx")
	{
		return AssetKind::Texture;
	}
	if (ext == ".lua")
	{
		return AssetKind::Script;
	}
	if (ext == ".oscene")
	{
		return AssetKind::Scene;
	}
	if (ext == ".oprefab")
	{
		return AssetKind::Prefab;
	}
	return AssetKind::Unknown;
}

const char* assetKindLabel(AssetKind kind)
{
	switch (kind)
	{
	case AssetKind::Mesh:		return "mesh";
	case AssetKind::Texture:	return "texture";
	case AssetKind::Script:		return "script";
	case AssetKind::Scene:		return "scene";
	case AssetKind::Prefab:		return "prefab";
	case AssetKind::Unknown:	break;
	}
	return "file";
}

void instantiateAssetIntoScene(EditorState& state, Orkige::EditorCore& core,
	AssetKind kind, std::string const& absolutePath)
{
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	const std::string fileName = fs::path(absolutePath).filename().string();
	const std::string stem = fs::path(absolutePath).stem().string();
	switch (kind)
	{
	case AssetKind::Mesh:
	{
		// the mesh is already a project resource (registered in the project
		// group at open time) - instantiate it by bare file name at the origin
		const std::string id = uniqueObjectId(manager,
			stem.empty() ? "Mesh" : stem);
		core.executeCommand(Orkige::onew(new Orkige::CreateObjectCommand(
			id, fileName, Orkige::Vec3::ZERO)));
		break;
	}
	case AssetKind::Texture:
	{
		// SpriteComponent::loadSprite resolves the texture by bare file name
		// across all resource groups (the project assets/ group among them)
		const std::string id = uniqueObjectId(manager,
			stem.empty() ? "Sprite" : stem);
		core.executeCommand(Orkige::onew(new Orkige::CreateSpriteObjectCommand(
			id, fileName, Orkige::Vec3::ZERO)));
		break;
	}
	case AssetKind::Prefab:
	{
		// carry the project-relative reference + the stable asset id onto the
		// new instance root so it re-resolves across renames/moves like a
		// scene-loaded instance
		std::string prefabRef;
		std::string assetId;
		if (state.project.isLoaded())
		{
			prefabRef = state.project.makeProjectRelative(absolutePath);
			if (optr<Orkige::AssetDatabase> const& database =
				state.project.getAssetDatabase())
			{
				assetId = database->idForPath(prefabRef);
			}
		}
		const std::string id = uniqueObjectId(manager,
			stem.empty() ? "Prefab" : stem);
		core.executeCommand(Orkige::onew(
			new Orkige::CreatePrefabInstanceCommand(id, absolutePath, prefabRef,
				assetId, Orkige::Vec3::ZERO)));
		break;
	}
	case AssetKind::Scene:
		openSceneFromPath(state, core, absolutePath);
		break;
	case AssetKind::Script:
	case AssetKind::Unknown:
	default:
		SDL_Log("orkige_editor: '%s' cannot be instantiated on its own "
			"(add it to an object as a component instead)", fileName.c_str());
		break;
	}
}

void handleAssetDropTarget(EditorState& state, Orkige::EditorCore& core)
{
	if (!ImGui::BeginDragDropTarget())
	{
		return;
	}
	if (ImGuiPayload const* payload =
		ImGui::AcceptDragDropPayload(ASSET_DND_PAYLOAD))
	{
		if (payload->Data &&
			payload->DataSize == static_cast<int>(sizeof(AssetDragDropPayload)))
		{
			AssetDragDropPayload data;
			std::memcpy(&data, payload->Data, sizeof(data));
			data.path[sizeof(data.path) - 1] = '\0';
			instantiateAssetIntoScene(state, core, data.kind, data.path);
		}
	}
	ImGui::EndDragDropTarget();
}

// The Assets panel: the open project's assets/scripts/scenes as a flat,
// filtered list. Each row is a drag source ("ORKIGE_ASSET"), double-clicking a
// scene opens it and a prefab instantiates it, and the right-click menu offers
// Instantiate / Reveal in Finder / Delete. Sidecar-less assets (no stable id)
// render dimmed. Project-only: without an open project the panel shows an
// empty-state hint (like the Build menu gating).
void drawAssetBrowserPanel(EditorState& state, Orkige::EditorCore& core,
	bool* visible)
{
	if (!ImGui::Begin("Assets###Assets", visible))
	{
		ImGui::End();
		return;
	}
	if (!state.project.isLoaded())
	{
		ImGui::TextDisabled("Open a project to browse its assets.");
		ImGui::TextDisabled("(File > Open Project...)");
		ImGui::End();
		return;
	}

	// filter box (same ImGuiTextFilter idiom as the Hierarchy)
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputTextWithHint("##assetFilter", "Filter",
		state.assetBrowserFilter.InputBuf,
		IM_ARRAYSIZE(state.assetBrowserFilter.InputBuf)))
	{
		state.assetBrowserFilter.Build();
	}
	ImGui::Separator();

	const std::vector<AssetBrowserItem> entries =
		enumerateProjectAssets(state.project);
	if (ImGui::BeginChild("##assetList"))
	{
		int shown = 0;
		for (AssetBrowserItem const& entry : entries)
		{
			const std::string label = std::string("[") +
				assetKindLabel(entry.kind) + "] " + entry.relativePath;
			if (!state.assetBrowserFilter.PassFilter(label.c_str()))
			{
				continue;
			}
			++shown;
			ImGui::PushID(entry.relativePath.c_str());
			if (entry.dimmed)
			{
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}
			ImGui::Selectable(label.c_str(), false);
			if (entry.dimmed)
			{
				ImGui::PopStyleColor();
				ImGui::SetItemTooltip("no asset id yet (sidecar-less)");
			}
			// double-click: open a scene / instantiate a prefab (meshes and
			// textures instantiate via drag or the context menu instead)
			if (ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (entry.kind == AssetKind::Scene ||
					entry.kind == AssetKind::Prefab)
				{
					instantiateAssetIntoScene(state, core, entry.kind,
						entry.absolutePath);
				}
			}
			// drag source: the codebase's first cross-panel asset drag
			if (ImGui::BeginDragDropSource(
				ImGuiDragDropFlags_SourceNoDisableHover))
			{
				AssetDragDropPayload payload;
				payload.kind = entry.kind;
				SDL_strlcpy(payload.path, entry.absolutePath.c_str(),
					sizeof(payload.path));
				ImGui::SetDragDropPayload(ASSET_DND_PAYLOAD, &payload,
					sizeof(payload));
				ImGui::TextUnformatted(label.c_str());
				ImGui::EndDragDropSource();
			}
			// right-click operations
			if (ImGui::BeginPopupContextItem("##assetmenu"))
			{
				const bool instantiable = entry.kind == AssetKind::Mesh ||
					entry.kind == AssetKind::Texture ||
					entry.kind == AssetKind::Prefab ||
					entry.kind == AssetKind::Scene;
				const char* instantiateLabel =
					entry.kind == AssetKind::Scene ? "Open" : "Instantiate";
				if (ImGui::MenuItem(instantiateLabel, nullptr, false,
					instantiable))
				{
					instantiateAssetIntoScene(state, core, entry.kind,
						entry.absolutePath);
				}
#ifdef __APPLE__
				if (ImGui::MenuItem("Reveal in Finder"))
				{
					const char* revealArgs[] =
						{ "open", "-R", entry.absolutePath.c_str(), nullptr };
					if (SDL_Process* reveal = SDL_CreateProcess(revealArgs, false))
					{
						SDL_DestroyProcess(reveal);
					}
				}
#endif
				ImGui::Separator();
				if (ImGui::MenuItem("Delete"))
				{
					// filesystem side effect, not undoable (like the import)
					deleteAssetFile(entry.absolutePath);
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if (entries.empty())
		{
			ImGui::TextDisabled("no assets yet - drop files onto the window "
				"or use File > Import Mesh...");
		}
		else if (shown == 0)
		{
			ImGui::TextDisabled("no assets match the filter");
		}
	}
	ImGui::EndChild();
	ImGui::End();
}
