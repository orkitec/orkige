// EditorTheme - the editor's macOS-dark-mode-inspired ImGui style.
//
// applyMacDarkTheme builds the whole ImGuiStyle from named palette constants
// (see EditorTheme.cpp) so the look is reviewable and tweakable in one place:
// macOS dark surfaces (~#232323 windows, #3a3a3c controls), the macOS accent
// blue #0a84ff for selection/checkmarks/sliders/focus, generous rounding and
// comfortable spacing. Stock ImGui has no hover animations - the theme is
// palette/rounding/spacing only, on purpose.
//
// loadMacSystemFont loads the San Francisco system font straight from the OS
// (/System/Library/Fonts/SFNS.ttf) AT RUNTIME - the font file is Apple's and
// is never copied into or redistributed with the project. On any failure
// (non-macOS, file missing, parse error) the caller keeps ImGui's default
// font; both functions are safe no-ops in that sense.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <imgui.h>

namespace Orkige
{
	//! @brief restyle the given ImGuiStyle as "macOS dark mode".
	//! @param style the style to overwrite (typically ImGui::GetStyle())
	//! @param contentScale UI scale factor (render-target pixels per window
	//!        point, 1.0 on non-retina surfaces); sizes/rounding are scaled
	//!        through ImGuiStyle::ScaleAllSizes with it
	void applyMacDarkTheme(ImGuiStyle& style, float contentScale = 1.0f);

	//! @brief try to load the macOS system font (San Francisco) from
	//! /System/Library/Fonts/SFNS.ttf into the given atlas.
	//! Must be called BEFORE the font atlas is built (for the editor: after
	//! the Ogre::ImGuiOverlay is constructed - it owns the ImGui context -
	//! but before ImGuiOverlay::show(), which builds and uploads the atlas).
	//! @param io the ImGui IO whose atlas receives the font
	//! @param sizePoints font size in points (~13-15 is macOS-like)
	//! @param contentScale render-target pixels per point (DPI scale)
	//! @return the loaded font, or nullptr if unavailable (caller keeps the
	//!         ImGui default font - never an error)
	ImFont* loadMacSystemFont(ImGuiIO& io, float sizePoints,
		float contentScale = 1.0f);

	//! @brief merge the editor icon font (Font Awesome 6 Free, Solid) into the
	//! font atlas. The glyphs are added twice from the same .ttf: MergeMode into
	//! the last-added (base UI) font so icons render inline with text at UI size,
	//! AND once more as a standalone larger font (see editorIconFont) the asset
	//! browser draws at grid-tile size - rasterised big so it stays crisp when
	//! scaled down for small tiles/list rows. Only the handful of codepoints the
	//! editor uses are rasterised, so the atlas cost is tiny.
	//! Call AFTER loadMacSystemFont (a base font must exist to merge into) and
	//! BEFORE the atlas is built.
	//! @param io the ImGui IO whose atlas receives the icons
	//! @param fontPath path to fa-solid-900.ttf
	//! @param sizePoints the base UI font size in points (the merged size)
	//! @param contentScale render-target pixels per point (DPI scale)
	//! On any failure (path missing, parse error) the atlas is left unchanged and
	//! editorIconFont() stays nullptr - callers fall back to drawn glyph icons.
	void loadEditorIconFont(ImGuiIO& io, const char* fontPath, float sizePoints,
		float contentScale = 1.0f);

	//! @brief the standalone larger icon font loaded by loadEditorIconFont, or
	//! nullptr when the icon font was unavailable (callers then draw the
	//! programmer-art glyph icons instead). Also the "are icons available?"
	//! probe: non-null implies the merged inline icons are present too.
	ImFont* editorIconFont();

	//! @brief the standalone icon font's native (1:1) draw size in font-size
	//! units, or 0 when no icon font is loaded. Grid draws clamp their requested
	//! glyph size to this so an icon only ever downscales from the crisp atlas.
	float editorIconFontRasterPixels();

	//! @brief an ImGui::Checkbox whose square is ~20% smaller than the themed
	//! default, for compact form rows. Pushes a reduced FramePadding for this one
	//! widget only (the global style is unchanged); same signature/return as
	//! ImGui::Checkbox. Use for form-style toggles, not menu items.
	bool compactCheckbox(const char* label, bool* value);
}
