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
//! @brief the visual `.oui` editor - an EDIT MODE hosted by the Game Preview
//! panel. It reuses the ONE `.oui` render path (the panel's GamePreviewStage
//! overlay - a second live gui stack would trip the GuiManager singleton) and
//! the ONE document model (GuiLayoutDoc via EditorUiEdit's UiEditDoc), drawing
//! editor adornments (selection outline, resize handles) over the composited
//! image and a sidebar (widget tree, properties, palette, save) beside it.
//! Click-select hit-tests the resolved overlay rects; drag/handles edit the
//! LayoutNode/position anchor-preservingly; Save serialises through GuiLayout.

#include "EditorUiEdit.h"

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
		std::string		selected;		//!< selected widget id ("" = none)
		// in-progress drag
		bool			dragging = false;
		UiHandle		dragHandle = UiHandle::None;
		float			dragStartX = 0.0f;	//!< screen px at grab
		float			dragStartY = 0.0f;
		// the selected widget's overlay rect (surface px) captured at grab
		UiRect			dragRect;
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
	//! move the selected widget by a surface-pixel delta (one undo step)
	void uiEditNudge(UiEditSession& s, float dxSurfacePx, float dySurfacePx);
	//! add a palette widget of `type` (parented to the selection), select it
	std::string uiEditAddWidget(UiEditSession& s, std::string const& type);
	//! remove the selected widget subtree (one undo step)
	void uiEditDeleteSelected(UiEditSession& s);
	void uiEditUndo(UiEditSession& s);
	//! write the document to disk and reload the overlay (returns false + error)
	bool uiEditSave(UiEditSession& s, GamePreviewStage& stage, std::string& error);
	//! the geometry design->surface scale for the loaded document + surface size
	float uiEditLayoutScale(UiEditSession const& s, float surfaceW, float surfaceH);
	//! @}

	//! @brief draw the edit adornments over the canvas AND handle mouse
	//! select/drag/resize (an invisible button covers the image). Called by the
	//! Game Preview panel when edit mode is on and a screen is selected.
	void uiEditDrawCanvas(UiEditSession& s, GamePreviewStage& stage,
		UiEditCanvas const& canvas, ImDrawList* draw, float snapDesign);

	//! @brief draw the edit sidebar (tree / properties / palette / save+undo)
	//! inside the current ImGui region. @p width is the sidebar width in points.
	void uiEditDrawSidebar(UiEditSession& s, GamePreviewStage& stage, float width);
}

#endif //__EditorUiEditorPanel_h__26_7_2026__12_00_00__
