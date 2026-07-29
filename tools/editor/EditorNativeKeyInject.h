/********************************************************************
	created:	Thursday 2026/07/30 at 12:00
	filename: 	EditorNativeKeyInject.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorNativeKeyInject_h__30_7_2026__12_00_00__
#define __EditorNativeKeyInject_h__30_7_2026__12_00_00__

//! @file EditorNativeKeyInject.h
//! @brief post a key event into the app's OWN native event queue (macOS only).
//!
//! A test that fabricates an SDL_Event and pushes it with SDL_PushEvent skips
//! the platform's keyboard translation entirely. On macOS that translation is
//! where a chord can be lost: while text input is active every key-down is run
//! through the AppKit field editor (interpretKeyEvents:) before SDL turns it
//! into an SDL_EVENT_KEY_DOWN, and an input method may consume it there. This
//! seam posts a real NSEvent to NSApp, so the editor's own event pump walks the
//! FULL AppKit -> SDL -> ImGui path a physical keypress walks - the only
//! in-process way to exercise it (synthesizing OS-level events needs the
//! accessibility permission a test cannot assume).
//!
//! Non-macOS builds compile the honest no-op below (returns false).

namespace OrkigeEditor
{
	//! @brief post a native key-down/up for a `letter` chord (optionally with the
	//! physical Ctrl and/or Cmd modifier as its own event first, the way real
	//! hardware sends them) into this process's own AppKit event queue. `letter`
	//! is a lowercase ASCII letter. Returns false when the platform has no such
	//! seam (or the app has no native application object yet) - the caller then
	//! falls back to the fabricated-SDL-event path.
#if defined(__APPLE__)
	//! @brief make this app the active one and its window key, so a posted key
	//! event is delivered the way one is to a focused app (the platform layer
	//! drops key-downs that arrive with no keyboard focus). Asynchronous - give
	//! it a frame before posting.
	bool nativeActivateApp();
	bool nativePostKeyChord(char letter, bool ctrl, bool cmd);
#else
	inline bool nativeActivateApp() { return false; }
	inline bool nativePostKeyChord(char, bool, bool) { return false; }
#endif
}

#endif //__EditorNativeKeyInject_h__30_7_2026__12_00_00__
