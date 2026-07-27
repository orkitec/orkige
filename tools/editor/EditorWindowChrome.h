/********************************************************************
	created:	Monday 2026/07/27 at 12:00
	filename: 	EditorWindowChrome.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorWindowChrome - macOS-only fixup for the editor's rounded window
// corners.
//
// macOS composites the pixels under the titlebar's rounded corner mask from
// the NSWindow's own backgroundColor, never from the Metal/GL drawable - so
// clearing the render surface to the editor's chrome colour (main.cpp
// applyEditorThemeNow) is not enough on its own: the small sliver AppKit
// masks in at each top corner still shows the system's default window
// background (a blue-grey), not the render clear colour underneath.
// setEditorWindowChromeBackground closes that gap by setting the NSWindow's
// backgroundColor to the SAME chrome colour, and keeps the window opaque so
// AppKit never blends the corner against anything transparent.
//
// Non-Apple platforms have no such compositing seam; callers only invoke
// this under #ifdef __APPLE__ (see main.cpp), the same gating MacMenu.h
// uses, so this header carries no platform #ifdef of its own.
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

struct SDL_Window;

namespace Orkige
{
	//! @brief set the native NSWindow's backgroundColor (and ensure it stays
	//! opaque) to the given editor chrome colour, so the rounded-corner
	//! slivers AppKit composites from the window background match the
	//! render surface's clear colour instead of the system default.
	//! @param window the SDL window whose native NSWindow handle to update
	//! @param r,g,b chrome colour components in 0..1 (the same values passed
	//!        to RenderSystem::setWindowBackgroundColour)
	void setEditorWindowChromeBackground(SDL_Window* window,
		float r, float g, float b);
}
