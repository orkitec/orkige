/**************************************************************
	created:	2026/07/11 at 13:00
	filename: 	HapticBridgeApple.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The iOS taptic-engine bridge behind HapticManager: UIFeedbackGenerator is
// UIKit ObjC, so it lives in this .mm (matches the engine's other Apple shims,
// e.g. SDLNativeWindow.mm); HapticManager stays plain C++ and calls the two
// extern "C" entry points below. iOS-only (added to the build only on iOS -
// macOS has no phone vibrator and takes HapticManager's desktop no-op path).

#import <UIKit/UIKit.h>

//! release a manually-owned ObjC object (a no-op under ARC)
static inline void orkige_haptic_release(id object)
{
#if !__has_feature(objc_arc)
	[object release];
#else
	(void)object;
#endif
}

//! generic buzz: map a 0..1 strength onto an impact style (iOS impacts are
//! discrete, not a continuous duration - the taptic engine owns the envelope)
extern "C" void orkige_haptic_play_ios(float strength, int durationMs)
{
	(void)durationMs;
	if (@available(iOS 10.0, *))
	{
		UIImpactFeedbackStyle style = UIImpactFeedbackStyleMedium;
		if (strength < 0.4f)
		{
			style = UIImpactFeedbackStyleLight;
		}
		else if (strength > 0.7f)
		{
			style = UIImpactFeedbackStyleHeavy;
		}
		UIImpactFeedbackGenerator* generator =
			[[UIImpactFeedbackGenerator alloc] initWithStyle:style];
		[generator prepare];
		[generator impactOccurred];
		orkige_haptic_release(generator);
	}
}

//! named pattern: mirrors HapticManager::Pattern order
//! (Light,Medium,Heavy,Success,Warning,Error,Selection) onto the matching
//! feedback generator - impacts, notifications and the selection tick
extern "C" void orkige_haptic_pattern_ios(int pattern)
{
	if (@available(iOS 10.0, *))
	{
		switch (pattern)
		{
		case 0:	// Light
		case 1:	// Medium
		case 2:	// Heavy
		{
			UIImpactFeedbackStyle style =
				pattern == 0 ? UIImpactFeedbackStyleLight :
				pattern == 2 ? UIImpactFeedbackStyleHeavy :
				UIImpactFeedbackStyleMedium;
			UIImpactFeedbackGenerator* generator =
				[[UIImpactFeedbackGenerator alloc] initWithStyle:style];
			[generator prepare];
			[generator impactOccurred];
			orkige_haptic_release(generator);
			break;
		}
		case 3:	// Success
		case 4:	// Warning
		case 5:	// Error
		{
			UINotificationFeedbackType type =
				pattern == 3 ? UINotificationFeedbackTypeSuccess :
				pattern == 4 ? UINotificationFeedbackTypeWarning :
				UINotificationFeedbackTypeError;
			UINotificationFeedbackGenerator* generator =
				[[UINotificationFeedbackGenerator alloc] init];
			[generator prepare];
			[generator notificationOccurred:type];
			orkige_haptic_release(generator);
			break;
		}
		case 6:	// Selection
		default:
		{
			UISelectionFeedbackGenerator* generator =
				[[UISelectionFeedbackGenerator alloc] init];
			[generator prepare];
			[generator selectionChanged];
			orkige_haptic_release(generator);
			break;
		}
		}
	}
}
