// EditorAutosave - scene autosave + backup conventions and their decision
// logic, kept UI-independent in the orkige_editor_core library so the headless
// editor-core unit tests exercise the naming, the timed-autosave gate and the
// recovery/staleness decision directly.
//
// Conventions (siblings of the scene file, so a project's scenes/ folder holds
// them next to the .oscene):
//   scene.oscene.autosave  - the timed crash-recovery copy, written while the
//                            scene is dirty and NEVER over the real file. It is
//                            removed on a clean save; a leftover file that is
//                            NEWER than the scene is what an open offers to
//                            restore.
//   scene.oscene.bak       - one backup generation, the previous on-disk scene
//                            copied aside right before a save overwrites it.
//
// The actual world write (SceneSerializer::saveScene) stays at the call sites
// that own a GameObjectManager; this module owns the paths, the fire/skip
// decision and the filesystem side effects (backup/remove/staleness).
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_util/String.h>

namespace Orkige
{
	namespace EditorAutosave
	{
		//! the default timed-autosave interval (seconds) - a save fires no more
		//! often than this while the scene stays dirty
		double defaultIntervalSeconds();

		//! the ".autosave" sibling of a scene path ("" for an empty path -
		//! an untitled scene has no file to sit next to)
		String autosavePath(String const& scenePath);
		//! the ".bak" sibling of a scene path ("" for an empty path)
		String backupPath(String const& scenePath);

		//! @brief should the timed autosave fire this frame? Pure decision, no
		//! I/O: only while the scene is DIRTY, NOT during an automated run (the
		//! selfchecks drive the mechanism directly), NOT while a play session is
		//! live and NOT while a prefab edit stage is open (its world is the
		//! prefab subtree, not the scene), and only once the interval elapsed.
		bool shouldAutosave(bool sceneDirty, bool automatedRun, bool playActive,
			bool prefabEditActive, double secondsSinceLastAutosave,
			double intervalSeconds);

		//! @brief is there a recoverable autosave for scenePath? True when the
		//! ".autosave" sibling exists and is at least as new as the scene file
		//! (or the scene file is gone) - the "a newer autosave survived a crash"
		//! signal an open acts on. False when scenePath is empty.
		bool recoveryAvailable(String const& scenePath);

		//! @brief copy the existing scene file aside to its ".bak" sibling (one
		//! generation, overwriting a previous backup). A no-op that returns true
		//! when the scene file does not exist yet (a first Save / Save As to a
		//! new path); false only on a real copy failure.
		bool writeBackup(String const& scenePath);

		//! @brief remove the ".autosave" sibling of scenePath if present. Returns
		//! true when the file was removed or was already absent; false only on a
		//! real removal failure. A no-op for an empty path.
		bool removeAutosave(String const& scenePath);
	}
}
