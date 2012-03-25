/********************************************************************
	created:	Tuesday 2012/02/21 at 20:12
	filename: 	BigZipArchive.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __BigZipArchive_h__21_2_2012__20_12_01__
#define __BigZipArchive_h__21_2_2012__20_12_01__

#include "engine_filesystem/BigZipArchiveFactory.h"

namespace Orkige
{
	/** \addtogroup Filesystem
	*  @{
	*/
	/** Specialisation of the ZipArchive class to allow reading of files from a single zip
	format source archive.
	@remarks
	This archive format supports all archives compressed in the standard
	zip format, including iD pk3 files.
	*/
	class ORKIGE_ENGINE_DLL BigZipArchive : public Ogre::Archive 
	{
		//--- Types -------------------------------------------------
	public:
	protected:
		//! File list (since zziplib seems to only allow scanning of dir tree once)
		Ogre::FileInfoList mFileList;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		BigZipArchive(const Ogre::String& name, const Ogre::String& archType );
		~BigZipArchive();
		//! @copydoc Archive::isCaseSensitive
		bool isCaseSensitive(void) const { return false; }

		//! @copydoc Archive::load
		void load();
		//! @copydoc Archive::unload
		void unload();

		//! @copydoc Archive::open
		Ogre::DataStreamPtr open(const Ogre::String& filename, bool readOnly = true) const;

		//! @copydoc Archive::create
		Ogre::DataStreamPtr create(const Ogre::String& filename) const;

		//! @copydoc Archive::remove
		void remove(const String& filename) const;

		//! @copydoc Archive::list
		Ogre::StringVectorPtr list(bool recursive = true, bool dirs = false);

		//! @copydoc Archive::listFileInfo
		Ogre::FileInfoListPtr listFileInfo(bool recursive = true, bool dirs = false);

		//! @copydoc Archive::find
		Ogre::StringVectorPtr find(const String& pattern, bool recursive = true, bool dirs = false);
#if OGRE_VERSION_MINOR >= 8
		//! @copydoc Archive::findFileInfo
		Ogre::FileInfoListPtr findFileInfo(const Ogre::String& pattern, bool recursive = true, bool dirs = false) const;
#else
		//! @copydoc Archive::findFileInfo
		Ogre::FileInfoListPtr findFileInfo(const Ogre::String& pattern, bool recursive = true, bool dirs = false);
#endif
		//! @copydoc Archive::exists
		bool exists(const Ogre::String& filename);

		//! @copydoc Archive::getModifiedTime
		time_t getModifiedTime(const Ogre::String& filename);
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__BigZipArchive_h__21_2_2012__20_12_01__
