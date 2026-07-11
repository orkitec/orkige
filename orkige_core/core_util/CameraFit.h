/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	CameraFit.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CameraFit_h__11_7_2026__12_00_00__
#define __CameraFit_h__11_7_2026__12_00_00__

#include <algorithm>

namespace Orkige
{
	//! @brief pure orthographic 2D-camera fit policy: given a fit MODE, a design
	//! rectangle (the world-unit area a game is authored against) and the current
	//! viewport aspect (width/height), compute the orthographic vertical
	//! HALF-extent (what CameraComponent::orthoSize / Engine::setCameraOrthographic
	//! take - the camera then derives the visible width from the aspect).
	//! @remarks Renderer- and math-library-independent (plain floats) so it unit
	//! tests headlessly and is shared by the CameraComponent apply path and the
	//! script-driven window-camera path on both render flavors.
	namespace CameraFit
	{
		//! how a 2D camera reconciles the design rectangle with the viewport aspect
		enum FitMode
		{
			//! the authored half-height is honoured exactly; the visible WIDTH
			//! grows/shrinks with the aspect. The historical default - a taller
			//! screen shows the same height, a wider screen shows more sideways.
			FIT_HEIGHT = 0,
			//! a fixed design WIDTH is always fully visible; the half-height is
			//! derived so the width fits. A taller screen shows more vertically.
			FIT_WIDTH = 1,
			//! the WHOLE design rectangle is always fully visible; whichever axis
			//! has slack for the current aspect grows to show more world (never
			//! less than the design rect, never distorted).
			EXPAND = 2
		};

		//! @brief the orthographic vertical half-extent for the given fit.
		//! @param mode the reconciliation policy
		//! @param designWidth the design rectangle's FULL world width
		//! @param designHeight the design rectangle's FULL world height
		//! @param aspect the viewport aspect (width / height); clamped positive
		//! @return the vertical half-extent to feed the camera (always positive)
		inline float orthoHalfHeight(FitMode mode, float designWidth,
			float designHeight, float aspect)
		{
			// a degenerate aspect (headless/first frame) falls back to 1:1 so the
			// result stays finite and positive
			const float safeAspect = aspect > 1.0e-4f ? aspect : 1.0f;
			const float halfWidth = 0.5f * std::max(designWidth, 0.0f);
			const float halfHeight = 0.5f * std::max(designHeight, 0.0f);
			float result = halfHeight;
			switch(mode)
			{
			case FIT_WIDTH:
				// keep the full design width on screen: halfWidth = halfHeight*aspect
				result = halfWidth / safeAspect;
				break;
			case EXPAND:
				// satisfy BOTH: at least the design height, AND enough height that
				// the derived width (halfHeight*aspect) still covers the design width
				result = std::max(halfHeight, halfWidth / safeAspect);
				break;
			case FIT_HEIGHT:
			default:
				result = halfHeight;
				break;
			}
			// never zero: a zero design collapses the projection - clamp like
			// CameraComponent::setOrthoSize does
			return result > 0.001f ? result : 0.001f;
		}

		//! @brief the visible world rectangle for a given half-height + aspect
		//! (the selfcheck asserts against this): full visible width and height in
		//! world units.
		inline void visibleWorldSize(float orthoHalfExtent, float aspect,
			float & outWidth, float & outHeight)
		{
			const float safeAspect = aspect > 1.0e-4f ? aspect : 1.0f;
			outHeight = 2.0f * orthoHalfExtent;
			outWidth = outHeight * safeAspect;
		}

		//! @brief the centered design-aspect sub-rectangle of a viewport, in
		//! normalized 0..1 viewport coords (top-left origin) - the letterbox /
		//! pillarbox rect a game would draw bars around to crop the view to a
		//! fixed design aspect. Returned as pure math (engine draws no bars - see
		//! the fit-policy note in Docs); a full-viewport rect means no bars.
		//! @param designAspect the target aspect (width/height) to letterbox to
		//! @param viewportAspect the actual viewport aspect (width/height)
		inline void letterboxRect(float designAspect, float viewportAspect,
			float & outLeft, float & outTop, float & outWidth, float & outHeight)
		{
			const float safeDesign = designAspect > 1.0e-4f ? designAspect : 1.0f;
			const float safeView = viewportAspect > 1.0e-4f ? viewportAspect : 1.0f;
			outLeft = 0.0f;
			outTop = 0.0f;
			outWidth = 1.0f;
			outHeight = 1.0f;
			if(safeView > safeDesign)
			{
				// viewport wider than design -> pillarbox (bars left/right)
				outWidth = safeDesign / safeView;
				outLeft = 0.5f * (1.0f - outWidth);
			}
			else if(safeView < safeDesign)
			{
				// viewport taller than design -> letterbox (bars top/bottom)
				outHeight = safeView / safeDesign;
				outTop = 0.5f * (1.0f - outHeight);
			}
		}
	}
}

#endif //__CameraFit_h__11_7_2026__12_00_00__
