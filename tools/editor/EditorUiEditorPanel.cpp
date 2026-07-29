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
#include "EditorApp.h"
#include "EditorTreeDnd.h"
#include "EditorTreeDndDraw.h"
#include "EditorTabMenu.h"
#include "EditorPropertyWidgets.h"
#include "GamePreviewStage.h"
#include "GuiPreviewStage.h"
#include "EditorTheme.h"
#include "ImGuiFacadeRenderer.h"
#include "IconsFontAwesome6.h"

#include <core_util/UiLayout.h>
#include <core_util/StringUtil.h>
#include <core_util/StringTable.h>
#include <engine_render/RenderTexture.h>
#include <engine_gui/GuiManager.h>
#include <engine_gui/UiAtlas.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace OrkigeEditor
{
	using Orkige::String;
	using Orkige::GuiLayoutDoc;
	using Orkige::GuiLayoutSection;

	namespace
	{
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
		//! a section's render layer (its `z` key, default 0) - the canvas picking
		//! priority (higher draws on top)
		float sectionZ(GuiLayoutSection const& sec)
		{
			if(String const* v = sec.find("z"))
			{
				std::istringstream in(*v);
				float z = 0.0f;
				if(in >> z) { return z; }
			}
			return 0.0f;
		}
		//! a section's parent-chain nesting depth (a root is 0, its child 1, ...);
		//! the canvas picks the DEEPER widget at equal z so a child inside a parent
		//! is selectable
		int sectionDepth(GuiLayoutDoc const& doc, GuiLayoutSection const& sec)
		{
			int depth = 0;
			GuiLayoutSection const* cur = &sec;
			for(int guard = 0; guard < 64; ++guard)
			{
				String const* p = cur->find("parent");
				if(!p) { break; }
				const int idx = sectionIndex(doc, *p);
				if(idx < 0) { break; }
				++depth;
				cur = &doc.sections[static_cast<size_t>(idx)];
			}
			return depth;
		}
		//! @brief the CANVAS adornment/hit/resize rects: EVERY widget's resolved
		//! LAYOUT BOX from the document (never the runtime's reported draw-size).
		//! A Label reports its TEXT extent (GuiLabel::getSize), not its box - so the
		//! live overlay rect would put the resize grips on the text, and a grip drag
		//! grows sizeDelta but the reported rect never moves (the resize-does-nothing
		//! bug). The layout box IS what the resize/anchor math edits, so the canvas
		//! adorns and hits THAT, and each rect carries its z + nesting depth for the
		//! child-over-parent picking. Same surface-pixel space as the live rects, so
		//! the surface->screen map is unchanged. Both flavors (pure resolve).
		std::vector<UiRect> canvasBoxRects(UiEditSession const& s,
			GamePreviewStage& stage)
		{
			float surfW = 1000.0f, surfH = 1000.0f;
			resolveSurface(s, stage, surfW, surfH);
			const float scale = uiEditLayoutScale(s, surfW, surfH);
			std::vector<UiRect> out;
			for(GuiLayoutSection const& sec : s.doc.doc().sections)
			{
				if(sec.id.empty()) { continue; }
				UiRect r = docRectOf(s.doc.doc(), sec, surfW, surfH, scale);
				r.z = sectionZ(sec);
				r.depth = sectionDepth(s.doc.doc(), sec);
				out.push_back(r);
			}
			return out;
		}
		//! the selected widget's canvas layout box (surface px); false when absent
		bool canvasBoxRectOf(UiEditSession const& s, GamePreviewStage& stage,
			String const& id, UiRect& out)
		{
			for(UiRect const& r : canvasBoxRects(s, stage))
			{
				if(r.id == id) { out = r; return true; }
			}
			return false;
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
	void uiEditTreeSelect(UiEditSession& s, String const& widgetId, bool additive)
	{
		const bool already = std::find(s.selection.begin(), s.selection.end(),
			widgetId) != s.selection.end();
		switch(uiTreeClickAction(already, additive))
		{
		case UiTreeClickAction::Toggle:  uiEditSelectToggle(s, widgetId); break;
		case UiTreeClickAction::Replace: uiEditSelect(s, widgetId); break;
		}
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
	namespace
	{
		//! the lower-case `.oui` [Type] token of a section (GuiFactory lower-cases
		//! before dispatch, so the editor matches case-insensitively)
		std::string kindToken(GuiLayoutSection const& sec)
		{
			return Orkige::StringUtil::to_lower_copy(sec.type);
		}
		//! does this widget KIND render a text/caption? (GuiFactory passes `text`
		//! to these) - a decor panel / progress bar / scroll view does NOT, so the
		//! property surface hides the Text row for them.
		bool kindHasText(std::string const& type)
		{
			return type == "label" || type == "textbox" || type == "button" ||
				type == "checkbox" || type == "selectmenu" || type == "slider" ||
				type == "textentry" || type == "dropdown";
		}
		//! does this widget KIND render an atlas sprite face? (GuiFactory passes
		//! `sprite` to these) - a label / textbox / scroll view / list view does
		//! not, so the Sprite row is hidden for them.
		bool kindHasSprite(std::string const& type)
		{
			return type == "button" || type == "checkbox" || type == "selectmenu" ||
				type == "slider" || type == "progressbar" || type == "textentry" ||
				type == "decorwidget" || type == "panel" || type == "dropdown";
		}
		//! does this widget KIND wrap its text to the width the layout gives it?
		//! (GuiFactory applies `wrap` to these two) - everything else keeps a
		//! single clipped line, so the Wrap row is hidden for them.
		bool kindHasWrap(std::string const& type)
		{
			return type == "label" || type == "textbox";
		}
		//! a unique default id for @p type in @p s's doc (the palette's own scheme),
		//! WITHOUT adding a section - the prefill for the name popup
		std::string defaultWidgetId(UiEditSession const& s, std::string const& type)
		{
			return paletteSection(s.doc.doc(), type, std::string()).id;
		}
		//! add a palette widget of @p type under an EXPLICIT @p parentId (captured
		//! once when the picker opened - "" lands it at the root) with a
		//! caller-validated unique @p id; selects it. Persist is the caller's. The
		//! explicit parent is what stops repeated adds building a chain (@see
		//! addDestinationParent); (@see uiEditAddWidget for the auto-id path.)
		std::string addWidgetWithId(UiEditSession& s, std::string const& type,
			std::string const& id, std::string const& parentId)
		{
			s.doc.beginEdit();
			GuiLayoutSection section = paletteSection(s.doc.doc(), type, parentId);
			section.id = id;	// the caller guarantees uniqueness
			s.doc.doc().sections.push_back(section);
			s.doc.commitEdit();
			s.selection.assign(1, id);
			s.selected = id;
			s.needsReload = true;
			return id;
		}
		//! the outcome of one name-entry popup frame
		enum class NameEntry { Open, Confirm, Cancel };
		//! @brief draw the shared name-entry body (Add + Rename use it): an
		//! auto-focused text field prefilled by the caller, a LIVE uniqueness error
		//! (the honest inline message - never a silent rename on collision) and
		//! OK/Cancel. Enter with a valid name confirms; the OK button is disabled
		//! while the name collides/blanks. @p allowSelf is the widget's own current
		//! id so a no-op rename is valid.
		NameEntry nameEntryBody(GuiLayoutDoc const& doc, std::string const& allowSelf,
			bool appearing, char* buf, size_t bufSize)
		{
			if(appearing) { ImGui::SetKeyboardFocusHere(); }
			ImGui::SetNextItemWidth(220.0f);
			const bool entered = ImGui::InputText("##uiname", buf, bufSize,
				ImGuiInputTextFlags_EnterReturnsTrue);
			String err;
			const bool valid = isValidWidgetName(doc, String(buf), allowSelf, err);
			if(!valid && buf[0] != '\0')
			{
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s",
					err.c_str());
			}
			else
			{
				ImGui::TextDisabled("a unique widget name");
			}
			NameEntry result = NameEntry::Open;
			ImGui::BeginDisabled(!valid);
			if(ImGui::Button("OK") || (entered && valid))
			{
				result = NameEntry::Confirm;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) { result = NameEntry::Cancel; }
			return result;
		}
		//! @brief run the ADD popups (kind picker -> name step). The CALLER opens
		//! the pick popup ("##addpick_<idScope>") from its own button; this draws
		//! both popups and returns true when a widget was added. Split out so a
		//! caller can lay its Add button out on a shared row (with rename/delete).
		//! @brief run the ADD popups (kind picker -> name step). The CALLER opens
		//! the pick popup ("##addpick_<idScope>") from its own Add button and passes
		//! @p pickAnchor (the button's bottom-left in screen px) so the picker opens
		//! DOCKED under the button, not floating at the last mouse position. Draws
		//! both popups and returns true when a widget was added.
		bool runAddWidgetPopups(UiEditSession& s, GamePreviewStage& stage,
			char const* idScope, ImVec2 pickAnchor)
		{
			const String pickId = String("##addpick_") + idScope;
			const String nameId = String("##addname_") + idScope;
			static std::string pendingType;
			static char nameBuf[64] = { 0 };
			// the destination choices captured ONCE when the picker opens (the
			// selection is stable there), carried through the name step so working the
			// popup never disturbs them; destChoiceIndex is the user's explicit pick.
			// The sibling default (three adds -> siblings) is only the DEFAULT choice;
			// every option stays selectable (@see addDestinationChoices) so a
			// deliberate child-of-the-newest add is one radio click away.
			static UiAddDestinationChoices destChoices;
			static int destChoiceIndex = 0;
			static std::string lastConfirmedParent;
			static std::string lastCreatedId;
			// step 1: pick a kind. Choosing one stashes the kind + a unique default
			// name and hands off to the name popup (MenuItem auto-closes this one).
			bool openName = false;
			ImGui::SetNextWindowPos(pickAnchor, ImGuiCond_Appearing);
			if(ImGui::BeginPopup(pickId.c_str()))
			{
				static char filter[64] = { 0 };
				static bool focusPending = false;
				if(ImGui::IsWindowAppearing())
				{
					filter[0] = '\0';
					focusPending = true;
					// capture the destination CHOICES now; the sibling rule picks the
					// default when the selection is the widget the last add created
					const bool selIsLastCreated = !lastCreatedId.empty() &&
						s.selected == lastCreatedId;
					destChoices = addDestinationChoices(s.selected, selIsLastCreated,
						lastConfirmedParent);
					// drop any option whose container was deleted since (root stays
					// valid), keeping the default pointing at a live option
					{
						const std::string defParent = destChoices.options.empty()
							? std::string()
							: destChoices.options[static_cast<size_t>(
								destChoices.defaultIndex)].parent;
						UiAddDestinationChoices valid;
						for(UiAddDestination const& opt : destChoices.options)
						{
							if(opt.parent.empty() ||
								sectionIndex(s.doc.doc(), opt.parent) >= 0)
							{
								valid.options.push_back(opt);
							}
						}
						if(valid.options.empty())
						{
							valid.options.push_back(
								{ UiAddDestination::Kind::Root, std::string() });
						}
						valid.defaultIndex = 0;
						for(size_t i = 0; i < valid.options.size(); ++i)
						{
							if(valid.options[i].parent == defParent)
							{
								valid.defaultIndex = static_cast<int>(i);
								break;
							}
						}
						destChoices = valid;
					}
					destChoiceIndex = destChoices.defaultIndex;
				}
				if(focusPending)
				{
					ImGui::SetKeyboardFocusHere();
					focusPending = false;
				}
				ImGui::SetNextItemWidth(220.0f);
				ImGui::InputTextWithHint("##widgetsearch", "search widgets...",
					filter, sizeof(filter));
				ImGui::Separator();
				const String needle =
					Orkige::StringUtil::to_lower_copy(String(filter));
				for(UiWidgetKind const& kind : uiWidgetKinds())
				{
					if(!needle.empty() && Orkige::StringUtil::to_lower_copy(
						String(kind.label)).find(needle) == String::npos)
					{
						continue;
					}
					// the kind's tree glyph in front of the label (the SAME icon the
					// widget tree draws per row)
					const String row = String(uiWidgetKindIcon(kind.type)) + "  " +
						kind.label;
					if(ImGui::MenuItem(row.c_str()))
					{
						pendingType = kind.type;
						std::snprintf(nameBuf, sizeof(nameBuf), "%s",
							defaultWidgetId(s, kind.type).c_str());
						openName = true;
					}
				}
				ImGui::EndPopup();
			}
			if(openName) { ImGui::OpenPopup(nameId.c_str()); }
			// step 2: name it + choose the destination. Confirm adds; Cancel/Esc aborts.
			bool added = false;
			ImGui::SetNextWindowPos(pickAnchor, ImGuiCond_Appearing);
			if(ImGui::BeginPopup(nameId.c_str()))
			{
				const bool appearing = ImGui::IsWindowAppearing();
				ImGui::TextDisabled("Name the %s", pendingType.c_str());
				// the EXPLICIT destination choice: radios over the captured options,
				// the sibling rule's pick pre-selected. A single option (nothing
				// selected -> root) draws no radios (the honest "Adds at root" note).
				if(destChoices.options.size() > 1)
				{
					ImGui::TextDisabled("Add destination");
					for(size_t i = 0; i < destChoices.options.size(); ++i)
					{
						UiAddDestination const& opt = destChoices.options[i];
						std::string label;
						switch(opt.kind)
						{
						case UiAddDestination::Kind::ChildOfSelected:
							label = "Child of '" + opt.parent + "'"; break;
						case UiAddDestination::Kind::LastDestination:
							label = "Sibling (in '" + opt.parent + "')"; break;
						case UiAddDestination::Kind::Root:
							label = "At root"; break;
						}
						ImGui::PushID(static_cast<int>(i));
						if(ImGui::RadioButton(label.c_str(),
							destChoiceIndex == static_cast<int>(i)))
						{
							destChoiceIndex = static_cast<int>(i);
						}
						ImGui::PopID();
					}
				}
				else
				{
					ImGui::TextDisabled("Adds at root");
				}
				const std::string effectiveParent =
					(destChoiceIndex >= 0 && destChoiceIndex <
						static_cast<int>(destChoices.options.size()))
					? destChoices.options[static_cast<size_t>(destChoiceIndex)].parent
					: std::string();
				const NameEntry r = nameEntryBody(s.doc.doc(), std::string(),
					appearing, nameBuf, sizeof(nameBuf));
				if(r == NameEntry::Confirm)
				{
					const std::string newId = addWidgetWithId(s, pendingType,
						std::string(nameBuf), effectiveParent);
					lastConfirmedParent = effectiveParent;	// sibling default source
					lastCreatedId = newId;
					String err; persist(s, stage, err);
					added = true;
					ImGui::CloseCurrentPopup();
				}
				else if(r == NameEntry::Cancel)
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			return added;
		}
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
	bool uiEditPickSprite(UiEditSession& s, GamePreviewStage& stage,
		String const& value, String& error)
	{
		GuiLayoutSection* section = selectedSection(s);
		if(!section)
		{
			error = "no widget selected";
			return false;
		}
		s.doc.beginEdit();
		section->set("sprite", value);
		s.doc.commitEdit();
		s.needsReload = true;
		return persist(s, stage, error);
	}
	//---------------------------------------------------------
	bool uiEditSetBool(UiEditSession& s, GamePreviewStage& stage,
		String const& key, bool value, String& error)
	{
		GuiLayoutSection* section = selectedSection(s);
		if(!section)
		{
			error = "no widget selected";
			return false;
		}
		s.doc.beginEdit();
		// both states are written explicitly, so the file always states the
		// property the screen shows (never an absent-means-false silence)
		section->set(key, value ? "true" : "false");
		s.doc.commitEdit();
		s.needsReload = true;
		return persist(s, stage, error);
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
	bool uiEditRenameSelected(UiEditSession& s, GamePreviewStage& stage,
		String const& newId, String& error)
	{
		if(s.selected.empty())
		{
			error = "no widget selected";
			return false;
		}
		const std::string old = s.selected;
		s.doc.beginEdit();
		if(!renameWidget(s.doc.doc(), old, newId, error))
		{
			s.doc.commitEdit();	// nothing changed -> no undo step
			return false;
		}
		s.doc.commitEdit();
		for(std::string& id : s.selection)
		{
			if(id == old) { id = newId; }
		}
		s.selected = newId;
		s.needsReload = true;
		String perr; persist(s, stage, perr);
		return true;
	}
	//---------------------------------------------------------
	bool uiEditReparent(UiEditSession& s, GamePreviewStage& stage,
		String const& childId, String const& newParentId, String& error,
		String const& reorderAnchorId, bool reorderAfter)
	{
		if(!s.loaded)
		{
			error = "no document loaded";
			return false;
		}
		// resolve the OLD parent rect (of the child) and the NEW parent rect (the
		// target container, or the full surface for a root drop) before touching the
		// doc, so the geometry-preserving reparent has both contexts
		float scale = 1.0f;
		const Orkige::LayoutRect oldParent = parentRectOf(s, stage, childId, scale);
		Orkige::LayoutRect newParent;
		if(newParentId.empty())
		{
			float surfW = 1000.0f, surfH = 1000.0f;
			resolveSurface(s, stage, surfW, surfH);
			newParent = { 0.0f, 0.0f, surfW, surfH };
		}
		else
		{
			UiRect box;
			if(!canvasBoxRectOf(s, stage, newParentId, box))
			{
				error = "the target parent has no resolved rect";
				return false;
			}
			newParent = { box.left, box.top, box.width, box.height };
		}
		s.doc.beginEdit();
		if(!reparentWidget(s.doc.doc(), childId, newParentId, oldParent, newParent,
			scale, error))
		{
			s.doc.commitEdit();	// nothing changed -> no undo step
			return false;
		}
		// a between-rows drop also sets the sibling ORDER: move the child adjacent to
		// the anchor sibling in serialize/paint order, folded into the SAME undo step
		if(!reorderAnchorId.empty())
		{
			reorderSectionAdjacent(s.doc.doc(), childId, reorderAnchorId, reorderAfter);
		}
		s.doc.commitEdit();
		s.needsReload = true;
		String perr; persist(s, stage, perr);
		return true;
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

		//! the pure canvas placement behind the ImGui-facing helpers (@see
		//! mapSurfaceRectToScreen - the ONE surface->screen transform)
		UiCanvasPlacement placementOf(UiEditCanvas const& c)
		{
			UiCanvasPlacement p;
			p.imageX = c.imageX; p.imageY = c.imageY;
			p.drawW = c.drawW; p.drawH = c.drawH;
			p.surfaceW = c.surfaceW; p.surfaceH = c.surfaceH;
			return p;
		}
		//! map a surface-pixel rect to the on-screen image rect
		void mapRect(UiEditCanvas const& c, UiRect const& r,
			ImVec2& a, ImVec2& b)
		{
			const UiRect m = mapSurfaceRectToScreen(placementOf(c), r);
			a = ImVec2(m.left, m.top);
			b = ImVec2(m.left + m.width, m.top + m.height);
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
		// adorn / hit / resize against the resolved LAYOUT BOX of every widget,
		// never the runtime's reported draw-size (a Label reports its TEXT extent,
		// which would sit the grips on the text and make resize appear to do
		// nothing - @see canvasBoxRects)
		const std::vector<UiRect> rects = canvasBoxRects(s, stage);
		auto selected = [&](String const& id)
		{
			return std::find(s.selection.begin(), s.selection.end(), id) !=
				s.selection.end();
		};

		// clip EVERY adornment (outline, handles, anchors, pivot, guides, marquee,
		// drag ghosts) to the canvas image rect: the selection outline and the
		// edge/anchor grips of a stretch widget sit ON the surface edges and would
		// otherwise bleed across the sidebar / past the device frame (the draw list
		// is the whole panel window's). intersect_with_current keeps the panel's
		// own clip too. Balanced by PopClipRect at the end.
		const ImVec2 clipMin(canvas.imageX, canvas.imageY);
		const ImVec2 clipMax(canvas.imageX + canvas.drawW,
			canvas.imageY + canvas.drawH);
		draw->PushClipRect(clipMin, clipMax, true);
		UiEditorDebug& dbg = uiEditorDebug();
		dbg.adornClipApplied = true;
		dbg.clipLeft = clipMin.x; dbg.clipTop = clipMin.y;
		dbg.clipRight = clipMax.x; dbg.clipBottom = clipMax.y;

		// an invisible button over the image captures clicks/drags on the canvas
		ImGui::SetCursorScreenPos(ImVec2(canvas.imageX, canvas.imageY));
		ImGui::InvisibleButton("##ui_edit_canvas",
			ImVec2(canvas.drawW, canvas.drawH),
			ImGuiButtonFlags_MouseButtonLeft);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const bool shift = ImGui::GetIO().KeyShift;
		const bool alt = ImGui::GetIO().KeyAlt;

		// the key widget's layout-box rect (for handles / anchors), if any
		UiRect keyRect;
		const bool haveKey = !s.selected.empty() &&
			canvasBoxRectOf(s, stage, s.selected, keyRect);
		if(haveKey)
		{
			// publish the key box in SCREEN px so a synthetic-input selfcheck can
			// aim an SDL drag at a real resize grip (the box corners/edges)
			ImVec2 ka, kb; mapRect(canvas, keyRect, ka, kb);
			dbg.hasSelScreen = true;
			dbg.selScreenLeft = ka.x; dbg.selScreenTop = ka.y;
			dbg.selScreenWidth = kb.x - ka.x; dbg.selScreenHeight = kb.y - ka.y;
		}

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
			s.hasPendingClick = false;	// only a body press inside the selection arms it
			// Alt+press is the EXPLICIT stack-cycling gesture: it must never be
			// consumed by the anchor/pivot grips or the resize handles (on a
			// small 1x canvas a tiny key widget's handle tolerance covers its
			// whole body, and the handle branch swallowed the cycle click)
			// 1) anchor triangles + pivot dot on the key (only in Layout mode)
			GuiLayoutSection* keySec = selectedSection(s);
			if(!alt && haveKey && keySec && geomMode(*keySec) == UiGeomMode::Layout)
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
			// 2) a resize handle on the key (never on an Alt press - @see above)
			if(!alt && kind == UiEditSession::DragKind::None && haveKey)
			{
				ImVec2 a, b;
				mapRect(canvas, keyRect, a, b);
				const UiRect screenRect{ s.selected, a.x, a.y, b.x - a.x, b.y - a.y };
				handle = handleAt(screenRect, mouse.x, mouse.y, 6.0f);
				// only the eight handles here; the interior Move is decided below
				if(handle == UiHandle::Move) { handle = UiHandle::None; }
				if(handle != UiHandle::None) { kind = UiEditSession::DragKind::Widget; }
			}
			// 3) hit a widget. Alt+click cycles the whole pick stack (a buried
			//    widget - a parent behind its children - is reachable); a plain press
			//    INSIDE the current selection prefers the selection for a body drag
			//    (a covering widget on top does not steal it) and defers the topmost
			//    re-select to a click-release; a press elsewhere selects the topmost;
			//    an empty press starts a marquee.
			if(kind == UiEditSession::DragKind::None)
			{
				float mx = 0, my = 0;
				toSurface(canvas, mouse.x, mouse.y, mx, my);
				const std::vector<std::string> stack = hitTestAllWidgets(rects, mx, my);
				const String topmost = stack.empty() ? String() : stack.front();
				if(stack.empty())
				{
					if(!shift) { uiEditSelect(s, String()); }
					kind = UiEditSession::DragKind::Marquee;
					s.marqueeX0 = mx; s.marqueeY0 = my;
					s.marqueeX1 = mx; s.marqueeY1 = my;
				}
				else if(alt)
				{
					// cycle one layer DOWN the stack (wrapping); commit now so a
					// following drag moves the newly reached widget
					uiEditSelect(s, cycleStackSelection(stack, s.selected));
					kind = UiEditSession::DragKind::Widget;
					handle = UiHandle::Move;
					canvasBoxRectOf(s, stage, s.selected, keyRect);
				}
				else if(shift)
				{
					uiEditSelectToggle(s, topmost);	// extend, no drag
				}
				else
				{
					// a press inside a currently-selected widget's rect keeps the
					// selection (DRAG RESPECTS SELECTION); a click that never drags
					// switches to the topmost under the cursor
					bool insideSelection = false;
					for(UiRect const& r : rects)
					{
						if(selected(r.id) && mx >= r.left && mx <= r.left + r.width &&
							my >= r.top && my <= r.top + r.height)
						{
							insideSelection = true;
							break;
						}
					}
					if(insideSelection)
					{
						s.hasPendingClick = true;
						s.pendingClickSelect = topmost;
					}
					else
					{
						uiEditSelect(s, topmost);	// outside the selection: topmost
					}
					kind = UiEditSession::DragKind::Widget;
					handle = UiHandle::Move;
					canvasBoxRectOf(s, stage, s.selected, keyRect);
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
			// a deferred body press that never crossed the drag threshold is a CLICK,
			// not a drag: switch to the topmost widget under the cursor and apply no
			// move (covering widgets stay one click away).
			const float dragDist =
				std::sqrt(dScreenX * dScreenX + dScreenY * dScreenY);
			const bool wasClick = s.hasPendingClick &&
				dragDist < ImGui::GetIO().MouseDragThreshold;
			if(wasClick)
			{
				uiEditSelect(s, s.pendingClickSelect);
			}
			bool changed = false;
			if(wasClick)
			{
				// only the selection switched; no geometry edit
			}
			else if(s.dragKind == UiEditSession::DragKind::Marquee)
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
			s.hasPendingClick = false;
			s.pendingClickSelect.clear();
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
			// Delete/Backspace deletes the selection (the whole set, ONE undo
			// step) - the keyboard sibling of the UI Editor panel's trash button,
			// so the canvas needs no toolbar delete of its own
			if(ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
				ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
			{
				uiEditDeleteSelected(s);
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

		// record the pre-clip adornment bounds (the selection outlines grown by the
		// grip radius) so the selfcheck can prove they are what the clip bounds
		std::vector<UiRect> selRects;
		for(UiRect const& r : rects)
		{
			if(selected(r.id)) { selRects.push_back(r); }
		}
		const UiRect adorn = adornmentBoundsScreen(placementOf(canvas), selRects, 7.0f);
		dbg.adornLeft = adorn.left; dbg.adornTop = adorn.top;
		dbg.adornRight = adorn.left + adorn.width;
		dbg.adornBottom = adorn.top + adorn.height;

		draw->PopClipRect();
	}
	//=========================================================
	//=== the sidebar (tree / properties / palette / save) ====
	//=========================================================
	namespace
	{
		//! push the shared small value font (the baked value-column face the
		//! Inspector uses); pair with popValueFont(). Falls back to the base font.
		void pushValueFont()
		{
			ImFont* const valueFont = Orkige::editorSmallFont();
			ImGui::PushFont(valueFont != nullptr ? valueFont : ImGui::GetFont(),
				valueFont != nullptr ? Orkige::editorSmallFontSize() : 0.0f);
		}
		void popValueFont() { ImGui::PopFont(); }
		//! open one property row in the current 30/70 table: the label in the left
		//! column (baseline-nudged, with an optional OS-mannered tooltip), then the
		//! value column primed for a full-width value widget under the small font.
		//! The caller draws the value widget, then calls endPropertyRow().
		void beginPropertyRow(char const* label, char const* tooltip)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			if(tooltip != NULL && tooltip[0] != '\0')
			{
				ImGui::SetItemTooltip("%s", tooltip);
			}
			ImGui::TableSetColumnIndex(1);
			pushValueFont();
			ImGui::SetNextItemWidth(-FLT_MIN);	// the value widget fills the column
		}
		void endPropertyRow() { popValueFont(); }
		//! @brief a `@key` localisation completion for a text field: while the field
		//! is active and its trailing token starts with `@`, a small overlay lists
		//! the project's localisation keys (StringTable::listKeys, loaded by the
		//! preview stage) filtered by what follows the `@`. Clicking one rewrites
		//! @p buffer's trailing `@token` to `@key` (returns true; the caller
		//! sets+commits). A short hold keeps the list up a few frames after the
		//! field deactivates so a click lands. No table loaded => nothing shows.
		bool drawLocCompletion(char const* fieldId, bool active, char* buffer,
			size_t bufSize, ImVec2 const& fieldMin, ImVec2 const& fieldMax)
		{
			static std::string openField;	// the field whose list is showing
			static int hold = 0;			// frames to keep it up after deactivation
			const std::string text(buffer);
			const size_t at = text.find_last_of('@');
			const bool typingToken = at != std::string::npos &&
				text.find_first_of(" \t", at) == std::string::npos;
			if(active && typingToken)
			{
				openField = fieldId;
				hold = 4;
			}
			if(openField != fieldId || hold <= 0 || !typingToken)
			{
				if(openField == fieldId && (!typingToken || hold <= 0))
				{
					openField.clear();
				}
				return false;
			}
			--hold;
			Orkige::StringTable* st = Orkige::StringTable::getSingletonPtr();
			if(!st) { return false; }
			const std::vector<String> keys = st->listKeys();
			const String needle =
				Orkige::StringUtil::to_lower_copy(text.substr(at + 1));
			std::vector<String> matches;
			for(String const& k : keys)
			{
				if(needle.empty() ||
					Orkige::StringUtil::to_lower_copy(k).find(needle) == 0)
				{
					matches.push_back(k);
					if(matches.size() >= 8) { break; }
				}
			}
			if(matches.empty()) { return false; }
			bool picked = false;
			ImGui::SetNextWindowPos(ImVec2(fieldMin.x, fieldMax.y));
			ImGui::SetNextWindowSize(ImVec2(fieldMax.x - fieldMin.x, 0.0f));
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_AlwaysAutoResize;
			const String winId = String("##loccomplete_") + fieldId;
			if(ImGui::Begin(winId.c_str(), NULL, flags))
			{
				for(String const& m : matches)
				{
					if(ImGui::Selectable(m.c_str()))
					{
						const std::string rebuilt = text.substr(0, at + 1) + m;
						std::snprintf(buffer, bufSize, "%s", rebuilt.c_str());
						picked = true;
						openField.clear();
					}
				}
			}
			ImGui::End();
			return picked;
		}
		//! a single-line string property row (label left, input right). The model
		//! updates live per keystroke (no reload); a gesture brackets the edit
		//! (beginEdit on focus, commitEdit on blur) so an edit is ONE undo step.
		//! When @p locComplete is set, typing `@` offers a localisation-key
		//! completion (@see drawLocCompletion). Returns true on commit (blur-after-
		//! edit, or a completion pick) so the caller persists once.
		bool textRow(UiEditDoc& doc, GuiLayoutSection& sec,
			char const* label, char const* tooltip, char const* key,
			bool locComplete = false)
		{
			char buffer[256] = { 0 };
			if(String const* v = sec.find(key))
			{
				std::snprintf(buffer, sizeof(buffer), "%s", v->c_str());
			}
			beginPropertyRow(label, tooltip);
			const String id = String("##") + key;
			const bool changed = ImGui::InputText(id.c_str(), buffer, sizeof(buffer));
			const bool activated = ImGui::IsItemActivated();
			const bool active = ImGui::IsItemActive();
			const bool committed = ImGui::IsItemDeactivatedAfterEdit();
			const ImVec2 fieldMin = ImGui::GetItemRectMin();
			const ImVec2 fieldMax = ImGui::GetItemRectMax();
			endPropertyRow();
			if(activated)
			{
				doc.beginEdit();	// snapshot before the first keystroke
			}
			if(changed)
			{
				sec.set(key, buffer);
			}
			// the `@key` completion draws its overlay AFTER the row; a pick rewrites
			// the buffer, which we commit as one edit (open a gesture if needed).
			if(locComplete && drawLocCompletion(key, active, buffer, sizeof(buffer),
				fieldMin, fieldMax))
			{
				doc.beginEdit();	// folds into an open gesture if already editing
				sec.set(key, buffer);
				doc.commitEdit();
				return true;
			}
			if(committed)
			{
				doc.commitEdit();
				return true;
			}
			return false;
		}
		//! @brief a BOOLEAN property row: a checkbox over one `key = true|false`
		//! entry. A flip is one whole edit (begin + set + commit), so it lands as a
		//! single undo step - a checkbox has no drag/blur phase to coalesce. The
		//! key is written explicitly in BOTH states (never dropped when false) so
		//! the file says what the screen shows.
		bool boolRow(UiEditDoc& doc, GuiLayoutSection& sec,
			char const* label, char const* tooltip, char const* key)
		{
			bool value = false;
			if(String const* v = sec.find(key))
			{
				const String lowered = Orkige::StringUtil::to_lower_copy(*v);
				value = (lowered == "true" || lowered == "1" ||
					lowered == "yes" || lowered == "on");
			}
			beginPropertyRow(label, tooltip);
			const String id = String("##") + key;
			const bool changed = ImGui::Checkbox(id.c_str(), &value);
			endPropertyRow();
			if(changed)
			{
				doc.beginEdit();
				sec.set(key, value ? "true" : "false");
				doc.commitEdit();
				return true;
			}
			return false;
		}
		//! the atlas name the current layout renders through (its [Layout] `atlas`
		//! key, else the engine default) - the atlas the sprite picker enumerates.
		String layoutAtlasName(UiEditSession const& s)
		{
			for(GuiLayoutSection const& sec : s.doc.doc().sections)
			{
				if(!sec.id.empty()) { continue; }	// widget sections carry an id
				String type = sec.type;
				std::transform(type.begin(), type.end(), type.begin(),
					[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
				if(type == "layout")
				{
					if(String const* a = sec.find("atlas")) { return *a; }
				}
			}
			return String("gui_default");
		}
		//! @brief resolve @p spriteName in @p atlasName to a live texture + UV rect.
		//! False (drawing nothing) when the atlas / sprite / texture cannot be
		//! resolved (classic / headless / an unknown name), which is the honest
		//! no-view state - the caller lays out a placeholder box then.
		bool spriteTexUv(String const& atlasName, String const& spriteName,
			ImTextureID& tex, ImVec2& uv0, ImVec2& uv1)
		{
			if(spriteName.empty()) { return false; }
			Orkige::GuiManager* gui = Orkige::GuiManager::getSingletonPtr();
			if(!gui) { return false; }
			Orkige::UiAtlas const* atlas = gui->getAtlas(atlasName);
			if(!atlas) { return false; }
			Orkige::UiSprite const* sp = atlas->getSprite(spriteName);
			if(!sp) { return false; }
			if(!gImGuiRenderer) { return false; }
			tex = gImGuiRenderer->textureIdForResource(atlas->getTextureName());
			if(tex == static_cast<ImTextureID>(0)) { return false; }
			uv0 = ImVec2(sp->uvLeft, sp->uvTop);
			uv1 = ImVec2(sp->uvRight, sp->uvBottom);
			return true;
		}
		//! the sprite thumbnail edge in a picker (field preview + popup rows).
		//! TASTE: 2x the former line-height preview, so a row reads as an actual
		//! sprite; the thumbnail drives the row / field height (still compact).
		float spriteThumbPx()
		{
			return std::floor(ImGui::GetTextLineHeight() * 2.0f);
		}
		//! @brief overlay the picker field's current-value preview - the sprite
		//! thumbnail (else a placeholder box on classic / headless) plus the name,
		//! vertically centred and clipped to the field so a long name never runs
		//! under the caret. Drawn on the window draw list (not as items), so the
		//! combo's own layout is untouched.
		void drawSpritePreviewOverlay(String const& atlasName, String const& cur,
			ImVec2 const& fieldMin, ImVec2 const& fieldMax, float thumb)
		{
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImGuiStyle& style = ImGui::GetStyle();
			const float cy = (fieldMin.y + fieldMax.y) * 0.5f;
			const ImVec2 boxMin(fieldMin.x + style.FramePadding.x, cy - thumb * 0.5f);
			const ImVec2 boxMax(boxMin.x + thumb, cy + thumb * 0.5f);
			float textX = fieldMin.x + style.FramePadding.x;
			ImTextureID tex; ImVec2 uv0, uv1;
			if(!cur.empty() && spriteTexUv(atlasName, cur, tex, uv0, uv1))
			{
				draw->AddImage(tex, boxMin, boxMax, uv0, uv1);
				textX = boxMax.x + style.ItemInnerSpacing.x;
			}
			else if(!cur.empty())
			{
				draw->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border));
				textX = boxMax.x + style.ItemInnerSpacing.x;
			}
			const char* text = cur.empty() ? "(none)" : cur.c_str();
			const ImU32 col = ImGui::GetColorU32(
				cur.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text);
			const float ty = cy - ImGui::GetTextLineHeight() * 0.5f;
			const float arrowW = fieldMax.y - fieldMin.y;	// the combo's arrow box
			draw->PushClipRect(fieldMin,
				ImVec2(fieldMax.x - arrowW, fieldMax.y), true);
			draw->AddText(ImVec2(textX, ty), col, text);
			draw->PopClipRect();
		}
		//! @brief draw the picker popup body (a search filter + the ordered
		//! entries from the pure spritePickerEntries, each a full-width row with a
		//! 2x thumbnail). A pick sets the `sprite` key as ONE undo step and closes
		//! the popup; returns true then (the caller persists). @p cur marks the
		//! selected row. Runs inside the open BeginCombo.
		bool drawSpritePickPopup(UiEditDoc& doc, GuiLayoutSection& sec,
			char const* key, String const& atlasName, String const& cur, float thumb)
		{
			static char filter[64] = { 0 };
			if(ImGui::IsWindowAppearing())
			{
				filter[0] = '\0';
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##spritesearch", "search sprites...",
				filter, sizeof(filter));
			ImGui::Separator();

			std::vector<String> names;
			if(Orkige::GuiManager* gui = Orkige::GuiManager::getSingletonPtr())
			{
				names = gui->getAtlasSpriteNames(atlasName);
			}
			const std::vector<UiSpritePickEntry> entries =
				spritePickerEntries(names, String(filter));

			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImGuiStyle& style = ImGui::GetStyle();
			bool committed = false;
			bool anySprite = false;
			for(size_t each = 0; each < entries.size(); ++each)
			{
				UiSpritePickEntry const& e = entries[each];
				ImGui::PushID(static_cast<int>(each));
				const ImVec2 p0 = ImGui::GetCursorScreenPos();
				const bool isSel = !e.isNone && cur == e.value;
				// a full-row selectable owns the hit area + the row height; the
				// thumbnail + name draw over it (draw list, so no extra layout).
				const bool clicked = ImGui::Selectable("##row", isSel,
					ImGuiSelectableFlags_None, ImVec2(0.0f, thumb));
				const ImVec2 boxMax(p0.x + thumb, p0.y + thumb);
				float textX = boxMax.x + style.ItemInnerSpacing.x;
				ImTextureID tex; ImVec2 uv0, uv1;
				if(!e.isNone && spriteTexUv(atlasName, e.value, tex, uv0, uv1))
				{
					draw->AddImage(tex, p0, boxMax, uv0, uv1);
					anySprite = true;
				}
				else if(!e.isNone)
				{
					draw->AddRect(p0, boxMax, ImGui::GetColorU32(ImGuiCol_Border));
				}
				else
				{
					textX = p0.x;	// the "(none)" clear carries no thumbnail
				}
				const ImU32 col = ImGui::GetColorU32(
					(e.isNone || e.isCustom) ? ImGuiCol_TextDisabled : ImGuiCol_Text);
				const float ty = p0.y + (thumb - ImGui::GetTextLineHeight()) * 0.5f;
				draw->AddText(ImVec2(textX, ty), col, e.label.c_str());
				if(clicked)
				{
					doc.beginEdit();
					sec.set(key, e.value);
					doc.commitEdit();
					committed = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			if(!anySprite)
			{
				// only the (none) clear (and maybe a typed free-text entry) exist:
				// the atlas is not live here (classic / headless) - say so
				ImGui::TextDisabled("no atlas sprites loaded");
			}
			return committed;
		}
		//! the `sprite` property row: ONE full-row-width combo whose caret is part
		//! of the field and whose popup matches the field width (the Anchor row
		//! idiom). The closed field previews the current sprite (thumbnail + name);
		//! the popup carries a search filter + the atlas sprites (the seam the
		//! runtime renders through - GuiManager::getAtlasSpriteNames) with a
		//! "(none)" clear and a free-text fallback for a name the live atlas has
		//! not loaded (classic / headless). Returns true on a committed change (the
		//! caller persists once).
		bool spriteRow(UiEditSession& s, UiEditDoc& doc, GuiLayoutSection& sec,
			char const* label, char const* tooltip, char const* key)
		{
			const String atlasName = layoutAtlasName(s);
			String const* curPtr = sec.find(key);
			const String cur = curPtr ? *curPtr : String();

			beginPropertyRow(label, tooltip);	// sets the full-column item width
			bool committed = false;

			const float thumb = spriteThumbPx();
			// grow the field so the doubled current-value thumbnail fits; the caret
			// + overlaid name centre in the taller frame. The row grows to the
			// thumbnail height (still compact).
			const ImGuiStyle& style = ImGui::GetStyle();
			const float padY = std::max(style.FramePadding.y,
				(thumb - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
				ImVec2(style.FramePadding.x, padY));

			const String comboId = String("##") + key;
			ImGui::SetNextItemWidth(-FLT_MIN);	// fill the value column (caret in field)
			const bool open = ImGui::BeginCombo(comboId.c_str(), "");
			const ImVec2 fieldMin = ImGui::GetItemRectMin();
			const ImVec2 fieldMax = ImGui::GetItemRectMax();
			// the frame is sized; the popup draws with the normal padding
			ImGui::PopStyleVar();
			ImGui::SetItemTooltip("pick a sprite from the layout atlas");
			if(open)
			{
				committed = drawSpritePickPopup(doc, sec, key, atlasName, cur, thumb);
				ImGui::EndCombo();
			}

			// the closed field's current-value preview, overlaid on the frame
			drawSpritePreviewOverlay(atlasName, cur, fieldMin, fieldMax, thumb);
			endPropertyRow();
			return committed;
		}
		//! the field key whose per-axis drag currently owns the open undo gesture
		//! ("" = none). Only one UI Editor panel exists, so a file-static is enough;
		//! drawUiEditToolsBody closes the gesture once the drag releases.
		std::string& activeAxisFieldKey()
		{
			static std::string key;
			return key;
		}
		//! a multi-value property row backed by a space-separated float string
		//! (@p key holds "l t r b" / "x y"), drawn as @p count per-axis drag-floats
		//! through the SHARED Inspector helper (trimmed display / full-precision
		//! edit). A drag folds into ONE undo step: the grab opens a gesture keyed by
		//! @p key (closed on release in drawUiEditToolsBody). Writes the value live
		//! (space-separated, whole numbers as integers - the .oui house form).
		//! Returns true when a value changed this frame (the caller sets needsReload).
		bool axisRow(UiEditSession& s, char const* label, char const* tooltip,
			char const* key, char const* const* axes, int count, float speed)
		{
			GuiLayoutSection* sec = selectedSection(s);
			if(!sec) { return false; }
			float v[4] = { 0, 0, 0, 0 };
			if(String const* cur = sec->find(key))
			{
				std::istringstream in(*cur);
				for(int each = 0; each < count; ++each) { in >> v[each]; }
			}
			beginPropertyRow(label, tooltip);
			bool activated = false;
			const bool edited =
				drawAxisDrags(key, axes, v, count, speed, &activated);
			endPropertyRow();
			if(activated && activeAxisFieldKey() != key)
			{
				// a fresh grab: close any stray gesture, then open this field's
				if(!activeAxisFieldKey().empty()) { s.doc.commitEdit(); }
				s.doc.beginEdit();
				activeAxisFieldKey() = key;
			}
			if(edited)
			{
				String out;
				char buf[64];
				for(int each = 0; each < count; ++each)
				{
					std::snprintf(buf, sizeof(buf), "%g", v[each]);
					if(each != 0) { out += ' '; }
					out += buf;
				}
				sec->set(key, out);
				s.needsReload = true;
			}
			return edited;
		}
		//! an anchor-preset combo row routed through the ONE size-preserving apply
		//! path (uiEditApplyAnchorPreset), so picking an anchor here can never leave
		//! a widget with a degenerate box (the same fix the gizmo click takes).
		bool anchorComboRow(UiEditSession& s, GamePreviewStage& stage,
			GuiLayoutSection& sec)
		{
			String anchor = sec.find("anchor") ? *sec.find("anchor")
				: String("topleft");
			beginPropertyRow("Anchor", "the widget's anchor preset");
			bool applied = false;
			if(ImGui::BeginCombo("##anchor", anchor.c_str()))
			{
				char const* presets[] = { "topleft","top","topright","left",
					"center","right","bottomleft","bottom","bottomright",
					"stretchtop","stretchmiddle","stretchbottom","stretchleft",
					"stretchcenter","stretchright","stretchall" };
				for(char const* preset : presets)
				{
					if(ImGui::Selectable(preset, anchor == preset))
					{
						Orkige::LayoutAnchorPreset parsed = Orkige::LAP_TOPLEFT;
						parseAnchorPreset(preset, parsed);
						uiEditApplyAnchorPreset(s, stage, parsed, AnchorPresetMods());
						applied = true;
					}
				}
				ImGui::EndCombo();
			}
			endPropertyRow();
			return applied;
		}
		//! the anchor-preset grid as a standard property ROW: a left-column
		//! "Alignment" label + the 4x4 gizmo RIGHT-ALIGNED in the value column,
		//! sized to the column width (square-ish cells), the Alt/Shift modifier hint
		//! under it. Each cell maps to a LayoutAnchorPreset; a click applies to the
		//! key widget (Alt ALSO moves the pivot, Shift ALSO keeps the on-screen
		//! rect). Must be called INSIDE the anchor property table. Returns true when
		//! it applied.
		bool anchorPresetGizmoRow(UiEditSession& s, GamePreviewStage& stage)
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
			// the property-row frame: "Alignment" label, grid in the value column
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Alignment");
			ImGui::SetItemTooltip("anchor preset - Alt also moves the pivot, "
				"Shift keeps the on-screen rect");
			ImGui::TableSetColumnIndex(1);

			// the grid takes three quarters of the value column, CENTERED in it -
			// wide enough to read as a proper property-row control beside the text
			// inputs, without the full-width block dominating the panel (cells stay
			// square by construction; only a tiny-panel floor)
			const float availW = ImGui::GetContentRegionAvail().x;
			float cell = (availW * 0.75f) / 4.0f;
			if(cell < 14.0f) { cell = 14.0f; }
			const float gridW = cell * 4.0f;
			const ImVec2 cellStart = ImGui::GetCursorScreenPos();
			const ImVec2 origin(cellStart.x +
				std::max(0.0f, (availW - gridW) * 0.5f), cellStart.y);
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
			// reserve the grid footprint (full column width), then the modifier hint
			// under it in the value column
			ImGui::SetCursorScreenPos(cellStart);
			ImGui::Dummy(ImVec2(availW, gridW));
			ImGui::TextDisabled("Alt: +pivot   Shift: keep rect");
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
		//! a square icon action button (a frame-height glyph button) for the
		//! add/rename/delete action row - the Inspector's component-row control
		//! shape. Returns true on click; @p enabled false dims it inert.
		bool actionIconButton(char const* id, char const* glyph, char const* tooltip,
			bool enabled)
		{
			const float side = ImGui::GetFrameHeight() * 1.6f;	// matches Add Widget
			ImGui::BeginDisabled(!enabled);
			ImGui::PushID(id);
			const bool clicked = ImGui::Button(glyph, ImVec2(side, side));
			ImGui::PopID();
			ImGui::EndDisabled();
			if(enabled && tooltip != NULL && tooltip[0] != '\0')
			{
				ImGui::SetItemTooltip("%s", tooltip);
			}
			return clicked;
		}
	}
	//=========================================================
	//=== the dockable "UI Editor" tool panel =================
	//=========================================================
	namespace
	{
		//! the tools body (tree / properties / anchor gizmo / align / add-delete),
		//! rendered inside the UI Editor panel window after its undo/save header
		void drawUiEditToolsBody(UiEditSession& s, GamePreviewStage& stage)
		{

		// align/distribute over a multi-selection (the key holds; others move)
		if(s.selection.size() >= 2)
		{
			if(alignRow(s, stage)) { String err; persist(s, stage, err); }
			ImGui::Separator();
		}

		// the widget tree: a real parent/child TreeNode hierarchy (carets fold, a
		// per-kind glyph leads each row), Shift/Ctrl(Cmd)-click extends the ordered
		// multi-selection, a double-click on the label opens the rename popup.
		// DRAG a row onto another to reparent (child of the target) or onto the
		// background to reparent to root; both defer past the loop (the section
		// vector must not mutate mid-iteration) and refuse a self/descendant cycle.
		ImGui::TextDisabled("Widgets");
		// while a widget row is being dragged, a one-line discoverability hint
		if(ImGuiPayload const* dp = ImGui::GetDragDropPayload())
		{
			if(dp->IsDataType("ORKIGE_UI_WIDGET"))
			{
				ImGui::TextDisabled(
					"Drop ONTO a row to nest, BETWEEN rows to reorder - Esc cancels");
			}
		}
		bool renameFromTree = false;
		// the deferred drop outcome (the section vector must not mutate mid-iteration):
		// a reparent under newParent ("" = root) plus, for a between-rows drop, the
		// sibling to sit next to and which side (@see uiEditReparent).
		std::string pendingReparentChild, pendingReparentParent, pendingReorderAnchor;
		bool havePendingReparent = false, pendingReorderAfter = false;
		// section order -> a root list + a parent->children index (order preserved)
		std::vector<std::string> treeRoots;
		std::map<std::string, std::vector<std::string>> treeChildren;
		for(GuiLayoutSection const& sec : s.doc.doc().sections)
		{
			if(sec.id.empty()) { continue; }	// [Layout]/[Modal] etc.
			String const* p = sec.find("parent");
			if(p && !p->empty() && sectionIndex(s.doc.doc(), *p) >= 0)
			{
				treeChildren[*p].push_back(sec.id);
			}
			else
			{
				treeRoots.push_back(sec.id);
			}
		}
		std::function<void(std::string const&)> drawTreeRow =
			[&](std::string const& id)
		{
			const int idx = sectionIndex(s.doc.doc(), id);
			if(idx < 0) { return; }
			GuiLayoutSection const& sec = s.doc.doc().sections[static_cast<size_t>(idx)];
			const std::vector<std::string>& kids = treeChildren[id];
			const bool hasKids = !kids.empty();
			const bool isSel = std::find(s.selection.begin(), s.selection.end(),
				id) != s.selection.end();
			ImGui::PushID(id.c_str());
			const String label = String(uiWidgetKindIcon(sec.type)) + "  " +
				id + "  (" + sec.type + ")";
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
			if(isSel) { flags |= ImGuiTreeNodeFlags_Selected; }
			bool open = false;
			if(hasKids)
			{
				// fold state lives in the session (not persisted); default OPEN, so
				// the collapsed set is the exception. Force it each frame and adopt
				// the user's caret toggle back into the set.
				ImGui::SetNextItemOpen(s.treeCollapsed.find(id) ==
					s.treeCollapsed.end(), ImGuiCond_Always);
				open = ImGui::TreeNodeEx(label.c_str(), flags);
				if(ImGui::IsItemToggledOpen())
				{
					if(s.treeCollapsed.count(id)) { s.treeCollapsed.erase(id); }
					else { s.treeCollapsed.insert(id); }
				}
			}
			else
			{
				// a leaf: no caret, no tree push (so no TreePop)
				ImGui::TreeNodeEx(label.c_str(), flags |
					ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet |
					ImGuiTreeNodeFlags_NoTreePushOnOpen);
			}
			// the row's screen rect (for the drop-zone split + the cue geometry)
			const ImVec2 rowMin = ImGui::GetItemRectMin();
			const ImVec2 rowMax = ImGui::GetItemRectMax();
			// a click (not the caret arrow) selects; a double-click opens rename
			if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			{
				if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					uiEditSelect(s, id);
					renameFromTree = true;
				}
				else
				{
					const ImGuiIO& io = ImGui::GetIO();
					uiEditTreeSelect(s, id,
						io.KeyShift || io.KeyCtrl || io.KeySuper);
				}
			}
			// drag this row as the reparent source - the default preview tooltip is
			// suppressed and a lifted-row GHOST is drawn following the cursor instead
			if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
			{
				ImGui::SetDragDropPayload("ORKIGE_UI_WIDGET", id.c_str(),
					id.size() + 1);
				drawTreeDragGhost(uiWidgetKindIcon(sec.type), id.c_str());
				ImGui::EndDragDropSource();
			}
			// drop on this row: the top/bottom quarter reorders as a SIBLING (an
			// insertion line), the middle half nests as a CHILD (a full-row highlight)
			if(ImGui::BeginDragDropTarget())
			{
				if(ImGuiPayload const* pl = ImGui::AcceptDragDropPayload(
					"ORKIGE_UI_WIDGET", ImGuiDragDropFlags_AcceptBeforeDelivery |
					ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
				{
					const std::string dragged(static_cast<char const*>(pl->Data));
					const TreeDropZone zone = classifyTreeDrop(rowMin.y,
						rowMax.y - rowMin.y, ImGui::GetMousePos().y);
					std::string newParent, anchor;
					bool after = false, valid = false;
					if(dragged != id)
					{
						if(zone == TreeDropZone::Into)
						{
							newParent = id;
							valid = canReparentWidget(s.doc.doc(), dragged, id);
						}
						else
						{
							// a sibling of the target: its parent + the target as the
							// order anchor
							if(String const* pp = sec.find("parent"))
							{
								if(!pp->empty() &&
									sectionIndex(s.doc.doc(), *pp) >= 0)
								{
									newParent = *pp;
								}
							}
							anchor = id;
							after = (zone == TreeDropZone::After);
							valid = canReparentWidget(s.doc.doc(), dragged, newParent);
						}
					}
					if(valid)
					{
						drawTreeDropCue(rowMin, rowMax, rowMin.x, zone);
						if(pl->IsDelivery())
						{
							pendingReparentChild = dragged;
							pendingReparentParent = newParent;
							pendingReorderAnchor = anchor;
							pendingReorderAfter = after;
							havePendingReparent = true;
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if(hasKids && open)
			{
				for(std::string const& kid : kids) { drawTreeRow(kid); }
				ImGui::TreePop();
			}
			ImGui::PopID();
		};
		for(std::string const& rootId : treeRoots) { drawTreeRow(rootId); }

		// a trailing drop zone: dropping onto the empty tree background reparents to
		// root (matches the Hierarchy's drop-to-root gesture)
		{
			const float rootZoneH = std::max(12.0f,
				ImGui::GetContentRegionAvail().y * 0.0f + 16.0f);
			const ImVec2 zoneMin = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(-1.0f, rootZoneH));
			if(ImGui::BeginDragDropTarget())
			{
				if(ImGuiPayload const* pl = ImGui::AcceptDragDropPayload(
					"ORKIGE_UI_WIDGET", ImGuiDragDropFlags_AcceptBeforeDelivery |
					ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
				{
					const std::string dragged(static_cast<char const*>(pl->Data));
					if(canReparentWidget(s.doc.doc(), dragged, std::string()))
					{
						ImGui::GetWindowDrawList()->AddRectFilled(zoneMin,
							ImVec2(ImGui::GetItemRectMax().x,
								ImGui::GetItemRectMax().y),
							IM_COL32(120, 170, 255, 40), 3.0f);
						if(pl->IsDelivery())
						{
							pendingReparentChild = dragged;
							pendingReparentParent.clear();
							pendingReorderAnchor.clear();
							havePendingReparent = true;
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		// apply the deferred reparent (ONE undo step + persist/reload), past the loop
		if(havePendingReparent)
		{
			std::string rerr;
			uiEditReparent(s, stage, pendingReparentChild, pendingReparentParent,
				rerr, pendingReorderAnchor, pendingReorderAfter);
		}

		ImGui::Separator();

		// properties of the selection, in the Inspector's grouped label/value form
		// (30/70 columns, the baked small value font, the shared dense grid style,
		// a titled header per group in the component-header visual language)
		if(GuiLayoutSection* sec = selectedSection(s))
		{
			ImGui::PushStyleColor(ImGuiCol_Header,
				Orkige::editorComponentHeaderColor());
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
				Orkige::editorComponentHeaderHoverColor());
			ImGui::PushStyleColor(ImGuiCol_HeaderActive,
				Orkige::editorComponentHeaderHoverColor());

			bool committed = false;	// discrete edits that persist this frame
			static char const* const LTRB[] = { "L", "T", "R", "B" };
			static char const* const XY[] = { "X", "Y" };
			static char const* const WH[] = { "W", "H" };

			// === Widget ===
			const String widgetHeader =
				String(sec->id) + "  (" + sec->type + ")###uiWidget";
			if(ImGui::CollapsingHeader(widgetHeader.c_str(),
				ImGuiTreeNodeFlags_DefaultOpen))
			{
				Orkige::pushPropertyGridStyle();
				if(ImGui::BeginTable("##uiwidget", 2,
					ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("label",
						ImGuiTableColumnFlags_WidthStretch, 0.30f);
					ImGui::TableSetupColumn("value",
						ImGuiTableColumnFlags_WidthStretch, 0.70f);
					// only the properties this widget KIND actually consumes: a decor
					// panel / progress bar has no caption, a label / scroll view has
					// no sprite face (@see kindHasText / kindHasSprite)
					const std::string kind = kindToken(*sec);
					if(kindHasText(kind))
					{
						committed |= textRow(s.doc, *sec, "Text",
							"the caption text ('@key' localises via the string table)",
							"text", true);	// @-completion of localisation keys
					}
					committed |= textRow(s.doc, *sec, "Z Order",
						"the render layer (higher draws on top)", "z");
					if(kindHasSprite(kind))
					{
						committed |= spriteRow(s, s.doc, *sec, "Sprite",
							"the atlas sprite face (button / panel / ...)", "sprite");
					}
					if(kindHasWrap(kind))
					{
						committed |= boolRow(s.doc, *sec, "Wrap",
							"break the text to the widget's width instead of one "
							"clipped line (pair it with a width and a preferred "
							"vertical fit so it grows)", "wrap");
					}
					if(kind == "textentry")
					{
						committed |= boolRow(s.doc, *sec, "Multi-line",
							"a text area: the text soft-wraps, Return inserts a "
							"line break (it never submits) and the view scrolls "
							"to follow the caret", "multiline");
					}
					if(kind == "listview")
					{
						committed |= boolRow(s.doc, *sec, "Virtualized",
							"materialise only the rows the viewport shows - a big "
							"list then costs a screenful of widgets. Needs a "
							"uniform Item Height.", "virtualized");
						committed |= textRow(s.doc, *sec, "Item Height",
							"the uniform row height in design units a virtualized "
							"list places its rows on", "itemHeight");
					}
					ImGui::EndTable();
				}
				Orkige::popPropertyGridStyle();
			}

			if(geomMode(*sec) == UiGeomMode::Layout)
			{
				// === Anchors ===
				if(ImGui::CollapsingHeader("Anchors###uiAnchors",
					ImGuiTreeNodeFlags_DefaultOpen))
				{
					Orkige::pushPropertyGridStyle();
					if(ImGui::BeginTable("##uianchor", 2,
						ImGuiTableFlags_SizingStretchProp))
					{
						ImGui::TableSetupColumn("label",
							ImGuiTableColumnFlags_WidthStretch, 0.30f);
						ImGui::TableSetupColumn("value",
							ImGuiTableColumnFlags_WidthStretch, 0.70f);
						// the visual anchor-preset gizmo as the leading "Alignment"
						// property row (its click applies + persists)
						if(anchorPresetGizmoRow(s, stage))
						{
							String err; persist(s, stage, err);
						}
						// the anchor combo re-resolves the section's geometry form,
						// so re-fetch it before drawing the per-axis rows
						committed |= anchorComboRow(s, stage, *sec);
						if(GuiLayoutSection* live = selectedSection(s))
						{
							if(live->find("offsets"))
							{
								axisRow(s, "Offsets",
									"left / top / right / bottom insets (design px)",
									"offsets", LTRB, 4, 0.5f);
							}
							if(live->find("anchoredPos"))
							{
								axisRow(s, "Position",
									"the pivot's offset from the anchor (design px)",
									"anchoredPos", XY, 2, 0.5f);
							}
							if(live->find("sizeDelta"))
							{
								axisRow(s, "Size",
									"size beyond the anchor span (design px)",
									"sizeDelta", WH, 2, 0.5f);
							}
							if(live->find("pivot"))
							{
								axisRow(s, "Pivot",
									"the 0..1 point the position anchors to (x y)",
									"pivot", XY, 2, 0.01f);
							}
						}
						ImGui::EndTable();
					}
					Orkige::popPropertyGridStyle();
				}
			}
			else
			{
				// === Transform (absolute geometry) ===
				if(ImGui::CollapsingHeader("Transform###uiTransform",
					ImGuiTreeNodeFlags_DefaultOpen))
				{
					Orkige::pushPropertyGridStyle();
					if(ImGui::BeginTable("##uitransform", 2,
						ImGuiTableFlags_SizingStretchProp))
					{
						ImGui::TableSetupColumn("label",
							ImGuiTableColumnFlags_WidthStretch, 0.30f);
						ImGui::TableSetupColumn("value",
							ImGuiTableColumnFlags_WidthStretch, 0.70f);
						axisRow(s, "Position",
							"absolute top-left position (design px)",
							"position", XY, 2, 0.5f);
						axisRow(s, "Size", "absolute size (design px)",
							"size", WH, 2, 0.5f);
						ImGui::EndTable();
					}
					Orkige::popPropertyGridStyle();
				}
			}

			ImGui::PopStyleColor(3);

			// close a per-axis drag once the mouse releases: ONE undo step + one
			// persist per drag (axisRow opened the gesture on grab and edited the
			// model live; the canvas / hot-reload catch up here on release, exactly
			// like the canvas gizmo and the text fields' commit-on-blur)
			if(!activeAxisFieldKey().empty() &&
				!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				s.doc.commitEdit();
				activeAxisFieldKey().clear();
				committed = true;
			}
			if(committed)
			{
				String err; persist(s, stage, err);
			}
		}
		else
		{
			ImGui::TextDisabled("Select a widget to edit its properties.");
		}

		ImGui::Separator();

		// the action ROW (the Inspector's Add Component row shape): the "Add
		// Widget" primary button, then a rename (pen) and a delete (trash) icon
		// button on the SAME line - both act on the selection, dimmed inert when
		// nothing is selected. Add opens the kind->name picker; the pen opens the
		// name popup for the selected widget; the trash removes the selection.
		const bool haveSel = !s.selected.empty();
		bool openRename = renameFromTree;
		Orkige::pushInspectorButtonStyle();
		// the picker opens DOCKED under the Add Widget button - its bottom-left in
		// screen px, captured right after the button is laid out
		ImVec2 addPickAnchor(0.0f, 0.0f);
		{
			// size the Add Widget button to leave room for the two trailing icons
			const float iconSide = ImGui::GetFrameHeight() * 1.6f;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float availW = ImGui::GetContentRegionAvail().x;
			const float addW = std::max(ImGui::GetFrameHeight() * 3.0f,
				availW - 2.0f * (iconSide + spacing));
			if(ImGui::Button("Add Widget", ImVec2(addW, iconSide)))
			{
				ImGui::OpenPopup("##addpick_panel");
			}
			addPickAnchor = ImVec2(ImGui::GetItemRectMin().x,
				ImGui::GetItemRectMax().y);
		}
		ImGui::SameLine();
		if(actionIconButton("##uiRename", ICON_FA_PEN, "Rename widget", haveSel))
		{
			openRename = true;
		}
		ImGui::SameLine();
		if(actionIconButton("##uiDelete", ICON_FA_TRASH_CAN, "Delete widget",
			haveSel))
		{
			uiEditDeleteSelected(s);	// the whole selection, ONE undo step
			String err; persist(s, stage, err);
		}
		Orkige::popInspectorButtonStyle();
		// drive the Add button's kind->name popups (opened above), docked under it
		runAddWidgetPopups(s, stage, "panel", addPickAnchor);

		// the rename name popup (double-click a tree row or the pen button opens
		// it): prefilled with the current id, uniqueness enforced with an honest
		// inline error (never a silent rename on collision).
		static char renameBuf[64] = { 0 };
		if(openRename && !s.selected.empty())
		{
			std::snprintf(renameBuf, sizeof(renameBuf), "%s", s.selected.c_str());
			ImGui::OpenPopup("##uiRenamePopup");
		}
		if(ImGui::BeginPopup("##uiRenamePopup"))
		{
			const bool appearing = ImGui::IsWindowAppearing();
			ImGui::TextDisabled("Rename '%s'", s.selected.c_str());
			const NameEntry r = nameEntryBody(s.doc.doc(), s.selected, appearing,
				renameBuf, sizeof(renameBuf));
			if(r == NameEntry::Confirm)
			{
				std::string rerr;
				uiEditRenameSelected(s, stage, std::string(renameBuf), rerr);
				ImGui::CloseCurrentPopup();
			}
			else if(r == NameEntry::Cancel)
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// clicking the panel's empty background (no tree row / control) clears the
		// selection - the standard deselect gesture, matching the canvas's
		// empty-click. IsAnyItemHovered() shields every real item (tree rows,
		// property widgets, the action buttons); an EXPLICIT any-popup guard keeps it
		// inert while the add/rename picker is open (a click that opens or works a
		// popup must never read as a background deselect and clear the captured add
		// parent between the kind pick and the confirm). Routes through the ONE
		// selection seam and pushes no undo step.
		if(!s.selection.empty() && ImGui::IsWindowHovered() &&
			!ImGui::IsAnyItemHovered() &&
			!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
				ImGuiPopupFlags_AnyPopupLevel) &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			uiEditSelect(s, String());	// clears selection + key
		}
		}	// drawUiEditToolsBody
	}
	//---------------------------------------------------------
	UiEditorPanelLink& uiEditorPanelLink()
	{
		static UiEditorPanelLink link;
		return link;
	}
	//---------------------------------------------------------
	bool uiEditContextWantsUndo()
	{
		UiEditorPanelLink const& link = uiEditorPanelLink();
		return link.editActive && link.contextFocused &&
			link.session != nullptr && link.session->loaded;
	}
	//---------------------------------------------------------
	void uiEditUndoShared()
	{
		UiEditorPanelLink const& link = uiEditorPanelLink();
		if(!link.editActive || !link.session || !link.stage) { return; }
		if(!link.session->doc.canUndo()) { return; }
		uiEditUndo(*link.session);
		String err; persist(*link.session, *link.stage, err);
	}
	//---------------------------------------------------------
	void uiEditRedoShared()
	{
		UiEditorPanelLink const& link = uiEditorPanelLink();
		if(!link.editActive || !link.session || !link.stage) { return; }
		if(!link.session->doc.canRedo()) { return; }
		link.session->doc.redo();
		pruneSelection(*link.session);
		String err; persist(*link.session, *link.stage, err);
	}
	//---------------------------------------------------------
	void drawUiEditorPanel(bool* open)
	{
		static bool dockAttempted = false;
		dockUiEditorBesideInspectorOnce(dockAttempted);
		const bool shown = ImGui::Begin(UI_EDITOR_WINDOW_EDIT, open);
		editorPanelTabMenu(open);
		if(!shown)
		{
			ImGui::End();
			return;
		}
		UiEditorPanelLink& link = uiEditorPanelLink();
		// the panel holding focus makes Cmd/Ctrl+Z edit the document (not the scene)
		link.contextFocused |=
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if(!link.editActive || !link.session || !link.session->loaded ||
			!link.stage)
		{
			// honest empty state (the house dormant-panel pattern). This panel is
			// normally opened/retired with the Preview's picked screen, so this is
			// only a transient safety net.
			ImGui::TextDisabled("No UI open.");
			ImGui::TextWrapped(
				"Open a .oui screen in the Preview panel to edit its widgets here.");
			ImGui::End();
			return;
		}

		UiEditSession& s = *link.session;
		GamePreviewStage& stage = *link.stage;

		// header: undo / redo / save + the dirty indicator (the save state reads
		// best beside its controls - the Preview slim row carries no indicator)
		ImGui::BeginDisabled(!s.doc.canUndo());
		if(ImGui::SmallButton("Undo")) { uiEditUndoShared(); }
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!s.doc.canRedo());
		if(ImGui::SmallButton("Redo")) { uiEditRedoShared(); }
		ImGui::EndDisabled();
		ImGui::SameLine();
		if(ImGui::SmallButton("Save"))
		{
			String err; persist(s, stage, err);
		}
		ImGui::SameLine();
		ImGui::TextDisabled(s.doc.dirty() ? "* unsaved" : "saved");
		ImGui::Separator();

		drawUiEditToolsBody(s, stage);
		ImGui::End();
	}
}
