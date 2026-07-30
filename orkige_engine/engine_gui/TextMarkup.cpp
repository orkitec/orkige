/**************************************************************
	created:	2026/07/30 at 09:00
	filename: 	TextMarkup.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the inline rich-text grammar: pure parse + the cell builder
				(@see TextMarkup.h).
***************************************************************/

#include "engine_gui/TextMarkup.h"
#include "engine_gui/UiAtlas.h"		// UiFont / UiGlyph / UiSprite metrics
#include "core_util/StringUtil.h"

#include <algorithm>

namespace Orkige
{
	namespace TextMarkup
	{
		namespace
		{
			//! @brief parse a hex colour body ("RRGGBB" or "RRGGBBAA") into 0..1
			//! components; false on any non-hex digit or a length that is neither
			bool parseHexColour(String const & body, float rgba[4])
			{
				if(body.size() != 6 && body.size() != 8)
				{
					return false;
				}
				unsigned int channels[4] = { 0, 0, 0, 255 };
				for(size_t each = 0; each < body.size(); each += 2)
				{
					unsigned int value = 0;
					for(size_t digit = 0; digit < 2; ++digit)
					{
						const char c = body[each + digit];
						unsigned int nibble = 0;
						if(c >= '0' && c <= '9')		{ nibble = uint(c - '0'); }
						else if(c >= 'a' && c <= 'f')	{ nibble = uint(c - 'a') + 10; }
						else if(c >= 'A' && c <= 'F')	{ nibble = uint(c - 'A') + 10; }
						else							{ return false; }
						value = (value << 4) | nibble;
					}
					channels[each / 2] = value;
				}
				for(size_t each = 0; each < 4; ++each)
				{
					rgba[each] = float(channels[each]) / 255.0f;
				}
				return true;
			}

			//! the tag NAME of a body ("c=FF0000" -> "c", "/c" -> "/c"), lower
			//! cased: tag names are case-insensitive, their arguments are not (a
			//! font role name and a sprite name are ids)
			String tagName(String const & body)
			{
				const size_t equals = body.find('=');
				return StringUtil::to_lower_copy((equals == String::npos)
					? body : body.substr(0, equals));
			}
			//! the argument of a `name=value` body ("" when there is no '=')
			String tagArgument(String const & body)
			{
				const size_t equals = body.find('=');
				return (equals == String::npos) ? String() : body.substr(equals + 1);
			}

