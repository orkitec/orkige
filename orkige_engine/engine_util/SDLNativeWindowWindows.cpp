/**************************************************************
	created:	2026/07/10 at 17:20
	filename: 	SDLNativeWindowWindows.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// Bridges an SDL3 window to the native window handle OGRE's render systems
// expect on Windows - the desktop counterpart of SDLNativeWindowLinux.cpp
// and engine_util/SDLNativeWindow.mm, same contract, declared by apps as:
// extern "C" void* orkige_native_window_handle(SDL_Window*). The caller
// stringifies the returned pointer value (decimal size_t) and hands it to
// the engine boot. On Windows both render backends consume the HWND
// directly: classic OGRE's Win32 window path parses the decimal back into
// an HWND (externalWindowHandle), and Ogre-Next's VulkanWin32Window does
// the same through its external-window misc param.
#include <SDL3/SDL.h>

extern "C" void* orkige_native_window_handle(SDL_Window* window)
{
	if (!window)
	{
		return nullptr;
	}
	return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
		SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}
