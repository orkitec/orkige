// EditorDocument.cpp - scene/project document operations: New/Open/Save
// scene, the project open/close/create (with the dedicated
// resource group) and the mesh import (asset database wiring included).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorAutosave.h"
#include "MeshImport.h"
#include "PythonToolchain.h"

#include <core_game/LevelComponent.h>
#include <core_game/LevelSequence.h>
#include <core_game/PrefabSerializer.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_script/ScriptRuntime.h>
#include <engine_gocomponent/ScriptComponentRegistry.h>
#include "EditorScriptHost.h"
#include <engine_render/RenderSystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using Orkige::optr;
using Orkige::woptr;

// File > New Scene: clear all GameObjects - removing the components tears
// down their scene nodes (TransformComponent::onRemove wipes via NodeUtil)
void newScene(EditorState& state, Orkige::EditorCore& core)
{
	if (prefabEditBlocks(state, "New Scene"))
	{
		return;
	}
	core.getGameObjectManager().clear();
	core.resetForScene();
	state.currentScenePath.clear();
}

bool saveSceneToPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	// the world holds a prefab subtree, not the scene - writing it to a
	// .oscene would clobber the scene file with prefab content
	if (prefabEditBlocks(state, "Save Scene (use Save Prefab)"))
	{
		return false;
	}
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	// keep one backup generation: copy the existing on-disk scene aside to its
	// ".bak" sibling BEFORE the save overwrites it (a no-op for a first save /
	// Save As to a new path)
	if (!Orkige::EditorAutosave::writeBackup(path))
	{
		SDL_Log("orkige_editor: could not back up scene '%s' before saving",
			path.c_str());
	}
	if (!Orkige::SceneSerializer::saveScene(path, gameObjectManager))
	{
		SDL_Log("orkige_editor: saving scene '%s' failed", path.c_str());
		return false;
	}
	SDL_Log("orkige_editor: scene saved to '%s' (%zu GameObjects)",
		path.c_str(), gameObjectManager.getGameObjects().size());
	state.currentScenePath = path;
	core.clearSceneDirty();
	// the scene is clean on disk now - drop any stale autosave sibling
	Orkige::EditorAutosave::removeAutosave(path);
	recordRecentScene(path);
	return true;
}

bool openSceneFromPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& path)
{
	if (prefabEditBlocks(state, "Open Scene"))
	{
		return false;
	}
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
	// crash recovery: a ".autosave" sibling NEWER than the scene file means a
	// prior session died with unsaved work. Automated runs auto-discard it
	// silently (they must never block on a modal); an interactive session raises
	// the Restore/Discard modal, drawn by drawEditorModals.
	if (Orkige::EditorAutosave::recoveryAvailable(path))
	{
		if (gAutomatedRun)
		{
			Orkige::EditorAutosave::removeAutosave(path);
			SDL_Log("orkige_editor: discarded a stale autosave for '%s' "
				"(automated run)", path.c_str());
		}
		else
		{
			state.autosaveRecoveryScenePath = path;
			state.openAutosaveRecoveryPopup = true;
		}
	}
	return true;
}

bool writeSceneAutosave(EditorState& state, Orkige::EditorCore& core)
{
	if (state.currentScenePath.empty())
	{
		return false;	// an untitled scene has no file to sit next to
	}
	const std::string autosavePath =
		Orkige::EditorAutosave::autosavePath(state.currentScenePath);
	// write the world to the sibling - NEVER the real file, and the dirty flag
	// stays set (the user still owes a real save)
	if (!Orkige::SceneSerializer::saveScene(autosavePath,
		core.getGameObjectManager()))
	{
		SDL_Log("orkige_editor: autosave to '%s' failed", autosavePath.c_str());
		return false;
	}
	return true;
}

