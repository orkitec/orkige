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
	//! @brief the PURE single-line text-editing model behind GuiTextEntry:
	//! a UTF-8 buffer + a caret BYTE index, with insert / delete / caret motion
	//! that step by whole UTF-8 code points (so multibyte input is never split).
	//! Render-free on purpose - the widget owns the on-screen glyphs; these
	//! functions own the string, and the headless unit test drives them directly.
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
	}
}

#endif //__GuiTextEdit_h__11_7_2026__12_00_00__
