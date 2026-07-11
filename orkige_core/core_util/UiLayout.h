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
//! @brief the pure rect-anchor layout resolver shared by the gui widget
//! hierarchy and its unit tests. A LayoutNode expresses a widget's placement
//! as parent-relative anchor fractions + a pivot + offsets, and resolveRect
//! turns a parent's absolute pixel rect into the child's. No renderer or
//! platform types (plain floats), so it resolves identically on every backend
//! and is exercised headlessly. This is the generalisation of the point-anchor
//! math in SafeArea.h's UiAnchor::place - a widget can pin to a corner/edge or
//! stretch with its parent region, and nested widgets lay out inside the
//! resolved rect of their parent.

#include "core_module/OrkigePrerequisites.h"

#include <vector>

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
	//! (GuiView::Alignment) generalised, plus the stretch bands/columns
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

	//==========================================================
	//=== layout groups + content-size-fit (the two-pass core) =
	//==========================================================

	//! @brief a group arranges its children automatically, OVERRIDING their
	//! anchor placement. LGT_None = a plain container (children keep resolving
	//! by their own anchors against the parent rect).
	enum LayoutGroupType
	{
		LGT_None = 0,		//!< not a group: children resolve by their anchors
		LGT_Horizontal,		//!< place children left-to-right
		LGT_Vertical,		//!< place children top-to-bottom
		LGT_Grid			//!< place children on a fixed-cell grid
	};

	//! @brief how a grid decides its column/row count
	enum LayoutGridConstraint
	{
		LGC_Flexible = 0,	//!< pick a near-square column count from the child count
		LGC_FixedColumns,	//!< constraintCount columns, rows follow
		LGC_FixedRows		//!< constraintCount rows, columns follow (column-major)
	};

	//! @brief cross-axis alignment of a horizontal/vertical group's children
	enum LayoutAlign
	{
		LAL_Start = 0,		//!< left (vertical group) / top (horizontal group)
		LAL_Center,			//!< centred on the cross axis
		LAL_End				//!< right (vertical group) / bottom (horizontal group)
	};

	//! @brief how a content-size fitter drives an axis
	enum LayoutFitMode
	{
		LFM_Unconstrained = 0,	//!< size comes from the anchors (default)
		LFM_Preferred			//!< size the node to its preferred content extent
	};

	//! @brief inner padding of a group, in DESIGN units (scaled by layoutScale)
	struct ORKIGE_CORE_DLL LayoutPadding
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	//! @brief a node's group arrangement (plain data; serialises into the .oui
	//! and binds to Lua). Lengths (padding/spacing/cellSize) are DESIGN units.
	struct ORKIGE_CORE_DLL LayoutGroup
	{
		LayoutGroupType			type = LGT_None;
		LayoutPadding			padding;
		float					spacing = 0.0f;		//!< main-axis gap; grid column gap
		float					spacingY = 0.0f;	//!< grid row gap (<= 0 uses spacing)
		LayoutAlign				childAlign = LAL_Start;	//!< H/V cross-axis alignment
		bool					childForceExpand = false;//!< stretch children across the cross axis
		LayoutVec2				cellSize{100.0f, 100.0f};//!< grid cell size (design units)
		LayoutGridConstraint	constraint = LGC_Flexible;
		int						constraintCount = 0;	//!< columns / rows count

		inline bool isGroup() const { return this->type != LGT_None; }
	};

	//! @brief a node's content-size-fit: a Preferred axis sizes the node to its
	//! measured preferred content (e.g. a button to its label + padding)
	struct ORKIGE_CORE_DLL LayoutContentFit
	{
		LayoutFitMode	horizontal = LFM_Unconstrained;
		LayoutFitMode	vertical = LFM_Unconstrained;
	};

	//! @brief one node of a layout tree the two-pass resolver walks. Plain data
	//! owned by the caller (GuiManager rebuilds a transient forest of these
	//! from its widget hierarchy each relayout; the unit tests build them
	//! directly). @c children are NOT owned - the caller keeps the storage
	//! alive across a resolve. @c userData is an opaque back-reference (the
	//! GuiWidget*) the caller reads @c resolved out of.
	struct ORKIGE_CORE_DLL LayoutItem
	{
		LayoutNode				node;			//!< anchor placement (for non-group parents)
		LayoutGroup				group;			//!< child arrangement (LGT_None = passthrough)
		LayoutContentFit		fit;			//!< content-size-fit
		//! this leaf's intrinsic preferred content size in WINDOW pixels (a
		//! widget measures it, e.g. a label's text). Ignored for a group (its
		//! preferred size comes from its arranged children).
		LayoutVec2				contentSize;
		//! a pixel offset added to this node's children positions - the scroll
		//! amount of a scroll viewport (0 for everything else). NOT scaled.
		LayoutVec2				scrollOffset;
		std::vector<LayoutItem*> children;		//!< child nodes (not owned)
		void*					userData = nullptr;	//!< opaque back-reference

		//--- outputs (filled by the resolver) ---
		LayoutVec2				preferred;		//!< measurePreferred result
		LayoutRect				resolved;		//!< assignRects result
	};

	//! @brief pass 1 (bottom-up): fill @c preferred for the whole subtree and
	//! return this node's preferred size. A group's preferred size is the
	//! extent of its arranged children (plus padding/spacing); a leaf's is its
	//! @c contentSize. Lengths use @p layoutScale for the design-unit metrics.
	ORKIGE_CORE_DLL LayoutVec2 measurePreferred(LayoutItem & item,
		float layoutScale);

	//! @brief pass 2 (top-down): @p itemRect is this node's final absolute rect;
	//! place its children. A group arranges them sequentially (overriding their
	//! anchors); a plain container resolves each child via resolveRect against
	//! this node's (padding + scroll shifted) content rect. Recurses. Requires
	//! measurePreferred to have run over the subtree.
	ORKIGE_CORE_DLL void assignRects(LayoutItem & item,
		LayoutRect const & itemRect, float layoutScale);

	//! @brief this node's own absolute rect: resolveRect against @p parent, then
	//! a Preferred content-fit axis overrides that axis' size (keeping the pivot
	//! point fixed). Requires @c preferred to be set (measurePreferred ran).
	ORKIGE_CORE_DLL LayoutRect resolveItemRect(LayoutRect const & parent,
		LayoutItem const & item, float layoutScale);

	//! @brief the whole two-pass resolve for one root against a parent rect:
	//! measurePreferred, then resolveItemRect, then assignRects.
	ORKIGE_CORE_DLL void resolveTree(LayoutItem & root,
		LayoutRect const & parentRect, float layoutScale);

	//! @brief clamp a scroll offset so the content stays pinned to the viewport:
	//! the offset is <= 0 and >= min(0, viewportExtent - contentExtent). Content
	//! that fits (content <= viewport) clamps to 0.
	ORKIGE_CORE_DLL float clampScroll(float offset, float contentExtent,
		float viewportExtent);
}

#endif //__UiLayout_h__11_7_2026__10_00_00__
