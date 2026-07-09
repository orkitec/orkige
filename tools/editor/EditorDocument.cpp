// EditorDocument.cpp - scene/project document operations: New/Open/Save
// scene, the project open/close/create (with the dedicated
// resource group) and the mesh import (asset database wiring included).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "MeshImport.h"

#include <core_game/PrefabSerializer.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_script/ScriptRuntime.h>
#include <engine_render/RenderSystem.h>

#include <cstdlib>
#include <filesystem>

// File > New Scene: clear all GameObjects - removing the components tears
// down their scene nodes (TransformComponent::onRemove wipes via NodeUtil)
void newScene(EditorState& state, Orkige::EditorCore& core)
{
	core.getGameObjectManager().clear();
	core.resetForScene();
	state.currentScenePath.clear();
}

bool saveSceneToPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	if (!Orkige::SceneSerializer::saveScene(path, gameObjectManager))
	{
		SDL_Log("orkige_editor: saving scene '%s' failed", path.c_str());
		return false;
	}
	SDL_Log("orkige_editor: scene saved to '%s' (%zu GameObjects)",
		path.c_str(), gameObjectManager.getGameObjects().size());
	state.currentScenePath = path;
	core.clearSceneDirty();
	recordRecentScene(path);
	return true;
}

bool openSceneFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	// loadScene replaces the current world (clears the manager first); the
	// undo history refers to the old world, so it goes too
	core.resetForScene();
	if (!Orkige::SceneSerializer::loadScene(path, gameObjectManager))
	{
		SDL_Log("orkige_editor: opening scene '%s' failed", path.c_str());
		return false;
	}
	applyUnlitFixToLoadedModels(core);
	SDL_Log("orkige_editor: scene opened from '%s' (%zu GameObjects)",
		path.c_str(), gameObjectManager.getGameObjects().size());
	state.currentScenePath = path;
	recordRecentScene(path);
	return true;
}

//--- project handling ("open a project, not a scene") ----------

// A loaded project registers its assets/ and scenes/ directories as engine
// resource locations in the DEDICATED "OrkigeProject" group (the player's
// --project mode registers the identical set). The dedicated group is what
// makes switching projects clean: destroyResourceGroup unloads and
// unindexes every resource that came from the outgoing project, so
// name-cached meshes never leak into the next one.

namespace
{
//! tear down the previous project's resource group (call with the world
//! already cleared - entities referencing those meshes must be gone)
void unregisterProjectResources()
{
	Orkige::RenderSystem::get()->destroyResourceGroup(
		Orkige::Project::RESOURCE_GROUP_NAME);
}

//! register the project's assets/ (recursively, per-subfolder) and scenes/
//! (flat) in the project group; missing directories are skipped honestly
void registerProjectResources(Orkige::Project const& project)
{
	// assets/ and each subfolder as their own flat location so a subfolder
	// asset resolves by bare name on both flavors (see the seam's doc)
	registerProjectAssetLocations(project.getAssetsDirectory());
	// scenes/ stays a single flat location
	const std::string scenesDir = project.getScenesDirectory();
	std::error_code ignored;
	if (std::filesystem::is_directory(scenesDir, ignored))
	{
		Orkige::RenderSystem::get()->addResourceLocation(scenesDir,
			Orkige::RenderSystem::LT_FILESYSTEM,
			Orkige::Project::RESOURCE_GROUP_NAME, false);
	}
	else
	{
		SDL_Log("orkige_editor: project directory '%s' does not exist - "
			"not registered", scenesDir.c_str());
	}
}

} // namespace

