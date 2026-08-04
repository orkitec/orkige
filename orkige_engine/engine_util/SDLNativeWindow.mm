/**************************************************************
	created:	2026/07/07
	filename: 	SDLNativeWindow.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// Bridges an SDL3 window to the native window pointer OGRE's render systems
// expect as "externalWindowHandle": the NSWindow on macOS, the UIWindow on
// iOS (OGRE's EAGL2 window attaches its own GLES view + view controller to it).
// Shared by every SDL-hosted Orkige app (hello_orkige, editor, player) -
// formerly duplicated per app, folded here when the player became the third
// caller. Apps declare: extern "C" void* orkige_native_window_handle(SDL_Window*).
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif
#include <SDL3/SDL.h>

#if !TARGET_OS_IPHONE
// Runs before main() in every app that references the bridge symbol below
// (all SDL-hosted Orkige apps): opt out of the system press-and-hold accent
// picker for THIS app only. Holding a key over the editor or a game must
// key-repeat (camera nudges, WASD), not open the accented-character popover
// the OS shows for text fields; registerDefaults is volatile and app-scoped,
// so the user's global preference is never written.
__attribute__((constructor)) static void orkigeDisablePressAndHoldAccents()
{
	[[NSUserDefaults standardUserDefaults]
		registerDefaults:@{ @"ApplePressAndHoldEnabled" : @NO }];
}
#endif

extern "C" void* orkige_native_window_handle(SDL_Window* window)
{
#if TARGET_OS_IPHONE
	UIWindow* uiwindow = (__bridge UIWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
	return (__bridge void*)uiwindow;
#else
	NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	return (__bridge void*)nswindow;
#endif
}

// The macOS half of PlatformWindow::hasDisplaySession - here rather than in
// PlatformWindow.cpp because the answer comes from CoreGraphics, and this is
// the file that already carries the platform's own frameworks.
#include "engine_util/PlatformWindow.h"

namespace Orkige
{
	namespace PlatformWindow
	{
		//---------------------------------------------------------
		bool hasDisplaySession()
		{
#if TARGET_OS_IPHONE
			// a device always has its screen; there is no session to lose
			return true;
#else
			// FAST USER SWITCHING: only the login session that owns the
			// console owns the window server. A process in a background
			// session gets no main display, and CGDisplayCopyAllDisplayModes
			// answers NULL - which is exactly the value the render backend's
			// macOS GL support hands to CFArrayGetCount without checking, so
			// the process dies inside CoreFoundation before any engine code
			// runs. Ask it here, where the answer can still become a sentence.
			CFArrayRef modes =
				CGDisplayCopyAllDisplayModes(CGMainDisplayID(), NULL);
			if(modes == NULL)
			{
				return false;
			}
			CFRelease(modes);
			return true;
#endif
		}
	}
}
