/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	RenderResourceReader.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __RenderResourceReader_h__20_7_2026__12_00_00__
#define __RenderResourceReader_h__20_7_2026__12_00_00__

#include "core_filesystem/ResourceReader.h"
#include "engine_render/RenderSystem.h"

namespace Orkige
{
	//! @brief the engine's ResourceReader: reads a named text resource through
	//! the archive-aware render resource system
	//! (RenderSystem::readResourceText), so a name resolves across loose files
	//! AND mounted paks/APKs identically. This is the concrete implementation
	//! AppHost installs into the core ResourceAccess provider at boot (after
	//! resource mounts are up), so a core loader (scripts today; scenes/config
	//! next) reads content IN PLACE from an archive with no fopen and no APK
	//! extraction. Backend-neutral: it names only the engine_render facade (no
	//! Ogre), so it works identically on both render flavors.
	class RenderResourceReader : public ResourceReader
	{
	public:
		bool readText(String const & name, String & out) const override
		{
			RenderSystem * render = RenderSystem::get();
			return render != nullptr && render->readResourceText(name, out);
		}
	};
}

#endif //__RenderResourceReader_h__20_7_2026__12_00_00__
