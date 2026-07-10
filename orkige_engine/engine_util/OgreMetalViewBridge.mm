// Splices a render-system-created Metal view into an SDL-hosted UIKit window
// on iOS. The Ogre-Next Metal window builds its own view detached from SDL's
// UIWindow (the Metal window path only adopts a view that is already an
// OgreMetalView, so SDL's UIWindow cannot be handed to it); this bridge takes
// that view and adds it as a subview of SDL's window so it becomes visible and
// tracks the screen. It trades in opaque void* only - the renderer containment
// seam - so no Ogre types leak into this translation unit.
#import <UIKit/UIKit.h>

extern "C" void orkige_ios_attach_metal_view(void* metalView, void* uiWindow)
{
	UIView* view = (__bridge UIView*)metalView;
	UIWindow* window = (__bridge UIWindow*)uiWindow;
	if(!view || !window)
	{
		return;
	}
	// prefer the root view controller's view (SDL installs its own view
	// there) so the Metal view shares its bounds and lives above it; fall
	// back to the window itself if there is no controller yet
	UIViewController* rootController = window.rootViewController;
	UIView* host = rootController ? rootController.view : window;
	view.frame = host.bounds;
	view.autoresizingMask =
		UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	view.contentScaleFactor = [UIScreen mainScreen].nativeScale;
	// input keeps flowing to SDL's view underneath (touches fall through)
	view.userInteractionEnabled = NO;
	[host addSubview:view];
}