bool restoreSceneAutosave(EditorState& state, Orkige::EditorCore& core,
	std::string const& scenePath)
{
	const std::string autosavePath =
		Orkige::EditorAutosave::autosavePath(scenePath);
	Orkige::GameObjectManager& gameObjectManager = core.getGameObjectManager();
	core.resetForScene();
	if (!Orkige::SceneSerializer::loadScene(autosavePath, gameObjectManager))
	{
		SDL_Log("orkige_editor: restoring autosave '%s' failed",
			autosavePath.c_str());
		return false;
	}
	applyUnlitFixToLoadedModels(core);
	state.currentScenePath = scenePath;
	// the recovered state is unsaved (newer than the file) - mark dirty so the
	// user saves it, and clear the autosave now that it lives in the world
	core.markSceneDirty();
	Orkige::EditorAutosave::removeAutosave(scenePath);
	SDL_Log("orkige_editor: restored autosave for '%s'", scenePath.c_str());
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
	if (prefabEditBlocks(state, "Open Project"))
	{
		return false;
	}
	Orkige::Project project;
	std::string error;
	if (!project.load(path, &error))
	{
		SDL_Log("orkige_editor: opening project failed - %s", error.c_str());
		return false;
	}
	newScene(state, core);
	unregisterProjectResources();
	// the armed tile prefab belongs to the outgoing project - painting it
	// into the incoming one would reference a foreign asset path
	paletteArmAsset(state, core, std::string());
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
	// discover the project's SCRIPT COMPONENT KINDS (*.component.lua) so they
	// appear in Add Component, resolve their exported properties for the
	// Inspector, and let scenes that attach them load (the editor authors them;
	// the spawned player runs them)
	Orkige::ScriptComponentRegistry::getSingleton().scanProject(
		state.project.getScriptsDirectory(), state.project.getRootDirectory());
	// discover the project's EDITOR TOOLS (*.editor.lua under scripts/) so the
	// Tools menu lists them (same folder + rescan discipline as the components)
	if (state.editorScripts)
	{
		state.editorScripts->scanProject(state.project.getScriptsDirectory());
	}
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
	if (prefabEditBlocks(state, "Close Project"))
	{
		return;
	}
	if (!state.project.isLoaded())
	{
		return;
	}
	SDL_Log("orkige_editor: project '%s' closed",
		state.project.getName().c_str());
	newScene(state, core);
	unregisterProjectResources();
	// the armed tile prefab lives in the project being closed
	paletteArmAsset(state, core, std::string());
	state.project.close();
	Orkige::ScriptRuntime::getSingleton().setScriptSearchRoot("");
	// drop the closing project's script component kinds (and their factory
	// aliases) so they never leak into the next project / loose-scene mode
	Orkige::ScriptComponentRegistry::getSingleton().clear();
	// drop the closing project's editor tools so they never leak into the next
	if (state.editorScripts)
	{
		state.editorScripts->clear();
	}
	// back to the built-in default layers (loose-scene mode)
	core.resetPhysicsLayers();
}

// File > New Project...: create the skeleton (project name = the picked
// folder's name) in the chosen folder, open it, and save the initial empty
// main scene the manifest points at - a fresh project is instantly playable
bool newProjectAtPath(EditorState& state, Orkige::EditorCore& core,
	std::string const& folder)
{
	if (prefabEditBlocks(state, "New Project"))
	{
		return false;
	}
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

// An .svg is COOKED to the native .oshape on import (the runtime never parses
// SVG - it reads pre-flattened contours). The cook is Util/cook_shapes.py run
// as a subprocess: the same Python-delegated transform the exporter uses for
// textures, and the only SVG reader in the tree (there is no C++ SVG parser).
// Unlike the async export it runs synchronously - import returns the produced
// asset path to its callers (drop/browser/MCP). The source .svg is NOT kept in
// assets/ (unlike a texture's directly-loadable source): the .oshape IS the
// asset, the .svg is only the on-ramp. Returns the cooked .oshape path, or ""
// (+ *error) on any failure.
std::string cookSvgFileToDir(std::string const& sourcePath,
	std::string const& destDir, std::string* error)
{
	auto fail = [error](std::string const& message) -> std::string
	{
		SDL_Log("orkige_editor: SVG cook failed - %s", message.c_str());
		if (error)
		{
			*error = message;
		}
		return "";
	};
	// preflight the python3 toolchain (cached per run) before spawning the cook -
	// an honest "install python3 >= 3.10" message beats an opaque spawn failure
	const Orkige::PythonProbeResult& python = Orkige::probePythonToolchain();
	if (!python.ok)
	{
		return fail(python.error);
	}
	std::error_code ec;
	std::filesystem::create_directories(destDir, ec);
	const std::string destPath = (std::filesystem::path(destDir) /
		(std::filesystem::path(sourcePath).stem().string() + ".oshape"))
			.string();
	const std::string cook =
		std::string(ORKIGE_EDITOR_ENGINE_ROOT) + "/Util/cook_shapes.py";
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured({ python.executable, cook, sourcePath, destPath },
		output, exitCode))
	{
		return fail("could not launch '" + python.executable +
			"' for cook_shapes.py");
	}
	if (exitCode != 0)
	{
		return fail("cook_shapes.py exited " + std::to_string(exitCode) +
			" for '" + sourcePath + "'" +
			(output.empty() ? "" : (" - " + output)));
	}
	if (!std::filesystem::is_regular_file(destPath, ec))
	{
		return fail("cook_shapes.py produced no .oshape for '" + sourcePath +
			"'");
	}
	return destPath;
}

