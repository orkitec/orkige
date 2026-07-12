// EditorAssetBrowserPanel.cpp - the project Asset browser (v3):
// a content browser over the open project's assets/, scripts/ and scenes/. It
// is the codebase's FIRST user of ImGui drag & drop across panels (the
// "ORKIGE_ASSET" payload the Scene and Hierarchy panels accept), of ImGui
// multi-select + the list clipper, and the home of the generic
// asset-instantiation dispatch (mesh/texture/prefab/scene) and the drop-target
// helper both those panels call.
//
// Layout: a folder TREE on the left (expandable ImGui TreeNodes rooted at the
// project root, like the Hierarchy panel) and, on the right, a content grid
// that scales continuously from large thumbnail tiles down to compact list
// rows via a size slider. The current folder lives in
// AssetBrowserState::currentDir and every navigation (tree, breadcrumb, folder
// double-click, "..", back/forward) routes through navigateTo so the history
// stays consistent. A search box (recursive or current-folder) plus per-kind
// filter chips narrow the grid; a Create menu mints New Folder/Script/Scene.
//
// Enumeration is a live filesystem walk (rather than AssetDatabase::listAssets)
// because the browser must ALSO show sidecar-less files (dimmed) and the
// scenes/ directory the database does not track at all - listAssets() returns
// only id-carrying assets under assets/+scripts/. enumerateProjectAssets does
// the whole-project recursive walk (kept for the selfcheck); enumerateAssetFolder
// does one folder for the panel; searchAssets walks a subtree for a name query.
//
// Texture thumbnails reuse the Scene panel's RTT-in-ImGui bridge: a texture is
// bound by its bare resource name through
// ImGuiFacadeRenderer::textureIdForResource -> the DrawLayer2D named-texture
// path (identical machinery to the ImGui font atlas). Loading happens OFF the
// paint path through a budgeted per-frame queue keyed by absolute path + mtime;
// every non-texture kind draws a per-kind icon-font glyph (Font Awesome 6
// solid, colour-tinted; a drawn rounded-rect glyph is the fallback when the
// icon font is unavailable). Rendered mesh previews (a per-asset RTT render)
// are a DEFERRED heavier follow-on.
//
// Selection is keyed by project-relative path (so it survives re-enumeration
// and renames of OTHER items) and drives the asset ops. Filesystem side effects
// (rename/move/duplicate/delete) are honestly NOT undoable: the stable asset id
// travels with the sidecar and scene references self-heal on load, so only
// Delete confirms.
//
// Split out of main.cpp's per-panel decomposition (see EditorApp.h).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorApp.h"
#include "AssetTilePresentation.h"
#include "EditorTheme.h"
#include "IconsFontAwesome6.h"
#include "ImGuiFacadeRenderer.h"
#include "MeshImport.h"

#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_serialization/XMLArchive.h>
#include <core_util/VectorAnimAsset.h>
#include <core_util/VectorAnimEval.h>
#include <core_util/VectorShapeAsset.h>
#include <core_util/VectorShapeRaster.h>
#include <core_util/VectorTessellator.h>
#include <engine_render/RenderSystem.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

using Orkige::optr;
using Orkige::woptr;

// the drag-drop payload tags (declared extern in EditorAssetDnd.h)
const char* const ASSET_DND_PAYLOAD = "ORKIGE_ASSET";
const char* const ASSET_DND_PAYLOAD_MULTI = "ORKIGE_ASSET_MULTI";

// single-entry thumbnail eviction (defined next to assetThumbnailFor below):
// the rename/move/delete asset ops above it destroy the owned shape upload on
// the way out, so a CPU-rasterized .oshape thumbnail is not leaked
// (clearCachedThumbnails, the wholesale form, is declared in EditorApp.h)
void dropCachedThumbnail(AssetBrowserState& browser, std::string const& key);

namespace
{

namespace fs = std::filesystem;

//! the file's last-write time as a raw tick count (0 when it cannot be
//! stat'ed); a monotonic key the thumbnail cache uses to detect an edit
long long fileMTime(std::string const& path)
{
	std::error_code ec;
	const auto stamp = fs::last_write_time(path, ec);
	if (ec)
	{
		return 0;
	}
	return static_cast<long long>(stamp.time_since_epoch().count());
}

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
	entry.mtime = fileMTime(absolutePath);
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

//! first free "<base>"/"<base> <N>" GameObject id (mirrors the mesh import).
//! reserved carries ids already claimed by sibling commands in the same batch,
//! so a multi-drop of same-stemmed assets does not collide when the commands
//! execute; the returned id is inserted into it.
std::string reserveObjectId(Orkige::GameObjectManager& manager,
	std::set<std::string>& reserved, std::string base)
{
	if (base.empty())
	{
		base = "Object";
	}
	std::string candidate = base;
	int suffix = 2;
	while (manager.objectExists(candidate) || reserved.count(candidate) > 0)
	{
		candidate = base + " " + std::to_string(suffix);
		++suffix;
	}
	reserved.insert(candidate);
	return candidate;
}

//! @brief build the undoable creation command for one instantiable asset kind
//! (mesh -> CreateObjectCommand, texture -> CreateSpriteObjectCommand, prefab ->
//! CreatePrefabInstanceCommand). Returns null for scene/script/unknown - a
//! scene OPENS rather than instantiates, the rest are not standalone objects.
//! reserved keeps object ids unique across a batch (see reserveObjectId).
optr<Orkige::EditorCommand> makeInstantiateCommand(EditorState& state,
	Orkige::GameObjectManager& manager, AssetKind kind,
	std::string const& absolutePath, std::set<std::string>& reserved)
{
	const std::string fileName = fs::path(absolutePath).filename().string();
	const std::string stem = fs::path(absolutePath).stem().string();
	switch (kind)
	{
	case AssetKind::Mesh:
	{
		// the mesh is already a project resource (registered in the project
		// group at open time) - instantiate it by bare file name at the origin
		const std::string id = reserveObjectId(manager, reserved,
			stem.empty() ? "Mesh" : stem);
		return Orkige::onew(new Orkige::CreateObjectCommand(
			id, fileName, Orkige::Vec3::ZERO));
	}
	case AssetKind::Texture:
	{
		// SpriteComponent::loadSprite resolves the texture by bare file name
		// across all resource groups (the project assets/ group among them)
		const std::string id = reserveObjectId(manager, reserved,
			stem.empty() ? "Sprite" : stem);
		return Orkige::onew(new Orkige::CreateSpriteObjectCommand(
			id, fileName, Orkige::Vec3::ZERO));
	}
	case AssetKind::VectorShape:
	{
		// VectorShapeComponent::loadShape resolves the .oshape by bare file name
		// across all resource groups (the project assets/ group among them)
		const std::string id = reserveObjectId(manager, reserved,
			stem.empty() ? "Shape" : stem);
		return Orkige::onew(new Orkige::CreateVectorShapeObjectCommand(
			id, fileName, Orkige::Vec3::ZERO));
	}
	case AssetKind::Prefab:
	{
		// no nesting: an instance dropped INTO a prefab edit stage would be a
		// prefab inside a prefab, which the format (and Save Prefab) refuses
		if (prefabEditBlocks(state, "Instantiate Prefab"))
		{
			return optr<Orkige::EditorCommand>();
		}
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
		const std::string id = reserveObjectId(manager, reserved,
			stem.empty() ? "Prefab" : stem);
		return Orkige::onew(new Orkige::CreatePrefabInstanceCommand(
			id, absolutePath, prefabRef, assetId, Orkige::Vec3::ZERO));
	}
	case AssetKind::Scene:
	case AssetKind::Script:
	case AssetKind::Audio:
	case AssetKind::Material:	// assigned into a material slot, not standalone
	case AssetKind::Unknown:
	default:
		return optr<Orkige::EditorCommand>();
	}
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
//! asset self-heals to a placeholder on the next load)
void deleteAssetFile(std::string const& absolutePath)
{
	std::error_code ec;
	fs::remove(absolutePath, ec);
	fs::remove(absolutePath +
		Orkige::AssetDatabase::META_FILE_EXTENSION, ec);
	SDL_Log("orkige_editor: deleted asset '%s'", absolutePath.c_str());
}

//! rebuild the project's assets/ resource index after a filesystem change so a
//! moved/renamed/duplicated asset resolves by bare name again - including one
//! moved INTO a subfolder: registerProjectAssetLocations registers assets/ and
//! each subfolder as its own flat location (a single recursive location indexes
//! subfolder files by sub-path on the next backend, so bare-name loads miss
//! there). Idempotent remove-then-add, non-destructive to loaded resources.
void reindexProjectAssets(EditorState& state)
{
	if (!state.project.isLoaded())
	{
		return;
	}
	registerProjectAssetLocations(state.project.getAssetsDirectory());
}

} // namespace

//---------------------------------------------------------------------------

bool renameAssetEntry(EditorState& state, AssetBrowserItem const& item,
	std::string const& newFileName)
{
	std::error_code ec;
	std::string wanted = newFileName;
	// a rename may not carry a path - only a bare name
	if (wanted.empty() || wanted.find('/') != std::string::npos ||
		wanted.find('\\') != std::string::npos)
	{
		return false;
	}
	const fs::path source(item.absolutePath);
	// keep the original extension when the user typed only a stem
	if (fs::path(wanted).extension().empty())
	{
		wanted += source.extension().string();
	}
	if (wanted == source.filename().string())
	{
		return true;	// a no-op rename is trivially done
	}
	const fs::path dest = source.parent_path() / wanted;
	if (fs::exists(dest, ec))
	{
		SDL_Log("orkige_editor: rename target '%s' already exists",
			dest.string().c_str());
		return false;
	}
	optr<Orkige::AssetDatabase> const& database =
		state.project.getAssetDatabase();
	const bool idTracked = state.project.isLoaded() &&
		isIdTrackedPath(state.project, item.absolutePath);
	bool ok = false;
	if (idTracked && database)
	{
		// the database renames file + sidecar together, preserving the id
		ok = database->moveAsset(
			state.project.makeProjectRelative(source.string()),
			state.project.makeProjectRelative(dest.string()));
	}
	else
	{
		// scenes/ or sidecar-less: a plain file rename (+ its sidecar if any)
		fs::rename(source, dest, ec);
		ok = !ec;
		if (ok)
		{
			const std::string sidecar = source.string() +
				Orkige::AssetDatabase::META_FILE_EXTENSION;
			if (fs::exists(sidecar, ec))
			{
				fs::rename(sidecar, dest.string() +
					Orkige::AssetDatabase::META_FILE_EXTENSION, ec);
			}
		}
	}
	if (!ok)
	{
		SDL_Log("orkige_editor: rename '%s' -> '%s' failed",
			source.string().c_str(), dest.string().c_str());
		return false;
	}
	// the selection + thumbnail keys follow the asset to its new name
	const std::string newRel =
		state.project.makeProjectRelative(dest.string());
	if (state.assetBrowser.selection.erase(item.relativePath) > 0)
	{
		state.assetBrowser.selection.insert(newRel);
	}
	if (state.assetBrowser.selectionAnchor == item.relativePath)
	{
		state.assetBrowser.selectionAnchor = newRel;
	}
	dropCachedThumbnail(state.assetBrowser, source.string());
	reindexProjectAssets(state);	// the new bare name resolves again
	SDL_Log("orkige_editor: renamed '%s' to '%s'", item.relativePath.c_str(),
		newRel.c_str());
	return true;
}

int moveAssetsIntoFolder(EditorState& state,
	std::vector<std::string> const& absolutePaths, std::string const& destDir)
{
	std::error_code ec;
	if (!fs::is_directory(destDir, ec))
	{
		return 0;
	}
	optr<Orkige::AssetDatabase> const& database =
		state.project.getAssetDatabase();
	int moved = 0;
	for (std::string const& sourceAbs : absolutePaths)
	{
		const fs::path source(sourceAbs);
		if (source.parent_path().empty() ||
			fs::equivalent(source.parent_path(), destDir, ec))
		{
			continue;	// already in the destination folder
		}
		const fs::path dest = fs::path(destDir) / source.filename();
		if (fs::exists(dest, ec))
		{
			SDL_Log("orkige_editor: '%s' already exists in the destination",
				source.filename().string().c_str());
			continue;
		}
		const bool isDir = fs::is_directory(source, ec);
		if (isDir)
		{
			// refuse dropping a folder into its own subtree (would recurse)
			bool intoOwnDescendant = false;
			for (fs::path p(destDir); !p.empty(); p = p.parent_path())
			{
				if (fs::equivalent(p, source, ec))
				{
					intoOwnDescendant = true;
					break;
				}
				if (p == p.parent_path())
				{
					break;
				}
			}
			if (intoOwnDescendant)
			{
				SDL_Log("orkige_editor: cannot move folder '%s' into itself",
					source.string().c_str());
				continue;
			}
		}
		const std::string oldRel =
			state.project.makeProjectRelative(sourceAbs);
		const bool idTracked = !isDir && state.project.isLoaded() &&
			isIdTrackedPath(state.project, sourceAbs);
		bool ok = false;
		if (idTracked && database)
		{
			// id-tracked file: the database carries the id (file + sidecar)
			ok = database->moveAsset(oldRel,
				state.project.makeProjectRelative(dest.string()));
		}
		else
		{
			// a folder, a scenes/ file, or a sidecar-less file: a plain rename
			// (a folder carries its sidecars inside; the next refresh re-reads
			// the moved ids)
			fs::rename(source, dest, ec);
			ok = !ec;
			if (ok && !isDir)
			{
				const std::string sidecar = source.string() +
					Orkige::AssetDatabase::META_FILE_EXTENSION;
				if (fs::exists(sidecar, ec))
				{
					fs::rename(sidecar, dest.string() +
						Orkige::AssetDatabase::META_FILE_EXTENSION, ec);
				}
			}
		}
		if (!ok)
		{
			SDL_Log("orkige_editor: move '%s' -> '%s' failed",
				source.string().c_str(), dest.string().c_str());
			continue;
		}
		++moved;
		const std::string newRel =
			state.project.makeProjectRelative(dest.string());
		if (state.assetBrowser.selection.erase(oldRel) > 0)
		{
			state.assetBrowser.selection.insert(newRel);
		}
		dropCachedThumbnail(state.assetBrowser, source.string());
	}
	if (moved > 0)
	{
		reindexProjectAssets(state);	// moved bare names resolve again
		SDL_Log("orkige_editor: moved %d asset(s) into '%s'", moved,
			destDir.c_str());
	}
	return moved;
}

int duplicateAssetEntries(EditorState& state,
	std::vector<std::string> const& absolutePaths)
{
	optr<Orkige::AssetDatabase> const& database =
		state.project.getAssetDatabase();
	std::error_code ec;
	std::set<std::string> copies;
	for (std::string const& sourceAbs : absolutePaths)
	{
		const fs::path source(sourceAbs);
		if (!fs::is_regular_file(source, ec))
		{
			continue;	// folders are not duplicated here
		}
		const bool idTracked = state.project.isLoaded() &&
			isIdTrackedPath(state.project, sourceAbs);
		if (idTracked && database)
		{
			// the database mints a FRESH id for the copy and carries the import
			// block over (settings are per-asset, the id is per-file)
			const std::string copyRel = database->duplicateAsset(
				state.project.makeProjectRelative(sourceAbs));
			if (!copyRel.empty())
			{
				copies.insert(copyRel);
			}
		}
		else
		{
			// scenes/ or sidecar-less: a plain unique-named copy, no sidecar
			const std::string copyPath = uniqueFileName(
				source.parent_path().string(), source.stem().string() + " Copy",
				source.extension().string());
			fs::copy_file(source, copyPath, ec);
			if (!ec)
			{
				const std::string copyRel =
					state.project.makeProjectRelative(copyPath);
				copies.insert(copyRel.empty()
					? fs::path(copyPath).filename().string() : copyRel);
			}
		}
	}
	if (!copies.empty())
	{
		// the copies become the selection (standard content-browser feel)
		state.assetBrowser.selection = copies;
		state.assetBrowser.selectionAnchor = *copies.rbegin();
		reindexProjectAssets(state);	// the copies resolve by bare name
		SDL_Log("orkige_editor: duplicated %d asset(s)",
			static_cast<int>(copies.size()));
	}
	return static_cast<int>(copies.size());
}

int deleteAssetEntries(EditorState& state,
	std::vector<std::string> const& absolutePaths)
{
	std::error_code ec;
	int deleted = 0;
	for (std::string const& targetAbs : absolutePaths)
	{
		if (fs::is_directory(targetAbs, ec))
		{
			fs::remove_all(targetAbs, ec);
			if (!ec)
			{
				++deleted;
			}
		}
		else
		{
			deleteAssetFile(targetAbs);	// file + its sidecar
			++deleted;
		}
		state.assetBrowser.selection.erase(
			state.project.makeProjectRelative(targetAbs));
		dropCachedThumbnail(state.assetBrowser, targetAbs);
	}
	// prune the stale id-map entries the raw removals left behind (cheap at
	// project scale; the panel's live walk enumerates the disk regardless)
	if (state.project.isLoaded())
	{
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			database->refresh(state.project.getRootDirectory(), true);
		}
	}
	SDL_Log("orkige_editor: deleted %d item(s)", deleted);
	return deleted;
}

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

