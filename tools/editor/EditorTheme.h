// EditorTheme - the editor's macOS-inspired ImGui style, in light and dark.
//
// applyEditorTheme builds the whole ImGuiStyle from a named EditorPalette (see
// EditorTheme.cpp) so the look is reviewable and tweakable in one place. The
// colour->slot mapping is shared between the two variants; only the palette
// values differ (darkPalette / lightPalette), each hand-tuned after its macOS
// counterpart - dark surfaces (~#232323) or light surfaces (~#ECECEC), the
// macOS accent blue for selection/checkmarks/sliders/focus, generous rounding
// and comfortable spacing (metrics are identical across variants). Stock ImGui
// has no hover animations - the theme is palette/rounding/spacing only, on
// purpose.
//
// The variant is chosen by resolveEditorTheme from an EditorThemeMode (the
// persisted user preference): System follows SDL_GetSystemTheme, or the user
// pins Dark/Light. currentEditorThemeVariant reports what is live so the few
// colours drawn outside the ImGuiStyle (console log lines, asset-kind tints)
// can pick a legible value for the active background.
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
	//! the persisted theme preference (View > Theme). System defers to the OS
	//! appearance (and tracks live changes); Dark/Light pin one look.
	enum class EditorThemeMode
	{
		System,
		Dark,
		Light,
	};

	//! a concrete resolved look - what applyEditorTheme actually paints.
	enum class EditorThemeVariant
	{
		Dark,
		Light,
	};

	//! resolve a preference to a concrete variant. System reads the live OS
	//! appearance (SDL_GetSystemTheme); an unknown OS appearance falls back to
	//! Dark (the editor's historical default).
	EditorThemeVariant resolveEditorTheme(EditorThemeMode mode);

	//! @brief restyle the given ImGuiStyle as the chosen variant.
	//! Records the variant as the live one (see currentEditorThemeVariant).
	//! @param style the style to overwrite (typically ImGui::GetStyle())
	//! @param variant Dark or Light
	//! @param contentScale UI scale factor (render-target pixels per window
	//!        point, 1.0 on non-retina surfaces); sizes/rounding are scaled
	//!        through ImGuiStyle::ScaleAllSizes with it
	void applyEditorTheme(ImGuiStyle& style, EditorThemeVariant variant,
		float contentScale = 1.0f);

	//! the variant applyEditorTheme last painted (defaults to Dark before any
	//! call). Colours drawn outside the ImGuiStyle branch on this.
	EditorThemeVariant currentEditorThemeVariant();

	//! the dockspace/empty-area background of the active variant, as an engine
	//! window clear colour (r,g,b in 0..1 packed into an ImVec4, alpha unused).
	//! The UI-only editor window clears to this so the gaps between docked
	//! panels match the theme.
	ImVec4 editorDockspaceBackground();

	//! console log line colours for the active variant: a warning amber and an
	//! error red picked to stay legible on the variant's console background.
	ImVec4 editorWarningTextColor();
	ImVec4 editorErrorTextColor();

	//! @brief try to load the macOS system font (San Francisco) from
	//! /System/Library/Fonts/SFNS.ttf into the given atlas.
	//! Must be called BEFORE the font atlas is built (for the editor: after
	//! the ImGui context exists but before the atlas is built and uploaded).
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
