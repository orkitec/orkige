// EditorAssetBrowserPanel.cpp - the project Asset browser (WP #76 -> v2, task
// #93): a Unity-style two-pane FOLDER browser over the open project's assets/,
// scripts/ and scenes/. It is the codebase's FIRST user of ImGui drag & drop
// across panels (the "ORKIGE_ASSET" payload the Scene and Hierarchy panels
// accept), the home of the generic asset-instantiation dispatch
// (mesh/texture/prefab/scene) and the drop-target helper both those panels call.
//
// v2 layout: a folder TREE on the left (expandable ImGui TreeNodes rooted at
// the project root, like the Hierarchy panel) and the CURRENT folder's contents
// on the right (its subfolders then its files). The current folder lives in
// EditorState::assetBrowserCurrentDir; double-clicking a subfolder descends, a
// breadcrumb / ".." goes up. A Create toolbar mints New Folder/Script/Scene in
// the current folder. The ImGuiTextFilter is scoped to the current folder's
// immediate contents (NOT recursive) - it narrows the right pane only.
//
// Enumeration is a live filesystem walk (rather than AssetDatabase::listAssets)
// because the browser must ALSO show sidecar-less files (dimmed) and the
// scenes/ directory the database does not track at all - listAssets() returns
// only id-carrying assets under assets/+scripts/. enumerateProjectAssets does
// the whole-project recursive walk (kept for the selfcheck); enumerateAssetFolder
// does one folder for the panel.
//
// Texture thumbnails reuse the Scene panel's RTT-in-ImGui bridge: a texture is
// bound into an ImGui::Image by its bare resource name through
// ImGuiFacadeRenderer::textureIdForResource -> the DrawLayer2D named-texture
// path (identical machinery to the ImGui font atlas). Mesh/scene/prefab/script
// get a text type icon; rendered mesh previews (a per-asset RTT render) are a
// DEFERRED heavier follow-on.
//
// Split out of main.cpp's per-panel decomposition (see EditorApp.h).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorApp.h"
#include "ImGuiFacadeRenderer.h"
#include "MeshImport.h"

#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_serialization/XMLArchive.h>
#include <engine_render/RenderSystem.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>

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

//! build one AssetBrowserItem for a file path (kind + id/dimming). idTracked =
//! the file lives under an AssetDatabase-indexed tree (assets/, scripts/): a
//! file with no id there is "sidecar-less" and renders dimmed; scenes/ is not
//! id-tracked, so its rows are never dimmed for a missing id.
AssetBrowserItem makeItem(std::string const& absolutePath,
	Orkige::Project const& project, optr<Orkige::AssetDatabase> const& database,
	bool idTracked)
{
	AssetBrowserItem entry;
	entry.absolutePath = absolutePath;
	entry.relativePath = project.makeProjectRelative(absolutePath);
	if (entry.relativePath.empty())
	{
		entry.relativePath = fs::path(absolutePath).filename().string();
	}
	entry.kind = classifyAsset(entry.absolutePath);
	const Orkige::String id = (idTracked && database)
		? database->idForPath(entry.relativePath) : Orkige::String();
	entry.hasId = !id.empty();
	entry.dimmed = idTracked && id.empty();
	return entry;
}

//! is an absolute path inside the project's id-tracked trees (assets/ or
//! scripts/)? (scenes/ and the project root itself are not id-tracked)
bool isIdTrackedPath(Orkige::Project const& project, std::string const& absolutePath)
{
	std::error_code ec;
	for (std::string const& tracked : { project.getAssetsDirectory(),
		project.getScriptsDirectory() })
	{
		const fs::path base = fs::path(tracked);
		fs::path candidate = fs::path(absolutePath);
		for (fs::path p = candidate; !p.empty(); p = p.parent_path())
		{
			if (fs::equivalent(p, base, ec))
			{
				return true;
			}
			if (p == p.parent_path())
			{
				break;
			}
		}
	}
	return false;
}