void registerProjectAssetLocations(std::string const& assetsDirectory)
{
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	std::error_code ec;
	if (!render || !std::filesystem::is_directory(assetsDirectory, ec))
	{
		return;
	}
	// each directory a FLAT location: a file resolves by its bare name from its
	// own directory. Remove-then-add per directory keeps this idempotent (the
	// re-index after a move/create) without unloading already-loaded resources.
	const auto registerFlat = [&](std::string const& directory)
	{
		render->removeResourceLocation(directory,
			Orkige::Project::RESOURCE_GROUP_NAME);
		render->addResourceLocation(directory, Orkige::RenderSystem::LT_FILESYSTEM,
			Orkige::Project::RESOURCE_GROUP_NAME, false);
	};
	registerFlat(assetsDirectory);
	for (std::filesystem::recursive_directory_iterator
		it(assetsDirectory, ec), end; !ec && it != end; it.increment(ec))
	{
		if (it->is_directory(ec))
		{
			registerFlat(it->path().string());
		}
	}
}

// File > Open Project... / Open Recent Project / the scripted project test:
// accepts a project directory or a .orkproj manifest. The current world is
// cleared FIRST (its entities may reference the outgoing project's
// resources), then the old project group is torn down, the new project's
// roots registered and its main scene opened (a project without a main
// scene starts on an empty untitled scene). On a failed load nothing
// changes - the previously open project stays open.
bool openProjectFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	Orkige::Project project;
	std::string error;
	if (!project.load(path, &error))
	{
		SDL_Log("orkige_editor: opening project failed - %s", error.c_str());
		return false;
	}
	newScene(state, core);
	unregisterProjectResources();
	// the editor is the authoring tool: import the project's assets (mint
	// sidecar .orkmeta ids for sidecar-less assets, drop orphaned sidecars) -
	// runtimes only READ the sidecars (Project::load)
	project.importAssets();
	state.project = project;
	registerProjectResources(state.project);
	recordRecentProject(state.project.getRootDirectory());
	// scripts stay dormant in the editor (edit mode never ticks components;
	// the spawned player runs them), but the script console resolves project
	// scripts the same way the runtimes do
	Orkige::ScriptRuntime::getSingleton().setScriptSearchRoot(
		state.project.getRootDirectory());
	// the collision-layer config feeds the RigidBody Inspector's layer dropdown
	// (the editor never simulates; this is purely for authoring)
	core.loadPhysicsLayers(state.project);
	SDL_Log("orkige_editor: project '%s' opened (root '%s', %zu scenes)",
		state.project.getName().c_str(),
		state.project.getRootDirectory().c_str(),
		state.project.listScenes().size());
	const std::string mainScenePath = state.project.getMainScenePath();
	std::error_code ignored;
	if (!mainScenePath.empty() &&
		std::filesystem::exists(mainScenePath, ignored))
	{
		openSceneFromPath(state, core, mainScenePath);
	}
	else
	{
		SDL_Log("orkige_editor: project '%s' has no main scene yet - "
			"starting on an empty scene",
			state.project.getName().c_str());
	}
	return true;
}

// File > Close Project: back to loose-scene mode (empty untitled scene,
// project resources torn down)
void closeProject(EditorState& state, Orkige::EditorCore& core)
{
	if (!state.project.isLoaded())
	{
		return;
	}
	SDL_Log("orkige_editor: project '%s' closed",
		state.project.getName().c_str());
	newScene(state, core);
	unregisterProjectResources();
	state.project.close();
	Orkige::ScriptRuntime::getSingleton().setScriptSearchRoot("");
	// back to the built-in default layers (loose-scene mode)
	core.resetPhysicsLayers();
}

// File > New Project...: create the skeleton (project name = the picked
// folder's name) in the chosen folder, open it, and save the initial empty
// main scene the manifest points at - a fresh project is instantly playable
bool newProjectAtPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& folder)
{
	Orkige::Project created;
	std::string error;
	if (!Orkige::Project::create(folder, "", created, &error))
	{
		SDL_Log("orkige_editor: creating project failed - %s", error.c_str());
		return false;
	}
	if (!openProjectFromPath(state, core, created.getRootDirectory()))
	{
		return false;
	}
	const std::string mainScenePath = state.project.getMainScenePath();
	std::error_code ignored;
	if (!mainScenePath.empty() &&
		!std::filesystem::exists(mainScenePath, ignored))
	{
		saveSceneToPath(state, core, mainScenePath);
	}
	return true;
}

