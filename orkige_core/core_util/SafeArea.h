/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	SafeArea.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SafeArea_h__10_7_2026__12_00_00__
#define __SafeArea_h__10_7_2026__12_00_00__

//! @file SafeArea.h
//! @brief backend-neutral safe-area model: the display insets a notch /
//! rounded corner / home indicator eats into, plus the pure anchor math that
//! places a UI rect inside the safe box. No renderer or platform types - the
//! Engine fills SafeAreaInsets from the window system, the gui layer and a
//! unit test share UiAnchor::place.

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	//! @brief window-safe insets in PIXELS (notch / rounded corners / home
	//! indicator). left/top/right/bottom shrink the drawable rect from each
	//! edge; all zero on an unnotched desktop window.
	struct ORKIGE_CORE_DLL SafeAreaInsets
	{
		unsigned int	mLeft	= 0;
		unsigned int	mTop	= 0;
		unsigned int	mRight	= 0;
		unsigned int	mBottom	= 0;

		//! @brief derive the four edge insets from a safe rect that sits INSIDE
		//! a surface of the given pixel extent (safe.x/y/w/h in the SAME pixel
		//! space as the surface). The pure arithmetic the platform query funnels
		//! through - unit tested without a window.
		static SafeAreaInsets fromSafeRect(unsigned int surfaceWidth,
			unsigned int surfaceHeight, int safeX, int safeY,
			int safeWidth, int safeHeight);
	};

	//! @brief pure anchoring math for the gui layer (and its unit test):
	//! places a rect inside the safe box so HUD/menu content never draws under
	//! a notch or the home indicator.
	struct ORKIGE_CORE_DLL UiAnchor
	{
		//! @brief top-left pixel position of a `sizeW x sizeH` rect anchored
		//! inside the safe box of a `windowW x windowH` surface.
		//! @param anchorRight hug the right safe edge (else the left)
		//! @param anchorBottom hug the bottom safe edge (else the top)
		//! @param marginX,marginY extra gap in pixels from the anchored edge
		//! @remarks the result is clamped so the rect never crosses an inset it
		//! is anchored to; a rect wider/taller than the safe box pins to the
		//! left/top safe edge. All inputs and outputs are pixels.
		static void place(float sizeW, float sizeH, float marginX, float marginY,
			unsigned int windowW, unsigned int windowH,
			SafeAreaInsets const & insets, bool anchorRight, bool anchorBottom,
			float & outX, float & outY);
	};
}

#endif //__SafeArea_h__10_7_2026__12_00_00__
