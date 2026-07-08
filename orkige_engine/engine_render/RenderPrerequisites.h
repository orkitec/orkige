/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderPrerequisites.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderPrerequisites_h__8_7_2026__12_00_00__
#define __RenderPrerequisites_h__8_7_2026__12_00_00__

//! @file RenderPrerequisites.h
//! @brief shared ground for the engine_render facade headers
//! @remarks engine_render is the renderer-agnostic scene-graph facade
//! (Docs/render-abstraction.md). Its headers must stay free of backend
//! types: do NOT include engine_module/EnginePrerequisites.h here (it
//! pulls in all of <Ogre.h>) - facade headers include only core headers,
//! this file and engine_render/RenderMath.h. The backend is selected at
//! BUILD time via the ORKIGE_RENDER_BACKEND CMake option (classic|next);
//! exactly one backend links into a binary - classic OGRE and Ogre-Next
//! share symbol names, so mixing them violates the ODR. There is no
//! runtime backend switch for that reason.

#include "core_module/OrkigePrerequisites.h"
#include <core_util/optr.h>

//! same export rule as engine_module/EnginePrerequisites.h - duplicated
//! here (guarded) so facade headers do not need the Ogre-including
//! umbrella; inert while ORKIGE_STATIC builds everything statically
#ifndef ORKIGE_ENGINE_DLL
#	ifdef WIN32
#		if defined( ORKIGE_STATIC ) || defined( __MINGW32__ )
#			define ORKIGE_ENGINE_DLL
#		else
#			ifdef orkige_engine_EXPORTS
#				define ORKIGE_ENGINE_DLL __declspec( dllexport )
#			else
#				define ORKIGE_ENGINE_DLL __declspec( dllimport )
#			endif
#		endif
#	else // Linux / Mac OSX etc
#		define ORKIGE_ENGINE_DLL
#	endif
#endif //ORKIGE_ENGINE_DLL

namespace Orkige
{
	//--- facade forward declarations (one per engine_render header) ----
	class RenderSystem;
	class RenderWorld;
	class RenderNode;
	class MeshInstance;
	class SpriteQuad;
	class RenderCamera;
	class RenderLight;
	class RenderTexture;
}

#endif //__RenderPrerequisites_h__8_7_2026__12_00_00__
