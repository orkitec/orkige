/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/LevelManager.h"
#include "core_game/SaveStore.h"

namespace Orkige
{
	namespace
	{
		//! the shared-SaveStore key namespace the progression rides under
		const String KEY_RESUME = "level.resume";
		//! per-level best-slide-count key ("level.best.<index>")
		String bestMovesKey(int index)
		{
			return "level.best." + std::to_string(index);
		}
	}
	//---------------------------------------------------------
	IMPL_OSINGLETON(LevelManager);
	//---------------------------------------------------------
	LevelManager::LevelManager()
		: mCurrentIndex(0), mHasPending(false), mPendingIndex(-1)
	{
	}
	//---------------------------------------------------------
	LevelManager::~LevelManager()
	{
	}
	//---------------------------------------------------------
	void LevelManager::loadLevel(int index)
	{
		if(!mSequence.isValidIndex(index))
		{
			oDebugMsg("game", 0, "LevelManager: loadLevel(" << index
				<< ") ignored - out of range (" << mSequence.getCount() << " levels)");
			return;
		}
		if(mHasPending)
		{
			// re-entrancy guard: a load already queued this frame wins; a second
			// request is dropped rather than racing the pump
			oDebugMsg("game", 0, "LevelManager: loadLevel(" << index
				<< ") ignored - a load is already pending");
			return;
		}
		mHasPending = true;
		mPendingIndex = index;
		mPendingPath.clear();
		oDebugMsg("game", 0, "LevelManager: queued deferred load of level " << index
			<< " (\"" << mSequence.getScenePath(index) << "\")");
	}
	//---------------------------------------------------------
	void LevelManager::loadScenePath(String const & path)
	{
		if(path.empty())
		{
			return;
		}
		if(mHasPending)
		{
			oDebugMsg("game", 0, "LevelManager: loadScenePath(\"" << path
				<< "\") ignored - a load is already pending");
			return;
		}
		mHasPending = true;
		mPendingIndex = -1;
		mPendingPath = path;
		oDebugMsg("game", 0, "LevelManager: queued deferred load of scene \"" << path << "\"");
	}
	//---------------------------------------------------------
	bool LevelManager::consumePendingLoad(int & outIndex, String & outPath)
	{
		if(!mHasPending)
		{
			return false;
		}
		mHasPending = false;
		outIndex = mPendingIndex;
		if(mPendingIndex >= 0)
		{
			outPath = mSequence.getScenePath(mPendingIndex);
		}
		else
		{
			outPath = mPendingPath;
		}
		mPendingIndex = -1;
		mPendingPath.clear();
		return true;
	}
	//---------------------------------------------------------
	int LevelManager::resumeLevel() const
	{
		// progression lives in the shared SaveStore; with none (edit mode /
		// scriptless run) the honest answer is "start at level 0"
		SaveStore* store = SaveStore::getSingletonPtr();
		if(!store)
		{
			return 0;
		}
		const int resume = static_cast<int>(store->getNumber(KEY_RESUME, 0.0));
		return resume < 0 ? 0 : resume;
	}
	//---------------------------------------------------------
	void LevelManager::setResumeLevel(int index)
	{
		if(SaveStore* store = SaveStore::getSingletonPtr())
		{
			store->setNumber(KEY_RESUME, index);
		}
	}
	//---------------------------------------------------------
	int LevelManager::bestMoves(int index) const
	{
		SaveStore* store = SaveStore::getSingletonPtr();
		if(!store)
		{
			return -1;
		}
		const String key = bestMovesKey(index);
		return store->has(key)
			? static_cast<int>(store->getNumber(key, -1.0)) : -1;
	}
	//---------------------------------------------------------
	void LevelManager::recordBestMoves(int index, int moves)
	{
		SaveStore* store = SaveStore::getSingletonPtr();
		if(!store)
		{
			return;
		}
		// keep the minimum (a better - lower - slide count wins)
		const String key = bestMovesKey(index);
		if(!store->has(key) ||
			moves < static_cast<int>(store->getNumber(key, moves)))
		{
			store->setNumber(key, moves);
		}
	}
	//---------------------------------------------------------
	bool LevelManager::saveProgress() const
	{
		// the progression keys were set live through setResumeLevel /
		// recordBestMoves; flushing the shared store writes them atomically
		SaveStore* store = SaveStore::getSingletonPtr();
		return store ? store->flush() : false;
	}
	//---------------------------------------------------------
}
