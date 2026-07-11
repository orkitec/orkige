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
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! cross-axis offset of a child inside a group's cross extent
		inline float crossAlignOffset(LayoutAlign align, float extent,
			float childExtent)
		{
			switch(align)
			{
			case LAL_Center:	return (extent - childExtent) * 0.5f;
			case LAL_End:		return extent - childExtent;
			case LAL_Start:
			default:			return 0.0f;
			}
		}
		//! grid column/row counts for a child count under a constraint
		inline void gridDimensions(LayoutGroup const & group, int count,
			int & columns, int & rows)
		{
			const int n = std::max(0, count);
			switch(group.constraint)
			{
			case LGC_FixedColumns:
				columns = std::max(1, group.constraintCount);
				rows = (n + columns - 1) / columns;
				break;
			case LGC_FixedRows:
				rows = std::max(1, group.constraintCount);
				columns = (n + rows - 1) / rows;
				break;
			case LGC_Flexible:
			default:
				columns = std::max(1,
					int(std::ceil(std::sqrt(double(std::max(1, n))))));
				rows = (n + columns - 1) / columns;
				break;
			}
			if(rows < 1) rows = 1;
			if(columns < 1) columns = 1;
		}
		//! this group's preferred size from its (already measured) children
		LayoutVec2 measureGroupPreferred(LayoutItem const & item, float scale)
		{
			LayoutGroup const & g = item.group;
			const float padX = (g.padding.left + g.padding.right) * scale;
			const float padY = (g.padding.top + g.padding.bottom) * scale;
			const size_t n = item.children.size();
			LayoutVec2 pref;
			if(g.type == LGT_Grid)
			{
				int columns = 1, rows = 1;
				gridDimensions(g, int(n), columns, rows);
				const float cellW = g.cellSize.x * scale;
				const float cellH = g.cellSize.y * scale;
				const float gapX = g.spacing * scale;
				const float gapY = (g.spacingY > 0.0f ? g.spacingY : g.spacing) * scale;
				pref.x = padX + columns * cellW + std::max(0, columns - 1) * gapX;
				pref.y = padY + rows * cellH + std::max(0, rows - 1) * gapY;
				return pref;
			}
			const float gap = g.spacing * scale;
			float mainSum = 0.0f, crossMax = 0.0f;
			for(LayoutItem const * child : item.children)
			{
				if(g.type == LGT_Horizontal)
				{
					mainSum += child->preferred.x;
					crossMax = std::max(crossMax, child->preferred.y);
				}
				else // LGT_Vertical
				{
					mainSum += child->preferred.y;
					crossMax = std::max(crossMax, child->preferred.x);
				}
			}
			if(n > 1)
			{
				mainSum += gap * float(n - 1);
			}
			if(g.type == LGT_Horizontal)
			{
				pref.x = padX + mainSum;
				pref.y = padY + crossMax;
			}
			else
			{
				pref.x = padX + crossMax;
				pref.y = padY + mainSum;
			}
			return pref;
		}
		//! arrange a group's children inside its (padded, scrolled) content rect
		void arrangeGroupChildren(LayoutItem & item, LayoutRect const & content,
			float scale)
		{
			LayoutGroup const & g = item.group;
			if(g.type == LGT_Grid)
			{
				int columns = 1, rows = 1;
				gridDimensions(g, int(item.children.size()), columns, rows);
				const float cellW = g.cellSize.x * scale;
				const float cellH = g.cellSize.y * scale;
				const float gapX = g.spacing * scale;
				const float gapY = (g.spacingY > 0.0f ? g.spacingY : g.spacing) * scale;
				for(size_t i = 0; i < item.children.size(); ++i)
				{
					int col = 0, row = 0;
					if(g.constraint == LGC_FixedRows)
					{
						// column-major fill (rows is the fixed dimension)
						col = int(i) / rows;
						row = int(i) % rows;
					}
					else
					{
						col = int(i) % columns;
						row = int(i) / columns;
					}
					LayoutRect r;
					r.x = content.x + col * (cellW + gapX);
					r.y = content.y + row * (cellH + gapY);
					r.w = cellW;
					r.h = cellH;
					assignRects(*item.children[i], r, scale);
				}
				return;
			}
			const float gap = g.spacing * scale;
			const bool horizontal = (g.type == LGT_Horizontal);
			float cursor = horizontal ? content.x : content.y;
			for(LayoutItem * child : item.children)
			{
				LayoutRect r;
				if(horizontal)
				{
					const float cw = child->preferred.x;
					const float ch = g.childForceExpand ? content.h
						: child->preferred.y;
					r.x = cursor;
					r.y = content.y + (g.childForceExpand ? 0.0f
						: crossAlignOffset(g.childAlign, content.h, ch));
					r.w = cw;
					r.h = ch;
					cursor += cw + gap;
				}
				else
				{
					const float ch = child->preferred.y;
					const float cw = g.childForceExpand ? content.w
						: child->preferred.x;
					r.x = content.x + (g.childForceExpand ? 0.0f
						: crossAlignOffset(g.childAlign, content.w, cw));
					r.y = cursor;
					r.w = cw;
					r.h = ch;
					cursor += ch + gap;
				}
				assignRects(*child, r, scale);
			}
		}
	}
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
	//---------------------------------------------------------
	LayoutVec2 measurePreferred(LayoutItem & item, float layoutScale)
	{
		// bottom-up: children first, so a group can sum their preferred sizes
		for(LayoutItem * child : item.children)
		{
			measurePreferred(*child, layoutScale);
		}
		if(item.group.isGroup())
		{
			item.preferred = measureGroupPreferred(item, layoutScale);
		}
		else
		{
			// a leaf (or a plain container) is sized by its own measured content
			item.preferred = item.contentSize;
		}
		return item.preferred;
	}
	//---------------------------------------------------------
	LayoutRect resolveItemRect(LayoutRect const & parent, LayoutItem const & item,
		float layoutScale)
	{
		LayoutRect rect = resolveRect(parent, item.node, layoutScale);
		// a Preferred content-fit axis overrides the anchor-derived size, holding
		// the pivot point fixed so the node grows/shrinks around it
		if(item.fit.horizontal == LFM_Preferred)
		{
			const float newW = item.preferred.x;
			rect.x += (rect.w - newW) * item.node.pivot.x;
			rect.w = newW;
		}
		if(item.fit.vertical == LFM_Preferred)
		{
			const float newH = item.preferred.y;
			rect.y += (rect.h - newH) * item.node.pivot.y;
			rect.h = newH;
		}
		return rect;
	}
	//---------------------------------------------------------
	void assignRects(LayoutItem & item, LayoutRect const & itemRect,
		float layoutScale)
	{
		item.resolved = itemRect;
		// the rect the children lay out inside: a group insets by its padding;
		// the scroll offset shifts every child (a scroll viewport's content)
		LayoutRect content = itemRect;
		if(item.group.isGroup())
		{
			const LayoutPadding & p = item.group.padding;
			content.x += p.left * layoutScale;
			content.y += p.top * layoutScale;
			content.w -= (p.left + p.right) * layoutScale;
			content.h -= (p.top + p.bottom) * layoutScale;
		}
		content.x += item.scrollOffset.x;
		content.y += item.scrollOffset.y;

		if(item.group.isGroup())
		{
			arrangeGroupChildren(item, content, layoutScale);
		}
		else
		{
			// a plain container: children resolve by their own anchors against
			// this node's content rect (nesting anchored rects inside groups)
			for(LayoutItem * child : item.children)
			{
				const LayoutRect childRect = resolveItemRect(content, *child,
					layoutScale);
				assignRects(*child, childRect, layoutScale);
			}
		}
	}
	//---------------------------------------------------------
	void resolveTree(LayoutItem & root, LayoutRect const & parentRect,
		float layoutScale)
	{
		measurePreferred(root, layoutScale);
		const LayoutRect rect = resolveItemRect(parentRect, root, layoutScale);
		assignRects(root, rect, layoutScale);
	}
	//---------------------------------------------------------
	float clampScroll(float offset, float contentExtent, float viewportExtent)
	{
		// content shorter than the viewport cannot scroll (pin to the top)
		const float minOffset = std::min(0.0f, viewportExtent - contentExtent);
		if(offset > 0.0f)
		{
			return 0.0f;
		}
		if(offset < minOffset)
		{
			return minOffset;
		}
		return offset;
	}
}
