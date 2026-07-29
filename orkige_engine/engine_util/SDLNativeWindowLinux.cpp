/**************************************************************
	created:	2026/07/09
	filename: 	SDLNativeWindowLinux.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// Bridges an SDL3 window to the native window handle OGRE's render systems
// expect on Linux - the desktop counterpart of engine_util/SDLNativeWindow.mm
// (macOS/iOS) and SDLNativeWindowAndroid.cpp, same contract, declared by apps
// as: extern "C" void* orkige_native_window_handle(SDL_Window*). The caller
// stringifies the returned pointer value (decimal size_t) and hands it to the
// engine boot, so WHAT the pointer means is per render backend:
//
//   classic (OGRE 14 GLX):  the X11 Window id itself, cast into the void*.
//     GLXWindow parses the decimal back into an XID; Engine.cpp passes it as
//     parentWindowHandle/externalWindowHandle (the OgreBites-SDL embed
//     pattern: OGRE creates its GL child window inside the SDL window).
//   next (Ogre-Next Vulkan): a pointer to a persistent {Display*, ::Window}
//     pair - VulkanXcbWindow's external-window path ("SDL2x11" misc param)
//     dereferences exactly that struct layout (it is SDL2's SDL_SysWMinfo
//     x11 shape, which SDL3 no longer provides - rebuilt here from the SDL3
//     window properties).
//
// X11 only: both backends attach to X11 window ids, so this TU also steers
// SDL towards the x11 video driver (XWayland covers Wayland desktops; the
// SDL_VIDEO_DRIVER env var still overrides the hint) and installs the X error
// guard below - the two process-wide X11 facts every SDL-hosted app needs.
// TODO(linux): native Wayland needs (a) a Wayland-capable render path -
// classic GLX cannot do it, Ogre-Next 3.0 has no Vulkan Wayland windowing -
// and (b) a wl_surface branch here. Revisit when a backend can consume it.
#include <SDL3/SDL.h>
#include <core_debug/DebugMacros.h>
// the selection-race test seam declared for every platform (the X11 body of
// probeDeadClipboardRequestor lives at the bottom of this file, beside the
// guard it exercises). Included BEFORE the X11 headers - their Bool/None/
// Status macros must not reach the engine headers.
#include "engine_util/PlatformWindow.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <ios>

#include <X11/Xlib.h>
#include <X11/Xproto.h>

namespace
{
	//! the error handler standing before ours; NULL means Xlib's own default,
	//! which prints the request and terminates the process
	int (*sPreviousX11ErrorHandler)(Display*, XErrorEvent*) = NULL;
	//! reported races so far - a burst stays bounded in the log. Atomic: the
	//! handler runs on whichever thread is talking to the server, and the
	//! render backend has its own Display connection.
	std::atomic<int> sSelectionRaceCount(0);
	const int SELECTION_RACE_LOG_LIMIT = 4;

	//! @brief is this error the inter-client selection race?
	//! The X11 selection protocol has the clipboard OWNER answer a request by
	//! writing the data onto the REQUESTOR's window (X_ChangeProperty) and
	//! notifying it (X_SendEvent). Nothing keeps the requestor alive in
	//! between: when it exits first - a second editor or player process ending
	//! its run, a clipboard viewer closing - the answer names a window the
	//! server has already reaped and the server reports BadWindow. The error
	//! describes the peer's lifetime, not this process' state, and there is no
	//! way to avoid it: the window can die after the request is checked.
	bool isSelectionAnswerRace(XErrorEvent const * error)
	{
		return error->error_code == BadWindow &&
			(error->request_code == X_ChangeProperty ||
				error->request_code == X_SendEvent);
	}

	//! @brief the process-wide X error handler (Xlib has exactly one, shared by
	//! every Display connection). X protocol errors arrive asynchronously, so
	//! this runs on whichever thread next talks to the server - long after the
	//! call that caused it. Tolerated races return 0 (Xlib carries on);
	//! everything else keeps the verdict it had before.
	int orkigeX11ErrorHandler(Display* display, XErrorEvent* error)
	{
		if(isSelectionAnswerRace(error))
		{
			const int seen =
				sSelectionRaceCount.fetch_add(1, std::memory_order_relaxed) + 1;
			if(seen <= SELECTION_RACE_LOG_LIMIT)
			{
				oDebugWarn("platform", 0, "X11 BadWindow answering a clipboard "
					"request for window 0x" << std::hex <<
					static_cast<unsigned long>(error->resourceid) << std::dec <<
					" - the requesting process exited before its answer "
					"arrived; the clipboard transfer is dropped" <<
					(seen == SELECTION_RACE_LOG_LIMIT ?
						" (further occurrences stay silent)" : ""));
			}
			return 0;
		}
		if(sPreviousX11ErrorHandler != NULL)
		{
			return sPreviousX11ErrorHandler(display, error);
		}
		// Xlib's default handler is what stood here: keep its fatal verdict,
		// but say what happened in the engine's own voice first (an error also
		// lands in the crash breadcrumbs)
		char text[256] = { 0 };
		XGetErrorText(display, error->error_code, text, sizeof(text));
		oDebugError("platform", 0, "X11 protocol error: " << text <<
			" - request " << static_cast<int>(error->request_code) << "." <<
			static_cast<int>(error->minor_code) << ", resource 0x" <<
			std::hex << static_cast<unsigned long>(error->resourceid) <<
			std::dec << " - terminating");
		std::exit(EXIT_FAILURE);
	}
}

// Runs before main() in every app that references the bridge symbol below
// (all SDL-hosted Orkige apps): prefer X11 while both render backends speak
// only X11. SDL_SetHint is legal before SDL_Init and keeps normal priority,
// so a user's SDL_VIDEO_DRIVER environment variable still wins.
//
// The X error guard goes in with it. Xlib's default handler terminates the
// process on ANY protocol error, which makes the unavoidable clipboard race
// above fatal for the editor and the player alike; the guard downgrades that
// one class to a warning and leaves every other error exactly as fatal as it
// was. Installing it here (rather than after video init) keeps it standing for
// every Display connection in the process, including the render backend's own
// - SDL's short-lived internal handlers save and restore it around their own
// scopes, so it is the standing handler everywhere else.
__attribute__((constructor)) static void orkigeInitX11Platform()
{
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
	sPreviousX11ErrorHandler = XSetErrorHandler(orkigeX11ErrorHandler);
}

extern "C" void* orkige_native_window_handle(SDL_Window* window)
{
	SDL_PropertiesID properties = SDL_GetWindowProperties(window);
#if defined(ORKIGE_RENDER_NEXT)
	// VulkanXcbWindow's SDL2x11 struct: { Display* display; ::Window window; }
	// (::Window is an XID = unsigned long). Static: the render system reads
	// it during window _initialize, after the boot call returns nothing
	// keeps the pointer - but a static keeps it valid for the whole run
	// anyway (the engine models exactly one main window).
	static struct
	{
		void* display;			// Display* (Xlib)
		unsigned long xwindow;	// ::Window (XID)
	} sdlHandles;
	sdlHandles.display = SDL_GetPointerProperty(properties,
		SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
	sdlHandles.xwindow = static_cast<unsigned long>(SDL_GetNumberProperty(
		properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	if(!sdlHandles.display || !sdlHandles.xwindow)
	{
		const char* videoDriver = SDL_GetCurrentVideoDriver();
		oWarning("orkige_native_window_handle: no X11 handles on this SDL "
			"window (video driver: " << (videoDriver ? videoDriver : "?") <<
			") - the render system will create its own window; run under "
			"X11/XWayland");
		return NULL;
	}
	return &sdlHandles;
#else
	// classic GLX: the X11 Window id, pointer-encoded for the shared
	// stringify-a-size_t contract
	const uintptr_t xwindow = static_cast<uintptr_t>(SDL_GetNumberProperty(
		properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	if(!xwindow)
	{
		const char* videoDriver = SDL_GetCurrentVideoDriver();
		oWarning("orkige_native_window_handle: no X11 window number on this "
			"SDL window (video driver: " << (videoDriver ? videoDriver : "?") <<
			") - run under X11/XWayland");
	}
	return reinterpret_cast<void*>(xwindow);
#endif
}

namespace Orkige
{
	namespace PlatformWindow
	{
		//---------------------------------------------------------
		bool probeDeadClipboardRequestor()
		{
			Display* probeDisplay = XOpenDisplay(NULL);
			if(probeDisplay == NULL)
			{
				return false;	// no display to probe from
			}
			const Window probeWindow = XCreateSimpleWindow(probeDisplay,
				DefaultRootWindow(probeDisplay), -10, -10, 1, 1, 0, 0, 0);
			const Atom clipboard = XInternAtom(probeDisplay, "CLIPBOARD", False);
			const Atom property =
				XInternAtom(probeDisplay, "ORKIGE_PROBE_SELECTION", False);
			// ask for both shapes the owner answers: the advertised target list
			// and the text itself (SDL writes each onto the requestor's window)
			XConvertSelection(probeDisplay, clipboard,
				XInternAtom(probeDisplay, "TARGETS", False), property,
				probeWindow, CurrentTime);
			XConvertSelection(probeDisplay, clipboard,
				XInternAtom(probeDisplay, "UTF8_STRING", False), property,
				probeWindow, CurrentTime);
			XFlush(probeDisplay);
			// die before the answer can be written: the requests are already
			// queued for the owner, the window they name is not
			XDestroyWindow(probeDisplay, probeWindow);
			XCloseDisplay(probeDisplay);
			return true;
		}
	}
}
