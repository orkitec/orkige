/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __LevelManager_h__9_7_2026__12_00_00__
#define __LevelManager_h__9_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"
#include "core_game/LevelSequence.h"

#include <map>

namespace Orkige
{
	//! @brief the runtime level director: owns the level sequence, the current
	//! level index, the DEFERRED scene-load request that drives win->next-level
	//! and the small progression save (resume index + best moves per level).
	//! @remarks ONLY a runtime that pumps the deferred load ticks this - the
	//! player creates one (tools/player/main.cpp), the editor never does, so
	//! the Lua level/loadScene API is an honest no-op in edit mode (like the
	//! TweenManager). The load is DEFERRED on purpose: a script requests a
	//! switch mid-update (loadLevel / loadScenePath set the pending request),
	//! and the player applies it at the frame boundary AFTER physics
	//! (PLAYER LOOP TICK ORDER, SLOT #87) - never mid-update, where in-flight
	//! script/update pointers would dangle. consumePendingLoad hands the target
	//! to the player, which tears down through GameObjectManager::clear (the
	//! #86 scene teardown hook - never a second teardown path) and reloads.
	//! The save is written to a path the player sets from the same directory
	//! its engine log uses (PlatformUtil::getDocumentsDirectory on mobile,
	//! getSupportDirectory for desktop exports); a missing/garbage file starts
	//! the game at level 0.
	class ORKIGE_CORE_DLL LevelManager : public Singleton<LevelManager>
	{
		DECL_OSINGLETON(LevelManager);
		//--- Variables ---------------------------------------
	public:
		static const String SAVE_FILE_MAGIC;		//!< "orkige.osave"
		static const int SAVE_FORMAT_VERSION;		//!< 1
	private:
		LevelSequence		mSequence;			//!< the ordered level list
		int					mCurrentIndex;		//!< the live level (0-based)
		bool				mHasPending;		//!< a deferred load is queued
		int					mPendingIndex;		//!< queued level index (-1 = path-only)
		String				mPendingPath;		//!< queued raw scene path ("" = index-based)
		String				mSaveFile;			//!< progression save path ("" = no persistence)
		int					mResumeLevel;		//!< persisted resume index (default 0)
		std::map<int, int>	mBestMoves;			//!< persisted best slide count per level index
		//--- Methods -----------------------------------------
	public:
		LevelManager();
		virtual ~LevelManager();

		//--- sequence (loaded by the player from levels.olevels) ---
		LevelSequence & sequence() { return mSequence; }
		LevelSequence const & getSequence() const { return mSequence; }

		//! Lua: number of levels
		int count() const { return mSequence.getCount(); }
		//! Lua: the live level index
		int currentIndex() const { return mCurrentIndex; }
		//! the player sets this after APPLYING a deferred load
		void setCurrentIndex(int index) { mCurrentIndex = index; }
		//! Lua: display name of level i
		String levelName(int index) const { return mSequence.getName(index); }
		//! Lua: par slide count of level i
		int levelPar(int index) const { return mSequence.getPar(index); }
		//! Lua: project-relative scene path of level i
		String levelScene(int index) const { return mSequence.getScenePath(index); }
		//! Lua: is there a level after the current one
		bool hasNext() const { return mSequence.isValidIndex(mCurrentIndex + 1); }

		//--- deferred load request (Lua-driven, player-applied) ---
		//! Lua: request the deferred load of level `index` (guarded: ignored on
		//! an out-of-range index or while a load is already pending)
		void loadLevel(int index);
		//! Lua: request the deferred load of an arbitrary scene path (the
		//! general world.loadScene primitive; guarded like loadLevel)
		void loadScenePath(String const & path);
		//! is a deferred load queued
		bool hasPendingLoad() const { return mHasPending; }
		//! @brief take the queued load (clears it). false when none is pending.
		//! @param outIndex the target level index, or -1 for a path-only request
		//! @param outPath the scene path to load (resolved from the sequence for
		//! an index request, or the raw requested path)
		bool consumePendingLoad(int & outIndex, String & outPath);

		//--- progression save (resume index + best moves) ---
		//! set the progression save file path ("" disables persistence)
		void setSaveFile(String const & path) { mSaveFile = path; }
		String const & getSaveFile() const { return mSaveFile; }
		//! @brief load the progression save; a missing/garbage/too-new file is
		//! an HONEST fallback to a fresh state (resume 0, no best moves) and
		//! returns false without disturbing anything else
		bool loadProgress();
		//! Lua/player: write the progression save (no-op returning false when no
		//! save file is set)
		bool saveProgress() const;

		//! Lua: the persisted resume level index
		int resumeLevel() const { return mResumeLevel; }
		//! Lua: set the resume level index (persisted on the next saveProgress)
		void setResumeLevel(int index) { mResumeLevel = index; }
		//! Lua: the best (fewest) slide count recorded for level i, or -1
		int bestMoves(int index) const;
		//! Lua: record a finish of level i in `moves` slides (keeps the minimum)
		void recordBestMoves(int index, int moves);
	protected:
	private:
	};
}

#endif //__LevelManager_h__9_7_2026__12_00_00__
