/********************************************************************
	created:	Friday 2026/07/18 at 05:00
	filename: 	PakMount.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __PakMount_h__18_7_2026__05_00_00__
#define __PakMount_h__18_7_2026__05_00_00__

#include "engine_module/EnginePrerequisites.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief the engine's pak-mount seam: register a zip/pak archive with the
	//! resource system so its contents resolve like loose files, on BOTH render
	//! flavors (Docs/filesystem.md). The pak is read through each flavor's STOCK
	//! Ogre "Zip" archive (PakArchive wraps it), so no bespoke zip reader ships.
	//! Application code mounts through the backend-neutral RenderSystem::mountPak
	//! facade, which forwards here; this namespace keeps the Ogre plumbing in the
	//! sanctioned engine_filesystem zone.
	namespace PakMount
	{
		//! @brief the Ogre archive-type name pak locations register under
		extern const String ARCHIVE_TYPE;	//!< "OrkigePak"

		//! @brief normalize a mount point into a sub-tree prefix: a leading
		//! "./" is dropped, "\\" become "/", and a non-empty result is made to
		//! end with exactly one "/". An empty (or "/"-only) mount point returns
		//! "" - mount the whole zip. Pure string logic, unit-tested without a
		//! renderer (MountPointTests).
		String normalizeMountPoint(String const & mountPoint);

		//! @brief mount @p pakPath (optionally only its @p mountPoint sub-tree)
		//! into resource group @p groupName ("" = the default group). Idempotent
		//! per (path, mountPoint, group): a repeat mount is a no-op. Registers
		//! the pak archive factory with Ogre::ArchiveManager on first use.
		void mount(String const & pakPath, String const & mountPoint,
			String const & groupName);

		//! @brief unmount a previously mounted pak (idempotent - unmounting one
		//! that was never mounted is a no-op).
		void unmount(String const & pakPath, String const & mountPoint,
			String const & groupName);
	}
}

#endif //__PakMount_h__18_7_2026__05_00_00__
