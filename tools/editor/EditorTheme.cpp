/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	EditorTheme.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorTheme - macOS-inspired ImGui style, light and dark (see header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorTheme.h"
#include "EditorTerminalSession.h"
#include "IconsFontAwesome6.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Orkige
{
	namespace
	{
		constexpr ImVec4 TRANSPARENT_ = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

		//! sRGB hex -> ImVec4 (straight conversion, no gamma games - matches
		//! how the reference values are picked from macOS screenshots)
		constexpr ImVec4 rgba(unsigned int rgb, float alpha = 1.0f)
		{
			return ImVec4(
				((rgb >> 16) & 0xFF) / 255.0f,
				((rgb >> 8) & 0xFF) / 255.0f,
				(rgb & 0xFF) / 255.0f,
				alpha);
		}

		//! One named surface/accent set. Both variants fill the SAME fields
		//! (each after its macOS counterpart) so the colour->ImGuiCol mapping in
		//! applyEditorTheme stays single-sourced - only these values change.
		struct EditorPalette
		{
			// window/panel surfaces
			ImVec4 windowBg;		//!< panel body
			ImVec4 chromeBg;		//!< non-tabbed strips (toolbar) - darker chrome
			ImVec4 dockspaceBg;		//!< empty dock area (recessed vs panels)
			ImVec4 popupBg;			//!< menus/popups (elevated)
			ImVec4 titleBg;			//!< flat title/tab strip
			ImVec4 menubarBg;
			// controls (buttons, combos) - lighter than the panel
			// in dark, whiter/raised than the panel in light
			ImVec4 controlBg;
			ImVec4 controlHover;
			// input fields (text/number wells, checkboxes) - RECESSED below the
			// panel so a field reads as a well: darker than the panel in dark,
			// off-white grey in light. Hover/active stay calm (no bright flash).
			ImVec4 fieldBg;
			ImVec4 fieldHover;
			ImVec4 fieldActive;
			ImVec4 fieldBorder;		//!< the field's 1px recess hairline
			// component header bars in the Inspector - a slightly distinct
			// surface from the panel body so each component reads as a titled bar
			ImVec4 headerBar;
			ImVec4 headerBarHover;
			// recessed region ground for browsing areas (asset folder tree, tile
			// grid) - a step darker than the panel so the area reads as distinct
			// from the header strip above it
			ImVec4 regionBg;
			// the macOS accent blue and its derivatives
			ImVec4 accent;
			ImVec4 accentHover;
			ImVec4 accentSelection;	//!< list selection fill (translucent)
			ImVec4 accentSoft;		//!< hover/soft highlight (translucent)
			// text
			ImVec4 textPrimary;
			ImVec4 textSecondary;
			// hairlines (translucent so they read on any surface)
			ImVec4 separator;
			ImVec4 border;
			// scrollbars
			ImVec4 scrollGrab;
			ImVec4 scrollGrabHover;
			// docked tabs styled like a macOS segmented control: resting
			// segments blend into the strip, the selected one is a raised pill -
			// no blue, no overline (the "flatter" part)
			ImVec4 tabResting;
			ImVec4 tabHover;
			ImVec4 tabSelected;
			ImVec4 tabDimmed;
			// zebra table stripe (translucent overlay on the row)
			ImVec4 rowStripe;
			// dimming scrims (modal/nav) - always a black wash
			ImVec4 navDim;
			ImVec4 modalDim;
			// console log lines, tuned to stay legible on this variant's panel
			ImVec4 warningText;
			ImVec4 errorText;
		};

		//! macOS dark mode: dark surfaces, controls a step lighter than panels.
		constexpr EditorPalette DARK_PALETTE = {
			// the panel body carries the brighter surface shade; the SELECTED
			// tab matches it exactly (one connected surface at the brighter
			// level - resting tabs recede below it)
			/*windowBg*/        rgba(0x3a3a3c),
			/*chromeBg*/        rgba(0x232323),
			/*dockspaceBg*/     rgba(0x1a1a1a),
			/*popupBg*/         rgba(0x2a2a2c, 0.98f),
			/*titleBg*/         rgba(0x2d2d2d),
			/*menubarBg*/       rgba(0x282828),
			/*controlBg*/       rgba(0x3a3a3c),
			/*controlHover*/    rgba(0x48484a),
			// fields sit clearly BELOW the panel body (0x3a3a3c) and the
			// selected tab so an input never blends into either; hover/active
			// ladder UP from the recessed base so focus still reads, staying
			// under the panel shade. A near-black 1px border draws the recess.
			/*fieldBg*/         rgba(0x28282a),
			/*fieldHover*/      rgba(0x2f2f32),
			/*fieldActive*/     rgba(0x353539),
			/*fieldBorder*/     rgba(0x1f1f1f),
			/*headerBar*/       rgba(0x323234),
			/*headerBarHover*/  rgba(0x2a2a2c),	// DARKER under the cursor
			/*regionBg*/        rgba(0x2f2f31),	// between fieldBg and windowBg
			/*accent*/          rgba(0x0a84ff),
			/*accentHover*/     rgba(0x409cff),
			/*accentSelection*/ rgba(0x0a84ff, 0.55f),
			/*accentSoft*/      rgba(0x0a84ff, 0.30f),
			/*textPrimary*/     rgba(0xe5e5e7),
			/*textSecondary*/   rgba(0x98989d),
			/*separator*/       rgba(0xffffff, 0.10f),
			/*border*/          rgba(0xffffff, 0.08f),
			/*scrollGrab*/      rgba(0x5a5a5e, 0.80f),
			/*scrollGrabHover*/ rgba(0x6e6e73, 0.90f),
			/*tabResting*/      rgba(0x2d2d2d),
			/*tabHover*/        rgba(0x434345),
			/*tabSelected*/     rgba(0x3a3a3c),
			/*tabDimmed*/       rgba(0x262626),
			/*rowStripe*/       rgba(0xffffff, 0.03f),
			/*navDim*/          rgba(0x000000, 0.35f),
			/*modalDim*/        rgba(0x000000, 0.45f),
			/*warningText*/     rgba(0xf2cc40),
			/*errorText*/       rgba(0xf25a4d),
		};

		//! macOS light mode: light-grey window, white raised controls, near-
		//! black text; the accent stays the system blue. The dark/light
		//! relationships are mirrored (controls step AWAY from the panel toward
		//! the extreme; the dockspace gap is recessed = darker than panels; the
		//! selected tab is a raised = brighter pill), so future tweaks to one
		//! variant have an obvious counterpart in the other.
		constexpr EditorPalette LIGHT_PALETTE = {
			// panel body at the brighter level; selected tab matches (below)
			/*windowBg*/        rgba(0xffffff),
			/*chromeBg*/        rgba(0xececec),
			/*dockspaceBg*/     rgba(0xd2d2d2),
			/*popupBg*/         rgba(0xffffff, 0.98f),
			/*titleBg*/         rgba(0xe0e0e0),
			/*menubarBg*/       rgba(0xe6e6e6),
			/*controlBg*/       rgba(0xffffff),
			/*controlHover*/    rgba(0xf1f1f3),
			// a recessed grey against the white panel (0xffffff); hover/active
			// press DOWN a touch so focus reads without going dark. A light-grey
			// 1px border draws the recess.
			/*fieldBg*/         rgba(0xe8e8ea),
			/*fieldHover*/      rgba(0xe0e0e3),
			/*fieldActive*/     rgba(0xd8d8dc),
			/*fieldBorder*/     rgba(0xc8c8ca),
			/*headerBar*/       rgba(0xededf0),
			/*headerBarHover*/  rgba(0xe0e0e5),	// DARKER under the cursor
			/*regionBg*/        rgba(0xf1f1f3),	// between fieldBg and windowBg
			/*accent*/          rgba(0x007aff),
			/*accentHover*/     rgba(0x2f95ff),
			/*accentSelection*/ rgba(0x007aff, 0.28f),
			/*accentSoft*/      rgba(0x007aff, 0.16f),
			/*textPrimary*/     rgba(0x1d1d1f),
			/*textSecondary*/   rgba(0x76767b),
			/*separator*/       rgba(0x000000, 0.12f),
			/*border*/          rgba(0x000000, 0.14f),
			/*scrollGrab*/      rgba(0x000000, 0.28f),
			/*scrollGrabHover*/ rgba(0x000000, 0.42f),
			/*tabResting*/      rgba(0xdedede),
			/*tabHover*/        rgba(0xe9e9e9),
			/*tabSelected*/     rgba(0xffffff),
			/*tabDimmed*/       rgba(0xd8d8d8),
			/*rowStripe*/       rgba(0x000000, 0.04f),
			/*navDim*/          rgba(0x000000, 0.20f),
			/*modalDim*/        rgba(0x000000, 0.30f),
			/*warningText*/     rgba(0x9a6a00),
			/*errorText*/       rgba(0xc4372b),
		};

		//! the live variant (what applyEditorTheme last painted); the out-of-
		//! style colour accessors branch on it. Dark until the first apply.
		EditorThemeVariant gActiveVariant = EditorThemeVariant::Dark;

		const EditorPalette& activePalette()
		{
			return gActiveVariant == EditorThemeVariant::Light
				? LIGHT_PALETTE : DARK_PALETTE;
		}

		//! system UI font candidates, best first: the macOS system font
		//! (San Francisco, present on every macOS install), the Windows UI
		//! font and common Linux distro fonts; loaded at runtime, never
		//! shipped with the project.
		//! No match -> nullptr -> ImGui's embedded default font.
		const char* const SYSTEM_FONT_PATHS[] = {
			"/System/Library/Fonts/SFNS.ttf",						// macOS
			"C:/Windows/Fonts/segoeui.ttf",							// Windows
			"C:/Windows/Fonts/tahoma.ttf",							// Windows fallback
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",		// Debian/Ubuntu
			"/usr/share/fonts/TTF/DejaVuSans.ttf",					// Arch/Fedora-ish
			"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",	// Noto fallback
		};

		//! monospace font candidates for the Script panel's code editor (system
		//! fonts loaded at runtime, never shipped); no match -> nullptr -> the
		//! panel renders in the UI font
		const char* const SYSTEM_MONO_FONT_PATHS[] = {
			"/System/Library/Fonts/SFNSMono.ttf",					// macOS
			"/System/Library/Fonts/Menlo.ttc",						// macOS fallback
			"C:/Windows/Fonts/consola.ttf",							// Windows
			"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",	// Debian/Ubuntu
			"/usr/share/fonts/TTF/DejaVuSansMono.ttf",				// Arch/Fedora-ish
		};

		//! the glyph blocks the terminal/Script mono atlas bakes beyond the
		//! default Basic-Latin + Latin-1 range, so common TUI output renders
		//! instead of the atlas's '?' fallback. The editor uploads ONE static
		//! atlas, so on-demand baking is unavailable and every block a terminal
		//! app might emit has to be requested here. Pairs are {first, last},
		//! terminated by 0. Braille (256 glyphs) drives the cost - a few extra
		//! KB of atlas, negligible.
		const ImWchar TERMINAL_GLYPH_RANGES[] = {
			0x0020, 0x00ff,		// Basic Latin + Latin-1 Supplement (the default)
			0x2000, 0x206f,		// General Punctuation (ellipsis, dashes, quotes)
			0x2190, 0x21ff,		// Arrows
			0x2500, 0x257f,		// Box Drawing
			0x2580, 0x259f,		// Block Elements (shading/progress bars)
			0x25a0, 0x25ff,		// Geometric Shapes (bullets, triangles)
			0x2600, 0x26ff,		// Miscellaneous Symbols
			0x2700, 0x27bf,		// Dingbats (sparkles/asterisk stars)
			0x2800, 0x28ff,		// Braille Patterns (spinner frames)
			0,
		};

		//! the standalone monospace font (Script panel + terminal); null until a
		//! successful loadMacSystemMonoFont
		ImFont* gMonoFont = nullptr;
		ImFont* gSmallFont = nullptr;
		float gSmallFontSize = 0.0f;

		//! the standalone larger icon font (grid-tile drawing); null until a
		//! successful loadEditorIconFont, which is also the "icons available" flag
		ImFont* gIconFontLarge = nullptr;

		//! the size (in font-size units) the standalone icon font was rasterised
		//! at, i.e. its native 1:1 draw size; grid draws clamp to it so an icon
		//! only ever downscales from the crisp atlas, never upscales past it
		float gIconFontLargePixels = 0.0f;

		//! the pixel size the standalone icon font is rasterised at: large enough
		//! that even the biggest grid tile (drawn at tile size) downscales from a
		//! crisp atlas rather than upscaling a small one. Only a handful of glyphs
		//! are rasterised, so the larger atlas footprint is negligible.
		constexpr float ICON_FONT_ATLAS_PIXELS = 128.0f;

		//! the FA6 codepoints the editor actually draws (asset kinds + folders) -
		//! a tight glyph range so the atlas rasterises ~a dozen glyphs, not the
		//! whole Font Awesome block. Pairs are {first, last}; see
		//! IconsFontAwesome6.h for the ICON_FA_* names behind each value.
		//! the file-format fallback icons FileFormatIcon.cpp hands back (asset
		//! browser rows/tiles with no real thumbnail, script-editor document
		//! tabs) add a further handful of glyphs below - keep the two tables
		//! in sync (FileFormatIconTests asserts every glyph it returns falls
		//! in one of these ranges).
		const ImWchar ICON_GLYPH_RANGES[] = {
			0xe13a, 0xe13a,		// code-compare (git change-markers toggle)
			0xf001, 0xf001,		// music (audio)
			0xf021, 0xf021,		// arrows-rotate (Source Control refresh)
			0xf068, 0xf068,		// minus (Source Control unstage / Stage All)
			0xf0ee, 0xf0ee,		// cloud-arrow-up (Source Control push / publish)
			0xf126, 0xf126,		// code-branch (Source Control panel + branch)
			0xf386, 0xf386,		// code-commit (Source Control commit)
			0xf008, 0xf008,		// film (scene)
			0xf00a, 0xf00a,		// table-cells (.ogui file icon)
			0xf02b, 0xf02b,		// tag (.orkmeta file icon)
			0xf031, 0xf031,		// font (.ttf file icon)
			0xf03d, 0xf03e,		// video (camera-owner glyph) / image (texture)
			0xf047, 0xf047,		// arrows-up-down-left-right (Translate tool)
			0xf04b, 0xf04b,		// play (debug Continue)
			0xf0cb, 0xf0cb,		// list-ol (.olevels file icon)
			0xf0c5, 0xf0c5,		// copy (Terminal MCP-connect copy button)
			0xf11b, 0xf11b,		// gamepad (.oactions file icon)
			0xf120, 0xf120,		// terminal (Terminal panel/tab, plain-shell glyph)
			0xf1ab, 0xf1ab,		// language (.xlf file icon)
			0xf28b, 0xf28b,		// circle-pause (debug Break on Next Statement)
			0xf2ed, 0xf2ed,		// trash-can (delete widget - UI Editor tree)
			0xf304, 0xf304,		// pen (rename widget - UI Editor action row)
			0xf061, 0xf063,		// arrow-right/up/down (debug Step Over/Out/In)
			0xf06e, 0xf06e,		// eye (Scene Display dropdown)
			0xf07b, 0xf07c,		// folder / folder-open
			0xf0b0, 0xf0b0,		// filter (the type-filter funnel button)
			0xf15b, 0xf15c,		// file (unknown) / file-lines (.json/.xml/.md icon)
			0xf185, 0xf185,		// sun (atmosphere-owner glyph)
			0xf188, 0xf188,		// bug (the Debug panel)
			0xf1b2, 0xf1b2,		// cube (mesh)
			0xf1c9, 0xf1c9,		// file-code (script)
			0xf1fc, 0xf1fc,		// paintbrush (Paint tool)
			0xf245, 0xf245,		// arrow-pointer (Select tool)
			0xf24d, 0xf24d,		// clone (prefab)
			0xf256, 0xf256,		// hand (Hand/pan tool)
			0xf2d0, 0xf2d0,		// window-maximize (.oui file icon)
			0xf2f1, 0xf2f1,		// rotate (Rotate tool)
			0xf424, 0xf424,		// up-right-and-down-left-from-center (Scale tool)
			0xf53f, 0xf53f,		// palette (material)
			0xf542, 0xf542,		// diagram-project (.orkproj file icon)
			0xf544, 0xf544,		// robot (Terminal agent-session glyph + MCP hint)
			0xf5fd, 0xf5fd,		// layer-group (.olayers file icon)
			0xf61f, 0xf61f,		// shapes (vector shape)
			0xf70c, 0xf70c,		// person-running (.oanim file icon)
			0,
		};
	}

	//---------------------------------------------------------
	EditorThemeVariant resolveEditorTheme(EditorThemeMode mode)
	{
		switch (mode)
		{
		case EditorThemeMode::Dark:		return EditorThemeVariant::Dark;
		case EditorThemeMode::Light:	return EditorThemeVariant::Light;
		case EditorThemeMode::System:
		default:
			// SDL reports the OS appearance; an unknown one keeps the editor's
			// historical dark default
			return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT
				? EditorThemeVariant::Light : EditorThemeVariant::Dark;
		}
	}
	//---------------------------------------------------------
	EditorThemeVariant currentEditorThemeVariant()
	{
		return gActiveVariant;
	}
	//---------------------------------------------------------
	ImVec4 editorDockspaceBackground()
	{
		return activePalette().dockspaceBg;
	}
	//---------------------------------------------------------
	ImVec4 editorChromeBackground()
	{
		return activePalette().chromeBg;
	}
	//---------------------------------------------------------
	ImVec4 editorComponentHeaderColor()
	{
		return activePalette().headerBar;
	}
	//---------------------------------------------------------
	ImVec4 editorComponentHeaderHoverColor()
	{
		return activePalette().headerBarHover;
	}
	//---------------------------------------------------------
	ImVec4 editorFieldBorderColor()
	{
		return activePalette().fieldBorder;
	}
	//---------------------------------------------------------
	void pushPropertyGridStyle()
	{
		// a recessed 1px field border makes each input read as a well; the table
		// itself draws no borders. The dense grid: tighter row spacing, shorter
		// inputs, edgier corners. Keep in lockstep with popPropertyGridStyle().
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
			ImVec2(ImGui::GetStyle().CellPadding.x, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Border, editorFieldBorderColor());
	}
	//---------------------------------------------------------
	void popPropertyGridStyle()
	{
		ImGui::PopStyleColor();		// field border
		ImGui::PopStyleVar(4);		// FrameBorderSize + density vars
	}
	//---------------------------------------------------------
	void pushInspectorButtonStyle()
	{
		// the darker header-bar shade on all three button states so a button
		// stands off a panel body whose control background equals the panel.
		// Keep in lockstep with popInspectorButtonStyle().
		ImGui::PushStyleColor(ImGuiCol_Button, editorComponentHeaderColor());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			editorComponentHeaderHoverColor());
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			editorComponentHeaderHoverColor());
	}
	//---------------------------------------------------------
	void popInspectorButtonStyle()
	{
		ImGui::PopStyleColor(3);	// Button + Hovered + Active
	}
	//---------------------------------------------------------
	ImVec4 editorRegionBackground()
	{
		return activePalette().regionBg;
	}
	//---------------------------------------------------------
	ImVec4 editorWarningTextColor()
	{
		return activePalette().warningText;
	}
	//---------------------------------------------------------
	ImVec4 editorErrorTextColor()
	{
		return activePalette().errorText;
	}
	//---------------------------------------------------------
	ImVec4 editorAccentColor()
	{
		return activePalette().accent;
	}
	//---------------------------------------------------------
	void applyEditorTheme(ImGuiStyle& style, EditorThemeVariant variant,
		float contentScale)
	{
		// tooltips behave like OS hints: appear only after the cursor has
		// RESTED on the item, and hide again the moment it moves
		{
			ImGuiStyle& hintStyle = ImGui::GetStyle();
			hintStyle.HoverStationaryDelay = 0.35f;
			hintStyle.HoverDelayShort = 0.60f;
			hintStyle.HoverDelayNormal = 0.80f;
			hintStyle.HoverFlagsForTooltipMouse =
				ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_Stationary |
				ImGuiHoveredFlags_AllowWhenDisabled;
		}

		gActiveVariant = variant;
		const EditorPalette& p = activePalette();

		style = ImGuiStyle(); // start from stock metrics, then restyle

		// --- metrics: generous rounding, comfortable spacing ---
		style.WindowRounding = 8.0f;
		style.ChildRounding = 6.0f;
		style.FrameRounding = 5.0f;
		style.PopupRounding = 8.0f;
		style.ScrollbarRounding = 12.0f;
		style.GrabRounding = 5.0f;
		style.TabRounding = 6.0f;
		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.TabBarBorderSize = 0.0f;
		style.TabBarOverlineSize = 0.0f;	// no accent overline - flat tabs
		style.WindowPadding = ImVec2(10.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.CellPadding = ImVec2(6.0f, 4.0f);
		style.IndentSpacing = 18.0f;
		style.ScrollbarSize = 12.0f;
		style.GrabMinSize = 10.0f;
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);	// centered, like macOS
		style.SeparatorTextBorderSize = 1.0f;

		// --- palette (mapping shared by both variants; p supplies the values) ---
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = p.textPrimary;
		colors[ImGuiCol_TextDisabled] = p.textSecondary;
		colors[ImGuiCol_WindowBg] = p.windowBg;
		colors[ImGuiCol_ChildBg] = TRANSPARENT_;
		colors[ImGuiCol_PopupBg] = p.popupBg;
		colors[ImGuiCol_Border] = p.border;
		colors[ImGuiCol_BorderShadow] = TRANSPARENT_;
		// input fields read as recessed wells (darker than the panel), so text
		// inputs / number drags / combos / checkboxes sit BELOW the surface
		colors[ImGuiCol_FrameBg] = p.fieldBg;
		colors[ImGuiCol_FrameBgHovered] = p.fieldHover;
		colors[ImGuiCol_FrameBgActive] = p.fieldActive;
		// flat title/tab strips (docked panels mostly show tabs, not titles)
		colors[ImGuiCol_TitleBg] = p.titleBg;
		colors[ImGuiCol_TitleBgActive] = p.titleBg;
		colors[ImGuiCol_TitleBgCollapsed] = p.titleBg;
		colors[ImGuiCol_MenuBarBg] = p.menubarBg;
		colors[ImGuiCol_ScrollbarBg] = TRANSPARENT_;
		colors[ImGuiCol_ScrollbarGrab] = p.scrollGrab;
		colors[ImGuiCol_ScrollbarGrabHovered] = p.scrollGrabHover;
		colors[ImGuiCol_ScrollbarGrabActive] = p.scrollGrabHover;
		// the checkbox/radio tick reads as a TEXT mark in a recessed field well
		// (matching the text-input look), NOT an accent-blue fill - the accent
		// stays for genuinely accent-y things (list selection, focus, sliders).
		// A CHECKED box keeps the same recessed field ground as an unchecked one
		// (this ImGui fills a checked box with CheckboxSelectedBg, accent-blue by
		// default) - the text-coloured tick is the on/off cue.
		colors[ImGuiCol_CheckMark] = p.textPrimary;
		colors[ImGuiCol_CheckboxSelectedBg] = p.fieldBg;
		colors[ImGuiCol_SliderGrab] = p.accent;
		colors[ImGuiCol_SliderGrabActive] = p.accentHover;
		colors[ImGuiCol_Button] = p.controlBg;
		colors[ImGuiCol_ButtonHovered] = p.controlHover;
		colors[ImGuiCol_ButtonActive] = p.accent;
		// Header drives Selectable/TreeNode selection - accent, like macOS lists
		colors[ImGuiCol_Header] = p.accentSelection;
		colors[ImGuiCol_HeaderHovered] = p.accentSoft;
		colors[ImGuiCol_HeaderActive] = p.accentSelection;
		colors[ImGuiCol_Separator] = p.separator;
		colors[ImGuiCol_SeparatorHovered] = p.accentSoft;
		colors[ImGuiCol_SeparatorActive] = p.accent;
		colors[ImGuiCol_ResizeGrip] = TRANSPARENT_;
		colors[ImGuiCol_ResizeGripHovered] = p.accentSoft;
		colors[ImGuiCol_ResizeGripActive] = p.accent;
		colors[ImGuiCol_Tab] = p.tabResting;
		colors[ImGuiCol_TabHovered] = p.tabHover;
		colors[ImGuiCol_TabSelected] = p.tabSelected;
		colors[ImGuiCol_TabSelectedOverline] = TRANSPARENT_;
		colors[ImGuiCol_TabDimmed] = p.tabDimmed;
		colors[ImGuiCol_TabDimmedSelected] = p.tabSelected;
		colors[ImGuiCol_TabDimmedSelectedOverline] = TRANSPARENT_;
		colors[ImGuiCol_DockingPreview] = p.accentSoft;
		colors[ImGuiCol_DockingEmptyBg] = p.dockspaceBg;
		colors[ImGuiCol_PlotLines] = p.textSecondary;
		colors[ImGuiCol_PlotLinesHovered] = p.accentHover;
		colors[ImGuiCol_PlotHistogram] = p.accent;
		colors[ImGuiCol_PlotHistogramHovered] = p.accentHover;
		colors[ImGuiCol_TableHeaderBg] = p.titleBg;
		colors[ImGuiCol_TableBorderStrong] = p.separator;
		colors[ImGuiCol_TableBorderLight] = p.border;
		colors[ImGuiCol_TableRowBg] = TRANSPARENT_;
		colors[ImGuiCol_TableRowBgAlt] = p.rowStripe;
		colors[ImGuiCol_TextSelectedBg] = p.accentSoft;
		colors[ImGuiCol_DragDropTarget] = p.accent;
		colors[ImGuiCol_NavCursor] = p.accent;
		colors[ImGuiCol_NavWindowingHighlight] = p.accentSoft;
		colors[ImGuiCol_NavWindowingDimBg] = p.navDim;
		colors[ImGuiCol_ModalWindowDimBg] = p.modalDim;

		if (contentScale > 1.0f)
		{
			style.ScaleAllSizes(contentScale);
		}
	}
	//---------------------------------------------------------
	ImFont* loadMacSystemFont(ImGuiIO& io, float sizePoints, float contentScale)
	{
		for (const char* fontPath : SYSTEM_FONT_PATHS)
		{
			std::error_code ignored;
			if (!std::filesystem::exists(fontPath, ignored))
			{
				continue;
			}
			// load at pixel size (points * scale) so retina surfaces get a
			// crisp atlas instead of an upscaled one
			ImFontConfig config;
			config.SizePixels = 0.0f;
			return io.Fonts->AddFontFromFileTTF(fontPath,
				sizePoints * contentScale, &config);
		}
		return nullptr; // no system font found - keep the default font
	}
	//---------------------------------------------------------
	ImFont* loadMacSystemSmallFont(ImGuiIO& io, float sizePoints,
		float contentScale)
	{
		gSmallFont = nullptr;
		gSmallFontSize = 0.0f;
		for (const char* fontPath : SYSTEM_FONT_PATHS)
		{
			std::error_code ignored;
			if (!std::filesystem::exists(fontPath, ignored))
			{
				continue;
			}
			ImFontConfig config;
			config.SizePixels = 0.0f;
			gSmallFont = io.Fonts->AddFontFromFileTTF(fontPath,
				sizePoints * contentScale, &config);
			if (gSmallFont != nullptr)
			{
				gSmallFontSize = sizePoints * contentScale;
				return gSmallFont;
			}
		}
		return nullptr;
	}
	//---------------------------------------------------------
	ImFont* editorSmallFont()
	{
		return gSmallFont;
	}
	//---------------------------------------------------------
	float editorSmallFontSize()
	{
		return gSmallFontSize;
	}
	//---------------------------------------------------------
	ImFont* loadMacSystemMonoFont(ImGuiIO& io, float sizePoints,
		float contentScale, const char* symbolsFontPath)
	{
		gMonoFont = nullptr;
		const float pixelSize = sizePoints * contentScale;
		for (const char* fontPath : SYSTEM_MONO_FONT_PATHS)
		{
			std::error_code ignored;
			if (!std::filesystem::exists(fontPath, ignored))
			{
				continue;
			}
			ImFontConfig config;
			config.SizePixels = 0.0f;
			// bake the TUI blocks (box drawing, braille, arrows, ...) the mono
			// font covers - not just default Latin - so a terminal's box UI and
			// spinners render instead of '?'. The ranges pointer must persist
			// until the atlas is built; TERMINAL_GLYPH_RANGES is static, so it
			// does.
			gMonoFont = io.Fonts->AddFontFromFileTTF(fontPath, pixelSize,
				&config, TERMINAL_GLYPH_RANGES);
			if (gMonoFont != nullptr)
			{
				break;
			}
		}
		if (gMonoFont == nullptr)
		{
			return nullptr; // no mono font found - the panel keeps the UI font
		}
		// merge a symbols fallback for the blocks the system mono font lacks
		// (braille above all - no macOS mono ships it). Merge glyphs never
		// override the primary's, so the box/block art stays cell-crisp and the
		// fallback only fills genuine holes. Codepoints NEITHER font carries
		// (a handful of emoji-tier dingbats) stay unbaked; the terminal draws
		// them as blank rather than '?'.
		if (symbolsFontPath != nullptr)
		{
			std::error_code ignored;
			if (std::filesystem::exists(symbolsFontPath, ignored))
			{
				ImFontConfig mergeConfig;
				mergeConfig.MergeMode = true;
				mergeConfig.SizePixels = 0.0f;
				io.Fonts->AddFontFromFileTTF(symbolsFontPath, pixelSize,
					&mergeConfig, TERMINAL_GLYPH_RANGES);
			}
		}
		return gMonoFont;
	}
	//---------------------------------------------------------
	ImFont* editorMonoFont()
	{
		return gMonoFont;
	}
	//---------------------------------------------------------
	void loadEditorIconFont(ImGuiIO& io, const char* fontPath, float sizePoints,
		float contentScale)
	{
		gIconFontLarge = nullptr;
		if (!fontPath)
		{
			return;
		}
		std::error_code ignored;
		if (!std::filesystem::exists(fontPath, ignored))
		{
			return; // no icon font - callers fall back to drawn glyph icons
		}
		// MergeMode needs a base font to merge into; if none was loaded (no
		// system font found) fall back to ImGui's built-in so inline icons still
		// have a host font. The fallback must carry an EXPLICIT pixel size: the
		// icon merge below passes one, and merging an explicit-size font into an
		// implicit-size destination is an ImGui assertion (on a platform whose
		// asserts open a dialog, that hang eats a headless test's whole timeout).
		if (io.Fonts->Fonts.empty())
		{
			ImFontConfig defaultConfig;
			defaultConfig.SizePixels = sizePoints * contentScale;
			io.Fonts->AddFontDefault(&defaultConfig);
		}
		// (1) merge the icons into the base UI font so they render inline with
		//     text (list rows, labelled buttons) at text size
		{
			ImFontConfig config;
			config.MergeMode = true;
			config.PixelSnapH = true;
			// keep icons from stretching the line advance past the text
			config.GlyphMinAdvanceX = sizePoints * contentScale;
			if (!io.Fonts->AddFontFromFileTTF(fontPath, sizePoints * contentScale,
				&config, ICON_GLYPH_RANGES))
			{
				return; // parse failure - leave the atlas as the base font only
			}
		}
		// (2) a standalone, larger copy the asset browser draws at grid-tile size
		{
			ImFontConfig config;
			config.OversampleH = 2;
			config.OversampleV = 2;
			const float rasterPixels = ICON_FONT_ATLAS_PIXELS * contentScale;
			gIconFontLarge = io.Fonts->AddFontFromFileTTF(fontPath,
				rasterPixels, &config, ICON_GLYPH_RANGES);
			gIconFontLargePixels = gIconFontLarge ? rasterPixels : 0.0f;
		}
	}
	//---------------------------------------------------------
	ImFont* editorIconFont()
	{
		return gIconFontLarge;
	}
	//---------------------------------------------------------
	float editorIconFontRasterPixels()
	{
		return gIconFontLargePixels;
	}
	//---------------------------------------------------------
	void bakeTerminalAgentBadges(ImGuiIO& io, float uiFontPixels)
	{
		if (io.Fonts->Fonts.empty())
		{
			return; // no base UI font - nothing to attach the badge glyphs to
		}
		ImFontAtlas* atlas = io.Fonts;
		ImFont* uiFont = atlas->Fonts[0];	// the base UI font (dock-tab titles)

		// a square badge cell sized to the UI glyph metrics so an agent tab lines
		// up with the icon-font tabs; a small negative Y offset sits it near cap
		// height beside the text.
		const int cell = std::max(8, static_cast<int>(uiFontPixels + 0.5f));
		const float advance = uiFontPixels * 1.05f;
		const float offsetY = -uiFontPixels * 0.12f;

		// (1) reserve one custom rect per recognised agent BEFORE the atlas builds
		ImFontAtlasRectId rectIds[static_cast<int>(
			OrkigeEditor::TerminalAgent::Count)];
		OrkigeEditor::TerminalAgent agents[] = {
			OrkigeEditor::TerminalAgent::Claude,
			OrkigeEditor::TerminalAgent::Codex,
			OrkigeEditor::TerminalAgent::Opencode,
			OrkigeEditor::TerminalAgent::Aider,
			OrkigeEditor::TerminalAgent::Gemini,
			OrkigeEditor::TerminalAgent::Generic,
		};
		const int agentCount =
			static_cast<int>(sizeof(agents) / sizeof(agents[0]));
		for (int i = 0; i < agentCount; ++i)
		{
			const unsigned int cp =
				OrkigeEditor::terminalAgentBadgeCodepoint(agents[i]);
			rectIds[i] = atlas->AddCustomRectFontGlyph(uiFont,
				static_cast<ImWchar>(cp), cell, cell, advance,
				ImVec2(0.0f, offsetY));
		}

		// (2) force the build + packing, then GENERATE + blit each badge into its
		// packed rect. GetTexDataAsRGBA32 caches the built buffer, so the renderer's
		// later call returns this same (now badge-filled) atlas for upload.
		unsigned char* pixels = nullptr;
		int atlasW = 0;
		int atlasH = 0;
		atlas->GetTexDataAsRGBA32(&pixels, &atlasW, &atlasH);
		if (pixels == nullptr || atlasW <= 0 || atlasH <= 0)
		{
			return;
		}
		for (int i = 0; i < agentCount; ++i)
		{
			const ImFontAtlasRect* rect =
				atlas->GetCustomRectByIndex(rectIds[i]);
			if (rect == nullptr || rect->w == 0 || rect->h == 0)
			{
				continue;
			}
			const int size = std::min(static_cast<int>(rect->w),
				static_cast<int>(rect->h));
			const std::vector<unsigned char> badge =
				OrkigeEditor::terminalAgentBadgePixels(agents[i], size);
			if (badge.empty())
			{
				continue;
			}
			for (int y = 0; y < size; ++y)
			{
				unsigned char* dst = pixels +
					(static_cast<std::size_t>(rect->y + y) * atlasW +
						rect->x) * 4;
				const unsigned char* src =
					badge.data() + static_cast<std::size_t>(y) * size * 4;
				std::memcpy(dst, src, static_cast<std::size_t>(size) * 4);
			}
		}
	}
	//---------------------------------------------------------
	bool compactCheckbox(const char* label, bool* value)
	{
		// the checkbox square is FontSize + FramePadding.y*2; trim ~20% off it by
		// pushing a reduced vertical frame padding for just this widget (the
		// global FramePadding is untouched, so surrounding controls keep their
		// size). Clamp to zero so a large font never asks for negative padding.
		ImGuiStyle const& style = ImGui::GetStyle();
		const float square = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
		float padY = (square * 0.8f - ImGui::GetFontSize()) * 0.5f;
		if (padY < 0.0f)
		{
			padY = 0.0f;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			ImVec2(style.FramePadding.x, padY));
		const bool changed = ImGui::Checkbox(label, value);
		ImGui::PopStyleVar();
		return changed;
	}
}
