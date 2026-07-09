// EditorDocument.cpp - scene/project document operations: New/Open/Save
// scene, the Unity-style project open/close/create (with the dedicated
// resource group) and the mesh import (asset database wiring included).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "MeshImport.h"

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

//--- project handling (Unity-style "open a project, not a scene") ----------

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

//! register the project's assets/ and scenes/ in the project group;
//! missing directories are skipped with an honest Console line
void registerProjectResources(Orkige::Project const& project)
{
	for (std::string const& projectDir : { project.getAssetsDirectory(),
		project.getScenesDirectory() })
	{
		std::error_code ignored;
		if (std::filesystem::is_directory(projectDir, ignored))
		{
			Orkige::RenderSystem::get()->addResourceLocation(projectDir,
				Orkige::RenderSystem::LT_FILESYSTEM,
				Orkige::Project::RESOURCE_GROUP_NAME);
		}
		else
		{
			SDL_Log("orkige_editor: project directory '%s' does not exist - "
				"not registered", projectDir.c_str());
		}
	}
}

} // namespace

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
	const std::string destDir = meshImportDestination(state);
	std::string error;
	const std::string destPath =
		Orkige::importMeshFileToDir(sourcePath, destDir, &error);
	if (destPath.empty())
	{
		SDL_Log("orkige_editor: import of '%s' failed - %s",
			sourcePath.c_str(), error.c_str());
		return false;
	}
	if (state.project.isLoaded())
	{
		// the assets/ location index was built when the project opened -
		// re-register it in the project group so the just-copied file is
		// findable by bare filename right away (also covers an assets/ that
		// was missing at open time and got created by this copy);
		// removeResourceLocation is idempotent by facade contract
		Orkige::RenderSystem* render = Orkige::RenderSystem::get();
		render->removeResourceLocation(destDir,
			Orkige::Project::RESOURCE_GROUP_NAME);
		render->addResourceLocation(destDir,
			Orkige::RenderSystem::LT_FILESYSTEM,
			Orkige::Project::RESOURCE_GROUP_NAME);
		// editor-side asset creation mints the stable id right away, so the
		// ModelComponent created below serializes it with the scene
		if (optr<Orkige::AssetDatabase> const& assetDatabase =
			state.project.getAssetDatabase())
		{
			assetDatabase->importAsset(destPath);
		}
	}
	else if (state.importResourceDirs.insert(destDir).second)
	{
		// indexes the directory contents immediately - the mesh is loadable
		// by bare filename right after this
		Orkige::RenderSystem::get()->addResourceLocation(destDir);
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
