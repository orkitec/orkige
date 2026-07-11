// MarqueeSelection - pure, UI-free geometry for the Scene viewport's rubber-band
// (marquee) box select, split out so the headless unit tests exercise exactly
// the decisions the panel drives with (no ImGui, no camera).
//
// The Scene panel projects each object's world bounds to a screen rectangle and
// asks whether it intersects the dragged marquee box; a short press that never
// travels past a pixel threshold stays a plain click, not a band select.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <algorithm>

namespace Orkige
{
	//! @brief an axis-aligned rectangle in screen/render-target pixels (the
	//! space ImGui's io.MousePos and the Scene image rect live in)
	struct ScreenRect
	{
		float minX = 0.0f;
		float minY = 0.0f;
		float maxX = 0.0f;
		float maxY = 0.0f;
	};

	//! @brief normalise a rectangle from two arbitrary corners (a marquee drag
	//! runs in any direction, so start/end are not ordered)
	inline ScreenRect screenRectFromCorners(float ax, float ay, float bx, float by)
	{
		return ScreenRect{ std::min(ax, bx), std::min(ay, by),
			std::max(ax, bx), std::max(ay, by) };
	}

	//! @brief do two screen rectangles overlap (touching edges count)? The
	//! marquee selects an object when its projected bounds intersect the box.
	inline bool screenRectsIntersect(ScreenRect const& a, ScreenRect const& b)
	{
		return a.minX <= b.maxX && b.minX <= a.maxX &&
			a.minY <= b.maxY && b.minY <= a.maxY;
	}

	//! @brief has the cursor travelled far enough from the press point to count
	//! as a drag (a marquee) rather than a click? A small pixel threshold keeps a
	//! jittery click from ever becoming a band select. thresholdPixels is the
	//! travel distance, already content-scaled by the caller.
	inline bool marqueeIsDrag(float startX, float startY, float curX, float curY,
		float thresholdPixels)
	{
		const float dx = curX - startX;
		const float dy = curY - startY;
		return (dx * dx + dy * dy) >= thresholdPixels * thresholdPixels;
	}
}
