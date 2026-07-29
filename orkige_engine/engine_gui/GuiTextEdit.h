/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	GuiTextEdit.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiTextEdit_h__11_7_2026__12_00_00__
#define __GuiTextEdit_h__11_7_2026__12_00_00__

#include "core_util/String.h"

namespace Orkige
{
	//! @brief the PURE text-editing model behind GuiTextEntry: a UTF-8 buffer +
	//! a caret BYTE index, with insert / delete / caret motion that step by
	//! whole UTF-8 code points (so multibyte input is never split).
	//! Render-free on purpose - the widget owns the on-screen glyphs; these
	//! functions own the string, and the headless unit test drives them directly.
	//! @remarks The model is line-aware: '\n' is an ordinary (single-byte)
	//! character in the buffer, so backspace/delete JOIN lines with no special
	//! case, and the line accessors below (lineStart / lineEnd / lineIndexOf /
	//! columnOf / moveUp / moveDown) read the LOGICAL lines a multi-line field
	//! edits in. Columns are counted in CODE POINTS, not in pixels: an up/down
	//! step keeps the code-point column, which is exact for the monospaced and
	//! near-exact for proportional text - the widget's SOFT wrap (@see TextWrap)
	//! is a display concern and never changes the buffer.
	namespace TextEntryEdit
	{
		//! is `byte` a UTF-8 continuation byte (10xxxxxx)?
		inline bool isContinuation(unsigned char byte)
		{
			return (byte & 0xC0) == 0x80;
		}
		//! number of code points in a UTF-8 string (the user-facing length)
		inline size_t codepointCount(String const & text)
		{
			size_t count = 0;
			for(size_t i = 0; i < text.size(); ++i)
			{
				if(!isContinuation(static_cast<unsigned char>(text[i])))
				{
					++count;
				}
			}
			return count;
		}
		//! byte index of the code point start BEFORE `byte` (clamped to 0)
		inline size_t prevCodepoint(String const & text, size_t byte)
		{
			if(byte == 0)
			{
				return 0;
			}
			size_t i = byte - 1;
			while(i > 0 && isContinuation(static_cast<unsigned char>(text[i])))
			{
				--i;
			}
			return i;
		}
		//! byte index of the code point start AFTER `byte` (clamped to size)
		inline size_t nextCodepoint(String const & text, size_t byte)
		{
			if(byte >= text.size())
			{
				return text.size();
			}
			size_t i = byte + 1;
			while(i < text.size() &&
				isContinuation(static_cast<unsigned char>(text[i])))
			{
				++i;
			}
			return i;
		}
		//! @brief insert UTF-8 `chunk` at the caret, honouring `maxCodepoints`
		//! (0 = unlimited); advances the caret past the inserted text. Returns
		//! true when anything was inserted.
		inline bool insert(String & text, size_t & caret, String const & chunk,
			size_t maxCodepoints)
		{
			if(chunk.empty() || caret > text.size())
			{
				return false;
			}
			String toInsert = chunk;
			if(maxCodepoints > 0)
			{
				const size_t current = codepointCount(text);
				if(current >= maxCodepoints)
				{
					return false;
				}
				// clip the chunk to the remaining code-point budget
				const size_t room = maxCodepoints - current;
				size_t kept = 0;
				size_t byte = 0;
				while(byte < toInsert.size() && kept < room)
				{
					byte = nextCodepoint(toInsert, byte);
					++kept;
				}
				toInsert.resize(byte);
				if(toInsert.empty())
				{
					return false;
				}
			}
			text.insert(caret, toInsert);
			caret += toInsert.size();
			return true;
		}
		//! delete the code point BEFORE the caret (backspace); true when one went
		inline bool backspace(String & text, size_t & caret)
		{
			if(caret == 0)
			{
				return false;
			}
			const size_t start = prevCodepoint(text, caret);
			text.erase(start, caret - start);
			caret = start;
			return true;
		}
		//! delete the code point AT the caret (forward delete); true when one went
		inline bool del(String & text, size_t & caret)
		{
			if(caret >= text.size())
			{
				return false;
			}
			const size_t end = nextCodepoint(text, caret);
			text.erase(caret, end - caret);
			return true;
		}
		//! move the caret one code point left
		inline void moveLeft(String const & text, size_t & caret)
		{
			caret = prevCodepoint(text, caret);
		}
		//! move the caret one code point right
		inline void moveRight(String const & text, size_t & caret)
		{
			caret = nextCodepoint(text, caret);
		}

