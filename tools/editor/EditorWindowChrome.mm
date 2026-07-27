/********************************************************************
	created:	Monday 2026/07/27 at 12:00
	filename: 	EditorWindowChrome.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorWindowChrome - native macOS implementation (see header for the
// rounded-corner rationale). ImGui-free by design: this only ever touches
// AppKit, the same seam engine_util/SDLNativeWindow.mm uses to pull the
// NSWindow out of an SDL3 window (SDL_PROP_WINDOW_COCOA_WINDOW_POINTER).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorWindowChrome.h"

#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>

namespace Orkige
{
	void setEditorWindowChromeBackground(SDL_Window* window,
		float r, float g, float b)
	{
		if (!window)
		{
			return;
		}
		NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
			SDL_GetWindowProperties(window),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
		if (!nswindow)
		{
			return;
		}
		// opaque first: an opaque window paints its own backgroundColor under
		// the corner mask, which is exactly the sliver this fixes - a
		// transparent window would let the desktop show through instead.
		[nswindow setOpaque:YES];
		[nswindow setBackgroundColor:[NSColor colorWithSRGBRed:r green:g
			blue:b alpha:1.0f]];
	}
}
