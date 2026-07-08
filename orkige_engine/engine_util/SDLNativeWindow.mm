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