void navigateTo(AssetBrowserState& browser, std::string const& dir)
{
	if (dir.empty() || dir == browser.currentDir)
	{
		browser.currentDir = dir;
		return;
	}
	if (!browser.currentDir.empty())
	{
		browser.backHistory.push_back(browser.currentDir);
	}
	browser.forwardHistory.clear();
	browser.currentDir = dir;
}

void navigateBack(AssetBrowserState& browser)
{
	if (browser.backHistory.empty())
	{
		return;
	}
	browser.forwardHistory.push_back(browser.currentDir);
	browser.currentDir = browser.backHistory.back();
	browser.backHistory.pop_back();
}

void navigateForward(AssetBrowserState& browser)
{
	if (browser.forwardHistory.empty())
	{
		return;
	}
	browser.backHistory.push_back(browser.currentDir);
	browser.currentDir = browser.forwardHistory.back();
	browser.forwardHistory.pop_back();
}

std::vector<AssetBrowserItem> searchAssets(Orkige::Project const& project,
	std::string const& rootDir, std::string const& query, bool recursive,
	unsigned int kindMask)
{
	std::vector<AssetBrowserItem> results;
	std::error_code ec;
	if (!project.isLoaded() || !fs::is_directory(rootDir, ec))
	{
		return results;
	}
	// case-insensitive substring: lower-case the needle once
	std::string needle = query;
	std::transform(needle.begin(), needle.end(), needle.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	optr<Orkige::AssetDatabase> const& database = project.getAssetDatabase();
	// true when a path's file name contains the (already lower-cased) needle;
	// an empty needle matches everything (a pure type-filter with no text)
	const auto nameMatches = [&](fs::path const& path)
	{
		if (needle.empty())
		{
			return true;
		}
		std::string lowered = path.filename().string();
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lowered.find(needle) != std::string::npos;
	};
	const auto consider = [&](fs::path const& file)
	{
		if (lowerExtension(file.string()) ==
			Orkige::AssetDatabase::META_FILE_EXTENSION)
		{
			return;		// the sidecars themselves are never listed
		}
		if (!nameMatches(file))
		{
			return;
		}
		const AssetKind kind = classifyAsset(file.string());
		if (kindMask != 0 &&
			(kindMask & (1u << static_cast<unsigned int>(kind))) == 0)
		{
			return;
		}
		const bool idTracked = isIdTrackedPath(project, file.string());
		results.push_back(makeItem(file.string(), project, database, idTracked));
	};
	// a name-matched folder is a result too: the content pane must show folders,
	// not only files, so a stray search term never hides the whole tree. Folders
	// carry no asset kind, so the type-filter mask does not gate them - they stay
	// as navigation targets regardless.
	const auto considerFolder = [&](fs::path const& dir)
	{
		if (!nameMatches(dir))
		{
			return;
		}
		AssetBrowserItem entry;
		entry.absolutePath = dir.string();
		entry.relativePath = project.makeProjectRelative(dir.string());
		if (entry.relativePath.empty())
		{
			entry.relativePath = dir.filename().string();
		}
		entry.isFolder = true;
		results.push_back(std::move(entry));
	};
	if (recursive)
	{
		for (fs::recursive_directory_iterator it(rootDir, ec), end;
			!ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec))
			{
				consider(it->path());
			}
			else if (it->is_directory(ec))
			{
				considerFolder(it->path());
			}
		}
	}
	else
	{
		for (fs::directory_iterator it(rootDir, ec), end; !ec && it != end;
			it.increment(ec))
		{
			if (it->is_regular_file(ec))
			{
				consider(it->path());
			}
			else if (it->is_directory(ec))
			{
				considerFolder(it->path());
			}
		}
	}
	// folders first, then files, each group by project-relative path (the same
	// folders-before-files order a plain folder listing shows)
	std::sort(results.begin(), results.end(),
		[](AssetBrowserItem const& a, AssetBrowserItem const& b)
		{
			if (a.isFolder != b.isFolder)
			{
				return a.isFolder;
			}
			return a.relativePath < b.relativePath;
		});
	return results;
}

