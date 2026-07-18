/********************************************************************
	created:	Friday 2026/07/18 at 05:00
	filename: 	PakArchive.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_filesystem/PakArchive.h"

#include <OgreDataStream.h>
#include <OgreString.h>
#include <OgreStringVector.h>

#include <cstring>
#include <sys/stat.h>

namespace Orkige
{
	//---------------------------------------------------------
	PakArchive::PakArchive(const Ogre::String& name, const Ogre::String& archType,
		const Ogre::String& zipPath, const Ogre::String& prefix)
		: Ogre::Archive(name, archType)
		, mZipPath(zipPath)
		, mPrefix(prefix)
	{
	}
	//---------------------------------------------------------
	PakArchive::~PakArchive()
	{
		unload();
	}
	//---------------------------------------------------------
	void PakArchive::load()
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		if(!this->mFileList.empty())
		{
			return;
		}
		if(!this->mZip.isOpen() && !this->mZip.open(this->mZipPath))
		{
			oDebugError("filesystem", 0, "PakArchive: cannot open pak '"
				<< this->mZipPath << "'");
			return;
		}
		const std::size_t prefixLength = this->mPrefix.length();
		for(std::map<std::string, MiniZip::Entry>::value_type const & pair :
			this->mZip.entries())
		{
			Ogre::String const & internalName = pair.first;
			if(prefixLength > 0)
			{
				if(internalName.length() < prefixLength ||
					internalName.compare(0, prefixLength, this->mPrefix) != 0)
				{
					continue;	// outside the mounted sub-tree
				}
			}
			Ogre::String rel = internalName.substr(prefixLength);
			if(rel.empty())
			{
				continue;	// the mount-point directory entry itself
			}
			Ogre::FileInfo out;
			out.archive = this;
			out.filename = rel;
			const std::size_t slash = rel.find_last_of('/');
			out.basename = (slash == Ogre::String::npos)
				? rel : rel.substr(slash + 1);
			out.path = (slash == Ogre::String::npos)
				? Ogre::String() : rel.substr(0, slash + 1);
			out.compressedSize =
				static_cast<std::size_t>(pair.second.compressedSize);
			out.uncompressedSize =
				static_cast<std::size_t>(pair.second.uncompressedSize);
			this->mFileList.push_back(out);
		}
	}
	//---------------------------------------------------------
	void PakArchive::unload()
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		this->mFileList.clear();
	}
	//---------------------------------------------------------
	Ogre::DataStreamPtr PakArchive::open(const Ogre::String& filename,
		bool readOnly) OPAK_ARCHIVE_CONST
	{
		(void)readOnly;	// the pak is read-only
		std::lock_guard<std::mutex> lock(this->mMutex);
		std::vector<unsigned char> bytes;
		if(!this->mZip.read(this->mPrefix + filename, bytes))
		{
			return Ogre::DataStreamPtr();
		}
		// hand the resource system a self-owned in-memory stream (a mounted
		// entry has no OS file handle); readOnly + freeOnClose so Ogre frees it
		Ogre::MemoryDataStreamPtr stream =
			std::make_shared<Ogre::MemoryDataStream>(filename,
				bytes.size(), true /*freeOnClose*/, true /*readOnly*/);
		if(!bytes.empty())
		{
			std::memcpy(stream->getPtr(), bytes.data(), bytes.size());
		}
		return stream;
	}
	//---------------------------------------------------------
	Ogre::DataStreamPtr PakArchive::create(const Ogre::String& filename)
	{
		OGRE_EXCEPT(Ogre::Exception::ERR_NOT_IMPLEMENTED,
			"Modification of pak archives is not supported",
			"PakArchive::create");
	}
	//---------------------------------------------------------
	void PakArchive::remove(const Ogre::String& filename)
	{
		(void)filename;	// read-only archive
	}
	//---------------------------------------------------------
	Ogre::StringVectorPtr PakArchive::list(bool recursive, bool dirs)
		OPAK_ARCHIVE_CONST
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		Ogre::StringVectorPtr ret = std::make_shared<Ogre::StringVector>();
		for(Ogre::FileInfo const & fi : this->mFileList)
		{
			if((dirs == (fi.compressedSize == std::size_t(-1))) &&
				(recursive || fi.path.empty()))
			{
				ret->push_back(fi.filename);
			}
		}
		return ret;
	}
	//---------------------------------------------------------
	Ogre::FileInfoListPtr PakArchive::listFileInfo(bool recursive, bool dirs)
		OPAK_ARCHIVE_CONST
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		Ogre::FileInfoListPtr ret = std::make_shared<Ogre::FileInfoList>();
		for(Ogre::FileInfo const & fi : this->mFileList)
		{
			if((dirs == (fi.compressedSize == std::size_t(-1))) &&
				(recursive || fi.path.empty()))
			{
				ret->push_back(fi);
			}
		}
		return ret;
	}
	//---------------------------------------------------------
	Ogre::StringVectorPtr PakArchive::find(const Ogre::String& pattern,
		bool recursive, bool dirs) OPAK_ARCHIVE_CONST
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		Ogre::StringVectorPtr ret = std::make_shared<Ogre::StringVector>();
		const bool fullMatch = (pattern.find('/') != Ogre::String::npos) ||
			(pattern.find('\\') != Ogre::String::npos);
		for(Ogre::FileInfo const & fi : this->mFileList)
		{
			if((dirs == (fi.compressedSize == std::size_t(-1))) &&
				(recursive || fullMatch || fi.path.empty()))
			{
				if(Ogre::StringUtil::match(
					fullMatch ? fi.filename : fi.basename, pattern, false))
				{
					ret->push_back(fi.filename);
				}
			}
		}
		return ret;
	}
	//---------------------------------------------------------
	Ogre::FileInfoListPtr PakArchive::findFileInfo(const Ogre::String& pattern,
		bool recursive, bool dirs) OPAK_ARCHIVE_CONST
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		Ogre::FileInfoListPtr ret = std::make_shared<Ogre::FileInfoList>();
		const bool fullMatch = (pattern.find('/') != Ogre::String::npos) ||
			(pattern.find('\\') != Ogre::String::npos);
		for(Ogre::FileInfo const & fi : this->mFileList)
		{
			if((dirs == (fi.compressedSize == std::size_t(-1))) &&
				(recursive || fullMatch || fi.path.empty()))
			{
				if(Ogre::StringUtil::match(
					fullMatch ? fi.filename : fi.basename, pattern, false))
				{
					ret->push_back(fi);
				}
			}
		}
		return ret;
	}
	//---------------------------------------------------------
	bool PakArchive::exists(const Ogre::String& filename) OPAK_ARCHIVE_CONST
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		return this->mZip.contains(this->mPrefix + filename);
	}
	//---------------------------------------------------------
	time_t PakArchive::getModifiedTime(const Ogre::String& filename)
		OPAK_ARCHIVE_CONST
	{
		(void)filename;	// the pak has one mod time: the file on disk
		struct stat tagStat;
		if(stat(this->mZipPath.c_str(), &tagStat) == 0)
		{
			return tagStat.st_mtime;
		}
		return 0;
	}
}
