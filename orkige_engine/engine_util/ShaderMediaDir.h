/**************************************************************
	created:	2026/08/08 at 10:00
	filename: 	ShaderMediaDir.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ShaderMediaDir_h__8_8_2026__10_00_00__
#define __ShaderMediaDir_h__8_8_2026__10_00_00__

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//! @brief where a render flavor reads its shader SOURCE FILES from.
	//!
	//! Each flavor has a directory of shader text its backend builds from: the
	//! next flavor's Hlms templates and the classic flavor's shader library.
	//! The host derives that path (a baked build-tree default, or the media
	//! directory a bundled/exported run carries), and ONE environment variable
	//! per flavor may point it somewhere else - which is what lets a run read
	//! shader files it may edit, without ever writing into the engine's own
	//! media. Both flavors resolve it through this same function so the two
	//! seams cannot drift apart.
	//!
	//! The rule is the whole function: a non-empty override wins, anything else
	//! (unset, or set to the empty string) keeps the host's path. An override
	//! is never merged with the host path - a half-overridden shader directory
	//! would load half of one template set and half of another.
	//! @param hostPath the directory the host derived (may be empty)
	//! @param overrideValue the environment variable's value, NULL when unset
	//! @return the directory to read shader source files from
	inline String resolveShaderMediaDir(String const & hostPath,
		char const * overrideValue)
	{
		if(overrideValue != NULL && overrideValue[0] != '\0')
		{
			return String(overrideValue);
		}
		return hostPath;
	}
}

#endif //__ShaderMediaDir_h__8_8_2026__10_00_00__
