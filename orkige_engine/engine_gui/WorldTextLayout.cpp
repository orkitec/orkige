/**************************************************************
	created:	2026/07/25 at 16:00
	filename: 	WorldTextLayout.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gui/WorldTextLayout.h"
#include "engine_gui/UiAtlas.h"		// UiFont / UiGlyph / QuadCorner

#include <algorithm>

namespace Orkige
{
	namespace WorldTextLayout
	{
		namespace
		{
			//! decode a UTF-8 byte string into Unicode codepoints. Self-
			//! contained (locale-independent) so a CJK/Cyrillic string pages
			//! its glyphs on every platform. An invalid byte yields U+FFFD and
			//! advances one byte (never wedges).
			void decodeUtf8(String const & utf8, std::vector<uint> & out)
			{
				const unsigned char * bytes =
					reinterpret_cast<const unsigned char *>(utf8.c_str());
				const std::size_t length = utf8.size();
				std::size_t index = 0;
				while(index < length)
				{
					const unsigned char lead = bytes[index];
					uint codepoint = 0xFFFD;
					std::size_t extra = 0;
					if(lead < 0x80)
					{
						codepoint = lead;
					}
					else if((lead & 0xE0) == 0xC0)
					{
						codepoint = lead & 0x1F;
						extra = 1;
					}
					else if((lead & 0xF0) == 0xE0)
					{
						codepoint = lead & 0x0F;
						extra = 2;
					}
					else if((lead & 0xF8) == 0xF0)
					{
						codepoint = lead & 0x07;
						extra = 3;
					}
					// gather the continuation bytes; a truncated/ill-formed
					// sequence falls back to the replacement codepoint
					bool valid = (extra == 0) || (codepoint != 0xFFFD);
					std::size_t consumed = 1;
					for(std::size_t each = 0; each < extra; ++each)
					{
						if(index + 1 + each >= length ||
							(bytes[index + 1 + each] & 0xC0) != 0x80)
						{
							valid = false;
							break;
						}
						codepoint = (codepoint << 6) |
							(bytes[index + 1 + each] & 0x3F);
						++consumed;
					}
					if(!valid)
					{
						out.push_back(0xFFFD);
						index += 1;
						continue;
					}
					out.push_back(codepoint);
					index += consumed;
				}
			}

			//! a glyph placed at a line-relative pen position (screen-like:
			//! +x right, +y DOWN, top of the cell at penTop), carrying its UVs
			struct Placed
			{
				float	left, top, right, bottom;	//!< line-relative box (px)
				Vec2	uv[4];
			};
		}

		Result build(UiFont const & font, String const & utf8, float worldPerLine)
		{
			Result result;
			const float lineHeight = font.getLineHeightScaled();
			if(worldPerLine <= 0.0f || lineHeight <= 0.0f || utf8.empty())
			{
				return result;
			}
			// world units per scaled design pixel: normalizing by the line
			// height makes the absolute UiGlyph::scale density cancel out
			const float worldScale = worldPerLine / lineHeight;
			const float spaceLength = font.getSpaceLengthScaled();

			std::vector<uint> codepoints;
			decodeUtf8(utf8, codepoints);

			// pass 1: place every glyph into per-line buffers (line-relative,
			// pen from 0) and record each line's measured width for centering
			std::vector<std::vector<Placed> >	lines(1);
			std::vector<float>					lineWidths(1, 0.0f);
			float	pen = 0.0f;
			float	lastKerning = 0.0f;
			uint	lastChar = 0;
			for(std::size_t each = 0; each < codepoints.size(); ++each)
			{
				const uint code = codepoints[each];
				if(code == static_cast<uint>('\n'))
				{
					// close the current line (drop the trailing kerning, like
					// the gui's measureText) and open a fresh one
					lineWidths.back() = pen - lastKerning;
					lines.push_back(std::vector<Placed>());
					lineWidths.push_back(0.0f);
					pen = 0.0f;
					lastKerning = 0.0f;
					lastChar = 0;
					continue;
				}
				if(code == static_cast<uint>(' '))
				{
					pen += spaceLength;
					lastKerning = 0.0f;
					lastChar = code;
					continue;
				}
				// a control code below the eager range never draws
				if(code < font.getRangeBegin())
				{
					lastChar = 0;
					continue;
				}
				UiGlyph const * glyph = font.getGlyph(code);
				if(glyph == NULL)
				{
					lastChar = 0;
					continue;
				}
				const float kerning = glyph->getKerningScaled(lastChar);
				const float glyphWidth = glyph->getGlyphWidthScaled();
				const float glyphHeight = glyph->getGlyphHeightScaled();
				// glyphs render TOP-aligned at the pen (the page bakes the cell
				// that way - the same convention as UiRenderer's captions)
				if(glyphWidth > 0.0f && glyphHeight > 0.0f)
				{
					Placed placed;
					placed.left = pen + kerning;
					placed.top = 0.0f;
					placed.right = placed.left + glyphWidth;
					placed.bottom = placed.top + glyphHeight;
					placed.uv[TopLeft] = glyph->texCoords[TopLeft];
					placed.uv[TopRight] = glyph->texCoords[TopRight];
					placed.uv[BottomRight] = glyph->texCoords[BottomRight];
					placed.uv[BottomLeft] = glyph->texCoords[BottomLeft];
					lines.back().push_back(placed);
				}
				pen += glyph->getGlyphAdvanceScaled() + kerning;
				lastKerning = kerning;
				lastChar = code;
			}
			lineWidths.back() = pen - lastKerning;

			result.lineCount = static_cast<int>(lines.size());
			float blockWidth = 0.0f;
			for(std::size_t line = 0; line < lineWidths.size(); ++line)
			{
				blockWidth = std::max(blockWidth, lineWidths[line]);
			}
			const float blockHeight = lineHeight * static_cast<float>(lines.size());

			// pass 2: center each line horizontally, center the block
			// vertically, and convert the screen-like boxes into text-local
			// world quads (+Y up). QuadCorner order TL,TR,BR,BL.
			for(std::size_t line = 0; line < lines.size(); ++line)
			{
				const float lineStartX = -lineWidths[line] * 0.5f;
				const float lineTop =
					static_cast<float>(line) * lineHeight - blockHeight * 0.5f;
				for(std::size_t g = 0; g < lines[line].size(); ++g)
				{
					Placed const & placed = lines[line][g];
					const float sx0 = lineStartX + placed.left;
					const float sx1 = lineStartX + placed.right;
					const float sy0 = lineTop + placed.top;
					const float sy1 = lineTop + placed.bottom;
					GlyphQuad quad;
					// screen -> world-local: x keeps sign, y flips (screen down
					// -> world up), all scaled to world units
					const float wLeft = sx0 * worldScale;
					const float wRight = sx1 * worldScale;
					const float wTop = -sy0 * worldScale;
					const float wBottom = -sy1 * worldScale;
					quad.corners[TopLeft] = Vec2(wLeft, wTop);
					quad.corners[TopRight] = Vec2(wRight, wTop);
					quad.corners[BottomRight] = Vec2(wRight, wBottom);
					quad.corners[BottomLeft] = Vec2(wLeft, wBottom);
					quad.uv[TopLeft] = placed.uv[TopLeft];
					quad.uv[TopRight] = placed.uv[TopRight];
					quad.uv[BottomRight] = placed.uv[BottomRight];
					quad.uv[BottomLeft] = placed.uv[BottomLeft];
					result.quads.push_back(quad);
				}
			}

			result.width = blockWidth * worldScale;
			result.height = blockHeight * worldScale;
			return result;
		}
	}
}
