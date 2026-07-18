/********************************************************************
	created:	Friday 2026/07/18 at 05:00
	filename: 	PakArchive.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __PakArchive_h__18_7_2026__05_00_00__
#define __PakArchive_h__18_7_2026__05_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_filesystem/MiniZip.h"

#include <OgreArchive.h>

#include <mutex>

namespace Orkige
{
	//! The engine-neutral Archive interface differs in const-ness between the
	//! two Ogre flavors: classic marks the read accessors const, Ogre-Next does
	//! not. The whole pak subsystem compiles unchanged on both by matching each
	//! override to the active flavor's signature through this one alias.
#ifdef ORKIGE_RENDER_NEXT
	#define OPAK_ARCHIVE_CONST
#else
	#define OPAK_ARCHIVE_CONST const
#endif

	/** \addtogroup Filesystem
	*  @{
	*/
	//! @brief a read-only sub-tree view over a zip/pak archive.
	//! @remarks A PakArchive reads a zip through the engine's small MiniZip
	//! reader and exposes a sub-tree of it named by @p prefix, with @p prefix
	//! stripped from every entry name - so a mounted APK whose media lives under
	//! "assets/" (or a game .pak whose media lives under "game/") resolves by
	//! BARE resource name exactly like loose files. An empty prefix mounts the
	//! whole zip. MiniZip (not a bespoke reader per flavor) does the IO because
	//! the Ogre-Next build carries no zip support while classic does - one
	//! reader keeps the contract identical on both flavors (Docs/filesystem.md).
	//! Instances are made by the pak archive factory (engine_filesystem/
	//! PakMount.cpp) - application code mounts through RenderSystem::mountPak.
	class ORKIGE_ENGINE_DLL PakArchive : public Ogre::Archive
	{
		//--- Variables ---------------------------------------------
	protected:
		//! the zip reader over the pak file (or the mounted APK)
		MiniZip				mZip;
		//! the zip file on disk (or the mounted APK path)
		Ogre::String		mZipPath;
		//! the sub-tree prefix, "" or a "/"-terminated path within the zip
		Ogre::String		mPrefix;
		//! the remapped (prefix-stripped) entry list, built on load()
		Ogre::FileInfoList	mFileList;
		//! the underlying stock zip reader is not thread-safe; the read
		//! accessors are const on classic, so the guard is mutable
		mutable std::mutex	mMutex;
		//--- Methods -----------------------------------------------
	public:
		PakArchive(const Ogre::String& name, const Ogre::String& archType,
			const Ogre::String& zipPath, const Ogre::String& prefix);
		~PakArchive() override;

		bool isCaseSensitive() const override { return false; }

		void load() override;
		void unload() override;

		Ogre::DataStreamPtr open(const Ogre::String& filename,
			bool readOnly = true) OPAK_ARCHIVE_CONST override;
		Ogre::DataStreamPtr create(const Ogre::String& filename) override;
		void remove(const Ogre::String& filename) override;

		Ogre::StringVectorPtr list(bool recursive = true,
			bool dirs = false) OPAK_ARCHIVE_CONST override;
		Ogre::FileInfoListPtr listFileInfo(bool recursive = true,
			bool dirs = false) OPAK_ARCHIVE_CONST override;
		Ogre::StringVectorPtr find(const Ogre::String& pattern,
			bool recursive = true, bool dirs = false) OPAK_ARCHIVE_CONST override;
		Ogre::FileInfoListPtr findFileInfo(const Ogre::String& pattern,
			bool recursive = true, bool dirs = false) OPAK_ARCHIVE_CONST override;
		bool exists(const Ogre::String& filename) OPAK_ARCHIVE_CONST override;
		time_t getModifiedTime(const Ogre::String& filename) OPAK_ARCHIVE_CONST override;
	};
	//---------------------------------------------------------------
	/** @} */
}

#endif //__PakArchive_h__18_7_2026__05_00_00__
