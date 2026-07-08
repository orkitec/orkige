// FileDialog - result marshaling for the native SDL3 file dialogs.
//
// SDL3's file dialogs (SDL_ShowOpenFileDialog / SDL_ShowSaveFileDialog -
// NSOpenPanel/NSSavePanel on macOS) are asynchronous: the show call returns
// immediately and the result callback fires later. On macOS the callback
// runs on the main thread (the panel's completion handler, delivered while
// SDL pumps AppKit events), but SDL only guarantees "possibly a different
// thread" across platforms - so NOTHING UI- or scene-related may happen
// inside the callback. It only deposits a FileDialogResult here; the
// editor's main loop consumes it once per frame and acts on the main thread.
//
// Pure std (no SDL/ImGui includes) so the unit tests (tests/editor_core)
// exercise the consume semantics headlessly.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_util/String.h>

#include <mutex>

namespace Orkige
{
	//! which editor action a file dialog was opened for
	enum class FileDialogAction
	{
		None,
		OpenScene,		//!< File > Open Scene... (.oscene)
		SaveSceneAs,	//!< File > Save Scene As... (.oscene)
		ImportMesh,		//!< File > Import Mesh... (.glb/.gltf/.obj/...)
		NewProject,		//!< File > New Project... (folder dialog - the picked folder becomes the project)
		OpenProject		//!< File > Open Project... (folder dialog; a .orkproj path also works via the fallback modal)
	};

	//! outcome of one dialog interaction (exactly one of accepted/failed is
	//! set on a decided dialog; neither = the user cancelled)
	struct FileDialogResult
	{
		FileDialogAction action = FileDialogAction::None;
		bool accepted = false;		//!< the user chose a file
		bool failed = false;		//!< the dialog itself errored out
		String path;				//!< chosen path (when accepted)
		String errorMessage;		//!< SDL error text (when failed)
	};

	//! @brief thread-safe single-slot mailbox between the SDL dialog callback
	//! (any thread) and the main loop.
	//! deliver() overwrites an unconsumed result (latest wins - the editor
	//! opens one dialog at a time, so this only matters if a stale callback
	//! straggles in); consume() hands each delivered result out exactly once.
	class FileDialogResultQueue
	{
	public:
		//! called from the dialog callback (any thread)
		void deliver(FileDialogResult const& result);

		//! called once per frame from the main loop.
		//! @param outResult receives the pending result (when there is one)
		//! @return true when a result was pending (outResult is valid)
		bool consume(FileDialogResult& outResult);

	private:
		std::mutex mMutex;
		FileDialogResult mPending;
		bool mHasPending = false;
	};
}
