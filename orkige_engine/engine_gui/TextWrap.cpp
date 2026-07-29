/**************************************************************
	created:	2026/07/26 at 10:00
	filename: 	TextWrap.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure greedy line-breaker (@see TextWrap.h).
***************************************************************/

#include "engine_gui/TextWrap.h"
#include "engine_gui/UiAtlas.h"	// UiFont / UiGlyph metrics

#include <algorithm>

namespace Orkige
{
	namespace TextWrap
	{
		namespace
		{
			//! decode a UTF-8 byte string into codepoints (locale-independent so
			//! CJK/Cyrillic pages on every platform). An ill-formed byte yields
			//! U+FFFD and advances one byte - never wedges. Mirrors the decoder in
			//! WorldTextLayout so world text and gui text page identically.
			//! @param offsets the source byte offset of each decoded codepoint
			void decodeUtf8(String const & utf8, std::vector<unsigned int> & out,
				std::vector<std::size_t> & offsets)
			{
				const unsigned char * bytes =
					reinterpret_cast<const unsigned char *>(utf8.c_str());
				const std::size_t length = utf8.size();
				std::size_t index = 0;
				while(index < length)
				{
					const unsigned char lead = bytes[index];
					unsigned int codepoint = 0xFFFD;
					std::size_t extra = 0;
					if(lead < 0x80)			{ codepoint = lead; }
					else if((lead & 0xE0) == 0xC0)	{ codepoint = lead & 0x1F; extra = 1; }
					else if((lead & 0xF0) == 0xE0)	{ codepoint = lead & 0x0F; extra = 2; }
					else if((lead & 0xF8) == 0xF0)	{ codepoint = lead & 0x07; extra = 3; }
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
						codepoint = (codepoint << 6) | (bytes[index + 1 + each] & 0x3F);
						++consumed;
					}
					if(!valid)
					{
						out.push_back(0xFFFD);
						offsets.push_back(index);
						index += 1;
						continue;
					}
					out.push_back(codepoint);
					offsets.push_back(index);
					index += consumed;
				}
			}
		}

		bool isBreakableIdeograph(unsigned int cp)
		{
			// the common runs that carry no inter-word spaces, so a line may
			// break between any two of their glyphs (the standard rule)
			return (cp >= 0x1100 && cp <= 0x11FF) ||	// Hangul Jamo
				(cp >= 0x2E80 && cp <= 0x2EFF) ||		// CJK radicals
				(cp >= 0x3040 && cp <= 0x30FF) ||		// Hiragana + Katakana
				(cp >= 0x3400 && cp <= 0x4DBF) ||		// CJK ext A
				(cp >= 0x4E00 && cp <= 0x9FFF) ||		// CJK unified ideographs
				(cp >= 0xAC00 && cp <= 0xD7AF) ||		// Hangul syllables
				(cp >= 0xF900 && cp <= 0xFAFF) ||		// CJK compat ideographs
				(cp >= 0xFF00 && cp <= 0xFF60) ||		// fullwidth forms
				(cp >= 0x20000 && cp <= 0x2FA1F);		// CJK ext B..
			// U+3000..303F (CJK symbols/punctuation) are intentionally excluded:
			// they are handled like ordinary glyphs so an opening bracket does not
			// dangle at a line end.
		}

		namespace
		{
			//! may a line START at cell @p i (a break opportunity before it)?
			inline bool opportunityAt(std::vector<WrapCell> const & cells, size_t i)
			{
				if(i == 0)
				{
					return false;
				}
				// an explicit CJK boundary, or the first non-space after a space
				return cells[i].breakBefore ||
					(cells[i - 1].space && !cells[i].space);
			}
		}

		void wrap(std::vector<WrapCell> const & cells, float maxWidth,
			WrapResult & out)
		{
			out.clear();
			const size_t n = cells.size();
			out.lineOf.assign(n, 0);
			out.penX.assign(n, 0.0f);
			if(n == 0)
			{
				out.lineCount = 0;
				return;
			}

			int line = 0;
			size_t lineStart = 0;
			float pen = 0.0f;			//!< pen after the last placed cell (px)
			int lastOpp = -1;			//!< last break opportunity on this line

			size_t i = 0;
			while(i < n)
			{
				WrapCell const & c = cells[i];
				if(c.forcedBreak)
				{
					// the newline sits at the pen and emits nothing; open a line
					out.lineOf[i] = line;
					out.penX[i] = pen;
					++line;
					lineStart = i + 1;
					pen = 0.0f;
					lastOpp = -1;
					++i;
					continue;
				}
				const bool begins = (i == lineStart);
				if(!begins && opportunityAt(cells, i))
				{
					lastOpp = int(i);
				}
				const float lead = begins ? 0.0f : c.leadKern;
				const float x = pen + lead;
				// a non-space cell whose right edge crosses the width wraps (never
				// the first cell of a line, which must be placed even if too wide)
				if(maxWidth > 0.0f && !begins && !c.space &&
					(x + c.width) > maxWidth)
				{
					// break at the last opportunity if we passed one this line,
					// else hard-break right here (a single over-wide run)
					const size_t bp = (lastOpp > int(lineStart))
						? size_t(lastOpp) : i;
					++line;
					lineStart = bp;
					pen = 0.0f;
					lastOpp = -1;
					i = bp;			// re-place the moved cells on the new line
					continue;
				}
				out.penX[i] = x;
				out.lineOf[i] = line;
				pen = x + c.advance;
				++i;
			}

			out.lineCount = line + 1;
			out.lineWidth.assign(size_t(out.lineCount), 0.0f);
			for(size_t each = 0; each < n; ++each)
			{
				WrapCell const & c = cells[each];
				if(c.space || c.forcedBreak)
				{
					continue;	// trailing spaces / newlines never extend a line
				}
				const int l = out.lineOf[each];
				out.lineWidth[size_t(l)] =
					std::max(out.lineWidth[size_t(l)], out.penX[each] + c.width);
			}
		}

