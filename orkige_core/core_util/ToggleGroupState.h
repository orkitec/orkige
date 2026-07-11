/********************************************************************
	created:	Saturday 2026/07/11 at 18:00
	filename: 	ToggleGroupState.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ToggleGroupState_h__11_7_2026__18_00_00__
#define __ToggleGroupState_h__11_7_2026__18_00_00__

//! @file ToggleGroupState.h
//! @brief backend-neutral single-selection (radio) state machine. A group of
//! N members holds AT MOST one selected index; tapping a member selects it and
//! implicitly clears the others. No renderer, no widget, no scripting - the gui
//! GuiToggleGroup drives its checkboxes from this, and a unit test shares the
//! exact same logic table.

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	//! @brief single-selection state over `count` indexed members. `selected` is
	//! the chosen index or -1 when none is selected (only reachable with
	//! allowNone, or before the first selection).
	struct ORKIGE_CORE_DLL ToggleGroupState
	{
		int		count = 0;			//!< number of members (0..count-1 are valid)
		int		selected = -1;		//!< selected member index, or -1 for none
		//! @brief allow deselecting the current member by tapping it again (a
		//! plain radio group keeps exactly one selected once chosen; a filter
		//! group may allow the empty state)
		bool	allowNone = false;

		//! @brief force the selection to @p index (clamped to a valid member;
		//! -1 clears it when allowNone). @return true when the selection changed.
		bool select(int index);

		//! @brief a member reports it was tapped. Selects it (clearing the
		//! others); tapping the already-selected member clears it only when
		//! allowNone. @return the resulting selected index.
		int onMemberTapped(int index);

		//! @brief is @p index the selected member?
		bool isSelected(int index) const { return index == this->selected; }
	};
}

#endif //__ToggleGroupState_h__11_7_2026__18_00_00__
