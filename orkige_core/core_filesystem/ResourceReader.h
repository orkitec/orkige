/********************************************************************
	created:	Sunday 2026/07/20 at 12:00
	filename: 	ResourceReader.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ResourceReader_h__20_7_2026__12_00_00__
#define __ResourceReader_h__20_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Filesystem
	*  @{ */

	//! @brief a backend-neutral reader for named CONTENT resources. Given a
	//! resource NAME - a project-relative path like "scripts/player.lua",
	//! "scenes/level.oscene" or a config file name - it returns that
	//! resource's text.
	//!
	//! WHY it exists: a core loader (a script, a scene, a config file) that
	//! reads with `fopen` needs a REAL file on disk - which forces an Android
	//! build to EXTRACT its content tree out of the APK before anything can
	//! open it. The engine's resource system already resolves a name across
	//! loose files AND mounted paks/APKs identically; wrapping that resolution
	//! behind this pure interface lets a core loader read the SAME content in
	//! place from a mounted archive, with no `fopen` and no extraction.
	//!
	//! It is a PURE core interface (no engine/renderer dependency): a core
	//! loader depends only on this header, and the engine supplies the concrete
	//! implementation (over its archive-aware resource read) through the
	//! ResourceAccess provider below. Kept GENERAL on purpose - scripts are the
	//! first consumer; SceneSerializer, the project manifest loader and the
	//! localisation string table are the intended next consumers, with no
	//! interface change.
	struct ResourceReader
	{
		//! @brief read the named resource's text into @p out.
		//! @return false (and @p out left untouched) when the name resolves
		//! nowhere - an honest miss the caller handles, typically by falling
		//! back to its own `fopen` path.
		virtual bool readText(String const & name, String & out) const = 0;

		virtual ~ResourceReader() = default;
	};

	//! @brief the process-wide provider seam for the injected ResourceReader.
	//! A core loader reaches the reader through here WITHOUT threading a
	//! ResourceReader* through every load call and every loader's construction
	//! - the same process-wide-accessor shape the engine already uses for its
	//! singletons (RenderSystem::get(), the ScriptRuntime registry).
	//!
	//! The engine sets it ONCE at boot, AFTER the resource mounts are up, and
	//! clears it at teardown; the pointer is NON-OWNING (the engine owns the
	//! implementation object). CONTRACT: when unset (reader() == nullptr) a
	//! caller MUST fall back to its existing `fopen` path - so headless core
	//! tests and loose-file dev keep working with no provider installed, and a
	//! reader that MISSES a name leaves the caller free to fall back too.
	class ORKIGE_CORE_DLL ResourceAccess
	{
	public:
		//! @brief set (or clear, with @p reader == nullptr) the process-wide
		//! reader. Non-owning: the caller keeps ownership and must clear this
		//! before the reader is destroyed.
		static void setReader(ResourceReader * reader);
		//! @brief the process-wide reader, or nullptr when none is installed.
		static ResourceReader * reader();
	private:
		ResourceAccess() = delete;
	};

	/** @} */
}

#endif //__ResourceReader_h__20_7_2026__12_00_00__
