/********************************************************************
	created:	Saturday 2026/07/26 at 12:00
	filename: 	EditorUiEdit.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorUiEdit_h__26_7_2026__12_00_00__
#define __EditorUiEdit_h__26_7_2026__12_00_00__

//! @file EditorUiEdit.h
//! @brief the UI-independent editing core behind the visual `.oui` editor. Pure
//! operations over the ONE document model (engine_gui/GuiLayout's GuiLayoutDoc)
//! and the ONE layout resolver (core_util/UiLayout): hit-testing over resolved
//! widget rects, the anchor-preserving drag->offset/resize math, palette
//! placement, add/remove of a widget subtree, plus a snapshot undo/redo
//! document wrapper with gesture grouping (one undo step per gesture). No
//! renderer, no ImGui - the panel (EditorUiEditorPanel) drives these; the tests
//! exercise them headlessly.

#include "engine_gui/GuiLayout.h"
#include "core_util/String.h"
#include "core_util/UiLayout.h"

#include <vector>

namespace OrkigeEditor
{
	//! @brief one `.oui` widget kind the palette offers (the grammar's widget
	//! [Type]s). ONE list - the sole source the palette enumerates; extend it
	//! (and GuiFactory's dispatch) when the widget set grows.
	struct UiWidgetKind
	{
		char const*	type;	//!< the .oui section [Type] token (lower-case canonical)
		char const*	label;	//!< the human palette label
	};

	//! the ordered palette kinds (label/button/checkbox/slider/... - the set
	//! GuiFactory::loadLayoutImpl creates). @see uiWidgetKindCount
	std::vector<UiWidgetKind> uiWidgetKinds();

	//! @brief the Font Awesome 6 glyph (a UTF-8 sequence, never null/empty) for a
	//! widget @p type - the per-kind icon the UI Editor's widget tree draws in each
	//! row. Every codepoint returned lives in EditorTheme's ICON_GLYPH_RANGES (so the
	//! atlas rasterises it, not a tofu box) - a unit test mirrors that list, the same
	//! coupled-pair contract fileFormatIcon keeps. An unknown/empty type falls back
	//! to the generic control glyph. Pure.
	char const* uiWidgetKindIcon(Orkige::String const& type);

	//! @brief a resolved widget rect for hit-testing (surface pixels, origin
	//! top-left - the DrawLayer2D convention the runtime readback uses). Mirrors
	//! GuiPreviewWidgetRect so the pure core does not depend on the stage header.
	//! @p z / @p depth carry the picking priority: @p z is the widget's render
	//! layer (the `.oui` `z` key), @p depth its nesting depth (the length of its
	//! `parent` chain). Both default to 0, so the legacy `{id,l,t,w,h}` aggregate
	//! init keeps the plain painter-order behaviour.
	struct UiRect
	{
		Orkige::String	id;
		float			left = 0.0f;
		float			top = 0.0f;
		float			width = 0.0f;
		float			height = 0.0f;
		float			z = 0.0f;		//!< render layer (higher = drawn on top)
		int				depth = 0;		//!< parent-chain nesting depth (child > parent)
	};

	//! @brief the topmost widget id at (@p px, @p py). Among the rects CONTAINING
	//! the point the winner is the one on top: higher @p z first, then the DEEPER
	//! widget (a child inside its parent wins at equal z - a button inside a decor
	//! panel is selectable, never swallowed by the parent), then painter order (a
	//! later rect wins). With every rect at the default z==0/depth==0 this is
	//! exactly "the last matching rect wins". Returns "" when the point hits
	//! nothing.
	Orkige::String hitTestWidget(std::vector<UiRect> const& rects,
		float px, float py);

	//! @brief the WHOLE picking stack at (@p px, @p py): every rect CONTAINING the
	//! point, ordered exactly the way hitTestWidget ranks them - topmost FIRST
	//! (higher @p z, then the DEEPER widget, then the later painter index), so
	//! `front()` equals hitTestWidget's winner and each following id sits one layer
	//! below. The Alt+click stack-cycle picks its next selection from this list.
	//! Returns the ids only (empty when the point hits nothing).
	std::vector<Orkige::String> hitTestAllWidgets(std::vector<UiRect> const& rects,
		float px, float py);

	//! @brief the next selection an Alt+click makes when cycling a picking @p stack
	//! (topmost first, @see hitTestAllWidgets): the id ONE STEP DOWN from @p current,
	//! wrapping back to the topmost after the bottom. When @p current is not in the
	//! stack (or empty), returns the topmost - so the FIRST Alt+click lands on top
	//! and each further one descends, reaching a buried parent behind its children.
	//! An empty stack returns "". Pure.
	Orkige::String cycleStackSelection(std::vector<Orkige::String> const& stack,
		Orkige::String const& current);

	//! @brief the outcome of a widget-TREE row click (the panel row / the hook).
	//! @c Replace swaps the whole selection for the clicked row (a plain click on
	//! an unselected row - the single-select default); @c Toggle flips the row's
	//! membership in the ordered set. Pure decision, shared by the panel and the
	//! headless tests. @see uiTreeClickAction
	enum class UiTreeClickAction { Replace, Toggle };

	//! @brief decide a tree-row click: a modifier click (@p additive: Shift/Ctrl/
	//! Cmd) ALWAYS toggles the row in the set; a plain click on an ALREADY selected
	//! row toggles it too (so a re-click deselects it - the toggle-off gesture);
	//! any other plain click replaces the selection with the row. Pure.
	UiTreeClickAction uiTreeClickAction(bool alreadySelected, bool additive);

	//! @brief the grab a pointer is over: the body (a move) or one of the eight
	//! resize handles. Corners beat edges; the interior is a Move; outside the
	//! rect grown by @p grab is None.
	enum class UiHandle
	{
		None,
		Move,
		Left, Right, Top, Bottom,
		TopLeft, TopRight, BottomLeft, BottomRight
	};

	//! @brief which handle of @p r is under (@p px, @p py)? @p grab is the handle
	//! half-size in surface pixels (the pointer tolerance around each edge/corner).
	UiHandle handleAt(UiRect const& r, float px, float py, float grab);

	//! @brief the on-screen placement of the composited overlay image plus the
	//! surface it represents - the pure subset of the panel's UiEditCanvas the
	//! adornment geometry needs. Kept here so the surface->screen rect transform
	//! AND the adornment-bounds computation are pure and unit-tested without ImGui.
	struct UiCanvasPlacement
	{
		float	imageX = 0.0f;		//!< screen x of the image top-left
		float	imageY = 0.0f;		//!< screen y of the image top-left
		float	drawW = 1.0f;		//!< image width on screen
		float	drawH = 1.0f;		//!< image height on screen
		float	surfaceW = 1.0f;	//!< the overlay RTT width (pixels)
		float	surfaceH = 1.0f;	//!< the overlay RTT height (pixels)
	};

	//! @brief map a surface-pixel rect to the on-screen image rect - the ONE
	//! canvas transform every adornment draws through. A full-surface rect
	//! (left 0, width surfaceW) maps EXACTLY to the image ([imageX, imageX+drawW])
	//! by construction, so a stretch widget never draws wider than the device
	//! screen; the clip that keeps edge grips inside the canvas is the panel's
	//! PushClipRect over this same image rect. Pure.
	UiRect mapSurfaceRectToScreen(UiCanvasPlacement const& c,
		UiRect const& surfaceRect);

	//! @brief the screen-space bounding box the selection adornments occupy: the
	//! union of the mapped selection outlines grown by @p handlePad (the resize
	//! grips / anchor grips / pivot dot all sit at or just past the outline and
	//! parent corners). The panel clips its draw list to the canvas image rect, so
	//! this box is what WOULD be drawn unclipped - a test/selfcheck asserts it
	//! against the canvas to prove the mapping is in-surface and the clip is what
	//! bounds the edge grips. Empty selection => a zero-size box at the origin.
	UiRect adornmentBoundsScreen(UiCanvasPlacement const& c,
		std::vector<UiRect> const& selectionSurfaceRects, float handlePad);

	//! @brief a section's geometry model: Layout when it carries any explicit
	//! rect-anchor key (anchor / anchorMin / pivot / offsets / anchoredPos /
	//! sizeDelta), else Absolute (the legacy position/size pair).
	enum class UiGeomMode { Absolute, Layout };
	UiGeomMode geomMode(Orkige::GuiLayoutSection const& s);

	//! @brief parse a named anchor preset ("center", "stretchall", ...). Returns
	//! false + LAP_TOPLEFT on an unknown name (the widget default).
	bool parseAnchorPreset(Orkige::String const& name,
		Orkige::LayoutAnchorPreset& out);

	//! @brief the rect-anchor node a section expresses (anchor preset / anchorMin
	//! / anchorMax / pivot / offsets / anchoredPos / sizeDelta), applied in the
	//! same order GuiFactory does. Absolute-only sections resolve to the default
	//! top-left node. Pure - used by the panel's adornments and the tests.
	Orkige::LayoutNode sectionLayoutNode(Orkige::GuiLayoutSection const& s);

	//! @brief move a section by a surface-pixel delta, honest per mode and
	//! anchor-preserving. @p layoutScale is design-units -> surface-pixels (the
	//! resolver's referenceScale for the surface), so the delta is converted to
	//! design units before it touches the file. @p snapDesign > 0 snaps the moved
	//! coordinate to a design-unit grid. Layout mode edits the section's own
	//! geometry form (offsets in place, else anchoredPos); Absolute edits position.
	void applyMove(Orkige::GuiLayoutSection& s, float dxPx, float dyPx,
		float layoutScale, float snapDesign);

	//! @brief resize a section by dragging @p handle a surface-pixel delta.
	//! Layout/offsets form moves the dragged edge(s); Layout/friendly form grows
	//! sizeDelta about the pivot; Absolute edits size (and position for a
	//! left/top drag). Same conversion/snap contract as applyMove.
	void applyResize(Orkige::GuiLayoutSection& s, UiHandle handle,
		float dxPx, float dyPx, float layoutScale, float snapDesign);

	//=========================================================
	//=== alignment tooling (pure, resolver-pinned) ===========
	//=========================================================

	//! @brief the modifier variants a preset-gizmo click carries. Plain click
	//! sets the anchor only (offsets untouched, so the widget follows its new
	//! anchor). @c alsoPivot ALSO moves the pivot to the preset point; @c
	//! alsoKeepRect ALSO recomputes offsets so the on-screen rect is unchanged
	//! (the widget stays put under the new anchor). The two compose.
	struct AnchorPresetMods
	{
		bool	alsoPivot = false;		//!< move the pivot to the preset point too
		bool	alsoKeepRect = false;	//!< keep the on-screen rect (recompute offsets)
	};

	//! @brief the unit-square point a preset visually centres on (the midpoint
	//! of its anchor rect: the fraction for a point anchor, the band centre for
	//! a stretch). Used to place the pivot on an alsoPivot click.
	Orkige::LayoutVec2 anchorPresetPoint(Orkige::LayoutAnchorPreset preset);

	//! @brief apply an anchor preset to @p s, honouring @p mods. @p parentRect
	//! (surface px) and @p layoutScale (design->surface) are the resolve context
	//! the keep-rect recomputation needs. Writes back in the section's own
	//! geometry form (offsets vs friendly) and drops any raw anchorMin/anchorMax
	//! in favour of the named `anchor` key. Pure.
	void applyAnchorPresetToSection(Orkige::GuiLayoutSection& s,
		Orkige::LayoutAnchorPreset preset, AnchorPresetMods mods,
		Orkige::LayoutRect const& parentRect, float layoutScale);

	//! @brief which anchor-rect corner an on-canvas anchor triangle drags.
	//! Min = (anchorMin.x, anchorMin.y), Max = (anchorMax.x, anchorMax.y),
	//! the two mixed corners carry one component of each.
	enum class UiAnchorCorner { Min, Max, MinXMaxY, MaxXMinY };

	//! @brief drag an anchor triangle to a new parent-relative fraction
	//! (@p fracX, @p fracY in 0..1, clamped). Updates anchorMin/anchorMax and
	//! recomputes offsets so the widget's on-screen rect does not jump. Writes
	//! the raw anchorMin/anchorMax (a custom anchor drops the named preset).
	void applyAnchorDrag(Orkige::GuiLayoutSection& s, UiAnchorCorner corner,
		float fracX, float fracY, Orkige::LayoutRect const& parentRect,
		float layoutScale);

	//! @brief drag the pivot dot to (@p pivotX, @p pivotY) in 0..1 (clamped),
	//! keeping the on-screen rect visually fixed (offsets stay; a friendly-form
	//! widget's anchoredPos is recomputed from the fixed offsets + new pivot).
	void applyPivotDrag(Orkige::GuiLayoutSection& s, float pivotX, float pivotY);

	//! @brief the alignment ops, taken against the KEY object's matching edge
	//! or centre (the key is rects[0] and never moves).
	enum class UiAlignOp { Left, HCenter, Right, Top, VCenter, Bottom };
	//! @brief distribute so the gaps between consecutive rects are equal on the
	//! axis (the two extreme rects hold; needs >= 3 rects to do anything).
	enum class UiDistributeOp { Horizontal, Vertical };

	//! @brief the per-rect surface-px translation an align produces. @p rects[0]
	//! is the key and gets (0,0); every other rect gets the delta that snaps its
	//! matching edge/centre to the key's. Operates purely on resolved rects, so
	//! it is space-agnostic (a screen-space translation the caller replays into
	//! each widget's own geometry via applyMove).
	std::vector<Orkige::LayoutVec2> alignDeltas(std::vector<UiRect> const& rects,
		UiAlignOp op);
	//! @brief the per-rect surface-px translation an even distribute produces.
	std::vector<Orkige::LayoutVec2> distributeDeltas(
		std::vector<UiRect> const& rects, UiDistributeOp op);

	//! @brief the ids whose rect intersects the marquee (surface px, any corner
	//! order), returned in painter order (the rects' own order).
	std::vector<Orkige::String> widgetsInMarquee(std::vector<UiRect> const& rects,
		float x0, float y0, float x1, float y1);

	//! @brief one smart-guide candidate line. @c vertical => a vertical line at
	//! x == @c pos (a left/centre/right alignment source); else a horizontal
	//! line at y == @c pos.
	struct UiGuide
	{
		bool	vertical = true;
		float	pos = 0.0f;
	};

	//! @brief the guide candidates a drag can snap to: every sibling's
	//! left/centre/right (vertical) and top/centre/bottom (horizontal), the
	//! parent rect's edges + centre, and - when @p hasDesignCenter - the design
	//! resolution centre. Surface px throughout.
	std::vector<UiGuide> guideCandidates(std::vector<UiRect> const& others,
		UiRect const& parentRect, bool hasDesignCenter,
		float designCenterX, float designCenterY);

	//! @brief the snap a moving rect takes toward the nearest candidate on each
	//! axis within @p threshold (surface px). dx/dy is the correction to apply
	//! to the moving rect; guideX/guideY carry the snapped line for drawing.
	struct UiSnap
	{
		float	dx = 0.0f;
		float	dy = 0.0f;
		bool	snappedX = false;
		bool	snappedY = false;
		float	guideX = 0.0f;
		float	guideY = 0.0f;
	};
	UiSnap snapToGuides(UiRect const& moving, std::vector<UiGuide> const& candidates,
		float threshold);

	//! @brief one row the sprite picker popup offers, in draw order (@see
	//! spritePickerEntries). @c value is what a pick writes to the widget's
	//! `sprite` key ("" clears it to none); @c label is the row text.
	struct UiSpritePickEntry
	{
		Orkige::String	value;			//!< written to `sprite` on pick ("" = none)
		Orkige::String	label;			//!< the row label ("(none)" / the name / use "x")
		bool			isNone = false;	//!< the leading clear entry (no thumbnail)
		bool			isCustom = false;//!< a free-text name the atlas has not loaded
	};

	//! @brief build the ordered sprite-picker entries: a leading "(none)" clear,
	//! then every atlas sprite whose name CONTAINS @p filter (case-insensitive,
	//! atlas order preserved), and - when @p filter is a non-blank single-token
	//! name that is not already an exact atlas entry - a trailing free-text entry
	//! carrying the typed name verbatim, so a sprite the live atlas has not loaded
	//! (classic / headless, where no view enumerates the atlas) stays selectable.
	//! Pure: the panel draws the returned rows, the unit test asserts the
	//! filtering / ordering / free-text contract.
	std::vector<UiSpritePickEntry> spritePickerEntries(
		std::vector<Orkige::String> const& atlasSprites,
		Orkige::String const& filter);

	//! @brief a fresh section for @p type (a palette kind) with sane defaults and
	//! an id unique within @p doc; @p parentId (when non-empty and present) is
	//! stamped as the widget's parent. An unknown/empty type falls back to a
	//! decorwidget. The section is NOT inserted - the caller places it.
	Orkige::GuiLayoutSection paletteSection(Orkige::GuiLayoutDoc const& doc,
		Orkige::String const& type, Orkige::String const& parentId);

	//! @brief the parent a NEW widget lands under when the Add flow opens. Normally
	//! the current @p selection IS the parent (add-under-selection). The exception
	//! keeps repeated adds from building an accidental parent CHAIN: after an add the
	//! new widget becomes the selection, so if @p selectionIsLastCreated (the current
	//! selection is exactly the widget the previous add just made, untouched since),
	//! the destination is that previous add's OWN parent (@p lastConfirmedParent) -
	//! so three adds in a row become SIBLINGS, not a chain. Pure decision, captured
	//! ONCE when the picker opens; a "" result means the widget lands at the root.
	Orkige::String addDestinationParent(Orkige::String const& selection,
		bool selectionIsLastCreated, Orkige::String const& lastConfirmedParent);

	//! index of the section whose id is @p id (case-sensitive), or -1
	int sectionIndex(Orkige::GuiLayoutDoc const& doc, Orkige::String const& id);

	//! @brief remove the widget @p id AND every descendant (a section naming it,
	//! transitively, as `parent`). Returns the removed ids (root first). A no-op
	//! that returns empty when @p id is absent.
	std::vector<Orkige::String> removeWidgetSubtree(Orkige::GuiLayoutDoc& doc,
		Orkige::String const& id);

	//! @brief is @p id a well-formed, UNIQUE widget id for @p doc? A name must be
	//! non-empty, carry no whitespace (the `.oui` header is `[Type id]`, so a
	//! space would split it) and not collide with any EXISTING widget id other
	//! than @p allowSelf (its own current name, so a no-op rename passes). Sets
	//! @p error with the human reason on failure. Pure.
	bool isValidWidgetName(Orkige::GuiLayoutDoc const& doc, Orkige::String const& id,
		Orkige::String const& allowSelf, Orkige::String& error);

	//! @brief may @p childId be reparented under @p newParentId without forming a
	//! cycle? False when @p childId is absent, equals @p newParentId, or @p
	//! newParentId is a DESCENDANT of @p childId (reparenting an ancestor under its
	//! own descendant would orphan the subtree into a loop). An empty @p newParentId
	//! (drop onto the tree background = reparent to root) is valid for any existing
	//! child; a non-empty parent must itself exist. Pure - the tree's drag-drop drop
	//! target gates on it and the headless test proves the refusal.
	bool canReparentWidget(Orkige::GuiLayoutDoc const& doc,
		Orkige::String const& childId, Orkige::String const& newParentId);

	//! @brief reparent @p childId under @p newParentId ("" = root) inside @p doc,
	//! keeping its ON-SCREEN rect visually fixed where the geometry form allows. @p
	//! oldParentRect / @p newParentRect (surface px) + @p layoutScale are the resolve
	//! context: a Layout-mode child's offsets/anchoredPos are recomputed against the
	//! new parent (resolveRect is unchanged), an Absolute child's position is shifted
	//! by the parent-origin delta. Sets/removes the `parent` key. Refuses (returns
	//! false + @p error, mutates nothing) on a cycle or a missing child (@see
	//! canReparentWidget). Pure; the caller brackets ONE undo step.
	bool reparentWidget(Orkige::GuiLayoutDoc& doc, Orkige::String const& childId,
		Orkige::String const& newParentId, Orkige::LayoutRect const& oldParentRect,
		Orkige::LayoutRect const& newParentRect, float layoutScale,
		Orkige::String& error);

	//! @brief rename the widget @p oldId to @p newId: sets the section's id AND
	//! rewrites every child's `parent` reference so the tree stays intact.
	//! Enforces uniqueness (@see isValidWidgetName) - fails with @p error and
	//! changes nothing on an empty/whitespace/colliding name or a missing @p oldId.
	//! A rename to the same id is a successful no-op. Pure; round-trips through
	//! GuiLayout::serialize.
	bool renameWidget(Orkige::GuiLayoutDoc& doc, Orkige::String const& oldId,
		Orkige::String const& newId, Orkige::String& error);

	//! @brief the `.oui` document being edited: the parsed GuiLayoutDoc plus a
	//! snapshot-based undo/redo history with gesture grouping. The document is a
	//! small text file, so a full-text snapshot per gesture is robust and
	//! guarantees the serialize() round-trip; a drag brackets many mutations
	//! between one beginEdit()/commitEdit() pair and collapses to ONE undo step
	//! (the scene gizmo contract, for a separate document off the scene's own
	//! undo stack).
	class UiEditDoc
	{
	public:
		//! parse @p text as the document; clears the history and marks it saved.
		//! Returns false + @p error on a malformed file (the doc is left empty).
		bool load(Orkige::String const& text, Orkige::String& error);
		//! the canonical serialization of the current document
		Orkige::String text() const;

		Orkige::GuiLayoutDoc const& doc() const { return this->mDoc; }
		Orkige::GuiLayoutDoc& doc() { return this->mDoc; }

		//! capture the pre-edit snapshot (once per gesture); the caller then
		//! mutates doc() freely. Nested begins fold into the outer gesture.
		void beginEdit();
		//! @brief begin a COALESCING gesture keyed by @p key: when the previous
		//! commit carried the same key (and nothing non-coalesced happened since),
		//! this gesture MERGES into it instead of pushing a fresh undo step - the
		//! arrow-nudge burst contract, the same way a drag's many mutations fold
		//! into one step. A different key (or any plain beginEdit/undo/redo in
		//! between) starts a new step.
		void beginCoalesced(Orkige::String const& key);
		//! close the gesture: push ONE undo entry iff the text actually changed
		//! (a no-op gesture leaves the history untouched), clearing the redo stack.
		void commitEdit();

		bool canUndo() const { return !this->mUndo.empty(); }
		bool canRedo() const { return !this->mRedo.empty(); }
		//! restore the previous / next snapshot (no-op when the stack is empty)
		void undo();
		void redo();

		//! has the document changed since the last load()/markSaved()?
		bool dirty() const;
		//! adopt the current text as the saved baseline (after a successful write)
		void markSaved();

	private:
		Orkige::GuiLayoutDoc	mDoc;
		std::vector<Orkige::String>	mUndo;
		std::vector<Orkige::String>	mRedo;
		Orkige::String			mSaved;		//!< text at the last save
		Orkige::String			mPending;	//!< pre-edit snapshot during a gesture
		Orkige::String			mPendingKey;	//!< coalesce key of the open gesture
		Orkige::String			mLastKey;	//!< coalesce key of the last commit ("" = none)
		bool					mEditing = false;
	};
}

#endif //__EditorUiEdit_h__26_7_2026__12_00_00__
