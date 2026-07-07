// Bridges an SDL3 window to the native NSWindow pointer OGRE's macOS render
// systems expect as "externalWindowHandle".
// Deliberate duplicate of samples/hello_orkige/native_window.mm: two callers
// do not justify a shared platform-glue target yet; fold both into one spot
// when a third app appears (or when Orkige::Application grows SDL support).
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>

extern "C" void* orkige_native_window_handle(SDL_Window* window)
{
	NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	return (__bridge void*)nswindow;
}
