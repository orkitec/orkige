/**************************************************************
	created:	2010/09/08 at 20:40
	filename: 	EnginePrerequisites.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __EnginePrerequisites_h__8_9_2010__20_40_50__
#define __EnginePrerequisites_h__8_9_2010__20_40_50__

//! @file EnginePrerequisites.h
//! @brief the BACKEND-NEUTRAL engine umbrella (Docs/render-abstraction.md)
//! @remarks Historically this header included the classic <Ogre.h> umbrella,
//! which chained every engine module to the classic render backend. It is
//! now backend-neutral: core prerequisites + the Meta/type system + the
//! facade math vocabulary (engine_render/RenderMath.h - Ogre math types on
//! both Ogre backends, the documented swap point before a non-Ogre backend).
//! Classic-only translation units (engine_graphic, engine_gui,
//! engine_filesystem's Ogre::Archive plumbing, the classic render backend)
//! include engine_module/EnginePrerequisitesClassic.h instead, which layers
//! the classic OGRE umbrella on top of this one.

#ifdef WIN32
#	if defined( ORKIGE_STATIC )
#   	define ORKIGE_ENGINE_DLL
#   else
#      if defined( __MINGW32__ )
#			define ORKIGE_ENGINE_DLL
#		else
#			pragma warning( disable : 4251)
#			ifdef orkige_engine_EXPORTS
#				define ORKIGE_ENGINE_DLL __declspec( dllexport )
#			else
#				define ORKIGE_ENGINE_DLL __declspec( dllimport )
#			endif
#		endif
#	endif
#else // Linux / Mac OSX etc
#	define ORKIGE_ENGINE_DLL
#endif

#include "core_module/OrkigePrerequisites.h"
#include "core_base/Meta.h"

// the engine math vocabulary (Orkige::Vec3 & friends over the Ogre math
// headers, identical on the classic and next backend - see the math
// decision in Docs/render-abstraction.md)
#include "engine_render/RenderMath.h"

void ORKIGE_ENGINE_DLL init_module_orkige_engine(void);

#include <core_debug/Profile.h>
#endif //__EnginePrerequisites_h__8_9_2010__20_40_50__
