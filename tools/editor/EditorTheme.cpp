// EditorTheme - macOS-dark-mode-inspired ImGui style (see header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorTheme.h"
#include "IconsFontAwesome6.h"

#include <cfloat>
#include <filesystem>

namespace Orkige
{
	namespace
	{
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

		// --- the palette (named after their macOS dark mode counterparts) ---
		// window/panel surfaces
		constexpr ImVec4 WINDOW_BG        = rgba(0x232323);	//!< panel body
		constexpr ImVec4 DOCKSPACE_BG     = rgba(0x1a1a1a);	//!< empty dock area (darker than panels)
		constexpr ImVec4 POPUP_BG         = rgba(0x2a2a2c, 0.98f);	//!< menus/popups (elevated)
		constexpr ImVec4 TITLE_BG         = rgba(0x2d2d2d);	//!< flat title/tab strip
		constexpr ImVec4 MENUBAR_BG       = rgba(0x282828);
		// controls (buttons, input fields, combos)
		constexpr ImVec4 CONTROL_BG       = rgba(0x3a3a3c);
		constexpr ImVec4 CONTROL_HOVER    = rgba(0x48484a);
		constexpr ImVec4 CONTROL_ACTIVE   = rgba(0x545456);
		// the macOS accent blue
		constexpr ImVec4 ACCENT           = rgba(0x0a84ff);
		constexpr ImVec4 ACCENT_HOVER     = rgba(0x409cff);
		constexpr ImVec4 ACCENT_SELECTION = rgba(0x0a84ff, 0.55f);	//!< list selection fill
		constexpr ImVec4 ACCENT_SOFT      = rgba(0x0a84ff, 0.30f);	//!< hover/soft highlight
		// text
		constexpr ImVec4 TEXT_PRIMARY     = rgba(0xe5e5e7);
		constexpr ImVec4 TEXT_SECONDARY   = rgba(0x98989d);
		// hairlines: macOS separators are ~10% white on dark
		constexpr ImVec4 SEPARATOR        = rgba(0xffffff, 0.10f);
		constexpr ImVec4 BORDER           = rgba(0xffffff, 0.08f);
		// scrollbars
		constexpr ImVec4 SCROLL_GRAB      = rgba(0x5a5a5e, 0.80f);
		constexpr ImVec4 SCROLL_GRAB_HOVER = rgba(0x6e6e73, 0.90f);
		// docked tabs styled like a macOS segmented control: resting segments
		// blend into the strip, the selected one is a lighter grey pill - no
		// blue, no overline (that is the "flatter" part)
		constexpr ImVec4 TAB_RESTING      = rgba(0x2d2d2d);
		constexpr ImVec4 TAB_HOVER        = rgba(0x3a3a3c);
		constexpr ImVec4 TAB_SELECTED     = rgba(0x48484a);
		constexpr ImVec4 TAB_DIMMED       = rgba(0x262626);
		constexpr ImVec4 TRANSPARENT_     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

		//! system UI font candidates, best first: the macOS system font
		//! (San Francisco, present on every macOS install) and common Linux
		//! distro fonts; loaded at runtime, never shipped with the project.
		//! No match -> nullptr -> ImGui's embedded default font.
		const char* const SYSTEM_FONT_PATHS[] = {
			"/System/Library/Fonts/SFNS.ttf",						// macOS
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",		// Debian/Ubuntu
			"/usr/share/fonts/TTF/DejaVuSans.ttf",					// Arch/Fedora-ish
			"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",	// Noto fallback
		};

		//! the standalone larger icon font (grid-tile drawing); null until a
		//! successful loadEditorIconFont, which is also the "icons available" flag
		ImFont* gIconFontLarge = nullptr;

		//! the pixel size the standalone icon font is rasterised at: big enough
		//! that grid tiles (drawn at tile size, up to ~128 px) only mildly upscale
		//! while small tiles / list rows downscale crisply
		constexpr float ICON_FONT_ATLAS_PIXELS = 48.0f;

		//! the FA6 codepoints the editor actually draws (asset kinds + folders) -
		//! a tight glyph range so the atlas rasterises ~a dozen glyphs, not the
		//! whole Font Awesome block. Pairs are {first, last}; see
		//! IconsFontAwesome6.h for the ICON_FA_* names behind each value.
		const ImWchar ICON_GLYPH_RANGES[] = {
			0xf001, 0xf001,		// music (audio)
			0xf008, 0xf008,		// film (scene)
			0xf03e, 0xf03e,		// image (texture)
			0xf07b, 0xf07c,		// folder / folder-open
			0xf15b, 0xf15b,		// file (unknown)
			0xf1b2, 0xf1b2,		// cube (mesh)
			0xf1c9, 0xf1c9,		// file-code (script)
			0xf24d, 0xf24d,		// clone (prefab)
			0,
		};
	}

