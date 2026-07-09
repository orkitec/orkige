// EditorFileDialogs.cpp - the native SDL3 file dialogs (async callback +
// main-thread mailbox dispatch) and the "Scene Path" fallback modal request.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <filesystem>
#include <string>

// the directory scene dialogs default into: the open project ROOTS them
// (scenes/), loose-scene mode keeps the historical sample scene dir
std::string defaultSceneDirectory(EditorState const& state)
{
	return state.project.isLoaded()
		? state.project.getScenesDirectory()
		: std::string(ORKIGE_EDITOR_SCENE_DIR);
}

namespace
{

// open the "Scene Path" FALLBACK modal preloaded with a sensible path (the
// current scene / a default inside the scene directory, the asset directory
// for a mesh import, or the project root for the project actions) - only
// reached when a native file dialog reported failure (requestFileDialog
// logs that and lands here)
void requestScenePathPopup(EditorState& state,
	Orkige::FileDialogAction action)
{
	state.scenePathAction = action;
	state.openScenePathPopup = true;
	std::string defaultPath;
	if (action == Orkige::FileDialogAction::ImportMesh)
	{
		defaultPath = ORKIGE_EDITOR_ASSET_DIR "/";
	}
	else if (action == Orkige::FileDialogAction::NewProject ||
		action == Orkige::FileDialogAction::OpenProject)
	{
		// a folder path (or a .orkproj for Open) - start where the current
		// project lives
		defaultPath = state.project.isLoaded()
			? std::filesystem::path(state.project.getRootDirectory())
				.parent_path().string() + "/"
			: std::string();
	}
	else
	{
		defaultPath = state.currentScenePath.empty()
			? defaultSceneDirectory(state) + "/scene.oscene"
			: state.currentScenePath;
	}
	SDL_strlcpy(state.scenePathInput, defaultPath.c_str(),
		sizeof(state.scenePathInput));
}

//--- native file dialogs ----------------------------------------------------

// File > Open/Save As/Import Mesh use SDL3's native file dialogs (NSOpenPanel/
// NSSavePanel on macOS - the save panel brings its own "replace existing
// file?" confirmation). The dialogs are ASYNC: SDL_ShowOpenFileDialog/
// SDL_ShowSaveFileDialog return immediately and invoke fileDialogCallback
// later - on macOS on the main thread (panel completion handler inside SDL's
// event pump), on other platforms possibly from a worker thread. The callback
// therefore only deposits the outcome in this mailbox; the main loop consumes
// it once per frame (dispatchFileDialogResults) and runs the scene/Ogre work
// on the main thread. A failed dialog falls back to the "Scene Path" modal
// (see requestScenePathPopup) so the action stays reachable.
Orkige::FileDialogResultQueue gFileDialogResults;

// filter tables passed to SDL - must stay valid until the callback fired
// (SDL requirement), hence static storage
const SDL_DialogFileFilter SCENE_FILE_FILTERS[] = {
	{ "Orkige scene", "oscene" },
};
// keep in sync with Orkige::isSupportedMeshFile (MeshImport.cpp)
const SDL_DialogFileFilter MESH_FILE_FILTERS[] = {
	{ "Mesh files", "glb;gltf;obj;fbx;dae;stl;ply;3ds;mesh" },
	{ "All files", "*" },
};

//! heap-allocated per dialog, owned by (and freed in) the callback
struct FileDialogRequest
{
	Orkige::FileDialogAction action = Orkige::FileDialogAction::None;
};

// the SDL dialog callback: NO UI/scene/ImGui work here (thread rules above) -
// classify the outcome and hand it to the mailbox
void SDLCALL fileDialogCallback(void* userdata, char const* const* filelist,
	int /*filterIndex*/)
{
	FileDialogRequest* request = static_cast<FileDialogRequest*>(userdata);
	Orkige::FileDialogResult result;
	result.action = request->action;
	delete request;
	if (!filelist)
	{
		// dialog error - SDL_GetError is thread-local, so read it HERE on
		// the callback's thread while it still carries the dialog's error
		result.failed = true;
		result.errorMessage = SDL_GetError();
	}
	else if (*filelist)
	{
		// the editor's dialogs are all single-selection - the first entry
		// is the choice
		result.accepted = true;
		result.path = *filelist;
	}
	// neither: the user cancelled - a consumed no-op
	gFileDialogResults.deliver(result);
}

} // namespace

