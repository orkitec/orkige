/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	FontBakeImpl.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The ONE translation unit that compiles the single-file TrueType
	rasteriser. Nothing else in the tree includes it, so stb_truetype stays
	out of every header, the neutral umbrella and the precompiled header.
	FontAtlas and the tests reach it only through the FontBake seam declared
	in FontBake.h - the same discipline as engine_sound/StbVorbisImpl.cpp.
*********************************************************************/

#include "engine_gui/FontBake.h"

#include <cstring>

// the single-file rasteriser trips a couple of the tree's warnings-as-
// behaviour flags; silence them locally without editing the vendored header
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wcast-qual"
#	pragma clang diagnostic ignored "-Wunused-function"
#	pragma clang diagnostic ignored "-Wtautological-compare"
#elif defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(__clang__)
#	pragma clang diagnostic pop
#elif defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif

namespace Orkige
{
	namespace FontBake
	{
		//! the opaque face: stb needs the font bytes resident for its lifetime
		struct Face
		{
			std::vector<unsigned char>	bytes;
			stbtt_fontinfo				info;
		};
		//----------------------------------------------------
		Face* open(unsigned char const * ttf, int size)
		{
			if(ttf == NULL || size <= 0)
			{
				return NULL;
			}
			Face* face = new Face();
			face->bytes.assign(ttf, ttf + size);
			const int offset =
				stbtt_GetFontOffsetForIndex(face->bytes.data(), 0);
			if(offset < 0 ||
				stbtt_InitFont(&face->info, face->bytes.data(), offset) == 0)
			{
				delete face;
				return NULL;
			}
			return face;
		}
		//----------------------------------------------------
		void close(Face * face)
		{
			delete face;
		}
		//----------------------------------------------------
		float scaleForPixelHeight(Face * face, float pixelHeight)
		{
			if(face == NULL)
			{
				return 0.0f;
			}
			return stbtt_ScaleForPixelHeight(&face->info, pixelHeight);
		}
		//----------------------------------------------------
		void verticalMetrics(Face * face, float scale,
			float & ascent, float & descent, float & lineGap)
		{
			ascent = descent = lineGap = 0.0f;
			if(face == NULL)
			{
				return;
			}
			int a = 0, d = 0, g = 0;
			stbtt_GetFontVMetrics(&face->info, &a, &d, &g);
			ascent = float(a) * scale;
			descent = float(d) * scale;
			lineGap = float(g) * scale;
		}
		//----------------------------------------------------
		void horizontalMetrics(Face * face, uint codepoint, float scale,
			float & advance, float & leftSideBearing)
		{
			advance = leftSideBearing = 0.0f;
			if(face == NULL)
			{
				return;
			}
			int adv = 0, lsb = 0;
			stbtt_GetCodepointHMetrics(&face->info, int(codepoint), &adv, &lsb);
			advance = float(adv) * scale;
			leftSideBearing = float(lsb) * scale;
		}
		//----------------------------------------------------
		float kerning(Face * face, uint left, uint right, float scale)
		{
			if(face == NULL)
			{
				return 0.0f;
			}
			const int kern = stbtt_GetCodepointKernAdvance(
				&face->info, int(left), int(right));
			return float(kern) * scale;
		}
		//----------------------------------------------------
		bool hasCodepoint(Face * face, uint codepoint)
		{
			if(face == NULL)
			{
				return false;
			}
			return stbtt_FindGlyphIndex(&face->info, int(codepoint)) != 0;
		}
		//----------------------------------------------------
		Bitmap rasterize(Face * face, uint codepoint, float scale)
		{
			Bitmap out;
			if(face == NULL || scale <= 0.0f)
			{
				return out;
			}
			int w = 0, h = 0, xoff = 0, yoff = 0;
			unsigned char* pixels = stbtt_GetCodepointBitmap(
				&face->info, scale, scale, int(codepoint),
				&w, &h, &xoff, &yoff);
			if(pixels == NULL || w <= 0 || h <= 0)
			{
				if(pixels != NULL)
				{
					stbtt_FreeBitmap(pixels, NULL);
				}
				return out;
			}
			out.width = w;
			out.height = h;
			out.xOffset = xoff;
			out.yOffset = yoff;
			out.coverage.assign(pixels, pixels + size_t(w) * size_t(h));
			stbtt_FreeBitmap(pixels, NULL);
			return out;
		}
	}
}