	//---------------------------------------------------------
	void applyMacDarkTheme(ImGuiStyle& style, float contentScale)
	{
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

		// --- palette ---
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = TEXT_PRIMARY;
		colors[ImGuiCol_TextDisabled] = TEXT_SECONDARY;
		colors[ImGuiCol_WindowBg] = WINDOW_BG;
		colors[ImGuiCol_ChildBg] = TRANSPARENT_;
		colors[ImGuiCol_PopupBg] = POPUP_BG;
		colors[ImGuiCol_Border] = BORDER;
		colors[ImGuiCol_BorderShadow] = TRANSPARENT_;
		colors[ImGuiCol_FrameBg] = CONTROL_BG;
		colors[ImGuiCol_FrameBgHovered] = CONTROL_HOVER;
		colors[ImGuiCol_FrameBgActive] = CONTROL_ACTIVE;
		// flat title/tab strips (docked panels mostly show tabs, not titles)
		colors[ImGuiCol_TitleBg] = TITLE_BG;
		colors[ImGuiCol_TitleBgActive] = TITLE_BG;
		colors[ImGuiCol_TitleBgCollapsed] = TITLE_BG;
		colors[ImGuiCol_MenuBarBg] = MENUBAR_BG;
		colors[ImGuiCol_ScrollbarBg] = TRANSPARENT_;
		colors[ImGuiCol_ScrollbarGrab] = SCROLL_GRAB;
		colors[ImGuiCol_ScrollbarGrabHovered] = SCROLL_GRAB_HOVER;
		colors[ImGuiCol_ScrollbarGrabActive] = SCROLL_GRAB_HOVER;
		colors[ImGuiCol_CheckMark] = ACCENT;
		colors[ImGuiCol_SliderGrab] = ACCENT;
		colors[ImGuiCol_SliderGrabActive] = ACCENT_HOVER;
		colors[ImGuiCol_Button] = CONTROL_BG;
		colors[ImGuiCol_ButtonHovered] = CONTROL_HOVER;
		colors[ImGuiCol_ButtonActive] = ACCENT;
		// Header drives Selectable/TreeNode selection - accent, like macOS lists
		colors[ImGuiCol_Header] = ACCENT_SELECTION;
		colors[ImGuiCol_HeaderHovered] = ACCENT_SOFT;
		colors[ImGuiCol_HeaderActive] = ACCENT_SELECTION;
		colors[ImGuiCol_Separator] = SEPARATOR;
		colors[ImGuiCol_SeparatorHovered] = ACCENT_SOFT;
		colors[ImGuiCol_SeparatorActive] = ACCENT;
		colors[ImGuiCol_ResizeGrip] = TRANSPARENT_;
		colors[ImGuiCol_ResizeGripHovered] = ACCENT_SOFT;
		colors[ImGuiCol_ResizeGripActive] = ACCENT;
		colors[ImGuiCol_Tab] = TAB_RESTING;
		colors[ImGuiCol_TabHovered] = TAB_HOVER;
		colors[ImGuiCol_TabSelected] = TAB_SELECTED;
		colors[ImGuiCol_TabSelectedOverline] = TRANSPARENT_;
		colors[ImGuiCol_TabDimmed] = TAB_DIMMED;
		colors[ImGuiCol_TabDimmedSelected] = TAB_SELECTED;
		colors[ImGuiCol_TabDimmedSelectedOverline] = TRANSPARENT_;
		colors[ImGuiCol_DockingPreview] = ACCENT_SOFT;
		colors[ImGuiCol_DockingEmptyBg] = DOCKSPACE_BG;
		colors[ImGuiCol_PlotLines] = TEXT_SECONDARY;
		colors[ImGuiCol_PlotLinesHovered] = ACCENT_HOVER;
		colors[ImGuiCol_PlotHistogram] = ACCENT;
		colors[ImGuiCol_PlotHistogramHovered] = ACCENT_HOVER;
		colors[ImGuiCol_TableHeaderBg] = TITLE_BG;
		colors[ImGuiCol_TableBorderStrong] = SEPARATOR;
		colors[ImGuiCol_TableBorderLight] = BORDER;
		colors[ImGuiCol_TableRowBg] = TRANSPARENT_;
		colors[ImGuiCol_TableRowBgAlt] = rgba(0xffffff, 0.03f);
		colors[ImGuiCol_TextSelectedBg] = ACCENT_SOFT;
		colors[ImGuiCol_DragDropTarget] = ACCENT;
		colors[ImGuiCol_NavCursor] = ACCENT;
		colors[ImGuiCol_NavWindowingHighlight] = ACCENT_SOFT;
		colors[ImGuiCol_NavWindowingDimBg] = rgba(0x000000, 0.35f);
		colors[ImGuiCol_ModalWindowDimBg] = rgba(0x000000, 0.45f);

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
		// have a host font
		if (io.Fonts->Fonts.empty())
		{
			io.Fonts->AddFontDefault();
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
			gIconFontLarge = io.Fonts->AddFontFromFileTTF(fontPath,
				ICON_FONT_ATLAS_PIXELS * contentScale, &config,
				ICON_GLYPH_RANGES);
		}
	}
	//---------------------------------------------------------
	ImFont* editorIconFont()
	{
		return gIconFontLarge;
	}
}
