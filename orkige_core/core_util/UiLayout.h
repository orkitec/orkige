/********************************************************************
	created:	Saturday 2026/07/11 at 10:00
	filename: 	UiLayout.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __UiLayout_h__11_7_2026__10_00_00__
#define __UiLayout_h__11_7_2026__10_00_00__

//! @file UiLayout.h
//! @brief the pure rect-anchor layout resolver shared by the fastgui widget
//! hierarchy and its unit tests. A LayoutNode expresses a widget's placement
//! as parent-relative anchor fractions + a pivot + offsets, and resolveRect
//! turns a parent's absolute pixel rect into the child's. No renderer or
//! platform types (plain floats), so it resolves identically on every backend
//! and is exercised headlessly. This is the generalisation of the point-anchor
//! math in SafeArea.h's UiAnchor::place - a widget can pin to a corner/edge or
//! stretch with its parent region, and nested widgets lay out inside the
//! resolved rect of their parent.

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	//! @brief a 2D point / size in the layout's own units (plain data)
	struct ORKIGE_CORE_DLL LayoutVec2
	{
		float	x = 0.0f;
		float	y = 0.0f;
	};

	//! @brief an absolute pixel rectangle (origin top-left, the DrawLayer2D
	//! convention). w/h are extents, not far corners.
	struct ORKIGE_CORE_DLL LayoutRect
	{
		float	x = 0.0f;
		float	y = 0.0f;
		float	w = 0.0f;
		float	h = 0.0f;
	};

	//! @brief the named anchor presets - the 9-way alignment vocabulary
	//! (FastGuiView::Alignment) generalised, plus the stretch bands/columns
	//! and the full stretch. Each maps to an (anchorMin, anchorMax) pair.
	enum LayoutAnchorPreset
	{
		LAP_TOPLEFT = 0,	//!< point anchor at the parent's top-left
		LAP_TOP,			//!< point anchor at the top edge centre
		LAP_TOPRIGHT,
		LAP_LEFT,
		LAP_CENTER,
		LAP_RIGHT,
		LAP_BOTTOMLEFT,
		LAP_BOTTOM,
		LAP_BOTTOMRIGHT,
		LAP_STRETCH_TOP,	//!< stretch horizontally, pinned to the top edge
		LAP_STRETCH_MIDDLE,	//!< stretch horizontally, vertically centred
		LAP_STRETCH_BOTTOM,	//!< stretch horizontally, pinned to the bottom edge
		LAP_STRETCH_LEFT,	//!< stretch vertically, pinned to the left edge
		LAP_STRETCH_CENTER,	//!< stretch vertically, horizontally centred
		LAP_STRETCH_RIGHT,	//!< stretch vertically, pinned to the right edge
		LAP_STRETCH_ALL		//!< stretch on both axes (fill the parent rect)
	};

	//! @brief one widget's placement in the rect-anchor model (plain data;
	//! serialises 1:1 into a declarative layout file and binds to Lua). All
	//! lengths (offsets) are DESIGN units; resolveRect multiplies them by the
	//! layout scale. Anchor fractions and the pivot are unitless 0..1.
	struct ORKIGE_CORE_DLL LayoutNode
	{
		//! anchor rect as fractions of the PARENT rect. min == max on an axis
		//! => a point anchor (fixed size on that axis); min < max => the widget
		//! stretches with the parent along it. (0,0) = top-left corner.
		LayoutVec2	anchorMin;
		LayoutVec2	anchorMax;
		//! 0..1 point inside the widget's own rect the anchoredPosition refers
		//! to (only used by the friendly accessors; it does NOT move the rect)
		LayoutVec2	pivot;
		//! offsets from the anchor-rect corners to the widget corners, in DESIGN
		//! units. offsetMin = top-left gap, offsetMax = bottom-right gap.
		LayoutVec2	offsetMin;
		LayoutVec2	offsetMax;

		//! size added beyond the anchor-rect span (offsetMax - offsetMin)
		LayoutVec2 sizeDelta() const;
		//! the pivot point's offset from the anchor reference point
		LayoutVec2 anchoredPosition() const;
		//! set sizeDelta, keeping anchoredPosition fixed (recomputes offsets)
		void setSizeDelta(float w, float h);
		//! set anchoredPosition, keeping sizeDelta fixed (recomputes offsets)
		void setAnchoredPosition(float x, float y);
		//! set both offset corners directly (design units)
		void setOffsets(float left, float top, float right, float bottom);
	};

	//! @brief set a node's anchorMin/anchorMax from a named preset; offsets,
	//! pivot and the rest are left untouched (the caller sizes/positions after)
	ORKIGE_CORE_DLL void applyAnchorPreset(LayoutNode & node,
		LayoutAnchorPreset preset);

	//! @brief resolve a child's absolute pixel rect from its parent's rect.
	//! Pure: anchor fractions scale the parent extents; offsets (design units)
	//! are multiplied by layoutScale and added; the pivot is not consulted
	//! (it only defines the anchoredPosition reference, not the rect corners).
	ORKIGE_CORE_DLL LayoutRect resolveRect(LayoutRect const & parent,
		LayoutNode const & node, float layoutScale);

	//! @brief how a design resolution maps to the actual window: the geometry
	//! scale ("design units -> window pixels") the layout applies to offsets,
	//! sizeDelta, spacing and fixed widget sizes. Distinct from the display
	//! DPI/content scale (which keeps glyphs and pixel art crisp) so the two
	//! compose instead of fighting - reference scale owns layout geometry,
	//! content scale owns glyph/pixel density.
	struct ORKIGE_CORE_DLL LayoutScalePolicy
	{
		//! how the two axis ratios combine into one scale
		enum MatchMode
		{
			MM_MATCH = 0,	//!< mix(win.w/design.w, win.h/design.h, matchWidthHeight)
			MM_SHRINK,		//!< min of the two ratios (whole design fits - shortest side)
			MM_EXPAND		//!< max of the two ratios (design fills - longest side)
		};

		float		designWidth = 0.0f;		//!< 0 disables scaling (scale = 1)
		float		designHeight = 0.0f;
		float		matchWidthHeight = 0.0f;//!< 0 = match width, 1 = match height (MM_MATCH)
		MatchMode	mode = MM_MATCH;

		//! @brief the layout scale for a given window size. Returns 1 when no
		//! design resolution is set, so a game that never opts in is unscaled.
		float referenceScale(float windowWidth, float windowHeight) const;
	};
}

#endif //__UiLayout_h__11_7_2026__10_00_00__
