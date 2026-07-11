/********************************************************************
	created:	Saturday 2026/07/11 at 10:00
	filename: 	UiLayout.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure rect-anchor layout resolver (@see UiLayout.h).
*********************************************************************/

#include "core_util/UiLayout.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	LayoutVec2 LayoutNode::sizeDelta() const
	{
		LayoutVec2 delta;
		delta.x = this->offsetMax.x - this->offsetMin.x;
		delta.y = this->offsetMax.y - this->offsetMin.y;
		return delta;
	}
	//---------------------------------------------------------
	LayoutVec2 LayoutNode::anchoredPosition() const
	{
		const LayoutVec2 delta = this->sizeDelta();
		LayoutVec2 position;
		position.x = this->offsetMin.x + this->pivot.x * delta.x;
		position.y = this->offsetMin.y + this->pivot.y * delta.y;
		return position;
	}
	//---------------------------------------------------------
	void LayoutNode::setSizeDelta(float w, float h)
	{
		// keep the anchoredPosition fixed while the size changes
		const LayoutVec2 position = this->anchoredPosition();
		this->offsetMin.x = position.x - this->pivot.x * w;
		this->offsetMin.y = position.y - this->pivot.y * h;
		this->offsetMax.x = this->offsetMin.x + w;
		this->offsetMax.y = this->offsetMin.y + h;
	}
	//---------------------------------------------------------
	void LayoutNode::setAnchoredPosition(float x, float y)
	{
		// keep the size (sizeDelta) fixed while the pivot point moves
		const LayoutVec2 delta = this->sizeDelta();
		this->offsetMin.x = x - this->pivot.x * delta.x;
		this->offsetMin.y = y - this->pivot.y * delta.y;
		this->offsetMax.x = this->offsetMin.x + delta.x;
		this->offsetMax.y = this->offsetMin.y + delta.y;
	}
	//---------------------------------------------------------
	void LayoutNode::setOffsets(float left, float top, float right, float bottom)
	{
		this->offsetMin.x = left;
		this->offsetMin.y = top;
		this->offsetMax.x = right;
		this->offsetMax.y = bottom;
	}
	//---------------------------------------------------------
	void applyAnchorPreset(LayoutNode & node, LayoutAnchorPreset preset)
	{
		// each preset is an (anchorMin, anchorMax) fraction pair. Point anchors
		// keep min == max; a stretch spreads min..max across the axis.
		float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
		switch(preset)
		{
		case LAP_TOPLEFT:		minX = 0.0f; minY = 0.0f; maxX = 0.0f; maxY = 0.0f; break;
		case LAP_TOP:			minX = 0.5f; minY = 0.0f; maxX = 0.5f; maxY = 0.0f; break;
		case LAP_TOPRIGHT:		minX = 1.0f; minY = 0.0f; maxX = 1.0f; maxY = 0.0f; break;
		case LAP_LEFT:			minX = 0.0f; minY = 0.5f; maxX = 0.0f; maxY = 0.5f; break;
		case LAP_CENTER:		minX = 0.5f; minY = 0.5f; maxX = 0.5f; maxY = 0.5f; break;
		case LAP_RIGHT:			minX = 1.0f; minY = 0.5f; maxX = 1.0f; maxY = 0.5f; break;
		case LAP_BOTTOMLEFT:	minX = 0.0f; minY = 1.0f; maxX = 0.0f; maxY = 1.0f; break;
		case LAP_BOTTOM:		minX = 0.5f; minY = 1.0f; maxX = 0.5f; maxY = 1.0f; break;
		case LAP_BOTTOMRIGHT:	minX = 1.0f; minY = 1.0f; maxX = 1.0f; maxY = 1.0f; break;
		case LAP_STRETCH_TOP:		minX = 0.0f; minY = 0.0f; maxX = 1.0f; maxY = 0.0f; break;
		case LAP_STRETCH_MIDDLE:	minX = 0.0f; minY = 0.5f; maxX = 1.0f; maxY = 0.5f; break;
		case LAP_STRETCH_BOTTOM:	minX = 0.0f; minY = 1.0f; maxX = 1.0f; maxY = 1.0f; break;
		case LAP_STRETCH_LEFT:		minX = 0.0f; minY = 0.0f; maxX = 0.0f; maxY = 1.0f; break;
		case LAP_STRETCH_CENTER:	minX = 0.5f; minY = 0.0f; maxX = 0.5f; maxY = 1.0f; break;
		case LAP_STRETCH_RIGHT:		minX = 1.0f; minY = 0.0f; maxX = 1.0f; maxY = 1.0f; break;
		case LAP_STRETCH_ALL:		minX = 0.0f; minY = 0.0f; maxX = 1.0f; maxY = 1.0f; break;
		}
		node.anchorMin.x = minX;
		node.anchorMin.y = minY;
		node.anchorMax.x = maxX;
		node.anchorMax.y = maxY;
	}
	//---------------------------------------------------------
	LayoutRect resolveRect(LayoutRect const & parent, LayoutNode const & node,
		float layoutScale)
	{
		// anchor rect: the fractions pick a sub-rect of the parent extents
		const float axMin = parent.x + node.anchorMin.x * parent.w;
		const float axMax = parent.x + node.anchorMax.x * parent.w;
		const float ayMin = parent.y + node.anchorMin.y * parent.h;
		const float ayMax = parent.y + node.anchorMax.y * parent.h;
		// offsets are design lengths -> scale them to window pixels
		const float xMin = axMin + node.offsetMin.x * layoutScale;
		const float xMax = axMax + node.offsetMax.x * layoutScale;
		const float yMin = ayMin + node.offsetMin.y * layoutScale;
		const float yMax = ayMax + node.offsetMax.y * layoutScale;
		LayoutRect rect;
		rect.x = xMin;
		rect.y = yMin;
		rect.w = xMax - xMin;
		rect.h = yMax - yMin;
		return rect;
	}
	//---------------------------------------------------------
	float LayoutScalePolicy::referenceScale(float windowWidth,
		float windowHeight) const
	{
		if(this->designWidth <= 0.0f || this->designHeight <= 0.0f)
		{
			return 1.0f;	// no design resolution set: layout stays 1:1
		}
		const float ratioWidth = windowWidth / this->designWidth;
		const float ratioHeight = windowHeight / this->designHeight;
		switch(this->mode)
		{
		case MM_SHRINK:
			return std::min(ratioWidth, ratioHeight);
		case MM_EXPAND:
			return std::max(ratioWidth, ratioHeight);
		case MM_MATCH:
		default:
			{
				const float match = std::min(1.0f,
					std::max(0.0f, this->matchWidthHeight));
				return ratioWidth * (1.0f - match) + ratioHeight * match;
			}
		}
	}
}
