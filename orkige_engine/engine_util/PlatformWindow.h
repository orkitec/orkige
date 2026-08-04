/********************************************************************
	created:	Friday 2026/07/10 at 12:00
	filename: 	PlatformWindow.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __PlatformWindow_h__10_7_2026__12_00_00__
#define __PlatformWindow_h__10_7_2026__12_00_00__

//! @file PlatformWindow.h
//! @brief the app-owned SDL window seen by the engine: the app (player /
//! editor / samples) registers its SDL_Window* here after creating it, and the
//! Engine queries the display's content scale and safe-area insets through it
//! on BOTH render flavors. Kept out of the widely-included Engine.h so no SDL
//! type leaks there; the SDL calls live in the .cpp. A headless override seam
//! lets selfchecks force a content scale / inset without a real window.

#include "engine_module/EnginePrerequisites.h"
#include "core_util/SafeArea.h"

namespace Orkige
{
	//! @brief a thin accessor over the one SDL window the render surface uses.
	//! Pure functions over a file-static handle - no ownership, no lifetime.
	namespace PlatformWindow
	{
		//! register the app's SDL_Window* (opaque here); pass NULL to detach
		ORKIGE_ENGINE_DLL void setActiveWindow(void* sdlWindow);
		//! the registered SDL_Window* (NULL when none) - opaque void*
		ORKIGE_ENGINE_DLL void* getActiveWindow();

		//! @brief safe-area insets mapped into the render surface's PIXEL space.
		//! @param surfaceWidth,surfaceHeight the surface pixel extent (the
		//! caller passes RenderSystem::getWindowSize so the insets share the
		//! coordinate space UI layout uses). SDL reports the safe rect in window
		//! points; this scales it to surface pixels. All-zero when no window is
		//! registered or the platform has no safe-area concept.
		ORKIGE_ENGINE_DLL SafeAreaInsets getSafeAreaInsets(
			unsigned int surfaceWidth, unsigned int surfaceHeight);
		//! @brief the display content scale (SDL_GetWindowDisplayScale): 1.0 on
		//! standard-DPI, ~2-3 on retina / phone. 1.0 fallback when unknown.
		ORKIGE_ENGINE_DLL float getContentScale();

		//! @brief can this PROCESS reach a display at all? Asked BEFORE a
		//! render system is installed, because on macOS the answer decides
		//! between a refusal and a crash.
		//!
		//! With fast user switching only the ACTIVE login session owns the
		//! window server. A process in a background session has no main
		//! display, so CoreGraphics answers a NULL display-mode list, and the
		//! GL support code in the render backend dereferences it before any
		//! engine code runs. Asking the same question here turns that into a
		//! sentence a caller can act on. True where the platform has no such
		//! notion, so nothing outside macOS changes behaviour.
		ORKIGE_ENGINE_DLL bool hasDisplaySession();

		//--- headless test seam ----------------------------------------
		//! @brief provoke the X11 inter-client selection race, so a selfcheck
		//! can prove the X error guard survives it. Asks THIS process - which
		//! must own the clipboard - for the selection from a throwaway second
		//! display connection, then drops that connection before the answer is
		//! written: the reply names a window the server has already reaped and
		//! the server answers with an asynchronous BadWindow, which Xlib's
		//! default handler would turn into process death. Returns false where
		//! there is nothing to probe (no X11, or no display to open).
		ORKIGE_ENGINE_DLL bool probeDeadClipboardRequestor();
		//! force the content scale regardless of any window (<= 0 clears)
		ORKIGE_ENGINE_DLL void setContentScaleOverride(float scale);
		//! force the safe-area insets (in surface pixels) regardless of window
		ORKIGE_ENGINE_DLL void setSafeAreaInsetsOverride(
			SafeAreaInsets const & insets);
		//! drop the forced safe-area insets
		ORKIGE_ENGINE_DLL void clearSafeAreaInsetsOverride();
	}
}

#endif //__PlatformWindow_h__10_7_2026__12_00_00__
