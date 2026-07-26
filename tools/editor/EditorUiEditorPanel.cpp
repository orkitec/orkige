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
#include <engine_render/RenderTexture.h>

#include <imgui.h>

#include <algorithm>
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
		//! keep `selected` == the selection front (the key). An empty set clears it.
		void syncKey(UiEditSession& s)
		{
			s.selected = s.selection.empty() ? std::string() : s.selection.front();
		}

		//=== resolved rects (live stage first, doc-resolve fallback) =====
		// The live overlay gives exact rects on the next flavor; classic/headless
		// has none, so the alignment tooling resolves each widget's rect from the
		// document against the ONE UiLayout resolver - the SAME math, so the two
		// paths agree and the tools work (and self-check) on both flavors.

		//! the surface the editor resolves against: the live target, else the doc's
		//! design resolution, else a neutral 1000x1000 (scale stays 1 there).
		void resolveSurface(UiEditSession const& s, GamePreviewStage& stage,
			float& outW, float& outH)
		{
			if(Orkige::optr<Orkige::RenderTexture> t = stage.getTarget())
			{
				if(t->getWidth() > 0 && t->getHeight() > 0)
				{
					outW = static_cast<float>(t->getWidth());
					outH = static_cast<float>(t->getHeight());
					return;
				}
			}
			outW = 1000.0f;
			outH = 1000.0f;
			if(GuiLayoutSection const* layout = s.doc.doc().findSection("Layout"))
			{
				if(String const* design = layout->find("design"))
				{
					std::istringstream ds(*design);
					float dw = 0, dh = 0;
					if((ds >> dw >> dh) && dw > 0 && dh > 0) { outW = dw; outH = dh; }
				}
			}
		}
		//! resolve a section's surface rect from the document, walking parents
		UiRect docRectOf(GuiLayoutDoc const& doc, GuiLayoutSection const& sec,
			float surfW, float surfH, float scale, int depth = 0)
		{
			Orkige::LayoutRect parent{ 0.0f, 0.0f, surfW, surfH };
			if(depth < 32)
			{
				if(String const* p = sec.find("parent"))
				{
					const int pidx = sectionIndex(doc, *p);
					if(pidx >= 0)
					{
						const UiRect pr = docRectOf(doc,
							doc.sections[static_cast<size_t>(pidx)],
							surfW, surfH, scale, depth + 1);
						parent = { pr.left, pr.top, pr.width, pr.height };
					}
				}
			}
			if(geomMode(sec) == UiGeomMode::Absolute)
			{
				float pos[2] = { 0, 0 };
				float sz[2] = { 0, 0 };
				if(String const* v = sec.find("position"))
				{
					std::istringstream in(*v); in >> pos[0] >> pos[1];
				}
				if(String const* v = sec.find("size"))
				{
					std::istringstream in(*v); in >> sz[0] >> sz[1];
				}
				return { sec.id, parent.x + pos[0] * scale, parent.y + pos[1] * scale,
					sz[0] * scale, sz[1] * scale };
			}
			const Orkige::LayoutRect r =
				Orkige::resolveRect(parent, sectionLayoutNode(sec), scale);
			return { sec.id, r.x, r.y, r.w, r.h };
		}
		//! rects for every widget section (live stage first, doc-resolve fallback)
		std::vector<UiRect> resolvedRects(UiEditSession const& s,
			GamePreviewStage& stage)
		{
			std::vector<UiRect> live;
			for(GuiPreviewWidgetRect const& r : stage.getOverlayWidgetRects())
			{
				live.push_back({ r.id, r.left, r.top, r.width, r.height });
			}
			if(!live.empty()) { return live; }
			float surfW = 1000.0f, surfH = 1000.0f;
			resolveSurface(s, stage, surfW, surfH);
			const float scale = uiEditLayoutScale(s, surfW, surfH);
			std::vector<UiRect> out;
			for(GuiLayoutSection const& sec : s.doc.doc().sections)
			{
				if(sec.id.empty()) { continue; }
				out.push_back(docRectOf(s.doc.doc(), sec, surfW, surfH, scale));
			}
			return out;
		}
		//! the parent surface rect the anchor tooling resolves against for @p id:
		//! the parent widget's rect if it has one, else the full surface.
		Orkige::LayoutRect parentRectOf(UiEditSession const& s,
			GamePreviewStage& stage, String const& id, float& outScale)
		{
			float surfW = 1000.0f, surfH = 1000.0f;
			resolveSurface(s, stage, surfW, surfH);
			outScale = uiEditLayoutScale(s, surfW, surfH);
			const int idx = sectionIndex(s.doc.doc(), id);
			if(idx >= 0)
			{
				GuiLayoutSection const& sec =
					s.doc.doc().sections[static_cast<size_t>(idx)];
				if(String const* p = sec.find("parent"))
				{
					const int pidx = sectionIndex(s.doc.doc(), *p);
					if(pidx >= 0)
					{
						const UiRect pr = docRectOf(s.doc.doc(),
							s.doc.doc().sections[static_cast<size_t>(pidx)],
							surfW, surfH, outScale);
						return { pr.left, pr.top, pr.width, pr.height };
					}
				}
			}
			return { 0.0f, 0.0f, surfW, surfH };
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
		if(widgetId.empty())
		{
			s.selection.clear();
			s.selected.clear();
			return;
		}
		if(sectionIndex(s.doc.doc(), widgetId) >= 0)
		{
			s.selection.assign(1, widgetId);	// single-select replaces the set
			s.selected = widgetId;
		}
	}
	//---------------------------------------------------------
	void uiEditSelectToggle(UiEditSession& s, String const& widgetId)
	{
		if(widgetId.empty() || sectionIndex(s.doc.doc(), widgetId) < 0) { return; }
		std::vector<std::string>::iterator it =
			std::find(s.selection.begin(), s.selection.end(), widgetId);
		if(it != s.selection.end())
		{
			s.selection.erase(it);	// shift-click an already-selected removes it
		}
		else
		{
			s.selection.push_back(widgetId);	// appended: never the key unless first
		}
		syncKey(s);
	}
	//---------------------------------------------------------
	void uiEditSetSelection(UiEditSession& s, std::vector<std::string> const& ids)
	{
		s.selection.clear();
		for(std::string const& id : ids)
		{
			if(sectionIndex(s.doc.doc(), id) >= 0 &&
				std::find(s.selection.begin(), s.selection.end(), id) ==
					s.selection.end())
			{
				s.selection.push_back(id);
			}
		}
		syncKey(s);
	}
	//---------------------------------------------------------
	void uiEditMarqueeSelect(UiEditSession& s, GamePreviewStage& stage,
		float x0, float y0, float x1, float y1)
	{
		uiEditSetSelection(s, widgetsInMarquee(resolvedRects(s, stage),
			x0, y0, x1, y1));
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
	namespace
	{
		//! move every selected widget by the same surface-pixel delta within one
		//! open gesture (the caller brackets begin/commit). A pure translation, so
		//! it reads the same in every geometry mode / parent space.
		void moveSelection(UiEditSession& s, float dxPx, float dyPx, float scale)
		{
			for(std::string const& id : s.selection)
			{
				const int idx = sectionIndex(s.doc.doc(), id);
				if(idx < 0) { continue; }
				applyMove(s.doc.doc().sections[static_cast<size_t>(idx)],
					dxPx, dyPx, scale, 0.0f);
			}
		}
	}
	//---------------------------------------------------------
	void uiEditNudgeKey(UiEditSession& s, float dxDesign, float dyDesign)
	{
		if(s.selection.empty()) { return; }
		const float scale = nudgeScale(s);
		// a design-unit step is a scale*step surface delta; the whole burst on
		// this exact selection folds into ONE undo step (the drag contract).
		std::string key = "nudge";
		for(std::string const& id : s.selection) { key += ":" + id; }
		s.doc.beginCoalesced(key);
		moveSelection(s, dxDesign * scale, dyDesign * scale, scale);
		s.doc.commitEdit();
		s.needsReload = true;
	}
	//---------------------------------------------------------
	namespace
	{
		//! the resolved rects of the current selection, key (front) first - the
		//! order alignDeltas/distributeDeltas expect.
		std::vector<UiRect> selectionRects(UiEditSession const& s,
			GamePreviewStage& stage)
		{
			const std::vector<UiRect> all = resolvedRects(s, stage);
			std::vector<UiRect> out;
			for(std::string const& id : s.selection)
			{
				for(UiRect const& r : all)
				{
					if(r.id == id) { out.push_back(r); break; }
				}
			}
			return out;
		}
		//! replay per-rect surface-px deltas into each widget's own geometry as one
		//! undo step; deltas line up with the selection order.
		void applySelectionDeltas(UiEditSession& s, GamePreviewStage& stage,
			std::vector<Orkige::LayoutVec2> const& deltas)
		{
			float surfW = 1000.0f, surfH = 1000.0f;
			resolveSurface(s, stage, surfW, surfH);
			const float scale = uiEditLayoutScale(s, surfW, surfH);
			s.doc.beginEdit();
			for(size_t each = 0; each < s.selection.size() &&
				each < deltas.size(); ++each)
			{
				const int idx = sectionIndex(s.doc.doc(), s.selection[each]);
				if(idx < 0) { continue; }
				if(deltas[each].x == 0.0f && deltas[each].y == 0.0f) { continue; }
				applyMove(s.doc.doc().sections[static_cast<size_t>(idx)],
					deltas[each].x, deltas[each].y, scale, 0.0f);
			}
			s.doc.commitEdit();
			s.needsReload = true;
		}
	}
	//---------------------------------------------------------
	void uiEditAlign(UiEditSession& s, GamePreviewStage& stage, UiAlignOp op)
	{
		if(s.selection.size() < 2) { return; }
		applySelectionDeltas(s, stage, alignDeltas(selectionRects(s, stage), op));
	}
	//---------------------------------------------------------
	void uiEditDistribute(UiEditSession& s, GamePreviewStage& stage,
		UiDistributeOp op)
	{
		if(s.selection.size() < 3) { return; }
		applySelectionDeltas(s, stage,
			distributeDeltas(selectionRects(s, stage), op));
	}
	//---------------------------------------------------------
	void uiEditApplyAnchorPreset(UiEditSession& s, GamePreviewStage& stage,
		Orkige::LayoutAnchorPreset preset, AnchorPresetMods mods)
	{
		GuiLayoutSection* section = selectedSection(s);
		if(!section || geomMode(*section) != UiGeomMode::Layout) { return; }
		float scale = 1.0f;
		const Orkige::LayoutRect parent = parentRectOf(s, stage, s.selected, scale);
		s.doc.beginEdit();
		applyAnchorPresetToSection(*section, preset, mods, parent, scale);
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
		s.selection.assign(1, id);
		s.selected = id;
		s.needsReload = true;
		return id;
	}
	//---------------------------------------------------------
	namespace
	{
		//! drop any selection ids no longer present after a structural change
		void pruneSelection(UiEditSession& s)
		{
			std::vector<std::string> kept;
			for(std::string const& id : s.selection)
			{
				if(sectionIndex(s.doc.doc(), id) >= 0) { kept.push_back(id); }
			}
			s.selection.swap(kept);
			syncKey(s);
		}
	}
	//---------------------------------------------------------
	void uiEditDeleteSelected(UiEditSession& s)
	{
		if(s.selection.empty()) { return; }
		s.doc.beginEdit();
		for(std::string const& id : s.selection)	// whole selection, one step
		{
			removeWidgetSubtree(s.doc.doc(), id);
		}
		s.doc.commitEdit();
		s.selection.clear();
		s.selected.clear();
		s.needsReload = true;
	}
	//---------------------------------------------------------
	void uiEditUndo(UiEditSession& s)
	{
		s.doc.undo();
		pruneSelection(s);
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
		const ImU32 KEY_OUTLINE = IM_COL32(255, 190, 90, 255);	// the align key object
		const ImU32 ANCHOR_COL = IM_COL32(255, 210, 90, 230);
		const ImU32 PIVOT_COL = IM_COL32(90, 210, 255, 255);
		const ImU32 GUIDE_COL = IM_COL32(255, 90, 160, 220);
		const ImU32 MARQUEE_COL = IM_COL32(120, 180, 255, 90);

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
		//! map a surface-pixel point to the on-screen image point
		ImVec2 mapPoint(UiEditCanvas const& c, float sxSurface, float sySurface)
		{
			const float sx = c.drawW / (c.surfaceW > 0 ? c.surfaceW : 1.0f);
			const float sy = c.drawH / (c.surfaceH > 0 ? c.surfaceH : 1.0f);
			return ImVec2(c.imageX + sxSurface * sx, c.imageY + sySurface * sy);
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
		//! the four anchor-rect corners of @p sec in SURFACE px (min, max, and the
		//! two mixed corners), against @p parent. Index order matches DragKind.
		void anchorCornersSurface(GuiLayoutSection const& sec,
			Orkige::LayoutRect const& parent, float out[4][2])
		{
			const Orkige::LayoutNode n = sectionLayoutNode(sec);
			const float x0 = parent.x + n.anchorMin.x * parent.w;
			const float x1 = parent.x + n.anchorMax.x * parent.w;
			const float y0 = parent.y + n.anchorMin.y * parent.h;
			const float y1 = parent.y + n.anchorMax.y * parent.h;
			out[0][0] = x0; out[0][1] = y0;	// AnchorMin
			out[1][0] = x1; out[1][1] = y1;	// AnchorMax
			out[2][0] = x0; out[2][1] = y1;	// AnchorMinXMaxY
			out[3][0] = x1; out[3][1] = y0;	// AnchorMaxXMinY
		}
		//! a small filled diamond marker (an anchor grip) at a screen point
		void drawDiamond(ImDrawList* draw, ImVec2 const& p, float r, ImU32 col)
		{
			draw->AddQuadFilled(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y),
				ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), col);
		}
	}
	//---------------------------------------------------------
	namespace
	{
		//! surface->screen delta helpers for the canvas
		float surfDeltaX(UiEditCanvas const& c, float screenDx)
		{
			return screenDx * (c.surfaceW / (c.drawW > 0 ? c.drawW : 1.0f));
		}
		float surfDeltaY(UiEditCanvas const& c, float screenDy)
		{
			return screenDy * (c.surfaceH / (c.drawH > 0 ? c.drawH : 1.0f));
		}
		//! is the mouse within @p rad screen px of a mapped surface point?
		bool nearPoint(UiEditCanvas const& c, float surfX, float surfY,
			ImVec2 const& mouse, float rad)
		{
			const ImVec2 p = mapPoint(c, surfX, surfY);
			return std::fabs(p.x - mouse.x) <= rad && std::fabs(p.y - mouse.y) <= rad;
		}
	}
	//---------------------------------------------------------
	void uiEditDrawCanvas(UiEditSession& s, GamePreviewStage& stage,
		UiEditCanvas const& canvas, ImDrawList* draw, float snapDesign)
	{
		if(!s.loaded) { return; }
		const std::vector<UiRect> rects = rectsFor(stage);
		auto selected = [&](String const& id)
		{
			return std::find(s.selection.begin(), s.selection.end(), id) !=
				s.selection.end();
		};

		// an invisible button over the image captures clicks/drags on the canvas
		ImGui::SetCursorScreenPos(ImVec2(canvas.imageX, canvas.imageY));
		ImGui::InvisibleButton("##ui_edit_canvas",
			ImVec2(canvas.drawW, canvas.drawH),
			ImGuiButtonFlags_MouseButtonLeft);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const bool shift = ImGui::GetIO().KeyShift;

		// the key widget's live rect (for handles / anchors), if any
		UiRect keyRect;
		const bool haveKey = !s.selected.empty() && rectOf(stage, s.selected, keyRect);

		// hover highlight (not while dragging)
		if(hovered && !s.dragging)
		{
			float mx = 0, my = 0;
			toSurface(canvas, mouse.x, mouse.y, mx, my);
			const String hoverId = hitTestWidget(rects, mx, my);
			for(UiRect const& r : rects)
			{
				if(r.id == hoverId && !selected(r.id))
				{
					ImVec2 a, b;
					mapRect(canvas, r, a, b);
					draw->AddRect(a, b, HOVER_OUTLINE);
					break;
				}
			}
		}

		// begin a drag: anchor/pivot grip, else a widget handle, else marquee/select
		if(ImGui::IsItemActivated())
		{
			UiEditSession::DragKind kind = UiEditSession::DragKind::None;
			UiHandle handle = UiHandle::None;
			// 1) anchor triangles + pivot dot on the key (only in Layout mode)
			GuiLayoutSection* keySec = selectedSection(s);
			if(haveKey && keySec && geomMode(*keySec) == UiGeomMode::Layout)
			{
				float scale = 1.0f;
				const Orkige::LayoutRect parent =
					parentRectOf(s, stage, s.selected, scale);
				float corners[4][2];
				anchorCornersSurface(*keySec, parent, corners);
				const UiEditSession::DragKind cornerKind[4] = {
					UiEditSession::DragKind::AnchorMin,
					UiEditSession::DragKind::AnchorMax,
					UiEditSession::DragKind::AnchorMinXMaxY,
					UiEditSession::DragKind::AnchorMaxXMinY };
				for(int c = 0; c < 4; ++c)
				{
					if(nearPoint(canvas, corners[c][0], corners[c][1], mouse, 7.0f))
					{
						kind = cornerKind[c];
						break;
					}
				}
				if(kind == UiEditSession::DragKind::None)
				{
					const Orkige::LayoutNode n = sectionLayoutNode(*keySec);
					const float pvx = keyRect.left + n.pivot.x * keyRect.width;
					const float pvy = keyRect.top + n.pivot.y * keyRect.height;
					if(nearPoint(canvas, pvx, pvy, mouse, 7.0f))
					{
						kind = UiEditSession::DragKind::Pivot;
					}
				}
				if(kind != UiEditSession::DragKind::None)
				{
					s.dragParentX = parent.x; s.dragParentY = parent.y;
					s.dragParentW = parent.w; s.dragParentH = parent.h;
					s.dragScale = scale;
				}
			}
			// 2) a resize handle on the key
			if(kind == UiEditSession::DragKind::None && haveKey)
			{
				ImVec2 a, b;
				mapRect(canvas, keyRect, a, b);
				const UiRect screenRect{ s.selected, a.x, a.y, b.x - a.x, b.y - a.y };
				handle = handleAt(screenRect, mouse.x, mouse.y, 6.0f);
				// only the eight handles here; the interior Move is decided below
				if(handle == UiHandle::Move) { handle = UiHandle::None; }
				if(handle != UiHandle::None) { kind = UiEditSession::DragKind::Widget; }
			}
			// 3) hit a widget: select/toggle, then a body move; else a marquee
			if(kind == UiEditSession::DragKind::None)
			{
				float mx = 0, my = 0;
				toSurface(canvas, mouse.x, mouse.y, mx, my);
				const String hit = hitTestWidget(rects, mx, my);
				if(hit.empty())
				{
					if(!shift) { uiEditSelect(s, String()); }
					kind = UiEditSession::DragKind::Marquee;
					s.marqueeX0 = mx; s.marqueeY0 = my;
					s.marqueeX1 = mx; s.marqueeY1 = my;
				}
				else if(shift)
				{
					uiEditSelectToggle(s, hit);	// extend, no drag
				}
				else
				{
					if(!selected(hit)) { uiEditSelect(s, hit); }
					kind = UiEditSession::DragKind::Widget;
					handle = UiHandle::Move;
					rectOf(stage, s.selected, keyRect);
				}
			}
			if(kind != UiEditSession::DragKind::None &&
				kind != UiEditSession::DragKind::Marquee)
			{
				s.dragging = true;
			}
			else if(kind == UiEditSession::DragKind::Marquee)
			{
				s.dragging = true;
				s.marquee = true;
			}
			s.dragKind = kind;
			s.dragHandle = handle;
			s.dragStartX = mouse.x;
			s.dragStartY = mouse.y;
			s.dragRect = keyRect;
		}

		// live feedback while dragging
		std::vector<UiGuide> activeGuides;
		if(s.dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			const float dSx = surfDeltaX(canvas, mouse.x - s.dragStartX);
			const float dSy = surfDeltaY(canvas, mouse.y - s.dragStartY);
			if(s.dragKind == UiEditSession::DragKind::Marquee)
			{
				float mx = 0, my = 0;
				toSurface(canvas, mouse.x, mouse.y, mx, my);
				s.marqueeX1 = mx; s.marqueeY1 = my;
				ImVec2 a = mapPoint(canvas, s.marqueeX0, s.marqueeY0);
				ImVec2 b = mapPoint(canvas, s.marqueeX1, s.marqueeY1);
				draw->AddRectFilled(a, b, MARQUEE_COL);
				draw->AddRect(a, b, SEL_OUTLINE);
			}
			else if(s.dragKind == UiEditSession::DragKind::Widget &&
				s.dragHandle == UiHandle::Move)
			{
				// smart guides: snap the key ghost to sibling/parent/centre lines
				// (grid snap held with Shift takes over, guides off then)
				UiRect ghost = s.dragRect;
				ghost.left += dSx; ghost.top += dSy;
				if(snapDesign <= 0.0f)
				{
					std::vector<UiRect> others;
					for(UiRect const& r : rects)
					{
						if(!selected(r.id)) { others.push_back(r); }
					}
					const UiRect parent{ "", 0, 0, canvas.surfaceW, canvas.surfaceH };
					const std::vector<UiGuide> cands = guideCandidates(others, parent,
						true, canvas.surfaceW * 0.5f, canvas.surfaceH * 0.5f);
					const UiSnap snap = snapToGuides(ghost, cands,
						surfDeltaX(canvas, 6.0f));
					if(snap.snappedX) { ghost.left += snap.dx; activeGuides.push_back({ true, snap.guideX }); }
					if(snap.snappedY) { ghost.top += snap.dy; activeGuides.push_back({ false, snap.guideY }); }
				}
				// draw every selected widget's ghost translated by the same delta
				const float gdx = ghost.left - s.dragRect.left;
				const float gdy = ghost.top - s.dragRect.top;
				for(UiRect const& r : rects)
				{
					if(!selected(r.id)) { continue; }
					UiRect g = r; g.left += gdx; g.top += gdy;
					ImVec2 a, b; mapRect(canvas, g, a, b);
					draw->AddRect(a, b, SEL_OUTLINE, 0.0f, 0, 2.0f);
				}
			}
			else if(s.dragKind == UiEditSession::DragKind::Widget)
			{
				UiRect ghost = s.dragRect;
				if(s.dragHandle == UiHandle::Left || s.dragHandle == UiHandle::TopLeft ||
					s.dragHandle == UiHandle::BottomLeft) { ghost.left += dSx; ghost.width -= dSx; }
				if(s.dragHandle == UiHandle::Right || s.dragHandle == UiHandle::TopRight ||
					s.dragHandle == UiHandle::BottomRight) { ghost.width += dSx; }
				if(s.dragHandle == UiHandle::Top || s.dragHandle == UiHandle::TopLeft ||
					s.dragHandle == UiHandle::TopRight) { ghost.top += dSy; ghost.height -= dSy; }
				if(s.dragHandle == UiHandle::Bottom || s.dragHandle == UiHandle::BottomLeft ||
					s.dragHandle == UiHandle::BottomRight) { ghost.height += dSy; }
				ImVec2 a, b; mapRect(canvas, ghost, a, b);
				draw->AddRect(a, b, SEL_OUTLINE, 0.0f, 0, 2.0f);
			}
			// anchor/pivot drags redraw their grip live below (position from mouse)
		}
		// draw the snapped guide lines full-canvas
		for(UiGuide const& g : activeGuides)
		{
			if(g.vertical)
			{
				const ImVec2 p = mapPoint(canvas, g.pos, 0.0f);
				draw->AddLine(ImVec2(p.x, canvas.imageY),
					ImVec2(p.x, canvas.imageY + canvas.drawH), GUIDE_COL);
			}
			else
			{
				const ImVec2 p = mapPoint(canvas, 0.0f, g.pos);
				draw->AddLine(ImVec2(canvas.imageX, p.y),
					ImVec2(canvas.imageX + canvas.drawW, p.y), GUIDE_COL);
			}
		}

		// end the drag: apply once (one undo step), persist+reload
		if(s.dragging && ImGui::IsItemDeactivated())
		{
			const float dScreenX = mouse.x - s.dragStartX;
			const float dScreenY = mouse.y - s.dragStartY;
			const float dSx = surfDeltaX(canvas, dScreenX);
			const float dSy = surfDeltaY(canvas, dScreenY);
			const float scale = uiEditLayoutScale(s, canvas.surfaceW, canvas.surfaceH);
			bool changed = false;
			if(s.dragKind == UiEditSession::DragKind::Marquee)
			{
				uiEditMarqueeSelect(s, stage, s.marqueeX0, s.marqueeY0,
					s.marqueeX1, s.marqueeY1);
			}
			else if(s.dragKind == UiEditSession::DragKind::Widget &&
				(dScreenX != 0.0f || dScreenY != 0.0f))
			{
				if(s.dragHandle == UiHandle::Move)
				{
					float useDx = dSx, useDy = dSy;
					// guide snap (off while a grid snap is held) - fold into the delta
					if(snapDesign <= 0.0f)
					{
						UiRect ghost = s.dragRect; ghost.left += dSx; ghost.top += dSy;
						std::vector<UiRect> others;
						for(UiRect const& r : rects)
						{
							if(!selected(r.id)) { others.push_back(r); }
						}
						const UiRect parent{ "", 0, 0, canvas.surfaceW, canvas.surfaceH };
						const UiSnap snap = snapToGuides(ghost,
							guideCandidates(others, parent, true,
								canvas.surfaceW * 0.5f, canvas.surfaceH * 0.5f),
							surfDeltaX(canvas, 6.0f));
						if(snap.snappedX) { useDx += snap.dx; }
						if(snap.snappedY) { useDy += snap.dy; }
					}
					s.doc.beginEdit();
					for(std::string const& id : s.selection)	// one undo step
					{
						const int idx = sectionIndex(s.doc.doc(), id);
						if(idx < 0) { continue; }
						applyMove(s.doc.doc().sections[static_cast<size_t>(idx)],
							useDx, useDy, scale, snapDesign);
					}
					s.doc.commitEdit();
					changed = true;
				}
				else if(GuiLayoutSection* section = selectedSection(s))
				{
					s.doc.beginEdit();
					applyResize(*section, s.dragHandle, dSx, dSy, scale, snapDesign);
					s.doc.commitEdit();
					changed = true;
				}
			}
			else if(GuiLayoutSection* section = selectedSection(s))
			{
				const Orkige::LayoutRect parent{ s.dragParentX, s.dragParentY,
					s.dragParentW, s.dragParentH };
				if(s.dragKind == UiEditSession::DragKind::Pivot)
				{
					float mxS = 0, myS = 0;
					toSurface(canvas, mouse.x, mouse.y, mxS, myS);
					const float px = s.dragRect.width > 0 ?
						(mxS - s.dragRect.left) / s.dragRect.width : 0.0f;
					const float py = s.dragRect.height > 0 ?
						(myS - s.dragRect.top) / s.dragRect.height : 0.0f;
					s.doc.beginEdit();
					applyPivotDrag(*section, px, py);
					s.doc.commitEdit();
					changed = true;
				}
				else if(s.dragKind == UiEditSession::DragKind::AnchorMin ||
					s.dragKind == UiEditSession::DragKind::AnchorMax ||
					s.dragKind == UiEditSession::DragKind::AnchorMinXMaxY ||
					s.dragKind == UiEditSession::DragKind::AnchorMaxXMinY)
				{
					float mxS = 0, myS = 0;
					toSurface(canvas, mouse.x, mouse.y, mxS, myS);
					const float fx = parent.w > 0 ? (mxS - parent.x) / parent.w : 0.0f;
					const float fy = parent.h > 0 ? (myS - parent.y) / parent.h : 0.0f;
					UiAnchorCorner corner = UiAnchorCorner::Min;
					if(s.dragKind == UiEditSession::DragKind::AnchorMax)
						corner = UiAnchorCorner::Max;
					else if(s.dragKind == UiEditSession::DragKind::AnchorMinXMaxY)
						corner = UiAnchorCorner::MinXMaxY;
					else if(s.dragKind == UiEditSession::DragKind::AnchorMaxXMinY)
						corner = UiAnchorCorner::MaxXMinY;
					s.doc.beginEdit();
					applyAnchorDrag(*section, corner, fx, fy, parent, s.dragScale);
					s.doc.commitEdit();
					changed = true;
				}
			}
			if(changed)
			{
				String err;
				persist(s, stage, err);
			}
			s.dragging = false;
			s.marquee = false;
			s.dragKind = UiEditSession::DragKind::None;
			s.dragHandle = UiHandle::None;
		}

		// arrow-key nudge: 1 design unit (10 with Shift), one undo step per burst
		if((hovered || ImGui::IsItemFocused()) && !s.dragging && !s.selection.empty())
		{
			const float step = shift ? 10.0f : 1.0f;
			float nx = 0.0f, ny = 0.0f;
			if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  { nx -= step; }
			if(ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) { nx += step; }
			if(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))    { ny -= step; }
			if(ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))  { ny += step; }
			if(nx != 0.0f || ny != 0.0f)
			{
				uiEditNudgeKey(s, nx, ny);
				String err; persist(s, stage, err);
			}
		}

		// adornments: outline every selected widget; key handles + anchor grips
		for(UiRect const& r : rects)
		{
			if(!selected(r.id)) { continue; }
			ImVec2 a, b; mapRect(canvas, r, a, b);
			const bool isKey = r.id == s.selected;
			draw->AddRect(a, b, isKey ? KEY_OUTLINE : SEL_OUTLINE, 0.0f, 0, 2.0f);
		}
		if(haveKey && !s.dragging)
		{
			ImVec2 a, b; mapRect(canvas, keyRect, a, b);
			drawHandles(draw, a, b);
			GuiLayoutSection* keySec = selectedSection(s);
			if(keySec && geomMode(*keySec) == UiGeomMode::Layout)
			{
				float scale = 1.0f;
				const Orkige::LayoutRect parent =
					parentRectOf(s, stage, s.selected, scale);
				float corners[4][2];
				anchorCornersSurface(*keySec, parent, corners);
				for(int c = 0; c < 4; ++c)
				{
					drawDiamond(draw,
						mapPoint(canvas, corners[c][0], corners[c][1]), 5.0f, ANCHOR_COL);
				}
				const Orkige::LayoutNode n = sectionLayoutNode(*keySec);
				const ImVec2 pv = mapPoint(canvas,
					keyRect.left + n.pivot.x * keyRect.width,
					keyRect.top + n.pivot.y * keyRect.height);
				draw->AddCircleFilled(pv, 4.0f, PIVOT_COL);
				draw->AddCircle(pv, 4.0f, IM_COL32(0, 0, 0, 200));
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
		//! the 4x4 anchor-preset gizmo: a compact draw-list grid mapping each
		//! cell to a LayoutAnchorPreset. Click applies to the key widget; Alt
		//! ALSO moves the pivot to the preset point, Shift ALSO keeps the
		//! on-screen rect (recomputes offsets). Returns true when it applied.
		bool anchorPresetGizmo(UiEditSession& s, GamePreviewStage& stage)
		{
			// the visual arrangement (row-major): the 3x3 point block + a stretch
			// column, then a stretch row - the whole 16-preset vocabulary.
			static const Orkige::LayoutAnchorPreset kGrid[16] = {
				Orkige::LAP_TOPLEFT, Orkige::LAP_TOP, Orkige::LAP_TOPRIGHT, Orkige::LAP_STRETCH_TOP,
				Orkige::LAP_LEFT, Orkige::LAP_CENTER, Orkige::LAP_RIGHT, Orkige::LAP_STRETCH_MIDDLE,
				Orkige::LAP_BOTTOMLEFT, Orkige::LAP_BOTTOM, Orkige::LAP_BOTTOMRIGHT, Orkige::LAP_STRETCH_BOTTOM,
				Orkige::LAP_STRETCH_LEFT, Orkige::LAP_STRETCH_CENTER, Orkige::LAP_STRETCH_RIGHT, Orkige::LAP_STRETCH_ALL
			};
			const int idx = sectionIndex(s.doc.doc(), s.selected);
			Orkige::LayoutAnchorPreset current = Orkige::LAP_TOPLEFT;
			if(idx >= 0)
			{
				if(String const* a = s.doc.doc().sections[static_cast<size_t>(idx)]
					.find("anchor"))
				{
					parseAnchorPreset(*a, current);
				}
			}
			const float cell = 22.0f;
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			bool applied = false;
			for(int i = 0; i < 16; ++i)
			{
				const int col = i % 4;
				const int row = i / 4;
				const ImVec2 p(origin.x + col * cell, origin.y + row * cell);
				ImGui::SetCursorScreenPos(p);
				ImGui::PushID(i);
				ImGui::InvisibleButton("##ap", ImVec2(cell - 2.0f, cell - 2.0f));
				const bool hov = ImGui::IsItemHovered();
				const bool cur = kGrid[i] == current;
				const ImU32 border = cur ? IM_COL32(255, 190, 90, 255)
					: (hov ? IM_COL32(200, 200, 200, 255) : IM_COL32(110, 110, 110, 255));
				dl->AddRect(p, ImVec2(p.x + cell - 2.0f, p.y + cell - 2.0f), border);
				// a dot at the preset's anchor point + a bar for the stretch axes
				const Orkige::LayoutVec2 pt = anchorPresetPoint(kGrid[i]);
				Orkige::LayoutNode n; Orkige::applyAnchorPreset(n, kGrid[i]);
				const float ix = p.x + 3.0f, iy = p.y + 3.0f, ie = cell - 8.0f;
				const ImU32 mark = cur ? IM_COL32(255, 210, 120, 255)
					: IM_COL32(180, 180, 180, 255);
				if(n.anchorMin.x != n.anchorMax.x)
				{
					dl->AddLine(ImVec2(ix, iy + pt.y * ie),
						ImVec2(ix + ie, iy + pt.y * ie), mark, 2.0f);
				}
				if(n.anchorMin.y != n.anchorMax.y)
				{
					dl->AddLine(ImVec2(ix + pt.x * ie, iy),
						ImVec2(ix + pt.x * ie, iy + ie), mark, 2.0f);
				}
				if(n.anchorMin.x == n.anchorMax.x && n.anchorMin.y == n.anchorMax.y)
				{
					dl->AddCircleFilled(
						ImVec2(ix + pt.x * ie, iy + pt.y * ie), 2.5f, mark);
				}
				if(ImGui::IsItemClicked())
				{
					AnchorPresetMods mods;
					mods.alsoPivot = ImGui::GetIO().KeyAlt;
					mods.alsoKeepRect = ImGui::GetIO().KeyShift;
					uiEditApplyAnchorPreset(s, stage, kGrid[i], mods);
					applied = true;
				}
				ImGui::PopID();
			}
			// reserve the grid's footprint so following widgets stack below it
			ImGui::SetCursorScreenPos(origin);
			ImGui::Dummy(ImVec2(cell * 4.0f, cell * 4.0f));
			ImGui::TextDisabled("Anchors (Alt: +pivot, Shift: keep rect)");
			return applied;
		}
		//! the align/distribute toolbar row (needs a multi-selection). Returns
		//! true when a command ran (the caller persists).
		bool alignRow(UiEditSession& s, GamePreviewStage& stage)
		{
			bool ran = false;
			ImGui::TextDisabled("Align (key: %s)", s.selected.c_str());
			struct Btn { char const* label; UiAlignOp op; };
			static const Btn kBtns[6] = {
				{ "L", UiAlignOp::Left }, { "C", UiAlignOp::HCenter },
				{ "R", UiAlignOp::Right }, { "T", UiAlignOp::Top },
				{ "M", UiAlignOp::VCenter }, { "B", UiAlignOp::Bottom } };
			for(int i = 0; i < 6; ++i)
			{
				if(i != 0) { ImGui::SameLine(); }
				ImGui::PushID(i);
				if(ImGui::SmallButton(kBtns[i].label))
				{
					uiEditAlign(s, stage, kBtns[i].op);
					ran = true;
				}
				ImGui::PopID();
			}
			ImGui::SameLine();
			if(ImGui::SmallButton("Dist H"))
			{
				uiEditDistribute(s, stage, UiDistributeOp::Horizontal); ran = true;
			}
			ImGui::SameLine();
			if(ImGui::SmallButton("Dist V"))
			{
				uiEditDistribute(s, stage, UiDistributeOp::Vertical); ran = true;
			}
			return ran;
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
			pruneSelection(s);
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

		// align/distribute over a multi-selection (the key holds; others move)
		if(s.selection.size() >= 2)
		{
			if(alignRow(s, stage)) { String err; persist(s, stage, err); }
			ImGui::Separator();
		}

		// the widget tree (the .oui section order; parenting shown by indent).
		// Shift/Ctrl(Cmd)-click extends the ordered multi-selection.
		ImGui::TextDisabled("Widgets");
		for(GuiLayoutSection const& sec : s.doc.doc().sections)
		{
			if(sec.id.empty()) { continue; }	// [Layout]/[Modal] etc.
			const bool child = sec.find("parent") != NULL;
			ImGui::PushID(sec.id.c_str());
			if(child) { ImGui::Indent(14.0f); }
			const String label = sec.id + "  (" + sec.type + ")";
			const bool isSel = std::find(s.selection.begin(), s.selection.end(),
				sec.id) != s.selection.end();
			if(ImGui::Selectable(label.c_str(), isSel))
			{
				const ImGuiIO& io = ImGui::GetIO();
				if(io.KeyShift || io.KeyCtrl || io.KeySuper)
				{
					uiEditSelectToggle(s, sec.id);
				}
				else
				{
					uiEditSelect(s, sec.id);
				}
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
				// the visual anchor-preset gizmo (its click applies + persists)
				if(anchorPresetGizmo(s, stage)) { String err; persist(s, stage, err); }
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
