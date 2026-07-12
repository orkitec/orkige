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
//! @brief the CLASSIC-ONLY engine umbrella (Docs/render-abstraction.md)
//! @remarks The classic-only prerequisites: the
//! neutral engine umbrella plus the classic OGRE umbrella headers. Only
//! classic-gated translation units may include this (engine_graphic's
//! Engine/console/debug renderables,
//! engine_filesystem's Ogre::Archive subclasses,
//! engine_render_classic and the unbuilt legacy tools) - everything the
//! next flavor compiles sticks to the neutral umbrella. The flavor guard
//! below turns an accidental include on the next flavor into an honest
//! compile error instead of an Ogre-Next header-soup mismatch.

#ifdef ORKIGE_RENDER_NEXT
#	error "EnginePrerequisitesClassic.h included on the Ogre-Next flavor - this TU is classic-only (see the flavor gates in orkige_engine/CMakeLists.txt)"
#endif

#include "engine_module/EnginePrerequisites.h"

#include <Ogre.h>
#include <OgreFontManager.h>
#include <OgreBorderPanelOverlayElement.h>
#include <OgreTextAreaOverlayElement.h>
#include <OgreExternalTextureSourceManager.h>
#include <OgreWireBoundingBox.h>
#endif //__EnginePrerequisitesClassic_h__8_7_2026__12_00_00__
