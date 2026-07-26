/********************************************************************
	created:	Saturday 2026/07/26 at 12:00
	filename: 	EditorUiEditorPanel.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the visual `.oui` editor's edit mode hosted by the Game Preview
				panel (@see EditorUiEditorPanel.h).
*********************************************************************/

#include "EditorUiEditorPanel.h"
#include "GamePreviewStage.h"
#include "GuiPreviewStage.h"
#include "EditorTheme.h"

#include <core_util/UiLayout.h>

#include <imgui.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace OrkigeEditor
{
	using Orkige::String;
	using Orkige::GuiLayoutDoc;
	using Orkige::GuiLayoutSection;

	namespace
	{
		//! the overlay's resolved rects as the pure hit-test/adornment input
		std::vector<UiRect> rectsFor(GamePreviewStage& stage)
		{
			std::vector<UiRect> out;
			for(GuiPreviewWidgetRect const& r : stage.getOverlayWidgetRects())
			{
				out.push_back({ r.id, r.left, r.top, r.width, r.height });
			}
			return out;
		}
		//! the selected widget's current overlay rect (surface px), or a zero rect
		bool rectOf(GamePreviewStage& stage, String const& id, UiRect& out)
		{
			for(GuiPreviewWidgetRect const& r : stage.getOverlayWidgetRects())
			{
				if(r.id == id)
				{
					out = { r.id, r.left, r.top, r.width, r.height };
					return true;
				}
			}
			return false;
		}
		//! write text to projectRoot/relPath (binary, LF preserved)
		bool writeFile(String const& projectRoot, String const& relPath,
			String const& text, String& error)
		{
			const String path = projectRoot + "/" + relPath;
			std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
			if(!out)
			{
				error = "could not open '" + relPath + "' for writing";
				return false;
			}
			out << text;
			return static_cast<bool>(out);
		}
		//! persist the document + reload the overlay so the canvas is WYSIWYG.
		//! Each edit gesture flows through here (the file is the truth; the panel's
		//! own mtime watcher and the Play hot-reload path both pick the write up).
		bool persist(UiEditSession& s, GamePreviewStage& stage, String& error)
		{
			if(!writeFile(s.projectRoot, s.relPath, s.doc.text(), error))
			{
				return false;
			}
			s.doc.markSaved();
			stage.setOverlayScreen(s.projectRoot, s.relPath, error);
			return true;
		}
		//! a mutable pointer to the selected section (NULL when none)
		GuiLayoutSection* selectedSection(UiEditSession& s)
		{
			const int idx = sectionIndex(s.doc.doc(), s.selected);
			if(idx < 0) { return NULL; }
			return &s.doc.doc().sections[static_cast<size_t>(idx)];
		}
	}
	//---------------------------------------------------------
	UiEditorDebug& uiEditorDebug()
	{
		static UiEditorDebug debug;
		return debug;
	}
	//---------------------------------------------------------
	float uiEditLayoutScale(UiEditSession const& s, float surfaceW, float surfaceH)
	{
		Orkige::LayoutScalePolicy policy;
		if(GuiLayoutSection const* layout = s.doc.doc().findSection("Layout"))
		{
			if(String const* design = layout->find("design"))
			{
				std::istringstream ds(*design);
				float dw = 0, dh = 0, match = 0;
				ds >> dw >> dh >> match;
				policy.designWidth = dw;
				policy.designHeight = dh;
				policy.matchWidthHeight = match;
			}
		}
		return policy.referenceScale(surfaceW, surfaceH);
	}
	//---------------------------------------------------------
	bool uiEditLoad(UiEditSession& s, GamePreviewStage& stage,
		String const& projectRoot, String const& relPath, String& error)
	{
		std::ifstream in((projectRoot + "/" + relPath).c_str(), std::ios::binary);
		if(!in)
		{
			error = "could not read '" + relPath + "'";
			return false;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		if(!s.doc.load(ss.str(), error))
		{
			s.loaded = false;
			return false;
		}
		s.projectRoot = projectRoot;
		s.relPath = relPath;
		s.selected.clear();
		s.loaded = true;
		s.dragging = false;
		// make sure the overlay is showing this exact file
		String showErr;
		stage.setOverlayScreen(projectRoot, relPath, showErr);
		return true;
	}
	//---------------------------------------------------------
	void uiEditSelect(UiEditSession& s, String const& widgetId)
	{
		if(widgetId.empty() || sectionIndex(s.doc.doc(), widgetId) >= 0)
		{
			s.selected = widgetId;
		}
	}
	namespace
	{
		//! the nudge/selfcheck path has no live surface; evaluate the design->
		//! surface scale at the design resolution itself (scale == 1 there, so a
		//! surface-pixel delta equals a design-unit delta - the honest default for
		//! a headless edit). A design-less doc also yields 1.
		float nudgeScale(UiEditSession const& s)
		{
			if(GuiLayoutSection const* layout = s.doc.doc().findSection("Layout"))
			{
				if(String const* design = layout->find("design"))
				{
					std::istringstream ds(*design);
					float dw = 0, dh = 0;
					ds >> dw >> dh;
					if(dw > 0 && dh > 0)
					{
						return uiEditLayoutScale(s, dw, dh);
					}
				}
			}
			return 1.0f;
		}
	}
	//---------------------------------------------------------
	void uiEditNudge(UiEditSession& s, float dxSurfacePx, float dySurfacePx)
	{
		GuiLayoutSection* section = selectedSection(s);
		if(!section) { return; }
		s.doc.beginEdit();
		applyMove(*section, dxSurfacePx, dySurfacePx, nudgeScale(s), 0.0f);
		s.doc.commitEdit();
		s.needsReload = true;
	}
	//---------------------------------------------------------
	String uiEditAddWidget(UiEditSession& s, String const& type)
	{
		s.doc.beginEdit();
		GuiLayoutSection section = paletteSection(s.doc.doc(), type, s.selected);
		const String id = section.id;
		s.doc.doc().sections.push_back(section);
		s.doc.commitEdit();
		s.selected = id;
		s.needsReload = true;
		return id;
	}
	//---------------------------------------------------------
	void uiEditDeleteSelected(UiEditSession& s)
	{
		if(s.selected.empty()) { return; }
		s.doc.beginEdit();
		removeWidgetSubtree(s.doc.doc(), s.selected);
		s.doc.commitEdit();
		s.selected.clear();
		s.needsReload = true;
	}
	//---------------------------------------------------------
	void uiEditUndo(UiEditSession& s)
	{
		s.doc.undo();
		if(sectionIndex(s.doc.doc(), s.selected) < 0) { s.selected.clear(); }
		s.needsReload = true;
	}
	//---------------------------------------------------------
	bool uiEditSave(UiEditSession& s, GamePreviewStage& stage, String& error)
	{
		return persist(s, stage, error);
	}
	//=========================================================
	//=== the canvas (adornments + mouse) =====================
	//=========================================================
	namespace
	{
		const ImU32 SEL_OUTLINE = IM_COL32(90, 200, 255, 255);
		const ImU32 SEL_HANDLE = IM_COL32(255, 255, 255, 255);
		const ImU32 HOVER_OUTLINE = IM_COL32(120, 200, 120, 180);

		//! map a surface-pixel rect to the on-screen image rect
		void mapRect(UiEditCanvas const& c, UiRect const& r,
			ImVec2& a, ImVec2& b)
		{
			const float sx = c.drawW / (c.surfaceW > 0 ? c.surfaceW : 1.0f);
			const float sy = c.drawH / (c.surfaceH > 0 ? c.surfaceH : 1.0f);
			a = ImVec2(c.imageX + r.left * sx, c.imageY + r.top * sy);
			b = ImVec2(c.imageX + (r.left + r.width) * sx,
				c.imageY + (r.top + r.height) * sy);
		}
		//! screen point -> surface point
		void toSurface(UiEditCanvas const& c, float px, float py,
			float& sxOut, float& syOut)
		{
			const float sx = c.surfaceW / (c.drawW > 0 ? c.drawW : 1.0f);
			const float sy = c.surfaceH / (c.drawH > 0 ? c.drawH : 1.0f);
			sxOut = (px - c.imageX) * sx;
			syOut = (py - c.imageY) * sy;
		}
		//! draw the eight resize handles of a mapped rect
		void drawHandles(ImDrawList* draw, ImVec2 const& a, ImVec2 const& b)
		{
			const float hx = (a.x + b.x) * 0.5f;
			const float hy = (a.y + b.y) * 0.5f;
			const ImVec2 pts[8] = {
				{ a.x, a.y }, { hx, a.y }, { b.x, a.y },
				{ a.x, hy },              { b.x, hy },
				{ a.x, b.y }, { hx, b.y }, { b.x, b.y }
			};
			for(ImVec2 const& p : pts)
			{
				draw->AddRectFilled(ImVec2(p.x - 3, p.y - 3),
					ImVec2(p.x + 3, p.y + 3), SEL_HANDLE);
				draw->AddRect(ImVec2(p.x - 3, p.y - 3),
					ImVec2(p.x + 3, p.y + 3), SEL_OUTLINE);
			}
		}
	}
	//---------------------------------------------------------
	void uiEditDrawCanvas(UiEditSession& s, GamePreviewStage& stage,
		UiEditCanvas const& canvas, ImDrawList* draw, float snapDesign)
	{
		if(!s.loaded) { return; }
		const std::vector<UiRect> rects = rectsFor(stage);

		// an invisible button over the image captures clicks/drags on the canvas
		ImGui::SetCursorScreenPos(ImVec2(canvas.imageX, canvas.imageY));
		ImGui::InvisibleButton("##ui_edit_canvas",
			ImVec2(canvas.drawW, canvas.drawH),
			ImGuiButtonFlags_MouseButtonLeft);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetIO().MousePos;

		// hover highlight (not while dragging)
		if(hovered && !s.dragging)
		{
			float mx = 0, my = 0;
			toSurface(canvas, mouse.x, mouse.y, mx, my);
			const String hoverId = hitTestWidget(rects, mx, my);
			for(UiRect const& r : rects)
			{
				if(r.id == hoverId && r.id != s.selected)
				{
					ImVec2 a, b;
					mapRect(canvas, r, a, b);
					draw->AddRect(a, b, HOVER_OUTLINE);
					break;
				}
			}
		}

		// begin a drag: pick a handle on the selection, else select what's clicked
		if(ImGui::IsItemActivated())
		{
			UiRect selRect;
			UiHandle handle = UiHandle::None;
			if(!s.selected.empty() && rectOf(stage, s.selected, selRect))
			{
				ImVec2 a, b;
				mapRect(canvas, selRect, a, b);
				const UiRect screenRect{ s.selected, a.x, a.y, b.x - a.x, b.y - a.y };
				handle = handleAt(screenRect, mouse.x, mouse.y, 6.0f);
			}
			if(handle == UiHandle::None)
			{
				// selection changes: hit-test the point
				float mx = 0, my = 0;
				toSurface(canvas, mouse.x, mouse.y, mx, my);
				s.selected = hitTestWidget(rects, mx, my);
				if(!s.selected.empty() && rectOf(stage, s.selected, selRect))
				{
					handle = UiHandle::Move;
				}
			}
			if(handle != UiHandle::None)
			{
				s.dragging = true;
				s.dragHandle = handle;
				s.dragStartX = mouse.x;
				s.dragStartY = mouse.y;
				s.dragRect = selRect;
			}
		}

		// live ghost while dragging
		if(s.dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			const float dScreenX = mouse.x - s.dragStartX;
			const float dScreenY = mouse.y - s.dragStartY;
			UiRect ghost = s.dragRect;
			// convert the screen delta to a surface delta for the ghost geometry
			const float toSx = canvas.surfaceW / (canvas.drawW > 0 ? canvas.drawW : 1.0f);
			const float toSy = canvas.surfaceH / (canvas.drawH > 0 ? canvas.drawH : 1.0f);
			const float dSx = dScreenX * toSx;
			const float dSy = dScreenY * toSy;
			if(s.dragHandle == UiHandle::Move) { ghost.left += dSx; ghost.top += dSy; }
			else
			{
				if(s.dragHandle == UiHandle::Left || s.dragHandle == UiHandle::TopLeft ||
					s.dragHandle == UiHandle::BottomLeft) { ghost.left += dSx; ghost.width -= dSx; }
				if(s.dragHandle == UiHandle::Right || s.dragHandle == UiHandle::TopRight ||
					s.dragHandle == UiHandle::BottomRight) { ghost.width += dSx; }
				if(s.dragHandle == UiHandle::Top || s.dragHandle == UiHandle::TopLeft ||
					s.dragHandle == UiHandle::TopRight) { ghost.top += dSy; ghost.height -= dSy; }
				if(s.dragHandle == UiHandle::Bottom || s.dragHandle == UiHandle::BottomLeft ||
					s.dragHandle == UiHandle::BottomRight) { ghost.height += dSy; }
			}
			ImVec2 a, b;
			mapRect(canvas, ghost, a, b);
			draw->AddRect(a, b, SEL_OUTLINE, 0.0f, 0, 2.0f);
		}

		// end the drag: apply the total delta once (one undo step), persist+reload
		if(s.dragging && ImGui::IsItemDeactivated())
		{
			const float dScreenX = mouse.x - s.dragStartX;
			const float dScreenY = mouse.y - s.dragStartY;
			const float toSx = canvas.surfaceW / (canvas.drawW > 0 ? canvas.drawW : 1.0f);
			const float toSy = canvas.surfaceH / (canvas.drawH > 0 ? canvas.drawH : 1.0f);
			const float dSx = dScreenX * toSx;
			const float dSy = dScreenY * toSy;
			GuiLayoutSection* section = selectedSection(s);
			if(section && (dScreenX != 0.0f || dScreenY != 0.0f))
			{
				const float scale = uiEditLayoutScale(s, canvas.surfaceW, canvas.surfaceH);
				s.doc.beginEdit();
				if(s.dragHandle == UiHandle::Move)
				{
					applyMove(*section, dSx, dSy, scale, snapDesign);
				}
				else
				{
					applyResize(*section, s.dragHandle, dSx, dSy, scale, snapDesign);
				}
				s.doc.commitEdit();
				String err;
				persist(s, stage, err);
			}
			s.dragging = false;
			s.dragHandle = UiHandle::None;
		}

		// selection outline + handles (from the live overlay rect)
		UiRect selRect;
		if(!s.selected.empty() && rectOf(stage, s.selected, selRect))
		{
			ImVec2 a, b;
			mapRect(canvas, selRect, a, b);
			draw->AddRect(a, b, SEL_OUTLINE, 0.0f, 0, 2.0f);
			if(!s.dragging)
			{
				drawHandles(draw, a, b);
			}
		}
	}
	//=========================================================
	//=== the sidebar (tree / properties / palette / save) ====
	//=========================================================
	namespace
	{
		//! a compact single-line string field editing entry @p key. The model
		//! updates live per keystroke (no reload); a gesture brackets the edit
		//! session (beginEdit on focus, commitEdit on blur) so a field edit is ONE
		//! undo step. Returns true on commit (blur-after-edit) so the caller
		//! persists + reloads once, not per keystroke.
		bool textField(UiEditDoc& doc, GuiLayoutSection& sec,
			char const* label, char const* key)
		{
			char buffer[256] = { 0 };
			if(String const* v = sec.find(key))
			{
				std::snprintf(buffer, sizeof(buffer), "%s", v->c_str());
			}
			ImGui::SetNextItemWidth(120.0f);
			const String id = String("##") + key;
			const bool changed = ImGui::InputText(id.c_str(), buffer, sizeof(buffer));
			// query the InputText's own activation BEFORE drawing the label
			const bool activated = ImGui::IsItemActivated();
			const bool committed = ImGui::IsItemDeactivatedAfterEdit();
			if(activated)
			{
				doc.beginEdit();	// snapshot before the first keystroke
			}
			if(changed)
			{
				sec.set(key, buffer);
			}
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextDisabled(" %s", label);
			if(committed)
			{
				doc.commitEdit();
				return true;
			}
			return false;
		}
	}
	//---------------------------------------------------------
	void uiEditDrawSidebar(UiEditSession& s, GamePreviewStage& stage, float width)
	{
		ImGui::BeginChild("##ui_edit_sidebar", ImVec2(width, 0.0f), true);

		// toolbar: undo/redo + a save/refresh + the dirty dot
		if(ImGui::SmallButton("Undo") && s.doc.canUndo())
		{
			uiEditUndo(s);
			String err; persist(s, stage, err);
		}
		ImGui::SameLine();
		if(ImGui::SmallButton("Redo") && s.doc.canRedo())
		{
			s.doc.redo();
			if(sectionIndex(s.doc.doc(), s.selected) < 0) { s.selected.clear(); }
			String err; persist(s, stage, err);
		}
		ImGui::SameLine();
		if(ImGui::SmallButton("Save"))
		{
			String err; persist(s, stage, err);
		}
		ImGui::SameLine();
		ImGui::TextDisabled(s.doc.dirty() ? "*" : "saved");

		ImGui::Separator();

		// the widget tree (the .oui section order; parenting shown by indent)
		ImGui::TextDisabled("Widgets");
		for(GuiLayoutSection const& sec : s.doc.doc().sections)
		{
			if(sec.id.empty()) { continue; }	// [Layout]/[Modal] etc.
			const bool child = sec.find("parent") != NULL;
			ImGui::PushID(sec.id.c_str());
			if(child) { ImGui::Indent(14.0f); }
			const String label = sec.id + "  (" + sec.type + ")";
			if(ImGui::Selectable(label.c_str(), s.selected == sec.id))
			{
				s.selected = sec.id;
			}
			if(child) { ImGui::Unindent(14.0f); }
			ImGui::PopID();
		}

		ImGui::Separator();

		// properties of the selection
		if(GuiLayoutSection* sec = selectedSection(s))
		{
			ImGui::TextDisabled("Properties: %s", sec->id.c_str());
			bool edited = false;
			edited |= textField(s.doc, *sec, "text", "text");
			edited |= textField(s.doc, *sec, "z", "z");
			edited |= textField(s.doc, *sec, "sprite", "sprite");
			if(geomMode(*sec) == UiGeomMode::Layout)
			{
				// anchor preset picker (a discrete change: its own gesture)
				String anchor = sec->find("anchor") ? *sec->find("anchor") : String("topleft");
				ImGui::SetNextItemWidth(120.0f);
				if(ImGui::BeginCombo("##anchor", anchor.c_str()))
				{
					char const* presets[] = { "topleft","top","topright","left",
						"center","right","bottomleft","bottom","bottomright",
						"stretchtop","stretchall" };
					for(char const* preset : presets)
					{
						if(ImGui::Selectable(preset, anchor == preset))
						{
							s.doc.beginEdit();
							sec->set("anchor", preset);
							s.doc.commitEdit();
							edited = true;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::TextDisabled(" anchor");
				if(sec->find("offsets")) { edited |= textField(s.doc, *sec, "offsets", "offsets"); }
				if(sec->find("anchoredPos")) { edited |= textField(s.doc, *sec, "anchoredPos", "anchoredPos"); }
				if(sec->find("sizeDelta")) { edited |= textField(s.doc, *sec, "sizeDelta", "sizeDelta"); }
			}
			else
			{
				edited |= textField(s.doc, *sec, "position", "position");
				edited |= textField(s.doc, *sec, "size", "size");
			}
			if(edited)
			{
				// the gesture is already committed by the field; persist once so the
				// canvas reflects it (and Play hot-reloads)
				String err; persist(s, stage, err);
			}
			if(ImGui::Button("Delete widget"))
			{
				uiEditDeleteSelected(s);
				String err; persist(s, stage, err);
			}
		}
		else
		{
			ImGui::TextDisabled("Select a widget to edit its properties.");
		}

		ImGui::Separator();

		// the palette: click a kind to add it under the selection (or root)
		ImGui::TextDisabled("Add widget");
		int column = 0;
		for(UiWidgetKind const& kind : uiWidgetKinds())
		{
			if(column++ % 2 != 0) { ImGui::SameLine(); }
			if(ImGui::SmallButton(kind.label))
			{
				uiEditAddWidget(s, kind.type);
				String err; persist(s, stage, err);
			}
		}

		ImGui::EndChild();
	}
}
