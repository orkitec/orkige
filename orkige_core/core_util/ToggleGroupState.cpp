/********************************************************************
	created:	Saturday 2026/07/11 at 18:00
	filename: 	ToggleGroupState.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure single-selection state machine (@see ToggleGroupState.h)
*********************************************************************/

#include "core_util/ToggleGroupState.h"

namespace Orkige
{
	//---------------------------------------------------------
	bool ToggleGroupState::select(int index)
	{
		int clamped = index;
		if(clamped < 0 || clamped >= this->count)
		{
			// -1 (or any out-of-range) means "no selection"; only honoured when
			// the group allows the empty state, else the request is ignored
			if(!this->allowNone)
			{
				return false;
			}
			clamped = -1;
		}
		if(clamped == this->selected)
		{
			return false;
		}
		this->selected = clamped;
		return true;
	}
	//---------------------------------------------------------
	int ToggleGroupState::onMemberTapped(int index)
	{
		if(index < 0 || index >= this->count)
		{
			return this->selected;	// a tap outside the group changes nothing
		}
		if(index == this->selected)
		{
			// re-tapping the selected member clears it only in an allow-none
			// group; a plain radio group keeps it selected
			if(this->allowNone)
			{
				this->selected = -1;
			}
		}
		else
		{
			this->selected = index;
		}
		return this->selected;
	}
}
