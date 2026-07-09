/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	SelfcheckBootstrap.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SelfcheckBootstrap_h__8_7_2026__18_00_00__
#define __SelfcheckBootstrap_h__8_7_2026__18_00_00__

//! @file SelfcheckBootstrap.h
//! @brief the per-backend bootstrap seam of the render facade selfcheck
//! @remarks The selfcheck's CHECKS (selfcheck_main.cpp) are backend-
//! agnostic by construction - they include ONLY engine_render facade
//! headers plus this seam. What a backend cannot express through the
//! facade is exactly its bootstrap: window/host plumbing and whatever
//! media its shader pipeline needs registered before startup. Each
//! backend contributes one bootstrap TU implementing these four
//! functions (classic: bootstrap_classic.cpp; Ogre-Next adds
//! bootstrap_next.cpp - see Docs/render-abstraction.md).

#include <engine_render/RenderPrerequisites.h>
#include <core_util/String.h>

namespace SelfcheckBootstrap
{
	//! @brief bring the render backend up with a real window of the given
	//! size (incl. whatever internal media/shader libs the backend needs)
	//! @return the live render system, or NULL on failure (test aborts)
	Orkige::RenderSystem* boot(unsigned int width, unsigned int height,
		Orkige::String const & logFileName);
	//! @brief pump the host window/event loop once (per rendered frame)
	//! @param outQuitRequested set when the host asks the app to close
	void pumpHostEvents(bool & outQuitRequested);
	//! @brief tear the backend and the window down again (all facade
	//! handles must be released before this runs)
	void shutdown();
	//! @brief verification plumbing: does the image file contain at least
	//! one clearly non-black pixel? (backend-implemented so the agnostic
	//! checks need no image decoder of their own)
	bool imageHasNonBlackPixel(Orkige::String const & fileName);
	//! @brief verification plumbing: read one pixel of an image file (the
	//! DrawLayer2D pattern checks); false when the file cannot be decoded
	//! or the coordinates are out of range
	bool readImagePixel(Orkige::String const & fileName,
		unsigned int x, unsigned int y,
		float & outRed, float & outGreen, float & outBlue);
}

#endif //__SelfcheckBootstrap_h__8_7_2026__18_00_00__
