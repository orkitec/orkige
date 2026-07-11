/********************************************************************
	created:	Saturday 2026/07/11 at 22:10
	filename: 	ScreenStack.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ScreenStack_h__11_7_2026__22_10_00__
#define __ScreenStack_h__11_7_2026__22_10_00__

//! @file ScreenStack.h
//! @brief the backend-neutral LIFO stack of screen names behind the gui screen
//! router. A "screen" is a whole UI page (one .oui layout or a registered
//! builder); this pure state machine only tracks WHICH screens are stacked and
//! in what order. No renderer, no widget - the gui GuiManager keeps one of
//! these and drives widget lifecycles + transitions off its transitions, while
//! a unit test shares the stack arithmetic.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <vector>

namespace Orkige
{
	//! @brief a LIFO stack of screen names, bottom-to-top. `push` covers the
	//! current screen with a new one, `pop` returns to the one beneath, and
	//! `replace` swaps the top without growing the stack (the "go here instead"
	//! navigation). The root screen (index 0) is the bottom; a router refuses to
	//! pop it empty by policy, not here - this container just does the arithmetic.
	struct ORKIGE_CORE_DLL ScreenStack
	{
		std::vector<String>	entries;	//!< bottom-to-top order (top == back())

		//! @brief push a screen on top of the stack (it becomes current)
		void push(String const & name);
		//! @brief replace the top screen with @p name without changing the depth;
		//! on an empty stack this is a plain push (nothing to replace)
		void replace(String const & name);
		//! @brief pop the top screen. @return its name, or "" when empty.
		String pop();
		//! @brief the current (top) screen's name, or "" when empty
		String current() const;
		//! @brief the whole bottom-to-top path (the readback an agent inspects)
		std::vector<String> const & path() const { return this->entries; }
		//! @brief is @p name anywhere on the stack
		bool contains(String const & name) const;
		void clear() { this->entries.clear(); }
		bool empty() const { return this->entries.empty(); }
		std::size_t size() const { return this->entries.size(); }
	};
}

#endif //__ScreenStack_h__11_7_2026__22_10_00__