		//--- logical lines (the multi-line field; '\n' is the separator) ---

		//! @brief byte index of the first character of the line @p byte sits on
		//! (0 for the first line; just after the preceding '\n' otherwise)
		inline size_t lineStart(String const & text, size_t byte)
		{
			if(byte > text.size())
			{
				byte = text.size();
			}
			const size_t previous = text.rfind('\n', byte == 0 ? 0 : byte - 1);
			if(previous == String::npos || byte == 0)
			{
				return 0;
			}
			return previous + 1;
		}
		//! @brief byte index one past the last character of the line @p byte sits
		//! on (the position of the terminating '\n', or the end of the text)
		inline size_t lineEnd(String const & text, size_t byte)
		{
			if(byte > text.size())
			{
				return text.size();
			}
			const size_t next = text.find('\n', byte);
			return (next == String::npos) ? text.size() : next;
		}
		//! @brief 0-based index of the line @p byte sits on ('\n' count before it)
		inline size_t lineIndexOf(String const & text, size_t byte)
		{
			if(byte > text.size())
			{
				byte = text.size();
			}
			size_t line = 0;
			for(size_t i = 0; i < byte; ++i)
			{
				if(text[i] == '\n')
				{
					++line;
				}
			}
			return line;
		}
		//! @brief number of logical lines (>= 1; a trailing '\n' opens an empty one)
		inline size_t lineCount(String const & text)
		{
			return lineIndexOf(text, text.size()) + 1;
		}
		//! @brief the caret's code-point column inside its own line (0 = line start)
		inline size_t columnOf(String const & text, size_t byte)
		{
			const size_t start = lineStart(text, byte);
			size_t column = 0;
			for(size_t i = start; i < byte && i < text.size(); ++i)
			{
				if(!isContinuation(static_cast<unsigned char>(text[i])))
				{
					++column;
				}
			}
			return column;
		}
		//! @brief byte index of code-point column @p column on the line starting at
		//! @p start, clamped to that line's end (a shorter line lands on its end)
		inline size_t byteAtColumn(String const & text, size_t start, size_t column)
		{
			const size_t end = lineEnd(text, start);
			size_t byte = start;
			for(size_t stepped = 0; stepped < column && byte < end; ++stepped)
			{
				byte = nextCodepoint(text, byte);
			}
			return (byte > end) ? end : byte;
		}
		//! @brief move the caret to the start of its line (Home)
		inline void moveLineHome(String const & text, size_t & caret)
		{
			caret = lineStart(text, caret);
		}
		//! @brief move the caret to the end of its line (End)
		inline void moveLineEnd(String const & text, size_t & caret)
		{
			caret = lineEnd(text, caret);
		}
		//! @brief move the caret one line up, keeping its code-point column (a
		//! shorter line clamps to its end). No-op on the first line.
		//! @return true when the caret moved
		inline bool moveUp(String const & text, size_t & caret)
		{
			const size_t start = lineStart(text, caret);
			if(start == 0)
			{
				return false;	// already on the first line
			}
			const size_t column = columnOf(text, caret);
			const size_t previousStart = lineStart(text, start - 1);
			caret = byteAtColumn(text, previousStart, column);
			return true;
		}
		//! @brief move the caret one line down, keeping its code-point column (a
		//! shorter line clamps to its end). No-op on the last line.
		//! @return true when the caret moved
		inline bool moveDown(String const & text, size_t & caret)
		{
			const size_t end = lineEnd(text, caret);
			if(end >= text.size())
			{
				return false;	// already on the last line
			}
			const size_t column = columnOf(text, caret);
			caret = byteAtColumn(text, end + 1, column);
			return true;
		}
		//! @brief insert a line break at the caret (the multi-line Return); obeys
		//! the same code-point budget as any other insert
		inline bool insertNewline(String & text, size_t & caret,
			size_t maxCodepoints)
		{
			return insert(text, caret, String("\n"), maxCodepoints);
		}
	}
}

#endif //__GuiTextEdit_h__11_7_2026__12_00_00__
