// Bridges an SDL3 window to the native window pointer OGRE's render systems
// expect as "externalWindowHandle": the ANativeWindow* on Android (OGRE's
// AndroidEGLWindow creates its EGL surface directly on it). The Android
// counterpart of engine_util/SDLNativeWindow.mm - same contract, declared by
// apps as: extern "C" void* orkige_native_window_handle(SDL_Window*).
#include <SDL3/SDL.h>

extern "C" void* orkige_native_window_handle(SDL_Window* window)
{
	return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
		SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL);
}