			//! flush the accumulated literal text as one run carrying the currently
			//! open colour / font
			void flushText(String & literal, std::vector<float> const & colourStack,
				std::vector<String> const & fontStack, std::vector<Run> & out)
			{
				if(literal.empty())
				{
					return;
				}
				Run run;
				run.kind = Run::RK_Text;
				run.text = literal;
				if(colourStack.size() >= 4)
				{
					run.hasColour = true;
					const size_t base = colourStack.size() - 4;
					for(size_t each = 0; each < 4; ++each)
					{
						run.colour[each] = colourStack[base + each];
					}
				}
				if(!fontStack.empty())
				{
					run.fontRef = fontStack.back();
				}
				out.push_back(run);
				literal.clear();
			}
		}
		//---------------------------------------------------------
		void parse(String const & text, Parse & out)
		{
			out.clear();
			// the open spans, innermost last (a colour is four floats per level)
			std::vector<float> colourStack;
			std::vector<String> fontStack;
			String literal;

			size_t index = 0;
			while(index < text.size())
			{
				const char c = text[index];
				if(c != '[')
				{
					literal.push_back(c);
					++index;
					continue;
				}
				// "[[" is the escape for a literal '['
				if(index + 1 < text.size() && text[index + 1] == '[')
				{
					literal.push_back('[');
					index += 2;
					out.sawMarkup = true;
					continue;
				}
				const size_t close = text.find(']', index);
				if(close == String::npos)
				{
					// an unterminated tag: the rest of the string is plain text, so
					// the author sees what they typed instead of losing it
					out.diagnostics.push_back("unterminated markup tag: '" +
						text.substr(index) + "' has no closing ']' - drawn as text");
					literal.append(text.substr(index));
					break;
				}
				const String body = text.substr(index + 1, close - index - 1);
				const size_t after = close + 1;
				const String name = tagName(body);
				const String argument = tagArgument(body);
				bool recognised = false;

				if(name == "/c")
				{
					if(colourStack.size() >= 4)
					{
						flushText(literal, colourStack, fontStack, out.runs);
						colourStack.resize(colourStack.size() - 4);
						recognised = true;
					}
					else
					{
						out.diagnostics.push_back("markup '[/c]' closes a colour "
							"span that was never opened - ignored");
						recognised = true;	// dropped, not drawn
					}
				}
				else if(name == "/f")
				{
					if(!fontStack.empty())
					{
						flushText(literal, colourStack, fontStack, out.runs);
						fontStack.pop_back();
						recognised = true;
					}
					else
					{
						out.diagnostics.push_back("markup '[/f]' closes a font span "
							"that was never opened - ignored");
						recognised = true;
					}
				}
				else if(name == "c")
				{
					float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					if(parseHexColour(argument, rgba))
					{
						flushText(literal, colourStack, fontStack, out.runs);
						for(size_t each = 0; each < 4; ++each)
						{
							colourStack.push_back(rgba[each]);
						}
						recognised = true;
					}
					else
					{
						out.diagnostics.push_back("markup colour '[" + body +
							"]' is not RRGGBB or RRGGBBAA hex - drawn as text");
					}
				}
				else if(name == "f")
				{
					if(!argument.empty())
					{
						flushText(literal, colourStack, fontStack, out.runs);
						fontStack.push_back(argument);
						recognised = true;
					}
					else
					{
						out.diagnostics.push_back("markup '[f=]' names no font - "
							"drawn as text");
					}
				}
				else if(name == "sprite")
				{
					if(!argument.empty())
					{
						flushText(literal, colourStack, fontStack, out.runs);
						Run run;
						run.kind = Run::RK_Sprite;
						run.sprite = argument;
						// an inline sprite is TINTED by the open colour span, so
						// "[c=FF8800][sprite=coin][/c]" works like text does
						if(colourStack.size() >= 4)
						{
							run.hasColour = true;
							const size_t base = colourStack.size() - 4;
							for(size_t each = 0; each < 4; ++each)
							{
								run.colour[each] = colourStack[base + each];
							}
						}
						out.runs.push_back(run);
						recognised = true;
					}
					else
					{
						out.diagnostics.push_back("markup '[sprite=]' names no "
							"sprite - drawn as text");
					}
				}
				else
				{
					out.diagnostics.push_back("unknown markup tag '[" + body +
						"]' - drawn as text");
				}

				if(recognised)
				{
					out.sawMarkup = true;
					index = after;
				}
				else
				{
					// verbatim: the whole tag becomes text (brackets included)
					literal.append(text.substr(index, after - index));
					index = after;
				}
			}

			// a span still open at the end of the text closes there - the run keeps
			// its colour/font, which is what the author visibly asked for
			if(colourStack.size() >= 4)
			{
				out.diagnostics.push_back("markup colour span is never closed with "
					"'[/c]' - it ends with the text");
			}
			if(!fontStack.empty())
			{
				out.diagnostics.push_back("markup font span is never closed with "
					"'[/f]' - it ends with the text");
			}
			flushText(literal, colourStack, fontStack, out.runs);
		}
		//---------------------------------------------------------
		void buildCells(std::vector<ResolvedRun> const & runs, float textScale,
			std::vector<WrapCell> & cells, std::vector<CellAttr> & attrs,
			float & lineHeight)
		{
			cells.clear();
			attrs.clear();
			const float ts = textScale > 0.0f ? textScale : 1.0f;
			// pass one parks each cell's OWN run height in dropY; once the block's
			// line height is known (the tallest run wins) pass two turns that into
			// the per-cell drop, so runs of different sizes share one bottom edge
			// instead of hanging from the line's top
			for(ResolvedRun const & run : runs)
			{
				if(run.sprite != NULL)
				{
					// one atomic cell: TextWrap moves it whole to the next line when
					// it no longer fits (it carries no break opportunity of its own)
					WrapCell cell;
					cell.advance = Real(run.sprite->spriteWidth) * ts;
					cell.width = cell.advance;
					cells.push_back(cell);
					CellAttr attr;
					attr.sprite = run.sprite;
					attr.colour = run.colour;
					attrs.push_back(attr);
					const float spriteHeight =
						float(run.sprite->spriteHeight) * ts;
					attrs.back().dropY = spriteHeight;
					lineHeight = std::max(lineHeight, spriteHeight);
					continue;
				}
				if(run.font == NULL || run.text.empty())
				{
					continue;
				}
				// the SHARED cell builder does the glyph metrics, the kerning, the
				// space advance, the '\n' cells and the CJK break flags
				std::vector<WrapCell> runCells;
				std::vector<UiGlyph const *> runGlyphs;
				TextWrap::buildRun(*run.font, run.text, runCells, runGlyphs, ts);
				const float runHeight = float(run.font->getLineHeightScaled()) * ts;
				for(size_t each = 0; each < runCells.size(); ++each)
				{
					cells.push_back(runCells[each]);
					CellAttr attr;
					attr.glyph = runGlyphs[each];
					attr.colour = run.colour;
					attr.dropY = runHeight;
					attrs.push_back(attr);
				}
				lineHeight = std::max(lineHeight, runHeight);
			}
			// pass two: a shorter run sits on the line's bottom edge, so a
			// mixed-size line reads as one line (@see CellAttr::dropY)
			for(CellAttr & attr : attrs)
			{
				attr.dropY = lineHeight - attr.dropY;
			}
		}
	}
}