// File > Import Mesh... / SDL_EVENT_DROP_FILE: copy the mesh file into the
// scene's media folder (MeshImport.h documents the destination rule:
// "<sceneDir>/media" for a saved scene, the project import dir -
// samples/scenes/media, ORKIGE_EDITOR_IMPORT_DIR overrides - while unsaved),
// register that folder as an Ogre resource location (once per run) and create
// a GameObject with a ModelComponent at the origin through the undoable
// CreateObjectCommand (undo removes the object; the copied file stays). Any
// failure - unsupported extension, copy error, mesh load error - logs to the
// Console and leaves the scene untouched (CreateObjectCommand tears down a
// half-created object itself).
//! the directory File > Import Mesh copies into: an open project ROOTS the
//! import (everything lands in its assets/), otherwise the historical
//! loose-scene rule applies (<sceneDir>/media for a saved scene; the sample
//! import dir - or the ORKIGE_EDITOR_IMPORT_DIR test override - while
//! unsaved)
std::string meshImportDestination(EditorState const& state)
{
	if (state.project.isLoaded())
	{
		return state.project.getAssetsDirectory();
	}
	const char* importDirOverride = std::getenv("ORKIGE_EDITOR_IMPORT_DIR");
	const std::string projectImportDir = importDirOverride
		? importDirOverride : ORKIGE_EDITOR_SCENE_DIR "/media";
	return Orkige::meshImportDestinationDir(state.currentScenePath,
		projectImportDir);
}

// The copy+register+sidecar-mint MIDDLE of the old importMeshFromPath, made
// generic so any asset (texture/script/prefab/scene from a Finder drop or the
// browser) rides the same path meshes always did - the copied file lands where
// the resource groups (and SpriteComponent::loadSprite) resolve it, and gets
// its stable .orkmeta id in a project. The mesh-instantiate tail stays below,
// mesh-only.
std::string importAssetFile(EditorState& state, std::string const& sourcePath,
	std::string* error)
{
	const std::string destDir = meshImportDestination(state);
	std::string localError;
	const std::string destPath =
		Orkige::importMeshFileToDir(sourcePath, destDir, &localError);
	if (destPath.empty())
	{
		SDL_Log("orkige_editor: import of '%s' failed - %s",
			sourcePath.c_str(), localError.c_str());
		if (error)
		{
			*error = localError;
		}
		return "";
	}
	if (state.project.isLoaded())
	{
		// re-register assets/ + each subfolder (flat, per-directory) so the
		// just-copied file is findable by bare filename right away - and by bare
		// name even in a subfolder (a recursive location would miss those on the
		// next backend); idempotent by the seam's remove-then-add contract
		registerProjectAssetLocations(destDir);
		// editor-side asset creation mints the stable id right away, so a
		// component referencing it serializes the id with the scene
		if (optr<Orkige::AssetDatabase> const& assetDatabase =
			state.project.getAssetDatabase())
		{
			assetDatabase->importAsset(destPath);
		}
	}
	else if (state.importResourceDirs.insert(destDir).second)
	{
		// indexes the directory contents immediately - the file is loadable
		// by bare filename right after this
		Orkige::RenderSystem::get()->addResourceLocation(destDir);
	}
	return destPath;
}

bool importMeshFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& sourcePath)
{
	if (!Orkige::isSupportedMeshFile(sourcePath))
	{
		SDL_Log("orkige_editor: import refused - '%s' is not a supported mesh "
			"file (.glb/.gltf/.obj/.fbx/.dae/.stl/.ply/.3ds/.mesh)",
			sourcePath.c_str());
		return false;
	}
	// copy into the project (+ sidecar mint + resource-location refresh)
	const std::string destPath = importAssetFile(state, sourcePath);
	if (destPath.empty())
	{
		return false;
	}
	const std::string meshName =
		std::filesystem::path(destPath).filename().string();
	std::string objectId = Orkige::meshImportObjectBaseName(destPath);
	if (objectId.empty())
	{
		objectId = "ImportedMesh";
	}
	if (core.getGameObjectManager().objectExists(objectId))
	{
		int suffix = 2;
		std::string candidate;
		do
		{
			candidate = objectId + " " + std::to_string(suffix);
			++suffix;
		} while (core.getGameObjectManager().objectExists(candidate));
		objectId = candidate;
	}
	// undoable create at the origin; execute selects the new object
	if (!core.executeCommand(Orkige::onew(new Orkige::CreateObjectCommand(
		objectId, meshName, Orkige::Vec3::ZERO))))
	{
		SDL_Log("orkige_editor: import of '%s' failed - mesh '%s' did not "
			"load (see the log above)", sourcePath.c_str(), meshName.c_str());
		return false;
	}
	SDL_Log("orkige_editor: imported '%s' -> '%s' as GameObject '%s'",
		sourcePath.c_str(), destPath.c_str(), objectId.c_str());
	return true;
}

