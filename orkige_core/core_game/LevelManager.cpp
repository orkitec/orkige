/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/LevelManager.h"
#include "core_serialization/XMLArchive.h"

namespace Orkige
{
	const String LevelManager::SAVE_FILE_MAGIC = "orkige.osave";
	const int LevelManager::SAVE_FORMAT_VERSION = 1;
	//---------------------------------------------------------
	IMPL_OSINGLETON(LevelManager);
	//---------------------------------------------------------
	LevelManager::LevelManager()
		: mCurrentIndex(0), mHasPending(false), mPendingIndex(-1)
		, mResumeLevel(0)
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
	int LevelManager::bestMoves(int index) const
	{
		std::map<int, int>::const_iterator it = mBestMoves.find(index);
		return it != mBestMoves.end() ? it->second : -1;
	}
	//---------------------------------------------------------
	void LevelManager::recordBestMoves(int index, int moves)
	{
		std::map<int, int>::iterator it = mBestMoves.find(index);
		if(it == mBestMoves.end() || moves < it->second)
		{
			mBestMoves[index] = moves;
		}
	}
	//---------------------------------------------------------
	bool LevelManager::loadProgress()
	{
		// reset to a fresh state first - a missing/garbage file is the honest
		// "start at level 0" fallback
		mResumeLevel = 0;
		mBestMoves.clear();
		if(mSaveFile.empty())
		{
			return false;
		}
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startReading(mSaveFile))
		{
			// no save yet - a first run, not an error
			return false;
		}
		String magic;
		ar >> magic;
		if(magic != SAVE_FILE_MAGIC)
		{
			oDebugMsg("game", 0, "LevelManager: " << mSaveFile
				<< " is not an orkige save file (magic: \"" << magic
				<< "\") - starting fresh");
			ar->stopReading();
			return false;
		}
		int version = 0;
		ar >> version;
		if(version > SAVE_FORMAT_VERSION)
		{
			oDebugMsg("game", 0, "LevelManager: save file " << mSaveFile
				<< " has unsupported version " << version << " - starting fresh");
			ar->stopReading();
			return false;
		}
		int resume = 0;
		ar >> resume;
		mResumeLevel = resume < 0 ? 0 : resume;
		unsigned int entryCount = 0;
		ar >> entryCount;
		for(unsigned int entryIndex = 0; entryIndex < entryCount; ++entryIndex)
		{
			int levelIndex = 0;
			int moves = 0;
			ar >> levelIndex;
			ar >> moves;
			mBestMoves[levelIndex] = moves;
		}
		ar->stopReading();
		oDebugMsg("game", 0, "LevelManager: progression loaded from " << mSaveFile
			<< " (resume level " << mResumeLevel << ", " << mBestMoves.size()
			<< " scored)");
		return true;
	}
	//---------------------------------------------------------
	bool LevelManager::saveProgress() const
	{
		if(mSaveFile.empty())
		{
			return false;
		}
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startWriting(mSaveFile))
		{
			oDebugMsg("game", 0, "LevelManager: could not start writing save file: " << mSaveFile);
			return false;
		}
		ar << SAVE_FILE_MAGIC;
		int version = SAVE_FORMAT_VERSION;
		ar << version;
		int resume = mResumeLevel;
		ar << resume;
		unsigned int entryCount = static_cast<unsigned int>(mBestMoves.size());
		ar << entryCount;
		for(std::map<int, int>::const_iterator it = mBestMoves.begin();
			it != mBestMoves.end(); ++it)
		{
			int levelIndex = it->first;
			int moves = it->second;
			ar << levelIndex;
			ar << moves;
		}
		bool written = ar->stopWriting();
		if(!written)
		{
			oDebugMsg("game", 0, "LevelManager: error while writing save file: " << mSaveFile);
		}
		return written;
	}
	//---------------------------------------------------------
}
