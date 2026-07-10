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
	//! @remarks the window is created with SDL_WINDOW_HIGH_PIXEL_DENSITY: the
	//! engine policy is that render surfaces track the OS backing scale, so
	//! both flavors derive the SAME drawable size from the same window request
	//! (the render_backend_parity gate). The passed width/height are the
	//! LOGICAL (points) request; the drawable comes out points x backingScale.
	Orkige::RenderSystem* boot(unsigned int width, unsigned int height,
		Orkige::String const & logFileName);
	//! @brief the host window's LOGICAL size in points (what SDL_CreateWindow
	//! was asked for), independent of the backing scale. The parity driver
	//! cross-checks this against the backend's drawable (pixel) size so a
	//! flavor that mis-maps points->pixels is caught, not resized away.
	void getLogicalWindowSize(unsigned int & outWidth, unsigned int & outHeight);
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
