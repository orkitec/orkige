/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	SaveStore.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/SaveStore.h"
#include "core_serialization/XMLArchive.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace Orkige
{
	const String SaveStore::SAVE_FILE_MAGIC = "orkige.save";
	const int SaveStore::SAVE_FORMAT_VERSION = 1;
	//---------------------------------------------------------
	IMPL_OSINGLETON(SaveStore);
	//---------------------------------------------------------
	namespace
	{
		//! canonical string form of a double, round-tripped losslessly by the
		//! getNumber parse below (std::ostringstream default precision is lossy;
		//! max_digits10 keeps the value bit-exact)
		String numberToString(double value)
		{
			std::ostringstream stream;
			stream.precision(17);	// std::numeric_limits<double>::max_digits10
			stream << value;
			return stream.str();
		}
	}
	//---------------------------------------------------------
	SaveStore::SaveStore()
		: mDirty(false)
	{
	}
	//---------------------------------------------------------
	SaveStore::~SaveStore()
	{
	}
	//---------------------------------------------------------
	void SaveStore::setNumber(String const & key, double value)
	{
		mEntries[key] = Entry(VK_NUMBER, numberToString(value));
		mDirty = true;
	}
	//---------------------------------------------------------
	void SaveStore::setBool(String const & key, bool value)
	{
		mEntries[key] = Entry(VK_BOOL, value ? "1" : "0");
		mDirty = true;
	}
	//---------------------------------------------------------
	void SaveStore::setString(String const & key, String const & value)
	{
		mEntries[key] = Entry(VK_STRING, value);
		mDirty = true;
	}
	//---------------------------------------------------------
	double SaveStore::getNumber(String const & key, double fallback) const
	{
		std::map<String, Entry>::const_iterator it = mEntries.find(key);
		if(it == mEntries.end() || it->second.kind != VK_NUMBER)
		{
			return fallback;
		}
		return std::strtod(it->second.value.c_str(), NULL);
	}
	//---------------------------------------------------------
	bool SaveStore::getBool(String const & key, bool fallback) const
	{
		std::map<String, Entry>::const_iterator it = mEntries.find(key);
		if(it == mEntries.end() || it->second.kind != VK_BOOL)
		{
			return fallback;
		}
		return it->second.value != "0" && !it->second.value.empty();
	}
	//---------------------------------------------------------
	String SaveStore::getString(String const & key, String const & fallback) const
	{
		std::map<String, Entry>::const_iterator it = mEntries.find(key);
		if(it == mEntries.end())
		{
			return fallback;
		}
		// a Number/Bool value answers with its canonical string form - the
		// string getter is the honest "read whatever is there" accessor
		return it->second.value;
	}
	//---------------------------------------------------------
	bool SaveStore::has(String const & key) const
	{
		return mEntries.find(key) != mEntries.end();
	}
	//---------------------------------------------------------
	void SaveStore::remove(String const & key)
	{
		if(mEntries.erase(key) > 0)
		{
			mDirty = true;
		}
	}
	//---------------------------------------------------------
	void SaveStore::clear()
	{
		if(!mEntries.empty())
		{
			mEntries.clear();
			mDirty = true;
		}
	}
	//---------------------------------------------------------
	bool SaveStore::flush()
	{
		if(!mDirty)
		{
			return true;	// nothing to persist since the last flush/load
		}
		if(mSaveFile.empty())
		{
			return false;	// no persistence configured (editor / scriptless run)
		}
		// ATOMIC write: build the full file under a sibling temp path, then
		// rename it over the real one. A crash mid-write leaves the previous
		// (complete) save intact - the temp file is simply orphaned.
		const String tempFile = mSaveFile + ".tmp";
		{
			optr<XMLArchive> ar = onew(new XMLArchive());
			if(!ar->startWriting(tempFile))
			{
				oDebugMsg("game", 0, "SaveStore: could not start writing "
					<< tempFile);
				return false;
			}
			ar << SAVE_FILE_MAGIC;
			int version = SAVE_FORMAT_VERSION;
			ar << version;
			unsigned int entryCount = static_cast<unsigned int>(mEntries.size());
			ar << entryCount;
			for(std::map<String, Entry>::const_iterator it = mEntries.begin();
				it != mEntries.end(); ++it)
			{
				String key = it->first;
				String kind = kindName(it->second.kind);
				String value = it->second.value;
				ar << key;
				ar << kind;
				ar << value;
			}
			if(!ar->stopWriting())
			{
				oDebugMsg("game", 0, "SaveStore: error while writing "
					<< tempFile);
				return false;
			}
		}
		std::error_code renameError;
		std::filesystem::rename(tempFile, mSaveFile, renameError);
		if(renameError)
		{
			oDebugMsg("game", 0, "SaveStore: could not replace " << mSaveFile
				<< " with the temp file (" << renameError.message() << ")");
			std::error_code ignored;
			std::filesystem::remove(tempFile, ignored);
			return false;
		}
		mDirty = false;
		oDebugMsg("game", 0, "SaveStore: flushed " << mEntries.size()
			<< " entries to " << mSaveFile);
		return true;
	}
	//---------------------------------------------------------
	bool SaveStore::load()
	{
		// reset first - a missing/garbage file is the honest empty-store fallback
		mEntries.clear();
		mDirty = false;
		if(mSaveFile.empty())
		{
			return false;
		}
		optr<XMLArchive> ar = onew(new XMLArchive());
		if(!ar->startReading(mSaveFile))
		{
			return false;	// no save yet - a first run, not an error
		}
		String magic;
		ar >> magic;
		if(magic != SAVE_FILE_MAGIC)
		{
			oDebugMsg("game", 0, "SaveStore: " << mSaveFile << " is not an "
				"orkige save file (magic \"" << magic << "\") - starting empty");
			ar->stopReading();
			return false;
		}
		int version = 0;
		ar >> version;
		if(version > SAVE_FORMAT_VERSION)
		{
			oDebugMsg("game", 0, "SaveStore: " << mSaveFile << " has unsupported "
				"version " << version << " - starting empty");
			ar->stopReading();
			return false;
		}
		unsigned int entryCount = 0;
		ar >> entryCount;
		for(unsigned int index = 0; index < entryCount; ++index)
		{
			String key;
			String kindText;
			String value;
			ar >> key;
			ar >> kindText;
			ar >> value;
			ValueKind kind = VK_STRING;
			if(!kindFromName(kindText, kind))
			{
				// a foreign/garbage record aborts the load into an empty store
				// rather than trusting a partly-parsed file
				oDebugMsg("game", 0, "SaveStore: " << mSaveFile << " has an "
					"unknown value kind \"" << kindText << "\" - starting empty");
				mEntries.clear();
				ar->stopReading();
				return false;
			}
			mEntries[key] = Entry(kind, value);
		}
		ar->stopReading();
		oDebugMsg("game", 0, "SaveStore: loaded " << mEntries.size()
			<< " entries from " << mSaveFile);
		return true;
	}
	//---------------------------------------------------------
	String SaveStore::kindName(ValueKind kind)
	{
		switch(kind)
		{
		case VK_NUMBER:	return "number";
		case VK_BOOL:	return "bool";
		case VK_STRING:	return "string";
		}
		return "string";
	}
	//---------------------------------------------------------
	bool SaveStore::kindFromName(String const & name, ValueKind & outKind)
	{
		if(name == "number")	{ outKind = VK_NUMBER; return true; }
		if(name == "bool")		{ outKind = VK_BOOL; return true; }
		if(name == "string")	{ outKind = VK_STRING; return true; }
		return false;
	}
	//---------------------------------------------------------
}
