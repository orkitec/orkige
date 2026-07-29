/********************************************************************
	created:	Thursday 2026/07/30 at 12:00
	filename: 	EditorNativeKeyInject.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorNativeKeyInject.mm - post a real NSEvent key chord into the app's own
// event queue so a test walks the FULL AppKit -> SDL -> ImGui keyboard path
// (see the header for why a fabricated SDL_Event is not enough).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorNativeKeyInject.h"

#import <AppKit/AppKit.h>

namespace OrkigeEditor
{
	namespace
	{
		// macOS virtual key codes (Carbon kVK_*): the letter row and modifiers
		constexpr unsigned short kVkControl = 59;
		constexpr unsigned short kVkCommand = 55;
		//! kVK_ANSI_A.. for a lowercase letter (the ANSI layout order is not
		//! alphabetical, so this is a table)
		unsigned short vkForLetter(char letter)
		{
			switch (letter)
			{
				case 'a': return 0;		case 'b': return 11;	case 'c': return 8;
				case 'd': return 2;		case 'e': return 14;	case 'f': return 3;
				case 'g': return 5;		case 'h': return 4;		case 'i': return 34;
				case 'j': return 38;	case 'k': return 40;	case 'l': return 37;
				case 'm': return 46;	case 'n': return 45;	case 'o': return 31;
				case 'p': return 35;	case 'q': return 12;	case 'r': return 15;
				case 's': return 1;		case 't': return 17;	case 'u': return 32;
				case 'v': return 9;		case 'w': return 13;	case 'x': return 7;
				case 'y': return 16;	case 'z': return 6;
				default:  return 0xffff;
			}
		}

		//! the raw device-dependent flag AppKit reports for the LEFT Ctrl key
		//! (NX_DEVICELCTLKEYMASK) - the platform layer reads it to tell left
		//! from right, so a posted event must carry it like a real one
		constexpr NSUInteger kDeviceLeftCtrl = 0x00000001;
		//! NX_DEVICELCMDKEYMASK, the same for the left Command key
		constexpr NSUInteger kDeviceLeftCmd = 0x00000008;
	}

	bool nativeActivateApp()
	{
		if (NSApp == nil)
		{
			return false;
		}
		[NSApp activateIgnoringOtherApps:YES];
		NSWindow* first = [[NSApp windows] firstObject];
		if (first != nil)
		{
			[first makeKeyAndOrderFront:nil];
		}
		return true;
	}

	bool nativePostKeyChord(char letter, bool ctrl, bool cmd)
	{
		const unsigned short vk = vkForLetter(letter);
		if (vk == 0xffff || NSApp == nil)
		{
			return false;
		}
		NSWindow* target = [NSApp keyWindow];
		if (target == nil)
		{
			target = [[NSApp windows] firstObject];
		}
		const NSInteger windowNumber = (target != nil) ? [target windowNumber] : 0;
		const NSTimeInterval when = [[NSProcessInfo processInfo] systemUptime];
		NSEventModifierFlags flags = 0;
		unsigned short modVk = 0;
		if (ctrl)
		{
			flags |= NSEventModifierFlagControl | kDeviceLeftCtrl;
			modVk = kVkControl;
		}
		if (cmd)
		{
			flags |= NSEventModifierFlagCommand | kDeviceLeftCmd;
			modVk = kVkCommand;
		}
		// what a real chord carries in -characters: Ctrl+<letter> produces the
		// C0 code, a Cmd chord (and a bare key) the plain letter
		const unichar produced = ctrl
			? static_cast<unichar>((letter - 'a') + 1)
			: static_cast<unichar>(letter);
		NSString* characters = [NSString stringWithCharacters:&produced length:1];
		NSString* bare = [NSString stringWithFormat:@"%c", letter];

		// 1) the modifier as its own event, exactly like real hardware
		NSEvent* modDown = (modVk != 0)
			? [NSEvent keyEventWithType:NSEventTypeFlagsChanged
				location:NSZeroPoint modifierFlags:flags timestamp:when
				windowNumber:windowNumber context:nil characters:@""
				charactersIgnoringModifiers:@"" isARepeat:NO keyCode:modVk]
			: nil;
		// 2) the letter with the modifier active, 3) its release
		NSEvent* keyDown = [NSEvent keyEventWithType:NSEventTypeKeyDown
			location:NSZeroPoint modifierFlags:flags timestamp:when
			windowNumber:windowNumber context:nil characters:characters
			charactersIgnoringModifiers:bare isARepeat:NO keyCode:vk];
		NSEvent* keyUp = [NSEvent keyEventWithType:NSEventTypeKeyUp
			location:NSZeroPoint modifierFlags:flags timestamp:when
			windowNumber:windowNumber context:nil characters:characters
			charactersIgnoringModifiers:bare isARepeat:NO keyCode:vk];
		// 4) the modifier release
		NSEvent* modUp = (modVk != 0)
			? [NSEvent keyEventWithType:NSEventTypeFlagsChanged
				location:NSZeroPoint modifierFlags:0 timestamp:when
				windowNumber:windowNumber context:nil characters:@""
				charactersIgnoringModifiers:@"" isARepeat:NO keyCode:modVk]
			: nil;
		if (keyDown == nil || keyUp == nil)
		{
			return false;
		}
		if (modDown != nil) { [NSApp postEvent:modDown atStart:NO]; }
		[NSApp postEvent:keyDown atStart:NO];
		[NSApp postEvent:keyUp atStart:NO];
		if (modUp != nil) { [NSApp postEvent:modUp atStart:NO]; }
		return true;
	}
}
