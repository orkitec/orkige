/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	FontBake.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __FontBake_h__11_7_2026__03_30_00__
#define __FontBake_h__11_7_2026__03_30_00__

//! @file FontBake.h
//! @brief the TrueType rasterisation seam: a thin, renderer-free wrapper
//! over the single-file stb_truetype rasteriser.
//! @remarks the ONE place stb_truetype is compiled is FontBakeImpl.cpp
//! (STB_TRUETYPE_IMPLEMENTATION); everything else (FontAtlas, the unit
//! tests) reaches it ONLY through these functions, so the library never
//! leaks into headers, the neutral umbrella or the precompiled header - the
//! same isolation pattern as engine_sound/StbVorbisImpl.cpp. Handles are
//! opaque; all metrics are returned in DESIGN pixels (already multiplied by
//! the requested pixel height's scale), advances/kerning as fractional
//! pixels, so the caller stores them straight into UiFont/UiGlyph.

#include "engine_module/EnginePrerequisites.h"

#include <vector>

namespace Orkige
{
	namespace FontBake
	{
		//! opaque owner of a parsed TrueType face (holds a copy of the bytes)
		struct Face;

		//! @brief parse a TrueType blob; NULL when it will not parse. The
		//! bytes are copied and held by the returned face until close().
		Face*	open(unsigned char const * ttf, int size);
		//! free a face opened by FontBake::open
		void	close(Face * face);

		//! @brief the stb pixel scale for a target cap/pixel height
		float	scaleForPixelHeight(Face * face, float pixelHeight);

		//! @brief vertical metrics for a scale (design pixels): ascent is
		//! positive above the baseline, descent negative below, lineGap the
		//! extra leading. All already multiplied by `scale`.
		void	verticalMetrics(Face * face, float scale,
			float & ascent, float & descent, float & lineGap);

		//! @brief horizontal advance + left side bearing of a codepoint at a
		//! scale (design pixels). advance is 0 for a codepoint the face lacks.
		void	horizontalMetrics(Face * face, uint codepoint, float scale,
			float & advance, float & leftSideBearing);

		//! @brief additional advance between two codepoints (design pixels at
		//! `scale`; 0 when the face has no kerning for the pair)
		float	kerning(Face * face, uint left, uint right, float scale);

		//! @brief does the face contain a glyph for this codepoint?
		bool	hasCodepoint(Face * face, uint codepoint);

		//! @brief a rasterised glyph: an 8-bit coverage box plus its offset
		//! from the pen origin (xOffset right, yOffset DOWN from the baseline
		//! - stb's convention, yOffset is negative for ink above the baseline)
		struct Bitmap
		{
			std::vector<unsigned char>	coverage;	//!< w*h bytes, 0..255
			int	width = 0, height = 0;
			int	xOffset = 0, yOffset = 0;
		};

		//! @brief rasterise one codepoint at a pixel scale into an owned
		//! coverage box. An empty box (width/height 0) for whitespace or a
		//! missing glyph.
		Bitmap	rasterize(Face * face, uint codepoint, float scale);
	}
}

#endif //__FontBake_h__11_7_2026__03_30_00__
