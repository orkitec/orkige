/********************************************************************
	created:	Wednesday 2012/02/22 at 17:22
	filename: 	BigZipArchive.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_filesystem/BigZipArchive.h"
#include "engine_filesystem/BigZipArchiveFactory.h"

#include <sys/stat.h>

namespace Orkige
{

	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	//-----------------------------------------------------------------------
	BigZipArchive::BigZipArchive(const Ogre::String& name, const Ogre::String& archType )
		: Ogre::Archive(name, archType)
	{
	}
	//-----------------------------------------------------------------------
	BigZipArchive::~BigZipArchive()
	{
		unload();
	}
	//-----------------------------------------------------------------------
	void BigZipArchive::load()
	{
		OGRE_LOCK_AUTO_MUTEX
			if (this->mFileList.empty())
			{
				oDebugMsg("filesystem", 0, "-------------------------------");
				oDebugMsg("filesystem", 0, "Loading Archive: " << this->mName);
				Ogre::FileInfoListPtr fil = BigZipArchiveFactory::getSingleton().getFileInfoList(this->mName);
				String prefix = BigZipArchiveFactory::getSingleton().getPathPrefix() + this->mName;
				std::size_t prefixLength = prefix.length();
				for(Ogre::FileInfoList::iterator it = fil->begin(), itend = fil->end(); it != itend; it++)
				{
					//oDebugMsg("filesystem", 0, this->mName << " <- trying to add: " << it->filename);
					if(it->path.length() < prefixLength)
					{
						continue;
					}
					String subPath = it->path.substr(0, prefix.length());
					if(subPath != prefix)
					{
						//oDebugMsg("filesystem", 0, "1");
						continue;
					}
					//oDebugMsg("filesystem", 0, "2");
					String subFile = it->filename;
					if(subFile.find('/') != String::npos && subFile.length() > prefix.length())
					{
						//oDebugMsg("filesystem", 0, "3");
						subFile = it->filename.substr(0, prefix.length());
						if(subFile != prefix)
						{
							//oDebugMsg("filesystem", 0, "4");
							continue;
						}
						subFile = it->filename.substr(prefixLength, it->filename.length()-1);
					}

					//oDebugMsg("filesystem", 0, "5");
					

					Ogre::FileInfo fi;
					fi.archive = this;

					
					fi.filename = subFile;
					subPath = it->path.substr(prefixLength, it->path.length()-1);
					fi.path = subPath;
					fi.basename = it->basename;
					fi.compressedSize = it->compressedSize;
					fi.uncompressedSize = it->uncompressedSize;
					this->mFileList.push_back(fi);

					oDebugMsg("filesystem", 0, this->mName << " added: " << subFile);
				}
			}
	}
	//-----------------------------------------------------------------------
	void BigZipArchive::unload()
	{
		OGRE_LOCK_AUTO_MUTEX
		mFileList.clear();
	}
	//-----------------------------------------------------------------------
	Ogre::DataStreamPtr BigZipArchive::open(const Ogre::String& filename, bool readOnly) const
	{
		// zziplib is not threadsafe
		OGRE_LOCK_AUTO_MUTEX
		Ogre::DataStreamPtr dstr = BigZipArchiveFactory::getSingleton().open(this->mName + filename, readOnly);
		return dstr;
	}
	//---------------------------------------------------------------------
	Ogre::DataStreamPtr BigZipArchive::create(const Ogre::String& filename)
	{
		OGRE_EXCEPT(Ogre::Exception::ERR_NOT_IMPLEMENTED, 
			"Modification of zipped archives is not supported", 
			"BigZipArchive::create");

	}
	//---------------------------------------------------------------------
	void BigZipArchive::remove(const Ogre::String& filename)
	{
	}
	//-----------------------------------------------------------------------
	Ogre::StringVectorPtr BigZipArchive::list(bool recursive, bool dirs) const
	{
		OGRE_LOCK_AUTO_MUTEX
			Ogre::StringVectorPtr ret = std::make_shared<Ogre::StringVector>();

		Ogre::FileInfoList::const_iterator i, iend;
		iend = mFileList.end();
		for (i = mFileList.begin(); i != iend; ++i)
			if ((dirs == (i->compressedSize == size_t (-1))) &&
				(recursive || i->path.empty()))
				ret->push_back(i->filename);

		return ret;
	}
	//-----------------------------------------------------------------------
	Ogre::FileInfoListPtr BigZipArchive::listFileInfo(bool recursive, bool dirs) const
	{
		OGRE_LOCK_AUTO_MUTEX
			Ogre::FileInfoListPtr fil = std::make_shared<Ogre::FileInfoList>();
		Ogre::FileInfoList::const_iterator i, iend;
		iend = mFileList.end();
		for (i = mFileList.begin(); i != iend; ++i)
			if ((dirs == (i->compressedSize == size_t (-1))) &&
				(recursive || i->path.empty()))
				fil->push_back(*i);

		return fil;
	}
	//-----------------------------------------------------------------------
	Ogre::StringVectorPtr BigZipArchive::find(const Ogre::String& pattern, bool recursive, bool dirs) const
	{
		OGRE_LOCK_AUTO_MUTEX
			Ogre::StringVectorPtr ret = std::make_shared<Ogre::StringVector>();
		// If pattern contains a directory name, do a full match
		bool full_match = (pattern.find ('/') != Ogre::String::npos) ||
			(pattern.find ('\\') != Ogre::String::npos);

		Ogre::FileInfoList::const_iterator i, iend;
		iend = mFileList.end();
		for (i = mFileList.begin(); i != iend; ++i)
			if ((dirs == (i->compressedSize == size_t (-1))) &&
				(recursive || full_match || i->path.empty()))
				// Check basename matches pattern (zip is case insensitive)
				if (Ogre::StringUtil::match(full_match ? i->filename : i->basename, pattern, false))
					ret->push_back(i->filename);

		return ret;
	}
	//-----------------------------------------------------------------------
	Ogre::FileInfoListPtr BigZipArchive::findFileInfo(const Ogre::String& pattern, bool recursive, bool dirs) const
	{
		OGRE_LOCK_AUTO_MUTEX
			Ogre::FileInfoListPtr ret = std::make_shared<Ogre::FileInfoList>();
		// If pattern contains a directory name, do a full match
		bool full_match = (pattern.find ('/') != Ogre::String::npos) ||
			(pattern.find ('\\') != Ogre::String::npos);

		Ogre::FileInfoList::const_iterator i, iend;
		iend = mFileList.end();
		for (i = mFileList.begin(); i != iend; ++i)
			if ((dirs == (i->compressedSize == size_t (-1))) &&
				(recursive || full_match || i->path.empty()))
				// Check name matches pattern (zip is case insensitive)
				if (Ogre::StringUtil::match(full_match ? i->filename : i->basename, pattern, false))
					ret->push_back(*i);

		return ret;
	}
	//-----------------------------------------------------------------------
	bool BigZipArchive::exists(const Ogre::String& filename) const
	{
		// zziplib is not threadsafe
		OGRE_LOCK_AUTO_MUTEX
		return BigZipArchiveFactory::getSingleton().exists(this->mName + filename);
	}
	//---------------------------------------------------------------------
	time_t BigZipArchive::getModifiedTime(const Ogre::String& filename) const
	{
		// Zziplib doesn't yet support getting the modification time of individual files
		// so just check the mod time of the zip itself
		struct stat tagStat;
		bool ret = (stat(mName.c_str(), &tagStat) == 0);

		if (ret)
		{
			return tagStat.st_mtime;
		}
		else
		{
			return 0;
		}

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
