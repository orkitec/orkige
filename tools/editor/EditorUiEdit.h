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

	//! @brief a resolved widget rect for hit-testing (surface pixels, origin
	//! top-left - the DrawLayer2D convention the runtime readback uses). Mirrors
	//! GuiPreviewWidgetRect so the pure core does not depend on the stage header.
	struct UiRect
	{
		Orkige::String	id;
		float			left = 0.0f;
		float			top = 0.0f;
		float			width = 0.0f;
		float			height = 0.0f;
	};

	//! @brief the topmost widget id at (@p px, @p py). The runtime returns rects
	//! in submission (painter's) order, so a LATER rect draws on top - the last
	//! matching rect wins. Returns "" when the point hits nothing.
	Orkige::String hitTestWidget(std::vector<UiRect> const& rects,
		float px, float py);

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

	//! @brief a fresh section for @p type (a palette kind) with sane defaults and
	//! an id unique within @p doc; @p parentId (when non-empty and present) is
	//! stamped as the widget's parent. An unknown/empty type falls back to a
	//! decorwidget. The section is NOT inserted - the caller places it.
	Orkige::GuiLayoutSection paletteSection(Orkige::GuiLayoutDoc const& doc,
		Orkige::String const& type, Orkige::String const& parentId);

	//! index of the section whose id is @p id (case-sensitive), or -1
	int sectionIndex(Orkige::GuiLayoutDoc const& doc, Orkige::String const& id);

	//! @brief remove the widget @p id AND every descendant (a section naming it,
	//! transitively, as `parent`). Returns the removed ids (root first). A no-op
	//! that returns empty when @p id is absent.
	std::vector<Orkige::String> removeWidgetSubtree(Orkige::GuiLayoutDoc& doc,
		Orkige::String const& id);

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
		bool					mEditing = false;
	};
}

#endif //__EditorUiEdit_h__26_7_2026__12_00_00__
