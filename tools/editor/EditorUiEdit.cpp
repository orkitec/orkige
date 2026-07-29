/********************************************************************
	created:	Saturday 2026/07/26 at 12:00
	filename: 	EditorUiEdit.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the visual `.oui` editor's UI-independent editing core
				(@see EditorUiEdit.h).
*********************************************************************/

#include "EditorUiEdit.h"

#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <cctype>

namespace OrkigeEditor
{
	using Orkige::String;
	using Orkige::GuiLayoutDoc;
	using Orkige::GuiLayoutSection;
	using Orkige::LayoutNode;
	using Orkige::LayoutVec2;

	namespace
	{
		//! format a number cleanly: whole values as integers (the .oui house
		//! form - "460", "0"), fractions with trimmed trailing zeros
		String fmtNum(float v)
		{
			if(std::fabs(v - std::round(v)) < 1e-4f)
			{
				std::ostringstream out;
				out << static_cast<long>(std::lround(v));
				return out.str();
			}
			std::ostringstream out;
			out.precision(4);
			out << v;
			return out.str();
		}
		//! "x y"
		String fmtVec2(float x, float y)
		{
			return fmtNum(x) + " " + fmtNum(y);
		}
		//! "l t r b"
		String fmtQuad(float l, float t, float r, float b)
		{
			return fmtNum(l) + " " + fmtNum(t) + " " + fmtNum(r) + " " + fmtNum(b);
		}
		//! read up to four whitespace-separated floats from a value string
		void readFloats(String const& value, float* out, int count)
		{
			std::istringstream in(value);
			for(int each = 0; each < count; ++each)
			{
				if(!(in >> out[each]))
				{
					out[each] = 0.0f;
				}
			}
		}
		//! snap @p v to the nearest multiple of @p step (step <= 0 => unchanged)
		float snapTo(float v, float step)
		{
			if(step <= 0.0f)
			{
				return v;
			}
			return std::round(v / step) * step;
		}
		//! design-units per surface pixel is 1/layoutScale; guard a zero scale
		float toDesign(float px, float layoutScale)
		{
			const float scale = layoutScale > 1e-6f ? layoutScale : 1.0f;
			return px / scale;
		}
		//! does the section carry any of the named keys?
		bool hasAny(GuiLayoutSection const& s, std::initializer_list<char const*> keys)
		{
			for(char const* key : keys)
			{
				if(s.find(key) != NULL)
				{
					return true;
				}
			}
			return false;
		}
	}
	//---------------------------------------------------------
	std::vector<UiWidgetKind> uiWidgetKinds()
	{
		// the widget [Type]s GuiFactory::loadLayoutImpl creates (pass 1); the
		// palette lists exactly these. Non-widget sections (Layout/Modal/
		// ToggleGroup) are not palette kinds.
		return {
			{ "label",		"Label" },
			{ "button",		"Button" },
			{ "checkbox",	"Check Box" },
			{ "slider",		"Slider" },
			{ "progressbar","Progress Bar" },
			{ "selectmenu",	"Select Menu" },
			{ "dropdown",	"Drop Down" },
			{ "textentry",	"Text Entry" },
			{ "textbox",	"Text Box" },
			{ "panel",		"Panel" },
			{ "scrollview",	"Scroll View" },
			{ "listview",	"List View" },
		};
	}
	//---------------------------------------------------------
	char const* uiWidgetKindIcon(String const& type)
	{
		String kind = type;
		std::transform(kind.begin(), kind.end(), kind.begin(),
			[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		// each glyph's codepoint is in EditorTheme's ICON_GLYPH_RANGES (mirrored by
		// the WidgetKindIcon unit test); a decorwidget IS a panel face
		if(kind == "label")			{ return ICON_FA_FONT; }			// U+f031
		if(kind == "button")		{ return ICON_FA_WINDOW_MAXIMIZE; }	// U+f2d0
		if(kind == "checkbox")		{ return ICON_FA_SQUARE_CHECK; }	// U+f14a
		if(kind == "slider")		{ return ICON_FA_SLIDERS; }			// U+f1de
		if(kind == "progressbar")	{ return ICON_FA_BARS_PROGRESS; }	// U+f828
		if(kind == "selectmenu")	{ return ICON_FA_RECTANGLE_LIST; }	// U+f022
		if(kind == "dropdown")		{ return ICON_FA_SQUARE_CARET_DOWN; }// U+f150
		if(kind == "textentry")		{ return ICON_FA_KEYBOARD; }		// U+f11c
		if(kind == "textbox")		{ return ICON_FA_FILE_LINES; }		// U+f15c
		if(kind == "panel" || kind == "decorwidget") { return ICON_FA_TABLE_CELLS; }	// U+f00a
		if(kind == "scrollview")	{ return ICON_FA_LIST; }			// U+f03a
		if(kind == "listview")		{ return ICON_FA_LIST_UL; }			// U+f0ca
		return ICON_FA_WINDOW_MAXIMIZE;	// the generic control fallback (U+f2d0)
	}
	//---------------------------------------------------------
	String hitTestWidget(std::vector<UiRect> const& rects, float px, float py)
	{
		// among the rects CONTAINING the point, pick the one drawn on top:
		// higher z first, then the DEEPER widget (a child inside its parent wins
		// at equal z - so a button inside a decor panel is selectable, never
		// swallowed), then painter order (a later index wins). With every rect at
		// the default z==0/depth==0 this reduces to "the last matching rect wins".
		int best = -1;
		for(size_t each = 0; each < rects.size(); ++each)
		{
			UiRect const& r = rects[each];
			if(px < r.left || px > r.left + r.width ||
				py < r.top || py > r.top + r.height)
			{
				continue;
			}
			if(best < 0)
			{
				best = static_cast<int>(each);
				continue;
			}
			UiRect const& cur = rects[static_cast<size_t>(best)];
			const bool better = r.z > cur.z ||
				(r.z == cur.z && r.depth > cur.depth) ||
				(r.z == cur.z && r.depth == cur.depth);	// equal => later index wins
			if(better)
			{
				best = static_cast<int>(each);
			}
		}
		return best < 0 ? String() : rects[static_cast<size_t>(best)].id;
	}
	//---------------------------------------------------------
	std::vector<String> hitTestAllWidgets(std::vector<UiRect> const& rects,
		float px, float py)
	{
		// collect the indices of every rect containing the point, then order them by
		// the SAME winner rule hitTestWidget uses (z desc, then depth desc, then a
		// later painter index on top) so front() == hitTestWidget's pick and the rest
		// descend layer by layer.
		std::vector<size_t> hits;
		for(size_t each = 0; each < rects.size(); ++each)
		{
			UiRect const& r = rects[each];
			if(px < r.left || px > r.left + r.width ||
				py < r.top || py > r.top + r.height)
			{
				continue;
			}
			hits.push_back(each);
		}
		std::sort(hits.begin(), hits.end(), [&](size_t a, size_t b)
		{
			UiRect const& ra = rects[a];
			UiRect const& rb = rects[b];
			if(ra.z != rb.z) { return ra.z > rb.z; }
			if(ra.depth != rb.depth) { return ra.depth > rb.depth; }
			return a > b;	// a later painter index draws on top
		});
		std::vector<String> out;
		out.reserve(hits.size());
		for(size_t idx : hits)
		{
			out.push_back(rects[idx].id);
		}
		return out;
	}
	//---------------------------------------------------------
	String cycleStackSelection(std::vector<String> const& stack, String const& current)
	{
		if(stack.empty())
		{
			return String();
		}
		for(size_t each = 0; each < stack.size(); ++each)
		{
			if(stack[each] == current)
			{
				return stack[(each + 1) % stack.size()];	// one layer down, wrapping
			}
		}
		return stack.front();	// current not in the stack -> start at the topmost
	}
	//---------------------------------------------------------
	UiTreeClickAction uiTreeClickAction(bool alreadySelected, bool additive)
	{
		// a modifier extends/removes from the ordered set; a plain re-click of an
		// already-selected row toggles it OFF; every other plain click replaces.
		return (additive || alreadySelected) ? UiTreeClickAction::Toggle
			: UiTreeClickAction::Replace;
	}
	//---------------------------------------------------------
	UiHandle handleAt(UiRect const& r, float px, float py, float grab)
	{
		const float left = r.left;
		const float right = r.left + r.width;
		const float top = r.top;
		const float bottom = r.top + r.height;
		// outside the grown rect => nothing
		if(px < left - grab || px > right + grab ||
			py < top - grab || py > bottom + grab)
		{
			return UiHandle::None;
		}
		bool nearL = std::fabs(px - left) <= grab;
		bool nearR = std::fabs(px - right) <= grab;
		bool nearT = std::fabs(py - top) <= grab;
		bool nearB = std::fabs(py - bottom) <= grab;
		// a rect SMALLER than the grab tolerance puts the pointer near BOTH
		// opposite edges at once - keep only the NEARER one, else the fixed
		// corner priority below grabs the far corner (pressing the bottom-right
		// of a tiny widget read as TopRight, and the outward drag collapsed the
		// height to zero)
		if(nearL && nearR)
		{
			if(std::fabs(px - left) <= std::fabs(px - right)) { nearR = false; }
			else { nearL = false; }
		}
		if(nearT && nearB)
		{
			if(std::fabs(py - top) <= std::fabs(py - bottom)) { nearB = false; }
			else { nearT = false; }
		}
		// corners beat edges
		if(nearL && nearT) { return UiHandle::TopLeft; }
		if(nearR && nearT) { return UiHandle::TopRight; }
		if(nearL && nearB) { return UiHandle::BottomLeft; }
		if(nearR && nearB) { return UiHandle::BottomRight; }
		if(nearL) { return UiHandle::Left; }
		if(nearR) { return UiHandle::Right; }
		if(nearT) { return UiHandle::Top; }
		if(nearB) { return UiHandle::Bottom; }
		// interior
		if(px >= left && px <= right && py >= top && py <= bottom)
		{
			return UiHandle::Move;
		}
		return UiHandle::None;
	}
	//---------------------------------------------------------
	UiGeomMode geomMode(GuiLayoutSection const& s)
	{
		if(hasAny(s, { "anchor", "anchorMin", "anchorMax", "pivot", "offsets",
			"anchoredPos", "sizeDelta" }))
		{
			return UiGeomMode::Layout;
		}
		return UiGeomMode::Absolute;
	}
	//---------------------------------------------------------
	bool parseAnchorPreset(String const& name, Orkige::LayoutAnchorPreset& out)
	{
		using namespace Orkige;
		String key = name;
		std::transform(key.begin(), key.end(), key.begin(),
			[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		out = LAP_TOPLEFT;
		if(key == "topleft")		{ out = LAP_TOPLEFT; return true; }
		if(key == "top")			{ out = LAP_TOP; return true; }
		if(key == "topright")		{ out = LAP_TOPRIGHT; return true; }
		if(key == "left")			{ out = LAP_LEFT; return true; }
		if(key == "center" || key == "centre") { out = LAP_CENTER; return true; }
		if(key == "right")			{ out = LAP_RIGHT; return true; }
		if(key == "bottomleft")		{ out = LAP_BOTTOMLEFT; return true; }
		if(key == "bottom")			{ out = LAP_BOTTOM; return true; }
		if(key == "bottomright")	{ out = LAP_BOTTOMRIGHT; return true; }
		if(key == "stretchtop")		{ out = LAP_STRETCH_TOP; return true; }
		if(key == "stretchmiddle")	{ out = LAP_STRETCH_MIDDLE; return true; }
		if(key == "stretchbottom")	{ out = LAP_STRETCH_BOTTOM; return true; }
		if(key == "stretchleft")	{ out = LAP_STRETCH_LEFT; return true; }
		if(key == "stretchcenter" || key == "stretchcentre")
									{ out = LAP_STRETCH_CENTER; return true; }
		if(key == "stretchright")	{ out = LAP_STRETCH_RIGHT; return true; }
		if(key == "stretchall" || key == "stretch")
									{ out = LAP_STRETCH_ALL; return true; }
		return false;
	}
	//---------------------------------------------------------
	LayoutNode sectionLayoutNode(GuiLayoutSection const& s)
	{
		// mirror GuiFactory::applyLayoutKeys ordering so the node the panel draws
		// matches what the runtime resolves
		LayoutNode node;
		if(String const* v = s.find("anchor"))
		{
			Orkige::LayoutAnchorPreset preset = Orkige::LAP_TOPLEFT;
			parseAnchorPreset(*v, preset);
			Orkige::applyAnchorPreset(node, preset);
		}
		if(String const* v = s.find("anchorMin"))
		{
			float mn[2] = { 0, 0 };
			readFloats(*v, mn, 2);
			float mx[2] = { mn[0], mn[1] };
			if(String const* vmax = s.find("anchorMax"))
			{
				readFloats(*vmax, mx, 2);
			}
			node.anchorMin = { mn[0], mn[1] };
			node.anchorMax = { mx[0], mx[1] };
		}
		if(String const* v = s.find("pivot"))
		{
			float p[2] = { 0, 0 };
			readFloats(*v, p, 2);
			node.pivot = { p[0], p[1] };
		}
		if(String const* v = s.find("offsets"))
		{
			float q[4] = { 0, 0, 0, 0 };
			readFloats(*v, q, 4);
			node.offsetMin = { q[0], q[1] };
			node.offsetMax = { q[2], q[3] };
		}
		if(String const* v = s.find("anchoredPos"))
		{
			float p[2] = { 0, 0 };
			readFloats(*v, p, 2);
			node.setAnchoredPosition(p[0], p[1]);
		}
		if(String const* v = s.find("sizeDelta"))
		{
			float sz[2] = { 0, 0 };
			readFloats(*v, sz, 2);
			node.setSizeDelta(sz[0], sz[1]);
		}
		return node;
	}
	//---------------------------------------------------------
	void applyMove(GuiLayoutSection& s, float dxPx, float dyPx,
		float layoutScale, float snapDesign)
	{
		const float ddx = toDesign(dxPx, layoutScale);
		const float ddy = toDesign(dyPx, layoutScale);

		if(geomMode(s) == UiGeomMode::Absolute)
		{
			float p[2] = { 0, 0 };
			if(String const* v = s.find("position")) { readFloats(*v, p, 2); }
			s.set("position", fmtVec2(
				snapTo(p[0] + ddx, snapDesign), snapTo(p[1] + ddy, snapDesign)));
			return;
		}
		// Layout mode. Prefer the section's own geometry form for a minimal diff:
		// an offsets-form widget shifts all four offsets (anchor-preserving); a
		// friendly-form widget shifts anchoredPos.
		const bool offsetsForm = s.find("offsets") != NULL &&
			s.find("anchoredPos") == NULL && s.find("sizeDelta") == NULL;
		if(offsetsForm)
		{
			float q[4] = { 0, 0, 0, 0 };
			readFloats(*s.find("offsets"), q, 4);
			// snap the top-left corner, translate the far corner by the same amount
			const float newL = snapTo(q[0] + ddx, snapDesign);
			const float newT = snapTo(q[1] + ddy, snapDesign);
			const float appliedX = newL - q[0];
			const float appliedY = newT - q[1];
			s.set("offsets", fmtQuad(newL, newT, q[2] + appliedX, q[3] + appliedY));
			return;
		}
		LayoutNode node = sectionLayoutNode(s);
		const LayoutVec2 ap = node.anchoredPosition();
		s.set("anchoredPos", fmtVec2(
			snapTo(ap.x + ddx, snapDesign), snapTo(ap.y + ddy, snapDesign)));
	}
	//---------------------------------------------------------
	namespace
	{
		bool affectsLeft(UiHandle h)
		{
			return h == UiHandle::Left || h == UiHandle::TopLeft ||
				h == UiHandle::BottomLeft;
		}
		bool affectsRight(UiHandle h)
		{
			return h == UiHandle::Right || h == UiHandle::TopRight ||
				h == UiHandle::BottomRight;
		}
		bool affectsTop(UiHandle h)
		{
			return h == UiHandle::Top || h == UiHandle::TopLeft ||
				h == UiHandle::TopRight;
		}
		bool affectsBottom(UiHandle h)
		{
			return h == UiHandle::Bottom || h == UiHandle::BottomLeft ||
				h == UiHandle::BottomRight;
		}
	}
	//---------------------------------------------------------
	void applyResize(GuiLayoutSection& s, UiHandle handle,
		float dxPx, float dyPx, float layoutScale, float snapDesign)
	{
		if(handle == UiHandle::None || handle == UiHandle::Move)
		{
			return;
		}
		const float ddx = toDesign(dxPx, layoutScale);
		const float ddy = toDesign(dyPx, layoutScale);

		if(geomMode(s) == UiGeomMode::Absolute)
		{
			float p[2] = { 0, 0 };
			float sz[2] = { 0, 0 };
			if(String const* v = s.find("position")) { readFloats(*v, p, 2); }
			if(String const* v = s.find("size")) { readFloats(*v, sz, 2); }
			// dragging a left/top edge moves the origin AND shrinks the size
			if(affectsLeft(handle)) { p[0] += ddx; sz[0] -= ddx; }
			if(affectsRight(handle)) { sz[0] += ddx; }
			if(affectsTop(handle)) { p[1] += ddy; sz[1] -= ddy; }
			if(affectsBottom(handle)) { sz[1] += ddy; }
			s.set("position", fmtVec2(p[0], p[1]));
			s.set("size", fmtVec2(
				snapTo(std::max(0.0f, sz[0]), snapDesign),
				snapTo(std::max(0.0f, sz[1]), snapDesign)));
			return;
		}
		const bool offsetsForm = s.find("offsets") != NULL &&
			s.find("anchoredPos") == NULL && s.find("sizeDelta") == NULL;
		if(offsetsForm)
		{
			float q[4] = { 0, 0, 0, 0 };
			readFloats(*s.find("offsets"), q, 4);
			if(affectsLeft(handle)) { q[0] = snapTo(q[0] + ddx, snapDesign); }
			if(affectsRight(handle)) { q[2] = snapTo(q[2] + ddx, snapDesign); }
			if(affectsTop(handle)) { q[1] = snapTo(q[1] + ddy, snapDesign); }
			if(affectsBottom(handle)) { q[3] = snapTo(q[3] + ddy, snapDesign); }
			s.set("offsets", fmtQuad(q[0], q[1], q[2], q[3]));
			return;
		}
		// friendly form: grow sizeDelta about the pivot. A left/top drag grows
		// as a right/bottom drag would (the pivot stays fixed) - documented v1.
		LayoutNode node = sectionLayoutNode(s);
		const LayoutVec2 sd = node.sizeDelta();
		float dw = 0.0f;
		float dh = 0.0f;
		if(affectsRight(handle)) { dw += ddx; }
		if(affectsLeft(handle)) { dw -= ddx; }
		if(affectsBottom(handle)) { dh += ddy; }
		if(affectsTop(handle)) { dh -= ddy; }
		s.set("sizeDelta", fmtVec2(
			snapTo(std::max(0.0f, sd.x + dw), snapDesign),
			snapTo(std::max(0.0f, sd.y + dh), snapDesign)));
	}
	//=========================================================
	//=== alignment tooling ===================================
	//=========================================================
	namespace
	{
		//! remove every entry for @p key (entries is order-preserving public data)
		void eraseKey(GuiLayoutSection& s, char const* key)
		{
			for(std::vector<Orkige::GuiLayoutEntry>::iterator it = s.entries.begin();
				it != s.entries.end(); )
			{
				it = (it->key == key) ? s.entries.erase(it) : it + 1;
			}
		}
		//! friendly form = anchoredPos/sizeDelta present (they override offsets)
		bool friendlyForm(GuiLayoutSection const& s)
		{
			return s.find("anchoredPos") != NULL || s.find("sizeDelta") != NULL;
		}
		//! the anchor reference points (surface px) for a node against a parent
		void anchorRefs(LayoutNode const& node, Orkige::LayoutRect const& parent,
			float& axMin, float& axMax, float& ayMin, float& ayMax)
		{
			axMin = parent.x + node.anchorMin.x * parent.w;
			axMax = parent.x + node.anchorMax.x * parent.w;
			ayMin = parent.y + node.anchorMin.y * parent.h;
			ayMax = parent.y + node.anchorMax.y * parent.h;
		}
		//! recompute a node's offsets so resolveRect(parent,node,scale) == target
		void keepRectOffsets(LayoutNode& node, Orkige::LayoutRect const& parent,
			Orkige::LayoutRect const& target, float scale)
		{
			const float s = scale > 1e-6f ? scale : 1.0f;
			float axMin, axMax, ayMin, ayMax;
			anchorRefs(node, parent, axMin, axMax, ayMin, ayMax);
			node.offsetMin.x = (target.x - axMin) / s;
			node.offsetMax.x = (target.x + target.w - axMax) / s;
			node.offsetMin.y = (target.y - ayMin) / s;
			node.offsetMax.y = (target.y + target.h - ayMax) / s;
		}
		//! write a node's size/position back into the section, preserving its
		//! form: friendly (anchoredPos + sizeDelta) or offsets (the default).
		void writeGeom(GuiLayoutSection& s, LayoutNode const& node)
		{
			if(friendlyForm(s))
			{
				const LayoutVec2 ap = node.anchoredPosition();
				const LayoutVec2 sd = node.sizeDelta();
				s.set("anchoredPos", fmtVec2(ap.x, ap.y));
				s.set("sizeDelta", fmtVec2(sd.x, sd.y));
				eraseKey(s, "offsets");	// friendly form is authoritative now
			}
			else
			{
				s.set("offsets", fmtQuad(node.offsetMin.x, node.offsetMin.y,
					node.offsetMax.x, node.offsetMax.y));
			}
		}
		float clamp01(float v)
		{
			return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
		}
	}
	//---------------------------------------------------------
	LayoutVec2 anchorPresetPoint(Orkige::LayoutAnchorPreset preset)
	{
		LayoutNode probe;
		Orkige::applyAnchorPreset(probe, preset);
		LayoutVec2 point;
		point.x = (probe.anchorMin.x + probe.anchorMax.x) * 0.5f;
		point.y = (probe.anchorMin.y + probe.anchorMax.y) * 0.5f;
		return point;
	}
	//---------------------------------------------------------
	void applyAnchorPresetToSection(GuiLayoutSection& s,
		Orkige::LayoutAnchorPreset preset, AnchorPresetMods mods,
		Orkige::LayoutRect const& parentRect, float layoutScale)
	{
		LayoutNode node = sectionLayoutNode(s);
		const Orkige::LayoutRect kept = resolveRect(parentRect, node, layoutScale);

		Orkige::applyAnchorPreset(node, preset);
		if(mods.alsoPivot)
		{
			node.pivot = anchorPresetPoint(preset);
		}
		if(mods.alsoKeepRect)
		{
			// keep the whole on-screen rect (position AND size) pinned
			keepRectOffsets(node, parentRect, kept, layoutScale);
		}
		else
		{
			// PLAIN apply re-homes the widget to the new anchor but PRESERVES its
			// on-screen SIZE. Keeping the raw offsets across an anchor whose span
			// changed (a stretch axis collapsing to a point, say stretchtop->center)
			// reinterprets a right/bottom inset against the new, smaller anchor rect
			// and yields a ZERO- or NEGATIVE-width box - a degenerate rect whose
			// runtime caption then centres its text OUTSIDE the widget. Re-derive
			// sizeDelta for the new anchor span so the resolved size is unchanged;
			// anchoredPosition (the pivot's offset from the anchor point) is left
			// as-is, so the widget still follows the new anchor. A point->point
			// preset leaves the size untouched; a stretch axis keeps its span via a
			// matching (possibly negative) sizeDelta - keep-the-size re-anchoring.
			const float scale = layoutScale > 1e-6f ? layoutScale : 1.0f;
			const float spanX = node.anchorMax.x - node.anchorMin.x;
			const float spanY = node.anchorMax.y - node.anchorMin.y;
			node.setSizeDelta(
				(kept.w - spanX * parentRect.w) / scale,
				(kept.h - spanY * parentRect.h) / scale);
		}

		// name the preset (drops any raw anchorMin/anchorMax pair)
		char const* names[] = { "topleft","top","topright","left","center","right",
			"bottomleft","bottom","bottomright","stretchtop","stretchmiddle",
			"stretchbottom","stretchleft","stretchcenter","stretchright","stretchall" };
		s.set("anchor", names[static_cast<int>(preset)]);
		eraseKey(s, "anchorMin");
		eraseKey(s, "anchorMax");
		if(mods.alsoPivot)
		{
			s.set("pivot", fmtVec2(node.pivot.x, node.pivot.y));
		}
		// both variants re-derive the geometry, so write it back in the section's
		// own form (offsets, else the friendly anchoredPos/sizeDelta pair)
		writeGeom(s, node);
	}
	//---------------------------------------------------------
	void applyAnchorDrag(GuiLayoutSection& s, UiAnchorCorner corner,
		float fracX, float fracY, Orkige::LayoutRect const& parentRect,
		float layoutScale)
	{
		LayoutNode node = sectionLayoutNode(s);
		const Orkige::LayoutRect kept = resolveRect(parentRect, node, layoutScale);

		const float fx = clamp01(fracX);
		const float fy = clamp01(fracY);
		switch(corner)
		{
		case UiAnchorCorner::Min:
			node.anchorMin.x = fx; node.anchorMin.y = fy; break;
		case UiAnchorCorner::Max:
			node.anchorMax.x = fx; node.anchorMax.y = fy; break;
		case UiAnchorCorner::MinXMaxY:
			node.anchorMin.x = fx; node.anchorMax.y = fy; break;
		case UiAnchorCorner::MaxXMinY:
			node.anchorMax.x = fx; node.anchorMin.y = fy; break;
		}
		// keep min <= max on each axis so the anchor rect stays well-formed
		if(node.anchorMin.x > node.anchorMax.x)
		{
			std::swap(node.anchorMin.x, node.anchorMax.x);
		}
		if(node.anchorMin.y > node.anchorMax.y)
		{
			std::swap(node.anchorMin.y, node.anchorMax.y);
		}
		keepRectOffsets(node, parentRect, kept, layoutScale);

		// a hand-dragged anchor is custom: write the raw pair, drop the named key
		eraseKey(s, "anchor");
		s.set("anchorMin", fmtVec2(node.anchorMin.x, node.anchorMin.y));
		s.set("anchorMax", fmtVec2(node.anchorMax.x, node.anchorMax.y));
		writeGeom(s, node);
	}
	//---------------------------------------------------------
	void applyPivotDrag(GuiLayoutSection& s, float pivotX, float pivotY)
	{
		LayoutNode node = sectionLayoutNode(s);
		// the offsets (rect corners) do not move, so the on-screen rect is fixed;
		// only the pivot changes, which shifts what anchoredPosition means.
		node.pivot.x = clamp01(pivotX);
		node.pivot.y = clamp01(pivotY);
		s.set("pivot", fmtVec2(node.pivot.x, node.pivot.y));
		if(friendlyForm(s))
		{
			// re-derive anchoredPos from the fixed offsets + the new pivot
			const LayoutVec2 ap = node.anchoredPosition();
			s.set("anchoredPos", fmtVec2(ap.x, ap.y));
		}
	}
	//---------------------------------------------------------
	namespace
	{
		float rectLeft(UiRect const& r)   { return r.left; }
		float rectRight(UiRect const& r)  { return r.left + r.width; }
		float rectCX(UiRect const& r)     { return r.left + r.width * 0.5f; }
		float rectTop(UiRect const& r)    { return r.top; }
		float rectBottom(UiRect const& r) { return r.top + r.height; }
		float rectCY(UiRect const& r)     { return r.top + r.height * 0.5f; }
	}
	//---------------------------------------------------------
	std::vector<LayoutVec2> alignDeltas(std::vector<UiRect> const& rects,
		UiAlignOp op)
	{
		std::vector<LayoutVec2> out(rects.size());
		if(rects.empty())
		{
			return out;
		}
		UiRect const& key = rects[0];
		for(size_t each = 0; each < rects.size(); ++each)
		{
			UiRect const& r = rects[each];
			LayoutVec2 d;	// (0,0) for the key by construction below
			switch(op)
			{
			case UiAlignOp::Left:    d.x = rectLeft(key)   - rectLeft(r);   break;
			case UiAlignOp::HCenter: d.x = rectCX(key)     - rectCX(r);     break;
			case UiAlignOp::Right:   d.x = rectRight(key)  - rectRight(r);  break;
			case UiAlignOp::Top:     d.y = rectTop(key)    - rectTop(r);    break;
			case UiAlignOp::VCenter: d.y = rectCY(key)     - rectCY(r);     break;
			case UiAlignOp::Bottom:  d.y = rectBottom(key) - rectBottom(r); break;
			}
			out[each] = d;
		}
		return out;
	}
	//---------------------------------------------------------
	std::vector<LayoutVec2> distributeDeltas(std::vector<UiRect> const& rects,
		UiDistributeOp op)
	{
		std::vector<LayoutVec2> out(rects.size());
		if(rects.size() < 3)
		{
			return out;	// nothing to spread between fewer than three
		}
		const bool horizontal = op == UiDistributeOp::Horizontal;
		// order by the leading edge on the axis
		std::vector<size_t> order(rects.size());
		for(size_t each = 0; each < rects.size(); ++each) { order[each] = each; }
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
		{
			return horizontal ? rectLeft(rects[a]) < rectLeft(rects[b])
				: rectTop(rects[a]) < rectTop(rects[b]);
		});
		// the span between the two extremes, minus the summed extents, split evenly
		float extentSum = 0.0f;
		for(UiRect const& r : rects)
		{
			extentSum += horizontal ? r.width : r.height;
		}
		UiRect const& first = rects[order.front()];
		UiRect const& last = rects[order.back()];
		const float spanStart = horizontal ? rectLeft(first) : rectTop(first);
		const float spanEnd = horizontal ? rectRight(last) : rectBottom(last);
		const float gap = (spanEnd - spanStart - extentSum) /
			static_cast<float>(rects.size() - 1);
		float cursor = spanStart;
		for(size_t idx : order)
		{
			UiRect const& r = rects[idx];
			const float lead = horizontal ? rectLeft(r) : rectTop(r);
			const float delta = cursor - lead;
			if(horizontal) { out[idx].x = delta; } else { out[idx].y = delta; }
			cursor += (horizontal ? r.width : r.height) + gap;
		}
		return out;
	}
	//---------------------------------------------------------
	std::vector<String> widgetsInMarquee(std::vector<UiRect> const& rects,
		float x0, float y0, float x1, float y1)
	{
		const float lx = std::min(x0, x1);
		const float rx = std::max(x0, x1);
		const float ty = std::min(y0, y1);
		const float by = std::max(y0, y1);
		std::vector<String> out;
		for(UiRect const& r : rects)
		{
			// axis-aligned overlap (a touching edge counts as intersecting)
			if(rectRight(r) >= lx && rectLeft(r) <= rx &&
				rectBottom(r) >= ty && rectTop(r) <= by)
			{
				out.push_back(r.id);
			}
		}
		return out;
	}
	//---------------------------------------------------------
	std::vector<UiGuide> guideCandidates(std::vector<UiRect> const& others,
		UiRect const& parentRect, bool hasDesignCenter,
		float designCenterX, float designCenterY)
	{
		std::vector<UiGuide> out;
		auto vline = [&](float x){ out.push_back({ true, x }); };
		auto hline = [&](float y){ out.push_back({ false, y }); };
		for(UiRect const& r : others)
		{
			vline(rectLeft(r)); vline(rectCX(r)); vline(rectRight(r));
			hline(rectTop(r));  hline(rectCY(r)); hline(rectBottom(r));
		}
		vline(rectLeft(parentRect)); vline(rectCX(parentRect)); vline(rectRight(parentRect));
		hline(rectTop(parentRect));  hline(rectCY(parentRect)); hline(rectBottom(parentRect));
		if(hasDesignCenter)
		{
			vline(designCenterX);
			hline(designCenterY);
		}
		return out;
	}
	//---------------------------------------------------------
	UiSnap snapToGuides(UiRect const& moving, std::vector<UiGuide> const& candidates,
		float threshold)
	{
		UiSnap snap;
		// the moving rect's own snap points on each axis
		const float xs[3] = { rectLeft(moving), rectCX(moving), rectRight(moving) };
		const float ys[3] = { rectTop(moving),  rectCY(moving), rectBottom(moving) };
		float bestX = threshold;	// smallest |correction| that still snaps
		float bestY = threshold;
		for(UiGuide const& g : candidates)
		{
			if(g.vertical)
			{
				for(float x : xs)
				{
					const float d = g.pos - x;
					if(std::fabs(d) <= bestX)
					{
						bestX = std::fabs(d);
						snap.dx = d;
						snap.snappedX = true;
						snap.guideX = g.pos;
					}
				}
			}
			else
			{
				for(float y : ys)
				{
					const float d = g.pos - y;
					if(std::fabs(d) <= bestY)
					{
						bestY = std::fabs(d);
						snap.dy = d;
						snap.snappedY = true;
						snap.guideY = g.pos;
					}
				}
			}
		}
		return snap;
	}
	//---------------------------------------------------------
	String addDestinationParent(String const& selection, bool selectionIsLastCreated,
		String const& lastConfirmedParent)
	{
		// the sibling default: when the selection is the widget the last add created,
		// repeat the last add's destination instead of nesting under it
		return selectionIsLastCreated ? lastConfirmedParent : selection;
	}
	//---------------------------------------------------------
	UiAddDestinationChoices addDestinationChoices(String const& selection,
		bool selectionIsLastCreated, String const& lastConfirmedParent)
	{
		UiAddDestinationChoices out;
		// option: child of the current selection (only when something is selected)
		if(!selection.empty())
		{
			out.options.push_back(
				{ UiAddDestination::Kind::ChildOfSelected, selection });
		}
		// option: the sibling default's destination - the last add's OWN parent -
		// but only while the sibling rule is live (the selection IS the widget the
		// last add created) and that destination is a DISTINCT non-root container (a
		// root last-destination is covered by the Root option below)
		if(selectionIsLastCreated && !lastConfirmedParent.empty() &&
			lastConfirmedParent != selection)
		{
			out.options.push_back(
				{ UiAddDestination::Kind::LastDestination, lastConfirmedParent });
		}
		// option: at root - always available
		out.options.push_back({ UiAddDestination::Kind::Root, String() });

		// the DEFAULT is the sibling-aware pick, mapped to whichever option carries
		// that parent. The sibling rule chooses ONLY the default; every option stays
		// selectable so a deliberate child-of-the-newest add is possible.
		const String def = addDestinationParent(selection, selectionIsLastCreated,
			lastConfirmedParent);
		out.defaultIndex = 0;
		for(size_t each = 0; each < out.options.size(); ++each)
		{
			if(out.options[each].parent == def)
			{
				out.defaultIndex = static_cast<int>(each);
				break;
			}
		}
		return out;
	}
	//---------------------------------------------------------
	int sectionIndex(GuiLayoutDoc const& doc, String const& id)
	{
		for(size_t each = 0; each < doc.sections.size(); ++each)
		{
			if(doc.sections[each].id == id && !id.empty())
			{
				return static_cast<int>(each);
			}
		}
		return -1;
	}
	//---------------------------------------------------------
	namespace
	{
		//! a lower-case copy (self-contained, so the pure core carries no
		//! StringUtil dependency)
		String toLowerCopy(String const& in)
		{
			String out = in;
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
			return out;
		}
	}
	//---------------------------------------------------------
	std::vector<UiSpritePickEntry> spritePickerEntries(
		std::vector<String> const& atlasSprites, String const& filter)
	{
		std::vector<UiSpritePickEntry> out;
		// the clear entry always leads, regardless of the filter (it is an action,
		// not a searchable sprite)
		UiSpritePickEntry none;
		none.label = "(none)";
		none.isNone = true;
		out.push_back(none);

		const String needle = toLowerCopy(filter);
		bool anyMatch = false;
		for(String const& name : atlasSprites)
		{
			if(name.empty()) { continue; }
			if(!needle.empty() && toLowerCopy(name).find(needle) == String::npos)
			{
				continue;
			}
			UiSpritePickEntry e;
			e.value = name;
			e.label = name;
			out.push_back(e);
			anyMatch = true;
		}
		// a typed name the atlas does not carry stays selectable (the classic /
		// headless case, where no live view enumerates the atlas, AND a full name
		// typed that is simply not present). Only when NOTHING matched - a filter
		// with live search hits is a search, not a new name - and a sprite name is
		// one token, so a filter with whitespace never offers a free-text entry.
		if(!filter.empty() && !anyMatch &&
			filter.find_first_of(" \t") == String::npos)
		{
			UiSpritePickEntry custom;
			custom.value = filter;
			custom.label = String("use \"") + filter + "\"";
			custom.isCustom = true;
			out.push_back(custom);
		}
		return out;
	}
	//---------------------------------------------------------
	GuiLayoutSection paletteSection(GuiLayoutDoc const& doc,
		String const& type, String const& parentId)
	{
		String kind = type;
		std::transform(kind.begin(), kind.end(), kind.begin(),
			[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		bool known = false;
		for(UiWidgetKind const& each : uiWidgetKinds())
		{
			if(kind == each.type) { known = true; break; }
		}
		if(!known) { kind = "panel"; }

		// a stable unique id: "<kind>N" for the first free N
		GuiLayoutSection section;
		section.type = kind;
		int suffix = 1;
		String id;
		do
		{
			std::ostringstream name;
			name << kind << suffix;
			id = name.str();
			++suffix;
		}
		while(sectionIndex(doc, id) >= 0);
		section.id = id;

		if(!parentId.empty() && sectionIndex(doc, parentId) >= 0)
		{
			section.set("parent", parentId);
		}
		section.set("z", "10");
		// sane per-kind defaults: a modest anchored box near the top-left
		if(kind == "label" || kind == "textbox")
		{
			section.set("font", "9");
			section.set("text", kind == "label" ? "Label" : "Text");
			section.set("anchor", "topleft");
			section.set("anchoredPos", "20 20");
			// a positive default box: the caption defaults to centre alignment
			// (@see GuiFactory) and clips per-glyph to width(), but the clip is
			// SKIPPED when width == 0 (@see UiCaption::_redraw) - so a zero-size
			// default would let the caption spill outside its rect. This box
			// comfortably fits the short default text at any UI font.
			section.set("sizeDelta", "120 32");
		}
		else if(kind == "scrollview")
		{
			section.set("anchor", "topleft");
			section.set("offsets", "20 20 220 220");
		}
		else if(kind == "listview")
		{
			// a list is a scroll viewport with rows; the uniform row height is
			// what a later `virtualized = true` needs, so seed it here
			section.set("font", "9");
			section.set("anchor", "topleft");
			section.set("offsets", "20 20 220 220");
			section.set("itemHeight", "24");
		}
		else if(kind == "panel")
		{
			section.set("sprite", "panel");
			section.set("anchor", "topleft");
			section.set("offsets", "20 20 220 220");
		}
		else
		{
			// interactive widgets: a sprite face + a fixed anchored box
			section.set("font", "9");
			if(kind == "button" || kind == "checkbox" || kind == "dropdown")
			{
				section.set("sprite", kind == "checkbox" ? "checkbox" : "button");
			}
			section.set("text", "New");
			section.set("anchor", "topleft");
			section.set("anchoredPos", "20 20");
			section.set("sizeDelta", "160 44");
		}
		return section;
	}
	//---------------------------------------------------------
	std::vector<String> removeWidgetSubtree(GuiLayoutDoc& doc, String const& id)
	{
		std::vector<String> removed;
		if(sectionIndex(doc, id) < 0)
		{
			return removed;
		}
		// collect the id and every transitive child (parent = a removed id)
		removed.push_back(id);
		bool grew = true;
		while(grew)
		{
			grew = false;
			for(GuiLayoutSection const& s : doc.sections)
			{
				if(s.id.empty()) { continue; }
				if(std::find(removed.begin(), removed.end(), s.id) != removed.end())
				{
					continue;
				}
				String const* parent = s.find("parent");
				if(parent && std::find(removed.begin(), removed.end(), *parent)
					!= removed.end())
				{
					removed.push_back(s.id);
					grew = true;
				}
			}
		}
		// erase the collected sections
		std::vector<GuiLayoutSection> kept;
		kept.reserve(doc.sections.size());
		for(GuiLayoutSection& s : doc.sections)
		{
			if(std::find(removed.begin(), removed.end(), s.id) == removed.end() ||
				s.id.empty())
			{
				kept.push_back(std::move(s));
			}
		}
		doc.sections = std::move(kept);
		return removed;
	}
	//---------------------------------------------------------
	bool isValidWidgetName(GuiLayoutDoc const& doc, String const& id,
		String const& allowSelf, String& error)
	{
		if(id.empty())
		{
			error = "the name cannot be empty";
			return false;
		}
		for(char c : id)
		{
			if(std::isspace(static_cast<unsigned char>(c)))
			{
				error = "the name cannot contain spaces";
				return false;
			}
		}
		for(GuiLayoutSection const& s : doc.sections)
		{
			if(s.id.empty()) { continue; }
			if(s.id == id && s.id != allowSelf)
			{
				error = "a widget named '" + id + "' already exists";
				return false;
			}
		}
		error.clear();
		return true;
	}
	//---------------------------------------------------------
	bool canReparentWidget(GuiLayoutDoc const& doc, String const& childId,
		String const& newParentId)
	{
		if(childId.empty() || sectionIndex(doc, childId) < 0)
		{
			return false;	// no such child
		}
		if(newParentId.empty())
		{
			return true;	// reparent to root always well-formed for a real child
		}
		if(newParentId == childId)
		{
			return false;	// a widget cannot parent itself
		}
		if(sectionIndex(doc, newParentId) < 0)
		{
			return false;	// the target parent must exist
		}
		// walk the target parent's ancestor chain: if it passes through the child,
		// the child is an ancestor of the target and the move would form a cycle
		String cursor = newParentId;
		for(int guard = 0; guard < 4096 && !cursor.empty(); ++guard)
		{
			if(cursor == childId)
			{
				return false;
			}
			const int idx = sectionIndex(doc, cursor);
			if(idx < 0)
			{
				break;
			}
			String const* p = doc.sections[static_cast<size_t>(idx)].find("parent");
			cursor = p ? *p : String();
		}
		return true;
	}
	//---------------------------------------------------------
	bool reparentWidget(GuiLayoutDoc& doc, String const& childId,
		String const& newParentId, Orkige::LayoutRect const& oldParentRect,
		Orkige::LayoutRect const& newParentRect, float layoutScale, String& error)
	{
		if(!canReparentWidget(doc, childId, newParentId))
		{
			error = "cannot reparent '" + childId + "' under '" + newParentId +
				"' (a missing widget or a parent cycle)";
			return false;
		}
		const int idx = sectionIndex(doc, childId);
		GuiLayoutSection& s = doc.sections[static_cast<size_t>(idx)];

		// keep the on-screen rect fixed across the parent swap where the form allows
		if(geomMode(s) == UiGeomMode::Absolute)
		{
			// absolute position is design px from the parent origin; shift by the
			// parent-origin delta so the on-screen point is unchanged
			const float scale = layoutScale > 1e-6f ? layoutScale : 1.0f;
			float p[2] = { 0, 0 };
			if(String const* v = s.find("position")) { readFloats(*v, p, 2); }
			p[0] += (oldParentRect.x - newParentRect.x) / scale;
			p[1] += (oldParentRect.y - newParentRect.y) / scale;
			s.set("position", fmtVec2(p[0], p[1]));
		}
		else
		{
			LayoutNode node = sectionLayoutNode(s);
			const Orkige::LayoutRect kept =
				resolveRect(oldParentRect, node, layoutScale);
			keepRectOffsets(node, newParentRect, kept, layoutScale);
			writeGeom(s, node);
		}

		// finally set / clear the parent key
		if(newParentId.empty())
		{
			eraseKey(s, "parent");
		}
		else
		{
			s.set("parent", newParentId);
		}
		error.clear();
		return true;
	}
	//---------------------------------------------------------
	bool reorderSectionAdjacent(GuiLayoutDoc& doc, String const& movingId,
		String const& anchorId, bool after)
	{
		if(movingId.empty() || anchorId.empty() || movingId == anchorId)
		{
			return false;
		}
		const int from = sectionIndex(doc, movingId);
		if(from < 0 || sectionIndex(doc, anchorId) < 0)
		{
			return false;
		}
		// lift the moving section out, then re-find the anchor (its index may have
		// shifted by the removal) and insert just before / after it
		GuiLayoutSection moved = doc.sections[static_cast<size_t>(from)];
		doc.sections.erase(doc.sections.begin() + from);
		const int anchor = sectionIndex(doc, anchorId);
		size_t at = static_cast<size_t>(anchor) + (after ? 1u : 0u);
		if(at > doc.sections.size()) { at = doc.sections.size(); }
		doc.sections.insert(doc.sections.begin() + static_cast<long>(at),
			std::move(moved));
		return true;
	}
	//---------------------------------------------------------
	bool renameWidget(GuiLayoutDoc& doc, String const& oldId, String const& newId,
		String& error)
	{
		const int idx = sectionIndex(doc, oldId);
		if(idx < 0)
		{
			error = "no widget named '" + oldId + "'";
			return false;
		}
		if(newId == oldId)
		{
			error.clear();
			return true;	// a no-op rename succeeds
		}
		if(!isValidWidgetName(doc, newId, oldId, error))
		{
			return false;
		}
		doc.sections[static_cast<size_t>(idx)].id = newId;
		// re-home every child that named the old id as its parent
		for(GuiLayoutSection& s : doc.sections)
		{
			if(s.id.empty()) { continue; }
			if(String const* p = s.find("parent"))
			{
				if(*p == oldId) { s.set("parent", newId); }
			}
		}
		error.clear();
		return true;
	}
	//=========================================================
	//=== UiEditDoc ===========================================
	//=========================================================
	bool UiEditDoc::load(String const& text, String& error)
	{
		GuiLayoutDoc parsed;
		if(!GuiLayoutDoc::parse(text, parsed, error))
		{
			this->mDoc = GuiLayoutDoc();
			return false;
		}
		this->mDoc = std::move(parsed);
		this->mUndo.clear();
		this->mRedo.clear();
		this->mEditing = false;
		this->mPending.clear();
		this->mPendingKey.clear();
		this->mLastKey.clear();
		this->mSaved = this->text();
		return true;
	}
	//---------------------------------------------------------
	String UiEditDoc::text() const
	{
		return this->mDoc.serialize();
	}
	//---------------------------------------------------------
	void UiEditDoc::beginEdit()
	{
		this->beginCoalesced(String());
	}
	//---------------------------------------------------------
	void UiEditDoc::beginCoalesced(String const& key)
	{
		if(this->mEditing)
		{
			return;	// nested begin folds into the outer gesture
		}
		this->mEditing = true;
		this->mPending = this->text();
		this->mPendingKey = key;
	}
	//---------------------------------------------------------
	void UiEditDoc::commitEdit()
	{
		if(!this->mEditing)
		{
			return;
		}
		this->mEditing = false;
		if(this->text() != this->mPending)
		{
			// a keyed gesture merges into the previous commit when the key matches
			// and that commit is still the top of the undo stack (the nudge-burst
			// contract); everything else pushes a fresh step.
			const bool merge = !this->mPendingKey.empty() &&
				this->mPendingKey == this->mLastKey && !this->mUndo.empty();
			if(!merge)
			{
				this->mUndo.push_back(this->mPending);
			}
			this->mRedo.clear();
			this->mLastKey = this->mPendingKey;
		}
		this->mPending.clear();
		this->mPendingKey.clear();
	}
	//---------------------------------------------------------
	void UiEditDoc::undo()
	{
		if(this->mUndo.empty())
		{
			return;
		}
		const String current = this->text();
		const String prev = this->mUndo.back();
		this->mUndo.pop_back();
		this->mRedo.push_back(current);
		this->mLastKey.clear();	// a nudge after an undo starts a fresh burst
		String error;
		GuiLayoutDoc::parse(prev, this->mDoc, error);
	}
	//---------------------------------------------------------
	void UiEditDoc::redo()
	{
		if(this->mRedo.empty())
		{
			return;
		}
		const String current = this->text();
		const String next = this->mRedo.back();
		this->mRedo.pop_back();
		this->mUndo.push_back(current);
		this->mLastKey.clear();
		String error;
		GuiLayoutDoc::parse(next, this->mDoc, error);
	}
	//---------------------------------------------------------
	bool UiEditDoc::dirty() const
	{
		return this->text() != this->mSaved;
	}
	//---------------------------------------------------------
	void UiEditDoc::markSaved()
	{
		this->mSaved = this->text();
	}
	//=========================================================
	//=== the canvas transform (pure - shared by the panel + tests) ===
	//=========================================================
	UiRect mapSurfaceRectToScreen(UiCanvasPlacement const& c,
		UiRect const& surfaceRect)
	{
		const float sx = c.drawW / (c.surfaceW > 0.0f ? c.surfaceW : 1.0f);
		const float sy = c.drawH / (c.surfaceH > 0.0f ? c.surfaceH : 1.0f);
		UiRect out;
		out.id = surfaceRect.id;
		out.left = c.imageX + surfaceRect.left * sx;
		out.top = c.imageY + surfaceRect.top * sy;
		out.width = surfaceRect.width * sx;
		out.height = surfaceRect.height * sy;
		return out;
	}
	//---------------------------------------------------------
	UiRect adornmentBoundsScreen(UiCanvasPlacement const& c,
		std::vector<UiRect> const& selectionSurfaceRects, float handlePad)
	{
		if(selectionSurfaceRects.empty()) { return UiRect(); }
		float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
		bool first = true;
		for(UiRect const& r : selectionSurfaceRects)
		{
			const UiRect m = mapSurfaceRectToScreen(c, r);
			const float l = m.left - handlePad;
			const float t = m.top - handlePad;
			const float rr = m.left + m.width + handlePad;
			const float bb = m.top + m.height + handlePad;
			if(first)
			{
				minX = l; minY = t; maxX = rr; maxY = bb; first = false;
			}
			else
			{
				minX = std::min(minX, l); minY = std::min(minY, t);
				maxX = std::max(maxX, rr); maxY = std::max(maxY, bb);
			}
		}
		UiRect out;
		out.left = minX; out.top = minY;
		out.width = maxX - minX; out.height = maxY - minY;
		return out;
	}
}
