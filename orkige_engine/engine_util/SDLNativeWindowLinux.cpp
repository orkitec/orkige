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
// SDL_VIDEO_DRIVER env var still overrides the hint).
// TODO(linux): native Wayland needs (a) a Wayland-capable render path -
// classic GLX cannot do it, Ogre-Next 3.0 has no Vulkan Wayland windowing -
// and (b) a wl_surface branch here. Revisit when a backend can consume it.
#include <SDL3/SDL.h>
#include <core_debug/DebugMacros.h>

#include <cstdint>

// Runs before main() in every app that references the bridge symbol below
// (all SDL-hosted Orkige apps): prefer X11 while both render backends speak
// only X11. SDL_SetHint is legal before SDL_Init and keeps normal priority,
// so a user's SDL_VIDEO_DRIVER environment variable still wins.
__attribute__((constructor)) static void orkigePreferX11VideoDriver()
{
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
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
