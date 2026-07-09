/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelSequence.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/LevelSequence.h"
#include "core_serialization/XMLArchive.h"

namespace Orkige
{
	const String LevelSequence::LEVELS_FILE_MAGIC = "orkige.olevels";
	const int LevelSequence::LEVELS_FORMAT_VERSION = 1;
	const String LevelSequence::LEVELS_SETTING_KEY = "levels";
	//---------------------------------------------------------
	LevelSequence::LevelSequence()
	{
	}
	//---------------------------------------------------------
	bool LevelSequence::isValidIndex(int index) const
	{
		return index >= 0 && index < this->getCount();
	}
	//---------------------------------------------------------
	String LevelSequence::getScenePath(int index) const
	{
		return this->isValidIndex(index) ? mEntries[index].scenePath : String();
	}
	//---------------------------------------------------------
	String LevelSequence::getName(int index) const
	{
		return this->isValidIndex(index) ? mEntries[index].name : String();
	}
	//---------------------------------------------------------
	int LevelSequence::getPar(int index) const
	{
		return this->isValidIndex(index) ? mEntries[index].par : 0;
	}
	//---------------------------------------------------------
	void LevelSequence::addEntry(Entry const & entry)
	{
		mEntries.push_back(entry);
	}
	//---------------------------------------------------------
	void LevelSequence::clear()
	{
		mEntries.clear();
	}
	//---------------------------------------------------------
	bool LevelSequence::load(String const & fileName)
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startReading(fileName))
		{
			oDebugMsg("game", 0, "LevelSequence: could not open level file: " << fileName);
			return false;
		}
		String magic;
		ar >> magic;
		if(magic != LEVELS_FILE_MAGIC)
		{
			oDebugMsg("game", 0, "LevelSequence: " << fileName
				<< " is not an orkige level file (magic: \"" << magic << "\")");
			ar->stopReading();
			return false;
		}
		int version = 0;
		ar >> version;
		if(version > LEVELS_FORMAT_VERSION)
		{
			oDebugMsg("game", 0, "LevelSequence: level file " << fileName
				<< " has unsupported version " << version << " (supported: "
				<< LEVELS_FORMAT_VERSION << ")");
			ar->stopReading();
			return false;
		}
		// build into scratch; the live sequence is only replaced on success
		std::vector<Entry> loaded;
		unsigned int levelCount = 0;
		ar >> levelCount;
		for(unsigned int levelIndex = 0; levelIndex < levelCount; ++levelIndex)
		{
			Entry entry;
			ar >> entry.scenePath;
			ar >> entry.name;
			ar >> entry.par;
			loaded.push_back(entry);
		}
		ar->stopReading();
		this->mEntries.swap(loaded);
		oDebugMsg("game", 0, "LevelSequence: loaded " << this->mEntries.size()
			<< " level(s) from " << fileName);
		return true;
	}
	//---------------------------------------------------------
	bool LevelSequence::save(String const & fileName) const
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startWriting(fileName))
		{
			oDebugMsg("game", 0, "LevelSequence: could not start writing level file: " << fileName);
			return false;
		}
		ar << LEVELS_FILE_MAGIC;
		int version = LEVELS_FORMAT_VERSION;
		ar << version;
		unsigned int levelCount = static_cast<unsigned int>(mEntries.size());
		ar << levelCount;
		for(Entry const & entry : mEntries)
		{
			String scenePath = entry.scenePath;
			String name = entry.name;
			int par = entry.par;
			ar << scenePath;
			ar << name;
			ar << par;
		}
		bool written = ar->stopWriting();
		if(!written)
		{
			oDebugMsg("game", 0, "LevelSequence: error while writing level file: " << fileName);
		}
		return written;
	}
	//---------------------------------------------------------
}