//! first "<base>"/"<base> N"/"<base>N.ext" name that does not exist in dir
std::string uniqueFileName(std::string const& dir, std::string const& base,
	std::string const& ext)
{
	std::error_code ec;
	fs::path candidate = fs::path(dir) / (base + ext);
	if (!fs::exists(candidate, ec))
	{
		return candidate.string();
	}
	int suffix = 2;
	do
	{
		candidate = fs::path(dir) / (base + " " + std::to_string(suffix) + ext);
		++suffix;
	} while (fs::exists(candidate, ec));
	return candidate.string();
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
		out.push_back(makeItem(it->path().string(), project, database,
			idTracked));
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

AssetFolderListing enumerateAssetFolder(Orkige::Project const& project,
	std::string const& absoluteDir)
{
	AssetFolderListing listing;
	std::error_code ec;
	if (!project.isLoaded() || !fs::is_directory(absoluteDir, ec))
	{
		return listing;
	}
	optr<Orkige::AssetDatabase> const& database = project.getAssetDatabase();
	const bool idTracked = isIdTrackedPath(project, absoluteDir);
	for (fs::directory_iterator it(absoluteDir, ec), end; !ec && it != end;
		it.increment(ec))
	{
		if (it->is_directory(ec))
		{
			listing.subfolders.push_back(it->path().string());
			continue;
		}
		if (!it->is_regular_file(ec))
		{
			continue;
		}
		if (lowerExtension(it->path().string()) ==
			Orkige::AssetDatabase::META_FILE_EXTENSION)
		{
			continue;	// the sidecars themselves are never listed
		}
		listing.files.push_back(makeItem(it->path().string(), project,
			database, idTracked));
	}
	std::sort(listing.subfolders.begin(), listing.subfolders.end());
	std::sort(listing.files.begin(), listing.files.end(),
		[](AssetBrowserItem const& a, AssetBrowserItem const& b)
		{ return a.relativePath < b.relativePath; });
	return listing;
}

ImTextureID assetThumbnailFor(EditorState& state,
	std::string const& absolutePath)
{
	if (!gImGuiRenderer)
	{
		return 0;
	}
	// key by bare file name: the DrawLayer2D named-texture path resolves by
	// bare name across all resource groups (exactly as SpriteComponent does)
	const std::string name = fs::path(absolutePath).filename().string();
	auto cached = state.assetThumbnailCache.find(name);
	if (cached != state.assetThumbnailCache.end())
	{
		return cached->second;
	}
	// bound the cache: a full clear is cheap and simple (the visible working
	// set of a folder is small; nothing here needs true LRU eviction yet)
	if (state.assetThumbnailCache.size() >= ASSET_THUMBNAIL_CACHE_MAX)
	{
		state.assetThumbnailCache.clear();
	}
	ImTextureID id = 0;
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	unsigned int width = 0;
	unsigned int height = 0;
	// getTextureSize LOADS the texture through the resource system (returns
	// false + a log line for a missing/broken image, e.g. a non-image file
	// with a .png name) - so a valid size means a real, bindable texture
	if (render && render->getTextureSize(name, width, height) &&
		width > 0 && height > 0)
	{
		id = gImGuiRenderer->textureIdForResource(name);
	}
	state.assetThumbnailCache[name] = id;
	return id;
}

std::string createFolderInDir(std::string const& dir, std::string const& name)
{
	std::error_code ec;
	if (dir.empty() || name.empty() || !fs::is_directory(dir, ec))
	{
		return "";
	}
	const fs::path target = fs::path(dir) / name;
	if (fs::exists(target, ec) || !fs::create_directory(target, ec))
	{
		SDL_Log("orkige_editor: New Folder '%s' failed", target.string().c_str());
		return "";
	}
	SDL_Log("orkige_editor: created folder '%s'", target.string().c_str());
	return target.string();
}

std::string createScriptInDir(EditorState& state, std::string const& dir,
	std::string const& name)
{
	std::error_code ec;
	if (dir.empty() || name.empty() || !fs::is_directory(dir, ec))
	{
		return "";
	}
	std::string fileName = name;
	if (lowerExtension(fileName) != ".lua")
	{
		fileName += ".lua";
	}
	const std::string scriptPath = (fs::path(dir) / fileName).string();
	{
		// a minimal ScriptComponent template: the init/update/shutdown seam
		// (see projects/jumper-lua/scripts/player.lua for the real contract)
		std::ofstream out(scriptPath, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			SDL_Log("orkige_editor: New Script '%s' failed", scriptPath.c_str());
			return "";
		}
		out <<
			"-- " << fileName << " - a ScriptComponent script.\n"
			"-- init(self) runs once after load, update(self, dt) every frame,\n"
			"-- shutdown(self) on remove. `self` carries the owner (self.id,\n"
			"-- self.gameObject) and its sibling components (self.transform, ...);\n"
			"-- the global `world` reaches other objects, `shared` holds shared state.\n"
			"\n"
			"function init(self)\n"
			"end\n"
			"\n"
			"function update(self, dt)\n"
			"end\n"
			"\n"
			"function shutdown(self)\n"
			"end\n";
	}
	// project mode: mint the stable id right away so a ScriptComponent that
	// references this script serializes the id with the scene (rename survival)
	if (state.project.isLoaded())
	{
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			database->importAsset(scriptPath);
		}
	}
	SDL_Log("orkige_editor: created script '%s'", scriptPath.c_str());
	return scriptPath;
}

