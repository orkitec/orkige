/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	PlatformWindow.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_util/PlatformWindow.h"

#include <SDL3/SDL_video.h>

#include <cmath>

namespace Orkige
{
	namespace PlatformWindow
	{
		namespace
		{
			//! the app-owned SDL window the render surface draws into
			SDL_Window*		gActiveWindow		= NULL;
			//! forced content scale for headless selfchecks (<= 0 = no override)
			float			gContentScaleOverride = 0.0f;
			//! forced safe-area insets for headless selfchecks
			bool			gHasSafeAreaOverride = false;
			SafeAreaInsets	gSafeAreaOverride;
		}
		//---------------------------------------------------------
		void setActiveWindow(void* sdlWindow)
		{
			gActiveWindow = static_cast<SDL_Window*>(sdlWindow);
		}
		//---------------------------------------------------------
		void* getActiveWindow()
		{
			return gActiveWindow;
		}
		//---------------------------------------------------------
		SafeAreaInsets getSafeAreaInsets(unsigned int surfaceWidth,
			unsigned int surfaceHeight)
		{
			if(gHasSafeAreaOverride)
			{
				return gSafeAreaOverride;
			}
			SafeAreaInsets zero;
			if(gActiveWindow == NULL)
			{
				return zero;
			}
			int windowPointsW = 0;
			int windowPointsH = 0;
			SDL_GetWindowSize(gActiveWindow, &windowPointsW, &windowPointsH);
			if(windowPointsW <= 0 || windowPointsH <= 0)
			{
				return zero;
			}
			SDL_Rect safe = { 0, 0, windowPointsW, windowPointsH };
			if(!SDL_GetWindowSafeArea(gActiveWindow, &safe))
			{
				return zero;	// honest zero on platforms without a safe area
			}
			// SDL reports the safe rect in window POINTS; the surface extent is
			// PIXELS. Scale per axis so the insets land in the surface's pixel
			// space (the same space getWindowWidth answers in).
			const double scaleX =
				static_cast<double>(surfaceWidth) / windowPointsW;
			const double scaleY =
				static_cast<double>(surfaceHeight) / windowPointsH;
			const int pixelX = static_cast<int>(std::lround(safe.x * scaleX));
			const int pixelY = static_cast<int>(std::lround(safe.y * scaleY));
			const int pixelW = static_cast<int>(std::lround(safe.w * scaleX));
			const int pixelH = static_cast<int>(std::lround(safe.h * scaleY));
			return SafeAreaInsets::fromSafeRect(surfaceWidth, surfaceHeight,
				pixelX, pixelY, pixelW, pixelH);
		}
		//---------------------------------------------------------
		float getContentScale()
		{
			if(gContentScaleOverride > 0.0f)
			{
				return gContentScaleOverride;
			}
			if(gActiveWindow == NULL)
			{
				return 1.0f;
			}
			const float scale = SDL_GetWindowDisplayScale(gActiveWindow);
			return scale > 0.0f ? scale : 1.0f;
		}
		//---------------------------------------------------------
		void setContentScaleOverride(float scale)
		{
			gContentScaleOverride = scale > 0.0f ? scale : 0.0f;
		}
		//---------------------------------------------------------
		void setSafeAreaInsetsOverride(SafeAreaInsets const & insets)
		{
			gSafeAreaOverride = insets;
			gHasSafeAreaOverride = true;
		}
		//---------------------------------------------------------
		void clearSafeAreaInsetsOverride()
		{
			gHasSafeAreaOverride = false;
			gSafeAreaOverride = SafeAreaInsets();
		}
	}
}
