/********************************************************************
	created:	Sunday 2026/08/03 at 12:00
	filename: 	RenderSystemSelection.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef _RENDERSYSTEMSELECTION_H_20260803_
#define _RENDERSYSTEMSELECTION_H_20260803_

#include "engine_render/RenderPrerequisites.h"
#include <core_util/String.h>

//! @file RenderSystemSelection.h
//! @brief which render system a process boots - the ONE name vocabulary
//! @remarks `ORKIGE_RENDERSYSTEM` names the graphics API a run wants. One
//! word in that vocabulary is not a graphics API at all: DEVICELESS asks for
//! a render system with no window and no GPU, so a process can hold a live
//! scene (transforms live in the render node graph) on a machine with no
//! display server. The parse is pure and unit-tested; the env read and the
//! per-flavor availability answer sit beside it so every host asks ONE place.

namespace Orkige
{
	namespace RenderSystemSelection
	{
		//! @brief does this ORKIGE_RENDERSYSTEM word ask for the deviceless
		//! render system? (case-insensitive, surrounding blanks ignored)
		//! @remarks PURE - the headless unit suite drives it directly.
		bool isDevicelessName(String const & name);

		//! @brief has this process been asked to boot deviceless?
		//! (reads ORKIGE_RENDERSYSTEM once per call; unset = false)
		bool devicelessRequested();

		//! @brief can THIS build honour a deviceless boot?
		//! @remarks a compile-time flavor fact, not a runtime probe: the
		//! Ogre-Next flavor links the deviceless render system beside its
		//! graphics one, the classic flavor does not carry it.
		bool devicelessAvailable();
	}
}

#endif