// GameObject > Create Prefab (also the Hierarchy context menu): write the
// primary selection's subtree as a .oprefab under the open project's assets/
// (prefabs are project assets - the file gets its stable .orkmeta id through
// the project's AssetDatabase right away) and convert the live subtree into a
// prefab INSTANCE through the undoable MakePrefabCommand. The destination is
// deterministic - "<assets>/<rootId>.oprefab" - instead of a save dialog:
// prefabs must live inside assets/ anyway, and the deterministic path keeps
// the flow scriptable for the automated tests. Creating a prefab where an
// UNRELATED .oprefab already sits is refused (rename the object first);
// running it on an existing instance ROOT re-makes (overwrites) that
// instance's own prefab file - the v1 "edit a prefab" loop (the structural
// overrides reset: the file now IS the live subtree).
bool createPrefabFromSelection(EditorState& state, Orkige::EditorCore& core)
{
	if (!core.hasSelection())
	{
		SDL_Log("orkige_editor: Create Prefab needs a selected object");
		return false;
	}
	if (!state.project.isLoaded())
	{
		SDL_Log("orkige_editor: Create Prefab needs an open project - "
			"prefabs live in the project's assets/");
		return false;
	}
	const std::string rootId = core.getSelectedObjectId();
	Orkige::String reason;
	if (!core.canMakePrefab(rootId, &reason))
	{
		SDL_Log("orkige_editor: Create Prefab refused for '%s' - %s",
			rootId.c_str(), reason.c_str());
		return false;
	}
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	optr<Orkige::GameObject> root = manager.getGameObject(rootId).lock();
	oAssert(root);
	const bool remake = !root->getPrefabRef().empty();
	std::string prefabRef;
	std::string prefabPath;
	if (remake)
	{
		// overwrite the instance's OWN source prefab
		prefabRef = root->getPrefabRef();
		prefabPath = state.project.getRootDirectory() + "/" + prefabRef;
	}
	else
	{
		// file name derived from the object id (path separators sanitized)
		std::string fileName = rootId;
		for (char& c : fileName)
		{
			if (c == '/' || c == '\\' || c == ':')
			{
				c = '_';
			}
		}
		prefabRef = "assets/" + fileName + ".oprefab";
		prefabPath = state.project.getAssetsDirectory() + "/" + fileName +
			".oprefab";
		std::error_code ignored;
		if (std::filesystem::exists(prefabPath, ignored))
		{
			SDL_Log("orkige_editor: Create Prefab refused - '%s' already "
				"exists (rename the object or remove the file first)",
				prefabPath.c_str());
			return false;
		}
	}
	{
		std::error_code ignored;
		std::filesystem::create_directories(
			std::filesystem::path(prefabPath).parent_path(), ignored);
	}
	if (!Orkige::PrefabSerializer::savePrefab(prefabPath, manager, rootId))
	{
		SDL_Log("orkige_editor: Create Prefab failed - could not write '%s' "
			"(see the log above)", prefabPath.c_str());
		return false;
	}
	// editor-side asset creation mints the stable id right away (an existing
	// sidecar - the re-make case - is reused)
	std::string assetId;
	if (optr<Orkige::AssetDatabase> const& assetDatabase =
		state.project.getAssetDatabase())
	{
		assetId = assetDatabase->importAsset(prefabPath);
	}
	if (remake)
	{
		// the file now IS the live subtree: stale structural overrides no
		// longer apply (the fs overwrite is not undoable, so neither is this)
		root->setPrefabRef(prefabRef,
			assetId.empty() ? root->getPrefabAssetId() : assetId);
		root->setSuppressedPrefabChildren(Orkige::StringVector());
		core.markSceneDirty();
		SDL_Log("orkige_editor: prefab '%s' re-made from instance '%s'",
			prefabRef.c_str(), rootId.c_str());
		return true;
	}
	if (!core.makePrefabInstance(rootId, prefabPath, prefabRef, assetId))
	{
		SDL_Log("orkige_editor: Create Prefab failed - could not convert "
			"'%s' into an instance (see the log above)", rootId.c_str());
		return false;
	}
	SDL_Log("orkige_editor: prefab '%s' created from '%s' (asset id %s)",
		prefabRef.c_str(), rootId.c_str(),
		assetId.empty() ? "<none>" : assetId.c_str());
	return true;
}