void pruneAssetSelection(AssetBrowserState& browser,
	std::set<std::string> const& presentRelativePaths)
{
	for (auto it = browser.selection.begin(); it != browser.selection.end(); )
	{
		if (presentRelativePaths.count(*it) == 0)
		{
			it = browser.selection.erase(it);
		}
		else
		{
			++it;
		}
	}
	if (!browser.selectionAnchor.empty() &&
		presentRelativePaths.count(browser.selectionAnchor) == 0)
	{
		browser.selectionAnchor.clear();
	}
}

// drop one cached thumbnail, destroying a CPU-rasterized upload we own (vector
// shapes) so the named GPU texture is not leaked; image thumbnails carry no
// uploadName and just leave the map. Call BEFORE erasing/overwriting the entry.
void dropCachedThumbnail(AssetBrowserState& browser, std::string const& key)
{
	auto it = browser.thumbnails.find(key);
	if (it == browser.thumbnails.end())
	{
		return;
	}
	if (!it->second.uploadName.empty())
	{
		if (Orkige::RenderSystem* render = Orkige::RenderSystem::get())
		{
			render->destroyTexture2D(it->second.uploadName);
		}
	}
	browser.thumbnails.erase(it);
}

// drop the whole thumbnail cache, destroying every owned upload first
void clearCachedThumbnails(AssetBrowserState& browser)
{
	if (Orkige::RenderSystem* render = Orkige::RenderSystem::get())
	{
		for (auto const& entry : browser.thumbnails)
		{
			if (!entry.second.uploadName.empty())
			{
				render->destroyTexture2D(entry.second.uploadName);
			}
		}
	}
	browser.thumbnails.clear();
}