// show the native dialog for one editor action (all entry points funnel
// through here: ImGui menu, native mac menu, Cmd/Ctrl shortcuts)
void requestFileDialog(EditorState& state, SDL_Window* window,
	Orkige::FileDialogAction action)
{
	// default location rule (SDL: trailing '/' = start directory, otherwise
	// directory + pre-filled file name): Save As offers the current scene
	// file, Open starts where the current scene lives, Import Mesh in the
	// sample asset directory - the scene actions fall back to the OPEN
	// PROJECT's scenes/ (or the sample scene dir in loose-scene mode). The
	// project actions use SDL's native FOLDER dialog: New Project picks (or
	// creates, via the panel's New Folder button) the project folder, Open
	// Project picks an existing project folder.
	std::string defaultLocation;
	switch (action)
	{
	case Orkige::FileDialogAction::SaveSceneAs:
		defaultLocation = state.currentScenePath.empty()
			? defaultSceneDirectory(state) + "/scene.oscene"
			: state.currentScenePath;
		break;
	case Orkige::FileDialogAction::OpenScene:
		defaultLocation = state.currentScenePath.empty()
			? defaultSceneDirectory(state) + "/"
			: std::filesystem::path(state.currentScenePath)
				.parent_path().string() + "/";
		break;
	case Orkige::FileDialogAction::ImportMesh:
		defaultLocation = ORKIGE_EDITOR_ASSET_DIR "/";
		break;
	case Orkige::FileDialogAction::NewProject:
	case Orkige::FileDialogAction::OpenProject:
		if (state.project.isLoaded())
		{
			defaultLocation = std::filesystem::path(
				state.project.getRootDirectory())
					.parent_path().string() + "/";
		}
		break;
	case Orkige::FileDialogAction::None:
		return;
	}
	FileDialogRequest* request = new FileDialogRequest{ action };
	if (action == Orkige::FileDialogAction::SaveSceneAs)
	{
		SDL_ShowSaveFileDialog(fileDialogCallback, request, window,
			SCENE_FILE_FILTERS, 1, defaultLocation.c_str());
	}
	else if (action == Orkige::FileDialogAction::OpenScene)
	{
		SDL_ShowOpenFileDialog(fileDialogCallback, request, window,
			SCENE_FILE_FILTERS, 1, defaultLocation.c_str(), false);
	}
	else if (action == Orkige::FileDialogAction::NewProject ||
		action == Orkige::FileDialogAction::OpenProject)
	{
		SDL_ShowOpenFolderDialog(fileDialogCallback, request, window,
			defaultLocation.empty() ? nullptr : defaultLocation.c_str(),
			false);
	}
	else
	{
		SDL_ShowOpenFileDialog(fileDialogCallback, request, window,
			MESH_FILE_FILTERS, 2, defaultLocation.c_str(), false);
	}
}

// once per frame on the main thread: act on whatever the dialog callback
// deposited - the ONLY place dialog outcomes touch the scene/editor state
void dispatchFileDialogResults(EditorState& state, Orkige::EditorCore& core)
{
	Orkige::FileDialogResult result;
	while (gFileDialogResults.consume(result))
	{
		if (result.failed)
		{
			SDL_Log("orkige_editor: native file dialog failed (%s) - "
				"falling back to the path-input modal",
				result.errorMessage.c_str());
			requestScenePathPopup(state, result.action);
			continue;
		}
		if (!result.accepted)
		{
			continue; // cancelled
		}
		switch (result.action)
		{
		case Orkige::FileDialogAction::SaveSceneAs:
			saveSceneToPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::OpenScene:
			openSceneFromPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::ImportMesh:
			importMeshFromPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::NewProject:
			newProjectAtPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::OpenProject:
			openProjectFromPath(state, core, result.path);
			break;
		case Orkige::FileDialogAction::None:
			break;
		}
	}
}