		void buildRun(UiFont const & font, String const & utf8,
			std::vector<WrapCell> & cells, std::vector<UiGlyph const *> & glyphs)
		{
			std::vector<unsigned int> codepoints;
			std::vector<std::size_t> offsets;
			decodeUtf8(utf8, codepoints, offsets);
			unsigned int lastChar = 0;
			for(std::size_t each = 0; each < codepoints.size(); ++each)
			{
				const unsigned int code = codepoints[each];
				const std::size_t offset = offsets[each];
				if(code == static_cast<unsigned int>('\n'))
				{
					WrapCell cell;
					cell.forcedBreak = true;
					cell.byteOffset = offset;
					cells.push_back(cell);
					glyphs.push_back(NULL);
					lastChar = 0;
					continue;
				}
				if(code == static_cast<unsigned int>(' '))
				{
					WrapCell cell;
					cell.space = true;
					cell.advance = font.getSpaceLengthScaled();
					cell.byteOffset = offset;
					cells.push_back(cell);
					glyphs.push_back(NULL);
					lastChar = static_cast<unsigned int>(' ');
					continue;
				}
				UiGlyph const * glyph = font.getGlyph(code);
				if(glyph == NULL)
				{
					lastChar = 0;	// an unbaked glyph draws nothing (as before)
					continue;
				}
				float kerning = glyph->getKerningScaled(lastChar);
				if(kerning == 0.0f)
				{
					kerning = font.getLetterSpacingScaled();
				}
				WrapCell cell;
				cell.leadKern = kerning;
				cell.advance = glyph->getGlyphAdvanceScaled();
				cell.width = glyph->getGlyphWidthScaled();
				cell.breakBefore = isBreakableIdeograph(code);
				cell.byteOffset = offset;
				cells.push_back(cell);
				glyphs.push_back(glyph);
				lastChar = code;
			}
		}

		CaretSpot locateCaret(std::vector<WrapCell> const & cells,
			WrapResult const & wrapped, size_t byteIndex)
		{
			CaretSpot spot;
			const size_t n = cells.size();
			if(n == 0 || wrapped.lineOf.size() < n)
			{
				return spot;
			}
			// the caret sits before the first cell that reaches its byte offset.
			// A codepoint the font could not bake emits no cell, so a caret
			// inside such a run resolves to the next drawable position.
			for(size_t each = 0; each < n; ++each)
			{
				if(cells[each].byteOffset >= byteIndex)
				{
					spot.line = wrapped.lineOf[each];
					spot.penX = wrapped.penX[each];
					return spot;
				}
			}
			// past every cell: after the last one - or at the start of the line a
			// trailing '\n' just opened
			WrapCell const & last = cells[n - 1];
			if(last.forcedBreak)
			{
				spot.line = wrapped.lineOf[n - 1] + 1;
				spot.penX = 0.0f;
				return spot;
			}
			spot.line = wrapped.lineOf[n - 1];
			spot.penX = wrapped.penX[n - 1] + last.advance;
			return spot;
		}

		void lineStartBytes(std::vector<WrapCell> const & cells,
			WrapResult const & wrapped, size_t textLength,
			std::vector<size_t> & out)
		{
			const int lines = wrapped.lineCount > 0 ? wrapped.lineCount : 1;
			// a line with no cell of its own (the empty tail a trailing '\n'
			// opens) starts at the end of the text
			out.assign(size_t(lines), textLength);
			std::vector<bool> seen(size_t(lines), false);
			for(size_t each = 0; each < cells.size(); ++each)
			{
				if(each >= wrapped.lineOf.size())
				{
					break;
				}
				const int line = wrapped.lineOf[each];
				if(line < 0 || line >= lines || seen[size_t(line)])
				{
					continue;
				}
				// a '\n' belongs to the line it ENDS, so the first cell carrying a
				// line index is always that line's first character (an empty line
				// opened by a '\n' starts at the next newline cell)
				out[size_t(line)] = cells[each].byteOffset;
				seen[size_t(line)] = true;
			}
			out[0] = 0;
		}
	}
}