std::string createSceneInDir(std::string const& dir, std::string const& name)
{
	std::error_code ec;
	if (dir.empty() || name.empty() || !fs::is_directory(dir, ec))
	{
		return "";
	}
	std::string fileName = name;
	if (lowerExtension(fileName) != ".oscene")
	{
		fileName += ".oscene";
	}
	const std::string scenePath = (fs::path(dir) / fileName).string();
	// write an EMPTY but valid scene through the real serializer primitives:
	// magic + current format version + zero objects, exactly what
	// SceneSerializer::saveScene emits for an empty world (GameObjectManager
	// is a singleton, so there is no throwaway manager to save instead)
	optr<Orkige::XMLArchive> archive = Orkige::onew(new Orkige::XMLArchive());
	if (!archive->startWriting(scenePath))
	{
		SDL_Log("orkige_editor: New Scene '%s' failed", scenePath.c_str());
		return "";
	}
	Orkige::String magic = Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
	archive << magic;
	int version = Orkige::SceneSerializer::SCENE_FORMAT_VERSION;
	archive << version;
	unsigned int objectCount = 0;
	archive << objectCount;
	if (!archive->stopWriting())
	{
		SDL_Log("orkige_editor: New Scene '%s' write error", scenePath.c_str());
		return "";
	}
	SDL_Log("orkige_editor: created scene '%s'", scenePath.c_str());
	return scenePath;
}

std::string fileUrlForPath(std::string const& absolutePath)
{
	std::string path = absolutePath;
#ifdef _WIN32
	// Windows: "C:\dir\file" -> "/C:/dir/file" so the URL is "file:///C:/..."
	std::replace(path.begin(), path.end(), '\\', '/');
	if (!path.empty() && path.front() != '/')
	{
		path.insert(path.begin(), '/');
	}
#endif
	static const char* const HEX = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(path.size() + 8);
	for (unsigned char c : path)
	{
		const bool unreserved = std::isalnum(c) || c == '-' || c == '_' ||
			c == '.' || c == '~' || c == '/';
		if (unreserved)
		{
			encoded.push_back(static_cast<char>(c));
		}
		else
		{
			encoded.push_back('%');
			encoded.push_back(HEX[c >> 4]);
			encoded.push_back(HEX[c & 0x0F]);
		}
	}
	return "file://" + encoded;
}

