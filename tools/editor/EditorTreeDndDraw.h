/********************************************************************
	created:	Wednesday 2026/07/29 at 12:00
	filename: 	EditorTreeDndDraw.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorTreeDndDraw_h__29_7_2026__12_00_00__
#define __EditorTreeDndDraw_h__29_7_2026__12_00_00__

//! @file EditorTreeDndDraw.h
//! @brief the SHARED ImGui drawing for the editor's tree drag-drop visual
//! language, used by BOTH the visual `.oui` editor's widget tree and the scene
//! GameObject Hierarchy so the two read identically: a translucent lifted-row
//! GHOST following the cursor, and a per-row DROP CUE - a full-row highlight for
//! an INTO (child) drop, a thin insertion LINE (indented to the target depth) for
//! a BEFORE/AFTER (sibling) drop. Header-only inline (imgui-side), zoned by the
//! pure EditorTreeDnd classifier.

#include "EditorTreeDnd.h"

#include "imgui.h"

#include <cstdio>

namespace OrkigeEditor
{
	//! @brief draw the lifted-row drag ghost on the foreground draw list at the
	//! cursor (offset a little), a compact translucent row: the kind @p icon glyph
	//! then the @p name. Called every frame the drag is live (from the source row's
	//! BeginDragDropSource), so it FOLLOWS the mouse.
	inline void drawTreeDragGhost(char const* icon, char const* name)
	{
		ImDrawList* fg = ImGui::GetForegroundDrawList();
		const ImVec2 m = ImGui::GetMousePos();
		const ImVec2 at(m.x + 16.0f, m.y + 8.0f);
		char buf[256];
		std::snprintf(buf, sizeof(buf), "%s  %s", icon ? icon : "",
			name ? name : "");
		const ImVec2 ts = ImGui::CalcTextSize(buf);
		const float padX = 8.0f, padY = 4.0f;
		const ImVec2 bmin(at.x - padX, at.y - padY);
		const ImVec2 bmax(at.x + ts.x + padX, at.y + ts.y + padY);
		fg->AddRectFilled(bmin, bmax, IM_COL32(40, 44, 52, 205), 4.0f);
		fg->AddRect(bmin, bmax, IM_COL32(120, 170, 255, 215), 4.0f);
		fg->AddText(at, IM_COL32(236, 239, 246, 235), buf);
	}

	//! @brief draw the drop CUE for a target row spanning [@p rowMin, @p rowMax]
	//! (screen px). @c Into fills + outlines the whole row (drop as a child); @c
	//! Before / @c After draw a thin insertion line at the row's top / bottom edge,
	//! its left edge at @p indentX (the target's depth indent - the line's indent
	//! tells which parent level the sibling insert lands at).
	inline void drawTreeDropCue(ImVec2 rowMin, ImVec2 rowMax, float indentX,
		TreeDropZone zone)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 accent = IM_COL32(120, 170, 255, 255);
		if(zone == TreeDropZone::Into)
		{
			dl->AddRectFilled(rowMin, rowMax, IM_COL32(120, 170, 255, 46), 3.0f);
			dl->AddRect(rowMin, rowMax, accent, 3.0f, 0, 1.5f);
		}
		else
		{
			const float y = (zone == TreeDropZone::Before) ? rowMin.y : rowMax.y;
			dl->AddLine(ImVec2(indentX, y), ImVec2(rowMax.x, y), accent, 2.0f);
			dl->AddCircleFilled(ImVec2(indentX, y), 3.0f, accent);
		}
	}
}

#endif //__EditorTreeDndDraw_h__29_7_2026__12_00_00__