// rasterize a .oshape into a CPU RGBA buffer and upload it as a named texture,
// returning the ImGui handle (0 on a parse/build/upload failure -> glyph
// fallback). The upload name is destroyed on eviction (dropCachedThumbnail).
ImTextureID buildShapeThumbnail(std::string const& absolutePath,
	std::string& outUploadName)
{
	outUploadName.clear();
	std::ifstream in(absolutePath, std::ios::binary);
	if (!in)
	{
		return 0;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::vector<Orkige::VectorTessellator::Region> regions;
	if (!Orkige::VectorShapeAsset::parse(buffer.str(), regions))
	{
		return 0;
	}
	const Orkige::VectorTessellator::Bounds bounds =
		Orkige::VectorTessellator::computeBounds(regions);
	Orkige::VectorTessellator::Mesh mesh;
	Orkige::VectorTessellator::build(regions,
		Orkige::VectorTessellator::defaultFeatherWidth(bounds), mesh);
	if (mesh.indices.empty())
	{
		return 0;
	}
	const int side = 128;
	std::vector<unsigned char> pixels(
		static_cast<std::size_t>(side) * side * 4, 0);
	Orkige::VectorShapeRaster::rasterize(mesh, side, side, pixels.data());
	const std::string uploadName = "__oshapethumb_" +
		std::to_string(std::hash<std::string>{}(absolutePath));
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	if (!render ||
		!render->createTexture2D(uploadName, pixels.data(), side, side))
	{
		return 0;
	}
	outUploadName = uploadName;
	return gImGuiRenderer->textureIdForResource(uploadName);
}

// rasterize a .oanim's default clip at frame 0 into a CPU RGBA buffer and
// upload it as a named texture - the .oshape thumbnail path with the animation
// evaluator in front (parse -> build -> evaluateAt(clip 0, t=0) -> composeRegions
// -> the SAME tessellate/raster/upload). Returns 0 (glyph fallback) on any
// parse/build/upload failure. The upload name is destroyed on eviction.
ImTextureID buildAnimThumbnail(std::string const& absolutePath,
	std::string& outUploadName)
{
	outUploadName.clear();
	std::ifstream in(absolutePath, std::ios::binary);
	if (!in)
	{
		return 0;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	Orkige::VectorAnimAsset::Document doc;
	if (!Orkige::VectorAnimAsset::parse(buffer.str(), doc))
	{
		return 0;
	}
	Orkige::VectorAnimEval eval;
	if (!eval.build(doc))
	{
		return 0;
	}
	Orkige::VectorAnimEval::Pose pose;
	if (!eval.evaluateAt(0, 0.0f, pose))
	{
		return 0;
	}
	std::vector<Orkige::VectorTessellator::Region> regions;
	eval.composeRegions(pose, regions);
	const Orkige::VectorTessellator::Bounds bounds =
		Orkige::VectorTessellator::computeBounds(regions);
	Orkige::VectorTessellator::Mesh mesh;
	Orkige::VectorTessellator::build(regions,
		Orkige::VectorTessellator::defaultFeatherWidth(bounds), mesh);
	if (mesh.indices.empty())
	{
		return 0;
	}
	const int side = 128;
	std::vector<unsigned char> pixels(
		static_cast<std::size_t>(side) * side * 4, 0);
	Orkige::VectorShapeRaster::rasterize(mesh, side, side, pixels.data());
	const std::string uploadName = "__oanimthumb_" +
		std::to_string(std::hash<std::string>{}(absolutePath));
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	if (!render ||
		!render->createTexture2D(uploadName, pixels.data(), side, side))
	{
		return 0;
	}
	outUploadName = uploadName;
	return gImGuiRenderer->textureIdForResource(uploadName);
}

ImTextureID assetThumbnailFor(EditorState& state,
	std::string const& absolutePath)
{
	if (!gImGuiRenderer)
	{
		return 0;
	}
	AssetBrowserState& browser = state.assetBrowser;
	const long long mtime = fileMTime(absolutePath);
	// key by ABSOLUTE path + mtime: two folders may hold same-named files
	// while the user reorganises, and an edited file must re-decode
	auto cached = browser.thumbnails.find(absolutePath);
	if (cached != browser.thumbnails.end() && cached->second.mtime == mtime)
	{
		return cached->second.textureId;
	}
	// bound the cache: a full clear is cheap and simple (the visible working
	// set of a folder is small; nothing here needs true LRU eviction yet).
	// clearCachedThumbnails destroys owned shape uploads first.
	if (browser.thumbnails.size() >= ASSET_THUMBNAIL_CACHE_MAX)
	{
		clearCachedThumbnails(browser);
	}
	// a re-decode of an EDITED file (changed mtime) must free the previous
	// upload we own before replacing the entry
	dropCachedThumbnail(browser, absolutePath);
	ImTextureID id = 0;
	std::string uploadName;
	// vector shapes rasterize on the CPU (no resource-system image); everything
	// else resolves as a named resource texture the DrawLayer2D path binds
	if (lowerExtension(absolutePath) == ".oshape")
	{
		id = buildShapeThumbnail(absolutePath, uploadName);
	}
	else if (lowerExtension(absolutePath) == ".oanim")
	{
		id = buildAnimThumbnail(absolutePath, uploadName);
	}
	else
	{
		// the DrawLayer2D named-texture path resolves by bare name across all
		// resource groups (exactly as SpriteComponent does)
		const std::string name = fs::path(absolutePath).filename().string();
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
	}
	browser.thumbnails[absolutePath] = AssetThumbnail{ id, mtime, uploadName };
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

namespace
{

//! post a transient notice to the browser status footer (see AssetBrowserState);
//! a silent filesystem failure (createFolderInDir returning "") surfaces here
void postBrowserStatus(AssetBrowserState& browser, std::string message)
{
	browser.statusMessage = std::move(message);
	browser.statusMessageExpiry = ImGui::GetTime() + 4.0;
}

//! reveal a freshly created item in the CURRENT folder: drop any active
//! search text and type filter (so the new item is never hidden by a stale
//! filter), select it alone and open the inline rename so the user names it at
//! once. The browser stays in the current folder - a new folder is shown IN
//! PLACE, not navigated into (its empty interior looked like nothing happened).
void revealCreatedItem(EditorState& state, std::string const& absolutePath)
{
	AssetBrowserState& browser = state.assetBrowser;
	browser.searchText[0] = '\0';
	browser.kindFilterMask = 0;
	const std::string rel = state.project.makeProjectRelative(absolutePath);
	browser.selection.clear();
	browser.selection.insert(rel);
	browser.selectionAnchor = rel;
	browser.renamingPath = rel;
	SDL_strlcpy(browser.renameBuffer,
		fs::path(absolutePath).filename().string().c_str(),
		sizeof(browser.renameBuffer));
	browser.renameFocusPending = true;
}

} // namespace

std::string createFolderAndReveal(EditorState& state)
{
	AssetBrowserState& browser = state.assetBrowser;
	const std::string name = fs::path(uniqueFileName(browser.currentDir,
		"New Folder", "")).filename().string();
	const std::string created = createFolderInDir(browser.currentDir, name);
	if (created.empty())
	{
		postBrowserStatus(browser, "Could not create a folder in this location.");
		return "";
	}
	revealCreatedItem(state, created);
	return created;
}

std::string createScriptAndReveal(EditorState& state)
{
	AssetBrowserState& browser = state.assetBrowser;
	const std::string name = fs::path(uniqueFileName(browser.currentDir,
		"NewScript", ".lua")).filename().string();
	const std::string created = createScriptInDir(state, browser.currentDir, name);
	if (created.empty())
	{
		postBrowserStatus(browser, "Could not create a script in this location.");
		return "";
	}
	revealCreatedItem(state, created);
	return created;
}

std::string createSceneAndReveal(EditorState& state)
{
	AssetBrowserState& browser = state.assetBrowser;
	const std::string name = fs::path(uniqueFileName(browser.currentDir,
		"NewScene", ".oscene")).filename().string();
	const std::string created = createSceneInDir(browser.currentDir, name);
	if (created.empty())
	{
		postBrowserStatus(browser, "Could not create a scene in this location.");
		return "";
	}
	revealCreatedItem(state, created);
	return created;
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
	if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
	{
		return AssetKind::Audio;
	}
	if (ext == ".oshape")
	{
		return AssetKind::VectorShape;
	}
	if (ext == ".omat")
	{
		return AssetKind::Material;
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
	case AssetKind::Audio:		return "audio";
	case AssetKind::VectorShape:	return "shape";
	case AssetKind::Material:	return "material";
	case AssetKind::Unknown:	break;
	}
	return "file";
}

void instantiateAssetIntoScene(EditorState& state, Orkige::EditorCore& core,
	AssetKind kind, std::string const& absolutePath)
{
	if (kind == AssetKind::Scene)
	{
		openSceneFromPath(state, core, absolutePath);
		return;
	}
	std::set<std::string> reserved;
	if (optr<Orkige::EditorCommand> command = makeInstantiateCommand(state,
		core.getGameObjectManager(), kind, absolutePath, reserved))
	{
		core.executeCommand(command);
		return;
	}
	SDL_Log("orkige_editor: '%s' cannot be instantiated on its own "
		"(add it to an object as a component instead)",
		fs::path(absolutePath).filename().string().c_str());
}

void instantiateAssetsIntoScene(EditorState& state, Orkige::EditorCore& core,
	std::vector<std::string> const& absolutePaths)
{
	// a lone scene drop opens the scene (a scene cannot be batched - opening it
	// replaces the current one; scenes inside a multi-drop are skipped)
	if (absolutePaths.size() == 1 &&
		classifyAsset(absolutePaths.front()) == AssetKind::Scene)
	{
		openSceneFromPath(state, core, absolutePaths.front());
		return;
	}
	std::set<std::string> reserved;
	std::vector<optr<Orkige::EditorCommand>> commands;
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	for (std::string const& absolutePath : absolutePaths)
	{
		if (optr<Orkige::EditorCommand> command = makeInstantiateCommand(state,
			manager, classifyAsset(absolutePath), absolutePath, reserved))
		{
			commands.push_back(command);
		}
	}
	if (commands.size() == 1)
	{
		// one instantiable asset: keep its own descriptive undo label
		core.executeCommand(commands.front());
	}
	else if (commands.size() > 1)
	{
		// many: collapse into ONE undo step (mirrors the multi-select batches)
		optr<Orkige::CompositeCommand> batch = Orkige::onew(
			new Orkige::CompositeCommand("Add " +
				std::to_string(commands.size()) + " assets"));
		for (optr<Orkige::EditorCommand> const& command : commands)
		{
			batch->addCommand(command);
		}
		core.executeCommand(batch);
	}
}

//! collect the absolute asset paths a drop delivers: the multi payload (newline-
//! joined) or the single ORKIGE_ASSET struct. Call inside a BeginDragDropTarget
//! block; returns empty until the payload is actually released.
std::vector<std::string> acceptDroppedAssetPaths()
{
	std::vector<std::string> paths;
	if (ImGuiPayload const* multi =
		ImGui::AcceptDragDropPayload(ASSET_DND_PAYLOAD_MULTI))
	{
		if (multi->Data && multi->DataSize > 0)
		{
			const std::string joined(static_cast<const char*>(multi->Data),
				static_cast<std::size_t>(multi->DataSize));
			std::string::size_type start = 0;
			while (start < joined.size())
			{
				std::string::size_type nl = joined.find('\n', start);
				if (nl == std::string::npos)
				{
					nl = joined.size();
				}
				if (nl > start)
				{
					paths.push_back(joined.substr(start, nl - start));
				}
				start = nl + 1;
			}
		}
	}
	else if (ImGuiPayload const* single =
		ImGui::AcceptDragDropPayload(ASSET_DND_PAYLOAD))
	{
		if (single->Data && single->DataSize ==
			static_cast<int>(sizeof(AssetDragDropPayload)))
		{
			AssetDragDropPayload data;
			std::memcpy(&data, single->Data, sizeof(data));
			data.path[sizeof(data.path) - 1] = '\0';
			paths.emplace_back(data.path);
		}
	}
	return paths;
}

void handleAssetDropTarget(EditorState& state, Orkige::EditorCore& core)
{
	if (!ImGui::BeginDragDropTarget())
	{
		return;
	}
	const std::vector<std::string> paths = acceptDroppedAssetPaths();
	if (!paths.empty())
	{
		instantiateAssetsIntoScene(state, core, paths);
	}
	ImGui::EndDragDropTarget();
}

bool handleSceneDropTarget(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::RenderCamera> const& camera, ImVec2 const& rectMin,
	ImVec2 const& rectSize, bool editor2D)
{
	if (!ImGui::BeginDragDropTarget())
	{
		return false;
	}
	bool tileDrop = false;
	// 2D editor mode: a single PAINTABLE asset dragged onto the grid paints into
	// the hovered cell (arming it first, exactly like a Tile Palette click). A
	// prefab instantiates; a bare texture/.oshape paints a bare tile. Peek to
	// classify the payload before deciding - only a paintable single asset claims
	// the grid path, everything else (a mesh/audio, a multi-drag, the 3D view)
	// stays with the generic instantiate-at-origin below.
	if (editor2D)
	{
		if (ImGuiPayload const* peek = ImGui::AcceptDragDropPayload(
			ASSET_DND_PAYLOAD, ImGuiDragDropFlags_AcceptPeekOnly))
		{
			if (peek->Data && peek->DataSize ==
				static_cast<int>(sizeof(AssetDragDropPayload)))
			{
				AssetDragDropPayload data;
				std::memcpy(&data, peek->Data, sizeof(data));
				data.path[sizeof(data.path) - 1] = '\0';
				if (data.kind == AssetKind::Prefab ||
					data.kind == AssetKind::Texture ||
					data.kind == AssetKind::VectorShape)
				{
					tileDrop = true;	// the grid owns this payload
					if (peek->IsDelivery() &&
						paletteArmAsset(state, core, data.path))
					{
						ImGuiIO& io = ImGui::GetIO();
						const float nx =
							(io.MousePos.x - rectMin.x) / rectSize.x;
						const float ny =
							(io.MousePos.y - rectMin.y) / rectSize.y;
						// the 2D ortho camera looks straight down -Z, so the ray
						// origin's XY is the world point under the drop
						const Orkige::Vec3 world =
							camera->viewportPointToRay(nx, ny).getOrigin();
						const Orkige::EditorPaintGrid grid =
							core.resolvePaintGrid();
						const int col = Orkige::paintCellCoord(
							world.x, grid.originX, grid.cellSize);
						const int row = Orkige::paintCellCoord(
							world.y, grid.originY, grid.cellSize);
						core.paintTileAtCell(
							paletteMakePaintDesc(state.tilePalette),
							Orkige::paintCellCenter(col, grid.originX,
								grid.cellSize),
							Orkige::paintCellCenter(row, grid.originY,
								grid.cellSize),
							grid.cellSize, 0);
					}
				}
			}
		}
	}
	if (!tileDrop)
	{
		const std::vector<std::string> paths = acceptDroppedAssetPaths();
		if (!paths.empty())
		{
			instantiateAssetsIntoScene(state, core, paths);
		}
	}
	ImGui::EndDragDropTarget();
	return tileDrop;
}

bool selectedBrowserTextureMeta(EditorState& state, std::string& metaFilePath,
	std::string& assetId)
{
	AssetBrowserState const& browser = state.assetBrowser;
	if (browser.selection.size() != 1 || !state.project.isLoaded())
	{
		return false;
	}
	const std::string relativePath = *browser.selection.begin();
	if (classifyAsset(relativePath) != AssetKind::Texture)
	{
		return false;
	}
	optr<Orkige::AssetDatabase> const& database =
		state.project.getAssetDatabase();
	if (!database)
	{
		return false;
	}
	assetId = database->idForPath(relativePath);
	if (assetId.empty())
	{
		return false;	// sidecar-less: no settings to edit
	}
	metaFilePath = database->metaFilePathForId(assetId);
	return !metaFilePath.empty();
}

bool applyTextureImportEdit(EditorState& state,
	Orkige::TextureImport const& texture)
{
	std::string metaFilePath;
	std::string assetId;
	if (!selectedBrowserTextureMeta(state, metaFilePath, assetId))
	{
		return false;
	}
	// preserve the id: the sidecar is rewritten with its EXISTING id plus the
	// new <texture> block (the documented keep-the-id contract)
	Orkige::String preservedId = assetId;
	Orkige::AssetDatabase::readMetaFile(metaFilePath, preservedId);
	const bool ok =
		Orkige::AssetDatabase::writeMetaFile(metaFilePath, preservedId, texture);
	if (ok)
	{
		SDL_Log("orkige_editor: wrote texture import settings for '%s' "
			"(already-loaded sprites re-sample on the texture's next load)",
			metaFilePath.c_str());
	}
	return ok;
}

namespace
{

//! the accent colour of a kind's glyph icon (defined below; forward-declared so
//! the folder tree can share the one warm folder tone with the content grid)
ImU32 assetKindColor(AssetKind kind, bool isFolder);

//! a MOVE drop target for a folder (absolute path): accepts a dragged asset (or
//! multi-selection) and moves it into destDir. Call right after the widget the
//! folder is drawn as (a tree node, a breadcrumb segment, a grid folder tile).
void folderDropTarget(EditorState& state, std::string const& destDir)
{
	if (!ImGui::BeginDragDropTarget())
	{
		return;
	}
	const std::vector<std::string> paths = acceptDroppedAssetPaths();
	if (!paths.empty())
	{
		moveAssetsIntoFolder(state, paths, destDir);
	}
	ImGui::EndDragDropTarget();
}

//! open a file per its kind on double-click: a scene opens into the editor, a
//! prefab opens the prefab EDIT stage (instantiating stays on drag-and-drop
//! into the viewport - double-click mirrors how a scene opens rather than
//! embeds); everything else - script, image, mesh (no in-editor viewer yet),
//! unknown - opens with the OS default app
void openAssetEntry(EditorState& state, Orkige::EditorCore& core,
	AssetBrowserItem const& entry)
{
	switch (entry.kind)
	{
	case AssetKind::Scene:
		instantiateAssetIntoScene(state, core, entry.kind, entry.absolutePath);
		break;
	case AssetKind::Prefab:
		openPrefabForEdit(state, core, entry.absolutePath);
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
//! gone, or no longer under the open project (a project switch). A reset also
//! forgets the navigation history and selection - they belonged to the folder
//! we just left behind.
void ensureCurrentDir(EditorState& state)
{
	std::error_code ec;
	// makeProjectRelative returns "" only for a path OUTSIDE the project root
	// (the root itself maps to "."), so an empty result means a stale dir
	// (project switch) - reset to the root. Also reset when it vanished.
	if (state.assetBrowser.currentDir.empty() ||
		!fs::is_directory(state.assetBrowser.currentDir, ec) ||
		state.project.makeProjectRelative(
			state.assetBrowser.currentDir).empty())
	{
		state.assetBrowser.currentDir = state.project.getRootDirectory();
		state.assetBrowser.backHistory.clear();
		state.assetBrowser.forwardHistory.clear();
		state.assetBrowser.selection.clear();
		state.assetBrowser.selectionAnchor.clear();
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
	if (fs::path(dir) == fs::path(state.assetBrowser.currentDir))
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	ImGui::PushID(dir.c_str());
	// the same folder icon the content grid uses, inline via the merged icon
	// font; bare label when the icon font is absent. The widget draws the whole
	// label in text colour, so afterwards the icon glyph is over-painted in the
	// shared warm folder tone (the grid's folder colour) while the name stays
	// normal text colour - one folder colour everywhere.
	const bool haveIcon = Orkige::editorIconFont() != nullptr;
	std::string label = haveIcon
		? std::string(ICON_FA_FOLDER "  ") + name : name;
	// the label's icon is painted at CursorPos + GetTreeNodeToLabelSpacing()
	// (== text_offset_x) for an unframed node; capture the origin first
	const ImVec2 nodeOrigin = ImGui::GetCursorScreenPos();
	const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
	if (haveIcon)
	{
		const ImVec2 iconAt(nodeOrigin.x + ImGui::GetTreeNodeToLabelSpacing(),
			nodeOrigin.y);
		ImGui::GetWindowDrawList()->AddText(iconAt,
			assetKindColor(AssetKind::Unknown, true), ICON_FA_FOLDER);
	}
	// a dragged asset dropped onto a folder node moves into it
	folderDropTarget(state, dir);
	// a click on the label (not the expand arrow) navigates
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		navigateTo(state.assetBrowser, dir);
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
//! folder as clickable segments - clicking one navigates up
void drawBreadcrumb(EditorState& state)
{
	const fs::path root(state.project.getRootDirectory());
	const fs::path current(state.assetBrowser.currentDir);
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
			navigateTo(state.assetBrowser, chain[i].string());
		}
		// dropping onto a breadcrumb segment moves into that ancestor folder
		folderDropTarget(state, chain[i].string());
		ImGui::PopID();
	}
}

//! the accent colour of a kind's glyph icon (folders share one warm tone)
ImU32 assetKindColor(AssetKind kind, bool isFolder)
{
	if (isFolder)
	{
		return IM_COL32(214, 182, 122, 255);
	}
	switch (kind)
	{
	case AssetKind::Texture:	return IM_COL32(88, 168, 158, 255);
	case AssetKind::Mesh:		return IM_COL32(92, 132, 200, 255);
	case AssetKind::Script:		return IM_COL32(110, 178, 112, 255);
	case AssetKind::Scene:		return IM_COL32(150, 122, 202, 255);
	case AssetKind::Prefab:		return IM_COL32(212, 150, 88, 255);
	case AssetKind::Audio:		return IM_COL32(200, 110, 152, 255);
	case AssetKind::VectorShape:	return IM_COL32(198, 132, 196, 255);
	case AssetKind::Material:	return IM_COL32(176, 148, 96, 255);
	case AssetKind::Unknown:
	default:					return IM_COL32(122, 122, 122, 255);
	}
}

//! the short tag drawn inside a kind's glyph icon
const char* assetKindTag(AssetKind kind, bool isFolder)
{
	if (isFolder)
	{
		return "DIR";
	}
	switch (kind)
	{
	case AssetKind::Texture:	return "IMG";
	case AssetKind::Mesh:		return "MESH";
	case AssetKind::Script:		return "LUA";
	case AssetKind::Scene:		return "SCN";
	case AssetKind::Prefab:		return "PFB";
	case AssetKind::Audio:		return "SND";
	case AssetKind::VectorShape:	return "SHP";
	case AssetKind::Material:	return "MAT";
	case AssetKind::Unknown:
	default:					return "?";
	}
}

//! the icon-font glyph for a kind (Font Awesome 6 solid; see IconsFontAwesome6.h)
const char* assetKindIcon(AssetKind kind, bool isFolder)
{
	if (isFolder)
	{
		return ICON_FA_FOLDER;
	}
	switch (kind)
	{
	case AssetKind::Texture:	return ICON_FA_IMAGE;
	case AssetKind::Mesh:		return ICON_FA_CUBE;
	case AssetKind::Script:		return ICON_FA_FILE_CODE;
	case AssetKind::Scene:		return ICON_FA_FILM;
	case AssetKind::Prefab:		return ICON_FA_CLONE;
	case AssetKind::Audio:		return ICON_FA_MUSIC;
	case AssetKind::VectorShape:	return ICON_FA_SHAPES;
	case AssetKind::Material:	return ICON_FA_PALETTE;
	case AssetKind::Unknown:
	default:					return ICON_FA_FILE;
	}
}

//! apply the sidecar-less "dimmed" convention to a kind colour (half alpha)
ImU32 dimIfNeeded(ImU32 color, bool dimmed)
{
	return dimmed ? ((color & 0x00FFFFFFu) | 0x80000000u) : color;
}

//! draw a self-contained glyph type icon (a rounded rect + a short kind tag)
//! into a rect - the fallback for when the icon font failed to load, so every
//! kind still reads at a glance with no art files
void drawKindGlyph(ImDrawList* drawList, ImVec2 minCorner, ImVec2 maxCorner,
	AssetKind kind, bool isFolder, bool dimmed)
{
	const ImU32 fill = dimIfNeeded(assetKindColor(kind, isFolder), dimmed);
	const float side = std::min(maxCorner.x - minCorner.x,
		maxCorner.y - minCorner.y);
	const float rounding = std::min(6.0f, side * 0.18f);
	drawList->AddRectFilled(minCorner, maxCorner, fill, rounding);
	const char* tag = assetKindTag(kind, isFolder);
	const ImVec2 tagSize = ImGui::CalcTextSize(tag);
	if (tagSize.x <= (maxCorner.x - minCorner.x) &&
		tagSize.y <= (maxCorner.y - minCorner.y))
	{
		const ImVec2 at((minCorner.x + maxCorner.x) * 0.5f - tagSize.x * 0.5f,
			(minCorner.y + maxCorner.y) * 0.5f - tagSize.y * 0.5f);
		drawList->AddText(at, IM_COL32(24, 24, 24, 255), tag);
	}
}

//! draw a kind's icon-font glyph centred in a rect, tinted by the kind colour;
//! folders read a touch larger and warmer, like an established file browser.
//! Falls back to the drawn glyph when the icon font is unavailable.
void drawKindIcon(ImDrawList* drawList, ImVec2 minCorner, ImVec2 maxCorner,
	AssetKind kind, bool isFolder, bool dimmed)
{
	ImFont* iconFont = Orkige::editorIconFont();
	if (!iconFont)
	{
		drawKindGlyph(drawList, minCorner, maxCorner, kind, isFolder, dimmed);
		return;
	}
	const ImU32 color = dimIfNeeded(assetKindColor(kind, isFolder), dimmed);
	const char* glyph = assetKindIcon(kind, isFolder);
	const float side = std::min(maxCorner.x - minCorner.x,
		maxCorner.y - minCorner.y);
	float pixels = side * (isFolder ? 0.94f : 0.82f);
	// never upscale past the atlas raster size - an icon only ever downscales
	// from the crisp standalone font, so big tiles stay sharp instead of soft
	const float rasterPixels = Orkige::editorIconFontRasterPixels();
	if (rasterPixels > 0.0f && pixels > rasterPixels)
	{
		pixels = rasterPixels;
	}
	const ImVec2 size = iconFont->CalcTextSizeA(pixels, FLT_MAX, 0.0f, glyph);
	const ImVec2 at((minCorner.x + maxCorner.x) * 0.5f - size.x * 0.5f,
		(minCorner.y + maxCorner.y) * 0.5f - size.y * 0.5f);
	drawList->AddText(iconFont, pixels, at, color, glyph);
}

//! a light/dark checkerboard clipped to a rect - the classic "this is
//! transparency" backing so alpha textures read against any tile colour
void drawCheckerboard(ImDrawList* drawList, ImVec2 minCorner, ImVec2 maxCorner)
{
	const float cell = 6.0f;
	const ImU32 light = IM_COL32(58, 58, 60, 255);
	const ImU32 dark = IM_COL32(44, 44, 46, 255);
	drawList->AddRectFilled(minCorner, maxCorner, dark);
	drawList->PushClipRect(minCorner, maxCorner, true);
	int row = 0;
	for (float y = minCorner.y; y < maxCorner.y; y += cell, ++row)
	{
		for (float x = minCorner.x + (row & 1 ? cell : 0.0f); x < maxCorner.x;
			x += cell * 2.0f)
		{
			drawList->AddRectFilled(ImVec2(x, y),
				ImVec2(std::min(x + cell, maxCorner.x),
					std::min(y + cell, maxCorner.y)), light);
		}
	}
	drawList->PopClipRect();
}

//! the ONE seam for a content item's visual across grid, list and tree: the
//! real texture thumbnail when one is loaded, otherwise the kind's icon.
//! @param tile when true (grid tiles) a loaded texture thumbnail sits on a
//!        checkerboard backing (so alpha reads against any tile colour) with
//!        rounded corners; every glyph fallback draws bare (tileDrawsBacking),
//!        as do the compact list/tree/rename rows (tile false)
void drawAssetIcon(ImDrawList* drawList, ImVec2 minCorner, ImVec2 maxCorner,
	AssetKind kind, bool isFolder, bool dimmed, ImTextureID thumb,
	bool tile = false)
{
	const float rounding = tile
		? std::min(6.0f, (maxCorner.y - minCorner.y) * 0.14f) : 0.0f;
	const bool hasThumbnail = thumb != 0;
	if (tile && Orkige::tileDrawsBacking(isFolder, hasThumbnail))
	{
		drawCheckerboard(drawList, minCorner, maxCorner);
	}
	if (hasThumbnail)
	{
		if (tile)
		{
			drawList->AddImageRounded(thumb, minCorner, maxCorner,
				ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, rounding);
		}
		else
		{
			drawList->AddImage(thumb, minCorner, maxCorner);
		}
		return;
	}
	drawKindIcon(drawList, minCorner, maxCorner, kind, isFolder, dimmed);
}

//! clip text to a pixel width with a trailing ellipsis (grid labels)
std::string ellipsizeToWidth(std::string const& text, float maxWidth)
{
	if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
	{
		return text;
	}
	std::string clipped = text;
	while (!clipped.empty() &&
		ImGui::CalcTextSize((clipped + "...").c_str()).x > maxWidth)
	{
		clipped.pop_back();
	}
	return clipped + "...";
}

//! the thumbnail texture wanted for an item WITHOUT loading it on the paint
//! path: a fresh cache hit returns the id, otherwise the absolute path is
//! queued (deduplicated) for the budgeted post-draw service and 0 is returned
//! so the caller draws the glyph icon this frame
ImTextureID queuedThumbnail(EditorState& state, AssetBrowserItem const& item)
{
	AssetBrowserState& browser = state.assetBrowser;
	auto cached = browser.thumbnails.find(item.absolutePath);
	if (cached != browser.thumbnails.end() && cached->second.mtime == item.mtime)
	{
		return cached->second.textureId;
	}
	if (std::find(browser.thumbnailQueue.begin(), browser.thumbnailQueue.end(),
		item.absolutePath) == browser.thumbnailQueue.end())
	{
		browser.thumbnailQueue.push_back(item.absolutePath);
	}
	return 0;
}

//! service a few queued thumbnail loads per frame so a big folder never
//! hitches (the decode goes through the main-thread render-facade resource
//! system; a real off-thread decoder is future facade work)
void serviceThumbnailQueue(EditorState& state)
{
	AssetBrowserState& browser = state.assetBrowser;
	int budget = 2;
	while (budget-- > 0 && !browser.thumbnailQueue.empty())
	{
		const std::string absolute = browser.thumbnailQueue.front();
		browser.thumbnailQueue.pop_front();
		assetThumbnailFor(state, absolute);		// synchronous load into cache
	}
}

//! one entry in the content pane's flat, index-addressable grid: a folder or a
//! file, carrying the display name and (search mode) the containing folder
struct GridEntry
{
	bool isFolder = false;
	AssetBrowserItem item;			//!< file data; folders fill path/rel only
	std::string name;				//!< the display name (folder or file name)
	std::string containingFolder;	//!< search mode: project-relative parent
};

//! apply a BeginMultiSelect / EndMultiSelect request list against the
//! relativePath-keyed selection set (user data = the item's grid index)
void applyMultiSelectRequests(ImGuiMultiSelectIO const* io,
	std::vector<GridEntry> const& entries, AssetBrowserState& browser)
{
	for (ImGuiSelectionRequest const& request : io->Requests)
	{
		if (request.Type == ImGuiSelectionRequestType_SetAll)
		{
			browser.selection.clear();
			if (request.Selected)
			{
				for (GridEntry const& entry : entries)
				{
					browser.selection.insert(entry.item.relativePath);
				}
			}
		}
		else if (request.Type == ImGuiSelectionRequestType_SetRange)
		{
			for (int i = static_cast<int>(request.RangeFirstItem);
				i <= static_cast<int>(request.RangeLastItem); ++i)
			{
				if (i < 0 || i >= static_cast<int>(entries.size()))
				{
					continue;
				}
				if (request.Selected)
				{
					browser.selection.insert(entries[i].item.relativePath);
				}
				else
				{
					browser.selection.erase(entries[i].item.relativePath);
				}
			}
		}
	}
}

//! the absolute paths a context/keyboard op should act on: the whole selection
//! when the item is part of a multi-selection, else just the item
std::vector<std::string> assetOpTargets(EditorState& state,
	GridEntry const& entry, bool selected)
{
	AssetBrowserState const& browser = state.assetBrowser;
	if (selected && browser.selection.size() > 1)
	{
		std::vector<std::string> targets;
		const fs::path root(state.project.getRootDirectory());
		for (std::string const& rel : browser.selection)
		{
			targets.push_back((root / rel).string());
		}
		return targets;
	}
	return { entry.item.absolutePath };
}

//! begin an inline rename of an item (seed the buffer with its current name)
void beginItemRename(AssetBrowserState& browser, GridEntry const& entry)
{
	browser.renamingPath = entry.item.relativePath;
	SDL_strlcpy(browser.renameBuffer, entry.name.c_str(),
		sizeof(browser.renameBuffer));
	browser.renameFocusPending = true;
}

//! the shared item context menu (files get the full asset-op set, folders a
//! reduced one) - the item is already selected by the multiselect right-click
void drawItemContextMenu(EditorState& state, Orkige::EditorCore& core,
	GridEntry const& entry, bool selected)
{
	AssetBrowserState& browser = state.assetBrowser;
	if (!entry.isFolder)
	{
		const bool instantiable = entry.item.kind == AssetKind::Mesh ||
			entry.item.kind == AssetKind::Texture ||
			entry.item.kind == AssetKind::Prefab ||
			entry.item.kind == AssetKind::Scene;
		const char* instantiateLabel =
			entry.item.kind == AssetKind::Scene ? "Open" : "Instantiate";
		if (ImGui::MenuItem(instantiateLabel, nullptr, false, instantiable))
		{
			instantiateAssetIntoScene(state, core, entry.item.kind,
				entry.item.absolutePath);
		}
		// a prefab also opens the EDIT stage (the double-click behavior)
		if (entry.item.kind == AssetKind::Prefab &&
			ImGui::MenuItem("Open Prefab", nullptr, false,
				!isPrefabEditActive(state)))
		{
			openPrefabForEdit(state, core, entry.item.absolutePath);
		}
		if (ImGui::MenuItem("Open with default app"))
		{
			openWithDefaultApp(entry.item.absolutePath);
		}
	}
	else if (ImGui::MenuItem("Open"))
	{
		navigateTo(browser, entry.item.absolutePath);
	}
	// reveal in the OS file manager: macOS highlights the item (open -R), other
	// desktops open the containing folder (no highlight - honest)
#ifdef __APPLE__
	if (ImGui::MenuItem("Reveal in Finder"))
	{
		const char* revealArgs[] =
			{ "open", "-R", entry.item.absolutePath.c_str(), nullptr };
		if (SDL_Process* reveal = SDL_CreateProcess(revealArgs, false))
		{
			SDL_DestroyProcess(reveal);
		}
	}
#else
	if (ImGui::MenuItem("Reveal in file manager"))
	{
		const std::string parent =
			fs::path(entry.item.absolutePath).parent_path().string();
		openWithDefaultApp(parent);
	}
#endif
	ImGui::Separator();
	if (ImGui::MenuItem("Rename"))
	{
		beginItemRename(browser, entry);
	}
	if (!entry.isFolder && ImGui::MenuItem("Duplicate"))
	{
		duplicateAssetEntries(state, assetOpTargets(state, entry, selected));
	}
	if (ImGui::MenuItem("Copy Path"))
	{
		ImGui::SetClipboardText(entry.item.relativePath.c_str());
	}
	if (!entry.isFolder && ImGui::MenuItem("Copy Asset ID", nullptr, false,
		entry.item.hasId))
	{
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			ImGui::SetClipboardText(
				database->idForPath(entry.item.relativePath).c_str());
		}
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Delete..."))
	{
		browser.pendingDelete = assetOpTargets(state, entry, selected);
	}
}

//! draw one content-pane cell (grid or list mode): a Selectable that carries
//! the hover/selection visuals + multiselect + keyboard nav, with the
//! thumbnail/glyph and label painted over it, plus the item interactions
//! (double-click, drag source, context menu, inline rename)
void drawContentItem(EditorState& state, Orkige::EditorCore& core,
	std::vector<GridEntry> const& entries, int index, bool listMode,
	bool searchMode, float cellW, float cellH, float thumbH)
{
	GridEntry const& entry = entries[index];
	AssetBrowserState& browser = state.assetBrowser;
	ImGui::PushID(index);
	const bool selected = browser.selection.count(entry.item.relativePath) > 0;
	const bool renaming = entry.item.relativePath == browser.renamingPath;
	// texture thumbnails plus the CPU-rasterized vector kinds (a .oshape fill,
	// a .oanim default-clip pose - both resolved by assetThumbnailFor); .oanim
	// is not its own AssetKind, so it is matched by extension
	const bool wantsThumbnail = !entry.isFolder &&
		(entry.item.kind == AssetKind::Texture ||
			entry.item.kind == AssetKind::VectorShape ||
			lowerExtension(entry.item.absolutePath) == ".oanim");
	const ImTextureID thumb = wantsThumbnail
		? queuedThumbnail(state, entry.item) : 0;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float inset = 4.0f;

	if (renaming)
	{
		// the cell is replaced by its icon + an InputText (Enter commits through
		// renameAssetEntry, Escape/blur cancels) - the Hierarchy's pattern
		ImGui::BeginGroup();
		const ImVec2 iconPos = ImGui::GetCursorScreenPos();
		const float iconSide = listMode ? ImGui::GetTextLineHeight() : thumbH;
		ImGui::Dummy(ImVec2(listMode ? iconSide : cellW, iconSide));
		const ImVec2 iconMin(iconPos.x + inset, iconPos.y + inset);
		const ImVec2 iconMax(iconMin.x + iconSide - inset * 2.0f,
			iconMin.y + iconSide - inset * 2.0f);
		drawAssetIcon(drawList, iconMin, iconMax, entry.item.kind,
			entry.isFolder, entry.item.dimmed, thumb, !listMode);
		if (listMode)
		{
			ImGui::SameLine();
		}
		ImGui::SetNextItemWidth(listMode ? -FLT_MIN : cellW);
		if (browser.renameFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			browser.renameFocusPending = false;
		}
		const bool committed = ImGui::InputText("##rename",
			browser.renameBuffer, sizeof(browser.renameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		if (committed)
		{
			renameAssetEntry(state, entry.item, browser.renameBuffer);
			browser.renamingPath.clear();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
			ImGui::IsItemDeactivated())
		{
			browser.renamingPath.clear();
		}
		ImGui::EndGroup();
		ImGui::PopID();
		return;
	}

	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 cellSize(listMode ? 0.0f : cellW, cellH);
	// draw the selection/hover fill ourselves (rounded, theme accent) instead of
	// the Selectable's square block: capture the theme colours, then suppress the
	// widget's own fill by pushing the header slots transparent
	const ImU32 selFill = ImGui::GetColorU32(ImGuiCol_Header);
	const ImU32 hoverFill = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
	ImGui::PushStyleColor(ImGuiCol_Header, 0);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
	ImGui::SetNextItemSelectionUserData(index);
	ImGui::Selectable("##cell", selected, ImGuiSelectableFlags_None, cellSize);
	ImGui::PopStyleColor(3);
	{
		const bool hovered = ImGui::IsItemHovered();
		const bool isTile = !listMode;
		const ImVec2 hlMin(pos.x, pos.y);
		const ImVec2 hlMax(pos.x + cellW, pos.y + cellH);
		const float rounding = ImGui::GetStyle().FrameRounding;
		if (selected && !Orkige::tileSelectionDrawsFill(isTile))
		{
			// grid tiles draw NO selection fill and no extra border - a filled
			// box hid the thumbnail's transparency checkerboard and the kind
			// glyph, and the focus ring on the selectable already outlines the
			// selected tile. A faint hover fill keeps motion feedback.
			if (hovered)
			{
				drawList->AddRectFilled(hlMin, hlMax, hoverFill, rounding);
			}
		}
		else if (selected || hovered)
		{
			// list/tree rows (no thumbnail area to obscure) keep the classic
			// filled highlight
			drawList->AddRectFilled(hlMin, hlMax,
				selected ? selFill : hoverFill, rounding);
		}
	}

	// interactions on the cell
	if (ImGui::IsItemHovered() &&
		ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		if (entry.isFolder)
		{
			navigateTo(browser, entry.item.absolutePath);
		}
		else
		{
			openAssetEntry(state, core, entry.item);
		}
	}
	// a folder tile is a MOVE target (drop assets into it); files are drag
	// sources
	if (entry.isFolder)
	{
		folderDropTarget(state, entry.item.absolutePath);
	}
	else if (ImGui::BeginDragDropSource(
		ImGuiDragDropFlags_SourceNoDisableHover))
	{
		// dragging within a multi-selection carries the WHOLE selection as a
		// newline-joined path list; a lone item carries the single struct
		// payload (ImGui holds one payload type per drag, so the type is chosen
		// by selection size - the accept sites take either)
		if (selected && browser.selection.size() > 1)
		{
			std::string joined;
			const fs::path rootPath(state.project.getRootDirectory());
			for (std::string const& rel : browser.selection)
			{
				if (!joined.empty())
				{
					joined += '\n';
				}
				joined += (rootPath / rel).string();
			}
			ImGui::SetDragDropPayload(ASSET_DND_PAYLOAD_MULTI, joined.data(),
				joined.size());
			ImGui::Text("%d assets",
				static_cast<int>(browser.selection.size()));
		}
		else
		{
			AssetDragDropPayload payload;
			payload.kind = entry.item.kind;
			SDL_strlcpy(payload.path, entry.item.absolutePath.c_str(),
				sizeof(payload.path));
			ImGui::SetDragDropPayload(ASSET_DND_PAYLOAD, &payload,
				sizeof(payload));
			ImGui::TextUnformatted(entry.name.c_str());
		}
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginPopupContextItem("##itemmenu"))
	{
		drawItemContextMenu(state, core, entry, selected);
		ImGui::EndPopup();
	}
	// paint the icon + label over the item rect
	const ImU32 textColor = ImGui::GetColorU32(
		entry.item.dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
	bool nameTruncated = false;
	if (listMode)
	{
		const float iconSide = ImGui::GetTextLineHeight();
		const ImVec2 iconMin(pos.x, pos.y);
		const ImVec2 iconMax(pos.x + iconSide, pos.y + iconSide);
		drawAssetIcon(drawList, iconMin, iconMax, entry.item.kind,
			entry.isFolder, entry.item.dimmed, thumb);
		// right-aligned meta: the kind (and, in search mode, the folder)
		std::string meta = entry.isFolder ? "folder"
			: assetKindLabel(entry.item.kind);
		if (searchMode && !entry.containingFolder.empty())
		{
			meta += "  " + entry.containingFolder;
		}
		const float metaWidth = ImGui::CalcTextSize(meta.c_str()).x;
		const float rightEdge = pos.x + ImGui::GetContentRegionAvail().x;
		// the name clips to whatever the meta leaves free (ellipsis + tooltip)
		const float nameLeft = pos.x + iconSide + 6.0f;
		const float nameRoom = (rightEdge - metaWidth - 8.0f) - nameLeft;
		const std::string label = ellipsizeToWidth(entry.name, nameRoom);
		nameTruncated = label != entry.name;
		drawList->AddText(ImVec2(nameLeft, pos.y), textColor, label.c_str());
		drawList->AddText(ImVec2(rightEdge - metaWidth, pos.y),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), meta.c_str());
	}
	else
	{
		const float squareSide = std::min(cellW, thumbH) - inset * 2.0f;
		const ImVec2 center(pos.x + cellW * 0.5f, pos.y + thumbH * 0.5f);
		const ImVec2 squareMin(center.x - squareSide * 0.5f,
			center.y - squareSide * 0.5f);
		const ImVec2 squareMax(center.x + squareSide * 0.5f,
			center.y + squareSide * 0.5f);
		drawAssetIcon(drawList, squareMin, squareMax, entry.item.kind,
			entry.isFolder, entry.item.dimmed, thumb, true);
		const std::string label = ellipsizeToWidth(entry.name,
			cellW - inset * 2.0f);
		nameTruncated = label != entry.name;
		const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
		drawList->AddText(ImVec2(pos.x + (cellW - labelWidth) * 0.5f,
			pos.y + thumbH + inset), textColor, label.c_str());
	}

	// one tooltip: the full name when it was clipped, plus the sidecar-less note
	if (nameTruncated || entry.item.dimmed)
	{
		std::string tip = entry.name;
		if (entry.item.dimmed)
		{
			tip += "\n(no asset id yet - sidecar-less)";
		}
		ImGui::SetItemTooltip("%s", tip.c_str());
	}
	ImGui::PopID();
}

} // namespace

// The Assets panel (v3): a content browser over the open project. The LEFT
// pane is a folder tree (ImGui TreeNodes rooted at the project root); the RIGHT
// pane is a content grid that scales continuously from large thumbnail tiles
// down to compact list rows via a size slider. A toolbar carries back/forward
// history, a clickable breadcrumb, a recursive search box, per-kind filter
// chips and the size slider; a Create menu mints New Folder/Script/Scene.
// Textures preview with real thumbnails (loaded off the paint path through a
// budgeted per-frame queue), every other kind draws a colour-tinted icon-font
// glyph. Multi-selection (ctrl/shift/box-select/keyboard, keyed by
// project-relative path so it survives re-enumeration) drives the asset ops:
// each item is a drag source ("ORKIGE_ASSET"); the context menu offers
// Instantiate/Open, Reveal, Rename (F2), Duplicate (Cmd/Ctrl+D), Copy Path,
// Copy Asset ID and Delete; sidecar-less assets render dimmed. Filesystem ops
// are honestly NOT undoable (the stable id survives via the sidecar and scene
// references self-heal on load), so only Delete confirms. Project-only:
// without an open project the panel shows an empty-state hint.
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
		state.assetBrowser.focused = false;
		ImGui::End();
		return;
	}

	AssetBrowserState& browser = state.assetBrowser;
	ensureCurrentDir(state);
	// panel-wide keyboard focus (this window or any of its children) gates the
	// local shortcut routing, mirroring scenePanelFocused / hierarchyFocused
	browser.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	const std::string root = state.project.getRootDirectory();
	ImGuiStyle& style = ImGui::GetStyle();

	// --- toolbar row 1: back/forward history, Create, breadcrumb -----------
	ImGui::BeginDisabled(browser.backHistory.empty());
	if (ImGui::ArrowButton("##back", ImGuiDir_Left))
	{
		navigateBack(browser);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(browser.forwardHistory.empty());
	if (ImGui::ArrowButton("##forward", ImGuiDir_Right))
	{
		navigateForward(browser);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Create"))
	{
		ImGui::OpenPopup("##assetCreate");
	}
	if (ImGui::BeginPopup("##assetCreate"))
	{
		if (ImGui::MenuItem("New Folder"))
		{
			createFolderAndReveal(state);
		}
		if (ImGui::MenuItem("New Script"))
		{
			createScriptAndReveal(state);
		}
		if (ImGui::MenuItem("New Scene"))
		{
			createSceneAndReveal(state);
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("|");
	ImGui::SameLine();
	drawBreadcrumb(state);

	// --- toolbar row 2: search + type-filter funnel + recursive ------------
	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputTextWithHint("##assetSearch", "Search", browser.searchText,
		sizeof(browser.searchText));
	ImGui::SameLine();
	// the type filter lives behind a funnel button: it opens a checklist of
	// asset kinds, each toggling its AssetKind bit in the mask (0 = every kind).
	// A small badge on the button shows how many kinds are currently selected.
	const struct { AssetKind kind; const char* label; } kindFilters[] = {
		{ AssetKind::Texture, "Textures" }, { AssetKind::Mesh, "Meshes" },
		{ AssetKind::Audio, "Audio" }, { AssetKind::Script, "Scripts" },
		{ AssetKind::Prefab, "Prefabs" }, { AssetKind::Scene, "Scenes" } };
	int activeFilters = 0;
	for (auto const& f : kindFilters)
	{
		if (browser.kindFilterMask & (1u << static_cast<unsigned int>(f.kind)))
		{
			++activeFilters;
		}
	}
	const bool anyFilter = browser.kindFilterMask != 0;
	if (anyFilter)
	{
		ImGui::PushStyleColor(ImGuiCol_Button,
			ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	const ImVec2 funnelMin = ImGui::GetCursorScreenPos();
	const char* funnelLabel =
		Orkige::editorIconFont() ? ICON_FA_FILTER : "Filter";
	if (ImGui::Button(funnelLabel))
	{
		ImGui::OpenPopup("##kindFilter");
	}
	if (anyFilter)
	{
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip("filter by asset type");
	// the active-count badge: a small accent disc on the button's top-right
	if (anyFilter)
	{
		const ImVec2 funnelMax = ImGui::GetItemRectMax();
		const std::string count = std::to_string(activeFilters);
		const ImVec2 textSize = ImGui::CalcTextSize(count.c_str());
		const float radius = std::max(textSize.x, textSize.y) * 0.5f + 2.0f;
		const ImVec2 center(funnelMax.x - 1.0f, funnelMin.y + 1.0f);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddCircleFilled(center, radius,
			ImGui::GetColorU32(ImGuiCol_CheckMark));
		drawList->AddText(ImVec2(center.x - textSize.x * 0.5f,
			center.y - textSize.y * 0.5f), IM_COL32(255, 255, 255, 255),
			count.c_str());
	}
	if (ImGui::BeginPopup("##kindFilter"))
	{
		for (auto const& f : kindFilters)
		{
			const unsigned int bit = 1u << static_cast<unsigned int>(f.kind);
			bool on = (browser.kindFilterMask & bit) != 0;
			if (Orkige::compactCheckbox(f.label, &on))
			{
				if (on)
				{
					browser.kindFilterMask |= bit;
				}
				else
				{
					browser.kindFilterMask &= ~bit;
				}
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Clear", nullptr, false, anyFilter))
		{
			browser.kindFilterMask = 0;
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	Orkige::compactCheckbox("Recursive", &browser.searchRecursive);
	ImGui::Separator();

	// both panes and the divider share the height above the one-line footer
	const float footerHeight =
		ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;

	// --- two panes: folder tree (left) + content grid (right) --------------
	// a SINGLE vertical hairline separates the tree from the content pane (no
	// child-frame box); the tree still resizes by dragging its right edge
	const float treeWidth = std::min(220.0f,
		ImGui::GetContentRegionAvail().x * 0.35f);
	const float panesTop = ImGui::GetCursorScreenPos().y;
	const float paneHeight = ImGui::GetContentRegionAvail().y - footerHeight;
	if (ImGui::BeginChild("##assetTree", ImVec2(treeWidth, paneHeight),
		ImGuiChildFlags_ResizeX))
	{
		drawFolderTree(state, root, 0);
	}
	ImGui::EndChild();
	ImGui::SameLine();
	// the divider sits in the item-spacing gap between the two panes
	const ImVec2 contentTop = ImGui::GetCursorScreenPos();
	const float dividerX = contentTop.x - style.ItemSpacing.x * 0.5f;
	ImGui::GetWindowDrawList()->AddLine(ImVec2(dividerX, panesTop),
		ImVec2(dividerX, panesTop + paneHeight),
		ImGui::GetColorU32(ImGuiCol_Separator));

	// enumerate the content pane: search mode (a recursive/flat name search)
	// or folder mode (the current folder's subfolders + files); the kind mask
	// applies in both
	const bool searchMode = browser.searchText[0] != '\0';
	std::vector<GridEntry> entries;
	int folderCount = 0;
	int fileCount = 0;
	int filteredOut = 0;
	if (searchMode)
	{
		for (AssetBrowserItem const& found : searchAssets(state.project,
			browser.currentDir, browser.searchText, browser.searchRecursive,
			browser.kindFilterMask))
		{
			GridEntry entry;
			entry.isFolder = found.isFolder;
			entry.item = found;
			entry.name = fs::path(found.absolutePath).filename().string();
			entry.containingFolder =
				fs::path(found.relativePath).parent_path().string();
			entries.push_back(std::move(entry));
			if (found.isFolder)
			{
				++folderCount;
			}
			else
			{
				++fileCount;
			}
		}
	}
	else
	{
		const AssetFolderListing listing =
			enumerateAssetFolder(state.project, browser.currentDir);
		for (std::string const& sub : listing.subfolders)
		{
			GridEntry entry;
			entry.isFolder = true;
			entry.item.absolutePath = sub;
			entry.item.relativePath = state.project.makeProjectRelative(sub);
			entry.name = fs::path(sub).filename().string();
			entries.push_back(std::move(entry));
			++folderCount;
		}
		for (AssetBrowserItem const& file : listing.files)
		{
			const unsigned int bit =
				1u << static_cast<unsigned int>(file.kind);
			if (browser.kindFilterMask != 0 &&
				(browser.kindFilterMask & bit) == 0)
			{
				++filteredOut;
				continue;
			}
			GridEntry entry;
			entry.item = file;
			entry.name = fs::path(file.absolutePath).filename().string();
			entries.push_back(std::move(entry));
			++fileCount;
		}
		// selection is keyed by relativePath; drop entries no longer present
		// (folder mode only - a search result may live in another folder)
		std::set<std::string> present;
		for (GridEntry const& entry : entries)
		{
			present.insert(entry.item.relativePath);
		}
		pruneAssetSelection(browser, present);
	}

	const bool listMode =
		browser.thumbnailSize < AssetBrowserState::LIST_THRESHOLD;
	// the content grid sits above a one-line status footer
	if (ImGui::BeginChild("##assetContents", ImVec2(0.0f, -footerHeight)))
	{
		const int count = static_cast<int>(entries.size());
		if (count == 0)
		{
			ImGui::TextDisabled(searchMode
				? "no assets match the search"
				: "empty folder - use Create, or drop files onto the window "
					"to import");
		}
		else
		{
			// cell metrics: grid tiles vs. compact list rows off the slider
			const float avail = ImGui::GetContentRegionAvail().x;
			const float lineHeight = ImGui::GetTextLineHeight();
			float cellW = 0.0f;
			float cellH = 0.0f;
			float thumbH = 0.0f;
			int columns = 1;
			if (listMode)
			{
				columns = 1;
				cellW = avail;
				thumbH = lineHeight;
				cellH = lineHeight;
			}
			else
			{
				cellW = browser.thumbnailSize;
				thumbH = browser.thumbnailSize;
				cellH = browser.thumbnailSize + lineHeight + 6.0f;
				columns = std::max(1, static_cast<int>(
					avail / (cellW + style.ItemSpacing.x)));
			}
			// multi-select over the whole grid (ctrl/shift/box/keyboard); the
			// user data is the item's index into this frame's entry vector
			ImGuiMultiSelectIO* msio = ImGui::BeginMultiSelect(
				ImGuiMultiSelectFlags_ClearOnEscape |
				ImGuiMultiSelectFlags_BoxSelect2d,
				static_cast<int>(browser.selection.size()), count);
			applyMultiSelectRequests(msio, entries, browser);
			const int rows = (count + columns - 1) / columns;
			const float rowStep = cellH + style.ItemSpacing.y;
			ImGuiListClipper clipper;
			clipper.Begin(rows, rowStep);
			// keep the range-source item's row unclipped so shift-select spans
			if (msio->RangeSrcItem != -1)
			{
				clipper.IncludeItemByIndex(
					static_cast<int>(msio->RangeSrcItem) / columns);
			}
			while (clipper.Step())
			{
				for (int line = clipper.DisplayStart; line < clipper.DisplayEnd;
					++line)
				{
					for (int col = 0; col < columns; ++col)
					{
						const int index = line * columns + col;
						if (index >= count)
						{
							break;
						}
						if (col > 0)
						{
							ImGui::SameLine();
						}
						drawContentItem(state, core, entries, index, listMode,
							searchMode, cellW, cellH, thumbH);
					}
				}
			}
			msio = ImGui::EndMultiSelect();
			applyMultiSelectRequests(msio, entries, browser);
			// the focused item becomes the range anchor / keyboard target
			if (msio->NavIdItem != -1)
			{
				const int nav = static_cast<int>(msio->NavIdItem);
				if (nav >= 0 && nav < count)
				{
					browser.selectionAnchor = entries[nav].item.relativePath;
				}
			}
		}
		// empty-space right-click: the Create menu (no drop-target Paste in v3)
		if (ImGui::BeginPopupContextWindow("##assetEmptyMenu",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("New Folder"))
			{
				createFolderAndReveal(state);
			}
			if (ImGui::MenuItem("New Script"))
			{
				createScriptAndReveal(state);
			}
			if (ImGui::MenuItem("New Scene"))
			{
				createSceneAndReveal(state);
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();

	// --- status footer -----------------------------------------------------
	ImGui::Separator();
	std::string status;
	if (searchMode)
	{
		status = std::to_string(folderCount + fileCount) + " results";
	}
	else
	{
		const int totalFiles = fileCount + filteredOut;
		status = std::to_string(folderCount) + " folders, " +
			std::to_string(totalFiles) + " files";
		if (filteredOut > 0)
		{
			status += " (" + std::to_string(fileCount) + " shown)";
		}
	}
	if (!browser.selection.empty())
	{
		status += ", " + std::to_string(browser.selection.size()) + " selected";
	}
	// a transient notice (a failed create) overrides the count line for a few
	// seconds so a silent filesystem failure becomes visible
	if (!browser.statusMessage.empty() &&
		ImGui::GetTime() < browser.statusMessageExpiry)
	{
		ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram),
			"%s", browser.statusMessage.c_str());
	}
	else
	{
		browser.statusMessage.clear();
		ImGui::TextUnformatted(status.c_str());
	}
	// right-aligned footer controls: the current-folder path, then a thin,
	// unlabeled thumbnail-size slider tucked into the far corner
	std::string here = state.project.makeProjectRelative(browser.currentDir);
	if (here.empty() || here == ".")
	{
		here = "/";
	}
	const float hereWidth = ImGui::CalcTextSize(here.c_str()).x;
	const float sliderWidth = 96.0f;
	const float contentWidth = ImGui::GetWindowContentRegionMax().x -
		ImGui::GetWindowContentRegionMin().x;
	ImGui::SameLine(std::max(0.0f, contentWidth - sliderWidth -
		style.ItemSpacing.x - hereWidth));
	ImGui::TextDisabled("%s", here.c_str());
	ImGui::SameLine(std::max(0.0f, contentWidth - sliderWidth));
	ImGui::SetNextItemWidth(sliderWidth);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
		ImVec2(style.FramePadding.x, 2.0f));	// keep the footer slider thin
	ImGui::SliderFloat("##thumbsize", &browser.thumbnailSize,
		AssetBrowserState::THUMBNAIL_MIN, AssetBrowserState::THUMBNAIL_MAX, "");
	ImGui::PopStyleVar();
	ImGui::SetItemTooltip("thumbnail size");
	if (ImGui::IsItemDeactivatedAfterEdit() && gRecordRecents && gViewSettings)
	{
		// persist the zoom (interactive runs only, like the other view prefs)
		gViewSettings->assetThumbnailSize = browser.thumbnailSize;
		gViewSettings->save();
	}

	// --- panel-local keyboard shortcuts (only while the panel has focus, and
	// never mid-rename) - the Hierarchy/global handlers stand down through the
	// browser.focused guard in EditorShortcuts.cpp so keys go to one panel ----
	if (browser.focused && !ImGui::GetIO().WantTextInput &&
		browser.renamingPath.empty())
	{
		ImGuiIO& io = ImGui::GetIO();
		const bool commandDown = io.KeySuper || io.KeyCtrl;
		const GridEntry* anchor = nullptr;
		for (GridEntry const& entry : entries)
		{
			if (entry.item.relativePath == browser.selectionAnchor)
			{
				anchor = &entry;
				break;
			}
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
			ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
		{
			if (anchor)
			{
				if (anchor->isFolder)
				{
					navigateTo(browser, anchor->item.absolutePath);
				}
				else
				{
					openAssetEntry(state, core, anchor->item);
				}
			}
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
		{
			if (anchor)
			{
				beginItemRename(browser, *anchor);
			}
		}
		else if (commandDown && ImGui::IsKeyPressed(ImGuiKey_D, false))
		{
			if (anchor)
			{
				duplicateAssetEntries(state,
					assetOpTargets(state, *anchor, true));
			}
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
			ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
		{
			// fill the confirm modal from the selection (or the anchor alone)
			std::vector<std::string> targets;
			const fs::path rootPath(root);
			for (std::string const& rel : browser.selection)
			{
				targets.push_back((rootPath / rel).string());
			}
			if (targets.empty() && anchor)
			{
				targets.push_back(anchor->item.absolutePath);
			}
			if (!targets.empty())
			{
				browser.pendingDelete = targets;
			}
		}
	}

	// delete-confirm modal: the only destructive asset op gets a confirmation
	// (rename/move/duplicate are non-destructive and land without one)
	if (!state.assetBrowser.pendingDelete.empty())
	{
		ImGui::OpenPopup("Delete Assets?");
	}
	if (ImGui::BeginPopupModal("Delete Assets?", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		const std::vector<std::string>& pending =
			state.assetBrowser.pendingDelete;
		ImGui::Text("Delete %d item(s)? This cannot be undone.",
			static_cast<int>(pending.size()));
		for (std::size_t i = 0; i < pending.size() && i < 6; ++i)
		{
			ImGui::BulletText("%s",
				fs::path(pending[i]).filename().string().c_str());
		}
		if (pending.size() > 6)
		{
			ImGui::TextDisabled("  ... and %d more",
				static_cast<int>(pending.size() - 6));
		}
		ImGui::Separator();
		if (ImGui::Button("Delete"))
		{
			deleteAssetEntries(state, pending);
			state.assetBrowser.pendingDelete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			state.assetBrowser.pendingDelete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::End();

	// service a budgeted slice of the thumbnail load queue off the paint path
	serviceThumbnailQueue(state);
}