// The copy+register+sidecar-mint MIDDLE of the old importMeshFromPath, made
// generic so any asset (texture/script/prefab/scene from a Finder drop or the
// browser) rides the same path meshes always did - the copied file lands where
// the resource groups (and SpriteComponent::loadSprite) resolve it, and gets
// its stable .orkmeta id in a project. An .svg source is cooked to .oshape
// first (cookSvgFileToDir); everything else is a byte copy. The mesh-instantiate
// tail stays below, mesh-only.
std::string importAssetFile(EditorState& state, std::string const& sourcePath,
	std::string* error)
{
	const std::string destDir = meshImportDestination(state);
	std::string localError;
	std::string sourceExt =
		std::filesystem::path(sourcePath).extension().string();
	std::transform(sourceExt.begin(), sourceExt.end(), sourceExt.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const std::string destPath = (sourceExt == ".svg")
		? cookSvgFileToDir(sourcePath, destDir, &localError)
		: Orkige::importMeshFileToDir(sourcePath, destDir, &localError);
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
	// the stage IS the prefab being edited - Save Prefab writes it; a nested
	// Create Prefab inside it is refused like the format refuses nesting
	if (prefabEditBlocks(state, "Create Prefab"))
	{
		return false;
	}
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
	if (prefabEditBlocks(state, "Apply to Prefab"))
	{
		return false;
	}
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
	if (prefabEditBlocks(state, "Revert to Prefab"))
	{
		return false;
	}
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

//--- prefab edit mode (the one-world context swap) ---------------------------
// See EditorApp.h for the design block. The load-bearing invariant lives in
// openPrefabForEdit: the scene snapshot is written BEFORE anything can touch
// the .oprefab, so the overrides stored in it were diffed against the file
// the instances were actually instantiated from - closing then reloads the
// snapshot, whose instances rebuild from the (possibly edited) file with
// those overrides re-applied by the scene loader's normal merge.

namespace
{
	//! the LIVE root-level object ids (no parent), sorted for stable messages
	std::vector<std::string> rootLevelObjectIds(
		Orkige::GameObjectManager& manager)
	{
		std::vector<std::string> roots;
		for (auto const& [id, gameObject] : manager.getGameObjects())
		{
			if (gameObject && gameObject->getParentId().empty())
			{
				roots.push_back(id);
			}
		}
		std::sort(roots.begin(), roots.end());
		return roots;
	}
}

bool prefabEditBlocks(EditorState const& state, char const* action)
{
	if (!isPrefabEditActive(state))
	{
		return false;
	}
	SDL_Log("orkige_editor: %s is unavailable while editing prefab '%s' - "
		"close the prefab first", action,
		state.prefabEditStack.back().prefabPath.c_str());
	return true;
}

bool openPrefabForEdit(EditorState& state, Orkige::EditorCore& core,
	std::string const& prefabPath)
{
	if (isPrefabEditActive(state))
	{
		SDL_Log("orkige_editor: Open Prefab refused - already editing '%s' "
			"(close the current prefab first)",
			state.prefabEditStack.back().prefabPath.c_str());
		return false;
	}
	std::error_code ignored;
	if (!std::filesystem::is_regular_file(prefabPath, ignored))
	{
		SDL_Log("orkige_editor: Open Prefab refused - '%s' not found",
			prefabPath.c_str());
		return false;
	}

	// the SNAPSHOT comes first - before the world changes and, structurally,
	// before any edit can reach the .oprefab file (the merge invariant)
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	const std::string snapshotPath =
		(std::filesystem::temp_directory_path() /
			("orkige_prefab_edit_" + std::to_string(
				std::chrono::steady_clock::now()
					.time_since_epoch().count()) + ".oscene")).string();
	if (!Orkige::SceneSerializer::saveScene(snapshotPath, manager))
	{
		SDL_Log("orkige_editor: Open Prefab refused - could not snapshot the "
			"scene to '%s'", snapshotPath.c_str());
		return false;
	}

	PrefabEditContext context;
	context.prefabPath = prefabPath;
	context.prefabRef = state.project.isLoaded()
		? std::string(state.project.makeProjectRelative(prefabPath))
		: std::string();
	context.rootId = std::filesystem::path(prefabPath).stem().string();
	context.snapshotPath = snapshotPath;
	context.stashedScenePath = state.currentScenePath;
	context.stashedSceneDirty = core.isSceneDirty();
	context.stashedSelection = core.getSelection();
	context.stashedCamera = state.camera;
	context.stashedEditor2D = gViewSettings ? gViewSettings->editor2D : false;

	// an armed paint asset would place ROOT-level grid objects - incompatible
	// with the single-root stage; disarm before the swap (arming is refused
	// while the stage is open)
	paletteArmAsset(state, core, std::string());

	// swap: the prefab subtree replaces the scene in the ONE world. The undo
	// history refers to the scene world, so it goes too (per-context scope).
	manager.clear();
	core.resetForScene();
	const Orkige::PrefabSerializer::InstantiateResult result =
		Orkige::PrefabSerializer::instantiatePrefab(prefabPath, manager,
			context.rootId, Orkige::StringVector());
	if (result != Orkige::PrefabSerializer::INSTANTIATE_OK)
	{
		// corrupt file (or a nested prefab - the format hard-errors): put the
		// scene back and report; the editor never strands on an empty world
		SDL_Log("orkige_editor: Open Prefab failed - could not instantiate "
			"'%s' (see the log above); restoring the scene",
			prefabPath.c_str());
		core.resetForScene();
		Orkige::SceneSerializer::loadScene(snapshotPath, manager);
		applyUnlitFixToLoadedModels(core);
		if (context.stashedSceneDirty)
		{
			core.markSceneDirty();
		}
		for (Orkige::String const& id : context.stashedSelection)
		{
			if (manager.objectExists(id))
			{
				core.addToSelection(id);
			}
		}
		std::filesystem::remove(snapshotPath, ignored);
		return false;
	}
	applyUnlitFixToLoadedModels(core);

	state.prefabEditStack.push_back(context);
	// the scene path is stashed empty so every "needs a saved scene" check
	// (Play among them) fails closed while the stage is open
	state.currentScenePath.clear();
	core.selectObject(context.rootId);
	state.frameSelectedRequested = true;
	SDL_Log("orkige_editor: editing prefab '%s' (root '%s', %zu objects)",
		prefabPath.c_str(), context.rootId.c_str(),
		manager.getGameObjects().size());
	return true;
}

bool openSelectedInstancePrefab(EditorState& state, Orkige::EditorCore& core)
{
	if (isPrefabEditActive(state))
	{
		SDL_Log("orkige_editor: Open Prefab refused - already editing '%s' "
			"(close the current prefab first)",
			state.prefabEditStack.back().prefabPath.c_str());
		return false;
	}
	std::string rootId;
	const std::string prefabPath =
		resolveSelectedInstancePrefab(state, core, "Open Prefab", rootId);
	if (prefabPath.empty())
	{
		return false;
	}
	return openPrefabForEdit(state, core, prefabPath);
}

bool savePrefabEdit(EditorState& state, Orkige::EditorCore& core)
{
	if (!isPrefabEditActive(state))
	{
		SDL_Log("orkige_editor: Save Prefab refused - no prefab is open");
		return false;
	}
	PrefabEditContext const& context = state.prefabEditStack.back();
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	if (!manager.objectExists(context.rootId))
	{
		SDL_Log("orkige_editor: Save Prefab refused - the prefab root '%s' "
			"was deleted (undo to restore it)", context.rootId.c_str());
		return false;
	}
	// savePrefab writes ONE subtree: stray root-level objects would silently
	// vanish from the asset - refuse and name them instead
	std::string strays;
	for (std::string const& id : rootLevelObjectIds(manager))
	{
		if (id != context.rootId)
		{
			strays += strays.empty() ? id : (", " + id);
		}
	}
	if (!strays.empty())
	{
		SDL_Log("orkige_editor: Save Prefab refused - objects outside the "
			"prefab root '%s': %s (parent them under the root or delete "
			"them)", context.rootId.c_str(), strays.c_str());
		return false;
	}
	if (!Orkige::PrefabSerializer::savePrefab(context.prefabPath, manager,
		context.rootId))
	{
		SDL_Log("orkige_editor: Save Prefab failed - could not write '%s' "
			"(see the log above)", context.prefabPath.c_str());
		return false;
	}
	// re-import so the asset database keeps the rewritten file's stable id
	// current (the Apply-to-Prefab precedent)
	if (state.project.isLoaded())
	{
		if (optr<Orkige::AssetDatabase> const& assetDatabase =
			state.project.getAssetDatabase())
		{
			assetDatabase->importAsset(context.prefabPath);
		}
	}
	core.clearSceneDirty();	// per-context: "prefab dirty" while staged
	SDL_Log("orkige_editor: prefab saved to '%s'",
		context.prefabPath.c_str());
	return true;
}

bool closePrefabEdit(EditorState& state, Orkige::EditorCore& core,
	PrefabClosePolicy policy)
{
	if (!isPrefabEditActive(state))
	{
		SDL_Log("orkige_editor: Close Prefab refused - no prefab is open");
		return false;
	}
	if (policy == PrefabClosePolicy::Save && core.isSceneDirty() &&
		!savePrefabEdit(state, core))
	{
		// never silently discard on a refused save - the stage stays open
		// with its edits (fix the refusal or close with Discard)
		SDL_Log("orkige_editor: Close Prefab cancelled - saving the prefab "
			"failed (see above)");
		return false;
	}
	const PrefabEditContext context = state.prefabEditStack.back();
	state.prefabEditStack.pop_back();

	// pop: the snapshot replaces the stage in the ONE world; its instances
	// rebuild from the (possibly edited) .oprefab and re-apply their stored
	// per-instance overrides - the scene loader's normal merge. The stage's
	// undo history refers to the prefab world, so it goes too.
	Orkige::GameObjectManager& manager = core.getGameObjectManager();
	core.resetForScene();
	if (!Orkige::SceneSerializer::loadScene(context.snapshotPath, manager))
	{
		// the snapshot was written by openPrefabForEdit - failing to read it
		// back is exceptional; report loudly and leave the file for rescue
		SDL_Log("orkige_editor: Close Prefab - restoring the scene from '%s' "
			"FAILED; the snapshot file is kept",
			context.snapshotPath.c_str());
		state.currentScenePath = context.stashedScenePath;
		return false;
	}
	applyUnlitFixToLoadedModels(core);
	state.currentScenePath = context.stashedScenePath;
	if (context.stashedSceneDirty)
	{
		core.markSceneDirty();
	}
	// best-effort selection restore (ids persist across the serialize
	// round-trip; vanished ones are skipped)
	core.clearSelection();
	for (Orkige::String const& id : context.stashedSelection)
	{
		if (manager.objectExists(id))
		{
			core.addToSelection(id);
		}
	}
	state.camera = context.stashedCamera;
	if (gViewSettings)
	{
		gViewSettings->editor2D = context.stashedEditor2D;
	}
	std::error_code ignored;
	std::filesystem::remove(context.snapshotPath, ignored);
	SDL_Log("orkige_editor: closed prefab '%s' - scene restored (%zu "
		"GameObjects)", context.prefabPath.c_str(),
		manager.getGameObjects().size());
	return true;
}

void requestClosePrefabEdit(EditorState& state, Orkige::EditorCore& core,
	bool quitAfter)
{
	if (!isPrefabEditActive(state))
	{
		return;
	}
	if (core.isSceneDirty())
	{
		// dirty: the Save/Discard/Cancel confirm modal resolves it (drawn by
		// drawEditorModals; automated runs and MCP call closePrefabEdit with
		// an explicit policy instead and never see the modal)
		state.openPrefabClosePopup = true;
		state.prefabCloseQuitIntent = quitAfter;
		return;
	}
	if (closePrefabEdit(state, core, PrefabClosePolicy::Discard) && quitAfter)
	{
		requestQuit(state, core);
	}
}

void saveCurrentDocument(EditorState& state, Orkige::EditorCore& core,
	SDL_Window* window)
{
	if (isPrefabEditActive(state))
	{
		savePrefabEdit(state, core);
		return;
	}
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

bool addCurrentSceneToLevels(EditorState& state, Orkige::EditorCore& core)
{
	if (prefabEditBlocks(state, "Add Scene to Level Sequence"))
	{
		return false;
	}
	if (!state.project.isLoaded())
	{
		SDL_Log("orkige_editor: Add Scene to Level Sequence refused - no open "
			"project");
		return false;
	}
	if (state.currentScenePath.empty())
	{
		SDL_Log("orkige_editor: Add Scene to Level Sequence refused - save the "
			"scene first");
		return false;
	}
	const Orkige::String rel =
		state.project.makeProjectRelative(state.currentScenePath);
	if (rel.empty())
	{
		SDL_Log("orkige_editor: Add Scene to Level Sequence refused - the scene "
			"lies outside the project root");
		return false;
	}

	const Orkige::String levelsRel = state.project.getSetting(
		Orkige::LevelSequence::LEVELS_SETTING_KEY, "levels.olevels");
	const Orkige::String levelsAbs = state.project.resolvePath(levelsRel);

	Orkige::LevelSequence sequence;
	sequence.load(levelsAbs);	// a missing file just starts an empty sequence
	int levelIndex = 1;
	for (Orkige::LevelSequence::Entry const& entry : sequence.getEntries())
	{
		if (entry.scenePath == rel)
		{
			SDL_Log("orkige_editor: Add Scene to Level Sequence refused - '%s' "
				"is already level %d", rel.c_str(), levelIndex);
			return false;
		}
		++levelIndex;
	}

	// par comes from the scene's LevelComponent (the first object carrying one)
	int par = 0;
	for (auto const& [id, gameObject] :
		core.getGameObjectManager().getGameObjects())
	{
		(void)id;
		if (gameObject && gameObject->hasComponent<Orkige::LevelComponent>())
		{
			par = gameObject->getComponentPtr<Orkige::LevelComponent>()->getPar();
			break;
		}
	}

	const Orkige::String name = std::filesystem::path(rel).stem().string();
	sequence.addEntry(Orkige::LevelSequence::Entry(rel, name, par));
	if (!sequence.save(levelsAbs))
	{
		SDL_Log("orkige_editor: Add Scene to Level Sequence failed - could not "
			"write '%s'", levelsAbs.c_str());
		return false;
	}
	// mint the manifest setting the first time so the game (and export) find
	// the sequence file
	if (!state.project.hasSetting(Orkige::LevelSequence::LEVELS_SETTING_KEY))
	{
		state.project.setSetting(Orkige::LevelSequence::LEVELS_SETTING_KEY,
			levelsRel);
		state.project.save();
	}
	SDL_Log("orkige_editor: added scene '%s' as level %d of '%s'", rel.c_str(),
		sequence.getCount(), levelsRel.c_str());
	return true;
}