namespace
{
	//! resolve the selected prefab-instance root's .oprefab path through the
	//! open project; "" (and a Console line) when the selection is not a live
	//! instance root or the file is missing. out receives the root id.
	std::string resolveSelectedInstancePrefab(EditorState& state,
		Orkige::EditorCore& core, std::string const& action, std::string& outRootId)
	{
		if (!core.hasSelection())
		{
			SDL_Log("orkige_editor: %s needs a selected prefab instance",
				action.c_str());
			return "";
		}
		outRootId = core.getSelectedObjectId();
		if (!core.canApplyOrRevertPrefab(outRootId))
		{
			SDL_Log("orkige_editor: %s refused - '%s' is not a prefab instance "
				"root", action.c_str(), outRootId.c_str());
			return "";
		}
		optr<Orkige::GameObject> root =
			core.getGameObjectManager().getGameObject(outRootId).lock();
		oAssert(root);
		const std::string prefabRef = root->getPrefabRef();
		std::string prefabPath = prefabRef;
		if (state.project.isLoaded())
		{
			prefabPath = state.project.getRootDirectory() + "/" + prefabRef;
		}
		std::error_code ignored;
		if (!std::filesystem::exists(prefabPath, ignored))
		{
			SDL_Log("orkige_editor: %s refused - prefab '%s' not found (%s)",
				action.c_str(), prefabRef.c_str(), prefabPath.c_str());
			return "";
		}
		return prefabPath;
	}
}

bool applyPrefabOverrides(EditorState& state, Orkige::EditorCore& core)
{
	std::string rootId;
	const std::string prefabPath =
		resolveSelectedInstancePrefab(state, core, "Apply Prefab", rootId);
	if (prefabPath.empty())
	{
		return false;
	}
	if (!core.applyPrefabToSource(rootId, prefabPath))
	{
		SDL_Log("orkige_editor: Apply Prefab failed - could not write '%s' "
			"(see the log above)", prefabPath.c_str());
		return false;
	}
	// re-import so the asset database picks up the rewritten file
	if (optr<Orkige::AssetDatabase> const& assetDatabase =
		state.project.getAssetDatabase())
	{
		assetDatabase->importAsset(prefabPath);
	}
	SDL_Log("orkige_editor: applied instance '%s' back to its prefab",
		rootId.c_str());
	return true;
}

bool revertPrefabInstance(EditorState& state, Orkige::EditorCore& core)
{
	std::string rootId;
	const std::string prefabPath =
		resolveSelectedInstancePrefab(state, core, "Revert Prefab", rootId);
	if (prefabPath.empty())
	{
		return false;
	}
	if (!core.revertPrefabInstance(rootId, prefabPath))
	{
		SDL_Log("orkige_editor: Revert Prefab failed for '%s' (see the log "
			"above)", rootId.c_str());
		return false;
	}
	SDL_Log("orkige_editor: reverted instance '%s' to its pristine prefab",
		rootId.c_str());
	return true;
}
