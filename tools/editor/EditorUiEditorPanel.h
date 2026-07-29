/********************************************************************
	created:	Saturday 2026/07/26 at 12:00
	filename: 	EditorUiEditorPanel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorUiEditorPanel_h__26_7_2026__12_00_00__
#define __EditorUiEditorPanel_h__26_7_2026__12_00_00__

//! @file EditorUiEditorPanel.h
//! @brief the visual `.oui` editor. Its EDIT MODE lives in the Preview panel
//! (the canvas + adornments, reusing the ONE `.oui` render path - the panel's
//! GamePreviewStage overlay, since a second live gui stack would trip the
//! GuiManager singleton) and its TOOL SURFACE is the dockable "UI Editor" panel
//! (widget tree, properties, anchor gizmo, align/distribute, add/delete, undo).
//! Both drive the ONE document model (GuiLayoutDoc via EditorUiEdit's UiEditDoc)
//! owned by the Preview panel and handed to the tool panel through
//! UiEditorPanelLink. Click-select hit-tests the resolved overlay rects;
//! drag/handles edit the LayoutNode/position anchor-preservingly; Save
//! serialises through GuiLayout.

#include "EditorUiEdit.h"

#include <set>
#include <string>

struct ImDrawList;

namespace OrkigeEditor
{
	class GamePreviewStage;

	//! @brief where the composited overlay image sits on screen and what surface
	//! it represents - the mapping the Game Preview panel already computed, passed
	//! to the edit code so canvas pixels convert to overlay surface pixels.
	struct UiEditCanvas
	{
		float	imageX = 0.0f;	//!< screen x of the image's top-left
		float	imageY = 0.0f;	//!< screen y of the image's top-left
		float	drawW = 1.0f;	//!< image width on screen
		float	drawH = 1.0f;	//!< image height on screen
		float	surfaceW = 1.0f;//!< the overlay RTT width in pixels
		float	surfaceH = 1.0f;//!< the overlay RTT height in pixels
	};

	//! @brief the live editing session (owned by the panel as a function static):
	//! the document, the selection, the in-progress drag and the file identity.
	struct UiEditSession
	{
		UiEditDoc		doc;			//!< the parsed + undoable document
		bool			loaded = false;	//!< a file is loaded into `doc`
		std::string		projectRoot;	//!< the project the file belongs to
		std::string		relPath;		//!< project-relative .oui path
		std::string		selected;		//!< the KEY widget id ("" = none); == selection[0]
		//! the ordered multi-selection (first = the align key object). `selected`
		//! mirrors selection.front(); an empty selection clears both.
		std::vector<std::string>	selection;
		//! the widget-tree fold state: ids whose TreeNode is COLLAPSED (default is
		//! open, so this is the exception set). Per-session, not persisted.
		std::set<std::string>		treeCollapsed;
		// a marquee in progress (surface px, set while dragging on empty canvas)
		bool			marquee = false;
		float			marqueeX0 = 0.0f, marqueeY0 = 0.0f;
		float			marqueeX1 = 0.0f, marqueeY1 = 0.0f;
		//! what a canvas drag manipulates (a widget body/handle, an anchor
		//! triangle, the pivot dot, or a marquee rubber-band)
		enum class DragKind { None, Widget, AnchorMin, AnchorMax,
			AnchorMinXMaxY, AnchorMaxXMinY, Pivot, Marquee };
		// in-progress drag
		bool			dragging = false;
		DragKind		dragKind = DragKind::None;
		UiHandle		dragHandle = UiHandle::None;	//!< the widget grab (Move/resize)
		float			dragStartX = 0.0f;	//!< screen px at grab
		float			dragStartY = 0.0f;
		// the selected widget's overlay rect (surface px) captured at grab
		UiRect			dragRect;
		// the parent rect + scale the anchor/pivot drag resolves against (grab-time)
		float			dragParentX = 0.0f, dragParentY = 0.0f;
		float			dragParentW = 1.0f, dragParentH = 1.0f;
		float			dragScale = 1.0f;
		//! a DEFERRED selection switch: set at a body press INSIDE the current
		//! selection (so the drag prefers the selected widget even when another sits
		//! on top). If the press releases WITHOUT crossing the drag threshold - a
		//! click, not a drag - the selection switches to @c pendingClickSelect (the
		//! topmost widget under the cursor), so a covering widget stays one click
		//! away. A real drag clears it and moves the selection instead.
		bool			hasPendingClick = false;
		std::string		pendingClickSelect;	//!< topmost id to select on a click-release
		bool			needsReload = false;	//!< the overlay must re-show after a save
	};

	//! @brief a last-draw debug seam (like GamePreviewPanelDebug) the selfcheck
	//! reads to assert the edit mode's state without touching ImGui internals.
	struct UiEditorDebug
	{
		bool		active = false;			//!< edit mode was drawn this frame
		bool		loaded = false;			//!< a document is loaded
		int			sectionCount = 0;		//!< sections in the document
		int			widgetRectCount = 0;	//!< resolved overlay rects available
		std::string	selected;				//!< the selected widget id
		bool		dirty = false;			//!< unsaved edits pending
		bool		canUndo = false;
		int			selectionCount = 0;		//!< widgets in the multi-selection
		//! the adornment CLIP the canvas draw applied this frame: true when
		//! PushClipRect(canvas image rect) bracketed every adornment draw, plus
		//! the clip rect itself and the pre-clip bounding box of the selection
		//! adornments (@see adornmentBoundsScreen) - the selfcheck reads these to
		//! prove the outline/handles/anchors never bleed past the canvas.
		bool		adornClipApplied = false;
		float		clipLeft = 0.0f, clipTop = 0.0f, clipRight = 0.0f, clipBottom = 0.0f;
		float		adornLeft = 0.0f, adornTop = 0.0f, adornRight = 0.0f, adornBottom = 0.0f;
		//! the KEY (selected) widget's LAYOUT BOX mapped to SCREEN pixels this
		//! frame - the seam a synthetic-input selfcheck reads to aim an SDL drag at
		//! a real resize grip (a grip sits at the box corners/edges). Zero when no
		//! widget is selected / the canvas did not draw.
		bool		hasSelScreen = false;
		float		selScreenLeft = 0.0f, selScreenTop = 0.0f;
		float		selScreenWidth = 0.0f, selScreenHeight = 0.0f;
		//! the canvas composite-image placement in SCREEN pixels this frame (the
		//! rect the InvisibleButton covers) + the overlay surface size - a
		//! synthetic-input selfcheck maps surface points to a click position and
		//! clicks the canvas centre to select a covering widget.
		float		canvasImageX = 0.0f, canvasImageY = 0.0f;
		float		canvasDrawW = 0.0f, canvasDrawH = 0.0f;
		float		canvasSurfaceW = 0.0f, canvasSurfaceH = 0.0f;
	};
	//! the process-wide edit-mode debug seam (@see UiEditorDebug)
	UiEditorDebug& uiEditorDebug();

	//! @brief a headless test hook: the selfcheck drives the session through the
	//! SAME code paths the mouse does (load / select / move / undo / add / save),
	//! so the edit loop is proven on both flavors without synthesising ImGui input.
	//! @{
	bool uiEditLoad(UiEditSession& s, GamePreviewStage& stage,
		std::string const& projectRoot, std::string const& relPath,
		std::string& error);
	void uiEditSelect(UiEditSession& s, std::string const& widgetId);
	//! @brief multi-selection edits (the ordered set, first = align key).
	//! @{
	//! toggle @p widgetId in the selection (shift-click): add to the end if
	//! absent, remove if present; keeps `selected` = the front (key).
	void uiEditSelectToggle(UiEditSession& s, std::string const& widgetId);
	//! replace the whole selection with @p ids (first becomes the key)
	void uiEditSetSelection(UiEditSession& s, std::vector<std::string> const& ids);
	//! @brief the widget-TREE row click seam (the panel row + the headless hook):
	//! a plain click single-selects @p widgetId, a plain click on the ALREADY
	//! selected row deselects it (toggle-off), and an @p additive click (Shift/
	//! Ctrl/Cmd) toggles it in the ordered set. Deselection is not an undo step.
	//! Routes the decision through the pure uiTreeClickAction, mutating the ONE
	//! selection seam so the canvas + panel stay coherent.
	void uiEditTreeSelect(UiEditSession& s, std::string const& widgetId,
		bool additive);
	//! select every widget whose rect intersects the marquee (surface px)
	void uiEditMarqueeSelect(UiEditSession& s, GamePreviewStage& stage,
		float x0, float y0, float x1, float y1);
	//! @}
	//! move the selected widget by a surface-pixel delta (one undo step)
	void uiEditNudge(UiEditSession& s, float dxSurfacePx, float dySurfacePx);
	//! @brief nudge the whole selection by a DESIGN-unit delta (arrow keys: 1,
	//! or 10 with shift). Consecutive bursts on the same selection fold into ONE
	//! undo step (the drag contract), so a held arrow is one entry.
	void uiEditNudgeKey(UiEditSession& s, float dxDesign, float dyDesign);
	//! align the selection to the key object's edge/centre (one undo step)
	void uiEditAlign(UiEditSession& s, GamePreviewStage& stage, UiAlignOp op);
	//! distribute the selection evenly on an axis (one undo step)
	void uiEditDistribute(UiEditSession& s, GamePreviewStage& stage,
		UiDistributeOp op);
	//! apply an anchor preset to the KEY widget with the gizmo modifiers
	void uiEditApplyAnchorPreset(UiEditSession& s, GamePreviewStage& stage,
		Orkige::LayoutAnchorPreset preset, AnchorPresetMods mods);
	//! add a palette widget of `type` (parented to the selection), select it
	std::string uiEditAddWidget(UiEditSession& s, std::string const& type);
	//! @brief the headless sprite-pick seam: set the KEY (selected) widget's
	//! `sprite` key to @p value ("" clears it to none) as ONE undo step, then
	//! persist + reload the overlay - the exact document mutation the Inspector's
	//! sprite-picker popup performs, exposed so the selfcheck drives a pick
	//! without synthesising ImGui combo input. Returns false + @p error when
	//! nothing is selected. (@see spritePickerEntries for the pure entry list the
	//! popup draws.)
	bool uiEditPickSprite(UiEditSession& s, GamePreviewStage& stage,
		std::string const& value, std::string& error);
	//! remove the selected widget subtree (one undo step)
	void uiEditDeleteSelected(UiEditSession& s);
	//! @brief rename the KEY (selected) widget to @p newId (one undo step +
	//! persist/reload; the selection follows to the new id). Enforces uniqueness -
	//! returns false + @p error and changes nothing on an empty/whitespace/
	//! colliding name (the caller shows the honest inline error).
	bool uiEditRenameSelected(UiEditSession& s, GamePreviewStage& stage,
		std::string const& newId, std::string& error);
	//! @brief reparent @p childId under @p newParentId ("" = root) as ONE undo step,
	//! keeping the widget's on-screen rect fixed where the geometry allows (@see
	//! reparentWidget), then persist + reload the overlay. Refuses a cycle / missing
	//! child honestly (returns false + @p error, changes nothing). When @p
	//! reorderAnchorId is non-empty the child is ALSO moved adjacent to it in
	//! serialize/paint order (@p reorderAfter picks before/after) - the between-rows
	//! sibling-reorder drop, folded into the SAME undo step. The widget-tree's
	//! drag-drop drop targets call this; the selfcheck drives it headlessly.
	bool uiEditReparent(UiEditSession& s, GamePreviewStage& stage,
		std::string const& childId, std::string const& newParentId,
		std::string& error, std::string const& reorderAnchorId = std::string(),
		bool reorderAfter = false);
	void uiEditUndo(UiEditSession& s);
	//! write the document to disk and reload the overlay (returns false + error)
	bool uiEditSave(UiEditSession& s, GamePreviewStage& stage, std::string& error);
	//! the geometry design->surface scale for the loaded document + surface size
	float uiEditLayoutScale(UiEditSession const& s, float surfaceW, float surfaceH);
	//! @}

	//! @brief draw the edit adornments over the canvas AND handle mouse
	//! select/drag/resize (an invisible button covers the image). Called by the
	//! Preview panel when Edit UI mode is on and a screen is selected.
	void uiEditDrawCanvas(UiEditSession& s, GamePreviewStage& stage,
		UiEditCanvas const& canvas, ImDrawList* draw, float snapDesign);

	//! @brief the once-per-frame hand-off from the Preview panel (which OWNS the
	//! edit session + the canvas) to the dockable UI Editor panel (the tool
	//! surface: tree / properties / anchor gizmo / align / add-delete / undo). The
	//! Preview panel fills this each frame it draws Edit UI; the loop clears it
	//! before the panels draw, so a stale pointer never survives a closed Preview.
	struct UiEditorPanelLink
	{
		UiEditSession*		session = nullptr;	//!< the live edit session (owned by the Preview panel)
		GamePreviewStage*	stage = nullptr;	//!< the overlay stage the tools resolve against
		bool				editActive = false;	//!< Edit UI is on with a screen loaded this frame
		//! the edit context holds keyboard focus (the UI Editor panel OR the
		//! Preview canvas) - drives whether global Cmd/Ctrl+Z routes to the doc
		bool				contextFocused = false;
		std::string			projectRoot;		//!< the loaded screen's project root
	};
	//! the process-wide Preview->UI-Editor link (@see UiEditorPanelLink)
	UiEditorPanelLink& uiEditorPanelLink();

	//! @brief draw the dockable "UI Editor" panel: the visual `.oui` editor's
	//! tool surface (widget tree, properties, anchor gizmo, align/distribute,
	//! add/delete and the undo/redo/save controls in its header). Meaningful only
	//! while a screen is open in the Preview panel's Edit UI mode - an honest
	//! empty state otherwise. Selection stays synced with the canvas both ways.
	void drawUiEditorPanel(bool* open);

	//! @brief the global-undo routing hooks the editor shortcut handler calls so
	//! Cmd/Ctrl+Z edits the `.oui` document (not the scene) whenever the edit
	//! context is focused. @{
	bool uiEditContextWantsUndo();	//!< editActive && the edit context is focused
	void uiEditUndoShared();		//!< undo the shared session + persist/reload
	void uiEditRedoShared();		//!< redo the shared session + persist/reload
	//! @}
}

#endif //__EditorUiEditorPanel_h__26_7_2026__12_00_00__