void openWithDefaultApp(std::string const& absolutePath)
{
	const std::string url = fileUrlForPath(absolutePath);
	if (!SDL_OpenURL(url.c_str()))
	{
		SDL_Log("orkige_editor: could not open '%s' with the default app - %s",
			absolutePath.c_str(), SDL_GetError());
	}
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

namespace
{

//! open a file per its kind on double-click: scene/prefab instantiate/open
//! into the current scene (the v1 behavior); everything else - script, image,
//! mesh (no in-editor viewer yet), unknown - opens with the OS default app
void openAssetEntry(EditorState& state, Orkige::EditorCore& core,
	AssetBrowserItem const& entry)
{
	switch (entry.kind)
	{
	case AssetKind::Scene:
	case AssetKind::Prefab:
		instantiateAssetIntoScene(state, core, entry.kind, entry.absolutePath);
		break;
	case AssetKind::Mesh:
	case AssetKind::Texture:
	case AssetKind::Script:
	case AssetKind::Unknown:
	default:
		openWithDefaultApp(entry.absolutePath);
		break;
	}
}

//! ensure the current folder is valid: reset to the project root when unset,
//! gone, or no longer under the open project (a project switch)
void ensureCurrentDir(EditorState& state)
{
	std::error_code ec;
	// makeProjectRelative returns "" only for a path OUTSIDE the project root
	// (the root itself maps to "."), so an empty result means a stale dir
	// (project switch) - reset to the root. Also reset when it vanished.
	if (state.assetBrowserCurrentDir.empty() ||
		!fs::is_directory(state.assetBrowserCurrentDir, ec) ||
		state.project.makeProjectRelative(
			state.assetBrowserCurrentDir).empty())
	{
		state.assetBrowserCurrentDir = state.project.getRootDirectory();
	}
}

//! the folder TREE (left pane): recursive ImGui TreeNodes rooted at the
//! project root (like the Hierarchy). Clicking a node label makes it the
//! current folder; the arrow expands. The root opens by default.
void drawFolderTree(EditorState& state, std::string const& dir, int depth)
{
	std::error_code ec;
	std::vector<std::string> subfolders;
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
		it.increment(ec))
	{
		if (it->is_directory(ec))
		{
			subfolders.push_back(it->path().string());
		}
	}
	std::sort(subfolders.begin(), subfolders.end());

	std::string name = fs::path(dir).filename().string();
	if (name.empty())
	{
		name = dir;
	}
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;
	if (subfolders.empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (depth == 0)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	if (fs::path(dir) == fs::path(state.assetBrowserCurrentDir))
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	ImGui::PushID(dir.c_str());
	const bool open = ImGui::TreeNodeEx(name.c_str(), flags);
	// a click on the label (not the expand arrow) navigates
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		state.assetBrowserCurrentDir = dir;
	}
	if (open)
	{
		for (std::string const& sub : subfolders)
		{
			drawFolderTree(state, sub, depth + 1);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

//! the breadcrumb strip: the path from the project root to the current
//! folder as clickable segments (Unity-style) - clicking one navigates up
void drawBreadcrumb(EditorState& state)
{
	const fs::path root(state.project.getRootDirectory());
	const fs::path current(state.assetBrowserCurrentDir);
	std::vector<fs::path> chain;
	for (fs::path p = current; ; p = p.parent_path())
	{
		chain.push_back(p);
		std::error_code ec;
		if (fs::equivalent(p, root, ec) || p == p.parent_path())
		{
			break;
		}
	}
	std::reverse(chain.begin(), chain.end());
	for (std::size_t i = 0; i < chain.size(); ++i)
	{
		if (i != 0)
		{
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::TextUnformatted("/");
			ImGui::SameLine(0.0f, 2.0f);
		}
		std::string label = chain[i].filename().string();
		if (label.empty())
		{
			label = chain[i].string();
		}
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::SmallButton(label.c_str()))
		{
			state.assetBrowserCurrentDir = chain[i].string();
		}
		ImGui::PopID();
	}
}

} // namespace

// The Assets panel (v2): a two-pane folder browser over the open project. The
// LEFT pane is a folder tree (ImGui TreeNodes rooted at the project root); the
// RIGHT pane shows the current folder's subfolders (double-click descends) and
// files (thumbnails for textures, type icons otherwise). A breadcrumb + ".."
// navigate up, a Create toolbar mints New Folder/Script/Scene in the current
// folder, and the filter narrows the current folder's contents. Preserved from
// v1: each file row is a drag source ("ORKIGE_ASSET"), the right-click menu
// offers Instantiate / Reveal in Finder / Delete, double-click opens (scene/
// prefab instantiate, everything else -> the OS default app), sidecar-less
// assets render dimmed. Project-only: without an open project the panel shows
// an empty-state hint.
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

	ensureCurrentDir(state);
	const std::string root = state.project.getRootDirectory();

	// --- top toolbar: Create menu + Reveal + filter ------------------------
	if (ImGui::Button("Create"))
	{
		ImGui::OpenPopup("##assetCreate");
	}
	if (ImGui::BeginPopup("##assetCreate"))
	{
		if (ImGui::MenuItem("New Folder"))
		{
			const std::string created = createFolderInDir(
				state.assetBrowserCurrentDir,
				fs::path(uniqueFileName(state.assetBrowserCurrentDir,
					"New Folder", "")).filename().string());
			if (!created.empty())
			{
				state.assetBrowserCurrentDir = created;
			}
		}
		if (ImGui::MenuItem("New Script"))
		{
			createScriptInDir(state, state.assetBrowserCurrentDir,
				fs::path(uniqueFileName(state.assetBrowserCurrentDir,
					"NewScript", ".lua")).filename().string());
		}
		if (ImGui::MenuItem("New Scene"))
		{
			createSceneInDir(state.assetBrowserCurrentDir,
				fs::path(uniqueFileName(state.assetBrowserCurrentDir,
					"NewScene", ".oscene")).filename().string());
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("|");
	ImGui::SameLine();
	drawBreadcrumb(state);

	// filter box (same ImGuiTextFilter idiom as the Hierarchy; scoped to the
	// current folder's immediate contents)
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputTextWithHint("##assetFilter", "Filter (this folder)",
		state.assetBrowserFilter.InputBuf,
		IM_ARRAYSIZE(state.assetBrowserFilter.InputBuf)))
	{
		state.assetBrowserFilter.Build();
	}
	ImGui::Separator();

	// --- two panes: folder tree (left) + folder contents (right) -----------
	const float treeWidth = std::min(220.0f,
		ImGui::GetContentRegionAvail().x * 0.35f);
	if (ImGui::BeginChild("##assetTree", ImVec2(treeWidth, 0.0f),
		ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders))
	{
		drawFolderTree(state, root, 0);
	}
	ImGui::EndChild();
	ImGui::SameLine();

	const AssetFolderListing listing =
		enumerateAssetFolder(state.project, state.assetBrowserCurrentDir);
	if (ImGui::BeginChild("##assetContents"))
	{
		int shown = 0;
		// ".." row (up a folder), unless already at the project root
		std::error_code ec;
		if (!fs::equivalent(state.assetBrowserCurrentDir, root, ec))
		{
			if (ImGui::Selectable("[..] (up)"))
			{
				state.assetBrowserCurrentDir =
					fs::path(state.assetBrowserCurrentDir).parent_path().string();
			}
		}
		// subfolders first: double-click descends
		for (std::string const& sub : listing.subfolders)
		{
			const std::string folderName = fs::path(sub).filename().string();
			const std::string label = std::string("[dir] ") + folderName;
			if (!state.assetBrowserFilter.PassFilter(label.c_str()))
			{
				continue;
			}
			++shown;
			ImGui::PushID(sub.c_str());
			ImGui::Selectable(label.c_str(), false);
			if (ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				state.assetBrowserCurrentDir = sub;
			}
			if (ImGui::BeginPopupContextItem("##foldermenu"))
			{
#ifdef __APPLE__
				if (ImGui::MenuItem("Reveal in Finder"))
				{
					const char* revealArgs[] =
						{ "open", "-R", sub.c_str(), nullptr };
					if (SDL_Process* reveal = SDL_CreateProcess(revealArgs, false))
					{
						SDL_DestroyProcess(reveal);
					}
				}
#endif
				if (ImGui::MenuItem("Delete Folder"))
				{
					fs::remove_all(sub, ec);
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		// files: thumbnail (textures) or type icon, drag source, context menu
		for (AssetBrowserItem const& entry : listing.files)
		{
			const std::string fileName =
				fs::path(entry.absolutePath).filename().string();
			const std::string label = std::string("[") +
				assetKindLabel(entry.kind) + "] " + fileName;
			if (!state.assetBrowserFilter.PassFilter(label.c_str()))
			{
				continue;
			}
			++shown;
			ImGui::PushID(entry.relativePath.c_str());
			// texture thumbnail (lazy + cached); other kinds get the text icon.
			// DEFERRED: rendered mesh/scene/prefab previews (a per-asset RTT
			// render) are a heavier follow-on - only textures preview here.
			const ImTextureID thumb = (entry.kind == AssetKind::Texture)
				? assetThumbnailFor(state, entry.absolutePath) : 0;
			if (thumb)
			{
				const float size = ImGui::GetTextLineHeight();
				ImGui::Image(thumb, ImVec2(size, size));
				ImGui::SameLine();
			}
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
			// double-click: scene/prefab instantiate; script/image/other open
			// with the OS default app (task #93 item 4)
			if (ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				openAssetEntry(state, core, entry);
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
				if (ImGui::MenuItem("Open with default app"))
				{
					openWithDefaultApp(entry.absolutePath);
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
		if (listing.subfolders.empty() && listing.files.empty())
		{
			ImGui::TextDisabled("empty folder - use Create, or drop files onto "
				"the window to import");
		}
		else if (shown == 0)
		{
			ImGui::TextDisabled("nothing in this folder matches the filter");
		}
	}
	ImGui::EndChild();
	ImGui::End();
}
