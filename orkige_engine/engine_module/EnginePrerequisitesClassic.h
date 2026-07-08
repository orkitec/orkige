/**************************************************************
	created:	2026/07/08 at 12:00
	filename: 	EnginePrerequisitesClassic.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EnginePrerequisitesClassic_h__8_7_2026__12_00_00__
#define __EnginePrerequisitesClassic_h__8_7_2026__12_00_00__

//! @file EnginePrerequisitesClassic.h
//! @brief the CLASSIC-ONLY engine umbrella (phase B3, Docs/render-abstraction.md)
//! @remarks What engine_module/EnginePrerequisites.h was before B3: the
//! neutral engine umbrella plus the classic OGRE umbrella headers. Only
//! classic-gated translation units may include this (engine_graphic's
//! Engine/console/debug renderables,
//! engine_filesystem's Ogre::Archive subclasses, engine_base/Localisation,
//! engine_render_classic and the unbuilt legacy tools) - everything the
//! next flavor compiles sticks to the neutral umbrella. The flavor guard
//! below turns an accidental include on the next flavor into an honest
//! compile error instead of an Ogre-Next header-soup mismatch.

#ifdef ORKIGE_RENDER_NEXT
#	error "EnginePrerequisitesClassic.h included on the Ogre-Next flavor - this TU is classic-only (see the flavor gates in orkige_engine/CMakeLists.txt)"
#endif

#include "engine_module/EnginePrerequisites.h"

#include <core_debug/DisableMemoryManager.h>

#include <Ogre.h>
#include <OgreFontManager.h>
#include <OgreBorderPanelOverlayElement.h>
#include <OgreTextAreaOverlayElement.h>
#include <OgreExternalTextureSourceManager.h>
#include <OgreWireBoundingBox.h>

#ifdef ORKIGE_ENABLE_MYGUI
#	include <MyGUI.h>
#	include <MyGUI_OgrePlatform.h>
#	include <Common/MessageBox/MessageBox.h>
	namespace MyGUI
	{
		typedef Message* MessagePtr;
	}
#endif //ORKIGE_ENABLE_MYGUI

#include <core_debug/EnableMemoryManager.h>
#endif //__EnginePrerequisitesClassic_h__8_7_2026__12_00_00__
