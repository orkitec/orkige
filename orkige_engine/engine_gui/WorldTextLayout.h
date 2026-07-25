/********************************************************************
	created:	Friday 2026/07/25 at 16:00
	filename: 	WorldTextLayout.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __WorldTextLayout_h__25_7_2026__16_00_00__
#define __WorldTextLayout_h__25_7_2026__16_00_00__

//! @file WorldTextLayout.h
//! @brief the PURE glyph-quad layout for world-space text: a UiFont + a
//! string + a world-units-per-line size -> a list of textured glyph quads in
//! a text-LOCAL 2D space (+X right, +Y up, CENTER-anchored on the origin).
//! @remarks Backend- and renderer-free: it reads only UiFont/UiGlyph metrics
//! (the SAME baked glyph pages, kerning and lazy CJK paging the gui consumes
//! through engine_gui/FontAtlas), so world text shares the gui's font page
//! rather than baking a second one. The placement recipe mirrors UiRenderer's
//! caption layout verbatim (top-aligned glyph cells at the pen, advance +
//! kerning, spaces by the font's space length, '\n' opens a new line) so world
//! text and gui text are WYSIWYG-matched. It is factored out of the component
//! precisely so it can be unit-tested headlessly against a baked UiFont.
//!
//! Metrics come through the *Scaled getters (design px x the global
//! UiGlyph::scale density hook) and are normalized by the line height, so the
//! absolute density cancels: `worldPerLine` alone sets the physical size. This
//! assumes a UNIFORM UiGlyph::scale (scale.x == scale.y), which is what the
//! engine sets from the display content scale.

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	class UiFont;

	namespace WorldTextLayout
	{
		//! @brief one inked glyph as a textured quad in text-local 2D space.
		//! Corners are in the QuadCorner order top-left, top-right,
		//! bottom-right, bottom-left (the SpriteBatch winding); uv[i] is the
		//! normalized page coordinate of corner i.
		struct GlyphQuad
		{
			Vec2	corners[4];		//!< text-local (+X right, +Y up), origin-centered
			Vec2	uv[4];			//!< matching normalized glyph-page UVs
		};

		//! @brief the laid-out block: one quad per inked (non-space) glyph plus
		//! the block's overall extents (world units) and line count.
		struct Result
		{
			std::vector<GlyphQuad>	quads;			//!< inked glyphs only (spaces emit none)
			float					width = 0.0f;	//!< block width, world units
			float					height = 0.0f;	//!< block height, world units
			int						lineCount = 0;	//!< '\n'-separated lines (>=1 for non-empty)
		};

		//! @brief lay a UTF-8 string out as world-space glyph quads.
		//! @param font the baked font (its glyph pages back the UVs; a codepoint
		//! beyond the eager range bakes on demand through the font's provider -
		//! the CJK/Cyrillic path - exactly as gui text pages it).
		//! @param utf8 the literal string; '\n' starts a new (center-justified)
		//! line. The component stores the literal - loc() lives in the script.
		//! @param worldPerLine world units mapped to ONE line height (the `size`
		//! property). <= 0, an empty string or an unbaked font yield no quads.
		//! @return the inked quads (center-anchored on the origin) + extents.
		ORKIGE_ENGINE_DLL Result build(UiFont const & font,
			String const & utf8, float worldPerLine);
	}
}

#endif //__WorldTextLayout_h__25_7_2026__16_00_00__
