/**************************************************************
	created:	2026/07/16 at 02:00
	filename: 	SDLNativeWindowWeb.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The browser counterpart of engine_util/SDLNativeWindow.mm and friends,
// same extern-C contract. A wasm page has exactly ONE render surface - the
// canvas element the Emscripten runtime is bound to - and no native window
// handle to bridge: SDL's window is that canvas (it registers the input
// handlers there), and the classic GLES2 render system binds the same
// canvas itself through Emscripten's EGL when it creates its render window.
// Returning null makes the engine boot take its own-window path (an empty
// external handle, see AppHost::setupEngineBody) instead of inventing a
// fake handle value.
#include <SDL3/SDL.h>

extern "C" void* orkige_native_window_handle(SDL_Window* window)
{
	(void)window;
	return nullptr;
}
